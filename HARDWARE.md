# Hardware Setup Guide

Complete instructions for assembling and testing Bug on ESP32-S3.

---

## 📦 Components Checklist

### Required
- [ ] **ESP32-S3 1.75" AMOLED Round Board** - Ordered
- [ ] **MicroSD Card 128GB+** - Order from Amazon (~$15-25)
- [ ] **MicroSD to USB Reader** - Order from Amazon (~$5-10)
- [ ] **5V 2A Power Supply** - Order from Amazon (~$10-15)
- [ ] **USB-C Cable** - Usually included with board

### Already Have
- [x] GPS Module (u-blox MAX-M8Q)

### Optional (Recommended for 4G Fallback)
- [ ] **SIM7670G 4G Module** - Order from Aliexpress (~$25-40)
- [ ] **4G SIM Card** - Get from carrier (~$1-5)

---

## 🔧 Assembly Steps

### Step 1: Unbox ESP32-S3 Device

When your device arrives:
```
✓ Check all components present
✓ Verify no physical damage
✓ Read included documentation
```

Device includes:
- ESP32-S3 board with AMOLED display
- USB-C cable
- Dual microphone array (built-in) ✓
- 2W speaker (built-in) ✓
- Aluminum protective case

### Step 2: Prepare MicroSD Card

On your computer:

```bash
# 1. Format MicroSD as FAT32 (macOS)
diskutil eraseVolume FAT32 MICROSD /Volumes/path/to/device

# 2. Or on Linux
sudo mkfs.vfat /dev/sdX1  # Replace X with your device

# 3. Or on Windows
# Right-click → Format → FAT32
```

### Step 3: Download Models (~200 MB)

Download these files to your computer:

**Whisper Base (Speech-to-Text) - 150 MB**
```
Download from: https://github.com/openai/whisper/releases
File: ggml-base.bin or similar
Save as: whisper_base.onnx
```

**Piper ES-CO (Text-to-Speech) - 50 MB**
```
Download from: https://github.com/rhasspy/piper
Search for: es_CO (Colombian Spanish)
File: es_CO-*.onnx
Save as: piper_es_CO.onnx
```

### Step 4: Prepare MicroSD Contents

```bash
# Connect MicroSD to your computer via USB reader

# Copy structure:
# Create folders on card:
/MICROSD
├── models/
│   ├── whisper_base.onnx     (paste here)
│   └── piper_es_CO.onnx      (paste here)
├── configs/
│   ├── device_config.json    (copy from repo)
│   ├── voice_params.json     (copy from repo)
│   └── power_profile.json    (copy from repo)
└── data/
    └── (empty, for conversation history)
```

**Copy commands:**

```bash
# Mount microSD (should auto-mount)
# Then copy:

cd "voice assitance"

mkdir -p "/Volumes/MICROSD/models"
mkdir -p "/Volumes/MICROSD/configs"
mkdir -p "/Volumes/MICROSD/data"

# Copy models
cp ~/Downloads/whisper_base.onnx "/Volumes/MICROSD/models/"
cp ~/Downloads/piper_es_CO.onnx "/Volumes/MICROSD/models/"

# Copy configs
cp microsd_files/configs/* "/Volumes/MICROSD/configs/"

# Verify
ls -la "/Volumes/MICROSD/models/"
ls -la "/Volumes/MICROSD/configs/"

# Eject safely
diskutil eject "/Volumes/MICROSD"
```

---

## ⚡ Flashing Firmware

### Prerequisites

Install ESP-IDF 5.1 (one-time):

```bash
# Clone ESP-IDF
git clone https://github.com/espressif/esp-idf.git --branch v5.1
cd esp-idf

# Initialize submodules
git submodule update --init --recursive

# Install tools
./install.sh esp32s3

# Source the environment (do this every session)
source export.sh
```

### Build Firmware

```bash
cd "voice assitance/firmware"

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Check output
ls -lh build/firmware.bin  # Should exist, ~500KB
```

### Flash to Device

**Mac/Linux:**
```bash
# Connect device via USB-C cable

# Find port
ls /dev/tty.usbserial*  # Mac
ls /dev/ttyUSB*        # Linux

# Flash
idf.py flash -p /dev/ttyUSB0

# Monitor boot
idf.py monitor
```

**Windows:**
```bash
# Find port in Device Manager

# Flash
idf.py flash -p COM3 -b 921600

# Monitor
idf.py monitor
```

### Verify Boot

You should see:
```
[Boot] Starting Boot Sequence...
[Init] Initializing modules...
[Audio] I2S driver initialized
[SD] MicroSD card mounted at /sd
[Models] Loading whisper_base.onnx
[Models] Loading piper_es_CO.onnx
[Network] WiFi initialized
[Ready] Device ready to listen for "Hola Bug"
```

---

## 🎤 Test Microphone & Speaker

### Manual Test (via Serial Monitor)

```bash
cd firmware && idf.py monitor
```

In serial output, you should see microphone levels updating every second.

### Voice Test

1. Power on device
2. Open serial monitor: `idf.py monitor`
3. Say "Hola Bug" (when Phase 4 enabled)
4. Should see in logs:
   ```
   [Wake] Wake word detected!
   [Audio] Recording command...
   [STT] Transcribing...
   [LLM] Processing...
   [TTS] Synthesizing...
   [Speaker] Playing audio...
   ```

---

## 🔋 Power Testing

### Equipment Needed
- USB power meter (~$20-50)
- Multimeter (optional)

### Test 1: Baseline Power (Idle)

