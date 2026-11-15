"""Measures how well a small on-board "who is speaking" method works, before putting it in the firmware.

Method (what the board would run): log-mel energies per 25 ms frame (24 bands, 100-4000 Hz), keep the frames that are speech, take the
mean and the standard deviation of each band (48 numbers), scale with fixed constants, compare with each person's average by cosine.

The only different voices on this PC are the ~26 neural voices of the synthetic command clips (server/wake_data/synth_commands), so
they stand in for family members: each voice has ~29 clips. Real people sound alike more often than these voices do, so read the numbers
as an upper bound, and re-measure with real family recordings (enroll through "Hola Bug, soy ...").

    python server/wake/speaker_eval.py [--enroll 3] [--people 4] [--trials 200]
"""
import argparse
import re
import wave
from collections import defaultdict
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1] / "wake_data"
RATE = 16000
FRAME, HOP, BANDS = 400, 160, 24
FMIN, FMAX = 100.0, 4000.0


def load(path):
    with wave.open(str(path), "rb") as f:
        raw = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
        if f.getnchannels() > 1:
            raw = raw[:: f.getnchannels()]
        if f.getframerate() != RATE:
            raw = np.interp(np.arange(0, len(raw), f.getframerate() / RATE), np.arange(len(raw)), raw)
    return raw


def mel_points(n, fmin, fmax):
    mel = lambda f: 2595 * np.log10(1 + f / 700)
    inv = lambda m: 700 * (10 ** (m / 2595) - 1)
    return inv(np.linspace(mel(fmin), mel(fmax), n))


def filterbank():
    fft_size = 512
    pts = mel_points(BANDS + 2, FMIN, FMAX)
    bins = np.floor((fft_size + 1) * pts / RATE).astype(int)
    fb = np.zeros((BANDS, fft_size // 2 + 1))
    for i in range(BANDS):
        a, b, c = bins[i], bins[i + 1], bins[i + 2]
        for k in range(a, b):
            fb[i, k] = (k - a) / max(b - a, 1)
        for k in range(b, c):
            fb[i, k] = (c - k) / max(c - b, 1)
    return fb


FB = filterbank()
WINDOW = np.hamming(FRAME)


def embedding(audio):
    """48 numbers: mean and standard deviation of the log-mel bands over the speech frames, or None if there is no speech."""
    if len(audio) < FRAME * 3:
        return None
    frames = np.stack([audio[i:i + FRAME] * WINDOW for i in range(0, len(audio) - FRAME, HOP)])
    energy = np.sqrt((frames ** 2).mean(axis=1))
    speech = energy > max(energy.max() * 0.15, 0.005)
    if speech.sum() < 15:
        return None
    spectrum = np.abs(np.fft.rfft(frames[speech], 512)) ** 2
    logmel = np.log(spectrum @ FB.T + 1e-8)
    logmel -= logmel.mean(axis=1, keepdims=True) * 0  # (kept absolute: loudness is removed below by the scaling per clip)
    return np.concatenate([logmel.mean(axis=0), logmel.std(axis=0)])


def collect():
    clips = defaultdict(list)
    for path in sorted((ROOT / "synth_commands").glob("*/*.wav")):
        match = re.match(r"(es[a-zA-Z]+Neural)_\d+$", path.stem)
        if match:
            clips[match.group(1)].append(path)
    return clips


def cosine(a, b):
    return float(a @ b / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--enroll", type=int, default=3, help="clips used to enroll each person")
    parser.add_argument("--people", type=int, default=4, help="how many people are in the house")
    parser.add_argument("--trials", type=int, default=300)
    args = parser.parse_args()
    rng = np.random.default_rng(1)

    voices = collect()
    embs = {}
    for voice, paths in voices.items():
        good = [e for e in (embedding(load(p)) for p in paths) if e is not None]
        if len(good) > args.enroll + 3:
            embs[voice] = np.array(good)
    names = sorted(embs)
    print(f"{len(names)} voices, {sum(len(v) for v in embs.values())} usable clips")

    # fixed scaling constants (what the board would store): mean and std of every number over all clips
    allv = np.concatenate(list(embs.values()))
    mu, sd = allv.mean(axis=0), allv.std(axis=0) + 1e-6
    scale = lambda e: (e - mu) / sd

    correct = wrong = unknown_ok = unknown_bad = 0
    for _ in range(args.trials):
        house = list(rng.choice(names, size=args.people + 1, replace=False))
        members, stranger = house[:-1], house[-1]
        centroids, held_out = {}, {}
        for m in members:
            order = rng.permutation(len(embs[m]))
            centroids[m] = scale(embs[m][order[: args.enroll]]).mean(axis=0)
            held_out[m] = [scale(embs[m][i]) for i in order[args.enroll:]]
        # a member speaks
        who = members[rng.integers(len(members))]
        probe = held_out[who][rng.integers(len(held_out[who]))]
        scores = {m: cosine(probe, c) for m, c in centroids.items()}
        best = max(scores, key=scores.get)
        if best == who:
            correct += 1
        else:
            wrong += 1
        # a stranger speaks: should score below a threshold
        stranger_probe = scale(embs[stranger][rng.integers(len(embs[stranger]))])
        stranger_best = max(cosine(stranger_probe, c) for c in centroids.values())
        member_best = max(scores.values())
        if stranger_best < member_best:
            unknown_ok += 1
        else:
            unknown_bad += 1
    total = correct + wrong
    print(f"house of {args.people}, {args.enroll} enrollment clips each, {total} trials")
    print(f"  right person picked:                     {100 * correct / total:5.1f}%")
    print(f"  stranger scores below a member (rough):  {100 * unknown_ok / (unknown_ok + unknown_bad):5.1f}%")


if __name__ == "__main__":
    main()
