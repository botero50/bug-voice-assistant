"""The audio lab: takes the raw two-microphone recordings the board made (server/wake_data/raw, stereo 32 kHz; see wake/start_session.py --raw)
and compares ways of turning them into the 16 kHz mono audio the rest of the system hears. The score is the one that matters: how many greetings Whisper
(the server's own settings) still recognises, in total and for each condition (whisper, music, TV, sideways, normal).

    python wake/audio_lab.py                 all the raw clips
    python wake/audio_lab.py --save best     also writes the processed clips of one variant to wake_data/lab/<variant>/ to listen to them

Variants: current (what the firmware does today: the four values averaged), mic1, mic2, sum (both mics, proper low-pass before halving the rate),
best (the louder-in-speech microphone), suppress (sum + spectral noise suppression using the quiet start of the recording), align (the mics aligned
by their delay, then suppression).
"""
import argparse
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import two_stage_eval as t  # noqa: E402  (Whisper, the greeting rule)

RAW = HERE.parent / "wake_data" / "raw" / "wake"
OUT = HERE.parent / "wake_data" / "lab"
CONDITIONS = ["casi susurrando", "con música de fondo", "con la tele puesta", "hablando de lado", "voz normal (referencia)"]  # the order of train_api.SESSION_HINTS_HARD


def read_raw(path):
    with wave.open(str(path), "rb") as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)
    return x[0::2], x[1::2]  # left and right microphone, 32 kHz


def decimate(x):
    """32 kHz -> 16 kHz with a real low-pass first (the firmware halves the rate by averaging pairs, which lets high sounds fold back in)."""
    return signal.resample_poly(x, 1, 2)


def current(left, right):
    """What the firmware does today: every 16 kHz sample is the average of two bus frames of two channels."""
    n = min(len(left), len(right)) // 2 * 2
    return ((left[:n:2] + right[:n:2] + left[1:n:2] + right[1:n:2]) / 4)


def active_segment(x, sr=16000):
    """(start, end) of the loudest 1.5 s: the greeting."""
    frame = sr // 50
    energy = np.convolve(x[: len(x) // frame * frame].reshape(-1, frame).std(axis=1), np.ones(25) / 25, mode="same")
    centre = int(np.argmax(energy)) * frame
    return max(0, centre - int(0.75 * sr)), centre + int(0.75 * sr)


def suppress(x, sr=16000, floor=0.08, over=1.6):
    """Spectral subtraction: the noise spectrum is measured on the quiet start of the recording (before the greeting) and taken away, with a floor so it
    does not turn into watery 'musical noise'. A light Wiener-style gain on every frequency of every 32 ms frame."""
    f, _, spec = signal.stft(x, sr, nperseg=512, noverlap=384)
    start, _ = active_segment(x, sr)
    quiet_frames = max(3, min(spec.shape[1] // 3, int(start / (512 - 384))))
    noise = np.mean(np.abs(spec[:, :quiet_frames]) ** 2, axis=1, keepdims=True)
    power = np.abs(spec) ** 2
    gain = np.maximum(1 - over * noise / np.maximum(power, 1e-9), floor)
    _, cleaned = signal.istft(spec * np.sqrt(gain), sr, nperseg=512, noverlap=384)
    return cleaned[: len(x)]


def delay_between(a, b, sr=16000, max_ms=1.0):
    """The shift to give b (np.roll) so it lines up with a, found with GCC-PHAT and limited to what the microphone spacing allows."""
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    cross = np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n))
    corr = np.fft.irfft(cross / np.maximum(np.abs(cross), 1e-9), n)
    limit = int(sr * max_ms / 1000)
    window = np.concatenate([corr[:limit + 1], corr[-limit:]]) if limit else corr[:1]
    index = int(np.argmax(window))
    return index if index <= limit else index - len(window)


def variants(left32, right32):
    l, r = decimate(left32), decimate(right32)
    s, e = active_segment(l + r)
    louder = l if np.std(l[s:e]) >= np.std(r[s:e]) else r
    lag = delay_between(l[s:e], r[s:e], 16000, 0.6)
    aligned = (l + np.roll(r, lag)) / 2
    return {
        "current": current(left32, right32),
        "mic1": l, "mic2": r,
        "sum": (l + r) / 2,
        "best": louder,
        "suppress": suppress((l + r) / 2),
        "align": suppress(aligned),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--save", default="", help="write the processed clips of this variant to wake_data/lab/<variant>/")
    args = parser.parse_args()
    files = sorted(RAW.glob("*.wav"))
    if not files:
        sys.exit("No raw clips yet: python wake/start_session.py --raw, then say 'Hola Bug, entrena mi voz' to the board.")
    listen = t.load_whisper()
    score = {}
    for i, path in enumerate(files):
        left, right = read_raw(path)
        condition = CONDITIONS[i % len(CONDITIONS)]
        for name, audio in variants(left, right).items():
            pcm = np.clip(audio, -32768, 32767).astype(np.int16)
            ok = t.heard_greeting(listen(pcm))
            entry = score.setdefault(name, {})
            entry.setdefault("total", [0, 0])
            entry.setdefault(condition, [0, 0])
            for key in ("total", condition):
                entry[key][0] += ok
                entry[key][1] += 1
            if args.save == name:
                (OUT / name).mkdir(parents=True, exist_ok=True)
                with wave.open(str(OUT / name / path.name), "wb") as w:
                    w.setnchannels(1), w.setsampwidth(2), w.setframerate(16000), w.writeframes(pcm.tobytes())
    print(f"{len(files)} raw clips; greetings Whisper recognises, per processing variant")
    print(f"{'variant':<10} {'total':>8}  " + "  ".join(f"{c.split(' (')[0][:14]:>14}" for c in CONDITIONS))
    for name, entry in score.items():
        total = entry["total"]
        cells = "  ".join(f"{entry.get(c, [0, 0])[0]:>6}/{entry.get(c, [0, 0])[1]:<7}" for c in CONDITIONS)
        print(f"{name:<10} {total[0]:>4}/{total[1]:<3} ({100 * total[0] / total[1]:3.0f}%)  {cells}")


if __name__ == "__main__":
    main()
