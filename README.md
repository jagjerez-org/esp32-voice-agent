# ESP32 Voice Agent 🎤🔊

Conversational AI device using ESP32-S3 with I2S microphone and speaker, controlled via MCP (Model Context Protocol) server.

## Architecture

```
User speaks → ESP32 (INMP441 mic) → WiFi → MCP Server → Whisper (STT)
    → Claude/LLM → TTS → MCP Server → WiFi → ESP32 (MAX98357A) → Speaker
```

## Hardware

| Component | Model | Purpose |
|-----------|-------|---------|
| MCU | ESP32-S3-WROOM-1 N16R8 | Main controller |
| Microphone | INMP441 | I2S digital mic input |
| Amplifier | MAX98357A | I2S audio output |
| Speaker | 3W 4Ω | Audio playback |

## Wiring

### INMP441 → ESP32-S3
| INMP441 | ESP32-S3 | Notes |
|---------|----------|-------|
| VDD | 3V3 | Power |
| GND | GND | Ground |
| SD | GPIO 16 | Serial Data |
| SCK | GPIO 17 | Serial Clock |
| WS | GPIO 18 | Word Select |
| L/R | GND | Left channel |

### MAX98357A → ESP32-S3
| MAX98357A | ESP32-S3 | Notes |
|-----------|----------|-------|
| VIN | 5V (USB) | Power |
| GND | GND | Ground |
| DIN | GPIO 8 | Data In |
| BCLK | GPIO 9 | Bit Clock |
| LRC | GPIO 10 | Left/Right Clock |
| GAIN | - | Float = 9dB |

## Project Structure

```
├── firmware/          # ESP32 Arduino/PlatformIO firmware
│   ├── src/
│   │   └── main.cpp
│   ├── include/
│   │   └── config.h
│   └── platformio.ini
├── server/            # MCP server (Node.js/TypeScript)
│   ├── src/
│   │   └── index.ts
│   ├── package.json
│   └── tsconfig.json
└── README.md
```

## Setup

### Firmware
```bash
cd firmware
pio run --target upload
```

### Server
```bash
cd server
npm install
npm run build
npm start
```

## License

MIT
