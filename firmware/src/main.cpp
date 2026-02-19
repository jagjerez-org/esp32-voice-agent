#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include "config.h"

// ── State Machine ─────────────────────────────────────
enum State {
  STATE_LISTENING,    // Always listening, waiting for speech
  STATE_RECORDING,    // Speech detected, recording
  STATE_SENDING,      // Sending audio to server
  STATE_PLAYING       // Playing response
};

State currentState = STATE_LISTENING;

// ── Audio Buffers (PSRAM) ─────────────────────────────
uint8_t* audioRecordBuffer = nullptr;
uint8_t* audioPlayBuffer = nullptr;
size_t recordedBytes = 0;
size_t playBufferSize = 0;

// ── VAD State ─────────────────────────────────────────
unsigned long speechStartTime = 0;
unsigned long lastSpeechTime = 0;
bool speechActive = false;

// ── Forward declarations ──────────────────────────────
void setupWiFi();
void setupI2SMic();
void setupI2SSpk();
void stopI2S(i2s_port_t port);
void sendAudioToServer();
void playAudioFromServer(uint8_t* data, size_t len);
int16_t calculateRMS(int16_t* samples, size_t count);
void setLED(bool on);

// ══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🎤 ESP32 Voice Agent (VAD mode) starting...");

  // Use PSRAM for audio buffers
  size_t recordBufSize = I2S_MIC_SAMPLE_RATE * 2 * (RECORD_DURATION_MS / 1000 + 1);
  audioRecordBuffer = (uint8_t*)ps_malloc(recordBufSize);
  audioPlayBuffer = (uint8_t*)ps_malloc(512 * 1024);

  if (!audioRecordBuffer || !audioPlayBuffer) {
    Serial.println("❌ PSRAM allocation failed!");
    while (true) delay(1000);
  }

  pinMode(LED_PIN, OUTPUT);
  setLED(false);

  setupWiFi();
  setupI2SMic();

  Serial.println("✅ Ready! Listening for speech...");
}

void loop() {
  switch (currentState) {
    case STATE_LISTENING: {
      // Continuously read mic and check for speech
      int16_t samples[AUDIO_BUFFER_SIZE / 2];
      size_t bytesRead = 0;
      i2s_read(I2S_MIC_PORT, samples, AUDIO_BUFFER_SIZE, &bytesRead, portMAX_DELAY);

      int16_t rms = calculateRMS(samples, bytesRead / 2);

      if (rms > VAD_SPEECH_THRESHOLD) {
        // Speech detected! Start recording
        Serial.printf("🎙️ Speech detected (RMS: %d) — recording...\n", rms);
        setLED(true);
        currentState = STATE_RECORDING;
        recordedBytes = 0;
        speechStartTime = millis();
        lastSpeechTime = millis();
        speechActive = true;

        // Copy this first chunk
        if (bytesRead > 0) {
          memcpy(audioRecordBuffer, samples, bytesRead);
          recordedBytes = bytesRead;
        }
      }
      break;
    }

    case STATE_RECORDING: {
      int16_t samples[AUDIO_BUFFER_SIZE / 2];
      size_t bytesRead = 0;
      i2s_read(I2S_MIC_PORT, samples, AUDIO_BUFFER_SIZE, &bytesRead, portMAX_DELAY);

      // Store audio
      size_t maxBytes = I2S_MIC_SAMPLE_RATE * 2 * (RECORD_DURATION_MS / 1000);
      if (bytesRead > 0 && recordedBytes + bytesRead < maxBytes) {
        memcpy(audioRecordBuffer + recordedBytes, samples, bytesRead);
        recordedBytes += bytesRead;
      }

      // VAD: check if speech is ongoing
      int16_t rms = calculateRMS(samples, bytesRead / 2);

      if (rms > VAD_SILENCE_THRESHOLD) {
        lastSpeechTime = millis();
      }

      unsigned long now = millis();
      bool maxDuration = (now - speechStartTime) >= RECORD_DURATION_MS;
      bool silenceTimeout = (now - lastSpeechTime) >= VAD_SILENCE_END_MS;
      bool minSpeech = (now - speechStartTime) >= VAD_SPEECH_MIN_MS;

      if ((silenceTimeout && minSpeech) || maxDuration) {
        Serial.printf("⏹️ Recording done: %d bytes, %lu ms\n",
          recordedBytes, now - speechStartTime);
        setLED(false);

        // Stop mic before sending
        stopI2S(I2S_MIC_PORT);
        currentState = STATE_SENDING;
      }
      break;
    }

    case STATE_SENDING: {
      Serial.println("📡 Sending to server...");
      sendAudioToServer();
      break;
    }

    case STATE_PLAYING: {
      // Re-initialize mic and go back to listening
      setupI2SMic();
      currentState = STATE_LISTENING;
      Serial.println("✅ Listening for speech...");
      break;
    }
  }
}

