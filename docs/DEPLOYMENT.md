# Phase 12: Hardware Testing & Deployment Guide

## Overview

When the ESP32-S3 AMOLED device and Claude API key arrive, follow this guide to deploy the complete Bug Voice Assistant system.

**Estimated Duration:** 10-15 hours  
**Prerequisites:** 
- ESP32-S3 1.75" AMOLED Round device
- Claude API key
- MicroSD card (64GB+ recommended)
- USB-C cable with data support
- Development machine (Linux/Mac/Windows)

---

## Phase 1: Preparation (1-2 hours)

### 1.1 Organize Materials

```
Required:
├── ESP32-S3 device (Waveshare, model B0F8867487)
├── MicroSD card (64GB+)
├── USB-C data cable (not just power)
├── Development machine with:
│   ├── ESP-IDF installed
│   ├── esptool.py
│   └── Python 3.8+
└── Backup power (USB charger)
```

### 1.2 Download Required Files

**Models (will be downloaded to MicroSD):**
```bash
# Sizes (total ~285 MB):
- whisper_base.onnx (150 MB) → Spanish/English STT
- piper_es_CO.onnx (50 MB) → Colombian Spanish TTS
- piper_en_US_lessac.onnx (40 MB) → English TTS
```

**Configuration:**
```bash
# Create device_config.json (MicroSD):
{
  "device_id": "bug_001",
  "language_primary": "Spanish",
  "language_secondary": "English",
  "wifi_ssid": "YOUR_SSID",
  "wifi_password": "YOUR_PASSWORD",
  "claude_api_key": "sk-...",
  "power_profile": "ALWAYS_LISTENING"
}
```

### 1.3 Set Up Development Environment

```bash
# Install ESP-IDF (if not already installed)
git clone https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source ./export.sh

# Clone your project
cd ~/voice\ assitance
source ~/esp-idf/export.sh

# Verify toolchain
idf.py --version
```

---

## Phase 2: Firmware Flashing (1-2 hours)

### 2.1 Build Firmware

```bash
cd ~/voice\ assitance/firmware

# Configure for ESP32-S3
idf.py set-target esp32s3

# Configure project (optional: menuconfig for specific settings)
# idf.py menuconfig

# Build
idf.py build
```

**Expected output:**
```
...
[1234/1234] Linking CXX executable app.elf
app.elf size: 4.5 MB (flash)
Memory usage:
  IRAM:   184 KB / 256 KB
  DRAM:    48 KB / 520 KB
  Flash:  4.5 MB / 16 MB
```

### 2.2 Connect Device & Flash

```bash
# Connect ESP32-S3 via USB-C
# Device should appear as /dev/ttyUSB0 (Linux) or /dev/cu.usbserial-* (Mac)

# Verify connection
ls /dev/tty.usb* # macOS
ls /dev/ttyUSB* # Linux

# Flash firmware (includes bootloader, partition table, app)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output (should see boot sequence)
idf.py -p /dev/ttyUSB0 monitor

# Expected boot sequence:
# ESP32-S3 rev 0 @ (clock) MHz
# Build: Jan 1 2024
# Mode: DIO, Speed: 40MHz, PSRAM: Quad
# Calibration OK
# [BOOT] Loading app from partition...
# [UI] Initializing Littlevgl...
# [POWER] Initializing power optimizer...
# 🎤 Bug is ready to listen!
```

### 2.3 Troubleshooting Flash Issues

| Issue | Solution |
|-------|----------|
| "Failed to connect to ESP32" | Check USB cable, try different port, hold BOOT button during flashing |
| "SPIFFS write error" | Erase flash: `idf.py erase-flash` |
| "PSRAM test failed" | Reboot device, try flashing again |
| Port not found | Install USB drivers: CH340/CP2102 drivers for your OS |

---

## Phase 3: MicroSD Card Preparation (1-2 hours)

### 3.1 Prepare MicroSD Card

```bash
# Format MicroSD card as FAT32 (on development machine)
# macOS:
diskutil secureErase freespace 0 /dev/disk2  # Replace disk2 with your device

# Linux:
mkfs.vfat -F 32 /media/sdcard

# Windows: Right-click → Format → FAT32
```

### 3.2 Download Models

