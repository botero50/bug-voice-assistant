"""Trains the command recogniser that runs on the board: after "Hola Bug", the recorded sentence (up to 2.5 s) is
classified as one of the fixed commands, or "unknown". Run with the training environment:
    C:\\Users\\<you>\\bug-train-venv\\Scripts\\python wake\\train_commands.py [rounds]

Data: the owner's guided recordings (server/wake_data/commands), the same phrases by many synthetic voices
(synth_commands), and real Spanish speech and noise recordings (corpus) as "unknown".
"""
import json
import math
import random
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train as T  # the same features as the wake detector

DATA = T.DATA
OUT_HEADER = T.OUT_HEADER.with_name("command_model.h")
SR = 16000
WINDOW = 40000  # 2.5 s
FRAMES = (WINDOW + 56 - T.N_FFT) // T.HOP + 1  # 124
LEAD = 4800     # 0.3 s: what the board keeps from before the speech starts
device = T.device
random.seed(3)
rng = np.random.default_rng(3)

COMMANDS = [c for c in json.loads((Path(__file__).resolve().parent / "commands.json").read_text(encoding="utf-8"))
            if c["id"] not in ("wake", "ruido")]
CLASSES = [c["id"] for c in COMMANDS] + ["wake", "unknown"]
WAKE = len(CLASSES) - 2
UNKNOWN = len(CLASSES) - 1


class Net(nn.Module):
    def __init__(self, n_classes):
        super().__init__()
        self.c1 = nn.Conv2d(1, 8, 3, padding=1, bias=False)
        self.b1 = nn.BatchNorm2d(8)
        self.c2 = nn.Conv2d(8, 16, 3, padding=1, bias=False)
        self.b2 = nn.BatchNorm2d(16)
        self.c3 = nn.Conv2d(16, 32, 3, padding=1, bias=False)
        self.b3 = nn.BatchNorm2d(32)
        self.d1 = nn.Linear(32 * 3 * 15, 64)
        self.drop = nn.Dropout(0.3)
        self.d2 = nn.Linear(64, n_classes)

    def forward(self, x):
        x = F.max_pool2d(F.relu(self.b1(self.c1(x))), 2)  # 24x124 -> 12x62
        x = F.max_pool2d(F.relu(self.b2(self.c2(x))), 2)  # -> 6x31
        x = F.max_pool2d(F.relu(self.b3(self.c3(x))), 2)  # -> 3x15
        return self.d2(self.drop(F.relu(self.d1(x.flatten(1)))))


# ------------------------------------------------------------------------------------------------ data
def load_folder(folder, real):
    clips = []
    for path in sorted(folder.glob("*.wav")):
        x = T.read_wav(path)
        if len(x) < 3000 or np.abs(x).max() < 1e-3:
            continue
        x = x / np.abs(x).max()
        clips.append({"x": x, "real": real, "span": T.keyword_span(x, False), "name": path.name})
    return clips


def load_all():
    per_class = {c: [] for c in CLASSES}
    for c in COMMANDS:
        per_class[c["id"]] += load_folder(DATA / "commands" / c["id"], True)
        per_class[c["id"]] += load_folder(DATA / "synth_commands" / c["id"], False)
    wake = load_folder(DATA / "commands" / "wake", True) + load_folder(DATA / "real" / "pos", True)
    for clip in wake:  # real recordings of the greeting alone: it ends at the first pause
        clip["span"] = T.keyword_span(clip["x"], True)
    wake += load_folder(DATA / "synth" / "pos", False)
    per_class["wake"] = wake
    unknown_real = load_folder(DATA / "commands" / "ruido", True) + load_folder(DATA / "real" / "neg", True)
    speech = load_folder(DATA / "corpus" / "speech", True)
    noise = load_folder(DATA / "corpus" / "noise", True)
    return per_class, unknown_real, speech, noise


def window_from(clip, noise_pool):
    """A 2.5 s window shaped like what the board records: 0.3 s before the speech, the speech, then quiet."""
    x = clip["x"]
    start, end = clip["span"]
    voiced = x[start:end]
    if not clip["real"] or random.random() < 0.5:
        if random.random() < 0.7:
            factor = random.uniform(0.9, 1.12)
            voiced = np.interp(np.arange(0, len(voiced), factor), np.arange(len(voiced)), voiced).astype(np.float32)
    if len(voiced) > WINDOW - LEAD - 1600:
        voiced = voiced[: WINDOW - LEAD - 1600]
    lead = LEAD + random.randint(-1600, 1600)
    window = np.zeros(WINDOW, dtype=np.float32)
    window[lead: lead + len(voiced)] = voiced
    if not clip["real"] and T.EQ_DIFF is not None and random.random() < 0.8:
        window = T.apply_eq(window)
    if random.random() < 0.3:
        window = T.reverb(window)
    return window


