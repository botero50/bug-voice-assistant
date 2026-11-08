# Bug voice server

Runs on your PC. The board sends it what it hears; it listens for "Hola Bug" /
"Hey Bug", runs the skill and sends back the answer spoken by Salomé
(`es-CO-SalomeNeural`), plus the data for the screen the answer opens.

Skills today: time and date, email and calendar (sample data), weather (real, Open-Meteo),
your city (found from your connection, or chosen by voice), internet radio (Radio Browser), your own songs (`~/Music`, or set `BUG_MUSIC_DIR`; `library.py`),
volume, and a few small replies.

## Run it

```bash
python -m venv %USERPROFILE%\bug-venv
%USERPROFILE%\bug-venv\Scripts\pip install -r requirements.txt
%USERPROFILE%\bug-venv\Scripts\python bug_server.py
```

Allow Python through the Windows firewall on private networks when asked, or the board
can't reach it.

## Speech recognition

The server uses faster-whisper. It tries the NVIDIA GPU first with the `turbo` model (about
1.6 GB, downloaded on the first start; roughly 0.4 s per sentence) and falls back to the CPU
with the `small` model (about 500 MB, roughly 1.4 s) if the GPU can't be used. The log says
which one is running, and so does `http://127.0.0.1:5000/health`.

The GPU needs the two NVIDIA libraries in `requirements.txt` (`nvidia-cublas-cu12` and
`nvidia-cudnn-cu12`, about 1 GB). They are installed inside the Python environment only.

## Test without the board

With the server running:

```bash
%USERPROFILE%\bug-venv\Scripts\python test_client.py --play
```

The test speaks phrases to the server and checks the replies, the screens and a real radio
stream. It changes the saved city while it runs and puts it back afterwards.

## Settings (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `BUG_PORT` | `5000` | Port the board connects to |
| `BUG_VOICE` | `es-CO-SalomeNeural` | Edge TTS voice |
| `BUG_WHISPER_MODEL` | empty | Whisper model. Empty means `turbo` on the GPU and `small` on the CPU |
| `BUG_WHISPER_DEVICE` | `auto` | `auto`, `cuda` or `cpu` |
| `BUG_CITY` | `Bogotá` | Only used if the city can't be detected and none was chosen |

## Your city

Detected from your internet connection (ip-api.com). "Cambia la ciudad a Cali" saves a city in
`user_settings.json` (ignored by Git); "detecta mi ciudad" goes back to automatic.

## Board settings

Wi-Fi and the server address are set on the board itself (Settings). To build the firmware with
them preset, copy `firmware/main/bug_config.local.h.example` to `bug_config.local.h`. That file
is ignored by Git.
