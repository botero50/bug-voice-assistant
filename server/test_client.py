"""Tests the Bug server without the board: speaks test phrases and checks the replies.

    python test_client.py            # checks only
    python test_client.py --play     # also plays each reply on this PC
"""
import asyncio
import io
import sys
import wave
from urllib.parse import unquote

import edge_tts
import miniaudio
import requests

URL = "http://127.0.0.1:5000"
SAMPLE_RATE = 16000


def speak(text):
    """Salomé saying `text` as 16 kHz PCM, padded with silence like the board's recordings."""

    async def fetch():
        audio = bytearray()
        async for chunk in edge_tts.Communicate(text, "es-CO-SalomeNeural").stream():
            if chunk["type"] == "audio":
                audio += chunk["data"]
        return bytes(audio)

    decoded = miniaudio.decode(
        asyncio.run(fetch()), output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=SAMPLE_RATE
    )
    silence = bytes(int(SAMPLE_RATE * 0.4) * 2)
    return silence + decoded.samples.tobytes() + silence


def play(pcm):
    import winsound

    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)
    winsound.PlaySound(buf.getvalue(), winsound.SND_MEMORY)


CASES = [
    # (what is said, mode, expected action, why)
    ("Hola Bug, ¿qué hora es?", "wake", "reply", "wake word and command in one sentence"),
    ("Hey Bug, ¿qué día es hoy?", "wake", "reply", "'Hey Bug' with the date skill"),
    ("Hola Bug", "wake", "wake", "wake word alone"),
    ("¿Qué hora es?", "command", "reply", "command after the wake word"),
    ("Buenos días, hoy vamos a la reunión", "wake", "ignore", "talking without the wake word"),
]


def main():
    try:
        requests.get(f"{URL}/health", timeout=3).raise_for_status()
    except Exception as error:
        sys.exit(f"Server is not running on {URL}: {error}")

    failures = 0
    for phrase, mode, expected, why in CASES:
        response = requests.post(f"{URL}/listen?mode={mode}", data=speak(phrase), timeout=60)
        action = response.headers.get("X-Action")
        heard = unquote(response.headers.get("X-Heard", ""))
        ok = action == expected
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {why}\n      said {phrase!r} -> heard {heard!r}, action={action} "
              f"(wanted {expected}), reply audio {len(response.content) / 2 / SAMPLE_RATE:.1f}s")
        if "--play" in sys.argv and action == "reply":
            play(response.content)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
