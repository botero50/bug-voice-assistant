"""Makes the synthetic part of the wake-word training set: "Hola Bug" and look-alike phrases spoken by many
neural voices (Microsoft's online voices through edge-tts, plus Piper), at different speeds and pitches.

Run with the server's Python (it has edge-tts, miniaudio and piper):
    python wake\\make_synthetic.py
Output: server/wake_data/synth/pos/*.wav and neg/*.wav (16 kHz, 16-bit mono). Ignored by git.
"""
import asyncio
import random
import sys
from pathlib import Path

import edge_tts
import miniaudio

ROOT = Path(__file__).resolve().parent.parent / "wake_data" / "synth"
RATE = 16000
random.seed(7)

GREETINGS = ["Hola", "Hey", "Oye", "Ey", "Hola,", "Hey,"]
POSITIVE = [f"{g} Bug" for g in GREETINGS] + ["Hola Bug.", "Hey Bug.", "¡Hola Bug!", "Hola bug", "Hey bug"]

# Look-alikes and everyday speech that must NOT wake the board
HARD_NEGATIVE = [
    "Hola", "Bug", "Hola Pablo", "Hola Bruno", "Hola Beto", "Hola mamá", "Hola Google", "Hey Siri", "Hey Alexa",
    "Hola buenos días", "Hola, ¿cómo estás?", "Oye Pablo", "Oye mira", "Hey amigo", "Hola a todos", "Hola Luis",
    "Un bug", "Hay un bug en el código", "Debug", "Hola Bugs Bunny", "Pug", "Dug", "Bus", "Bueno", "Hola hola",
    "Buenas tardes", "Buenas noches", "Mi bug favorito", "Hola Bugatti", "Hola Bud", "Hey Bud", "Hola Burt",
    "Oye, ¿qué hora es?", "Hey, ven acá", "Hola, ¿qué tal?", "Hola Beatriz", "Hey bro", "Hola Bob", "Hola Búho",
]
SENTENCES = [
    "¿Qué hora es?", "Pon salsa", "Sube el volumen", "Baja el volumen", "Para la música", "¿Cómo está el clima?",
    "Muéstrame los correos", "¿Qué tengo en el calendario?", "Pon un temporizador de diez minutos",
    "Despiértame a las seis y media", "Apaga la luz por favor", "Voy a la cocina a preparar el almuerzo",
    "Mañana tenemos reunión con el equipo a las nueve", "No encuentro las llaves del carro", "Qué buena película la de anoche",
    "Mi hermano llega el viernes por la tarde", "Tenemos que comprar leche, huevos y pan", "El perro no ha comido todavía",
    "Ya casi termino de trabajar", "¿Quieres un café o prefieres un jugo?", "Hoy hace mucho calor en la ciudad",
    "Me duele un poco la cabeza", "Vamos a ver el partido de fútbol", "La cena está lista cuando quieran",
    "Necesito cargar el celular", "Dame un momento y te ayudo", "Eso no es lo que yo dije", "Qué pena contigo",
    "Mañana madrugo para ir al médico", "Se me olvidó apagar el horno", "Cuéntame un chiste", "Pon la canción de Shakira",
    "¿Cuánto es veinticinco más diecisiete?", "Abre la ventana que hace calor", "Los niños están jugando afuera",
    "Voy a bajar a recoger el domicilio", "Esa serie está buenísima", "Llámame cuando llegues a la casa",
    "Mi mamá viene a visitarnos el domingo", "Tengo hambre, ¿qué hay de comer?", "Acuérdate de sacar la basura",
    "The meeting starts at nine tomorrow", "Can you turn the music down a little", "I think the bug is in the parser",
    "What time is it right now", "Let's order some food tonight", "That was a really good game last night",
    "Hello everyone and welcome back", "Hey, did you see the news this morning", "I will call you later today",
    "Gracias por todo, nos vemos mañana", "Buenos días, ¿cómo amaneciste?", "Hoy me toca ir al supermercado",
    "No me acuerdo dónde dejé el cargador", "Qué fastidio con este tráfico", "Ya mismo te mando el mensaje",
]


async def pick_voices():
    voices = await edge_tts.list_voices()
    spanish = [v["ShortName"] for v in voices if v["Locale"].startswith("es-")]
    english = [v["ShortName"] for v in voices if v["Locale"] in ("en-US", "en-GB", "en-AU", "en-IN", "en-CA", "en-IE")]
    return spanish, english