def finish(window, noise_pool, babble_pool):
    level = 10 ** random.uniform(-2.0, -0.6)
    window = window / (np.abs(window).max() + 1e-9) * level * 3.0
    if random.random() < 0.85:
        if noise_pool and random.random() < 0.6:
            n = random.choice(noise_pool)["x"]
            start = random.randint(0, max(0, len(n) - 1))
            noise = np.tile(n, WINDOW // max(len(n), 1) + 2)[start: start + WINDOW]
        else:
            noise = T.background(WINDOW, babble_pool)
        snr = random.uniform(3, 35)
        window = window + noise / (np.std(noise) + 1e-9) * max(level * 0.6, 0.0005) * 10 ** (-snr / 20) * 3.0
    window = window + rng.standard_normal(WINDOW).astype(np.float32) * 0.0004
    return np.clip(window, -1, 1)


def unknown_window(pool_speech, pool_noise):
    """Something that is not a command: a piece of ordinary speech, or just noise."""
    if random.random() < 0.8 and pool_speech:
        clip = random.choice(pool_speech)
        x = clip["x"]
        length = random.randint(int(0.6 * SR), min(len(x), int(2.1 * SR))) if len(x) > int(0.7 * SR) else len(x)
        s = random.randint(0, max(0, len(x) - length))
        window = np.zeros(WINDOW, dtype=np.float32)
        lead = LEAD + random.randint(-1600, 1600)
        piece = x[s: s + length]
        window[lead: lead + len(piece)] = piece[: WINDOW - lead]
        return window, True
    return np.zeros(WINDOW, dtype=np.float32), False


def composite(wake_clip, command_clip):
    """The greeting, a short pause, then a command: one sentence ("Hola Bug, que hora es")."""
    ws, we = wake_clip["span"]
    cs, ce = command_clip["span"]
    gap = np.zeros(random.randint(1600, 6400), dtype=np.float32)
    x = np.concatenate([wake_clip["x"][ws:we], gap, command_clip["x"][cs:ce]])
    return {"x": x, "span": (0, len(x)), "real": wake_clip["real"] and command_clip["real"]}


def make_batch(n, train_clips, unknown_real, speech, noise, babble_pool):
    xs = np.zeros((n, WINDOW), dtype=np.float32)
    ys = np.zeros(n, dtype=np.int64)
    for i in range(n):
        if random.random() < 0.32:
            window, has_speech = unknown_window(speech + unknown_real, noise)
            if not has_speech:
                window = T.background(WINDOW, babble_pool) * 0.05 * 10 ** random.uniform(-1, 1)
                xs[i] = np.clip(window + rng.standard_normal(WINDOW).astype(np.float32) * 0.0004, -1, 1)
                ys[i] = UNKNOWN
                continue
            xs[i] = finish(window, noise, babble_pool)
            ys[i] = UNKNOWN
        elif random.random() < 0.22:
            wake_pool = train_clips["wake"]
            real_wake = [c for c in wake_pool if c["real"]]
            clip = random.choice(real_wake) if real_wake and random.random() < 0.6 else random.choice(wake_pool)
            if random.random() < 0.3:  # greeting and command in one breath
                command_pool = train_clips[random.choice(COMMANDS)["id"]]
                if command_pool:
                    clip = composite(clip, random.choice(command_pool))
            xs[i] = finish(window_from(clip, noise), noise, babble_pool)
            ys[i] = WAKE
        else:
            label = random.randrange(len(COMMANDS))
            pool = train_clips[COMMANDS[label]["id"]]
            if not pool:
                xs[i], ys[i] = finish(np.zeros(WINDOW, np.float32), noise, babble_pool), UNKNOWN
                continue
            # the owner's own recordings count more than the synthetic ones
            real = [c for c in pool if c["real"]]
            clip = random.choice(real) if real and random.random() < 0.6 else random.choice(pool)
            xs[i] = finish(window_from(clip, noise), noise, babble_pool)
            ys[i] = label
    return xs, ys


def split_real(per_class):
    """Hold out a quarter of the owner's recordings of each phrase to measure real accuracy."""
    train, test = {}, {}
    for label, clips in per_class.items():
        real = [c for c in clips if c["real"]]
        synth = [c for c in clips if not c["real"]]
        random.shuffle(real)
        cut = max(1, len(real) // 4) if len(real) >= 4 else 0
        test[label] = real[:cut]
        train[label] = real[cut:] + synth
    return train, test


def evaluate(model, test, noise, babble, speech_test):
    model.eval()
    per_class_ok, per_class_n = {}, {}
    wrong = 0
    rows = []
    for index, command in enumerate(COMMANDS):
        for clip in test[command["id"]]:
            for _ in range(3):
                rows.append((finish(window_from(clip, noise), noise, babble), index))
    for clip in test["wake"]:
        for _ in range(3):
            rows.append((finish(window_from(clip, noise), noise, babble), WAKE))
    for clip in speech_test[:300]:
        w, _ = unknown_window([clip], noise)
        rows.append((finish(w, noise, babble), UNKNOWN))
    if not rows:
        return None
    x = torch.tensor(np.stack([r[0] for r in rows]), device=device)
    y = np.array([r[1] for r in rows])
    with torch.no_grad():
        probs = torch.softmax(torch.cat([model(T.features(x[i:i + 200])) for i in range(0, len(x), 200)]), dim=1).cpu().numpy()
    predicted = probs.argmax(axis=1)
    confident = probs.max(axis=1)
    summary = {}
    for threshold in (0.5, 0.7, 0.85, 0.95):
        accepted = confident >= threshold
        commands_mask = y != UNKNOWN
        right = ((predicted == y) & accepted)[commands_mask].mean() if commands_mask.any() else float("nan")
        wake_mask = y == WAKE
        wake_right = ((predicted == y) & accepted)[wake_mask].mean() if wake_mask.any() else float("nan")
        false_accept = (accepted & (predicted != UNKNOWN))[~commands_mask].mean() if (~commands_mask).any() else float("nan")
        summary[threshold] = (round(float(right), 3), round(float(false_accept), 3), "wake", round(float(wake_right), 3))
    return summary


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    per_class, unknown_real, speech, noise = load_all()
    counts = {c: len(v) for c, v in per_class.items() if v}
    real_counts = {c: sum(1 for x in v if x["real"]) for c, v in per_class.items() if v}
    print(f"{len(COMMANDS)} commands; recordings (real/total): " + ", ".join(f"{c} {real_counts.get(c, 0)}/{counts.get(c, 0)}" for c in list(counts)[:6]) + " ...", flush=True)
    print(f"unknown: {len(unknown_real)} real, {len(speech)} speech, {len(noise)} noise", flush=True)

    # microphone colouring from the owner's real recordings against the synthetic ones
    real_all = [c for v in per_class.values() for c in v if c["real"]]
    synth_all = [c for v in per_class.values() for c in v if not c["real"]][:300]
    if len(real_all) >= 20 and synth_all:
        T.EQ_DIFF = T.mel_profile(real_all[:300]) - T.mel_profile(synth_all)
        print("microphone colouring:", np.round(T.EQ_DIFF, 2).tolist(), flush=True)

    train_clips, test_clips = split_real(per_class)
    random.shuffle(speech)
    speech_test, speech_train = speech[: len(speech) // 6], speech[len(speech) // 6:]
    babble = [c for v in train_clips.values() for c in v][:400] + speech_train[:200]
    model = Net(len(CLASSES)).to(device)
    per_round, epochs = 18000, 4
    optimizer = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=1e-3)
    steps = rounds * epochs * (per_round // 128 + 1)
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=3e-3, total_steps=steps)
    for r in range(rounds):
        started = time.time()
        xs, ys = make_batch(per_round, train_clips, unknown_real, speech_train, noise, babble)
        xs_t, ys_t = torch.tensor(xs), torch.tensor(ys)
        print(f"round {r + 1}/{rounds}: data {time.time() - started:.0f} s", flush=True)
        for epoch in range(epochs):
            model.train()
            order = torch.randperm(per_round)
            total = 0.0
            for i in range(0, per_round, 128):
                idx = order[i:i + 128]
                loss = F.cross_entropy(model(T.features(xs_t[idx].to(device))), ys_t[idx].to(device), label_smoothing=0.05)
                optimizer.zero_grad()
                loss.backward()
                optimizer.step()
                schedule.step()
                total += float(loss) * len(idx)
            print(f"  epoch {epoch + 1}: loss {total / per_round:.4f}", flush=True)
        print("  held out (threshold: right command, false accepts):", evaluate(model, test_clips, noise, babble, speech_test), flush=True)
    torch.save(model.state_dict(), Path(__file__).resolve().parent / "command_model.pt")
    export(model)


def export(model):
    model.eval()
    w1, b1 = T.fold(model.c1, model.b1)
    w2, b2 = T.fold(model.c2, model.b2)
    w3, b3 = T.fold(model.c3, model.b3)
    text = "// Generated by server/wake/train_commands.py: the command recogniser. Do not edit.\n#pragma once\n"
    text += f"#define CMD_N_CLASSES {len(CLASSES)}\n#define CMD_FRAMES {FRAMES}\n#define CMD_WINDOW {WINDOW}\n"
    text += "static const char *const cmd_class_ids[CMD_N_CLASSES] = {" + ",".join(f'"{c}"' for c in CLASSES) + "};\n"
    for name, arr in (("w1", w1), ("b1", b1), ("w2", w2), ("b2", b2), ("w3", w3), ("b3", b3),
                      ("d1w", model.d1.weight.detach().cpu().numpy()), ("d1b", model.d1.bias.detach().cpu().numpy()),
                      ("d2w", model.d2.weight.detach().cpu().numpy()), ("d2b", model.d2.bias.detach().cpu().numpy())):
        text += T.c_array("cmd_" + name, arr)
    OUT_HEADER.write_text(text, encoding="utf-8")
    print("wrote", OUT_HEADER, flush=True)


if __name__ == "__main__":
    main()
