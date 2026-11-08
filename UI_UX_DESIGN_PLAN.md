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

### 3.1b The new look: "futuristic circular speaker" (the owner's reference)

The owner supplied an HTML mock-up as the interface they want: `docs/ui_reference_futuristic_circular_ai_speaker.html`
(open it in a browser; the "Simulate Voice Command" button and the state buttons show it moving). It replaces the
look of the Home screen, and the same frame is then used by the other screens. What it contains, and how each part
is built on the board:

| In the reference | On the board |
|---|---|
| Outer ring of 64 encoder tick marks, a major one (amber, long) every 8 and a medium one every 4 | Drawn once, never redrawn: 64 short lines (`lv_line`) or a background canvas in the external RAM |
| Thin dark track ring with a bright arc that rotates, its length following the voice | `lv_arc`, its angle updated 30 times a second |
| **Voice meter**: 32 radial bars around a ring, their length following the voice level, every fourth one brighter | 32 `lv_line` objects whose end points move with the level; only their small area is redrawn |
| Centre hub: a ring that grows with the voice, with crosshair marks | One `lv_obj` circle resized with the level, four short lines |
| HUD text: state tag on top (STATE: LISTENING), a small sub-line, the last words in a box, the clock and **the level in dB** at the bottom, the style name | The existing Spanish texts in the same positions: "ESCUCHANDO", "PENSANDO", "HABLANDO", "EN ESPERA"; the clock; "-26 dB" computed from the signal |
| States with colours: standby grey-slate, listening amber, thinking cyan, speaking emerald | The assistant's states map onto them; music is purple, muted and errors red, Wi-Fi setup amber (the colours we already use for those) |
| Three visual styles: 1 "cobot servo telemetry" (amber), 2 "sentinel optical core" (cyan, radar sweep and brackets), 3 "PLC logic ring" (emerald, a 16-step sequencer and 8 relay segments) | All three, chosen in Settings ("Estilo de pantalla"); the first one is the default |
| No blur, no gradients, only lines and arcs ("zero canvas filter") | Same rule: it keeps the CPU low and suits the AMOLED (black pixels are off) |

**The voice meter** is driven by real signals: the microphone level while the board hears you, the level of what the
board plays while it speaks (computed from the audio it writes) and the music level while music plays. The dB number is
20·log10 of that level, from -60 to 0.

Not taken from the reference: its tap-to-change-state demo (a tap on the board still starts listening) and its fonts
(Chakra Petch and JetBrains Mono; the board keeps Montserrat, a mono font can be added later).

Work, in order: **15a** the new Home screen in style 1 with the voice meter and the dB readout; **15b** styles 2 and 3
and the Settings choice; **15c** the tick ring and colours as the frame of the other screens, and the speaker level while
the board talks; **15d** optional mono font.

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

### 6.0f Briefing, lists, favourites, sleep timer, stopwatch, repeats, error notices (built)

| Say | What happens |
|---|---|
| "Buenos días" | Reads time and date, weather, today's events and unread mail; the list screen "Tu resumen" shows the same |
| "Agrega leche y huevos a la lista de compras", "qué hay en mi lista", "quita la leche de la lista", "borra la lista" | Shopping list, kept in `server/lists.json` |
| "Anota que mañana hay reunión", "mis notas", "borra las notas" | Notes, same file |
| "Guarda esta emisora en favoritas", "pon mis favoritas", "quita esta emisora de favoritas" | Radio favourites in `server/favorites.json`; "otra emisora" cycles through them |
| "Apaga la música en 30 minutos" | The board turns the music off by itself |
| "Inicia el cronómetro", "para el cronómetro", "reinicia el cronómetro" | Stopwatch screen with big time and two buttons; keeps counting after you leave the screen |
| "Cuándo es el próximo festivo", "mañana es festivo" | Colombian public holidays (computed, incl. the moved-to-Monday rule) on the list screen |
| "Cuántos días faltan para Navidad" (or "el 20 de julio", Año Nuevo, Halloween...) | Days until that date |
| "A qué hora se oculta el sol" | Sunrise and sunset in your city |
| "Cómo se dice casa en inglés", "traduce buenos días al francés" | Translation (English, French, Portuguese, German, Italian) on the Article screen |
| "Juguemos trivia", "la B" (or the answer), "otra pregunta", "cuántos puntos llevo" | 30 questions with three options on the Article screen; the score is kept until the server restarts |
| "Busca una actualización del firmware" | The board checks the installer page, and if its version is newer it downloads it (progress shows above the battery) and restarts; if the new firmware does not start properly the old one returns |
| "Juguemos al triqui" | Tic-tac-toe on the display: you are X and tap a square, Bug plays O (it slips now and then, so you can win); tap the board for a new game |
| "Despiértame todos los días a las 6 y media", "avísame todos los lunes a las 8 sacar la basura" | Repeating alarms and reminders, stored on the board; they ring again by themselves |

