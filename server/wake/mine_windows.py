"""Collects the moments where the board's "Hola Bug" detector fires, with the audio around each firing, to train the board's own second check
(wake/train_verifier.py). Two kinds: real greetings (the detector was right) and false alarms on everything that is not the greeting
(the detector was wrong): learning from its own mistakes is what lets the second check remove them.

    python wake/mine_windows.py            writes server/wake_data/mined.npz

Each window is 2.5 s starting 0.1 s before the voice begins (the same alignment the firmware uses), taken from 2 s before to 0.5 s after the firing.
Clips recorded at or after BUG_HOLDOUT_AFTER (default 20261109_164550) are your test set and are left out.
"""
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import baseline  # noqa: E402

SR = 16000
WINDOW = 40000
BEFORE, AFTER = 2 * SR, SR // 2
GAP = int(2.6 * SR)  # more than the detector's 2 s pause after a firing, so every clip gets its own chance
HOLDOUT = os.environ.get("BUG_HOLDOUT_AFTER", "20261109_164550")
NEG_COPIES, POS_COPIES = 3, 1   # perturbed copies of every clip (mining more mistakes of the detector)
LEVEL = 0.6          # a low level: we want its false alarms too
DATA = HERE.parent / "wake_data"


def onset(window, lead=0.1):
    """Where the voice begins in the window: the first 20 ms frame above a quarter of the loudest one, minus a short lead. Same rule as the firmware."""
    frame = SR // 50
    n = len(window) // frame
    level = np.abs(window[: n * frame].astype(np.float32)).reshape(n, frame).mean(axis=1)
    if n == 0 or level.max() < 1:
        return 0
    return max(0, int(np.argmax(level > 0.25 * level.max())) * frame - int(lead * SR))


def shape(window):
    start = onset(window)
    out = np.zeros(WINDOW, dtype=np.int16)
    part = window[start: start + WINDOW]
    out[: len(part)] = part
    return out


def stream_windows(exe, clips, rng, per_stream=40):
    """[(clip index, aligned window)] for every firing, running many clips through the detector in one go."""
    found = []
    for first in range(0, len(clips), per_stream):
        group = clips[first: first + per_stream]
        parts, starts, at = [], [], 0
        for _, x in group:
            quiet = (rng.standard_normal(GAP) * 20).astype(np.int16)
            parts += [quiet, x]
            starts.append(at + GAP)
            at += GAP + len(x)
        parts.append((rng.standard_normal(GAP) * 20).astype(np.int16))
        stream = np.concatenate(parts)
        raw = HERE / "mine.raw"
        raw.write_bytes(stream.tobytes())
        out = subprocess.run([str(exe), str(raw)], capture_output=True, text=True).stdout
        for line in out.splitlines():
            if line.startswith("TRIGGER"):
                trig = (int(line.split()[-1]) + 1) * 320
                owner = max(i for i, s in enumerate(starts) if s - GAP // 2 <= trig) if trig >= starts[0] - GAP // 2 else 0
                found.append((first + owner, shape(stream[max(0, trig - BEFORE): trig + AFTER])))
        raw.unlink(missing_ok=True)
    return found


def perturb(x, rng):
    """A changed copy of a clip, so the detector gets fresh chances to be wrong on speech it has memorised: another speed, another level, room noise."""
    speed = rng.uniform(0.88, 1.12)
    y = np.interp(np.arange(0, len(x), speed), np.arange(len(x)), x.astype(np.float32))
    y *= 10 ** rng.uniform(-0.8, 0.5)
    snr = rng.uniform(5, 30)
    noise = rng.standard_normal(len(y)).astype(np.float32)
    y += noise / (noise.std() + 1e-9) * (y.std() + 1) * 10 ** (-snr / 20)
    return np.clip(y, -32768, 32767).astype(np.int16)


def keep(path):
    return path.stem[:15] < HOLDOUT or not path.stem[:2] == "20"  # recordings carry their time in the name; synthetic clips do not


def folder_clips(folder, limit=None, copies=0, seed=1):
    """The clips of a folder (and subfolders), plus `copies` perturbed copies of each."""
    clips = []
    rng = np.random.default_rng(seed)
    for path in sorted(folder.rglob("*.wav")):
        if not keep(path):
            continue
        for name, x in baseline.read_clips_one(path):
            clips.append((name, x))
            clips += [(name, perturb(x, rng)) for _ in range(copies)]
    return clips[:limit] if limit else clips


def main():
    rng = np.random.default_rng(5)
    exe = baseline.build_detector(LEVEL)
    sources = {
        "pos": [DATA / "real" / "pos", DATA / "commands" / "wake", DATA / "synth" / "pos"],
        "neg": [DATA / "real" / "neg", DATA / "synth" / "neg", DATA / "corpus" / "speech", DATA / "corpus" / "noise", DATA / "commands" / "ruido"]
               + [d for d in sorted((DATA / "commands").glob("*")) if d.name not in ("wake", "ruido")],
    }
    out = {}
    for label, folders in sources.items():
        windows, origin = [], []
        for folder in folders:
            clips = folder_clips(folder, copies=NEG_COPIES if label == "neg" else POS_COPIES)
            if not clips:
                continue
            found = stream_windows(exe, clips, rng)
            fired = len({i for i, _ in found})
            print(f"{label:<4} {folder.relative_to(DATA)!s:<22} {len(clips):>5} clips -> {len(found):>5} firings (in {fired} clips)", flush=True)
            windows += [w for _, w in found]
            origin += [folder.name] * len(found)
        out[label] = np.array(windows, dtype=np.int16)
        out[label + "_origin"] = np.array(origin)
    np.savez_compressed(DATA / "mined.npz", **out)
    print("wrote", DATA / "mined.npz", {k: v.shape for k, v in out.items() if not k.endswith("origin")})
    exe.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
