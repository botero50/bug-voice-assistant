# UI/UX Design Plan for ESP32-S3 1.75" AMOLED Round Voice Assistant

## Executive Summary

For a 1.75" (466×466px) circular AMOLED display with capacitive touch, we design a minimalist voice-first interface that prioritizes voice interaction over screen tapping. The UI is optimized for battery efficiency (dark theme), accessibility (high contrast, large touch targets), and natural conversation flow. This document outlines screens, interactions, and design patterns for all voice assistant skills.

---

## 1. Display Specifications & Constraints

| Property | Value | Implication |
|----------|-------|------------|
| **Size** | 1.75 inches diagonal | ~44mm, fits wrist-wear form factor |
| **Resolution** | 466×466 pixels | 267 PPI (crisp, no visible pixels) |
| **Shape** | Circular (1:1 aspect) | Edges are cut off; avoid corner UI |
| **Panel** | AMOLED | Dark theme saves 30-58% battery |
| **Touch** | Capacitive (5-point multi-touch) | Finger + swipe capable |
| **Color Space** | RGB 16M colors (OLED) | Full color support |
| **Refresh** | ~60 Hz | Smooth animations possible |
| **Brightness** | 0-500 nits (auto-dim 30s) | Bright outdoors, saves power dim indoors |

### Circular Screen Geometry

The circular display means:
- **Safe Area:** ~380px diameter centered circle (safest for key UI)
- **Edge Area:** 0-380px+ radius (watch indicator zone)
- **Avoid:** Content in corners—it gets cut off (only edges show ring)

```
       (233,0) — Top
         ↑
         │
  (0,233)← + →(466,233) — Center ring
         │
         ↓
      (233,466) — Bottom
```

---

## 2. Core Design System

### 2.1 Color Palette (Dark Theme)

**Primary:**
- **Background:** #121212 (near-black, save power)
- **Text:** #E0E0E0 (off-white, prevent halation)
- **Accent:** #1DB954 (Spotify green for voice listening state)
- **Secondary Accent:** #1E90FF (Cornflower blue for processing)