```bash
# Create directory structure
mkdir -p /media/sdcard/models
mkdir -p /media/sdcard/config
mkdir -p /media/sdcard/data

# Download models (can do on device or development machine first)
# Use a download manager or curl:

# Whisper Base (150 MB)
curl -L https://huggingface.co/Xenova/whisper-base-onnx/resolve/main/onnx/model.onnx \
  -o /media/sdcard/models/whisper_base.onnx

# Piper Spanish (50 MB)
curl -L https://huggingface.co/rhasspy/piper/resolve/main/es_CO/nuria/medium/model.onnx \
  -o /media/sdcard/models/piper_es_CO.onnx

# Piper English (40 MB)
curl -L https://huggingface.co/rhasspy/piper/resolve/main/en_US/lessac/medium/model.onnx \
  -o /media/sdcard/models/piper_en_US_lessac.onnx

# Verify downloads
ls -lh /media/sdcard/models/
# Output should show 3 files totaling ~240 MB
```

### 3.3 Create Device Configuration

```bash
cat > /media/sdcard/config/device_config.json << 'EOF'
{
  "device_id": "bug_001",
  "device_name": "Bug Voice Assistant",
  "language_primary": "Spanish",
  "language_secondary": "English",
  "region": "Colombia",
  "timezone": "America/Bogota",
  "wifi": {
    "ssid": "YOUR_NETWORK_SSID",
    "password": "YOUR_NETWORK_PASSWORD",
    "auto_connect": true
  },
  "cloud": {
    "enabled": true,
    "claude_api_key": "sk-...",
    "fallback_mode": "offline"
  },
  "power": {
    "profile": "ALWAYS_LISTENING",
    "auto_switch_battery_20": "PERIODIC",
    "auto_switch_battery_5": "GESTURE_ONLY"
  },
  "display": {
    "brightness_auto": true,
    "dark_mode": true,
    "dimming_timeout_s": 30
  },
  "audio": {
    "microphone_enabled": true,
    "speaker_enabled": true,
    "wake_word": "Hola Bug"
  }
}
EOF

# Create voice_params.json
cat > /media/sdcard/config/voice_params.json << 'EOF'
{
  "stt": {
    "language": "es",
    "confidence_threshold": 0.7,
    "timeout_s": 10
  },
  "tts": {
    "language_spanish": "es_CO",
    "language_english": "en_US",
    "speech_rate": 1.0,
    "pitch": 1.0
  },
  "llm": {
    "model": "claude-3-5-sonnet",
    "max_tokens": 256,
    "temperature": 0.7,
    "system_prompt": "You are Bug, a helpful voice assistant designed for Spanish-speaking users."
  }
}
EOF

# Eject MicroSD card
eject /media/sdcard
```

### 3.4 Insert MicroSD into Device

1. Power off device (if powered)
2. Locate MicroSD slot (typically on side or under case)
3. Insert MicroSD card until it clicks
4. Power on device

---

## Phase 4: Initial Setup (1 hour)

### 4.1 Boot & Initialization

```bash
# Power on device via USB or battery
# Monitor serial output:
idf.py -p /dev/ttyUSB0 monitor

# Expected sequence:
# [MAIN] NVS initialized ✓
# [MAIN] MicroSD initialized ✓
# [MAIN] Storage: 58.3 GB / 64 GB free
# [MAIN] Audio driver ready ✓
# [MAIN] Display driver ready ✓
# [UI] UI system ready ✓
# [POWER] Power optimizer ready (ALWAYS_LISTENING)
# [NETWORK] WiFi connecting...
# [NETWORK] Connected to YOUR_SSID ✓
# 🎤 Bug is ready to listen!
```

### 4.2 WiFi & API Configuration

**If WiFi connection fails:**
```bash
# Edit device_config.json on MicroSD:
{
  "wifi": {
    "ssid": "YOUR_SSID",
    "password": "YOUR_PASSWORD"
  }
}

# Eject, reinsert MicroSD, reboot
```

**If Claude API key fails:**
```bash
# Device will automatically fall back to offline mode
# Voice commands will be processed locally (limited LLM capability)
# Update API key:
{
  "cloud": {
    "enabled": true,
    "claude_api_key": "sk-YOUR_ACTUAL_KEY_HERE"
  }
}
```

### 4.3 First Voice Test

```bash
# Speak clearly into microphone:
Voice: "Hola Bug" (wake word)
Device: Green ring pulses, "Listening..."

Voice: "¿Qué hora es?" (What time is it?)
Device: 
  - Green ring steady during recording
  - Blue spinner during processing
  - White ring during response playback
  - "Son las 3 y 45 minutos." (TTS audio)

# If offline mode (no API key):
Voice: "Hola Bug"
Device: "Estoy en modo offline. Solo puedo hacer tareas locales."
        (I'm in offline mode. I can only do local tasks.)
```

