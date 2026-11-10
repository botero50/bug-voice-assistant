# Improvement plan, reconciled with the real project (2026-11-09)

The research package in `Downloads` (`COMPLETE_RESEARCH_AND_PLAN.txt` and its three companions) was written from a different picture of the project.
This page keeps what is useful, corrects what is not true here, and says where Phase 1 starts. Nothing here is pushed to GitHub until the owner says so.

## 1. What the package says and what is true here

| The package says | In this project |
|---|---|
| ESP-IDF 5.1, 78 modules, 12 skills | ESP-IDF 5.3.2, 78 C files in `firmware/main`, **over 35 local skills** (lists, timers, weather, radio, calendar, mail, games, translation, books, jokes, news, Wikipedia, math, prices, "qué puedes hacer", Claude control...). |
| Wake word is "generic English WakeNet7, 80%" | There is **no WakeNet7**. The board has its own small neural detector for "Hola Bug" (`wake_model.h`, trained here by `server/wake/train.py` on 82 real greetings, 3,663 synthetic and 1,900 corpus clips) and a command model (`command_model.h`, 184 recordings). WakeNet custom words need Espressif's training service, so it is not an option for free. |
| STT 2.5 s, fix with Deepgram streaming | Speech to text runs on this PC (faster-whisper on the RTX 4070). Deepgram needs an account key and the internet, against the rule "the board first, free online services second, the PC last". Streaming from the PC's Whisper is the free way and is a valid Phase 2 item. |
| Phi-2 local LLM "on the microSD" | The ESP32-S3 cannot run it. It could run on the PC (llama.cpp) as a free fallback to the missing Claude key; it is a PC feature, not an offline-board one. |
| Offline 40% → 70% | Most skills already run on the board without the PC. The honest number needs counting, it is probably higher than 70% already. |
| Visual feedback: 3 → 9 states | The Lumina Home has standby, hearing, listening, thinking, speaking, music, muted and more, with palettes. A review of what is missing is cheap; a rebuild is not needed. |
| Context memory 0 → 5 turns | The board already remembers the last exchanges (`remember_exchange`) for follow-ups ("y el siguiente", riddles, books). A real 5-turn memory for a language model only matters once there is a model that uses it. |
| Smart home (MQTT), multi-user (speaker ID), plugins | Valid ideas, none exists. They are Phase 4 and optional. |
| Baseline 80% wake accuracy | **Not measured before.** See section 2. |

Rules from the package that match ours: no pushing without the owner's word, flash and test on the real board, report measurements before and after. Rules that do **not**: "no paid or key-based services" (Deepgram, NewsAPI need keys; the project already uses free keyless services), "Docker stack" (the PC server is plain Python, not Docker).

## 2. Baseline of the wake word, measured today

`server/wake/baseline.py` runs the board's own C detector on the real clips with a second of room noise around each, at several trigger levels.
Today's run, on all real clips (the model was trained on them, so **these numbers are optimistic**):

| Trigger level | Greetings caught (of 82) | False alarms per hour of other audio |
|---|---|---|
| 0.80 | 99% | 274 |
| 0.90 | 96% | 138 |
| 0.95 | 91% | 74 |
| 0.98 | 83% | 38 |

57 minutes of non-greeting audio were used. The trigger in the firmware is set to **never** (`THRESHOLD 1.01` in `wake_local.c`: "the real value is set after tuning"), because even at 0.98 the detector would fire about every 1.5 minutes
on speech that is not the greeting. That is the real Phase 1 problem: **not accuracy on the greeting, but false alarms**, and the streaming detector is switched off on purpose. **Today the board is already board-first:** it cuts every sentence by energy and its own classifier (`command_local.c`, class "wake", confidence 0.65) decides if it was the greeting; only when it is not sure does the PC's Whisper check it as a fallback. Whisper is a fallback, never the decision, and must stay so (the owner's rule: the PC is the last resort, and Bug must work without internet).

## 3. Phase 1 as it should be here

1. **Honest test set** (needs the owner): say "Hola Bug" about 100 times, varied (quiet, TV on, from 1 and 3 metres, tired, fast, whispering) in a session that ends **before** any retraining. Those clips become the baseline:
   `python wake/baseline.py --after <the session's start, YYYYMMDD_HHMMSS>`. Training must then leave them out (a `--holdout-after` option for `train.py`, to add).
2. **Fewer false alarms**: more "almost" phrases as negatives ("hola", "hey", "bueno", "Bug" alone, TV Spanish), a longer context window or a second confirmation stage (the PC's Whisper on the 1.5 s clip, which is free).
3. **Pick the level** with a budget (for example less than one false alarm per hour) and set `THRESHOLD`, measured again on the board.
4. **Report** before and after with the same script, then flash and test on the real board.

Beamforming (Phase 1.2 of the package) is possible later with the two microphones of the ES7210; it only helps once the false alarms are under control.

## 4. The rest of the package, adjusted

- **Phase 2:** streaming Whisper from the PC for lower latency; a review of the Home states; friendly errors (partly done: the board says why Gmail failed, why the PC is away); the 5-turn memory only when there is a model to use it.
- **Phase 3:** a Phi-2 or similar on the PC as the free fallback for conversation (the board stays offline-first for its own skills).
- **Phase 4:** MQTT smart home, speaker identification and a skills folder on the card are optional and each is a project of its own.
- **Phase 5:** tests, docs and a demo; the host tests already cover games, skills, the bridge and the gateway.

## 5. Improvements (to do after the phases above)

### Internal memory of the board (found 2026-11-10)

Symptom: the Wi-Fi screen hung on "Buscando redes..." because its task (4 to 6 KB of stack) could not start; the board had only about 5 KB of free internal RAM (about 9 KB after the verifier's buffers were moved out).
The ESP32-S3 has about 340 KB of internal RAM (200 KB of it taken by static data, 58%) and 8 MB of external RAM (PSRAM) that is mostly free. The microSD card **cannot** act as RAM (it is block storage on a slow bus, with no swap or virtual memory on this chip); it can hold the big things so they are loaded into the PSRAM only when needed.

1. **Audit**: print the heap by capability at boot and after a TLS request (`heap_caps_print_heap_info`, `idf.py size-components`) to see who really uses the internal RAM, instead of guessing.
2. **mbedtls buffers in the PSRAM** (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`): usually tens of KB saved, every https call (weather, radio, mail, gateway).
3. **Wi-Fi and lwIP buffers**: fewer static RX/TX buffers and smaller windows than the defaults; the board does not need those speeds.
4. **Task stacks in the PSRAM** where it is safe (tasks that never run while flash is being written): the assistant, dev bridge, radio and music tasks are candidates; each one is checked, not all.
5. **LVGL draw buffer in the PSRAM** (it was cut to 20 lines because of this) and the static tables still in internal RAM (`wake_local.c`, `command_local.c`: the FFT buffers, 5 KB each).
6. **Use the microSD for what is big**: the recorded voice clips (7.5 MB in a flash partition) and the models could live on the card and be loaded to the PSRAM on demand, freeing flash for the program; a card cache for downloads (covers, headlines, songs).
7. **A guard**: log the free internal RAM at every screen change and warn below 8 KB, so the next regression is seen the day it is introduced.
Done when: at least 40 KB free internal RAM after boot and after a TLS request, the Wi-Fi screen scans in under 6 seconds, and nothing else got slower.
