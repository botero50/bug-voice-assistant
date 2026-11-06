# Pre-Deployment Preparation Guide
## Before Hardware Arrives (1-2 weeks)

**Status:** ✅ DO THIS NOW (before device arrives)  
**Time:** ~2-3 hours  
**Impact:** Save 2-4 hours during actual hardware deployment

---

## 1. 🔑 API Key Setup

### Claude API Key
1. Go to https://console.anthropic.com
2. Sign up or log in
3. Create new API key in "API Keys" section
4. Copy key and save securely
5. **IMPORTANT:** Keep this private, never commit to git

**Store in:** `.env` file (never in code)
```bash
CLAUDE_API_KEY=sk-ant-xxxxx
```

### Gmail OAuth2 Setup (Optional, for email features)
1. Go to https://console.cloud.google.com
2. Create new project (name: "Bug Voice Assistant")
3. Enable "Gmail API"
4. Create OAuth2 credentials (Desktop application)
5. Download credentials JSON
6. Note: `client_id` and `client_secret`

**Store in:** `.env` file
```bash
GMAIL_CLIENT_ID=xxxxx.apps.googleusercontent.com
GMAIL_CLIENT_SECRET=xxxxx
```

### Outlook OAuth2 Setup (Optional)
1. Go to https://portal.azure.com
2. Register application in "App registrations"
3. Create client secret
4. Note: `client_id` and `client_secret`

**Store in:** `.env` file
```bash
OUTLOOK_CLIENT_ID=xxxxx
OUTLOOK_CLIENT_SECRET=xxxxx
```

---

## 2. ⚙️ Configuration Files Setup

### Copy Template Files
```bash
cd /Users/alejandrobotero/voice\ assitance

# Copy config template
cp config/device_config.template.json config/device_config.json

# Copy .env template
cp .env.template .env
```

### Edit Configuration Files

**`config/device_config.json`** - Set these values:
```json
{
  "device": {
    "location": "Bogotá, Colombia",
    "primary_language": "es-ES",
    "secondary_language": "en-US"
  },
  "apis": {
    "claude": {
      "api_key": "sk-your-key-here"
    }
  },
  "network": {
    "wifi_ssid": "Your WiFi SSID",
    "wifi_password": "Your WiFi Password"
  }
}
```

**`.env`** - Update these:
```bash
CLAUDE_API_KEY=sk-ant-xxxxx
WIFI_SSID=your-wifi-name
WIFI_PASSWORD=your-wifi-password
```

**Important:** Never commit `.env` or actual `device_config.json` to git!

---

## 3. 🐳 Docker Services Verification

### Test Docker Build
```bash
cd /Users/alejandrobotero/voice\ assitance

# Build all services
docker-compose build

# Should complete without errors
# This verifies all Dockerfiles are correct
```

### Test Services Startup
```bash
# Start services in background
docker-compose up -d

# Check health
docker-compose ps

# All services should show "healthy" or "up"
```

### Test Individual Services
```bash
# Test STT service
curl http://localhost:5001/health

# Test LLM service
curl http://localhost:5002/health

# Test TTS service
curl http://localhost:5003/health

# Test Orchestrator
curl http://localhost:5000/health

# All should return: {"status": "healthy"}
```

### Cleanup
```bash
# Stop all services
docker-compose down

# Free disk space
docker system prune -y
```

---

## 4. 📦 Model Download Guide

### Prepare MicroSD Card

**Requirements:**
- MicroSD card (128GB or larger recommended)
- MicroSD card reader
- Computer with fast internet

**Format MicroSD:**
```bash
# macOS:
diskutil secureErase freespace 0 /dev/diskX

# Linux:
sudo mkfs.ext4 /dev/sdX1

# Windows:
# Use Windows Disk Management or File Explorer
```

### Download Models

These need to be on the MicroSD card before flashing device:

**1. Faster-Whisper (~1.4 GB)**
```bash
# Download Whisper base model
# Python script to download (run on your computer)

python3 << 'PYTHON'
from faster_whisper import WhisperModel

# Download base model (good quality/speed balance)
model = WhisperModel("base", device="cpu", compute_type="int8")
# Files saved to ~/.cache/huggingface/hub/

print("✅ Model downloaded successfully")
PYTHON
```

**Copy to MicroSD:**
```bash
# Location: ~/.cache/huggingface/hub/models--Systran--faster-whisper-base/
# Copy entire folder to MicroSD:/models/whisper/
```

**2. Piper TTS (~300 MB)**
```bash
# Download Spanish models
mkdir -p /path/to/microsd/models/piper

# Spanish Spain
curl -L https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_ES/carme/medium/es_ES-carme-medium.onnx \
  -o /path/to/microsd/models/piper/es_ES-carme-medium.onnx

curl -L https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_ES/carme/medium/es_ES-carme-medium.onnx.json \
  -o /path/to/microsd/models/piper/es_ES-carme-medium.onnx.json
```

**3. English TTS (~150 MB)**
```bash
# Download English model (for secondary language)
curl -L https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx \
  -o /path/to/microsd/models/piper/en_US-lessac-medium.onnx

curl -L https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json \
  -o /path/to/microsd/models/piper/en_US-lessac-medium.onnx.json
```

**4. BERT Embeddings for Speaker ID (~400 MB)**
```bash
# Download sentence embeddings model
mkdir -p /path/to/microsd/models/embeddings

python3 << 'PYTHON'
from sentence_transformers import SentenceTransformer

# Download multilingual model
model = SentenceTransformer('sentence-transformers/xlm-r-base-multilingual-semantic-similarity')

# Will be saved to ~/.cache/huggingface/hub/

print("✅ Embeddings model downloaded")
PYTHON

# Copy to MicroSD
```

