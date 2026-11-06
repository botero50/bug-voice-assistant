# Pre-Hardware Verification Checklist
## Everything to Verify Before Device Arrives

**Status:** Do this while waiting for hardware (1-2 weeks)  
**Time:** ~2-3 hours total  
**Goal:** Ensure smooth deployment when device arrives

---

## ✅ Phase 1: Configuration Verification (30 min)

### Files Created
- [ ] `.env` file exists
- [ ] `config/device_config.json` exists  
- [ ] Both files are in `.gitignore` (don't commit them!)
- [ ] `.env.template` available as backup
- [ ] `config/device_config.template.json` available as backup

### `.env` Configured
```bash
# Check these values are set (not defaults):
[ ] CLAUDE_API_KEY=sk-ant-xxxxx (not "sk-your-key-here")
[ ] WIFI_SSID=your-actual-ssid (not "YOUR_SSID")
[ ] WIFI_PASSWORD=your-actual-password (not "YOUR_PASSWORD")
[ ] TELEGRAM_CHAT_ID set (if using Telegram)
[ ] LOG_LEVEL=INFO
```

### `device_config.json` Configured
```json
[ ] "location": "Bogotá, Colombia"  // or your location
[ ] "primary_language": "es-ES"     // or your language
[ ] "wifi_ssid": "your-ssid"        // matches .env
[ ] "api_key": "sk-ant-xxxxx"       // your Claude key
[ ] "battery_capacity_mah": 2000    // device specification
[ ] "timezone": "America/Bogota"    // matches your location
```

### Verify Nothing Sensitive Committed
```bash
cd /Users/alejandrobotero/voice\ assitance

# Check git status
git status

# Verify API keys NOT in git
git log -p | grep -i "claude_api_key" || echo "✅ No API keys in history"

# Verify .env/.json not tracked
git ls-files | grep ".env" || echo "✅ .env files not tracked"
git ls-files | grep "device_config.json" || echo "✅ device_config.json not tracked"
```

---

## ✅ Phase 2: Docker Services Verification (45 min)

### Docker Installation
```bash
[ ] Docker Desktop installed
[ ] Docker command works: docker --version
[ ] Docker Compose works: docker-compose --version
[ ] Can run docker commands without sudo
```

### Docker Build Test
```bash
cd /Users/alejandrobotero/voice\ assitance

# Try to build (download 1-2 GB, takes 2-5 min)
docker-compose build

# Should see:
# ✅ Successfully built [image-id] for all services
# ❌ Should NOT see: "ERROR" or "failed"
```

### Docker Services Startup Test
```bash
# Start all services
docker-compose up -d

# Wait 5 seconds for startup
sleep 5

# Check all services running
docker-compose ps

# Should see:
# SERVICE_NAME    STATUS          (all showing "healthy" or "up")
# stt             healthy
# llm             healthy
# tts             healthy
# orchestrator    up
# gmail           healthy
# ... etc
```

### Individual Service Health Check
```bash
# Test each critical service
curl http://localhost:5001/health  # STT
curl http://localhost:5002/health  # LLM
curl http://localhost:5003/health  # TTS
curl http://localhost:5000/health  # Orchestrator

# Each should return:
# {"status": "healthy", "service": "name"}
```

### Docker Cleanup
```bash
# Stop all services
docker-compose down

# Verify stopped
docker-compose ps  # Should show all "exited" or empty

# Optional: Clean up images (saves disk space)
docker system prune -y
```

### Verify Checklist
- [ ] Docker builds without errors
- [ ] All 19 services build successfully
- [ ] docker-compose up works
- [ ] All services reach "healthy" state
- [ ] Can curl health endpoints
- [ ] docker-compose down stops cleanly
- [ ] Docker images are 5-10GB total

---

## ✅ Phase 3: Models & Storage Verification (1 hour)

### MicroSD Card Preparation
- [ ] MicroSD card obtained (128GB recommended, 64GB minimum)
- [ ] MicroSD card reader available
- [ ] Card formatted to ext4 (Linux) or FAT32 (Windows/macOS)
- [ ] Card can be mounted on computer

### Create Directory Structure
```bash
# On MicroSD card, create folders:
models/
  whisper/
  piper/
  embeddings/
config/
logs/
```

### Download Models (Choose One Approach)

**Option A: Download to Computer First, Copy to MicroSD**
```bash
# 1. Download Whisper (~1.4 GB)
pip install faster-whisper
python3 -c "from faster_whisper import WhisperModel; WhisperModel('base')"

# 2. Find and copy to MicroSD
# Usually: ~/.cache/huggingface/hub/models--Systran--faster-whisper-base/

# 3. Download Piper models (~600 MB)
mkdir -p /path/to/microsd/models/piper
# Download from HuggingFace:
# https://huggingface.co/rhasspy/piper-voices/tree/main/es/es_ES/carme/medium
# https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/lessac/medium

# 4. Copy to MicroSD
```

**Option B: Download Directly to MicroSD (if lots of storage)**
```bash
# Mount MicroSD
mkdir -p ~/microsd_mount
# mount /dev/sdX1 ~/microsd_mount

# Download directly to MicroSD
export HF_HOME=~/microsd_mount/models
pip install faster-whisper
python3 -c "from faster_whisper import WhisperModel; WhisperModel('base')"
```

### Verify Model Files
- [ ] Whisper files exist: `models/whisper/xxx.onnx`
- [ ] Piper Spanish: `models/piper/es_ES-carme-medium.onnx*`
- [ ] Piper English: `models/piper/en_US-lessac-medium.onnx*`
- [ ] Total models size: 2-3 GB
- [ ] MicroSD has 5+ GB free space remaining

### Storage Checklist
```bash
# On your computer, verify MicroSD structure:
ls -la /path/to/microsd/

# Should show:
# models/
# config/
# logs/

# Check space:
du -sh /path/to/microsd/

# Should show usage <50GB (leaving room for logs/data)
```

---

## ✅ Phase 4: API Key Verification (30 min)

### Claude API
```bash
[ ] Created account at https://console.anthropic.com
[ ] API key generated in "API Keys" section
[ ] Key starts with "sk-ant-"
[ ] Key is in .env as CLAUDE_API_KEY
[ ] Never committed to git
[ ] Plan: "Scale-up API" selected (or intended for production)
```

### Test Claude API Locally (Optional)
```bash
python3 << 'PYTHON'
import requests
import os
from dotenv import load_dotenv

load_dotenv()
api_key = os.getenv("CLAUDE_API_KEY")

if api_key and api_key != "sk-your-key-here":
    # Test API key
    headers = {
        "x-api-key": api_key,
        "anthropic-version": "2023-06-01",
        "content-type": "application/json"
    }
    
    data = {
        "model": "claude-3-5-sonnet-20241022",
        "max_tokens": 256,
        "messages": [{"role": "user", "content": "Hola, ¿cómo estás?"}]
    }
    
    response = requests.post(
        "https://api.anthropic.com/v1/messages",
        headers=headers,
        json=data
    )
    
    if response.status_code == 200:
        print("✅ Claude API key is valid!")
        print(f"Response: {response.json()['content'][0]['text']}")
    else:
        print(f"❌ API error: {response.status_code}")
        print(response.text)
else:
    print("⚠️  No valid API key in .env")

PYTHON
```

### Gmail Integration (Optional)
```bash
[ ] OAuth2 credentials created at https://console.cloud.google.com
[ ] Gmail API enabled
[ ] client_id saved in .env
[ ] client_secret saved in .env
```

### Outlook Integration (Optional)
```bash
[ ] Application registered at https://portal.azure.com
[ ] Microsoft Graph API enabled
[ ] client_id saved in .env
[ ] client_secret saved in .env
```

---

## ✅ Phase 5: ESP-IDF Verification (30 min)

### ESP-IDF Installation
```bash
# Check if already installed
idf.py --version

# If not found, install:
# macOS:
brew install esp-idf

# Or from source (latest):
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh

# Add to ~/.zshrc or ~/.bashrc:
export IDF_PATH=~/esp/esp-idf
alias get_idf='. $IDF_PATH/export.sh'
```

### Firmware Build Test
```bash
cd /Users/alejandrobotero/voice\ assitance/firmware

# Check if project.yml or CMakeLists.txt exists
[ -f CMakeLists.txt ] && echo "✅ CMakeLists.txt found" || echo "❌ Missing"

# Try to build firmware (will fail without hardware, but checks setup)
idf.py build

# Should see:
# ✅ Successfully built xxx
# May fail at end with: "Device not found" - that's OK
```

### ESP-IDF Components Verified
- [ ] idf.py command works
- [ ] ESP-IDF components available (esp_common, freertos, driver, etc.)
- [ ] FreeRTOS available
- [ ] LVGL component available
- [ ] FAT filesystem available

---

## ✅ Phase 6: Firmware Components Verification (15 min)

### Critical Files Present
```bash
cd /Users/alejandrobotero/voice\ assitance/firmware/main

# Check all critical files exist
[ -f main.c ] && echo "✅ main.c"
[ -f audio_driver.c ] && echo "✅ audio_driver.c"
[ -f display_driver.c ] && echo "✅ display_driver.c"
[ -f voice_loop.c ] && echo "✅ voice_loop.c"
[ -f ui_config.h ] && echo "✅ ui_config.h"
[ -f CMakeLists.txt ] && echo "✅ CMakeLists.txt"

# Check file sizes (roughly)
wc -l *.c *.h | tail -1  # Should show 2000+ total lines
```

### Header Files Verified
- [ ] `ui_config.h` - Design system constants
- [ ] All #include paths valid
- [ ] No circular includes
- [ ] Function declarations present

### Build Configuration
- [ ] `CMakeLists.txt` has correct component requires
- [ ] REQUIRES: esp_common, esp_system, freertos, driver, etc.
- [ ] PRIV_REQUIRES: lvgl, sdmmc, fatfs, etc.
- [ ] idf_component_register present

---

## ✅ Phase 7: Documentation Review (15 min)

### Key Documents Exist
- [ ] `README.md` - Quick start
- [ ] `DEPLOYMENT_PREP_GUIDE.md` - Pre-deployment (this section!)
- [ ] `GPIO_PINOUT_REFERENCE.md` - Pin configuration
- [ ] `HARDWARE.md` - Assembly guide
- [ ] `PHASE_12_HARDWARE_TESTING.md` - Hardware testing steps
- [ ] `BATTERY_OPTIMIZATION_PLAN.md` - Power strategy
- [ ] `FINAL_SUMMARY.md` - Complete overview

### Documents Accurate
- [ ] Pin assignments match firmware code
- [ ] API endpoints match service implementations
- [ ] Default configurations match device_config.json template
- [ ] URLs and links all working

---

## ✅ Phase 8: Scripts & Tools Verification (10 min)

### Helper Scripts Created
```bash
[ -x setup.sh ] && echo "✅ setup.sh executable"
[ -x start_services.sh ] && echo "✅ start_services.sh"
[ -x stop_services.sh ] && echo "✅ stop_services.sh"
[ -x test_services.sh ] && echo "✅ test_services.sh"
```

### Git Status Clean
```bash
cd /Users/alejandrobotero/voice\ assitance

# Should show no uncommitted changes
git status
# "nothing to commit, working tree clean"

# No sensitive files
git ls-files | grep ".env\|device_config.json\|\.env"
# Should return empty
```

---

## ✅ Phase 9: Performance Baseline (10 min)

### Local Service Performance
```bash
# Start services
docker-compose up -d
sleep 5

# Measure LLM response time
time curl -X POST http://localhost:5002/chat \
  -H "Content-Type: application/json" \
  -d '{"text": "Hola"}'

# Expected: 100-500ms on modern computer
# Record this baseline for comparison on device
```

### Docker Resource Usage
```bash
docker stats --no-stream

# Record memory/CPU usage per service
# This shows if any service is too heavy
```

---

## ✅ Phase 10: Final Pre-Deployment Checklist

### Summary
- [ ] All 10 phases completed
- [ ] No errors in any verification
- [ ] All files present and readable
- [ ] Configuration templates understood
- [ ] Models ready (or download process clear)
- [ ] APIs configured
- [ ] Docker services tested
- [ ] Firmware build system working
- [ ] Documentation reviewed
- [ ] Helper scripts created

### Sign-Off
```
Device Ready for Deployment:  ✅ YES
Date: _______________
API Keys Secured:             ✅ YES
MicroSD Prepared:             ✅ YES (or planned)
Docker Verified:              ✅ YES
Build System Ready:           ✅ YES
Documentation Complete:       ✅ YES
```

---

## 📋 When Hardware Arrives

### Day 1 (Device Delivery)
- [ ] Unbox and inspect device
- [ ] Check components (display, microphone, MicroSD slot)
- [ ] Insert MicroSD card with models

### Day 2 (Flashing)
- [ ] Connect USB cable
- [ ] Run: `idf.py flash monitor`
- [ ] Should see boot messages
- [ ] Configure WiFi at startup

### Day 3 (Testing)
- [ ] Test voice commands
- [ ] Measure battery current
- [ ] Verify all skills working

### Day 4+ (Optimization)
- [ ] Fine-tune power profiles
- [ ] Test battery life (24h)
- [ ] Document any issues

---

## 🆘 If Something Fails

### Docker won't build:
```bash
# Clear Docker cache
docker system prune -a
docker-compose build --no-cache
```

### API key not working:
```bash
# Check .env syntax (no quotes needed)
grep CLAUDE_API_KEY .env

# Test key directly:
curl -H "x-api-key: YOUR_KEY" \
  https://api.anthropic.com/v1/messages
```

### ESP-IDF not found:
```bash
# Reinstall
brew install esp-idf

# Or add to PATH manually:
export PATH="$PATH:/path/to/esp-idf/tools"
```

### Models too large:
```bash
# Use smaller models:
# - Whisper "tiny" instead of "base" (~400 MB)
# - Piper "small" instead of "medium" (~100 MB)
# See DEPLOYMENT_PREP_GUIDE.md for alternatives
```

---

## ✅ Ready for Deployment!

If all 10 phases pass verification, you're ready.

**Hardware arrival → Follow PHASE_12_HARDWARE_TESTING.md**

---

**Last Updated:** November 4, 2026  
**Next Step:** Wait for hardware, then run PHASE_12_HARDWARE_TESTING.md
