# GPIO Pinout Reference
## ESP32-S3 AMOLED Round Display - Pin Configuration

**Hardware:** ESP32-S3 1.75" AMOLED 466×466px  
**Touch Controller:** GT911 capacitive touch  
**Microphone:** INMP441 I2S digital microphone  
**Power Management:** Boost converter (GPIO 13 control)

---

## 📌 Pin Allocation Summary

| Function | GPIO | Mode | Notes |
|----------|------|------|-------|
| **Microphone (I2S)** | | | |
| MCLK | 0 | I2S_MCLK | Master clock |
| BCLK | 4 | I2S_BCK | Bit clock |
| WS (LRCLK) | 5 | I2S_WS | Word select |
| DIN | 35 | I2S_DIN | Data input |
| | | | |
| **Display (I2C + GPIO)** | | | |
| SDA | 48 | I2C_SDA | Touch/Display I2C |
| SCL | 47 | I2C_SCL | Touch/Display I2C |
| Reset | 17 | Output | Display reset (low active) |
| Boost Enable | 13 | Output | AMOLED boost converter gating |
| | | | |
| **Audio Output (Future)** | | | |
| DAC1 | 17 | DAC | Could use for feedback (optional) |
| | | | |
| **Reserved** | | | |
| SPI (Flash) | 36, 37, 38 | SPI_IO | Internal Flash |
| UART (Debug) | 43, 44 | UART0 | Serial console |

---

## 🎤 Microphone Configuration (I2S)

**Device:** INMP441 MEMS Microphone  
**Interface:** I2S (Inter-IC Sound) digital

```
ESP32-S3          INMP441
─────────────────────────
GPIO 0 (MCLK) ──► MCLK (Master Clock)
GPIO 4 (BCLK) ──► CLK  (Bit Clock)
GPIO 5 (WS)   ──► L/R  (Left/Right = Word Select)
GPIO 35 (DIN) ──► SD   (Serial Data)
GND           ──► GND
3.3V          ──► VDD
```

**I2S Parameters:**
- **Sample Rate:** 16 kHz
- **Channels:** 1 (mono)
- **Bits per Sample:** 16-bit
- **Format:** Left-aligned, LSB first
- **Buffer Size:** 512 samples

**Code Reference:** `firmware/main/audio_driver.c`

---

## 🖥️ Display Configuration (I2C)

**Device:** 466×466px AMOLED Round Display  
**Touch:** GT911 Capacitive Touch Controller  
**Interface:** I2C

```
ESP32-S3          Display (IPS/OLED Driver)
─────────────────────────────────────
GPIO 48 (SDA) ──► I2C_SDA
GPIO 47 (SCL) ──► I2C_SCL
GPIO 17 (RST) ──► RESET (low = reset)
GPIO 13 (EN)  ──► BOOST_EN (boost converter enable)
GND           ──► GND
3.3V          ──► VDD
5V            ──► VOUT (from USB, to boost converter input)

GT911 Touch Controller (on same I2C bus):
─────────────────────────────────────────
I2C Address: 0x5D (or 0x5C depending on bridge pin)
GPIO 16 (INT) ──► INT (optional, interrupt)
GND           ──► GND
3.3V          ──► VDD
```

**I2C Parameters:**
- **Speed:** 400 kHz (standard) or 1 MHz (fast)
- **Address (Touch):** 0x5D
- **Display Resolution:** 466×466 pixels (RGB565)
- **Color Depth:** 16-bit RGB565

**Code Reference:** `firmware/main/display_driver.c`

---

## ⚡ Power Management

### Boost Converter Gating (GPIO 13)

**Purpose:** Turn off AMOLED boost converter when display is off  
**Savings:** 25-35 mA (biggest single optimization)

```
GPIO 13 Logic:
HIGH  = Boost converter ON  → AMOLED on, ~20-35mA
LOW   = Boost converter OFF → AMOLED off, display dark but <1mA

Typical Usage:
- Display ON:  GPIO 13 = HIGH
- Display OFF: GPIO 13 = LOW
```

**Implementation:**
```c
// firmware/main/display_driver.c
#define BOOST_CONVERTER_GPIO 13

void boost_converter_enable(void) {
  gpio_set_level(BOOST_CONVERTER_GPIO, 1);  // Enable
}

void boost_converter_disable(void) {
  gpio_set_level(BOOST_CONVERTER_GPIO, 0);  // Disable
}
```

**Testing:**
- Measure current with boost ON: ~20-35mA
- Measure current with boost OFF: <1mA
- Should see ~25mA+ difference

---

## 🔌 Power Measurements

**Recommended Multimeter Setup:**

```
+5V ┬──► Device VCC
    │
    [Multimeter]  Set to: DC Amperes (A)
    │             Range: 0-2A
    │
   GND ──► Device GND
```

**Expected Currents:**

