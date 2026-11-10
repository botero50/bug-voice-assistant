# Phase 1, week 1: wake word results (2026-11-09)

## What was done
1. Recorded **100 new greetings** ("Hola Bug" / "Hey Bug") on the real board in one sitting, each in a different condition (`wake/start_session.py`).
   Clips: 0.6 to 8.0 s, median peak 2,764 of 32,767 (2 very quiet, none clipped).
2. **Baseline**: the model that was in the firmware, measured on all 100 clips it had never heard (`wake/baseline.py --after 20261109_163823`).
3. **Retrained** the detector with 60 of the new clips added to the training data and the last 40 held out
   (`BUG_HOLDOUT_AFTER=20261109_164550 python wake/train.py`, about 6 minutes on the RTX 4070; real positives went from 108 to 168).
4. **Compared** old and new on the same 40 held-out clips, with the board's own C code (`wake/baseline.py --after 20261109_164550 [--model old.h]`).

## Measurements (the 40 clips neither model saw during training)

| Trigger level | Old model: greetings caught / false alarms per hour | New model: greetings caught / false alarms per hour |
|---|---|---|
| 0.80 | 80% / 227 | **92% / 124** |
| 0.90 | 62% / 119 | **88% / 55** |
| 0.95 | 55% / 68 | **80% / 32** |
| 0.98 | 45% / 32 | **70% / 13** |

At the same number of false alarms (about 32 an hour) the new model catches **80% against 45%**. For reference, the old model on all 100 new clips caught 72% at 0.90 with 133 false
alarms an hour, against 96% on the clips it was trained on: it had overfitted to the old data, and the new clips (other distances, whispering, TV) are what it was missing.

## What these numbers do and do not say
- "Per hour" is per hour of the **not-greeting recordings** (1,517 clips, 61 minutes, mostly speech the board heard). A normal day is mostly silence, so the real rate is lower, but this is the harsh test.
- Those not-greeting clips were part of the training data of both models (only the greeting clips are held out), so the false-alarm column is optimistic for both. The comparison between them is fair.
- 40 clips is a small test set: a difference of 2 clips is 5 points.
- The firmware trigger is still **off** (`THRESHOLD 1.01` in `wake_local.c`), so nothing changed on the board yet; `firmware/main/wake_model.h` now holds the new model (trained with 40 clips held out).

## Second round (same day): short-word negatives and the Whisper check

**What the detector still fired on** (14 firings at level 0.98 among the not-greeting clips): mostly one-second interjections ("gracias", "sí", "ah", "dale", "mátalo"), "Hola, Claude", "Hola."
and one real "Hey Bug" that had been filed as not-greeting (label noise). So: 2,430 new synthetic negatives of that kind were made with many voices (`wake/make_short_negatives.py`) and the detector retrained (model "v3", same 40 clips held out).

