# Bug: a Colombian-Spanish voice assistant on an ESP32-S3 round display

Bug is a voice assistant that lives on a **Waveshare ESP32-S3-Touch-AMOLED-1.75** (466x466 round screen, two microphones, speaker).
It answers by itself first, with free online services second, and uses a PC only as the last resort.

**[Install the firmware from your browser](https://botero50.github.io/bug-voice-assistant/)** (desktop Chrome or Edge, board plugged in by USB).

## How it answers (in this order)

1. **The board alone, no internet:** time, date, timers, alarms, stopwatch, notes and shopping list, math, units, holidays, games, riddles, music from the microSD, the 28 spoken commands, "qué puedes hacer".
2. **The board plus free online services:** weather (Open-Meteo), prices, radio, news, jokes, Wikipedia, translation, e-mail (IMAP), agenda (ICS links). The board talks to them itself and speaks with a free online voice.
3. **The PC (last resort):** speech recognition for the wake word check (Whisper on the GPU), skills that need the PC (your music library, books, the Claude Code bridge) and, when no skill understands a sentence, **free conversation with a local model** (Ollama, `qwen3:8b`).

The wake word "Hola Bug" is detected on the board (a small neural network plus an on-board verifier) before anything is sent to the PC.
Conversation memory: the last 5 exchanges survive a restart, "repite lo último", and follow-ups like "y mañana" / "y el euro".

Extra: **Claude Code from the board** (approve or reject permissions, "cómo va Claude", "dile a Claude que continúe", a small screenshot of the PC), at home or away through a Cloudflare Worker mailbox (`bug-gateway`, see below).

## Set up from zero

You need: the board, a USB-C cable, a Windows 11 PC (an NVIDIA GPU is optional but makes speech recognition fast) and your home Wi-Fi.

1. **Get the code:** `git clone https://github.com/botero50/bug-voice-assistant.git`
2. **Run the installer** (double-click, or in a terminal; safe to run again):
   ```
   scripts\Instalar.bat            server + local voice + Ollama chat model + desktop shortcut
   scripts\Instalar.bat -All       also board build tools, Claude Code hooks, training environment
   ```
   What it sets up: Python 3.12, the server environment `%USERPROFILE%\bug-venv`, the Piper offline voice, the Windows Firewall rule (needs an administrator window; otherwise choose "Private networks" when Windows asks), Ollama and the `qwen3:8b` model.
   With `-Firmware`: ESP-IDF 5.3.2 in `%USERPROFILE%\esp\esp-idf` and the build junction. With `-ClaudeHooks`: the hooks for Claude Code. With `-Train`: PyTorch for training the voice models.
3. **Start the server:** the installer already makes it **start with Windows** (a shortcut in your Startup folder, minimized; `-NoAutostart` skips it, `-RemoveAutostart` takes it away) and checks that the Ollama chat model answers. To start it by hand: double-click **`Iniciar Bug.bat`** (or the "Iniciar Bug" shortcut the installer put on the desktop). The first start downloads the speech model (about 1.6 GB). Leave the window open. Check `http://127.0.0.1:5000/health`.
4. **Put the firmware on the board:** open the installer page above and press Install, **or** build it yourself: `scripts\flash.ps1 -Port COM5`.
5. **Configure the board** (Wi-Fi, Gmail, calendar, PC address): on the same installer page, scroll to **Set up the board**. You type your own passwords there; they go straight to the board over USB and nowhere else.
6. **Give the PC a fixed address** in your router (the board finds the server by name, but a reserved IP is more reliable).
7. Say **"Hola Bug, qué puedes hacer"**.

Showing it to someone: **[docs/DEMO.md](docs/DEMO.md)** has a five-minute script, and `scripts\demo_check.ps1 -Port COM5` checks the demo phrases on your board first.

Everything else (city, music folder, books, calendar, e-mail, recording your voice to improve the wake word, training) is in **[SETUP.md](SETUP.md)**.

## Day to day

| I want to... | Do this |
| --- | --- |
| Start the server | `Iniciar Bug.bat` |
| Rebuild and flash the board | `scripts\flash.ps1 -Port COM5` (`-NoFlash` only compiles) |
| Check the server | `http://127.0.0.1:5000/health`; tests: `server\run_tests.ps1`, `python server\dev_bridge_test.py` |
| Test a phrase on the board without speaking | send `BUGCFG {"hear":"qué hora es"}` over USB (the installer page has a test box) |
| See the board's memory | `BUGCFG {"mem":1}` over USB |
| Use another chat model | `set BUG_LLM_MODEL=<model>` before starting; `BUG_LLM=0` turns free chat off |
| Pair the board with the dev bridge | `python server\tools\pair_board.py COM5` (Wi-Fi password typed in the prompt) |
| Let Claude Code talk to the board | `python server\tools\install_claude_hooks.py --apply` (`--remove` undoes it) |
| Try the smart home with no devices | `python server\tools\sim_home.py`, then say "enciende la luz de la sala" (needs `/sd/home/devices.txt`, see SETUP.md 8f) |
| Use it away from home | the gateway (below), then `python server\tools\gateway_setup.py` |

## Away from home: the gateway

The board and the PC both connect **out** to a small mailbox on Cloudflare Workers (`bug-gateway`: Worker plus a Durable Object, HMAC-signed requests, allowed commands only). Nothing on the PC is opened to the internet.
Its source lives in the company web project, `F:\Automation company\bug-gateway` (its README has the deploy steps). On a new PC:

```
python server\tools\gateway_setup.py        makes the tokens (never printed) and server\dev_relay.json
# then, in the gateway folder, the two commands it prints (wrangler secret put ...) and: npx wrangler deploy
```

After deploying the gateway, stop the Bug server for about 45 seconds so the mailbox restarts with the new code.

## Private files (never committed)

`server/dev_token.txt`, `server/pc_token.txt`, `server/dev_relay.json`, `server/dev_claude.json`, `server/mail_accounts.json`, `server/calendar_sources.txt`, `firmware/main/bug_config.local.h`, `server/wake_data/`, `server/dev_runs/`, `server/dev_audit.log`. Wi-Fi, Gmail and tokens are typed by you; no script here stores or prints them.

## Repository map

```
Iniciar Bug.bat          starts the server (uses %USERPROFILE%\bug-venv)
scripts/                 Instalar.bat/.ps1 (set up a new PC), flash.ps1 (build + flash)
firmware/                ESP-IDF 5.3 project (C, LVGL 8): assistant, skills, UI, audio, wake word, dev bridge
server/                  Python server (Flask): Whisper, voices, skills, Claude bridge, free chat (llm_chat.py)
server/tools/            pair_board, gateway_setup, Claude Code hooks, notifier
server/wake/             wake-word and command training and evaluation tools
web/                     the browser installer page (GitHub Pages; built by .github/workflows/installer.yml)
docs/                    IMPROVEMENT_PLAN.md (current plan and results), DEV_BRIDGE_PLAN.md, SKILLS.md, GAMES.md, TESTING.md, ...
SETUP.md                 every configuration step in order
ARCHITECTURE.md  HARDWARE.md  CREDITS.md
legacy/                  the first (Docker) design, kept for reference, not used today (see legacy/README.md)
.github/workflows/       installer.yml (builds the firmware and the installer page), tests.yml (host tests on every push)
```

## Status

Running on the real board. The current plan, measurements and what is left are in **[docs/IMPROVEMENT_PLAN.md](docs/IMPROVEMENT_PLAN.md)**. Untested by a person so far: the wake-word verifier in daily use and the Cloudflare rate-limit rules.

## Hardware

- **Waveshare ESP32-S3-Touch-AMOLED-1.75**: round AMOLED, ES7210 microphones (two), ES8311 speaker codec, QMI8658 motion sensor, 8 MB PSRAM, microSD. Pins: [docs/HARDWARE_PINS.md](docs/HARDWARE_PINS.md), details: [HARDWARE.md](HARDWARE.md).
- A microSD card (music, books) and a USB-C cable.

Credits: [CREDITS.md](CREDITS.md).
