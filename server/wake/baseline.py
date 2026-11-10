"""Measures the board's "Hola Bug" detector (its own C code, built for this PC) on real clips: how many greetings it catches and how many
false alarms it makes per hour of audio that is NOT the greeting, at several trigger levels.

    python wake/baseline.py                       all the real clips (they were also used to train the model, so the numbers are optimistic)
    python wake/baseline.py --after 20261110_000000   only GREETING clips recorded after that moment (name format YYYYMMDD_HHMMSS):
                                                      record a fresh set, then run this BEFORE retraining to get an honest baseline
                                                      (the false alarms are always measured on all the not-greeting audio)
Clips come from server/wake_data/real/pos ("Hola Bug", labelled by Whisper) and real/neg (everything else the board heard).
"""
import argparse
import re
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DATA = HERE.parent / "wake_data" / "real"
SR = 16000
LEVELS = (0.5, 0.7, 0.8, 0.9, 0.95, 0.98)


def read_clips(folder, after):
    clips = []
    for path in sorted(folder.glob("*.wav")):
        if after and path.stem[:15] < after:
            continue
        with wave.open(str(path), "rb") as w:
            if w.getframerate() != SR or w.getnchannels() != 1:
                continue
            x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        if len(x) >= 4000:
            clips.append((path.name, x))
    return clips


MODEL_DIR = None  # a folder with another wake_model.h to measure instead of the one in the firmware (--model)


def read_clips_one(path):
    """[(name, samples)] for one wav file (16 kHz mono, at least 0.25 s), or []."""
    with wave.open(str(path), "rb") as w:
        if w.getframerate() != SR or w.getnchannels() != 1:
            return []
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    return [(path.name, x)] if len(x) >= 4000 else []


def build_detector(level):
    exe = HERE / f"host_test_{int(level * 100)}.exe"
    source = (MODEL_DIR / "wake_local.c") if MODEL_DIR else (ROOT / "firmware" / "main" / "wake_local.c")  # a copy next to the other model: "..." includes look there first
    subprocess.run(["gcc", "-O2", f"-DTHRESHOLD={level}f", "-I", str(HERE / "stubs"), "-I", str(ROOT / "firmware" / "main"), str(HERE / "host_test.c"),
                    str(source), "-lm", "-o", str(exe)], check=True, capture_output=True)
    return exe


def triggers(exe, clips):
    """For every clip: how many times the detector fired while listening to it (with a second of room noise before and after)."""
    rng = np.random.default_rng(7)
    counts = []
    raw = HERE / "baseline.raw"
    for _, x in clips:
        quiet = lambda n: (rng.standard_normal(n) * 20).astype(np.int16)
        raw.write_bytes(np.concatenate([quiet(SR), x, quiet(int(1.5 * SR))]).tobytes())
        out = subprocess.run([str(exe), str(raw)], capture_output=True, text=True).stdout
        counts.append(sum(1 for line in out.splitlines() if line.startswith("TRIGGER")))
    raw.unlink(missing_ok=True)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--after", default="", help="only clips whose name is at or after YYYYMMDD_HHMMSS")
    parser.add_argument("--model", default="", help="a wake_model.h to measure instead of the firmware's (for before/after comparisons)")
    args = parser.parse_args()
    if args.model:
        global MODEL_DIR
        MODEL_DIR = HERE / "_model_dir"
        MODEL_DIR.mkdir(exist_ok=True)
        (MODEL_DIR / "wake_model.h").write_bytes(Path(args.model).read_bytes())
        (MODEL_DIR / "wake_local.c").write_bytes((ROOT / "firmware" / "main" / "wake_local.c").read_bytes())
    pos = read_clips(DATA / "pos", args.after) + read_clips(DATA.parent / "commands" / "wake", args.after)  # Whisper-labelled and guided recordings
    neg = read_clips(DATA / "neg", "") + read_clips(DATA.parent / "commands" / "ruido", "")  # all the not-greeting audio, whatever its date
    if not pos:
        sys.exit("No greeting clips found for that selection.")
    hours = sum(len(x) for _, x in neg) / SR / 3600
    print(f"{len(pos)} greetings, {len(neg)} other clips ({hours * 60:.1f} minutes of audio that is not the greeting)")
    print("level   caught   false alarms   per hour")
    for level in LEVELS:
        exe = build_detector(level)
        caught = sum(1 for c in triggers(exe, pos) if c >= 1)
        false = sum(triggers(exe, neg))
        print(f"{level:<7} {caught:>3}/{len(pos)} ({100 * caught / len(pos):4.0f}%)   {false:>5}   {false / hours if hours else 0:8.1f}")
        exe.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
