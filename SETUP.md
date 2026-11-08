# Setting up Bug from scratch

Everything you have to configure once, in order. Anything marked **private** is kept out of Git on
purpose: never paste it into chat, commits or screenshots.

Paths below assume the project is in `C:\Users\<you>\Desktop\voice assitance`.

## 1. The server (your PC)

1. Install Python 3.12 and, for fast speech recognition, the current NVIDIA driver (optional; without
   a GPU the server uses the CPU and is slower).
2. Make the environment and install the packages (about 2 GB because of the GPU libraries):
   ```bash
   python -m venv %USERPROFILE%\bug-venv
   %USERPROFILE%\bug-venv\Scripts\pip install -r server\requirements.txt
   ```
3. Start it:
   ```bash
   cd server
   %USERPROFILE%\bug-venv\Scripts\python bug_server.py
   ```
   The first start downloads the speech model (about 1.6 GB). Check `http://127.0.0.1:5000/health`: it
   says `"device": "cuda"` when the GPU is used.
4. Windows asks to allow Python on the network: choose **Private networks**. Without it the board
   cannot reach the server.
5. Give the PC a fixed address in your router (reserve the IP), because the board remembers it.
   The project uses `192.168.1.8`.

Settings (optional environment variables): `BUG_PORT`, `BUG_VOICE`, `BUG_WHISPER_MODEL`,
`BUG_WHISPER_DEVICE`, `BUG_CITY`. See `server/README.md`.

### Local voice (faster, works offline)

The server speaks with Microsoft's online voice (Salomé), and falls back to a voice that runs on this
PC (Piper) whenever the online one takes longer than 1.4 s. To set it up: `pip install piper-tts` is in
`requirements.txt`, and the voice file must be in `serveroices\`: download `es_MX-claude-high.onnx`
and `es_MX-claude-high.onnx.json` (about 63 MB) from
https://huggingface.co/rhasspy/piper-voices/tree/main/es/es_MX/claude/high into that folder. Without the
files the server just uses the online voice. Settings: `BUG_TTS=piper` (always the local voice, fastest),
`BUG_TTS=edge` (only the online voice), `BUG_TTS_EDGE_BUDGET=1.4` (seconds to wait for the online voice).

## 2. The board

**Easiest:** open https://botero50.github.io/bug-voice-assistant/ in Chrome or Edge, plug the board
in with USB and press Install. Then on the board open Settings (swipe up from Home) and enter your
Wi-Fi and the server address, for example `http://192.168.1.8:5000`.

**Build it yourself** (needs ESP-IDF 5.3): copy `firmware\main\bug_config.local.h.example` to
`bug_config.local.h` and put your Wi-Fi name, **private** Wi-Fi password and server address in it,
then `idf.py build flash`. That file is ignored by Git.

## 3. Your city

Automatic: found from your internet connection. By voice: "cambia la ciudad a Cali" (saved in
`server\user_settings.json`, **private**), and "detecta mi ciudad" to go back to automatic.

## 4. Your music

Put MP3, M4A, FLAC, WAV, OGG or WMA files in `C:\Users\<you>\Music` (subfolders are fine). To use
other folders, set the variable `BUG_MUSIC_DIR` (several folders separated by `;`) before starting
the server. Say "pon mi música", "pon la canción de ...", "siguiente canción".

## 5. Your books

Put `.txt` or `.epub` files in `C:\Users\<you>\Documents\Libros` (or set `BUG_BOOKS_DIR`). Say "lee un
libro" or "lee el libro" plus part of the title, then "sigue leyendo" or "repite la página". The page
you are on is saved in `server\reading_state.json`.

## 6. Your calendar (real events)

1. **Google Calendar:** open calendar.google.com, hover the calendar > three dots > Settings and
   sharing > **Secret address in iCal format**, copy it.
   **Outlook:** Settings > Calendar > Shared calendars > Publish a calendar > choose the calendar,
   permission "Can view all details" > Publish > copy the **ICS** link.
2. Create `server\calendar_sources.txt` with one link per line (**private**: whoever has the link can
   read your agenda). Lines starting with `#` are comments.
3. Restart the server and say "qué tengo en el calendario". Without links the sample events appear and
   the screen says "Datos de ejemplo".

## 7. Your email (real messages)

