#pragma once

// ── WiFi ──────────────────────────────────────────────
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// ── Server ────────────────────────────────────────────
#define SERVER_HOST   "192.168.1.100"  // MCP server IP
#define SERVER_PORT   3001

// ── I2S Microphone (INMP441) ──────────────────────────
#define I2S_MIC_PORT       I2S_NUM_0
#define I2S_MIC_SCK        17   // Serial Clock
#define I2S_MIC_WS         18   // Word Select
#define I2S_MIC_SD         16   // Serial Data
#define I2S_MIC_SAMPLE_RATE  16000
#define I2S_MIC_SAMPLE_BITS  16

// ── I2S Speaker (MAX98357A) ───────────────────────────
#define I2S_SPK_PORT       I2S_NUM_1
#define I2S_SPK_BCLK       9    // Bit Clock
#define I2S_SPK_LRC        10   // Left/Right Clock
#define I2S_SPK_DIN        8    // Data In
#define I2S_SPK_SAMPLE_RATE  16000
#define I2S_SPK_SAMPLE_BITS  16

// ── Audio / VAD ───────────────────────────────────────
#define RECORD_DURATION_MS   10000   // Max recording per chunk
#define VAD_SPEECH_THRESHOLD 800     // Amplitude to detect speech start
#define VAD_SILENCE_THRESHOLD 400    // Amplitude to detect speech end
#define VAD_SPEECH_MIN_MS    300     // Min speech duration to accept
#define VAD_SILENCE_END_MS   1500    // Silence after speech to stop recording
#define VAD_WINDOW_MS        50      // VAD analysis window
#define AUDIO_BUFFER_SIZE    1024    // I2S read buffer size

// ── LED Feedback ──────────────────────────────────────
#define LED_PIN        2    // Built-in LED (or RGB via neopixel)
