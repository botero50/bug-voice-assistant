"""Checks the board's C verifier against the PyTorch model on the same windows: the two must give the same probability."""
import subprocess
import sys
from pathlib import Path

import numpy as np
import torch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mine_windows as mw  # noqa: E402
import train as T  # noqa: E402
import train_verifier as V  # noqa: E402

ROOT = HERE.parent.parent
exe = HERE / "host_verify.exe"
subprocess.run(["gcc", "-O2", "-DHOST_TEST", "-I", str(HERE / "stubs"), "-I", str(ROOT / "firmware" / "main"), str(HERE / "host_verify.c"),
                str(ROOT / "firmware" / "main" / "wake_verify.c"), "-lm", "-o", str(exe)], check=True)
mined = np.load(T.DATA / "mined.npz")
windows = np.concatenate([mined["pos"][:20], mined["neg"][:20]])
model = V.Net().to(T.device)
model.load_state_dict(torch.load(HERE / "verify_model.pt", map_location=T.device))
model.eval()
with torch.no_grad():
    py = torch.sigmoid(model(T.features(torch.tensor(windows.astype(np.float32) / 32768.0).to(T.device)))).cpu().numpy()
worst = 0.0
for window, expected in zip(windows, py):
    raw = HERE / "verify_window.raw"
    raw.write_bytes(window.astype(np.int16).tobytes())
    out = subprocess.run([str(exe), str(raw)], capture_output=True, text=True).stdout
    got = float(next(l for l in out.splitlines() if l.startswith("SCORE")).split()[1])
    worst = max(worst, abs(got - float(expected)))
raw.unlink(missing_ok=True)
print(f"{len(windows)} windows: the largest difference between the C code and PyTorch is {worst:.5f}")
sys.exit(0 if worst < 0.02 else 1)
