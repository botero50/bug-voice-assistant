"""Trains the small "Hola Bug" detector that runs on the board, and exports it as a C header.

Run with the training environment (it has PyTorch with CUDA):
    C:\\Users\\<you>\\bug-train-venv\\Scripts\\python wake\\train.py

Data: server/wake_data/synth (many neural voices) and server/wake_data/real (clips your board sent, labelled by
Whisper). The detector looks at the last 1.5 seconds of audio: 24 log-mel bands, 74 frames (25 ms windows every
20 ms). The board computes exactly the same features in C (firmware/main/wake_local.c).
"""
import math
import random
import sys
import time
import wave
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent
DATA = HERE.parent / "wake_data"
OUT_HEADER = HERE.parent.parent / "firmware" / "main" / "wake_model.h"
SR = 16000
WINDOW = 24000            # 1.5 s
N_FFT, WIN, HOP = 512, 400, 320
N_MEL = 24
FRAMES = (WINDOW - WIN) // HOP + 1  # 74
FMIN, FMAX = 100.0, 7500.0
STD = 4.0
device = "cuda" if torch.cuda.is_available() else "cpu"
rng = np.random.default_rng(1)
random.seed(1)


# ------------------------------------------------------------------------------------ features
def mel_matrix():
    def hz_to_mel(f):
        return 2595.0 * np.log10(1.0 + f / 700.0)

    def mel_to_hz(m):
        return 700.0 * (10 ** (m / 2595.0) - 1.0)

    points = mel_to_hz(np.linspace(hz_to_mel(FMIN), hz_to_mel(FMAX), N_MEL + 2))
    freqs = np.arange(N_FFT // 2 + 1) * SR / N_FFT
    matrix = np.zeros((N_MEL, N_FFT // 2 + 1), dtype=np.float32)
    for i in range(N_MEL):
        lo, mid, hi = points[i], points[i + 1], points[i + 2]
        up = (freqs - lo) / (mid - lo)
        down = (hi - freqs) / (hi - mid)
        matrix[i] = np.maximum(0.0, np.minimum(up, down))
    return matrix


MEL = mel_matrix()
MEL_T = torch.tensor(MEL, device=device)
HANN = torch.tensor(0.5 - 0.5 * np.cos(2 * np.pi * np.arange(WIN) / (WIN - 1)), dtype=torch.float32, device=device)


def features(batch):
    """batch: float tensor (B, WINDOW) in [-1, 1] -> (B, 1, N_MEL, FRAMES), the same as the board computes."""
    batch = F.pad(batch, (56, 0))  # the board's 400-sample window ends at the newest sample (torch centres it in 512)
    spec = torch.stft(batch, n_fft=N_FFT, hop_length=HOP, win_length=WIN, window=HANN, center=False, return_complex=True)
    power = spec.real ** 2 + spec.imag ** 2                       # (B, 257, T)
    mel = torch.einsum("mf,bft->bmt", MEL_T, power)
    logmel = torch.log(mel + 1e-6)
    logmel = logmel - logmel.mean(dim=(1, 2), keepdim=True)       # the same whatever the volume
    return (logmel / STD).unsqueeze(1)


# ----------------------------------------------------------------------------------------- model
class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.c1 = nn.Conv2d(1, 6, 3, padding=1, bias=False)
        self.b1 = nn.BatchNorm2d(6)
        self.c2 = nn.Conv2d(6, 12, 3, padding=1, bias=False)
        self.b2 = nn.BatchNorm2d(12)
        self.c3 = nn.Conv2d(12, 24, 3, padding=1, bias=False)
        self.b3 = nn.BatchNorm2d(24)
        self.d1 = nn.Linear(24 * 3 * 9, 32)
        self.drop = nn.Dropout(0.3)
        self.d2 = nn.Linear(32, 1)

    def forward(self, x):
        x = F.max_pool2d(F.relu(self.b1(self.c1(x))), 2)   # 24x74 -> 12x37
        x = F.max_pool2d(F.relu(self.b2(self.c2(x))), 2)   # -> 6x18
        x = F.max_pool2d(F.relu(self.b3(self.c3(x))), 2)   # -> 3x9
        x = x.flatten(1)
        return self.d2(self.drop(F.relu(self.d1(x)))).squeeze(1)


# ------------------------------------------------------------------------------------------ data
def read_wav(path):
    with wave.open(str(path), "rb") as w:
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    return data


def voice_of(path):
    return path.name.split("_")[0] if "synth" in str(path) else "real"


def envelope(x, step=160):
    n = len(x) // step
    return np.sqrt((x[: n * step].reshape(n, step) ** 2).mean(axis=1) + 1e-12)


def keyword_span(x, real=False):
    """(start, end) sample of the spoken greeting. Synthetic clips are just the phrase; real clips can go on
    with a command, so the greeting ends at the first pause of 250 ms."""
    env = envelope(x)
    peak = env.max()
    voiced = env > peak * 0.04
    if not voiced.any():
        return 0, len(x)
    first = int(np.argmax(voiced))
    last = len(voiced) - 1 - int(np.argmax(voiced[::-1]))
    if real:
        gap = 0
        for i in range(first, len(voiced)):
            gap = gap + 1 if not voiced[i] else 0
            if gap >= 25:
                last = i - gap
                break
        else:
            last = min(last, first + 100)
    return first * 160, (last + 1) * 160


def load(folder, real=False):
    clips = []
    for path in sorted(folder.glob("*.wav")):
        x = read_wav(path)
        if len(x) < 4000 or np.abs(x).max() < 1e-3:
            continue
        clips.append({"x": x / np.abs(x).max(), "voice": voice_of(path), "span": keyword_span(x / np.abs(x).max(), real),
                      "real": real})
    return clips


def colored_noise(n, kind):
    white = rng.standard_normal(n).astype(np.float32)
    if kind == "white":
        return white
    spectrum = np.fft.rfft(white)
    freqs = np.fft.rfftfreq(n, 1 / SR)
    freqs[0] = 1
    power = {"pink": 0.5, "brown": 1.0, "blue": -0.5}[kind]
    shaped = np.fft.irfft(spectrum / freqs ** power, n).astype(np.float32)
    return shaped / (np.std(shaped) + 1e-9)


def background(n, babble_pool):
    """A random background: room noise, hum, babble of other speech."""
    kind = random.choice(["white", "pink", "brown", "pink", "hum", "babble", "babble", "silence"])
    if kind == "hum":
        t = np.arange(n) / SR
        base = random.choice([50.0, 60.0, 100.0, 120.0])
        sig = sum(np.sin(2 * math.pi * base * k * t + random.random() * 6) / k for k in range(1, 5))
        return (sig / np.std(sig) + 0.1 * colored_noise(n, "white")).astype(np.float32)
    if kind == "babble":
        out = np.zeros(n, dtype=np.float32)
        for _ in range(random.randint(2, 4)):
            clip = random.choice(babble_pool)["x"]
            start = random.randint(0, max(0, len(clip) - 1))
            piece = np.tile(clip, 3)[start:start + n]
            out[: len(piece)] += piece[:n] * random.uniform(0.3, 1.0)
        return out / (np.std(out) + 1e-9)
    if kind == "silence":
        return colored_noise(n, "white") * 0.05
    return colored_noise(n, kind)


def reverb(x):
    length = int(SR * random.uniform(0.08, 0.35))
    ir = rng.standard_normal(length).astype(np.float32) * np.exp(-np.arange(length) / (length / random.uniform(3, 7)))
    ir[0] = 1.0
    ir = ir / np.abs(ir).sum() * 2.5
    size = 1 << int(np.ceil(np.log2(len(x) + len(ir))))
    out = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir, size), size)[: len(x)]
    return out.astype(np.float32)


def speed(x):
    factor = random.uniform(0.92, 1.08)
    return np.interp(np.arange(0, len(x), factor), np.arange(len(x)), x).astype(np.float32)


EQ_DIFF = None   # per mel band: how the real microphone differs from the synthetic voices (set from the real clips)


def mel_profile(clips):
    """Mean log-mel of the voiced part of each clip, per band, with the overall mean removed."""
    rows = []
    for c in clips:
        s0, e0 = c["span"]
        x = c["x"][s0:e0]
        if len(x) < 4000:
            continue
        x = np.pad(x, (0, max(0, WINDOW - len(x))))[:WINDOW]
        rows.append(features(torch.tensor(x, dtype=torch.float32, device=device).unsqueeze(0))[0, 0].cpu().numpy().mean(axis=1) * STD)
    profile = np.mean(rows, axis=0)
    return profile - profile.mean()


def apply_eq(x):
    """Colours a synthetic voice like the board's microphone (measured from real clips), with some randomness."""
    size = len(x)
    spectrum = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(size, 1 / SR)
    centers = np.linspace(FMIN, FMAX, N_MEL)
    curve = EQ_DIFF * random.uniform(0.6, 1.2) + rng.normal(0, 0.25, N_MEL) + np.linspace(-1, 1, N_MEL) * rng.normal(0, 0.3)
    gain = np.exp(np.interp(freqs, centers, curve) / 2.0)
    return np.fft.irfft(spectrum * gain, size).astype(np.float32)


def render(clip, end_offset, babble_pool, with_keyword=True):
    """A 1.5 s window cut from the clip so that the greeting ENDS `end_offset` seconds before the window's end."""
    x = clip["x"]
    start, end = clip["span"]
    if random.random() < 0.7:
        x = speed(x)
        scale = len(x) / len(clip["x"])
        start, end = int(start * scale), int(end * scale)
    if EQ_DIFF is not None and not clip.get("real") and random.random() < 0.8:
        x = apply_eq(x)
    if random.random() < 0.3:
        x = reverb(x)
    if random.random() < 0.25:  # a duller or a thinner microphone
        cutoff = random.uniform(2500, 6500)
        a = math.exp(-2 * math.pi * cutoff / SR)
        kernel = ((1 - a) * a ** np.arange(256)).astype(np.float32)
        x = np.convolve(x, kernel)[: len(x)].astype(np.float32)
    pad = np.zeros(WINDOW * 2, dtype=np.float32)
    x = np.concatenate([pad, x, pad])
    window_end = (start_len := WINDOW * 2) + end + int(end_offset * SR)
    chunk = x[window_end - WINDOW: window_end]
    return chunk


def finish(chunk, babble_pool, speech_level=None):
    """Level, background noise, clipping: what the microphone would deliver."""
    level = speech_level if speech_level else 10 ** random.uniform(-2.0, -0.6)   # speech rms 0.01 - 0.25
    rms = np.sqrt((chunk ** 2).mean() + 1e-12)
    chunk = chunk / (np.abs(chunk).max() + 1e-9) * level * 3.0
    if random.random() < 0.85:
        noise = background(WINDOW, babble_pool)
        snr_db = random.uniform(2, 35)
        noise_rms = max(level * 0.6, 0.0005) * 10 ** (-snr_db / 20)
        chunk = chunk + noise / (np.std(noise) + 1e-9) * noise_rms * 3.0
    chunk = chunk + rng.standard_normal(WINDOW).astype(np.float32) * 0.0004  # the microphone's own hiss
    return np.clip(chunk, -1.0, 1.0)


def make_batch(n, pos, neg, babble_pool, hard_neg_ratio=0.5):
    xs = np.zeros((n, WINDOW), dtype=np.float32)
    ys = np.zeros(n, dtype=np.float32)
    for i in range(n):
        r = random.random()
        if r < 0.40 and pos:                                   # the greeting just ended: wake
            clip = random.choice(pos)
            chunk = render(clip, random.uniform(0.04, 0.55), babble_pool)
            ys[i] = 1.0
        elif r < 0.52 and pos:                                 # window cut before "Bug" finished, or long after
            clip = random.choice(pos)
            offset = random.choice([random.uniform(-0.7, -0.22), random.uniform(0.9, 1.3)])
            chunk = render(clip, offset, babble_pool)
        elif r < 0.85 and neg:                                 # other speech
            clip = random.choice(neg)
            span = len(clip["x"])
            chunk = render(clip, random.uniform(-0.2, 1.2), babble_pool) if span > 0 else np.zeros(WINDOW, np.float32)
        else:                                                  # no speech: background only
            chunk = np.zeros(WINDOW, dtype=np.float32)
            xs[i] = 0
        if r >= 0.85 or not np.any(chunk):
            chunk = background(WINDOW, babble_pool) * 0.05 * 10 ** random.uniform(-1, 1)
            xs[i] = np.clip(chunk + rng.standard_normal(WINDOW).astype(np.float32) * 0.0004, -1, 1)
        else:
            xs[i] = finish(chunk, babble_pool)
    return xs, ys


# ------------------------------------------------------------------------------------------- run
def evaluate(model, pos, neg, babble_pool, n=3000):
    model.eval()
    xs, ys = [], []
    x, y = make_batch(n, pos, neg, babble_pool)
    with torch.no_grad():
        scores = []
        for i in range(0, n, 500):
            batch = torch.tensor(x[i:i + 500], device=device)
            scores.append(torch.sigmoid(model(features(batch))).cpu().numpy())
    scores = np.concatenate(scores)
    positive, negative = scores[y == 1], scores[y == 0]
    result = {}
    for threshold in (0.5, 0.7, 0.8, 0.9, 0.95, 0.98):
        result[threshold] = (float((positive > threshold).mean()), float((negative > threshold).mean()))
    return result


def main():
    pos_synth, neg_synth = load(DATA / "synth" / "pos"), load(DATA / "synth" / "neg")
    pos_real, neg_real = load(DATA / "real" / "pos", real=True), load(DATA / "real" / "neg", real=True)
    # The guided recordings: "Hola Bug" is the positive; every other phrase, ordinary speech and noise are negatives
    pos_real += load(DATA / "commands" / "wake", real=True)
    for folder in sorted((DATA / "commands").glob("*")):
        if folder.name != "wake":
            neg_real += load(folder, real=True)
    corpus_speech = load(DATA / "corpus" / "speech", real=True)
    random.shuffle(corpus_speech)
    neg_real += corpus_speech[:700]
    neg_real += load(DATA / "corpus" / "noise", real=True)[:300]
    print(f"synthetic: {len(pos_synth)} positive, {len(neg_synth)} negative; real: {len(pos_real)} positive, {len(neg_real)} negative", flush=True)
    voices = sorted({c["voice"] for c in pos_synth + neg_synth})
    random.shuffle(voices)
    held_out = set(voices[: max(3, len(voices) // 7)])
    global EQ_DIFF
    if len(pos_real) >= 8:
        EQ_DIFF = mel_profile(pos_real) - mel_profile(pos_synth[:300])
        print("microphone colouring (log-mel per band):", np.round(EQ_DIFF, 2).tolist(), flush=True)
    train_pos = [c for c in pos_synth if c["voice"] not in held_out] + pos_real * max(8, 300 // max(len(pos_real), 1))   # real clips count more
    train_neg = [c for c in neg_synth if c["voice"] not in held_out] + neg_real * 2
    test_pos = [c for c in pos_synth if c["voice"] in held_out]
    test_neg = [c for c in neg_synth if c["voice"] in held_out]
    babble = train_neg + train_pos
    print(f"held-out voices: {sorted(held_out)}", flush=True)

    model = Net().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=1e-3)
    rounds, epochs_per_round, per_round = int(sys.argv[1]) if len(sys.argv) > 1 else 6, 4, 24000
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=3e-3, total_steps=rounds * epochs_per_round * (per_round // 256 + 1))
    for round_index in range(rounds):
        started = time.time()
        xs, ys = make_batch(per_round, train_pos, train_neg, babble)
        xs_t, ys_t = torch.tensor(xs), torch.tensor(ys)
        print(f"round {round_index + 1}/{rounds}: data {time.time() - started:.0f} s", flush=True)
        for epoch in range(epochs_per_round):
            model.train()
            order = torch.randperm(per_round)
            total = 0.0
            for i in range(0, per_round, 256):
                idx = order[i:i + 256]
                batch = features(xs_t[idx].to(device))
                target = ys_t[idx].to(device)
                loss = F.binary_cross_entropy_with_logits(model(batch), target, pos_weight=torch.tensor(1.0, device=device))
                optimizer.zero_grad()
                loss.backward()
                optimizer.step()
                schedule.step()
                total += float(loss) * len(idx)
            print(f"  epoch {epoch + 1}: loss {total / per_round:.4f}", flush=True)
        result = evaluate(model, test_pos, test_neg, babble)
        print("  held-out voices (threshold: detects wake, false alarms):", {k: (round(v[0], 3), round(v[1], 4)) for k, v in result.items()}, flush=True)

    torch.save(model.state_dict(), HERE / "wake_model.pt")
    export(model)


def fold(conv, bn):
    """Fold a batch norm into the convolution before it: returns weights (out, in, 3, 3) and bias (out)."""
    scale = bn.weight / torch.sqrt(bn.running_var + bn.eps)
    weight = conv.weight * scale.view(-1, 1, 1, 1)
    bias = bn.bias - bn.running_mean * scale
    return weight.detach().cpu().numpy(), bias.detach().cpu().numpy()


def c_array(name, array):
    flat = np.asarray(array, dtype=np.float32).flatten()
    def literal(v):
        text = f"{v:.7g}"
        return text + ("f" if any(c in text for c in ".e") else ".0f")

    body = ",".join(literal(v) for v in flat)
    return f"static const float {name}[{len(flat)}] = {{{body}}};\n"


def export(model):
    model.eval()
    w1, b1 = fold(model.c1, model.b1)
    w2, b2 = fold(model.c2, model.b2)
    w3, b3 = fold(model.c3, model.b3)
    text = "// Generated by server/wake/train.py: the \"Hola Bug\" detector. Do not edit.\n#pragma once\n"
    text += f"#define WAKE_N_MEL {N_MEL}\n#define WAKE_FRAMES {FRAMES}\n#define WAKE_FEATURE_STD {STD}f\n"
    text += c_array("wake_mel", MEL)
    text += c_array("wake_hann", HANN.cpu().numpy())
    for name, arr in (("w1", w1), ("b1", b1), ("w2", w2), ("b2", b2), ("w3", w3), ("b3", b3),
                      ("d1w", model.d1.weight.detach().cpu().numpy()), ("d1b", model.d1.bias.detach().cpu().numpy()),
                      ("d2w", model.d2.weight.detach().cpu().numpy()), ("d2b", model.d2.bias.detach().cpu().numpy())):
        text += c_array("wake_" + name, arr)
    OUT_HEADER.write_text(text, encoding="utf-8")
    print("wrote", OUT_HEADER, flush=True)


if __name__ == "__main__":
    main()
