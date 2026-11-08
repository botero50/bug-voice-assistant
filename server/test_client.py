"""Tests the Bug server without the board: speaks test phrases and checks the replies.

    python test_client.py            # checks only
    python test_client.py --play     # also plays each reply on this PC
"""
import asyncio
import io
import sys
import wave
from pathlib import Path
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
    # (what is said, mode, expected action, expected screen or None, why)
    ("Hola Bug, ¿qué hora es?", "wake", "reply", None, "wake word and command in one sentence"),
    ("Hey Bug, ¿qué día es hoy?", "wake", "reply", None, "'Hey Bug' with the date skill"),
    ("Hola Bug", "wake", "wake", None, "wake word alone"),
    ("¿Qué hora es?", "command", "reply", None, "command after the wake word"),
    ("Buenos días, hoy vamos a la reunión", "wake", "ignore", None, "talking without the wake word"),
    ("Hola Bug, muéstrame los correos", "wake", "reply", "email", "opens the email screen"),
    ("Lee el primero", "command", "reply", "email_detail?i=0", "opens the first email"),
    ("¿Qué tengo en el calendario?", "command", "reply", "calendar", "opens the calendar (events come from the test ICS file)", "prueba"),
    ("Lee el libro de prueba", "command", "reply", "article", "reads the first page of a book", "oración"),
    ("Sigue leyendo", "command", "reply", "article", "reads the next page", "oración"),
    ("Repite la página", "command", "reply", "article", "reads the same page again", "oración"),
    ("Lee el libro aventura", "command", "reply", "article", "reads an EPUB", "Frase"),
    ("Lee el libro de Zzyzx", "command", "reply", None, "a book that isn't there", "No encontré"),
    ("Inicio", "command", "reply", "home", "goes back Home"),
    ("Hola Bug, ¿cómo está el clima?", "wake", "reply", "weather?q=", "weather in your city"),
    ("¿Qué clima hace en Cali hoy?", "command", "reply", "weather?q=cali", "weather in a named city"),
    ("¿En qué ciudad estoy?", "command", "reply", None, "asks which city"),
    ("Cambia la ciudad a Cali", "command", "reply", None, "changes the city (undone by the next test)"),
    ("¿Cómo está el clima?", "command", "reply", "weather?q=", "weather now uses the saved city"),
    ("Detecta mi ciudad", "command", "reply", None, "back to automatic city"),
    ("Hola Bug, pon salsa", "wake", "reply", "music?id=*", "plays a salsa station"),
    ("Cambia de emisora", "command", "reply", "music?id=*", "next station"),
    ("Pon Tropicana", "command", "reply", "music?id=*", "plays a named station"),
    ("Sube el volumen", "command", "reply", "volume?d=15", "volume up"),
    ("Baja el volumen", "command", "reply", "volume?d=-15", "volume down"),
    ("Pon el volumen al 50 por ciento", "command", "reply", "volume?v=50", "volume to a number", "50"),
    ("Para la música", "command", "reply", "music_stop", "stops the music"),
    # These need BUG_MUSIC_DIR to point at a folder with songs (the test run uses two generated ones)
    ("Pon la canción de Shakira", "command", "reply", "music?lib=1", "plays a named song from the PC", "Waka"),
    ("Pon mi música", "command", "reply", "music?lib=1", "plays the whole library"),
    ("Siguiente canción", "command", "reply", "music?lib=1", "next song", "Siguiente"),
    ("Canción anterior", "command", "reply", "music?lib=1", "previous song", "Anterior"),
    ("Pon la canción de Zzyzx", "command", "reply", None, "a song that isn't there", "No encontré"),
    ("Cambia de emisora", "command", "reply", "music?id=*", "radio next still works"),
    ("Para la música", "command", "reply", "music_stop", "stops again"),
    ("¿Cuánto está el bitcoin?", "command", "reply", "rows?kind=crypto", "crypto prices", "dólares"),
    ("¿A cuánto está el dólar?", "command", "reply", "rows?kind=dollar", "dollar and euro", "pesos"),
    ("Dame las noticias", "command", "reply", "headlines", "news headlines", "Primera"),
    ("¿Quién fue Gabriel García Márquez?", "command", "reply", "article", "Wikipedia answer", "Márquez"),
    ("Cuéntame un chiste", "command", "reply", "article", "a joke"),
    ("¿Cuánto es 25 más 17?", "command", "reply", None, "math", "42"),
    ("Lanza un dado", "command", "reply", None, "dice", "Salió"),
    ("Tira una moneda", "command", "reply", None, "coin", "Salió"),
    ("Convierte 5 millas a kilómetros", "command", "reply", None, "unit conversion", "8,05"),
    ("¿Cómo está mi computador?", "command", "reply", "rows?kind=pc", "this PC's status", "procesador"),
    ("Buenos días", "command", "reply", "rows?kind=briefing", "morning briefing", "Hoy"),
    ("Agrega leche y huevos a la lista de compras", "command", "reply", "rows?kind=compras", "adds to the shopping list", "leche"),
    ("Agrega pan a la lista de compras", "command", "reply", "rows?kind=compras", "adds a third item", "pan"),
    ("¿Qué hay en mi lista de compras?", "command", "reply", "rows?kind=compras", "reads the list", "huevos"),
    ("Quita la leche de la lista", "command", "reply", "rows?kind=compras", "removes an item", "leche"),
    ("Borra la lista de compras", "command", "reply", None, "clears the list", "borré"),
    ("Anota que mañana hay reunión", "command", "reply", "rows?kind=notas", "adds a note", "reunión"),
    ("Mis notas", "command", "reply", "rows?kind=notas", "reads the notes", "reunión"),
    ("Borra las notas", "command", "reply", None, "clears the notes", "borré"),
    ("Pon salsa", "command", "reply", "music?id=*", "a station to save as a favourite"),
    ("Guarda esta emisora en favoritas", "command", "reply", None, "saves the favourite", "guardé"),
    ("Pon mis favoritas", "command", "reply", "music?id=*", "plays the favourites"),
    ("Quita esta emisora de las favoritas", "command", "reply", None, "removes the favourite", "quité"),
    ("Apaga la música en 30 minutos", "command", "reply", "music_sleep?s=1800", "music sleep timer", "30 minutos"),
    ("Para la música", "command", "reply", "music_stop", "stops the radio again"),
    ("Recuérdame todos los días a las 8 de la mañana tomar la pastilla", "command", "reply", "timer?id=*", "a daily reminder", "todos los días"),
    ("Despiértame todos los lunes a las 6 y media", "command", "reply", "timer?id=*", "a weekly alarm", "todos los lunes"),
    ("Cancela las alarmas", "command", "reply", "timer_cancel?ids=*", "cancels repeating alarms too", "cancel"),
    ("Cancela los recordatorios", "command", "reply", "timer_cancel?ids=*", "cancels repeating reminders", "cancel"),
    ("Juguemos al triqui", "command", "reply", "triqui?cmd=new", "opens the tic-tac-toe game", "triqui"),
    ("Juguemos trivia", "command", "reply", "article", "starts trivia with a question", "Juguemos trivia"),
    ("La opción B", "command", "reply", "article", "answers a trivia question", "Llevas"),
    ("Otra pregunta", "command", "reply", "article", "asks the next question"),
    ("Busca una actualización del firmware", "command", "reply", "ota?check", "firmware update by voice", "actualización"),
    ("¿Cuándo es el próximo festivo?", "command", "reply", "rows?kind=holidays", "next Colombian holiday", "festivo"),
    ("¿Cuántos días faltan para Navidad?", "command", "reply", None, "days until Christmas", "Navidad"),
    ("¿A qué hora se oculta el sol?", "command", "reply", None, "sunset time", "sol"),
    ("¿Cómo se dice casa en inglés?", "command", "reply", "article", "translation", "house"),
    ("Inicia el cronómetro", "command", "reply", "stopwatch?cmd=start", "starts the stopwatch", "marcha"),
    ("Para el cronómetro", "command", "reply", "stopwatch?cmd=pause", "pauses the stopwatch", "detenido"),
    ("Reinicia el cronómetro", "command", "reply", "stopwatch?cmd=reset", "resets the stopwatch", "cero"),
    ("Pon un temporizador de 10 minutos", "command", "reply", "timer?id=*", "a timer", "10 minutos"),
    ("Despiértame a las 6 y media de la mañana", "command", "reply", "timer?id=*", "an alarm", "6 y media"),
    ("Recuérdame en 20 minutos llamar a Ana", "command", "reply", "timer?id=*", "a reminder", "llamar a Ana"),
    ("¿Qué temporizadores tengo?", "command", "reply", "rows?kind=timers", "lists them", "Tienes"),
    ("Cancela el temporizador", "command", "reply", "timer_cancel?ids=*", "cancels the timer", "cancel"),
    ("Cancela las alarmas", "command", "reply", "timer_cancel?ids=*", "cancels the alarm", "cancel"),
]


