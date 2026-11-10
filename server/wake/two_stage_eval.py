"""Measures the two-stage wake: the board's detector fires, then Whisper on the PC listens to the audio around the firing and must hear "Hola/Hey Bug".
A greeting is caught only if both agree; a false alarm survives only if both are fooled.

    python wake/two_stage_eval.py --after 20261109_164550 [--model old_wake_model.h]

Same clips and rules as wake/baseline.py (greetings from the guided session after --after; false alarms on all the not-greeting audio).
Uses the same Whisper model and settings as the server (turbo on the GPU), without touching the running server or its saved clips.
"""
import argparse
import difflib
import os
import site
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import baseline  # noqa: E402
from text_utils import normalize  # noqa: E402

SR = 16000
BEFORE, AFTER = 2 * SR, SR // 2  # the audio given to Whisper: 2 s before the firing and half a second after
GREETINGS = {"hola", "ola", "hey", "ey", "ei", "hei", "oye", "jey", "hay", "hi"}  # the same words the server accepts
BUG_SOUNDS = {"bug", "bag", "bog", "bac", "bak", "buk", "bach", "bugs", "vag", "bugg", "bago"}


def is_bug(word):
    return word in BUG_SOUNDS or difflib.SequenceMatcher(None, word, "bug").ratio() >= 0.66


def heard_greeting(text):
    words = normalize(text).split()
    return any(w in GREETINGS and i + 1 < len(words) and is_bug(words[i + 1]) for i, w in enumerate(words[:8]))


def heard_hint(text):
    """A more permissive check: a greeting word OR a "bug"-like word anywhere in the first words (to see what it would recover and cost)."""
    words = normalize(text).split()[:6]
    return any(w in {"hola", "ola", "hey", "ey", "oye", "jey"} or w in BUG_SOUNDS for w in words)


def load_whisper():
    for root in site.getsitepackages():
        for folder in Path(root, "nvidia").glob("*/bin"):
            os.add_dll_directory(str(folder))
            os.environ["PATH"] = str(folder) + os.pathsep + os.environ["PATH"]
    from faster_whisper import WhisperModel
    model = WhisperModel("turbo", device="cuda", compute_type="float16")

    def listen(pcm):
        audio = pcm.astype(np.float32) / 32768.0
        peak = float(np.max(np.abs(audio)))
        if peak < 0.004:
            return ""
        audio *= min(0.8 / peak, 40.0)  # the board's mic is quiet: the server raises it the same way
        segments, _ = model.transcribe(audio, language="es", beam_size=1, temperature=0.0, max_new_tokens=24, without_timestamps=True,
                                       condition_on_previous_text=False, vad_filter=True, vad_parameters={"min_silence_duration_ms": 400},
                                       initial_prompt="Hola Bug. Hey Bug. ¿Qué hora es? Pon salsa. Pon un temporizador. Cronómetro, triqui, trivia, favoritas.")
        return " ".join(s.text.strip() for s in segments).strip()

    return listen


def firings(exe, x, rng):
    """[(window of audio around each firing)] for one clip, with a second of room noise before it and 1.5 s after."""
    quiet = lambda n: (rng.standard_normal(n) * 20).astype(np.int16)
    stream = np.concatenate([quiet(SR), x, quiet(int(1.5 * SR))])
    raw = HERE / "two_stage.raw"
    raw.write_bytes(stream.tobytes())
    out = subprocess.run([str(exe), str(raw)], capture_output=True, text=True).stdout
    raw.unlink(missing_ok=True)
    windows = []
    for line in out.splitlines():
        if line.startswith("TRIGGER"):
            at = (int(line.split()[-1]) + 1) * 320
            windows.append(stream[max(0, at - BEFORE): at + AFTER])
    return windows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--after", default="")
    parser.add_argument("--model", default="")
    parser.add_argument("--levels", default="0.8,0.9,0.95,0.98")
    parser.add_argument("--lenient", action="store_true", help="accept a greeting word or a bug-like word alone (instead of both together)")
    args = parser.parse_args()
    if args.model:
        baseline.MODEL_DIR = HERE / "_model_dir"
        baseline.MODEL_DIR.mkdir(exist_ok=True)
        (baseline.MODEL_DIR / "wake_model.h").write_bytes(Path(args.model).read_bytes())
        (baseline.MODEL_DIR / "wake_local.c").write_bytes((baseline.ROOT / "firmware" / "main" / "wake_local.c").read_bytes())
    pos = baseline.read_clips(baseline.DATA / "pos", args.after) + baseline.read_clips(baseline.DATA.parent / "commands" / "wake", args.after)
    neg = baseline.read_clips(baseline.DATA / "neg", "") + baseline.read_clips(baseline.DATA.parent / "commands" / "ruido", "")
    hours = sum(len(x) for _, x in neg) / SR / 3600
    listen = load_whisper()
    heard_greeting_rule = heard_hint if args.lenient else heard_greeting
    rng = np.random.default_rng(11)
    print(f"{len(pos)} greetings, {len(neg)} other clips ({hours * 60:.0f} minutes)")
    print("level  detector alone: caught  FA/h   |  detector + Whisper: caught  FA/h   (Whisper checks: seconds each)")
    for level in map(float, args.levels.split(",")):
        exe = baseline.build_detector(level)
        started = time.time()
        checks = 0
        caught_a = caught_b = 0
        for _, x in pos:
            windows = firings(exe, x, rng)
            if windows:
                caught_a += 1
                checks += 1
                if heard_greeting_rule(listen(windows[0])):  # the board accepts the first firing that Whisper confirms
                    caught_b += 1
                elif len(windows) > 1 and heard_greeting_rule(listen(windows[1])):
                    caught_b += 1
        fa_a = fa_b = 0
        for _, x in neg:
            for window in firings(exe, x, rng):
                fa_a += 1
                checks += 1
                if heard_greeting_rule(listen(window)):
                    fa_b += 1
        print(f"{level:<6} {caught_a:>3}/{len(pos)} ({100 * caught_a / len(pos):3.0f}%) {fa_a / hours:7.1f}   |   {caught_b:>3}/{len(pos)} ({100 * caught_b / len(pos):3.0f}%) {fa_b / hours:7.1f}"
              f"   ({(time.time() - started) / max(checks, 1):.2f}s)", flush=True)
        exe.unlink(missing_ok=True)
    if args.model:
        import shutil
        shutil.rmtree(baseline.MODEL_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()
