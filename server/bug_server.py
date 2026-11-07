"""Bug voice server: speech in, skill, speech out. Runs on the PC.

The board sends 16 kHz, 16-bit mono PCM to /listen and gets back an action
header plus, for replies, 16 kHz PCM it can play directly.
"""
import asyncio
import difflib
import functools
import logging
import os
import re
import time
import unicodedata
from datetime import datetime, timedelta, timezone
from urllib.parse import quote

import numpy as np
from flask import Flask, Response, jsonify, request

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger("bug")

SAMPLE_RATE = 16000
VOICE = os.getenv("BUG_VOICE", "es-CO-SalomeNeural")
WHISPER_MODEL = os.getenv("BUG_WHISPER_MODEL", "small")
WHISPER_DEVICE = os.getenv("BUG_WHISPER_DEVICE", "cpu")
PORT = int(os.getenv("BUG_PORT", "5000"))
MIN_SECONDS = 0.4
COLOMBIA = timezone(timedelta(hours=-5))  # Colombia has no daylight saving time

app = Flask(__name__)
model = None


# ---------------------------------------------------------------- text helpers
def normalize(text):
    """Lowercase, drop accents and punctuation: 'Qué hora es?' -> 'que hora es'."""
    text = unicodedata.normalize("NFKD", text.lower())
    text = "".join(c for c in text if not unicodedata.combining(c))
    return " ".join(re.sub(r"[^a-z0-9 ]+", " ", text).split())


GREETINGS = {"hola", "ola", "hey", "ey", "ei", "hei", "oye", "jey", "hay", "hi"}
BUG_SOUNDS = {"bug", "bag", "bog", "bac", "bak", "buk", "bach", "bugs", "vag", "bugg", "bago"}


def is_bug(word):
    return word in BUG_SOUNDS or difflib.SequenceMatcher(None, word, "bug").ratio() >= 0.66


def parse_wake(text):
    """Returns (heard_wake_word, rest_of_sentence) for 'Hola Bug, qué hora es'."""
    words = normalize(text).split()
    for i, word in enumerate(words[:4]):
        if word in GREETINGS and i + 1 < len(words) and is_bug(words[i + 1]):
            return True, " ".join(words[i + 2:])
    return False, ""


# ---------------------------------------------------------------------- skills
DAYS = ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"]
MONTHS = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
          "septiembre", "octubre", "noviembre", "diciembre"]


def skill_time(_text):
    now = datetime.now(COLOMBIA)
    hour12 = now.hour % 12 or 12
    if now.minute == 0 and now.hour == 12:
        return "Son las 12 del mediodía."
    if now.minute == 0 and now.hour == 0:
        return "Son las 12 de la medianoche."
    if now.hour < 6:
        period = "de la madrugada"
    elif now.hour < 12:
        period = "de la mañana"
    elif now.hour < 19:
        period = "de la tarde"
    else:
        period = "de la noche"
    opening = "Es la" if hour12 == 1 else "Son las"
    if now.minute == 0:
        minutes = "en punto"
    elif now.minute == 15:
        minutes = "y cuarto"
    elif now.minute == 30:
        minutes = "y media"
    else:
        minutes = f"y {now.minute}"
    return f"{opening} {hour12} {minutes} {period}."


def skill_date(_text):
    now = datetime.now(COLOMBIA)
    return f"Hoy es {DAYS[now.weekday()]}, {now.day} de {MONTHS[now.month - 1]} de {now.year}."


SKILLS = [
    (re.compile(r"\bhora\b"), skill_time),
    (re.compile(r"\b(fecha|que dia|a cuantos estamos|a cuanto estamos)\b"), skill_date),
    (re.compile(r"\b(como te llamas|cual es tu nombre|quien eres)\b"),
     lambda _t: "Soy Bug, tu asistente."),
    (re.compile(r"\bcomo estas\b"), lambda _t: "Muy bien, gracias por preguntar."),
    (re.compile(r"\bgracias\b"), lambda _t: "Con gusto."),
]
FALLBACK = "Todavía no sé hacer eso. Por ahora puedo decirte la hora y la fecha."


def run_skill(text):
    cleaned = normalize(text)
    for pattern, skill in SKILLS:
        if pattern.search(cleaned):
            return skill(cleaned)
    return FALLBACK


# ------------------------------------------------------------- speech in / out
def load_model():
    from faster_whisper import WhisperModel

    log.info("Loading Whisper '%s' on %s (the first run downloads it)...", WHISPER_MODEL, WHISPER_DEVICE)
    compute = "float16" if WHISPER_DEVICE == "cuda" else "int8"
    return WhisperModel(WHISPER_MODEL, device=WHISPER_DEVICE, compute_type=compute)


def transcribe(pcm):
    audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0
    if len(audio) < SAMPLE_RATE * MIN_SECONDS:
        return ""
    peak = float(np.max(np.abs(audio)))
    if peak < 0.004:  # silence
        return ""
    audio *= min(0.8 / peak, 40.0)  # the board's mic is quiet, so raise it
    segments, _ = model.transcribe(
        audio,
        language="es",
        beam_size=1,
        condition_on_previous_text=False,
        vad_filter=True,
        vad_parameters={"min_silence_duration_ms": 400},
        initial_prompt="Hola Bug. Hey Bug. ¿Qué hora es?",
    )
    return " ".join(s.text.strip() for s in segments).strip()


async def _tts_mp3(text):
    import edge_tts

    audio = bytearray()
    async for chunk in edge_tts.Communicate(text, VOICE).stream():
        if chunk["type"] == "audio":
            audio += chunk["data"]
    return bytes(audio)


@functools.lru_cache(maxsize=64)
def synthesize(text):
    """Returns 16 kHz, 16-bit mono PCM of the text in Salomé's voice."""
    import miniaudio

    mp3 = asyncio.run(_tts_mp3(text))
    decoded = miniaudio.decode(
        mp3, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=SAMPLE_RATE
    )
    return decoded.samples.tobytes()


# ------------------------------------------------------------------- endpoints
def respond(action, heard="", body=b""):
    headers = {"X-Action": action, "X-Heard": quote(heard[:200], safe="")}
    return Response(body, mimetype="application/octet-stream", headers=headers)


@app.get("/health")
def health():
    return jsonify(status="ok", model=WHISPER_MODEL, voice=VOICE)


@app.post("/listen")
def listen():
    """mode=wake: wait for 'Hola/Hey Bug'. mode=command: the next sentence is the command."""
    mode = request.args.get("mode", "wake")
    pcm = request.get_data()
    started = time.time()

    heard = transcribe(pcm)
    stt_seconds = time.time() - started
    if not heard:
        log.info("[%s] %.1fs of audio -> nothing heard (%.1fs)", mode, len(pcm) / 2 / SAMPLE_RATE, stt_seconds)
        return respond("ignore")

    woke, rest = parse_wake(heard)
    log.info("[%s] heard %r (%.1fs) wake=%s", mode, heard, stt_seconds, woke)

    if mode == "wake":
        if not woke:
            return respond("ignore", heard)
        if not rest:
            return respond("wake", heard)
        command = rest
    else:
        command = rest if woke and rest else heard

    answer = run_skill(command)
    audio = synthesize(answer)
    log.info("  command=%r -> %r (total %.1fs)", command, answer, time.time() - started)
    return respond("reply", heard, audio)


if __name__ == "__main__":
    model = load_model()
    log.info("Bug server ready on port %d. Voice: %s", PORT, VOICE)
    app.run(host="0.0.0.0", port=PORT, threaded=True)
