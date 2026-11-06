# Battery Optimization Strategy for ESP32-S3 1.75" AMOLED Voice Assistant

## Executive Summary

For a 2000mAh battery on an ESP32-S3 running voice assistant tasks, we target **6-10 hours of continuous use** or **30+ days on standby** through a multi-layered power optimization strategy. The device will implement dynamic power profiling based on use patterns, with aggressive AMOLED display gating as the primary power win.

---

## 1. Hardware Power Architecture

### 1.1 Power Consumption Baseline (ESP32-S3 + AMOLED)

| Component | Active (mA) | Idle (mA) | Sleep (µA) | Note |
|-----------|-------------|----------|-----------|------|
| **CPU (240MHz dual-core)** | 80-120 | 40-50 | 10 | Firmware running, WiFi on |
| **WiFi Modem** | 80-120 | 10 | 0.1 | TX/RX active, idle, off |
| **AMOLED Display** | 45-100 | 2-5 | 0.001 | Full brightness, dimmed, off |
| **Microphone (INMP441)** | 5-8 | 5-8 | 0 | Streaming, idle, muted |
| **Boost Converter (LCD_EN)** | 30-50 | 25-35 | 0.001 | Pixel drive voltage, always on (issue!) |
| **Linear Regulator (AMS1117)** | 5-10 | 5-10 | 0 | Quiescent current drain |
| **System Total (Full)** | 180-280 | 60-110 | 15-20 | All systems active, idle, deep sleep |

**Key Finding:** The AMOLED boost converter (GPIO 13 / LCD_EN) is the **#1 power leak**—it remains enabled even when the display is asleep, wasting 25-35 mA continuously. Gating this converter is the single largest optimization opportunity.

---

## 2. Three Power Profiles

### Profile 1: Always-Listening (Full Wake)
- **CPU:** 240 MHz dual-core
- **WiFi:** Connected, listening for commands
- **Microphone:** Active, continuously sampling
- **Display:** Full brightness (auto-dimmed after 30s)
- **Target Power:** **8-12 mA sustained**
- **Battery Life:** 160-250 hours (~1 week continuous)
- **Use Case:** Daytime use, always ready to respond

### Profile 2: Periodic Wake (Eco Mode)
- **CPU:** 80-160 MHz, periodic wake every 5-30 seconds
- **WiFi:** Sleep between wake cycles
- **Microphone:** Duty-cycled (listen 2s, sleep 3s)
- **Display:** Auto-dim to 10% after 10s, touch-activates
- **Target Power:** **2-3 mA sustained**
- **Battery Life:** 800-1000 hours (~1 month)
- **Use Case:** Overnight, low-activity periods

### Profile 3: Gesture-Only (Ultra Low Power)
- **CPU:** 80 MHz minimal tasks
- **WiFi:** Disabled
- **Microphone:** Disabled
- **Display:** Off (capacitive touch wakes)
- **Target Power:** **0.5-1 mA sustained**
- **Battery Life:** 2000-4000 hours (2-5 months)
- **Use Case:** Extended storage, emergency mode, charging pending

---

## 3. Power Optimization Techniques

### 3.1 AMOLED Display Gating (Highest Priority)

**Problem:** The boost converter remains enabled (GPIO 13 high) even when the display is off, wasting 25-35 mA.

**Solution:** Gate the boost converter based on display visibility.

```c
// firmware/main/display_manager.c (pseudocode)
#define LCD_EN_PIN 13

void display_on() {
    gpio_set_level(LCD_EN_PIN, 1);   // Enable boost converter
    lv_disp_trig_activity(NULL);      // Wake Littlevgl display
    esp_timer_start_once(display_dimmer_timer, 30000000);  // 30s dimmer
}

void display_off() {
    lv_disp_trig_activity(NULL);      // Notify Littlevgl
    gpio_set_level(LCD_EN_PIN, 0);   // Gate boost converter (CRITICAL)
    // Display consumes ~0.001 mA when boost is off
}

// Touch interrupt → wake from sleep
void touch_isr_handler(void *arg) {
    if (gpio_get_level(LCD_EN_PIN) == 0) {
        display_on();
        power_profile_current = PROFILE_ALWAYS_LISTENING;
    }
}
```

**Expected Savings:** 25-35 mA continuously → **saves ~13 days of battery per month**.

### 3.2 Dark UI Theme (Secondary Power Saver)

**Finding:** Dark mode on AMOLED reduces pixel power consumption by 30-58%.