async def synth(text, voice, rate, pitch, semaphore):
    async with semaphore:
        for attempt in range(3):
            try:
                audio = bytearray()

                async def run():
                    async for chunk in edge_tts.Communicate(text, voice, rate=rate, pitch=pitch).stream():
                        if chunk["type"] == "audio":
                            audio.extend(chunk["data"])

                await asyncio.wait_for(run(), 12)
                if audio:
                    decoded = miniaudio.decode(bytes(audio), output_format=miniaudio.SampleFormat.SIGNED16,
                                               nchannels=1, sample_rate=RATE)
                    return decoded.samples.tobytes()
            except Exception:
                await asyncio.sleep(0.5)
    return None


def save(folder, name, pcm):
    import wave

    folder.mkdir(parents=True, exist_ok=True)
    with wave.open(str(folder / f"{name}.wav"), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm)


async def main():
    spanish, english = await pick_voices()
    print(f"{len(spanish)} Spanish and {len(english)} English voices", flush=True)
    semaphore = asyncio.Semaphore(6)
    jobs = []  # (label, name, text, voice, rate, pitch)

    def prosody():
        return f"{random.choice([-12, -6, 0, 6, 12]):+d}%", f"{random.choice([-8, -4, 0, 4, 8]):+d}Hz"

    for voice in spanish:
        for text in POSITIVE:
            rate, pitch = prosody()
            jobs.append(("pos", f"{voice}_{abs(hash(text)) % 9999}_{rate}_{pitch}", text, voice, rate, pitch))
        for text in HARD_NEGATIVE:
            rate, pitch = prosody()
            jobs.append(("neg", f"{voice}_h{abs(hash(text)) % 9999}", text, voice, rate, pitch))
        for text in random.sample(SENTENCES, 14):
            rate, pitch = prosody()
            jobs.append(("neg", f"{voice}_s{abs(hash(text)) % 9999}", text, voice, rate, pitch))
    for voice in english:  # "Hey Bug" with an English accent, and English speech as negatives
        for text in ["Hey Bug", "Hey, Bug.", "Hey bug", "Hola Bug", "Oye Bug"]:
            rate, pitch = prosody()
            jobs.append(("pos", f"{voice}_{abs(hash(text)) % 9999}_{rate}_{pitch}", text, voice, rate, pitch))
        for text in random.sample(HARD_NEGATIVE, 10) + random.sample(SENTENCES, 8):
            rate, pitch = prosody()
            jobs.append(("neg", f"{voice}_e{abs(hash(text)) % 9999}", text, voice, rate, pitch))
    print(f"{len(jobs)} clips to make", flush=True)

    done = 0

    async def one(job):
        nonlocal done
        label, name, text, voice, rate, pitch = job
        pcm = await synth(text, voice, rate, pitch, semaphore)
        if pcm:
            save(ROOT / label, name.replace("%", "p").replace("+", "").replace("-", "m"), pcm)
        done += 1
        if done % 100 == 0:
            print(f"  {done}/{len(jobs)}", flush=True)

    await asyncio.gather(*(one(j) for j in jobs))

    # A few Piper clips (a local voice with a different character)
    try:
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
        import wave
        from piper import PiperVoice

        voice = PiperVoice.load(str(Path(__file__).resolve().parent.parent / "voices" / "es_MX-claude-high.onnx"))
        for label, texts in (("pos", POSITIVE), ("neg", HARD_NEGATIVE + SENTENCES[:20])):
            for i, text in enumerate(texts):
                import io

                buffer = io.BytesIO()
                with wave.open(buffer, "wb") as out:
                    voice.synthesize_wav(text, out)
                buffer.seek(0)
                with wave.open(buffer, "rb") as src:
                    frames, rate = src.readframes(src.getnframes()), src.getframerate()
                import numpy as np

                samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32)
                resampled = np.interp(np.arange(0, len(samples), rate / RATE), np.arange(len(samples)), samples)
                save(ROOT / label, f"piper_{i}", resampled.astype(np.int16).tobytes())
    except Exception as error:
        print("Piper clips skipped:", error)

    for label in ("pos", "neg"):
        print(label, len(list((ROOT / label).glob("*.wav"))), "clips")


asyncio.run(main())
