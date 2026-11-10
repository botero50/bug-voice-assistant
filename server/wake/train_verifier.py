"""Trains the board's own second check of "Hola Bug": a small network that looks at the 2.5 s around a firing of the detector and says whether it
really was the greeting. It learns from the detector's real mistakes (wake/mine_windows.py), runs on the board, and needs no PC and no internet.
Run with the training environment:
    C:\\Users\\<you>\\bug-train-venv\\Scripts\\python wake\\train_verifier.py [epochs]
Writes firmware/main/verify_model.h (and wake/verify_model.pt).
"""
import random
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import train as T  # noqa: E402  (the same features as the detector and the command recogniser)

DATA = T.DATA
OUT_HEADER = T.OUT_HEADER.with_name("verify_model.h")
WINDOW = 40000
FRAMES = (WINDOW + 56 - T.N_FFT) // T.HOP + 1  # 124
device = T.device
random.seed(9)
rng = np.random.default_rng(9)
torch.manual_seed(9)


class Net(nn.Module):
    """The same shape as the command recogniser's body, with one output: the greeting or not."""

    def __init__(self):
        super().__init__()
        self.c1 = nn.Conv2d(1, 8, 3, padding=1, bias=False)
        self.b1 = nn.BatchNorm2d(8)
        self.c2 = nn.Conv2d(8, 16, 3, padding=1, bias=False)
        self.b2 = nn.BatchNorm2d(16)
        self.c3 = nn.Conv2d(16, 32, 3, padding=1, bias=False)
        self.b3 = nn.BatchNorm2d(32)
        self.d1 = nn.Linear(32 * 3 * 15, 64)
        self.drop = nn.Dropout(0.35)
        self.d2 = nn.Linear(64, 1)

    def forward(self, x):
        x = F.max_pool2d(F.relu(self.b1(self.c1(x))), 2)
        x = F.max_pool2d(F.relu(self.b2(self.c2(x))), 2)
        x = F.max_pool2d(F.relu(self.b3(self.c3(x))), 2)
        return self.d2(self.drop(F.relu(self.d1(x.flatten(1))))).squeeze(1)


def augment(batch):
    """batch: (B, WINDOW) float in [-1, 1]. Random level, a little room noise and a small shift, like different rooms and moments."""
    out = np.empty_like(batch)
    for i, x in enumerate(batch):
        shift = random.randint(-1600, 1600)
        x = np.roll(x, shift)
        if shift > 0:
            x[:shift] = 0
        elif shift < 0:
            x[shift:] = 0
        x = x * 10 ** random.uniform(-0.7, 0.5)
        if random.random() < 0.6:
            noise = rng.standard_normal(WINDOW).astype(np.float32)
            x = x + noise / (noise.std() + 1e-9) * (np.abs(x).mean() + 1e-4) * 10 ** (-random.uniform(8, 30) / 20)
        out[i] = np.clip(x, -1, 1)
    return out


def scores(model, x):
    model.eval()
    with torch.no_grad():
        return torch.sigmoid(torch.cat([model(T.features(torch.tensor(x[i:i + 256]).to(device))) for i in range(0, len(x), 256)])).cpu().numpy()


def main():
    epochs = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    mined = np.load(DATA / "mined.npz")
    pos = mined["pos"].astype(np.float32) / 32768.0
    neg = mined["neg"].astype(np.float32) / 32768.0
    order_p, order_n = rng.permutation(len(pos)), rng.permutation(len(neg))
    cut_p, cut_n = len(pos) * 85 // 100, len(neg) * 85 // 100
    train_x = np.concatenate([pos[order_p[:cut_p]], neg[order_n[:cut_n]]])
    train_y = np.concatenate([np.ones(cut_p), np.zeros(cut_n)]).astype(np.float32)
    val_pos, val_neg = pos[order_p[cut_p:]], neg[order_n[cut_n:]]
    print(f"train: {cut_p} greetings, {cut_n} false alarms | validation: {len(val_pos)} and {len(val_neg)}", flush=True)

    model = Net().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-3)
    steps = epochs * (len(train_x) // 64 + 1)
    schedule = torch.optim.lr_scheduler.OneCycleLR(optimizer, max_lr=3e-3, total_steps=steps)
    weight = torch.tensor(len(neg) / len(pos) ** 1.0).clamp(0.5, 2.0).to(device)  # the false alarms matter more: do not let greetings dominate
    for epoch in range(epochs):
        started = time.time()
        model.train()
        perm = rng.permutation(len(train_x))
        total = 0.0
        for i in range(0, len(perm), 64):
            idx = perm[i:i + 64]
            xb = torch.tensor(augment(train_x[idx])).to(device)
            yb = torch.tensor(train_y[idx]).to(device)
            loss = F.binary_cross_entropy_with_logits(model(T.features(xb)), yb * 0.95 + 0.025, weight=torch.where(yb > 0.5, torch.ones_like(yb), weight * 0 + 1.0))
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            schedule.step()
            total += float(loss) * len(idx)
        if epoch % 5 == 4 or epoch == epochs - 1:
            sp, sn = scores(model, val_pos), scores(model, val_neg)
            row = {t: (round(float((sp > t).mean()), 3), round(float((sn > t).mean()), 3)) for t in (0.3, 0.5, 0.7, 0.9)}
            print(f"epoch {epoch + 1:>3}: loss {total / len(perm):.4f} ({time.time() - started:.1f} s)  validation (threshold: greetings kept, false alarms kept): {row}", flush=True)
    torch.save(model.state_dict(), HERE / "verify_model.pt")
    export(model)


def export(model):
    model.eval()
    w1, b1 = T.fold(model.c1, model.b1)
    w2, b2 = T.fold(model.c2, model.b2)
    w3, b3 = T.fold(model.c3, model.b3)
    text = "// Generated by server/wake/train_verifier.py: the board's own second check of the greeting. Do not edit.\n#pragma once\n"
    text += f"#define VFY_FRAMES {FRAMES}\n#define VFY_WINDOW {WINDOW}\n"
    for name, arr in (("w1", w1), ("b1", b1), ("w2", w2), ("b2", b2), ("w3", w3), ("b3", b3),
                      ("d1w", model.d1.weight.detach().cpu().numpy()), ("d1b", model.d1.bias.detach().cpu().numpy()),
                      ("d2w", model.d2.weight.detach().cpu().numpy()), ("d2b", model.d2.bias.detach().cpu().numpy())):
        text += T.c_array("vfy_" + name, arr)
    OUT_HEADER.write_text(text, encoding="utf-8")
    print("wrote", OUT_HEADER, flush=True)


if __name__ == "__main__":
    main()