def check_screen_data():
    """Each screen's data must be valid JSON of the right type."""
    failures = 0
    for path, expected in (("email", "email_list"), ("email_detail?i=1", "email_detail"), ("calendar", "calendar"),
                           ("weather?q=medellin", "weather"), ("rows?kind=crypto", "rows"),
                           ("rows?kind=dollar", "rows"), ("rows?kind=pc", "rows"), ("rows?kind=timers", "rows"),
                           ("headlines", "headlines"),
                           ("article", "article"), ("rows?kind=holidays", "rows")):
        for attempt in range(2):  # a web service can hiccup right after the server restarts
            data = requests.get(f"{URL}/screen/{path}", timeout=20).json()
            if "error" not in data:
                break
        real_data = expected in ("weather", "rows", "headlines", "article", "calendar")  # calendar is real when an ICS link is set
        ok = data.get("type") == expected and (data.get("sample") is True or real_data)
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  /screen/{path} -> type {data.get('type')}")
    return failures


def check_music_stream(music_ids):
    """The radio stream must deliver real audio: about 3 seconds of 16 kHz PCM that isn't silence."""
    import numpy as np

    if not music_ids:
        print("FAIL  no station was chosen, so there is no stream to test")
        return 1
    screen = requests.get(f"{URL}/screen/music?id={music_ids[0]}", timeout=15).json()
    wanted = SAMPLE_RATE * 2 * 3
    data = b""
    with requests.get(f"{URL}/music/stream?id={music_ids[0]}", stream=True, timeout=30) as response:
        for chunk in response.iter_content(4096):
            data += chunk
            if len(data) >= wanted:
                break
    samples = np.frombuffer(data[: len(data) // 2 * 2], dtype=np.int16).astype(np.float32)
    rms = float(np.sqrt(np.mean(samples ** 2))) if len(samples) else 0.0
    ok = len(data) >= wanted and rms > 100 and screen.get("type") == "music"
    print(f"{'PASS' if ok else 'FAIL'}  radio stream of {screen.get('station')!r}: {len(data) / 2 / SAMPLE_RATE:.1f} s of audio, rms {rms:.0f}")
    return 0 if ok else 1


def check_library_stream():
    """A library connection plays one whole song, names it in the headers, and the next connection
    gets the following song."""
    import numpy as np

    first = requests.get(f"{URL}/music/stream?lib=1", timeout=30)
    second = requests.get(f"{URL}/music/stream?lib=1", timeout=30)
    from urllib.parse import unquote
    title1, title2 = unquote(first.headers.get("X-Title", "")), unquote(second.headers.get("X-Title", ""))
    samples = np.frombuffer(first.content[: len(first.content) // 2 * 2], dtype=np.int16).astype(np.float32)
    rms = float(np.sqrt(np.mean(samples ** 2))) if len(samples) else 0.0
    ok = bool(title1) and title1 != title2 and rms > 100
    print(f"{'PASS' if ok else 'FAIL'}  library: {title1!r} ({len(first.content) / 2 / SAMPLE_RATE:.1f} s, rms {rms:.0f}), then {title2!r}")
    return 0 if ok else 1


def main():
    try:
        requests.get(f"{URL}/health", timeout=3).raise_for_status()
    except Exception as error:
        sys.exit(f"Server is not running on {URL}: {error}")

    # The tests change the saved city and add timers. Put both back exactly as they were, even if a test fails.
    personal_files = [Path(__file__).with_name(name) for name in ("user_settings.json", "timers.json", "lists.json", "favorites.json", "reading_state.json")]
    previous = {f: f.read_text(encoding="utf-8") if f.exists() else None for f in personal_files}
    try:
        failures = run_cases()
    finally:
        for file, content in previous.items():
            if content is None:
                file.unlink(missing_ok=True)
            else:
                file.write_text(content, encoding="utf-8")
    sys.exit(1 if failures else 0)


def run_cases():
    failures = 0
    music_ids = []
    for phrase, mode, expected, screen, why, *must in CASES:
        response = requests.post(f"{URL}/listen?mode={mode}", data=speak(phrase), timeout=60)
        action = response.headers.get("X-Action")
        got_screen = unquote(response.headers.get("X-Screen", "")) or None
        heard = unquote(response.headers.get("X-Heard", ""))
        reply = unquote(response.headers.get("X-Reply", ""))
        if screen and screen.endswith("*"):
            screen_ok = bool(got_screen) and got_screen.startswith(screen[:-1])
        else:
            screen_ok = got_screen == screen
        if got_screen and got_screen.startswith("music?id="):
            music_ids.append(got_screen.split("=", 1)[1])
        reply_ok = not must or must[0].lower() in reply.lower()
        ok = action == expected and screen_ok and reply_ok
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {why}\n      said {phrase!r} -> heard {heard!r}, action={action}, "
              f"screen={got_screen} (wanted {expected}, {screen})\n      reply: {reply!r}")
        if "--play" in sys.argv and action == "reply":
            play(response.content)
    failures += check_screen_data()
    failures += check_music_stream(music_ids)
    failures += check_library_stream()
    return failures


if __name__ == "__main__":
    main()
