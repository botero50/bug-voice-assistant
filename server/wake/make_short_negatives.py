"""Adds short everyday words and look-alikes as negatives ("gracias", "sí", "ah", "dale", "Hola Claude"...), the things the detector still fired on
after the first retraining (see docs/PHASE1_WEEK1_RESULTS.md). Same voices and method as make_synthetic.py, only the new phrases.

    python wake\make_short_negatives.py
Output: server/wake_data/synth/neg/<voice>_i<hash>.wav (ignored by git).
"""
import asyncio
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_synthetic as m  # noqa: E402

SHORT = [
    "Gracias", "Sí", "No", "Ah", "Ok", "Claro", "Listo", "Vale", "Dale", "Eso", "Mmm", "Ajá", "Ya", "Pues", "Uy", "Ay", "Qué", "¿Cómo?",
    "¿Qué?", "¿Sí?", "Aló", "Bueno, bueno", "Hola Claude", "Hola, Claude", "Hey Claude", "Oye Claude", "Mátalo", "Hola Nico", "Nico", "Tengo gusto",
    "Mucho gusto", "Con gusto", "Un momento", "Ahora sí", "Ya voy", "Qué pena", "Perfecto", "Exacto", "Por favor", "Disculpa", "Hola Bu", "Hola Bruno",
    "Oye Bugs", "Hey, hola", "Hola, hola", "Ahí voy", "Está bien", "Hola Blue", "Hola Book", "Hola Bing", "Hey Bing", "Hola Bix",
]


async def main():
    spanish, english = await m.pick_voices()
    semaphore = asyncio.Semaphore(6)
    random.seed(21)
    jobs = []
    for voice in spanish:
        for text in SHORT:
            rate, pitch = f"{random.choice([-12, -6, 0, 6, 12]):+d}%", f"{random.choice([-8, -4, 0, 4, 8]):+d}Hz"
            jobs.append((voice, text, rate, pitch))
    for voice in random.sample(english, min(6, len(english))):
        for text in random.sample(SHORT, 15):
            jobs.append((voice, text, "+0%", "+0Hz"))
    print(f"{len(jobs)} clips to make", flush=True)
    made = 0

    async def one(job):
        nonlocal made
        voice, text, rate, pitch = job
        pcm = await m.synth(text, voice, rate, pitch, semaphore)
        if pcm:
            m.save(m.ROOT / "neg", f"{voice}_i{abs(hash(text)) % 9999}_{rate}_{pitch}".replace("%", "p").replace("+", "").replace("-", "m"), pcm)
            made += 1
            if made % 100 == 0:
                print(f"  {made}", flush=True)

    await asyncio.gather(*(one(j) for j in jobs))
    print(f"done: {made} clips", flush=True)


if __name__ == "__main__":
    asyncio.run(main())
