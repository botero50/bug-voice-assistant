# UI/UX Design Plan for ESP32-S3 1.75" AMOLED Round Voice Assistant

## Executive Summary

For a 1.75" (466×466px) circular AMOLED display with capacitive touch, we design a minimalist voice-first interface that prioritizes voice interaction over screen tapping. The UI is optimized for battery efficiency (dark theme), accessibility (high contrast, large touch targets), and natural conversation flow. This document outlines screens, interactions, and design patterns for all voice assistant skills.

### Working agreement

- Each phase is built and flashed to the board over USB, then tested by the owner on the real device.
- Nothing is committed or pushed to GitHub when a phase finishes. The owner decides when code goes to GitHub.
- The skills are voice-first: **saying what you want opens the screen built for it** (see section 3.0). There are no icon pages and no swiping between apps.

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

**Gestures (as built):**
- **Tap:** open a card or press a button; tap the Home ring to listen without the wake word
- **Long-press the Home ring:** mute or unmute the microphone
- **Swipe up on Home:** Settings
- **Swipe down on Home:** History of recent commands
- **Swipe up on any other screen:** dismiss it and return Home
- **Swipe right:** back one step (email detail → list, Wi-Fi list → Settings, keyboard → cancel)
- No horizontal swiping between apps. Skill screens are opened by voice.

---

## 3. Screen Layouts & Flows

### 3.0 Voice Opens the Screen

The assistant is voice-first, so there are no app icons, no pages to swipe between and no menu of apps. Each skill has **one screen built for it**, and that screen opens when the user asks for the skill. The server decides which screen goes with an answer and sends the data for it; the board only draws it.

| User says | What happens |
|---|---|
| "Hola Bug, muéstrame los correos" | The email list opens while Bug summarizes it |
| "Lee el primero" (segundo, tercero…) | The email opens and Bug reads it |
| "¿Qué tengo en el calendario?" / "Mi agenda" | The agenda opens (today and tomorrow) |
| "¿Cómo está el clima?" / "¿Va a llover en Cali?" | The weather screen opens (real data; your city unless one is named) |
| "Pon salsa" / "Pon Tropicana" / "Pon música" | The music screen opens and the radio plays |
| (tap the ring) "Para la música" / "Otra emisora" / "Sube el volumen" | Controls the radio; the music resumes after each command |
| "Cambia la ciudad a Cali" / "Detecta mi ciudad" | Saves the city, or goes back to automatic |
| "¿Qué hora es?" / "¿Qué día es hoy?" | The answer shows on the Home ring; no extra screen needed |
| "Inicio" / "Cierra" / "Volver" | Back to Home |