Home in standby shows the next alarm or timer above "Di Hola Bug", with the time left.
Error notices: "Batería baja · conecta el cargador" at 15% and "Batería casi vacía" at 5% (not charging),
a red ring with "No oigo el micrófono" when the microphone stops delivering audio, "Sin WiFi" handling
that waits for the router to come back, and "No encuentro el servidor. ¿Está encendido el PC?".
The first time this firmware runs, alarms saved by an older version are cleared (their storage format changed).

### 6.0b-4 Real email over IMAP (built, needs your account)

The Email screens show your real newest 4 messages (all mailboxes together) when
`server/mail_accounts.json` exists (never committed):
`[{"name": "Gmail", "host": "imap.gmail.com", "user": "you@gmail.com", "password": "<app password>"}]`.
Gmail needs 2-step verification and an App password (Google Account > Security). Outlook/Microsoft 365
uses host `outlook.office365.com`, but many work accounts have password sign-in turned off by the
administrator; those need OAuth, which is not built. Messages are only peeked, never marked read.
The "Datos de ejemplo" note disappears when real mail shows. Without the file the samples stay.

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

Where each feature runs (board, a free online service called by the board, or the PC as the last resort) and the order of work are in [ARCHITECTURE.md](ARCHITECTURE.md).

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
| 9c | Music sound: the audio bus and codecs run at 32 kHz and music streams at 32 kHz mono (bandwidth 16 kHz instead of 8 kHz); the microphone and Bug's voice stay 16 kHz (averaged down / doubled in the driver). Stereo is pointless on this one small speaker | Built, waiting for your test |
| 10 | Crypto and dollar prices, news, Wikipedia answers, jokes, math and conversions, this PC's status | Done |
| 10b | Timers, alarms and reminders (the board keeps them and rings even with the PC off) | Done (alarm sound and auto-close fixed) |
| 10c | Music from your own files on this PC, with song titles | Done |
| 10d | Real calendar from ICS links and email over IMAP (steps in SETUP.md) | Built, you configure it when development ends |
| 10e | Book reader (txt and epub, one page at a time) | Done |
| 10f | Volume by number ("volumen al 50") | Done |
| 11 | OAuth sign-in for work mailboxes whose administrator blocks passwords | Later (needs you to register an app) |
| 12 | Error screens (low battery, mic fault, Wi-Fi lost, no server) | Built, waiting for your test (power profiles later) |
| 13 | Screen power saving: dims to one sixth after 10 s without touch or voice, switches off after 20 s; touch, "Hola Bug", the ring tap or an alarm wakes it (the touch that wakes it is not also a tap). It stays on while Bug is listening or answering | Built |
| 13a | Less sensitive standby: the trigger is at least 300 (was 150) and 3.5 times the room noise, speech must last 240 ms, and in standby the screen no longer reacts to sounds; it only changes once "Hola Bug" is confirmed (then "Te escucho") | Built |
| 13c | Faster answers: the end of a sentence is detected after 550 ms of silence (was 800), the wake chime is shorter (140 ms) and the mic is flushed for 150 ms (was 400), so the first words of the command are not lost | Built |
| 13d | Answer speed. Real causes found by timing every step: (1) the board downloaded at only 25 KB/s because of small CPU caches and Wi-Fi buffers (now 32 KB instruction / 64 KB data cache, bigger Wi-Fi buffers, TCP windows 32 KB, Wi-Fi and TCP buffers may use the external RAM, Wi-Fi power saving off): now 860 KB/s; (2) Flask's built-in server sent answers in small pieces (the server now runs on waitress): an 80 KB answer arrives in 50-200 ms instead of 2 s; (3) Edge TTS stalls 2-12 s about one time in three (backup request, and a local Piper voice after 1.4 s); (4) a long speech prompt slowed recognition (kept short); (5) the wake check uses fast decoding. Typical exchange now: cached answer about 0.5 s, new answer 1.5-2.7 s | Built |
| 14 | **Local-first**: the board decides what the words mean and answers by itself when it can, with its own voice (joined recorded words in the storage partition, `server/clips/make_clips.py`). The PC only turns speech into words (and is asked, through `/ask`, for what needs the internet or its data). Local so far: time, date, volume, stop music, stopwatch, tic-tac-toe, battery, home. Still on the PC for now: timers and alarms, lists, favourites, weather, news, Wikipedia, math, books, calendar, email | In progress |
| 14c | **Works without Wi-Fi or PC**: the board starts without waiting for Wi-Fi, recognises "Hola Bug" and 28 fixed commands by itself (a small neural network trained from your own recordings, `server/wake/train_commands.py`, running in about 0.75 s) and answers with its own voice; the PC is asked only when the board is not sure, and not at all while it is known to be down | Built, accuracy improves with more recordings |
| 14b | On-board "Hola Bug" detector (small neural network, `server/wake/`, `firmware/main/wake_local.c`): built and verified against PyTorch, trained on synthetic voices plus your real clips; it logs scores but does not wake yet | In progress |
| 15a | New Home screen from your reference: tick ring, rotating arc, voice meter (32 bars), hub, state tag, dB readout, in the cobot style | Built and flashed (tick bezel, turning arc, 32-bar meter fed by mic, speaker and music level, hub, dB). State tag, clock and transcript box still to refine after you look at it |
| 15e | **Lumina Kinetic look** (reference `docs/ui_reference_lumina_kinetic.html`): volume rim you drag, 48-tick bezel, 16 elastic petals driven by the mic, speaker and music level, two ripples, the clock in the middle (the words replace it while talking), three palettes (ceramic and ochre, nordic forest, mono with a red dot). **Now the default Home.** The cobot look (15a) stays as the second style | Built and flashed |
| 15f | Voice skill to change the look: "cambia la interfaz" (switches Lumina and cobot), "pon la interfaz clásica / Lumina", "cambia los colores", "pon el tema nórdico / cerámica / mono". Saved in the board's memory | Built and flashed |
| 15g | Text answers (jokes, news, Wikipedia, translation, books, briefing) are only spoken: the article and headline screens are no longer opened, so Home stays and nothing is left to close | Done |
| 13e | **Screen speed.** Measured with a frame counter (`DISPLAY_DRIVER: Screen:` lines in the log): menus scrolled at about 9 frames a second. Causes found and fixed: (1) every letter of the Montserrat TrueType font was redrawn from its outline each time (now each letter is drawn once and kept: scrolling went to 29 frames a second), (2) the firmware was compiled for debugging (now for speed), (3) the power manager could leave the CPU at 80 MHz (now held at 240 MHz while the screen is fully on), (4) the screen refreshes every 16 ms and the touch is read every 10 ms, (5) LVGL uses the library copy routines. A full-screen change is limited by the display link to about 18 frames a second | Done |
| 15b | Sentinel and PLC styles, "Estilo de pantalla" in Settings | Planned |
| 15c | The same frame on the other screens; speaker level while the board talks | Planned |
| 15d | Optional mono font | Planned |
| 13b | Accessibility testing, animation polish, performance | Later |

