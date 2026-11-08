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

## 9. Everything you can say

Start with "Hola Bug" (or tap the ring), then:

- Time and date: "qué hora es", "qué día es hoy". Briefing: "buenos días".
- Weather: "cómo está el clima", "clima en Cali". City: "cambia la ciudad a Cali", "detecta mi ciudad".
- Timers, alarms and reminders: "pon un temporizador de 10 minutos", "despiértame a las 6 y media",
  "recuérdame en 20 minutos llamar a Ana", "despiértame todos los días a las 6", "qué alarmas tengo",
  "cancela las alarmas". Stopwatch: "inicia / para / reinicia el cronómetro".
- Music: "pon salsa", "pon Tropicana", "otra emisora", "guarda esta emisora en favoritas",
  "pon mis favoritas", "pon mi música", "pon la canción de ...", "siguiente canción", "para la música",
  "apaga la música en 30 minutos", "sube / baja el volumen", "pon el volumen al 50 por ciento".
- Everyday: "cuándo es el próximo festivo", "mañana es festivo", "cuántos días faltan para Navidad",
  "a qué hora se oculta el sol", "cómo se dice casa en inglés".
- Games: "juguemos al triqui" (on the display), "juguemos trivia" then "la B", "otra pregunta".
- Firmware: "busca una actualización del firmware" (the board updates itself from the installer page).
- Lists: "agrega leche a la lista de compras", "qué hay en mi lista", "quita la leche de la lista",
  "anota que ...", "mis notas".
- Information: "dame las noticias", "cuánto está el bitcoin", "a cuánto está el dólar",
  "quién fue Gabriel García Márquez", "cuéntame un chiste", "cuánto es 25 más 17",
  "convierte 5 millas a kilómetros", "lanza un dado", "cómo está mi computador".
- Email, calendar, books: "muéstrame los correos", "lee el primero", "qué tengo en el calendario",
  "lee el libro ...", "sigue leyendo", "repite la página".

## 10. What stays private (never committed)

`firmware\main\bug_config.local.h`, `server\user_settings.json`, `server\timers.json`,
`server\reading_state.json`, `server\lists.json`, `server\favorites.json`, `server\calendar_sources.txt`, `server\mail_accounts.json`,
`server\test_data\`. They are all listed in `.gitignore`. Before pushing, check what is staged:
`git grep --cached -n -I "your password or link"` should print nothing.

## Not available yet

Features that need a Claude API key (free conversation, summaries of emails) and OAuth sign-in for
locked work mailboxes.