### Final MicroSD Structure
```
/microsd/
├── models/
│   ├── whisper/
│   │   └── [faster-whisper files]
│   ├── piper/
│   │   ├── es_ES-carme-medium.onnx
│   │   ├── es_ES-carme-medium.onnx.json
│   │   ├── en_US-lessac-medium.onnx
│   │   └── en_US-lessac-medium.onnx.json
│   └── embeddings/
│       └── [BERT model files]
├── config/
│   └── device_config.json
└── logs/
```

---

## 5. 🔌 Hardware Preparation Checklist

### What You'll Need When Device Arrives
- [ ] USB-C cable (for flashing)
- [ ] Micro USB power adapter (5V, 1A minimum)
- [ ] Formatted MicroSD card (with models)
- [ ] Internet connection (WiFi)
- [ ] Computer with ESP-IDF installed
- [ ] Multimeter (for power measurements)

### Prepare Development Machine

**Install ESP-IDF:**
```bash
# macOS
brew install esp-idf

# Or from source (recommended for latest version)
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh

# Add to .zshrc or .bashrc
export IDF_PATH=~/esp/esp-idf
alias get_idf='. $IDF_PATH/export.sh'
```

**Install Required Tools:**
```bash
# Python dependencies for flashing
pip install esptool pyserial

# Verify installation
esptool.py version
```

**USB Driver (macOS/Linux):**
- Usually automatic on macOS
- Linux: `sudo apt install python3-serial`

---

## 6. 📋 Pre-Flashing Checklist

Before plugging in the device:

- [ ] `.env` file created with WiFi credentials and API keys
- [ ] `config/device_config.json` updated with your location
- [ ] MicroSD card prepared with all models
- [ ] MicroSD card inserted into device
- [ ] Docker services tested locally
- [ ] ESP-IDF installed and working
- [ ] USB cable connected and recognized
- [ ] Device in bootloader mode (hold BOOT button while plugging in)

---

## 7. 🚀 Quick Test of Services (Without Hardware)

You can test the voice pipeline locally:

```bash
# Terminal 1: Start Docker services
docker-compose up -d

# Terminal 2: Test voice processing
python3 << 'PYTHON'
import requests
import json

# Test STT
stt_data = {"audio_file": "test.wav"}
# response = requests.post("http://localhost:5001/transcribe", json=stt_data)

# Test LLM
llm_data = {"text": "¿Hola, cómo estás?"}
response = requests.post("http://localhost:5002/chat", json=llm_data)
print("LLM Response:", response.json())

# Test all services are healthy
services = [
    "http://localhost:5001/health",  # STT
    "http://localhost:5002/health",  # LLM
    "http://localhost:5003/health",  # TTS
    "http://localhost:5000/health",  # Orchestrator
]

for service in services:
    r = requests.get(service)
    print(f"{service}: {r.json()['status']}")

PYTHON
```

---

## 8. 📊 Performance Baseline (Before Hardware)

Create a baseline of expected performance:

```bash
# Test LLM response time
time curl -X POST http://localhost:5002/chat \
  -H "Content-Type: application/json" \
  -d '{"text": "Hola"}'

# Test TTS response time
time curl -X POST http://localhost:5003/speak \
  -H "Content-Type: application/json" \
  -d '{"text": "Hola, como estás"}'

# Test orchestrator end-to-end
time curl -X POST http://localhost:5000/process \
  -H "Content-Type: application/json" \
  -d '{"text": "¿Qué hora es?"}'
```

Record these times - they'll be baseline for comparison on device.

---

## 9. 🧪 Battery Measurement Prep

When device arrives, you'll want to measure power:

**Get a Multimeter:**
- Recommended: Fluke 115 or similar (~$100)
- Budget option: UT61A (~$30)
- Skills: Measure DC current, 0-2A range

**Measurement Setup:**
```
+5V ────┬────► Device
        │
    Multimeter (A mode)
        │
   GND ─┴────► Device GND
```

**What to measure:**
- Always-Listening mode: Should be 8-12mA
- Periodic mode: Should be 2-3mA
- Gesture-Only mode: Should be 0.5-1mA

**Tracking spreadsheet:**
```
Time (min) | Profile | Current (mA) | Notes
0          | Always  | 10.2         | Initial boot
5          | Always  | 9.8          | Voice ready
10         | Periodic| 2.5          | Periodic mode
15         | Gesture | 0.6          | Ultra-low power
```

---

## 10. 📝 Troubleshooting Prep

Common issues and solutions (prepared in advance):

**"Device not found" when flashing:**
- Solution: Check USB cable, install CH341 driver (Windows)

**"Unknown board configuration":**
- Solution: Verify board is ESP32-S3, not ESP32

**"Models not found on MicroSD":**
- Solution: Verify folder structure, check MicroSD mount

**"WiFi won't connect":**
- Solution: Verify SSID/password in config, check 2.4GHz (not 5GHz)

**"API key rejected":**
- Solution: Verify key in `.env`, check Claude API console

---

## Summary

**Complete These 10 Tasks Now:**
1. ✅ Get Claude API key
2. ✅ Set up Gmail/Outlook (optional)
3. ✅ Create `.env` and `device_config.json`
4. ✅ Test Docker services locally
5. ✅ Prepare and format MicroSD card
6. ✅ Download all models to MicroSD
7. ✅ Install ESP-IDF
8. ✅ Get USB cable + multimeter ready
9. ✅ Create measurement tracking spreadsheet
10. ✅ Read troubleshooting guide

**When Device Arrives (30 min setup):**
- Flash firmware
- Insert MicroSD with models
- Configure WiFi
- Test voice interaction
- Measure battery

---

**Estimated Time Savings:** 2-4 hours on deployment day

**Next:** Hardware arrives → Follow PHASE_12_HARDWARE_TESTING.md