```bash
# Connect USB power meter between device and power supply
# Wait 2 minutes for stabilization
# Record current reading

Expected:
- Idle: 5-8 mA
- Periodic listening: 2-3 mA
```

### Test 2: Wake Word Power

```bash
# With power meter connected:
1. Record baseline (idle)
2. Say "Hola Bug" repeatedly
3. Observe current spike
4. Note peak mA and duration

Expected:
- Peak: 20-30 mA
- Duration: 25-50ms
```

### Test 3: Full Voice Command

```bash
# Say: "Hola Bug, ¿cuál es tu nombre?"
# Measure power during entire pipeline

Expected:
- STT: 50mA for ~1 second
- LLM: 30-40mA for ~1-2 seconds
- TTS: 40mA for ~1-2 seconds
- Total energy: 0.5-1.0 Joules
```

### Test 4: 24-Hour Battery

```bash
# Fully charge 2000mAh battery
# Run device continuously
# Every hour:
#   - Record time
#   - Measure voltage
#   - Speak 1-2 commands

Expected:
- Real-world battery life: 10-30 days
- Linear voltage drop
- No unexpected spikes
```

---

## 🌐 WiFi Setup

### Connect to WiFi

Edit `microsd_files/configs/device_config.json`:

```json
{
  "wifi": {
    "ssid": "YOUR_SSID",
    "password": "YOUR_PASSWORD",
    "timeout_ms": 10000
  }
}
```

Copy updated config to MicroSD:
```bash
cp microsd_files/configs/device_config.json "/Volumes/MICROSD/configs/"
```

Restart device.

---

## 📡 4G Setup (Optional)

### Install SIM7670G Module

If you have the 4G module:

```
GPIO Connections:
- SIM7670G TX → GPIO 16
- SIM7670G RX → GPIO 17
- SIM7670G GND → GND
- SIM7670G VCC → 3.3V (with voltage regulator)
```

### Insert SIM Card

```bash
# Get SIM card activated with carrier
# Insert into SIM7670G SIM slot
# Device will auto-register to network
```

### Test 4G Fallback

```bash
# Disconnect WiFi
# Say voice command
# Should fall back to 4G

Expected:
- Registration takes 5-30 seconds
- Command still works
- Latency +500ms (vs WiFi)
- Power draw: 300-500mA during transfer
```

---

## ✅ Complete Checklist

### Hardware Assembly
- [ ] MicroSD card prepared with models
- [ ] Device powered on successfully
- [ ] Boot sequence completes
- [ ] Microphone input verified
- [ ] Speaker output verified
- [ ] WiFi connected (check serial log)

### Firmware
- [ ] ESP-IDF 5.1 installed
- [ ] Firmware builds without errors
- [ ] Firmware flashed successfully
- [ ] Device boots without crashes
- [ ] All modules initialized

### Power
- [ ] Idle current: 5-8mA
- [ ] Wake word detected: <30ms latency
- [ ] Full command processed: <3 seconds
- [ ] Battery life: 10+ days

### Features
- [ ] Wake word works ("Hola Bug")
- [ ] STT transcribes Spanish correctly
- [ ] LLM responds in Spanish
- [ ] TTS speaks back in Colombian accent
- [ ] Email/calendar accessible
- [ ] Multi-user speaker ID works

### Stability
- [ ] No crashes over 1 hour
- [ ] No memory leaks detected
- [ ] WiFi reconnects automatically
- [ ] 4G fallback works (if installed)

---

## 🐛 Troubleshooting

### Device won't boot

```bash
# Check serial monitor
idf.py monitor

# Common issues:
# - MicroSD not detected: Check connection, try reformatting
# - Models not found: Check microsd_files/ copied correctly
# - WiFi issue: Verify SSID/password in device_config.json
```

### Microphone not recording

```bash
# In serial monitor, check:
# [Audio] I2S initialized
# [Audio] Microphone gain: X

# If not appearing:
# - Check GPIO 10/11/12 connections
# - Verify INMP441 powered (3.3V)
# - Check microphone not muted
```

### Speaker not playing

```bash
# Check serial output:
# [TTS] Synthesizing audio...
# [Speaker] Playing audio...

# If stuck:
# - Check GPIO 6/7/8 I2S connections
# - Check speaker powered
# - Check volume level in configs
```

### WiFi not connecting

```bash
# Serial should show:
# [Network] Connecting to SSID...
# [Network] Got IP: xxx.xxx.xxx.xxx

# If fails:
# - Verify SSID/password in config
# - Check 2.4GHz band (not 5GHz)
# - Move closer to router
# - Restart device
```

### High power consumption

```bash
# Normal:
# - Idle: 5-8mA
# - WiFi idle: 10-20mA
# - If >30mA idle: Check WiFi is idle
# - If >50mA: Check what's processing

# Solutions:
# - Disable WiFi when not needed
# - Enable power-save mode
# - Check CPU frequency scaling
```

---

## 📊 Performance Targets

| Metric | Target | Measured |
|--------|--------|----------|
| Boot time | <5s | — |
| Wake word latency | <100ms | — |
| STT latency | 2-3s | — |
| LLM latency | 1-2s | — |
| TTS latency | 1-2s | — |
| Total latency | <6s | — |
| Idle current | 5-8mA | — |
| WiFi idle | 10-20mA | — |
| Wake detection power | 20-30mA | — |
| Peak power | <1000mA | — |
| Battery life | 10+ days | — |

---

**Status:** Ready for hardware setup  
**Last Updated:** November 2026