1. **Gmail:** turn on 2-step verification (https://myaccount.google.com/security), then create an App
   password at https://myaccount.google.com/apppasswords, name it "Bug". Google shows a 16-letter code.
2. Copy `server\mail_accounts.example.json` to `server\mail_accounts.json` and fill it in
   (**private**, ignored by Git):
   ```json
   [
     {"name": "Gmail", "host": "imap.gmail.com", "user": "you@gmail.com", "password": "abcd efgh ijkl mnop"}
   ]
   ```
3. Restart the server and say "muéstrame los correos". The "Datos de ejemplo" note disappears when real
   mail shows. Bug only peeks, so nothing is marked as read.
4. **Outlook / Microsoft 365:** add another entry with host `outlook.office365.com` and your address and
   password. Many work accounts have password sign-in blocked by the administrator; those need OAuth,
   which is not built yet.

## 7b. Teach the board your voice (guided recording; part of the configuration phase)

So the board can recognise "Hola Bug" and its commands without the PC, it needs recordings of you. Say
"Hola Bug, entrena mi voz". The board then shows one phrase at a time ("¿Qué hora es?", "Sube el volumen", ...),
beeps, and records you saying it. The phrase with the fewest recordings comes next, so you can stop at any time
(stay silent for three prompts, or say nothing) and continue another day with the same words. The list of phrases and
how many recordings each needs is in `server/wake/commands.json` (436 recordings in all, about 40 minutes in total, in
as many sittings as you like). The clips are saved on this PC in `server/wake_data/commands` (never committed). Record in
different places, at different distances and volumes; the more varied, the better the models work.

## 7c. Train the board's voice models (do this in the configuration phase)

The board recognises "Hola Bug" and 28 commands by itself, with no PC and no internet, using two small neural
networks trained on this PC from your recordings. The version in the firmware today was trained on only 184
recordings (about 5 per phrase) and gets about 60 to 70% of the commands right, so do this once everything else
is set up:

1. **Record your voice** (section 7b) until the progress is complete: 436 recordings, about 40 minutes, in as many
   sittings as you like. Different places, distances and volumes make the models better.
2. **Prepare the training environment** (once): a separate Python environment with PyTorch for the GPU:
   ```bash
   python -m venv %USERPROFILE%\bug-train-venv
   %USERPROFILE%\bug-train-venv\Scripts\pip install torch --index-url https://download.pytorch.org/whl/cu124
   %USERPROFILE%\bug-train-venv\Scripts\pip install numpy scipy soundfile datasets
   ```
3. **Make the extra training data** (once; needs the internet; about 30 minutes): the same phrases by many synthetic
   voices, and real Spanish speech and everyday noise as examples of what is not a command:
   ```bash
   cd server
   %USERPROFILE%\bug-venv\Scripts\python wake\make_synthetic_commands.py
   %USERPROFILE%\bug-train-venv\Scripts\python wake\fetch_corpora.py
   ```
4. **Train** (about 15 minutes on the GPU; it prints the accuracy on recordings it did not train on):
   ```bash
   %USERPROFILE%\bug-train-venv\Scripts\python wake\train_commands.py 10
   ```
   This writes `firmware/main/command_model.h`.
5. **Build and flash** the firmware again (the web installer builds it from the repository, or `idf.py flash` here).

Repeat steps 4 and 5 whenever you add recordings. The phrases and how many recordings each needs are in
`server/wake/commands.json`; to add a command, add it there, record it, add its spoken sentence in
`firmware/main/command_local.c` and, if it needs a new answer, its words in `server/clips/make_clips.py`.

## 8. Check that everything works

- Server tests (speaks phrases to the server, restarts it on generated test songs, books and calendar,
  then starts it normally again): run it in the background with its output sent to a file, because the
  restarted server keeps the output pipe open:
  ```bash
  powershell -File server\run_tests.ps1 > %TEMP%\rt.txt
  ```
  The last lines say `TESTS_DONE exit=0` when everything passes.
- On the board: say "Hola Bug, qué hora es", "pon un temporizador de 20 segundos", "pon salsa",
  "qué clima hace".

## 8b. Agenda and email on the board itself (no PC needed afterwards)

The board keeps these in its own memory. You give them to it once:

- **Agenda:** put your calendar link (Google Calendar → Settings → "Secret address in iCal format") as one line in
  `server/calendar_sources.txt`, or type it on the board in Settings → Calendario. The first time you ask "qué tengo hoy"
  the board copies it from the PC and keeps it.
- **Email:** create `server/mail_accounts.json` with `[{"name": "Gmail", "host": "imap.gmail.com", "user": "you@gmail.com",
  "password": "app password"}]` (Gmail needs 2-step verification and an App password). The first time you say
  "revisa mis correos" the board copies the first mailbox from the PC and keeps it. Only machines on your home network
  can ask the PC for it. The board shows who wrote, the subject and the time; it never marks mail as read.

## 8c. Setting the board up from the installer page

After installing from the installer page (the GitHub Pages site), scroll to **Set up the board**. Type your Wi-Fi name and
password, your Gmail address and app password, your calendar link and, only if you run the Bug server, the PC's address, then press
**Send to the board** and pick the board's port. The page sends one line over USB (`BUGCFG {...}`) and the board keeps everything in
its own memory and connects to the Wi-Fi by itself. **What does the board have?** lists what is set (never the passwords);
**Forget everything** removes it. Nothing you type is uploaded: the page runs in your browser and talks only to the board.
The same can be done any time, even long after installing, by opening the page again.

## 9. Everything you can say

Start with "Hola Bug" (or tap the ring), then:

- Time and date: "qué hora es", "qué día es hoy". Briefing: "buenos días".
- Weather: "cómo está el clima", "clima en Cali". City: "cambia la ciudad a Cali", "detecta mi ciudad".
- Timers, alarms and reminders: "pon un temporizador de 10 minutos", "despiértame a las 6 y media",
  "recuérdame en 20 minutos llamar a Ana", "despiértame todos los días a las 6", "qué alarmas tengo",
  "cancela las alarmas". Stopwatch: "inicia / para / reinicia el cronómetro".
- Music from the card: copy MP3 files into a folder called `music` on the microSD card, then say "pon mi música"; "siguiente canción" / "canción anterior" skip.
- Music: "pon salsa", "pon Tropicana", "otra emisora", "guarda esta emisora en favoritas",
  "pon mis favoritas", "pon mi música", "pon la canción de ...", "siguiente canción", "para la música",
  "apaga la música en 30 minutos", "sube / baja el volumen", "pon el volumen al 50 por ciento".
- Everyday: "cuándo es el próximo festivo", "mañana es festivo", "cuántos días faltan para Navidad",
  "a qué hora se oculta el sol", "cómo se dice casa en inglés".
- Games (see `docs/GAMES.md`): "juguemos" opens the games menu (categories, then cards to swipe); "dime una adivinanza" tells a riddle aloud; "juguemos memoria", "2048", "mayor o menor", "topos", "simón dice" and
  "juguemos al triqui" go straight to one. "Juguemos trivia" then "la B", "otra pregunta" is a voice game.
- Firmware: "busca una actualización del firmware" (the board updates itself from the installer page).
- Online voice skills (the board speaks with a free online voice, so they need internet): "cuéntame un chiste", "dame las noticias",
  "quién fue Simón Bolívar", "cómo se dice casa en inglés", "traduce buenos días al francés", "buenos días" (date, weather and agenda).
- Radio favourites: "guarda esta emisora en favoritas", "pon mis favoritas", "quita esta emisora de favoritas".
- Books: copy `.txt` files into a folder called `books` on the microSD card, then say "lee el libro cien años", "sigue leyendo",
  "repite la página", "mis libros". The board remembers the page.
- Look of the screen: "cambia la interfaz" (Lumina Kinetic, the default, or the cobot telemetry look), "cambia los colores",
  "pon el tema nórdico / cerámica / mono". Drag the rim of the screen to change the volume.
- Agenda: "qué tengo hoy". The board reads the calendar links it keeps in Settings → Calendario (or copies them once from the PC's calendar_sources.txt).
- Lists: "agrega leche a la lista de compras", "qué hay en mi lista", "quita la leche de la lista",
  "anota que ...", "mis notas".
- Math, conversions and prices (the board answers by itself; prices need internet): "cuánto es 25 más 17", "cuánto es doce por cinco",
  "cuántos kilómetros son 10 millas", "cuántos grados fahrenheit son 20 celsius", "a cuánto está el dólar", "cuánto está el bitcoin".
- Information: "dame las noticias", "cuánto está el bitcoin", "a cuánto está el dólar",
  "quién fue Gabriel García Márquez", "cuéntame un chiste", "cuánto es 25 más 17",
  "convierte 5 millas a kilómetros", "lanza un dado", "cómo está mi computador".
- Email, calendar, books (the board reads the first four messages: "lee el primer correo", "lee el segundo"): "muéstrame los correos", "lee el primero", "qué tengo en el calendario",
  "lee el libro ...", "sigue leyendo", "repite la página".

## 10. What stays private (never committed)

`firmware\main\bug_config.local.h`, `server\user_settings.json`, `server\timers.json`,
`server\reading_state.json`, `server\lists.json`, `server\favorites.json`, `server\calendar_sources.txt`, `server\mail_accounts.json`,
`server\test_data\`. They are all listed in `.gitignore`. Before pushing, check what is staged:
`git grep --cached -n -I "your password or link"` should print nothing.

## Not available yet

Features that need a Claude API key (free conversation, summaries of emails) and OAuth sign-in for
locked work mailboxes.

## Note for the next release (installer)

- The installer now also writes the recorded voice (`firmware/voice_clips.bin`, about 7.5 MB, at 0x810000). That file is made by
  `server/clips/make_clips.py` and must be committed together with the firmware, because the firmware checks that the number of
  clips matches. After an over-the-air update only the program changes, so if new clips were added, install again from the
  installer page (the board then keeps working without its recorded voice, and the PC answers instead, until it is reinstalled).
- The board's models (`wake_model.h`, `command_model.h`) are compiled into the firmware, so they travel with it.