| Mode | Activity | Expected (mA) | Range |
|------|----------|---------------|-------|
| Always-Listening | Idle | 8-12 | 7-15 |
| Always-Listening | Processing | 80-150 | 70-200 |
| Periodic (wake) | Checking | 2-3 | 1-5 |
| Periodic (sleep) | Sleeping | 0.1-0.5 | <1 |
| Gesture-Only | Off | 0.5-1 | <2 |
| Gesture-Only | Active | 50-100 | 40-120 |

**Measurement Procedure:**

1. **Boot Current:**
   - Power on device
   - Measure immediately
   - Should see spike to 150-200mA, settle to 10-15mA

2. **Listening State:**
   - Let device settle for 2 minutes
   - Measure without activity
   - Record 10 readings over 1 minute

3. **Processing State:**
   - Trigger voice command
   - Measure during "processing" phase
   - Should spike to 80-150mA

4. **Sleep State:**
   - Switch to Periodic or Gesture-Only mode
   - Let device sleep 5 minutes
   - Measure after sleep settled
   - Should drop to <1mA

---

## 📊 GPIO Summary Table

```
GPIO  | Function        | Mode      | Notes
────────────────────────────────────────────
0     | MCLK            | I2S       | Microphone master clock
4     | BCLK            | I2S       | Microphone bit clock
5     | WS              | I2S       | Microphone word select
13    | BOOST_EN        | Output    | Boost converter enable (HIGH=on)
16    | TOUCH_INT       | Input     | Touch interrupt (optional)
17    | DISPLAY_RST     | Output    | Display reset (LOW=reset)
35    | I2S_DIN         | I2S       | Microphone data input
43    | UART_TX         | UART      | Serial console (debug)
44    | UART_RX         | UART      | Serial console (debug)
47    | I2C_SCL         | I2C       | Display + Touch I2C clock
48    | I2C_SDA         | I2C       | Display + Touch I2C data
```

---

## 🔍 Verification Checklist

When device arrives, verify each pin:

```bash
# 1. I2C Bus Test (Display + Touch)
idf.py idf_monitor
# Try to detect devices:
# i2cdetect -y 0
# Should see 0x5D (GT911 touch controller)

# 2. I2S Microphone Test
# Record audio and verify it works

# 3. Boost Converter Test
# Measure current with GPIO 13 HIGH and LOW
# Should see >25mA difference

# 4. Display Test
# Should show boot animation
# Try GPIO 17 (RST) - should reset display

# 5. UART Debug
# Should see boot messages at 115200 baud
```

---

## ⚠️ Common Mistakes to Avoid

❌ **Wrong:** Connecting MCLK to GPIO that doesn't support I2S_MCLK  
✅ **Right:** Use GPIO 0 for MCLK (only reliable option on ESP32-S3)

❌ **Wrong:** I2C address 0x5C instead of 0x5D for GT911  
✅ **Right:** Check board - may have address selection jumper

❌ **Wrong:** Not enabling boost converter at startup  
✅ **Right:** GPIO 13 HIGH in display_driver_init()

❌ **Wrong:** Measuring display current without boost converter gating  
✅ **Right:** Toggle GPIO 13 to measure savings

❌ **Wrong:** Forgetting to configure GPIO 17 (RST) as output  
✅ **Right:** Set direction to OUTPUT, keep HIGH (active LOW)

---

## 📚 Reference Files

| File | Purpose |
|------|---------|
| `firmware/main/audio_driver.c` | I2S microphone configuration |
| `firmware/main/display_driver.c` | I2C display + boost control |
| `firmware/main/power_optimizer.c` | Power measurement code |
| `firmware/main/ui_config.h` | Display constants (resolution, etc.) |

---

## 🧰 Testing Tools Needed

- **Multimeter:** DC current measurement (0-2A range)
- **USB Cable:** USB-C for flashing and power
- **Scope/Logic Analyzer** (optional): Verify I2S/I2C signals
- **Current Clamp** (optional): For high current spikes

---

## 📞 Troubleshooting

**"Display not initializing"**
- Check GPIO 17 (RST) is HIGH
- Check GPIO 13 (BOOST) is HIGH
- Verify I2C communication (use i2cdetect)

**"No audio input"**
- Check all I2S GPIOs connected
- Verify sample rate is 16 kHz
- Check microphone orientation (has input side)

**"High idle current (>20mA)"**
- Check GPIO 13 is being controlled
- Verify boost converter is turning off
- Check for active WiFi

**"Touch not responding"**
- Verify I2C communication to GT911 (0x5D)
- Check GPIO 16 (INT) if used
- Try calibration sequence

---

## ✅ Pre-Deployment Checklist

Before connecting device to power:
- [ ] All pins soldered correctly
- [ ] No short circuits visible
- [ ] Microphone oriented correctly
- [ ] MicroSD card inserted
- [ ] Display showing no cracks
- [ ] Boost converter circuit present
- [ ] USB cable plugged into computer (not yet device)

---

**Reference:** GPIO pinout from ESP32-S3 DevKitC and device schematic  
**Last Updated:** November 4, 2026