// ── RMS Calculation ───────────────────────────────────
int16_t calculateRMS(int16_t* samples, size_t count) {
  if (count == 0) return 0;
  int64_t sumSquares = 0;
  for (size_t i = 0; i < count; i++) {
    sumSquares += (int64_t)samples[i] * samples[i];
  }
  return (int16_t)sqrt((double)sumSquares / count);
}

// ── LED Control ───────────────────────────────────────
void setLED(bool on) {
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

// ── WiFi Setup ────────────────────────────────────────
void setupWiFi() {
  Serial.printf("📶 Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n❌ WiFi failed! Restarting...");
    ESP.restart();
  }
}

// ── I2S Microphone Setup ──────────────────────────────
void setupI2SMic() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_MIC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_MIC_PORT);
}

// ── I2S Speaker Setup ─────────────────────────────────
void setupI2SSpk() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = I2S_SPK_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SPK_BCLK,
    .ws_io_num = I2S_SPK_LRC,
    .data_out_num = I2S_SPK_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_SPK_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_SPK_PORT);
}

// ── Stop I2S ──────────────────────────────────────────
void stopI2S(i2s_port_t port) {
  i2s_stop(port);
  i2s_driver_uninstall(port);
}

// ── Send Audio & Receive Response ─────────────────────
void sendAudioToServer() {
  if (recordedBytes == 0) {
    Serial.println("⚠️ No audio recorded");
    currentState = STATE_PLAYING;
    return;
  }

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/conversation";
  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Sample-Rate", String(I2S_MIC_SAMPLE_RATE));
  http.addHeader("X-Bit-Depth", String(I2S_MIC_SAMPLE_BITS));
  http.setTimeout(30000);

  int httpCode = http.POST(audioRecordBuffer, recordedBytes);

  if (httpCode == 200) {
    int len = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    if (len > 0 && len < 512 * 1024) {
      size_t totalRead = 0;
      while (totalRead < (size_t)len) {
        size_t available = stream->available();
        if (available) {
          size_t toRead = min(available, (size_t)(len - totalRead));
          stream->readBytes(audioPlayBuffer + totalRead, toRead);
          totalRead += toRead;
        }
        delay(1);
      }
      playBufferSize = totalRead;

      Serial.printf("🔊 Playing %d bytes...\n", playBufferSize);
      setupI2SSpk();
      playAudioFromServer(audioPlayBuffer, playBufferSize);
      stopI2S(I2S_SPK_PORT);
    }
  } else {
    Serial.printf("❌ HTTP error: %d\n", httpCode);
  }

  http.end();
  currentState = STATE_PLAYING;
}

// ── Play Audio ────────────────────────────────────────
void playAudioFromServer(uint8_t* data, size_t len) {
  size_t offset = 0;
  while (offset < len) {
    size_t bytesWritten = 0;
    size_t toWrite = min((size_t)AUDIO_BUFFER_SIZE, len - offset);
    i2s_write(I2S_SPK_PORT, data + offset, toWrite, &bytesWritten, portMAX_DELAY);
    offset += bytesWritten;
  }
  uint8_t silence[1024] = {0};
  size_t written;
  i2s_write(I2S_SPK_PORT, silence, sizeof(silence), &written, portMAX_DELAY);
}
