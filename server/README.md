# Bug voice server

Runs on your PC. The board sends it what it hears; it listens for "Hola Bug" /
"Hey Bug", runs the skill (time, date, a few small replies) and sends back the
answer spoken by Salomé (`es-CO-SalomeNeural`).

## Run it

```bash
python -m venv %USERPROFILE%\bug-venv
%USERPROFILE%\bug-venv\Scripts\pip install -r requirements.txt
%USERPROFILE%\bug-venv\Scripts\python bug_server.py
```

The first start downloads the Whisper `small` speech model (about 500 MB). Allow
Python through the Windows firewall on private networks when asked, or the board
can't reach it.

## Test without the board

With the server running:

```bash
%USERPROFILE%\bug-venv\Scripts\python test_client.py --play
```

## Settings (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `BUG_PORT` | `5000` | Port the board connects to |
| `BUG_VOICE` | `es-CO-SalomeNeural` | Edge TTS voice |
| `BUG_WHISPER_MODEL` | `small` | Whisper model size |
| `BUG_WHISPER_DEVICE` | `cpu` | `cuda` to use an NVIDIA GPU (needs CUDA 12 libraries) |

## Board settings

Copy `firmware/main/bug_config.local.h.example` to `bug_config.local.h` and fill in
your Wi-Fi and this PC's address. That file is ignored by Git.
