"""Spoken versions of every command phrase by many neural voices (extra training data next to the owner's own
recordings). Run with the server's Python. Output: server/wake_data/synth_commands/<id>/*.wav (16 kHz)."""
import asyncio
import json
import random
import sys
from pathlib import Path

import edge_tts
import miniaudio

sys.path.insert(0, str(Path(__file__).resolve().parent))
ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "wake_data" / "synth_commands"
RATE = 16000
random.seed(11)


async def voices():
    found = await edge_tts.list_voices()
    spanish = [v["ShortName"] for v in found if v["Locale"].startswith("es-")]
    random.shuffle(spanish)
    return spanish[:26]


async def speak(text, voice, rate, pitch, semaphore):
    async with semaphore:
        for _ in range(3):
            try:
                audio = bytearray()

                async def run():
                    async for chunk in edge_tts.Communicate(text, voice, rate=rate, pitch=pitch).stream():
                        if chunk["type"] == "audio":
                            audio.extend(chunk["data"])

                await asyncio.wait_for(run(), 12)
                if audio:
                    return miniaudio.decode(bytes(audio), output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1,
                                            sample_rate=RATE).samples.tobytes()
            except Exception:
                await asyncio.sleep(0.5)
    return None


def save(label, name, pcm):
    import wave

    folder = OUT / label
    folder.mkdir(parents=True, exist_ok=True)
    with wave.open(str(folder / f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm)


async def main():
    commands = json.loads((Path(__file__).resolve().parent / "commands.json").read_text(encoding="utf-8"))
    chosen = await voices()
    semaphore = asyncio.Semaphore(6)
    jobs = []
    for command in commands:
        if command["id"] == "ruido":
            continue
        for index, voice in enumerate(chosen):
            text = command["text"][index % len(command["text"])]
            rate = f"{random.choice([-10, -5, 0, 5, 10]):+d}%"
            pitch = f"{random.choice([-6, -3, 0, 3, 6]):+d}Hz"
            jobs.append((command["id"], f"{voice}_{index}", text.strip("¿?¡!"), voice, rate, pitch))
    print(len(jobs), "clips", flush=True)
    done = 0

    async def one(job):
        nonlocal done
        label, name, text, voice, rate, pitch = job
        pcm = await speak(text, voice, rate, pitch, semaphore)
        if pcm:
            save(label, name.replace("-", "m"), pcm)
        done += 1
        if done % 100 == 0:
            print(f"  {done}/{len(jobs)}", flush=True)

    await asyncio.gather(*(one(j) for j in jobs))
    print("done", flush=True)


asyncio.run(main())