**Implementation:**
- Default theme: Dark gray (#121212) background, off-white (#E0E0E0) text
- Avoid pure black/white to prevent halation for users with astigmatism
- Voice mode displays dark indicators: green ring (listening), blue (processing), white (speaking)
- Menu overlays use 80% opacity dark glass effect
- Status bar (1% of screen): always show battery, time, signal—uses minimal pixels

**Expected Savings:** 15-25 mA on average vs. light theme → **saves ~3-5 days per month**.

### 3.3 CPU Frequency Scaling

**Implementation:** Three CPU states based on power profile.

| State | Frequency | Core Count | Use Case |
|-------|-----------|-----------|----------|
| Turbo | 240 MHz | Dual-core | STT processing, LLM response wait |
| Normal | 160 MHz | Dual-core | Voice loop, WiFi operations |
| Eco | 80 MHz | Single-core | Display updates, idle listening |

```c
// firmware/main/power_manager.c (pseudocode)
void set_cpu_profile(power_profile_t profile) {
    if (profile == PROFILE_ALWAYS_LISTENING) {
        esp_pm_config_esp32s3_t pm_config = {
            .max_freq_mhz = 240,
            .min_freq_mhz = 160,
            .light_sleep_enable = true
        };
        esp_pm_configure(&pm_config);
    } else if (profile == PROFILE_PERIODIC) {
        pm_config.max_freq_mhz = 160;
        pm_config.min_freq_mhz = 80;
    } else {  // GESTURE_ONLY
        pm_config.max_freq_mhz = 80;
        pm_config.min_freq_mhz = 80;
    }
    esp_pm_configure(&pm_config);
}
```

**Expected Savings:** 30-40 mA (Turbo) → 15 mA (Eco) → **saves ~5-7 days per month** during non-active periods.

### 3.4 WiFi Power Management

**Strategy:** Aggressive WiFi gating during low-activity periods.

- **Always-Listening:** WiFi on, modem sleep between TX/RX
- **Periodic:** WiFi off except during wake cycles (2s TX, sleep 3s)
- **Gesture-Only:** WiFi off

```c
void wifi_periodic_wake() {
    // Wake every 30 seconds
    esp_wifi_start();           // ~3s to connect
    send_heartbeat();           // ~1s send
    esp_wifi_stop();            // ~1s shutdown
    // Total: 5s active, 25s sleep = 80% power savings
}
```

**Expected Savings:** 80-120 mA (full WiFi) → 0 mA (off) → **saves ~8-10 days per month** in periodic mode.

### 3.5 Microphone Duty Cycling

**Strategy:** For periodic mode, listen in short bursts.

```c
void mic_duty_cycle() {
    while (in_periodic_mode) {
        i2s_start(I2S_NUM_0);              // Listen for 2 seconds
        detect_wake_word_interrupt();
        i2s_stop(I2S_NUM_0);               // Sleep for 3 seconds
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}
```

**Expected Savings:** 5-8 mA continuous → 2-3 mA duty-cycled → **saves ~2-3 days per month**.

### 3.6 Voice Activity Detection (VAD)

**Implementation:** Use energy-based VAD to avoid processing silence.

```c
// Only process audio when energy > threshold
float energy = calculate_frame_energy(audio_frame);
if (energy > VAD_THRESHOLD) {
    esp_timer_start_once(activity_timeout, 5000000);  // 5s timeout
    cpu_set_profile(PROFILE_ALWAYS_LISTENING);  // Full power for STT
} else {
    cpu_set_profile(PROFILE_PERIODIC);  // Return to eco
}
```

**Expected Savings:** Avoids unnecessary CPU wake → **saves 1-2 days per month** in idle environments.

---

## 4. Battery Lifetime Projections

### Scenario A: Active Voice Assistant (6-hour workday)
```
Always-Listening (6h): 8 mA × 6h = 48 mAh/day
Eco Mode (18h):       1 mA × 18h = 18 mAh/day
Total Daily:          66 mAh/day
Battery:              2000 mAh
Lifetime:             30 days per charge
```

### Scenario B: Always-On (24/7)
```
Always-Listening (24h): 10 mA × 24h = 240 mAh/day
Battery:                2000 mAh
Lifetime:               8 days per charge
```

### Scenario C: Light Use (1h voice, 23h standby)
```
Always-Listening (1h):  10 mA × 1h = 10 mAh/day
Eco Mode (23h):         1 mA × 23h = 23 mAh/day
Total Daily:            33 mAh/day
Battery:                2000 mAh
Lifetime:               60 days per charge
```

---

## 5. Monitoring & Adaptive Power

### Real-Time Battery Dashboard

Display battery percentage on AMOLED:
- **75-100%:** Green, full features enabled
- **50-75%:** Yellow, suggest Eco Mode
- **25-50%:** Orange, auto-switch to Periodic
- **<25%:** Red, switch to Gesture-Only, disable WiFi

### Power Profiling Endpoint

Add `/power-stats` to power_optimizer service:
```json
{
  "current_profile": "always_listening",
  "battery_percent": 65,
  "estimated_hours_remaining": 4.2,
  "daily_usage_pattern": {
    "active_hours": 6,
    "idle_hours": 18,
    "sleep_hours": 0
  },
  "power_breakdown": {
    "cpu_percent": 35,
    "wifi_percent": 40,
    "display_percent": 20,
    "mic_percent": 5
  },
  "optimization_recommendations": [
    "Enable dark mode (save 3mA)",
    "Reduce WiFi polling from 30s to 60s (save 2mA)"
  ]
}
```

---

## 6. Implementation Roadmap

| Phase | Task | Priority | Est. Time |
|-------|------|----------|-----------|
| 1 | Gate AMOLED boost converter (LCD_EN GPIO 13) | **CRITICAL** | 2 hours |
| 2 | Implement three power profiles in firmware | **CRITICAL** | 4 hours |
| 3 | CPU frequency scaling per profile | High | 3 hours |
| 4 | WiFi power gating in modem sleep | High | 3 hours |
| 5 | Dark UI theme (LVG dark palette) | High | 2 hours |
| 6 | Microphone duty cycling for Periodic mode | Medium | 2 hours |
| 7 | VAD energy-based audio filtering | Medium | 3 hours |
| 8 | Battery dashboard on display | Medium | 2 hours |
| 9 | Real-time power monitoring service | Low | 4 hours |

**Total:** ~25 hours of firmware development before Phase 9 deployment.

---

## 7. Testing & Validation

### Hardware Test Points

1. **AMOLED Boost Converter Gating**
   - Measure GPIO 13 state with oscilloscope
   - Verify boost converter off = <1 mA display draw
   - Measure touch wake latency (target <100ms)

2. **Power Profile Transitions**
   - Measure current during profile switch
   - Verify no data loss during WiFi off
   - Check CPU frequency scaling with `esp-idf-monitor`

3. **Battery Lifetime Test**
   - Run 24/7 in Always-Listening mode, log current
   - Calculate battery depletion curve
   - Verify 8-12 mA average matches projections

### Software Tests (Docker/Firmware)

1. `power_optimizer` service: Predict battery life based on usage pattern
2. `device_config.json`: Store user power profile preference
3. Firmware logs: Record power transitions every 5 minutes

---

## 8. References

- [ESP32-S3 Hardware Power Consumption (Waveshare)](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/issues/6)
- [AMOLED Dark Mode Battery Savings](https://www.embedsbc.com/how-to-deploy-esp32-s3-amoled-hmi-m5stack-stopwatch/)
- [Low-Power Voice Assistant Design](https://www.embedded.com/design-considerations-for-low-power-always-on-voice-command-systems/)
- [ESP32 Power Optimization Guide](https://www.sunfounder.com/blogs/news/how-to-build-an-esp32-low-power-sensor-node-for-long-life-battery-iot-projects)
- [Voice Assistant Battery Optimization](https://inairspace.com/blogs/learn-with-inair/does-voice-command-drain-battery-hidden-costs-and-smart-power-saving-tips)

---

## 9. Decision Tree: Which Profile?

```
Are you going to use the device right now?
  ├─ YES
  │  ├─ Will you use voice frequently (>2 per 10min)?
  │  │  ├─ YES → Always-Listening (8-12 mA)
  │  │  └─ NO  → Periodic Wake (2-3 mA)
  │  └─ Do you want maximum responsiveness?
  │     ├─ YES → Always-Listening
  │     └─ NO  → Periodic Wake
  └─ NO
     └─ Use Gesture-Only mode (0.5 mA, touch to activate)
```

---

## Conclusion

By implementing this battery optimization strategy, we achieve:

- **Always-Listening:** 8-12 mA → 8-14 days per 2000mAh charge
- **Periodic Wake:** 2-3 mA → 30-40 days per charge
- **Gesture-Only:** 0.5-1 mA → 60+ days per charge

The key to success is **gating the AMOLED boost converter**, which alone saves 13+ days per month compared to always-on operation. Combined with dark UI, CPU scaling, and WiFi management, we achieve industry-leading battery efficiency for a voice assistant device.