**States:**
- **Listening:** Green ring (#1DB954) pulsing at 60bpm
- **Processing:** Blue ring (#1E90FF) rotating clockwise
- **Speaking:** White ring (#FFFFFF) static or gentle glow
- **Error:** Red ring (#FF6B6B) steady
- **Idle/Sleep:** Dim ring #444444 (minimal power)

**Typography:**
- **Font:** Roboto or Segoe UI (system default)
- **Sizes:**
  - Large text: 24-28px (e.g., time, main status)
  - Normal text: 16-20px (e.g., voice response preview)
  - Small text: 12-14px (e.g., status bar)
- **Weight:** 400 (normal), 500 (headings), 700 (alerts)

### 2.2 Touch Targets

**Minimum touch target:** 1cm × 1cm = ~37×37 pixels (well above 44×44px standard).

**Common gestures:**
- **Tap:** 300ms hold, release → trigger (e.g., answer call)
- **Swipe up:** Dismiss current screen, return to home
- **Swipe down:** Open app menu
- **Swipe left/right:** Next/previous item in list
- **Long-press:** Context menu (delete, mute, etc.)

---

## 3. Screen Layouts & Flows

### 3.1 Home Screen (Always-On, Voice-First)

**Layout:**
```
        ┌─────────────────────────┐
        │      [Time: 3:45]       │  ← Status bar (6px height)
        │                         │
        │                         │
        │        ╱╲               │  ← Listening ring (pulsing green)
        │       ╱  ╲              │     Outer ring: wake word indicator
        │      │ ◉  │             │     Center: "Listening" text (or blank)
        │      │    │             │     Toggles based on power profile
        │       ╲  ╱              │
        │        ╲╱               │
        │                         │
        │   [◀ ◉ ▶]  Battery 73%  │  ← Bottom: navigation (small), battery
        │                         │
        └─────────────────────────┘

  ◀: Previous (swipe left)
  ◉: Home (press to return)
  ▶: Next (swipe right)
```

**Content:**
- **Top bar:** Time (large), signal strength (small icon), mute state
- **Center:** Listening indicator (animated ring)
  - Green pulsing = actively listening
  - Dim gray = idle, waiting
  - Touch to voice-activate if sleeping
- **Bottom bar:** Navigation dots (1 center, 0-2 sides for context)
- **Battery:** Display percentage color-coded (green >50%, yellow 25-50%, red <25%)

**Tap behaviors:**
- **Tap center ring:** Force listening state (useful if mic muted or in Gesture-Only mode)
- **Swipe left/right:** Cycle through apps (email, calendar, jokes, etc.)
- **Swipe up:** Go to settings
- **Swipe down:** Show recent commands/history
- **Long-press on ring:** Mute mic (shows gray ring)

### 3.2 Voice Feedback During STT/LLM/TTS

**Phase 1: Speech-to-Text (STT)**
```
        ┌─────────────────────────┐
        │      [3:45]  ◆          │  ◆ = Recording active
        │                         │
        │        ╱╲               │
        │       ╱  ╲              │
        │      │ ▓  │  ← Waveform │  Visual echo of speech
        │       ╲  ╱              │  (4-5 bars animated)
        │        ╲╱               │
        │                         │
        │    "I hear you..."      │  ← Transcription preview
        │                         │
        └─────────────────────────┘
```

**Interaction:**
- Green ring steadily glow (not pulsing—actively recording)
- Waveform bars animate based on audio energy (left/right channels)
- Show partial transcription as user speaks
- Max 2 lines of text to avoid UI clutter

**Phase 2: LLM Processing**
```
        ┌─────────────────────────┐
        │      [3:45]             │
        │                         │
        │        ╱╲               │
        │       ╱  ╲              │
        │      │ ⟳  │  ← Rotating │  Blue ring = thinking
        │       ╲  ╱              │  animation
        │        ╲╱               │
        │                         │
        │    "Thinking..."        │  ← Generic message
        │                         │
        └─────────────────────────┘
```

**Interaction:**
- Blue ring rotating (360° every 1.5s)
- Show spinner + "Thinking..." or "One moment..."
- Keep transcription preview visible below spinner (if space permits)

**Phase 3: Text-to-Speech (TTS)**
```
        ┌─────────────────────────┐
        │      [3:45]  ♪          │  ♪ = Audio output
        │                         │
        │        ╱╲               │
        │       ╱  ╲              │
        │      │ ◉  │  ← Glow      │  White ring glowing
        │       ╲  ╱              │  (steady or gentle
        │        ╲╱               │   pulse @ 2Hz)
        │                         │
        │    "Your email from..." │  ← Response preview
        │    "...is ready"        │  (2 lines max)
        │                         │
        └─────────────────────────┘
```

**Interaction:**
- White ring steady (or gentle 2Hz pulse)
- Show speaker icon (♪) to indicate audio output
- Display response text in speech-sized preview (16px font)
- Auto-scroll long responses (marquee style if >2 lines)

### 3.3 Email/Calendar Screens

**Email List View**
```
        ┌─────────────────────────┐
        │    [3:45] ✉ 3 new      │  ← Header: email icon + count
        │                         │
        │   ┌─────────────────┐   │
        │   │ Alex Jones      │ ◆ │  ← Sender, unread indicator
        │   │ Subject preview │   │  (swipe to expand)
        │   │ 2:30 pm         │   │  (time small)
        │   └─────────────────┘   │
        │                         │
        │   ┌─────────────────┐   │
        │   │ Manager         │   │  ← Next email (dimmed)
        │   │ Important upd...│   │
        │   │ 12:15 pm        │   │
        │   └─────────────────┘   │
        │                         │
        │    [◀ ◉ ▶] ← → ▲ ▼    │  ← Nav: prev, home, next
        │              arrows for
        │              scroll up/down
        └─────────────────────────┘
```

**Interactions:**
- **Tap email card:** Read full email (voice announces it)
- **Swipe left:** Delete email
- **Swipe right:** Mark as read
- **Swipe up/down:** Scroll through email list
- **Voice:** "Read my emails" → reads list, waiting for "Read email 1/2/3", "Delete", "Reply" voice commands

**Email Detail View (Full Read)**
```
        ┌─────────────────────────┐
        │  From: Alex Jones       │
        │  Subj: Project update   │
        │  2:30 pm                │
        │ ┌─────────────────────┐ │
        │ │                     │ │
        │ │ Hey team, we just   │ │  ← Email body (auto-scrolling
        │ │ launched the new    │ │     as TTS reads it)
        │ │ API endpoint. All   │ │
        │ │ tests passed.       │ │
        │ │                     │ │
        │ └─────────────────────┘ │
        │                         │
        │  [↶ Reply] [Del]        │  ← Action buttons (swipeable)
        └─────────────────────────┘
```

### 3.4 Calendar View

**Agenda (Next 24h)**
```
        ┌─────────────────────────┐
        │    [3:45] 📅 Today     │
        │                         │
        │  4:00 PM Team Standup   │  ← Time + event (16px)
        │         (Join now?)     │  ← Optional action (12px)
        │                         │
        │  6:30 PM Dentist appt   │  ← Future event (dimmed)
        │         (Downtown)      │
        │                         │
        │  Tomorrow:              │  ← Section divider
        │  9:00 AM All-hands      │
        │  2:00 PM 1:1 Manager    │
        │                         │
        │      [◀ ◉ ▶]           │
        └─────────────────────────┘
```

### 3.5 Settings Screen

**Compact Settings (No Submenu)**
```
        ┌─────────────────────────┐
        │  [Back ✕] Settings      │
        │                         │
        │  ☀ Brightness: [===─]   │  ← Slider (swipe to adjust)
        │                         │
        │  🔊 Volume: [════════]  │
        │                         │
        │  🎤 Mic: [toggle ◉ ]    │  ← Tap to toggle on/off
        │                         │
        │  ⚡ Power Profile       │  ← Tap for 3-option menu
        │     Always-Listening    │
        │                         │
        │  🌐 WiFi: [toggle ◉ ]   │
        │                         │
        │  🔋 Battery: 73%        │
        │                         │
        │     [Swipe up to back]  │
        └─────────────────────────┘
```

**Power Profile Selector (Voice-Controlled)**
```
Voice: "Change power profile"
Device: Shows circular menu:

        ┌─────────────────────────┐
        │  Power Profile          │
        │                         │
        │      [Always]           │  ← Listening (selected: bold)
        │   ╱ Listening ╲         │
        │  ╱             ╲        │
        │ │ Power Profile │       │  ← Center label
        │ ╱               ╲       │
        │  ╲ Gesture-Only ╱       │  ← Gesture-Only (dimmed)
        │   ╲             ╱       │
        │      [Periodic]        │  ← Periodic (tap to select)
        │                         │
        │   Swipe to select       │  ← Helper text
        │   Voice: say profile    │
        │                         │
        └─────────────────────────┘
```

---

## 4. Accessibility Features

### 4.1 High Contrast Design

- **Text:** Off-white (#E0E0E0) on dark (#121212) → ~15:1 contrast ratio (exceeds WCAG AAA standard)
- **Interactive elements:** Use distinct colors (green for listening, blue for processing, red for errors)
- **Focus indicators:** 2px white border around tapped element (lasts 200ms)

### 4.2 Large Touch Targets

- **Minimum 44×44px:** All buttons, interactive rings
- **Cards/Rows:** 60×60px minimum (emails, calendar events)
- **Spacing:** 8px between touchable elements

### 4.3 Voice-First Navigation

All screens should be operable via voice:
- "Read my emails" → Email list
- "Show calendar" → Calendar view
- "Settings" → Settings screen
- "Go back" → Previous screen
- "Home" → Return to home screen

### 4.4 Accessible Error Messages

- **Speech:** Speak error message clearly
- **Visual:** Red ring + text message (not just color)
- **Recovery:** Always offer 3 attempts before failure, suggest alternative (e.g., "Try saying it again, or open the app on your phone")

---

## 5. Animation & Transitions

### 5.1 State Transitions (Power Profile)

**Always-Listening → Periodic:**
```
1. Fade home ring to 50% brightness (200ms ease-out)
2. Dim status bar (100ms)
3. Show "Periodic mode" toast (2s auto-dismiss)
```

**Display Off → On (Touch):**
```
1. Fade from black to full brightness (300ms linear)
2. Show listening ring (200ms fade-in)
3. Re-enable touch input
```

### 5.2 Voice Flow Animations

**STT (Speech-to-Text):**
- Waveform bars animate smoothly (50ms refresh, 60fps)
- Energy-based bar heights (0-100px per bar)
- Use left/right stereo channels for dual-bar visualization

**LLM Processing:**
- Blue ring rotates clockwise (1.5s per rotation)
- Smooth easing (cubic-in-out)

**TTS (Text-to-Speech):**
- White ring pulses gently (0.5-1.0 opacity, 2Hz frequency)
- Text scrolls smoothly (marquee if >2 lines, 30px/s)

### 5.3 List Navigation

**Swipe left/right:**
- Card slides out (200ms ease-out)
- New card slides in from opposite side (200ms ease-out)
- Snap to final position

---

## 6. Skill-Specific UI Layouts

### 6.1 Jokes

```
        ┌─────────────────────────┐
        │      [3:45] 😄         │
        │                         │
        │   Setup:                │
        │   Why did the CLI go    │
        │   to school?            │
        │   ...                   │
        │                         │
        │  [Next Joke] [Category] │  ← Voice: "next joke"
        │                         │
        └─────────────────────────┘
```

### 6.2 Book Reader

```
        ┌─────────────────────────┐
        │  The Hobbit             │
        │  Chapter 3: A Short     │  ← Book + chapter title
        │  Rest - Page 42/180     │  ← Progress
        │                         │
        │  "In a hole in the      │  ← Current paragraph
        │   ground there lived    │  ← (auto-scroll with TTS)
        │   a hobbit."            │
        │                         │
        │  [|◄ ⏸ ►|] Speed: 1x   │  ← Playback controls
        │  [Prev] [Next Chapter]  │
        │                         │
        └─────────────────────────┘
```

### 6.3 Radio

```
        ┌─────────────────────────┐
        │  📻 Radio               │
        │                         │
        │  Streaming: BBC Radio 1 │  ← Station name
        │  ♫ The Weeknd           │  ← Now playing (scrolls if long)
        │                         │
        │  [Thumbs ↑] [⏹] [Skip→]│  ← Favorite/Stop/Next
        │                         │
        │  Stations:              │  ← Swipe up/down
        │  • BBC Radio 1          │  ← Current (bold)
        │  ○ BBC Radio 2          │
        │  ○ Classic FM           │
        │                         │
        └─────────────────────────┘
```

### 6.4 Crypto Prices

```
        ┌─────────────────────────┐
        │  💹 Crypto               │
        │                         │
        │  Bitcoin    $62,450     │  ← Symbol + price (16px)
        │  ↑ 2.3% today          │  ← Change (green ↑/red ↓)
        │                         │
        │  Ethereum   $2,580      │  ← Next crypto (dimmed swipe)
        │  ↓ 0.8% today          │
        │                         │
        │  [Refresh] [Add Watch]  │
        │                         │
        │  Last update: 3:40 PM   │
        │                         │
        └─────────────────────────┘
```

### 6.5 Story Generator

```
        ┌─────────────────────────┐
        │  📖 Story Time           │
        │                         │
        │  Once upon a time, in   │  ← Auto-scrolling text
        │  a land far away...     │     during TTS playback
        │                         │
        │  [⏸] [Continue] [New]   │  ← Playback controls
        │                         │
        │  Generated by Claude    │
        │                         │
        └─────────────────────────┘
```

---

## 7. Error States & Recovery

### 7.1 WiFi Disconnected

```
        ┌─────────────────────────┐
        │      [3:45] ⚠           │
        │                         │
        │        ╱╲               │
        │       ╱  ╲              │
        │      │ ✕  │  ← Red ring  │
        │       ╲  ╱              │
        │        ╲╱               │
        │                         │
        │   WiFi disconnected     │
        │   Trying to reconnect   │  ← Spoken + shown
        │                         │
        │   [Retry] [Settings]    │
        │                         │
        └─────────────────────────┘
```

### 7.2 Microphone Error

```
        ┌─────────────────────────┐
        │      [3:45] ⚠           │
        │                         │
        │        ╱╲               │
        │       ╱  ╲              │
        │      │ 🔇 │  ← Mute icon │
        │       ╲  ╱              │
        │        ╲╱               │
        │                         │
        │   Mic unavailable       │
        │   Check device, restart │
        │                         │
        │   [Troubleshoot] [OK]   │
        │                         │
        └─────────────────────────┘
```

### 7.3 Battery Low

```
        ┌─────────────────────────┐
        │      [3:45] 🔴          │
        │                         │
        │   Battery: 5%           │
        │   Gesture-only mode     │  ← Auto-switched
        │   Touch to use voice    │
        │   Plug in when done     │
        │                         │
        │      [Charge] [OK]      │
        │                         │
        └─────────────────────────┘
```

---

## 8. Responsive UI Framework (Littlevgl)

**Littlevgl (LVG) Configuration for circular 466×466 display:**

```c
// firmware/main/ui_config.h
#define LV_HOR_RES_MAX 466
#define LV_VER_RES_MAX 466

// Dark theme (power efficient)
lv_theme_t *theme = lv_theme_dark_init(
    lv_color_make(18, 18, 18),    // #121212 background
    lv_color_make(224, 224, 224)  // #E0E0E0 text
);
lv_theme_set_current(theme);

// Circular screen clip
lv_obj_t *root = lv_scr_act();
lv_obj_set_size(root, 466, 466);
lv_obj_add_style(root, &bg_style, 0);
```

**Key LVG Objects:**
- `lv_obj_t` – Base container (home screen, settings)
- `lv_label_t` – Text display (email subjects, responses)
- `lv_btn_t` – Buttons (minimalist, no background for dark mode)
- `lv_chart_t` – Waveform visualization (STT feedback)
- `lv_arc_t` – Animated rings (listening/processing/speaking)
- `lv_list_t` – Email/calendar lists (swipe-enabled)

---

## 9. Typography & Sizing Guide

| Element | Font Size | Weight | Color | Usage |
|---------|-----------|--------|-------|-------|
| **Time** | 28px | 500 | #E0E0E0 | Home screen, status bar |
| **Headline** | 20px | 500 | #E0E0E0 | Screen titles (Email, Calendar) |
| **Body Text** | 16px | 400 | #E0E0E0 | Email subjects, transcription |
| **Small Text** | 12px | 400 | #999999 | Times, metadata |
| **Error** | 16px | 500 | #FF6B6B | Error messages |
| **Status** | 14px | 400 | #1DB954 | Green listening state |

---

## 10. Implementation Roadmap

| Phase | Task | Priority | Est. Time |
|-------|------|----------|-----------|
| 1 | Set up LVG dark theme + circular clip | **CRITICAL** | 3 hours |
| 2 | Home screen layout (rings, time, battery) | **CRITICAL** | 4 hours |
| 3 | Voice feedback animations (STT/LLM/TTS) | **CRITICAL** | 5 hours |
| 4 | Email list + detail view | High | 4 hours |
| 5 | Calendar agenda view | High | 3 hours |
| 6 | Settings screen + power profile selector | High | 3 hours |
| 7 | Skill-specific screens (jokes, radio, crypto) | Medium | 6 hours |
| 8 | Error states + recovery flows | Medium | 3 hours |
| 9 | Accessibility testing (contrast, touch targets) | Medium | 2 hours |
| 10 | Animation polish + performance tuning | Low | 3 hours |

**Total:** ~36 hours of UI development before Phase 9 deployment.

---

## 11. Testing & Validation

### Hardware Testing

1. **Color Accuracy**
   - Verify dark gray (#121212) vs. black for power savings
   - Test text contrast on device (>12:1 ratio)
   - Measure brightness levels (nits) at 0%, 50%, 100%

2. **Touch Responsiveness**
   - Measure tap latency (target <100ms)
   - Test multi-touch swipe gestures (5-point)
   - Verify long-press detection (300ms threshold)

3. **Circular Screen Clipping**
   - Visual inspection: No content in corners
   - Check edge ring displays correctly
   - Verify status bar at top (0-50px)

### User Testing

1. **Voice Navigation**
   - Can users navigate using voice alone? (blind test)
   - Are button labels clear and discoverable?
   - Error recovery intuitive?

2. **Readability**
   - Can users read text from 30cm distance?
   - Is font size sufficient for older users?
   - High contrast adequate?

3. **Performance**
   - Frame rate >55 FPS during animations
   - No lag during swipes or taps
   - Memory usage <50% of 520MB PSRAM

---

## 12. References & Inspiration

- [Voice UI Design 2026 Best Practices](https://fuselabcreative.com/voice-user-interface-design-guide-2026/)
- [AMOLED Dark Mode Battery Savings](https://www.aufaitux.com/blog/voice-user-interface-design-best-practices/)
- [Mobile UI Best Practices](https://www.uxpin.com/studio/blog/what-is-mobile-ui/)
- [Littlevgl (LVG) Documentation](https://lvgl.io/)
- [Accessibility in Voice Interfaces](https://dianapps.com/blog/guide-for-designing-a-voice-user-interface-for-your-product/)

---

## Conclusion

This UI/UX design prioritizes:

1. **Voice First:** All features accessible via voice commands, not just touch
2. **Battery Efficient:** Dark theme, minimal animations, fast sleep-to-wake
3. **Accessible:** High contrast, large touch targets, clear error recovery
4. **Delightful:** Smooth animations, visual feedback, responsive interactions
5. **Circular Display:** Optimized for 466×466px round AMOLED, avoiding clipped corners

The design supports all existing skills (email, calendar, jokes, radio, crypto) and future expansions with a consistent interaction paradigm: **listen → respond → wait for next command**. This keeps the interface simple while maintaining full functionality.
