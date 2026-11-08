"""Replays held-out voices and real clips through the board's C detector (built for this PC) as one long stream,
at several trigger levels: how many greetings are caught, and how many false alarms per hour of speech."""
import subprocess, sys, re
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train

HERE = Path(__file__).resolve().parent
log = (Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace") if len(sys.argv) > 1 else "")
held = set(re.findall(r"'([^']+)'", next((l for l in log.splitlines() if l.startswith("held-out voices")), "")))
pos = [c for c in train.load(train.DATA / "synth" / "pos") if c["voice"] in held]
neg = [c for c in train.load(train.DATA / "synth" / "neg") if c["voice"] in held]
real_pos = train.load(train.DATA / "real" / "pos", real=True)
real_neg = train.load(train.DATA / "real" / "neg", real=True)
print(f"held-out voices {len(held)}: {len(pos)} positive and {len(neg)} negative clips; real: {len(real_pos)} positive, {len(real_neg)} negative")

rng = np.random.default_rng(5)
SR = 16000


def build(clips, with_keyword, n):
    parts, marks, t = [], [], 0
    for _ in range(n):
        c = clips[rng.integers(len(clips))]
        gap = rng.integers(int(1.2 * SR), int(2.5 * SR))
        parts.append((rng.standard_normal(gap) * 0.0006).astype(np.float32)); t += gap
        level = 10 ** rng.uniform(-1.8, -0.7)
        x = c["x"] * level
        noise = rng.standard_normal(len(x)).astype(np.float32) * level * 10 ** (-rng.uniform(8, 30) / 20)
        parts.append(x + noise)
        if with_keyword:
            marks.append((t + c["span"][1]) / SR)
        t += len(x)
    parts.append((rng.standard_normal(SR * 2) * 0.0006).astype(np.float32))
    return (np.clip(np.concatenate(parts), -1, 1) * 32767).astype(np.int16), marks


def run(audio, threshold):
    raw = HERE / "stream.raw"
    raw.write_bytes(audio.tobytes())
    exe = HERE / f"host_{threshold}.exe"
    subprocess.run(["gcc", "-O2", f"-DTHRESHOLD={threshold}f", "-I", str(HERE / "stubs"), "-I", str(HERE.parent.parent / "firmware" / "main"),
                    str(HERE / "host_test.c"), str(HERE.parent.parent / "firmware" / "main" / "wake_local.c"), "-lm", "-o", str(exe)], check=True)
    out = subprocess.run([str(exe), str(raw)], capture_output=True, text=True).stdout
    exe.unlink(missing_ok=True)
    return [int(l.split()[3]) * 0.02 for l in out.splitlines() if l.startswith("TRIGGER")]


streams = {"synthetic positives": build(pos, True, 150), "synthetic negatives": build(neg, False, 400)}
if real_pos:
    streams["real positives"] = build(real_pos, True, 60)
if real_neg:
    streams["real negatives"] = build(real_neg, False, 40)
for threshold in (0.80, 0.90, 0.95, 0.98):
    print(f"threshold {threshold}")
    for name, (audio, marks) in streams.items():
        triggers = run(audio, threshold)
        minutes = len(audio) / SR / 60
        if marks:
            caught = sum(any(m - 0.2 <= t <= m + 1.0 for t in triggers) for m in marks)
            extra = sum(not any(m - 0.2 <= t <= m + 1.0 for m in marks) for t in triggers)
            print(f"   {name}: caught {caught}/{len(marks)} ({caught / len(marks):.0%}), stray triggers {extra}")
        else:
            print(f"   {name}: {len(triggers)} false triggers in {minutes:.1f} min of speech and noise ({len(triggers) / minutes * 60:.1f} per hour)")
