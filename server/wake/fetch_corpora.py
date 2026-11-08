"""Downloads real Spanish speech (Colombian volunteers and FLEURS) and everyday noise (ESC-50) as examples of what is
NOT "Hola Bug" or a command. Streams a few hundred clips of each (not whole datasets) and stores them as 16 kHz wav in
server/wake_data/corpus (ignored by git). Run with the training environment."""
import io
import sys
import wave
from math import gcd
from pathlib import Path

import numpy as np
import soundfile as sf
from datasets import Audio, load_dataset
from scipy.signal import resample_poly

OUT = Path(__file__).resolve().parent.parent / "wake_data" / "corpus"


def save(folder, name, data, rate):
    if data.ndim > 1:
        data = data.mean(axis=1)
    if rate != 16000:
        g = gcd(16000, rate)
        data = resample_poly(data, 16000 // g, rate // g)
    pcm = (np.clip(data, -1, 1) * 32767).astype(np.int16)
    folder.mkdir(parents=True, exist_ok=True)
    with wave.open(str(folder / f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(pcm.tobytes())


def grab(dataset, config, split, folder, prefix, count, column="audio"):
    ds = load_dataset(dataset, config, split=split, streaming=True) if config else load_dataset(dataset, split=split, streaming=True)
    ds = ds.cast_column(column, Audio(decode=False))
    done = 0
    for item in ds:
        try:
            data, rate = sf.read(io.BytesIO(item[column]["bytes"]))
            save(OUT / folder, f"{prefix}_{done:04d}", data.astype(np.float32), rate)
            done += 1
        except Exception as error:
            print("skipped one:", str(error)[:80])
        if done >= count:
            break
        if done % 100 == 0:
            print(f"  {prefix}: {done}/{count}", flush=True)
    print(f"{prefix}: {done} clips", flush=True)


if __name__ == "__main__":
    grab("ylacombe/google-colombian-spanish", "female", "train", "speech", "co_f", 450)
    grab("ylacombe/google-colombian-spanish", "male", "train", "speech", "co_m", 450)
    grab("google/fleurs", "es_419", "train", "speech", "fleurs", 500)
    try:
        grab("ashraq/esc50", None, "train", "noise", "esc", 500)
    except Exception as error:
        print("noise dataset failed:", str(error)[:200])
