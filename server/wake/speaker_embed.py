"""Reference "who is speaking" features, written so the board's C code (firmware/main/voice_id.c) gives the same numbers.

28 numbers per utterance: mean and standard deviation (over the speech frames) of 13 cepstral coefficients of a 24-band mel
filterbank (100-4000 Hz, 25 ms frames every 10 ms), then log of the median pitch and the pitch's relative spread.
"""
import wave
from pathlib import Path

import numpy as np

RATE = 16000
FRAME, HOP, FFT, BANDS, CEPS = 400, 160, 512, 24, 13
FMIN, FMAX = 100.0, 4000.0
DIM = 2 * CEPS + 2


def load(path):
    with wave.open(str(path), "rb") as f:
        raw = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
        if f.getnchannels() > 1:
            raw = raw[:: f.getnchannels()]
        if f.getframerate() != RATE:
            raw = np.interp(np.arange(0, len(raw), f.getframerate() / RATE), np.arange(len(raw)), raw)
    return raw


def _filterbank():
    mel = lambda f: 2595 * np.log10(1 + f / 700)
    inv = lambda m: 700 * (10 ** (m / 2595) - 1)
    pts = inv(np.linspace(mel(FMIN), mel(FMAX), BANDS + 2))
    bins = np.floor((FFT + 1) * pts / RATE).astype(int)
    fb = np.zeros((BANDS, FFT // 2 + 1))
    for i in range(BANDS):
        a, b, c = bins[i], bins[i + 1], bins[i + 2]
        for k in range(a, b):
            fb[i, k] = (k - a) / max(b - a, 1)
        for k in range(b, c):
            fb[i, k] = (c - k) / max(c - b, 1)
    return fb


def _dct():
    k = np.arange(CEPS + 1)[:, None]
    n = np.arange(BANDS)[None, :]
    return (np.cos(np.pi * k * (2 * n + 1) / (2 * BANDS)) * np.sqrt(2.0 / BANDS))[1:]


FB, D, WINDOW = _filterbank(), _dct(), np.hamming(FRAME)


def embed(audio):
    """The 28 numbers, or None when there is too little speech."""
    audio = np.asarray(audio, dtype=np.float32)
    if len(audio) < FRAME * 3:
        return None
    frames = np.stack([audio[i:i + FRAME] for i in range(0, len(audio) - FRAME, HOP)])
    energy = np.sqrt((frames ** 2).mean(axis=1))
    speech = energy > max(energy.max() * 0.15, 0.005)
    if speech.sum() < 15:
        return None
    voiced = frames[speech]
    spectrum = np.abs(np.fft.rfft(voiced * WINDOW, FFT)) ** 2
    ceps = np.log(spectrum @ FB.T + 1e-8) @ D.T
    lo, hi = RATE // 400, RATE // 60
    f0s = []
    for frame in voiced[::2]:
        frame = frame - frame.mean()
        ac = np.correlate(frame, frame, "full")[len(frame) - 1:]
        if ac[0] <= 0:
            continue
        k = ac[lo:hi].argmax() + lo
        if ac[k] / ac[0] > 0.35:
            f0s.append(RATE / k)
    f0, spread = (float(np.median(f0s)), float(np.std(f0s))) if len(f0s) >= 5 else (150.0, 0.0)
    return np.concatenate([ceps.mean(axis=0), ceps.std(axis=0), [np.log(f0), spread / f0]]).astype(np.float64)


if __name__ == "__main__":
    import sys
    e = embed(load(Path(sys.argv[1])))
    print(" ".join(f"{x:.5f}" for x in e) if e is not None else "no speech")