---

## Phase 5: Testing (2-3 hours)

### 5.1 Voice Commands Test

```bash
# Test Spanish voice commands:
"Hola Bug" → Wake word detection
"¿Qué hora es?" → Current time
"¿Cuál es tu nombre?" → Assistant name
"Dime un chiste" → Random joke (jokes service)
"¿Cuál es el precio del Bitcoin?" → Current crypto price (requires API)
"Lee un libro" → Book reader (offline demo)
"Cuéntame una historia" → Story generator
"Juguemos trivia" → Trivia game

# Test English voice (if enabled):
"Bug" or "Hey Bug" → Wake word alternative
"What time is it?" → English response

# Test UI:
- Tap listening ring → Should activate mic if in gesture mode
- Swipe left/right → Navigate between screens
- Long-press home button → Settings
```

### 5.2 Email & Calendar Test (with API keys)

```bash
# Configure Gmail (requires OAuth):
Voice: "Lee mis correos" (Read my emails)
Device: Fetches emails from Gmail → Reads first email

# Configure Outlook (requires OAuth):
Voice: "Mis eventos" (My calendar events)
Device: Fetches calendar → Shows next 3 events

# Note: First time setup requires:
1. Device opens OAuth browser flow
2. User authenticates on phone/computer
3. Token saved to MicroSD config
```

### 5.3 Power Profile Test

```bash
# Manually switch power profiles via voice:
Voice: "Cambiar a modo periódico" (Switch to periodic mode)
Device: "Cambiando a modo periódico. WiFi deshabilitado en 2 minutos."
        Changes from Always-Listening (10mA) to Periodic (2mA)

Voice: "Máximo rendimiento" (Max performance)
Device: "Cambiando a máximo rendimiento."
        Back to Always-Listening

Voice: "Modo gestos" (Gesture mode)
Device: "Modo gestos activado. Toca para activar micrófono."
        Display turns off, awaiting touch
        (Gesture-Only mode, 0.5mA)

# Verify with serial monitor:
[POWER_OPTIMIZER] Profile switched: ALWAYS_LISTENING → PERIODIC
[POWER_OPTIMIZER] WiFi gated: OFF
[POWER_OPTIMIZER] Boost converter: ON (display still on)
```

### 5.4 Battery Monitoring Test

```bash
# Monitor serial output for power stats every 5 seconds:
[MAIN] Status: Battery=100%, Power=10.5mA, Est. 190.5 hours | VAD: energy=15.2, voice=NO | Boost: ON

# Test battery indicator on display:
- Battery >75%: Green
- Battery 25-75%: Yellow
- Battery <25%: Red (warning)

# Simulate low battery:
Power meter should show device auto-switching profiles
at 20% (PERIODIC) and 5% (GESTURE_ONLY)
```

### 5.5 Docker Backend Services Test

```bash
# On development machine, start Docker backend:
cd ~/voice\ assitance
docker-compose up -d

# Test services:
curl http://localhost:5106/health    # Jokes
curl http://localhost:5107/prices    # Crypto
curl http://localhost:5111/stations/popular  # Radio
curl http://localhost:5114/trivia/random  # Games

# Each service should return:
{ "status": "healthy", "service": "..." }
```

---

## Phase 6: Optimization & Fine-Tuning (1-2 hours)

### 6.1 Performance Tuning

```bash
# Monitor real power consumption with USB power meter:
1. Always-Listening (WiFi on, display full): 10-12 mA ✓
2. Processing (STT): Spike to 150-200 mA ✓
3. Periodic mode (WiFi off): 2-3 mA ✓
4. Gesture-only (display off): 0.5 mA ✓

# If higher than expected:
- Check boost converter is gating when display off
- Verify VAD is filtering noise (check serial logs)
- Ensure WiFi is truly sleeping during idle
```

### 6.2 Microphone Calibration

```bash
# Check VAD energy levels in different environments:

# Quiet room:
VAD: energy=5.0 (below threshold, no false activations)

# Normal background noise:
VAD: energy=25.0 (still below ~100 threshold)

# User speaking:
VAD: energy=85.0 (above threshold, activates STT)

# If too sensitive/insensitive:
Edit voice_activity_detection.c:
#define VAD_ENERGY_THRESHOLD 100  // Increase = less sensitive, Decrease = more sensitive
```

