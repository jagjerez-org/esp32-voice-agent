#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include "config.h"

// ── State Machine ─────────────────────────────────────
enum State {
  STATE_IDLE,
  STATE_LISTENING,
  STATE_SENDING,
  STATE_PLAYING
};

State currentState = STATE_IDLE;

// ── Audio Buffers (PSRAM) ─────────────────────────────
uint8_t* audioRecordBuffer = nullptr;
uint8_t* audioPlayBuffer = nullptr;
size_t recordedBytes = 0;
size_t playBufferSize = 0;

// ── Forward declarations ──────────────────────────────
void setupWiFi();
void setupI2SMic();
void setupI2SSpk();
void startRecording();
void stopRecording();
bool detectSilence(int16_t* samples, size_t count);
void sendAudioToServer();
void playAudioFromServer(uint8_t* data, size_t len);
void stopI2S(i2s_port_t port);

// ══════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🎤 ESP32 Voice Agent starting...");

  // Use PSRAM for audio buffers
  audioRecordBuffer = (uint8_t*)ps_malloc(I2S_MIC_SAMPLE_RATE * 2 * (RECORD_DURATION_MS / 1000 + 1));
  audioPlayBuffer = (uint8_t*)ps_malloc(512 * 1024);  // 512KB play buffer

  if (!audioRecordBuffer || !audioPlayBuffer) {
    Serial.println("❌ PSRAM allocation failed!");
    while (true) delay(1000);
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  setupWiFi();

  Serial.println("✅ Ready! Press BOOT button to talk.");
}

void loop() {
  switch (currentState) {
    case STATE_IDLE: {
      // Push-to-talk: BOOT button (active LOW)
      if (digitalRead(BUTTON_PIN) == LOW) {
        delay(50);  // debounce
        if (digitalRead(BUTTON_PIN) == LOW) {
          Serial.println("🎙️ Listening...");
          currentState = STATE_LISTENING;
          setupI2SMic();
          recordedBytes = 0;
        }
      }
      break;
    }

    case STATE_LISTENING: {
      // Record audio chunks
      int16_t samples[AUDIO_BUFFER_SIZE / 2];
      size_t bytesRead = 0;

      i2s_read(I2S_MIC_PORT, samples, AUDIO_BUFFER_SIZE, &bytesRead, portMAX_DELAY);

      if (bytesRead > 0 && recordedBytes + bytesRead < I2S_MIC_SAMPLE_RATE * 2 * (RECORD_DURATION_MS / 1000)) {
        memcpy(audioRecordBuffer + recordedBytes, samples, bytesRead);
        recordedBytes += bytesRead;
      }

      // Stop on button release or max duration or silence
      bool buttonReleased = digitalRead(BUTTON_PIN) == HIGH;
      bool maxDuration = recordedBytes >= (size_t)(I2S_MIC_SAMPLE_RATE * 2 * (RECORD_DURATION_MS / 1000));
      bool silence = detectSilence(samples, bytesRead / 2);

      if (buttonReleased || maxDuration) {
        Serial.printf("⏹️ Recorded %d bytes\n", recordedBytes);
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
      // Playback handled in sendAudioToServer callback
      // Return to idle after playback
      currentState = STATE_IDLE;
      Serial.println("✅ Ready! Press BOOT button to talk.");
      break;
    }
  }
}

// ── WiFi Setup ────────────────────────────────────────
void setupWiFi() {
  Serial.printf("📶 Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);  // Keep WiFi active for low latency

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

// ── Silence Detection ─────────────────────────────────
bool detectSilence(int16_t* samples, size_t count) {
  if (count == 0) return false;

  int64_t sum = 0;
  for (size_t i = 0; i < count; i++) {
    sum += abs(samples[i]);
  }
  int16_t avg = sum / count;
  return avg < SILENCE_THRESHOLD;
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
    currentState = STATE_IDLE;
    return;
  }

  HTTPClient http;
  String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/conversation";
  http.begin(url);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Sample-Rate", String(I2S_MIC_SAMPLE_RATE));
  http.addHeader("X-Bit-Depth", String(I2S_MIC_SAMPLE_BITS));
  http.setTimeout(30000);  // 30s timeout for LLM response

  int httpCode = http.POST(audioRecordBuffer, recordedBytes);

  if (httpCode == 200) {
    // Response is raw PCM audio
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
  // Flush with silence
  uint8_t silence[1024] = {0};
  size_t written;
  i2s_write(I2S_SPK_PORT, silence, sizeof(silence), &written, portMAX_DELAY);
}