**Two stages measured offline** (`wake/two_stage_eval.py`): the detector fires, then Whisper (turbo, the server's own settings) listens to the 2 s before and 0.5 s after the firing and must hear "Hola/Hey Bug".
Same 40 held-out greetings, 66 minutes of not-greeting audio:

| Level | v3 detector alone: caught / false alarms per hour | v3 + Whisper: caught / false alarms per hour |
|---|---|---|
| 0.80 | 88% / 64 | **78% / 0.0** |
| 0.90 | 82% / 31 | 75% / 0.0 |
| 0.95 | 75% / 17 | 72% / 0.0 |
| 0.98 | 70% / 8 | 68% / 0.0 |

(v2 alone at 0.90 was 88% / 55: the short-word negatives halved the false alarms at a small cost in catches.) Whisper takes 0.6 to 1.3 s per check on the GPU.

**The ceiling is Whisper, not the detector.** Today's wake check (Whisper on the whole clip) hears 84 of the 100 new greetings and 31 of the 40 held-out ones (78%), the same as the two-stage result. It fails in the hard conditions:
whispering 4/7, music 4/7, talking sideways 4/7, TV 5/7 (it hears "¡Gracias!", "Ponlas", "Hola." instead). Normal voice, close, 3 m and quiet speech are all 100%.
A more permissive confirmation (any greeting word or "bug"-like word) recovered no extra greeting and added false alarms (2.8 per hour), so the strict rule stays.

**What this means:** turning on the local detector with the Whisper check would give today's recall with zero false alarms, while sparing the PC from hearing every sound; it does **not** raise recall. To catch the 22% in noise, whispering or sideways, the next lever
is the audio itself (beamforming with the two microphones, noise suppression) or letting a very confident detector (0.98+) pass alone, which costs about 8 false alarms per hour of speech.

## Third round (2026-11-10): the audio itself

**What the board does today** (`audio_driver.c`): the two microphones are averaged into one, with a fixed 30 dB gain, and 32 kHz becomes 16 kHz by averaging pairs of samples (no real low-pass filter).

**The two microphones are twins.** In 50 raw recordings (both microphones, 32 kHz, `wake/start_session.py --raw`) the best delay between them is 0 samples in 48 cases (they sit almost touching), the background noise
correlates 0.88 between them and speech 0.99 to 1.00. Classic beamforming (steering towards the speaker, cancelling the TV) needs spacing that this board does not have, so it cannot help much here.

**The lab** (`wake/audio_lab.py`) turned those recordings into 16 kHz audio seven ways (today's average, each microphone alone, the sum with a proper low-pass, the louder microphone, spectral noise suppression, delay-aligned sum plus suppression) and counted the greetings Whisper recognises, in five conditions of 10 clips (whispering, music, TV, sideways, normal):

| Variant | Recognised of 50 |
|---|---|
| today's average | 48 |
| mic 1 / mic 2 | 48 / 47 |
| sum with proper low-pass | 48 |
| louder microphone | 48 |
| noise suppression | 48 |
| aligned + suppression | 48 |

**No processing helps**, because there is nothing left to gain: the clips the board sends today (1.2 s on average) are recognised 47 of 50 (94%): whispering 9/10, music 10/10, sideways 10/10, normal 10/10, TV 8/10 (the same 8/10 with every variant).
The 78% measured in the first session was not the audio: those were casual greetings in many conditions (far, quiet, half-asleep), and the people-speaking part, not the signal, made Whisper fail. Changing the front end would also shift what the local detector was trained on, for no measured gain.

**The local detector plus the Whisper check on these 50 clips** (never seen in training): at level 0.80, 50 of 51 caught by the detector (98%) and 49 of 51 (96%) after Whisper confirms, with **0 false alarms per hour** (66 min of other audio).
At 0.70: 94% with 0 false alarms; at 0.90: 88%. That reaches the plan's goal of 95% for greetings said deliberately; spontaneous ones (the earlier 78%) remain the open question, and only daily use will tell.

**So the audio path stays as it is.** The raw recording mode stays in the firmware (it only runs in a `--raw` session).

## Fourth round (2026-11-10): correction, and the check that stays on the board

**A mistake of mine, corrected.** The "detector + Whisper" design measured above puts the final decision on the PC. That goes against the project's rule (the board first, the PC last, and no internet needed), so it is **not** implemented and those numbers are only a reference.
And it was not even needed to understand how the board works today: in standby it cuts each sentence by energy and its own classifier decides ("wake" with confidence 0.65); Whisper is only the fallback when the board is not sure.

**Today's on-board decision**, on the 91 greetings and 721 other utterances recorded after the training data was frozen (none seen in training):

| | Greetings accepted by the board alone | Others wrongly accepted |
|---|---|---|
| on-board classifier, "wake" >= 0.65 (the firmware's rule) | 61 / 91 (67%) | 0 (but those utterances are the ones it rejected at the time, so 0 is by construction) |
| on-board classifier >= 0.50 | 67 / 91 (74%) | 14 (23 per hour of those utterances) |

So about a third of the greetings still go to the PC as a fallback.

**A better on-board check** (`wake/mine_windows.py` + `wake/train_verifier.py`, output `firmware/main/verify_model.h`, not yet in the firmware): a second small network trained on the detector's own mistakes (1,474 greeting windows and 1,994 false-alarm windows, from synthetic voices,
real clips, corpus speech and perturbed copies). Applied to each whole utterance, same unseen clips:

| Verifier threshold | Greetings accepted | Others wrongly accepted |
|---|---|---|
| 0.5 | 75 / 91 (82%) | 9 / 722 (1.2% of utterances) |
| 0.7 | 67 / 91 (74%) | 5 / 722 (0.7%) |
| 0.9 | 47 / 91 (52%) | 2 / 722 (0.3%) |

It accepts 74 to 82% of the greetings on its own against 67% today, but it **does** make false accepts that today's number hides, so the two are not directly comparable. A wrong accept is a false wake of the board, not a lost greeting.
Only about 2 to 5 events are behind each false-accept figure: treat them as an order of magnitude. The honest next step is a longer test with the board listening normally for days and logging what it accepts.

**Also learned:** the two microphones are twins (nothing to gain from beamforming) and no audio processing changed Whisper's recall (94 to 96% on deliberately said greetings in hard conditions).

## Integrated in the firmware (2026-11-10)

`firmware/main/wake_verify.c` runs the verifier on the board (the C code matches PyTorch to 5 decimals, `wake/check_verify.py`). In standby, after the board cuts a sentence by energy: (1) its classifier decides "wake" at 0.65 as before; (2) **if not**, the verifier gives a second opinion
and accepts the greeting from 0.70; (3) only if neither accepts does the sentence go to the PC (fallback). Each check costs about half a second on the board and runs only when needed.
Setting: `BUGCFG {"verify":0.7}` over USB (0 turns it off, lower accepts more greetings and more false wakes, higher fewer of both; it is kept in memory and shown by `{"status":1}`). The mic and the PC are not needed for it.
Expected from the offline measurement (unseen clips, threshold 0.70): about 74% of greetings accepted by the board alone (67% before the verifier, together the two should accept more) and about 0.7% of other utterances wrongly accepted. To be confirmed by days of normal use.

## Not good enough yet (first-round notes, kept)
Even the new model at 0.98 fires about 13 times per hour of speech that is not the greeting. The target for turning the detector on is **under 1 per hour of normal listening**. Next, in this order:
1. **Hard negatives**: record the phrases it confuses (the board logs what scored above 0.5), "hola", "hey", "bueno", "bug" alone, Spanish TV and talk; retrain.
2. **A second confirmation** on the PC (free): when the detector fires, Whisper checks the 1.5 s before it and a "no" cancels the wake. That drops false alarms by a large factor at the cost of about half a second.
3. **Final model**: retrain with all 100 clips (no hold-out) once the method is settled, then choose the level, set `THRESHOLD`, flash and test on the real board.