### 6.3 Voice Recognition Tuning

```bash
# Test wake word detection accuracy:
1. Say "Hola Bug" 10 times from different distances/angles
2. Check success rate in serial monitor:
   [WAKE_WORD] Detection confidence: 0.92 ✓
   [WAKE_WORD] Threshold: 0.70

# If too many false positives:
Increase threshold in wake_word_detector.c

# If missing real activations:
Decrease threshold
```

---

## Phase 7: Deployment (30 minutes)

### 7.1 Final Checks

```bash
# Verify firmware version:
Serial monitor should show build date/time

# Verify all services available:
✓ Audio (microphone + speaker)
✓ Network (WiFi connected)
✓ Display (dark theme, responsive)
✓ Models (whisper + piper loaded)
✓ Cloud API (Claude key configured)

# Test offline fallback:
Disconnect WiFi → Device should continue working
Local voice commands → Should process with mock LLM
```

### 7.2 Battery Calibration

```bash
# Calibrate battery voltage reading:
Power off completely
Charge to 100% (USB)
Power on → Should show 100%
Monitor for 1 hour → Power consumption matches estimates

# If battery % is inaccurate:
Adjust voltage calibration in power_manager.c
```

### 7.3 Prepare for Field Deployment

```bash
# Create backup of working configuration:
cp -r /media/sdcard ~/bug_device_backup/

# Document settings:
- WiFi SSID
- Claude API key (securely)
- Language preferences
- Power profile choice

# Label device:
"Bug Voice Assistant - Device 001"
"Built: 2024"
"Firmware v1.0"
```

---

## Phase 8: Post-Deployment (Ongoing)

### 8.1 Monitoring & Maintenance

```bash
# Weekly checks:
- Battery drains to expected level (1-2% per day in Always-Listening)
- Voice commands consistently recognized
- Display responsive to touch
- No error messages in serial logs

# Monthly checks:
- Microphone sensitivity (check for voice noise)
- WiFi connection stability
- Model updates (if needed from HuggingFace)
```

### 8.2 Log Collection & Analysis

```bash
# Save serial logs for debugging:
idf.py -p /dev/ttyUSB0 monitor > ~/bug_device_logs/device_001_$(date +%Y%m%d_%H%M%S).log

# Analyze power consumption:
grep "Status:" ~/bug_device_logs/*.log | \
  awk -F'Power=' '{print $2}' | \
  awk -F'mA' '{sum+=$1; count++} END {print "Avg power: " sum/count " mA"}'
```

### 8.3 Troubleshooting Guide

| Issue | Symptom | Fix |
|-------|---------|-----|
| Device unresponsive | Black screen, no serial output | Hard reset (hold RESET 5s), reflash firmware |
| WiFi drops | "WiFi disconnected" spam | Restart router, update WiFi credentials |
| Battery drains fast | >5% per day | Check boost converter gating, verify dark mode on |
| Microphone not working | No transcription in logs | Check I2S pins, reseat microphone connector |
| Display corruption | Pixels stuck/flickering | Reboot device, check SPI connection |
| Model loading fails | "Failed to load whisper_base.onnx" | Verify models on MicroSD, check file integrity |

---

## Success Criteria

Device is **ready for production** when:

- [x] Firmware boots without errors
- [x] All services online (health checks passing)
- [x] Voice commands recognized (>90% accuracy)
- [x] Battery life matches estimates (8-14 days)
- [x] Power profiles auto-switch correctly
- [x] Display responsive and readable
- [x] Email/Calendar integration working (if API keys configured)
- [x] Offline fallback functional
- [x] Serial logs clean (no error/warning spam)

---

## Next Steps

Once hardware testing complete:

1. **Document Lessons Learned** - What worked, what needs adjustment
2. **Fine-tune Battery** - Real-world measurements vs. estimates
3. **Optimize Models** - Consider INT8 quantization for faster inference
4. **Scale Backend** - Run services on cloud (AWS/GCP) for always-on availability
5. **Add Features** - Expand skill set based on user needs

---

**Total estimated time: 10-15 hours**

When hardware arrives, follow this guide step-by-step. Each phase builds on the previous one. Take time to test thoroughly before considering deployment complete.

Good luck with your Bug Voice Assistant! 🎤🐛

