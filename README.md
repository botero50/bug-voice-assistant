# 🎤 Bug Voice Assistant - ESP32-S3 AMOLED

**Spanish voice assistant for the Waveshare ESP32-S3-Touch-AMOLED-1.75 round display.**

**[Install the firmware from your browser](https://botero50.github.io/bug-voice-assistant/)** (desktop Chrome or Edge)

- **Status:** 🚧 Preview. The firmware compiles but hasn't been tested on a board. Listening, answers, touch and the connection to the server aren't working on the device yet, so some features below are still planned.
- **Code:** 8,829 lines (firmware + backend)
- **Services:** 19 Docker containers
- **Skills:** 15+ (entertainment + productivity)
- **Battery Life:** 8-14 days (Always-Listening mode)
- **Languages:** Spanish (primary) + English

---

## ✨ Features

### Voice System
- ✅ Wake word detection ("Hey Bug")
- ✅ Speech-to-text (Spanish/English, faster-whisper)
- ✅ Language understanding (Claude API)
- ✅ Speech synthesis (Piper TTS, bilingual)
- ✅ Multi-turn conversation memory
- ✅ Multi-user support (speaker identification)

### Entertainment (15+ Skills)
- **Games:** Akinator, Tic-Tac-Toe, 2048
- **Info:** Weather, Crypto, Radio
- **Content:** Stories, Books, Jokes
- **Games:** Trivia, Riddles, Word games

### Productivity
- **Email:** Gmail + Outlook
- **Calendar:** Merged calendar view
- **Memory:** Conversation history per user

### Smart Home
- **Devices:** Lights, thermostat, locks, speakers
- **Automations:** Good Night, Leave Home
- **Scenes:** Morning, Night, Movie time

### Power (8-14 days battery)
- **Always-Listening:** 10mA average
- **Periodic:** 2.5mA average
- **Gesture-Only:** 0.5mA average

---

## 🚀 Quick Start

### 1. Start Backend Services

```bash
cd "voice assitance"

# Build and run all services
docker-compose build
docker-compose up

# Test in another terminal:
curl http://localhost:8080        # Web UI
curl http://localhost:5000/health # Orchestrator
curl http://localhost:5101/emails # Gmail mock
```

### 2. Build Firmware (Optional Now)

```bash
# Install ESP-IDF 5.1
git clone https://github.com/espressif/esp-idf.git --branch v5.1
cd esp-idf && ./install.sh esp32s3

# Build
cd firmware && idf.py build

# Output: build/firmware.bin
```

### 3. When Hardware Arrives

```bash
# Prepare MicroSD (see HARDWARE.md)
# Download models to microsd_files/models/

# Flash firmware
idf.py flash -p /dev/ttyUSB0

# Monitor boot
idf.py monitor
```

---

## 🛠️ Hardware You Need

### Required
- **ESP32-S3 1.75" AMOLED Round** - [Buy Here](https://www.amazon.com/ESP32-S3-Development-Interface-Microphone-Interaction/dp/B0F8867487/)
  - Dual microphone array ✓
  - 2W speaker built-in ✓
  - Round AMOLED display ✓
  - WiFi + Bluetooth 5.0 ✓

- **MicroSD Card 128GB+** (~$15-25)
- **5V 2A Power Supply** (~$10-15)
- **USB-C Cable** (~$3-5)

### Optional
- **SIM7670G 4G Module** (~$25-40) - for cellular fallback
- **SIM Card 4G** (~$1-5)

### You Already Have
- ✅ GPS Module (u-blox MAX-M8Q)

**Total:** $83-118 (without 4G), ~$108-158 (with 4G)

---

## 📋 Repository Structure

```
voice-assistant/
├── README.md                ← You are here
├── HARDWARE.md              ← Setup instructions
├── docker-compose.yml       ← 12 backend services
├── .env.example             ← Config template
│
├── services/                ← Python/Flask backend
│   ├── stt/                 ← Speech-to-text
│   ├── llm/                 ← Language model
│   ├── tts/                 ← Text-to-speech
│   ├── orchestrator/        ← Coordinator
│   ├── gmail/               ← Gmail integration
│   ├── outlook/             ← Outlook integration
│   ├── calendar_merger/     ← Calendar sync
│   ├── email_composer/      ← Email drafting
│   ├── wake_word/           ← Wake word detection
│   ├── power_optimizer/     ← Power simulation
│   ├── speaker_identification/ ← User ID
│   ├── model_optimizer/     ← Model optimization
│   └── web/                 ← Web UI
│
├── firmware/                ← C code for ESP32-S3
│   ├── main/
│   │   ├── main.c           ← Boot sequence
│   │   ├── voice_loop.c     ← Audio pipeline
│   │   ├── microsd_manager.c
│   │   ├── power_manager.c
│   │   ├── audio_driver.c
│   │   ├── network_manager.c
│   │   ├── sim7670g_driver.c ← 4G module
│   │   ├── cellular_manager.c
│   │   ├── ppp_client.c
│   │   ├── http_cellular_client.c
│   │   ├── wake_word_detector.c
│   │   ├── speaker_identification.c
│   │   └── include/         ← Headers
│   └── CMakeLists.txt
│
├── microsd_files/           ← Prepare for device
│   ├── models/
│   │   ├── whisper_base.onnx (download)
│   │   └── piper_es_CO.onnx (download)
│   ├── configs/
│   │   ├── device_config.json
│   │   ├── voice_params.json
│   │   └── power_profile.json
│   └── data/
│
└── config/, logs/           ← Runtime
```

---

## 🔧 Configuration

Copy template and edit:

```bash
cp .env.example .env
nano .env
```

Key settings:
- `MOCK_MODE=true` - Use mock services (no API key needed)
- `ANTHROPIC_API_KEY=` - When ready, add your Claude API key
- `GMAIL_CLIENT_ID=` - Optional: Gmail OAuth
- `OUTLOOK_CLIENT_ID=` - Optional: Outlook OAuth

---

## 📥 Installation

### Prerequisites

```bash
# macOS
brew install docker python@3.11

# Linux (Ubuntu)
sudo apt install docker.io python3.11

# Windows
# Download Docker Desktop
```

### Start Services

```bash
cd "voice assitance"

# First time
docker-compose build

# Start
docker-compose up

# Or background
docker-compose up -d
```

### Test

```bash
# Web UI
http://localhost:8080

# Type Spanish: "Hola, ¿cómo estás?"
# Should get audio response with mock data
```

---

## 🔑 When You Have API Keys

### Anthropic Claude
```bash
1. Get key from: https://console.anthropic.com
2. Add to .env: ANTHROPIC_API_KEY=sk-ant-...
3. Restart: docker-compose restart llm
```

### Gmail OAuth (Optional)
```bash
1. Google Cloud Console
2. Create OAuth 2.0 credentials
3. Add to .env: GMAIL_CLIENT_ID, GMAIL_CLIENT_SECRET
4. Redirect URI: http://localhost:5101/auth/callback
```

### Outlook OAuth (Optional)
```bash
1. Azure AD Console
2. Register application
3. Add to .env: OUTLOOK_CLIENT_ID, OUTLOOK_CLIENT_SECRET
4. Redirect URI: http://localhost:5102/auth/callback
```

---

## 📥 When Hardware Arrives

### 1. Download Models (~200 MB)

```bash
# Download from GitHub:
# whisper_base.onnx (~150 MB)
#   → https://github.com/openai/whisper/releases
# piper_es_CO.onnx (~50 MB)
#   → https://github.com/rhasspy/piper

# Place in:
microsd_files/models/
```

### 2. Prepare MicroSD Card

```bash
# Format as FAT32
# Copy entire microsd_files/ contents to card root
# Insert into device
```

### 3. Flash Firmware

```bash
cd firmware

# Build (first time)
idf.py build

# Flash to device
idf.py flash -p /dev/ttyUSB0  # macOS/Linux
idf.py flash -p COM3          # Windows

# Monitor boot
idf.py monitor
```

### 4. Test on Device

- Power on
- Say: "Hola Bug" (when Phase 4 wake word enabled)
- Check serial monitor for logs
- Verify audio in/out
- Test battery consumption

---

## 🎤 Services & Ports

| Service | Port | Purpose |
|---------|------|---------|
| Web UI | 8080 | Browser interface |
| Orchestrator | 5000 | Coordinator |
| STT | 5001 | Speech-to-text |
| LLM | 5002 | Language model |
| TTS | 5003 | Text-to-speech |
| Gmail | 5101 | Email integration |
| Outlook | 5102 | Email integration |
| Calendar Merger | 5103 | Calendar sync |
| Email Composer | 5104 | Email drafting |
| Wake Word | 5105 | Wake detection |
| Power Optimizer | 5108 | Power simulation |
| Speaker ID | 5109 | User identification |
| Model Optimizer | 5110 | Model compression |

---

## 🔋 Power Estimates

| Mode | Current | Battery Life |
|------|---------|--------------|
| Idle (PSM) | 1-5 mA | 100+ days |
| WiFi idle | 10-20 mA | 6-12 days |
| Voice processing | 50-100 mA | — |
| 4G transfer | 300-500 mA | — |
| Peak | 500-1000 mA | — |

Based on 2000 mAh battery

---

## ✅ What's Complete

- [x] 12 backend services (Docker)
- [x] ~2,500 lines C firmware
- [x] ~2,000 lines Python services
- [x] Voice loop (STT→LLM→TTS)
- [x] Email & Calendar integration
- [x] Wake word detection
- [x] Speaker identification
- [x] Power optimization
- [x] 4G cellular support
- [x] Mock mode everywhere (test without API keys)

---

## 🚧 Future

| Phase | Status |
|-------|--------|
| Wake word "Hola Bug" | ✅ Built |
| Power profiles | ✅ Built |
| Speaker ID | ✅ Built |
| Model optimization | ✅ Built |
| Display UI | 🟡 Designed |
| SMS/Calls | 🔜 Not started |
| GPS | 🔜 Not started |
| Smart home | 🔜 Not started |
| OTA updates | 🔜 Not started |

---

## 🐛 Troubleshooting

### Services won't start
```bash
docker-compose logs
docker-compose down && docker-compose up --build
```

### Port in use
```bash
lsof -i :8080  # Check what's using port
docker-compose restart web
```

### Firmware build fails
```bash
source ~/esp-idf/export.sh
cd firmware && idf.py fullclean && idf.py build
```

### No LLM response
```bash
# Check MOCK_MODE enabled
grep MOCK_MODE .env

# If real API: export ANTHROPIC_API_KEY=sk-ant-...
docker-compose restart llm
```

---

## 📞 Support

Check logs first:
```bash
docker-compose logs orchestrator
docker-compose logs llm
docker-compose logs wake_word
```

---

**Status:** ✅ Ready to deploy  
**Date:** November 2026  
**Maintainer:** You (local repository)
