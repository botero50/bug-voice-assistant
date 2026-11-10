"""The two-stage wake done entirely on the board: the "Hola Bug" detector fires, then the board's own command classifier (command_local.c, the one that
knows "wake" and "unknown") looks at the audio around the firing and must say "wake". No PC, no internet.

    python wake/onboard_two_stage_eval.py --after 20261109_180353 [--levels 0.8,0.9] [--before 1.6] [--after-s 0.3] [--prob 0.5]

Same clips and rules as wake/two_stage_eval.py (greetings recorded after --after; false alarms on all the not-greeting audio), so the numbers compare.
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import baseline  # noqa: E402
import two_stage_eval as t  # noqa: E402  (firings())

SR = 16000
EXE = HERE / "host_cmd.exe"


def build():
    subprocess.run(["gcc", "-O2", "-I", str(HERE / "stubs"), "-I", str(baseline.ROOT / "firmware" / "main"), str(HERE / "host_cmd.c"),
                    str(baseline.ROOT / "firmware" / "main" / "command_local.c"), "-lm", "-o", str(EXE)], check=True, capture_output=True)


def align(window, lead=0.1):
    """Starts the window a little before the voice begins (the board's own recordings start about there): the first 20 ms frame whose level
    is more than a quarter of the loudest one, counted from the left."""
    frame = SR // 50
    x = window.astype(np.float32)
    n = len(x) // frame
    level = np.abs(x[: n * frame]).reshape(n, frame).mean(axis=1)
    if n == 0 or level.max() < 1:
        return window
    onset = int(np.argmax(level > 0.25 * level.max()))
    return window[max(0, onset * frame - int(lead * SR)):]


def classify(window):
    window = align(window)
    raw = HERE / "cmd_window.raw"
    raw.write_bytes(window.astype(np.int16).tobytes())
    lines = subprocess.run([str(EXE), str(raw)], capture_output=True, text=True).stdout.splitlines()
    raw.unlink(missing_ok=True)
    for line in lines:
        parts = line.split()
        if len(parts) == 3 and parts[0] == "CLASS":
            return parts[1], float(parts[2])
    return "", 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--after", default="")
    parser.add_argument("--levels", default="0.8,0.9")
    parser.add_argument("--before", type=float, default=1.6, help="seconds of audio before the firing given to the classifier")
    parser.add_argument("--after-s", type=float, default=0.3, help="seconds after the firing")
    parser.add_argument("--prob", type=float, default=0.5, help="minimum probability of the class wake")
    args = parser.parse_args()
    t.BEFORE, t.AFTER = int(args.before * SR), int(args.after_s * SR)
    build()
    pos = baseline.read_clips(baseline.DATA / "pos", args.after) + baseline.read_clips(baseline.DATA.parent / "commands" / "wake", args.after)
    neg = baseline.read_clips(baseline.DATA / "neg", "") + baseline.read_clips(baseline.DATA.parent / "commands" / "ruido", "")
    hours = sum(len(x) for _, x in neg) / SR / 3600
    rng = np.random.default_rng(11)
    print(f"{len(pos)} greetings, {len(neg)} other clips ({hours * 60:.0f} minutes); window {args.before}s before + {args.after_s}s after the firing, wake probability >= {args.prob}")
    print("level  detector alone: caught  FA/h   |  detector + on-board class check: caught  FA/h")
    for level in map(float, args.levels.split(",")):
        exe = baseline.build_detector(level)
        a = b = 0
        for _, x in pos:
            windows = t.firings(exe, x, rng)
            if windows:
                a += 1
                if any((lambda r: r[0] == "wake" and r[1] >= args.prob)(classify(w)) for w in windows[:2]):
                    b += 1
        fa_a = fa_b = 0
        for _, x in neg:
            for w in t.firings(exe, x, rng):
                fa_a += 1
                r = classify(w)
                if r[0] == "wake" and r[1] >= args.prob:
                    fa_b += 1
        print(f"{level:<6} {a:>3}/{len(pos)} ({100 * a / len(pos):3.0f}%) {fa_a / hours:7.1f}   |   {b:>3}/{len(pos)} ({100 * b / len(pos):3.0f}%) {fa_b / hours:7.1f}", flush=True)
        exe.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