Rules:
1. Voice opens a screen; touch works inside it (tap a card to open it, swipe to go back).
2. The screen stays until it is dismissed (swipe up, or "Inicio") or 60 seconds pass with no touch and no command, then it returns Home.
3. While the user talks to Bug on a skill screen, a small banner replaces the clock ("Te escucho…", "Pensando…", "Hablando…"). The skill screen stays visible.
4. If an answer needs no screen, the Home screen shows it.
5. Screens that show sample data say "Datos de ejemplo" until the real account is connected.
6. Skill screens by phase: Email and Calendar first; then Weather and Music (the owner's original asks); then jokes, radio, crypto prices, the book reader and the story generator from section 6.

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

  (As built, Home has no ◀ ◉ ▶ buttons and no page dots: skill screens open by voice.
   The bottom shows only the battery.)
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
- **Tap center ring:** Listen right now, without the wake word (also a way in when the wake word isn't picked up)
- **Swipe up:** Go to settings
- **Swipe down:** Show recent commands/history
- **Long-press on ring:** Mute mic (shows a dim ring and a muted icon)
- Left/right swipes do nothing on Home; skill screens are opened by voice (section 3.0)

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
- **Voice:** "Muéstrame los correos" opens this list while Bug summarizes it; "lee el primero" opens that email and Bug reads it
- **Tap a card:** open the full email
- **Swipe up:** back Home; **swipe right** on an open email goes back to the list
- The list shows the newest 4 plus "+N más". It never scrolls, so swiping up always means "back"
- Reply and Delete arrive when the real mailboxes are connected. They are not drawn before then, to avoid buttons that do nothing
- Until then the footer says "Datos de ejemplo"

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

**Agenda (Today and tomorrow)**

Opened by voice: "¿Qué tengo en el calendario?", "mi agenda", "mis reuniones". Bug reads today's events aloud while the screen shows both days. Swipe up to go back. The footer says "Datos de ejemplo" until the real calendars are connected.
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

### 6.0 Weather (built)

Opened by voice: "¿cómo está el clima?", "¿va a llover?", "¿qué clima hace en Medellín hoy?". The data is real (Open-Meteo, free, no account). With no city named it uses Bogotá (`BUG_CITY` on the server changes the default); naming any city looks it up.

```
        ┌─────────────────────────┐
        │        Medellín         │  ← city (small, gray)
        │          ☀              │  ← icon drawn from shapes: clear, partly,
        │          24°            │     cloudy, rain, storm, fog
        │      Tormenta eléctrica │  ← condition
        │ Máx 25° · Mín 18° · 100%│  ← today's range and rain chance
        │                         │
        │  Dom      Lun     Mar   │  ← next three days
        │  ☂        ☂       ☂     │
        │ 28° 18°  30° 18°  30° 17°│
        └─────────────────────────┘
```

Bug also says the temperature, the range and the chance of rain aloud. Swipe up to go back Home.

### 6.0b-3 Calendar from ICS links, book reader, volume by number (built)

**Calendar.** The Calendar screen is unchanged; its events now come from your real calendars when
you give Bug the links. Put one link (or .ics file path) per line in `server/calendar_sources.txt`
(never committed) or in the `BUG_ICS_URLS` variable. Google Calendar: Settings of the calendar >
"Secret address in iCal format". Outlook: Settings > Calendar > Shared calendars > Publish a calendar
(ICS link). Without links the sample events stay. Handles repeating events, all-day events and time
zones; a moved or cancelled single occurrence of a repeating event is not handled.

**Books.** Put .txt or .epub files in `Documentos\Libros` (or `BUG_BOOKS_DIR`). Say "lee un libro",
"lee el libro Cien años" (part of the title is enough), then "sigue leyendo" for the next page and
"repite la página". Each page is about 330 characters, shown on the Article screen with
"Página n de N" and read aloud. The place is saved per book in `server/reading_state.json`.

**Volume by number.** "Pon el volumen al 50 por ciento" (digits or tens such as "cincuenta").

### 6.0b-2 Music from this PC (built)

Same Music screen as the radio, but the top line says "Mi música", the big line is the song title
and the small line the artist. The songs are the files in `~/Music` (or the folders in the
`BUG_MUSIC_DIR` variable, separated by `;`): mp3, m4a, aac, wav, flac, ogg, opus, wma.

| Say | What happens |
|---|---|
| "Pon mi música" | Every song, shuffled |
| "Pon la canción de Shakira" / "pon mi música de Carlos Vives" | Songs whose artist, title or folder match (spelling may be off) |
| "Siguiente canción" / "canción anterior" | Changes song; the title on screen updates |
| "Para la música", tap Detener | Stops |

The server plays one song per connection and the board reconnects when it ends, so the next song
starts about a second later and its title arrives in the response headers. Bug talking pauses the
music and it resumes at the same second of the same song. "Otra emisora" still changes radio station.
Audio is 16 kHz mono like the radio.

### 6.0b Music: internet radio (built)

Opened by voice: "pon música", "pon salsa", "pon Tropicana", "pon Olímpica". Stations come from Radio Browser, a free community directory (no account); stations from the user's own city come first. The PC server decodes the stream and sends the board plain audio (16 kHz mono, so it sounds like AM radio for now).

```
        ┌─────────────────────────┐
        │          Radio          │
        │ Olímpica Stereo (Medellín)│  ← station
        │     Antioquia · salsa   │
        │        ╭───────╮        │
        │        │ ▂▅▇▅▂ │        │  ← equalizer follows the real audio
        │        ╰───────╯        │
        │  Toca el círculo para   │
        │        hablar           │
        │      [ ■ Detener ]      │
        └─────────────────────────┘
```

- **Mic and music:** while the radio plays, the board does not listen for "Hola Bug" (it would hear the music). **Tap the ring** to talk: the music pauses, Bug listens and answers, and the music resumes.
- Voice while it plays (after tapping): "para la música", "otra emisora", "sube el volumen", "baja el volumen", "pon vallenato".
- Swipe up goes Home and the music keeps playing; Home shows a purple ring and the station name.
- Later: stream quality (stereo, higher sample rate), the current song title, and your own MP3 files.

### 6.0c The user's city (built)

Weather, and the local stations first in the radio search, use the user's city. It is found automatically from the internet connection (ip-api.com). Saying "cambia la ciudad a Cali" saves a city on the server (`server/user_settings.json`), and "detecta mi ciudad" goes back to automatic. "¿En qué ciudad estoy?" tells which one is in use and why.

### 6.0d Skills that need no account or key (built)

Every one of these opens by voice, with a screen made for it. Their data comes from free services or from this PC, never from Claude.

| Say | Screen | Data |
|---|---|---|
| "¿Cuánto está el bitcoin?" | **Prices**: Bitcoin, Ethereum, Solana, in dollars with the 24 h change (green up, red down) and the value in pesos | CoinGecko |
| "¿A cuánto está el dólar?" | **Prices**: dollar and euro in pesos | open.er-api.com |
| "¿Cómo está mi computador?" | **Prices screen layout**: processor, memory, graphics card (with temperature) and disk of this PC | psutil and nvidia-smi on this PC |
| "Dame las noticias" | **Headlines**: four headlines with their sources | Google News (Colombia) |
| "¿Quién fue Gabriel García Márquez?" | **Article**: title and the first sentences | Wikipedia in Spanish |
| "Cuéntame un chiste" | **Article** (large, centered text) | 20 built-in jokes |
| "¿Cuánto es 25 más 17?", "convierte 5 millas a kilómetros", "lanza un dado", "tira una moneda" | None: Bug just answers (also on Home) | Computed on the server |

Conversions cover length, weight, volume, temperature and dollars, euros and pesos with the live rate.

Swipe up goes back Home, as on every skill screen.

### 6.0e Timers, alarms and reminders (built)

Say: "pon un temporizador de 10 minutos", "despiértame a las 6 y media de la mañana", "recuérdame en 20 minutos llamar a Ana", "¿qué temporizadores tengo?", "cancela el temporizador". Bug confirms aloud, and a list screen shows what is set when asked.

- **The board keeps them.** The server only understands the request and keeps the list; each entry is also saved in the board's own memory. They ring on time even if the PC is off, and survive restarts. An entry missed by more than five minutes while the board was off is dropped without ringing.
- **When one rings:** a burst of three beeps every second for up to a minute (the volume goes to at least 85 % and any radio pauses), and the screen shows a red pulsing ring with a bell, what it is for, and a Stop button. Tap the ring or Stop, or swipe up, to stop it. The radio then resumes.
- While it rings, the mic does not listen for the wake word, so the beeps aren't sent to the server.

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

Status as of 2026-11-07. Every phase is flashed to the board and tested by the owner; nothing is pushed to GitHub unless the owner asks.

| Phase | Task | Status |
|-------|------|--------|
| 1 | Dark theme, circular layout, Spanish font | Done |
| 2 | Home screen: ring, transcript, answer, clock, battery | Done |
| 3 | Voice feedback: waveform, spinner, pulsing ring | Done |
| 4 | Touch, swipes, tap to listen, long-press mute | Done |
| 5 | Settings (brightness, volume, mic, Wi-Fi, server) and Wi-Fi setup with keyboard | Done |
| 6 | History (swipe down) | Done |
| 7 | **Voice opens screens**: server sends the screen and its data (section 3.0) | Done |
| 8 | **Email** list and detail, **Calendar** agenda (sample data until accounts connect) | Done |
| 9 | **Weather** screen (real data) | Done |
| 9b | **Music** screen (internet radio), volume by voice, the user's city | Done |
| 9c | Music: your own MP3 files, stereo and better sound, song titles | Later |
| 10 | Crypto and dollar prices, news, Wikipedia answers, jokes, math and conversions, this PC's status | Done |
| 10b | Timers, alarms and reminders (the board keeps them and rings even with the PC off) | Done (alarm sound and auto-close fixed) |
| 10c | Music from your own files on this PC | Built, waiting for your test |
| 10d | Real calendar from ICS links (built, needs your links) and email (OAuth sign-in) for the Google and Outlook work accounts | Calendar built, email later |
| 10e | Book reader (txt and epub, one page at a time) | Built, waiting for your test |
| 10f | Volume by number ("volumen al 50") | Built |
| 10f | Story generator and open questions (needs a Claude API key) | Waiting for a key |
| 11 | Connect real Google and Outlook mail and calendar | Later |
| 12 | Error screens (low battery, mic fault), power profiles | Later |
| 13 | Accessibility testing, animation polish, performance | Later |

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
