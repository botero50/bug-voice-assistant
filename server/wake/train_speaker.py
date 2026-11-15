"""Makes the fixed projection the board uses to compare voices (firmware/main/voice_id_model.h) and measures how well it works.

The projection is a linear discriminant analysis over the 28 features (speaker_embed.py), learned from the ~26 neural voices of the
synthetic command clips, then 12 numbers per utterance. Honest checks, all on voices the projection never saw:

  1. who-is-it among a house of 3 or 5 (voices held out of the training)
  2. is-it-the-owner: the owner's own board recordings (server/wake_data/commands, real microphone) against the synthetic voices as strangers

    python server/wake/train_speaker.py            prints the numbers and writes voice_id_model.h
    python server/wake/train_speaker.py --no-write
"""
import argparse
import re
from collections import defaultdict
from pathlib import Path

import numpy as np

import speaker_embed as SE

ROOT = Path(__file__).resolve().parents[1] / "wake_data"
OUT = Path(__file__).resolve().parents[2] / "firmware" / "main" / "voice_id_model.h"
DIMS = 12
# Deliberately stricter than the equal-error point above (0.60): that test mixes a real microphone with synthetic voices, which is easier than telling
# real people apart, so a person who is not the owner would be accepted more often than the table says.
THRESHOLD = 0.70


def collect_voices():
    clips = defaultdict(list)
    for path in sorted((ROOT / "synth_commands").glob("*/*.wav")):
        m = re.match(r"(es[a-zA-Z]+Neural)_\d+$", path.stem)
        if m:
            clips[m.group(1)].append(path)
    out = {}
    for voice, paths in clips.items():
        good = [e for e in (SE.embed(SE.load(p)) for p in paths) if e is not None]
        if len(good) > 8:
            out[voice] = np.array(good)
    return out


def collect_owner():
    good = []
    for path in sorted((ROOT / "commands").glob("*/*.wav")):
        e = SE.embed(SE.load(path))
        if e is not None:
            good.append(e)
    return np.array(good)


def fit(groups, dims=DIMS):
    X = np.concatenate(groups)
    mu, sd = X.mean(0), X.std(0) + 1e-6
    Z = [(g - mu) / sd for g in groups]
    gm = np.concatenate(Z).mean(0)
    Sw = sum((z - z.mean(0)).T @ (z - z.mean(0)) for z in Z) / len(X)
    Sb = sum(len(z) * np.outer(z.mean(0) - gm, z.mean(0) - gm) for z in Z) / len(X)
    w, V = np.linalg.eig(np.linalg.solve(Sw + 1e-3 * np.eye(len(Sw)), Sb))
    return mu, sd, V[:, np.argsort(-w.real)[:dims]].real


def cos(a, b):
    return float(a @ b / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-write", action="store_true")
    parser.add_argument("--enroll", type=int, default=3)
    args = parser.parse_args()
    rng = np.random.default_rng(5)

    voices = collect_voices()
    names = sorted(voices)
    owner = collect_owner()
    print(f"{len(names)} synthetic voices, {len(owner)} real recordings of the owner")

    # 1. who is it, with the projection learned on other voices
    for people in (3, 5):
        ok = 0
        for _ in range(400):
            perm = list(rng.permutation(names))
            test, train = perm[:people], perm[people:]
            mu, sd, W = fit([voices[n] for n in train])
            proj = lambda e: ((e - mu) / sd) @ W
            cent, probes = {}, {}
            for m in test:
                o = rng.permutation(len(voices[m]))
                cent[m] = proj(voices[m][o[:args.enroll]]).mean(0)
                probes[m] = [proj(voices[m][i]) for i in o[args.enroll:]]
            who = test[rng.integers(people)]
            p = probes[who][rng.integers(len(probes[who]))]
            ok += max(cent, key=lambda m: cos(p, cent[m])) == who
        print(f"who is it, house of {people}: {100 * ok / 400:.1f}% right (synthetic voices held out of the training)")

    # 2. is it the owner? enroll with a few real clips, then genuine = other real clips, strangers = synthetic voices
    mu, sd, W = fit([voices[n] for n in names])
    proj = lambda e: ((e - mu) / sd) @ W
    genuine, stranger = [], []
    for _ in range(200):
        o = rng.permutation(len(owner))
        centroid = proj(owner[o[:args.enroll]]).mean(0)
        genuine += [cos(proj(owner[i]), centroid) for i in o[args.enroll:args.enroll + 5]]
        v = names[rng.integers(len(names))]
        stranger += [cos(proj(voices[v][rng.integers(len(voices[v]))]), centroid) for _ in range(5)]
    genuine, stranger = np.array(genuine), np.array(stranger)
    best = None
    for t in np.linspace(-0.5, 1.0, 151):
        far, frr = (stranger >= t).mean(), (genuine < t).mean()
        if best is None or abs(far - frr) < best[0]:
            best = (abs(far - frr), t, far, frr)
    _, t, far, frr = best
    print(f"owner vs synthetic strangers: equal-error threshold {t:.2f}: owner refused {100 * frr:.1f}%, stranger accepted {100 * far:.1f}%")
    for tt in (0.5, 0.6, 0.7):
        print(f"   at {tt:.2f}: owner accepted {100 * (genuine >= tt).mean():.1f}%, strangers accepted {100 * (stranger >= tt).mean():.1f}%")

    if args.no_write:
        return
    # the board's model: projection learned from all voices PLUS the owner's own microphone recordings as one more "speaker"
    # (so the recording chain of this board is part of what the projection ignores)
    mu, sd, W = fit([voices[n] for n in names] + [owner])
    lines = ["// Made by server/wake/train_speaker.py: the fixed projection that turns the 28 voice features into 12 numbers to compare voices.",
             "// Do not edit by hand.", "#ifndef VOICE_ID_MODEL_H", "#define VOICE_ID_MODEL_H", "",
             f"#define VID_FEATURES {SE.DIM}", f"#define VID_DIMS {DIMS}", f"#define VID_THRESHOLD {THRESHOLD:.2f}f", ""]
    def arr(name, a):
        flat = ", ".join(f"{x:.6g}f" for x in np.ravel(a))
        return f"static const float {name}[{a.size}] = {{{flat}}};"
    lines += [arr("VID_MEAN", mu), arr("VID_STD", sd), "// W[feature * VID_DIMS + dim]", arr("VID_W", W), "", "#endif", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"written {OUT}")


if __name__ == "__main__":
    main()
