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

// ── Audio ─────────────────────────────────────────────
#define RECORD_DURATION_MS   5000    // Max recording per chunk
#define SILENCE_THRESHOLD    500     // Silence detection threshold
#define SILENCE_DURATION_MS  1500    // Stop after this much silence
#define AUDIO_BUFFER_SIZE    1024    // I2S read buffer size

// ── Button ────────────────────────────────────────────
#define BUTTON_PIN     0   // BOOT button for push-to-talk
