# MicroSD Card File Structure

## Directory Layout

```
/sd (MicroSD Card Root)
├── models/                          # All AI models (stored once on MicroSD)
│   ├── whisper_base.onnx           # Speech-to-Text (faster-whisper)
│   │   └── Size: ~150MB
│   ├── piper_es_CO.onnx            # Text-to-Speech (piper TTS)
│   │   └── Size: ~50MB
│   └── tinyllama_q4.gguf           # Optional local LLM (TinyLLaMA quantized)
│       └── Size: ~4GB (optional, if local LLM is desired)
│
├── configs/                         # Device configuration files
│   ├── device_config.json          # Main device settings
│   ├── voice_params.json           # Audio & voice settings
│   └── power_profile.json          # Power management profiles
│
└── data/                           # Runtime data & history
    ├── conversation_history.json   # Chat history (auto-saved)
    ├── user_prefs.json            # User preferences
    └── firmware_version.txt        # Current firmware version
```

## Files Details

### Models Directory

**whisper_base.onnx** (~150MB)
- Speech-to-Text model
- faster-whisper base version
- Supports Spanish & English
- Runs locally on ESP32-S3 (CPU or ESP-NN accelerated)

**piper_es_CO.onnx** (~50MB)
- Text-to-Speech model
- Colombian Spanish female voice
- Generates 16kHz 16-bit PCM audio
- Runs locally on device

**tinyllama_q4.gguf** (~4GB, optional)
- Local LLM for offline inference
- TinyLLaMA 1.1B quantized Q4_K_M
- Alternative to cloud Claude API
- Slower but fully local (no internet required)

### Configuration Files

**device_config.json**
- Device name, language, timezone
- Audio settings (sample rate, bit depth)
- Power management settings
- Model paths & selection
- Network & service URLs

**voice_params.json**
- Voice Activity Detection (VAD) thresholds
- Audio preprocessing settings
- Colombian Spanish voice parameters
- Language & recognition settings

**power_profile.json**
- CPU frequency scaling profiles
- Modem sleep configuration
- Display brightness levels
- Battery threshold management

### Data Files

**conversation_history.json**
- Auto-saved conversation history
- Used for context in multi-turn chats
- Pruned after reaching size limit

**user_prefs.json**
- User language preference
- Voice speed & pitch settings
- Favorite contacts for email/calendar
- Notification preferences

**firmware_version.txt**
- Current firmware version
- Used for OTA update checks
- Rollback version tracking

---

## How to Load Models onto MicroSD

### Before First Boot

1. Format MicroSD card (FAT32, 128GB+ recommended)

2. Copy files:
   ```bash
   # macOS/Linux
   mkdir -p /Volumes/BUG_MICROSD/models
   cp whisper_base.onnx /Volumes/BUG_MICROSD/models/
   cp piper_es_CO.onnx /Volumes/BUG_MICROSD/models/
   # (tinyllama is optional, only if using local LLM)
   cp tinyllama_q4.gguf /Volumes/BUG_MICROSD/models/
   
   # Copy configs
   cp -r configs/ /Volumes/BUG_MICROSD/
   mkdir -p /Volumes/BUG_MICROSD/data
   
   # Eject safely
   diskutil eject /Volumes/BUG_MICROSD
   ```

3. Insert MicroSD into ESP32-S3 device slot

4. Boot device → firmware checks for models

5. If models found → device operates fully offline

6. If models missing → device falls back to cloud STT/TTS (requires WiFi/4G)

### Model Download Links

**whisper_base.onnx**
- Source: huggingface.co/openai/whisper-base
- ONNX converted version available via faster-whisper releases

**piper_es_CO.onnx**
- Source: huggingface.co/rhasspy/piper-voices
- Colombian Spanish: `es_CO-SalomeNeural-medium.onnx`

**tinyllama_q4.gguf**
- Source: huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF
- Download: `tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf`

---

## Storage Calculation

| Component | Size | Notes |
|-----------|------|-------|
| whisper_base.onnx | 150 MB | Required |
| piper_es_CO.onnx | 50 MB | Required |
| tinyllama_q4.gguf | 4 GB | Optional (local LLM only) |
| Configs | 1 MB | Required |
| Data (conversation history) | ~100 MB | Grows over time |
| **Total (without local LLM)** | **~202 MB** | Fits easily on any MicroSD |
| **Total (with local LLM)** | **~4.2 GB** | Still fits on 8GB+ MicroSD |

---

## Device-on-Boot Behavior

### If Models Present on MicroSD:
```
Boot → Load device_config.json
     → Check for whisper_base.onnx
     → Check for piper_es_CO.onnx
     → Initialize STT/TTS locally
     → Ready for voice interaction (100% offline)
```

### If Models Missing:
```
Boot → Load device_config.json
     → Models NOT found on MicroSD
     → Initialize WiFi/4G
     → Ready for voice (requires cloud fallback)
     → Display warning: "Models not found, using cloud services"
```

---

## Updating Models

To update models without re-flashing firmware:

1. Connect USB to device
2. Mount MicroSD via USB card reader
3. Delete old model files from /models/
4. Copy new model files
5. Eject & power cycle device
6. Device auto-detects new models on boot

No firmware rebuild needed.

---

## Best Practices

✅ **Do:**
- Use 128GB+ MicroSD (Class 10, UHS-II recommended)
- Backup configs/ directory regularly
- Check free space before adding models (aim for 20%+ free)
- Use consistent file naming across devices

❌ **Don't:**
- Fill MicroSD beyond 90% capacity
- Remove card while device is powered on
- Edit configs directly without understanding fields
- Delete tinyllama if planning to use local LLM later

---

**Next Step (STEP 4):**
- Add 4G module support (SIM7670G driver)
- Cloudflare Tunnel for cloud fallback
- Remote model updates via 4G
