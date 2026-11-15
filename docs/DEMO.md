# Demo script

A five-minute tour of Bug that uses only what works today. Everything in "Act 1" needs no PC and no internet.

## Before you start (2 minutes)

1. Start the server with **Iniciar Bug.bat** and wait for "Speech recognition ready" (needed for Act 3 and for the wake word check).
2. Board on its charger (or USB), Wi-Fi up, volume at about 60%, room quiet.
3. Optional but recommended: run the check, which sends the demo phrases to the board over USB and tells you which ones answer the way this script says:
   ```
   scripts\demo_check.ps1 -Port COM5          (13 phrases, about 4 minutes; closes the port when done)
   ```
   Last run (2026-11-10): **13 of 13** answered as expected. It uses the board's test hook, so it proves the skills and the routing, not the microphone.
4. Do not leave a timer running (the check cancels its own).

## Act 1: it thinks for itself (no PC, no internet)

Say "Hola Bug", wait for the chime, then the phrase. Or tap the ring.

| Say | What happens |
| --- | --- |
| "¿Qué puedes hacer?" | A spoken tour of its skills. Interrupt it by saying "Hola Bug". |
| "¿Qué hora es?" | Instant, in its own recorded voice. |
| "Pon un temporizador de 20 segundos" | Confirms at once; rings in 20 s with the alarm screen. (Cancel: "cancela los temporizadores".) |
| "¿Cuánto es 15 por 12?" | "El resultado es 180." Math and unit conversions are on the board. |
| "Cuándo es el próximo festivo" | Colombian holidays are computed on the board. |

Point out: this answered with the router unplugged. The board decides first; the PC is the last resort.

## Act 2: free internet services (the board calls them itself)

| Say | What happens |
| --- | --- |
| "¿Cómo está el clima?" | Real weather for the detected city (Open-Meteo). |
| "¿Cuánto vale el dólar?" then **"¿Y el euro?"** | Price, then a follow-up that leans on the last question. |
| "¿Y el bitcoin?" | Same thread. |
| "Repite lo último" | Says the last answer again (it remembers the last 5, even after a restart). |
| "Cuéntame un chiste" | Joke fetched, translated and said with the online voice; the first word starts about half a second after the text. |
| "¿Quién fue Simón Bolívar?" | Wikipedia summary, spoken in pieces without silences. |
| "¿Cómo se dice casa en inglés?" | Translation. |

## Act 3: when nothing else fits, the PC model answers (free)

| Say | What happens |
| --- | --- |
| "Dame un consejo para estudiar mejor" | No skill matches, so the local model on the PC (Ollama) answers in a sentence or two. Needs the server restarted after the Phase 3 update. |
| "¿Por qué el cielo es azul?" | Same. |

If the PC is off, Bug says so and lists what it can still do.

## Act 4: the developer extras (optional, 1 minute)

Needs the dev bridge (`SETUP.md` 8d). With Claude Code open on this project:

- "¿Cómo va Claude?" Bug reads the state of the session.
- When Claude asks for a permission, the board shows **Aprobar / Rechazar**; say "aprueba".
- "Toma un pantallazo": a small picture of the PC desktop appears; tap to close.

## Your own skills on the card (30 seconds, if the card is in)

Say "prueba de la tarjeta" (from `microsd_files/skills/ejemplo.txt`). Then open the file on the PC, add a line, put the card back and try it: no compiling or flashing.

## If something goes wrong

| Symptom | Do this |
| --- | --- |
| It does not wake on "Hola Bug" | Tap the ring instead and say the phrase; check the room is quiet. The wake word is the least reliable part today (about 67% of greetings heard on the board). |
| "Eso necesita mi servidor y no responde" | Start `Iniciar Bug.bat`; the board skills still work. |
| Online answers are silent | No internet: the text still shows on the screen. |
| A phrase goes to the PC that should be local | Try again with the full phrase; the board matches words, not meaning. |
| Free chat says "No te entendí" | Ollama is off or the model is loading (first answer after a cold start can be slow): `ollama list`, then restart the server. |

## What not to claim yet

- The wake word and the Claude-control voice commands have not been tested by a person in daily use.
- Use away from home (Cloudflare mailbox) works, but the rate-limit rules on the Cloudflare side are not set yet.
- Skills from a real card were tested only on a host, not on the board's card slot.