### Backlog: planned but not built yet

Nothing here needs a Claude key unless marked.

| Id | Item | Needs | Status |
|----|------|-------|--------|
| B1 | Home screen shows the next alarm or timer and counts down | nothing | Built |
| B2 | Morning briefing: "buenos días" reads time, weather, today's events, unread mail | nothing | Built |
| B3 | Radio favourites ("guarda esta emisora", "pon mis favoritas") | nothing | Built |
| B4 | Quick games: the gaming area (`docs/GAMES.md`): a menu and 25 touch games in six categories (all of TamaPoke's that fit Bug), plus triqui; trivia by voice | nothing | Built, untested on the device |
| B5 | Sleep timer for music ("apaga la música en 30 minutos") | nothing | Built |
| B6 | Reminders that repeat (every day, every Monday) | nothing | Built |
| B7 | Notes and shopping list by voice, shown on a screen and kept on the PC | nothing | Built |
| B8 | Stopwatch screen ("inicia el cronómetro") | nothing | Built |
| B9 | Smart home (lights, thermostat) | your devices and their accounts | Not built |
| B10 | Speaker identification for several users | nothing, but needs a test with each person | Not built |
| B11 | Offline fallback: timers, alarms, stopwatch and mute work without the server; the board's clock chip (PCF85063) keeps the time through restarts and power loss | nothing | Built |
| B12 | Updating firmware over Wi-Fi: "busca una actualización del firmware" (checks the installer page, downloads if newer, restarts; rolls back if the new one fails) | nothing | Built (works from the first version you publish after 0.5.0) |
| B13 | Free conversation, stories, email summaries and answers to open questions | Claude API key | Not built |

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
