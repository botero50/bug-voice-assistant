"""Compares the board's C detector (built for this PC) with the PyTorch model on the same audio."""
import subprocess, sys, wave
from pathlib import Path
import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train

HERE = Path(__file__).resolve().parent
clip = sorted((HERE.parent / "wake_data" / "synth" / "pos").glob("*.wav"))[3]
with wave.open(str(clip), "rb") as w:
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
rng = np.random.default_rng(3)
audio = np.concatenate([(rng.standard_normal(16000) * 20).astype(np.int16), x, (rng.standard_normal(24000) * 20).astype(np.int16)])
audio = (audio // 1).astype(np.int16)
raw = HERE / "check.raw"
raw.write_bytes(audio.tobytes())

exe = HERE / "host_test.exe"
subprocess.run(["gcc", "-O2", "-I", str(HERE / "stubs"), "-I", str(HERE.parent.parent / "firmware" / "main"),
                str(HERE / "host_test.c"), str(HERE.parent.parent / "firmware" / "main" / "wake_local.c"), "-lm", "-o", str(exe)], check=True)
out = subprocess.run([str(exe), str(raw)], capture_output=True, text=True).stdout
c_scores = {int(l.split()[1]): float(l.split()[2]) for l in out.splitlines() if l.startswith("SCORE")}

model = train.Net().to(train.device)
model.load_state_dict(torch.load(HERE / "wake_model.pt", map_location=train.device))
model.eval()
signal = np.concatenate([np.zeros(80), audio.astype(np.float32) / 32768.0]).astype(np.float32)
def mel_frame(k):
    """The board's alignment: the 400-sample window ends at the newest sample (torch.stft would place it 56 later)."""
    w = signal[320 * k: 320 * k + 400] * (0.5 - 0.5 * np.cos(2 * np.pi * np.arange(400) / 399))
    buf = np.zeros(512, dtype=np.float64)
    buf[56:456] = w
    power = np.abs(np.fft.rfft(buf)) ** 2
    return np.log(train.MEL.astype(np.float64) @ power + 1e-6)


frames_needed = max(c_scores) + 1
mel = torch.tensor(np.stack([mel_frame(k) for k in range(frames_needed)], axis=1), dtype=torch.float32, device=train.device)
worst = 0.0
for f, c in sorted(c_scores.items())[:60]:
    window = mel[:, f - 5 - train.FRAMES + 1: f - 5 + 1]  # the look is spread over 4 frames and uses the window of its first one
    window = (window - window.mean()) / train.STD
    with torch.no_grad():
        p = torch.sigmoid(model(window.unsqueeze(0).unsqueeze(0))).item()
    worst = max(worst, abs(p - c))
print(f"{len(c_scores)} looks, largest difference between C and PyTorch: {worst:.5f}")
print("highest C score:", max(c_scores.values()))
