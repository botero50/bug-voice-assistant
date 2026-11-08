"""Bug voice server: speech in, skill, speech out. Runs on the PC.

The board sends 16 kHz, 16-bit mono PCM to /listen and gets back an action
header plus, for replies, 16 kHz PCM it can play directly.
"""
import asyncio
import difflib
import functools
import json
import logging
import os
import re
import time
import unicodedata
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import quote

import numpy as np
import requests
from flask import Flask, Response, jsonify, request

import daily
import extras
import books as books_module
import calendar_ics
import library
import mail_imap
import personal
import radio
import sample_data
import timers
import trivia
from location import DEFAULT_CITY, current_place, forget_place, geocode, get_json, save_place
from text_utils import normalize

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger("bug")

SAMPLE_RATE = 16000
VOICE = os.getenv("BUG_VOICE", "es-CO-SalomeNeural")
WHISPER_MODEL = os.getenv("BUG_WHISPER_MODEL", "")  # empty: "turbo" on the GPU, "small" on the CPU
WHISPER_DEVICE = os.getenv("BUG_WHISPER_DEVICE", "auto")  # auto: the GPU if it works, otherwise the CPU
MODEL_INFO = {"name": "", "device": "", "beam": 1}
PORT = int(os.getenv("BUG_PORT", "5000"))
MIN_SECONDS = 0.4
COLOMBIA = timezone(timedelta(hours=-5))  # Colombia has no daylight saving time

app = Flask(__name__)
model = None


# ---------------------------------------------------------------- text helpers
# normalize() lives in text_utils.py: 'Qué hora es?' -> 'que hora es'

GREETINGS = {"hola", "ola", "hey", "ey", "ei", "hei", "oye", "jey", "hay", "hi"}
BUG_SOUNDS = {"bug", "bag", "bog", "bac", "bak", "buk", "bach", "bugs", "vag", "bugg", "bago"}


def is_bug(word):
    return word in BUG_SOUNDS or difflib.SequenceMatcher(None, word, "bug").ratio() >= 0.66


def parse_wake(text):
    """Returns (heard_wake_word, rest_of_sentence) for 'Hola Bug, qué hora es'.

    The rest keeps the original accents and capitals, because the board shows it on screen."""
    original = text.split()
    words = [normalize(word) for word in original]
    for i, word in enumerate(words[:8]):  # Whisper sometimes writes a few filler words first
        if word in GREETINGS and i + 1 < len(words) and is_bug(words[i + 1]):
            return True, " ".join(original[i + 2:]).strip(" ,.;:!¡¿?")
    return False, ""


# ---------------------------------------------------------------------- skills
DAYS = ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"]
MONTHS = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
          "septiembre", "octubre", "noviembre", "diciembre"]


def clock_words(hour, minute):
    """'las 3 y 40 de la tarde', as it is said aloud."""
    hour12 = hour % 12 or 12
    if minute == 0 and hour == 12:
        return "las 12 del mediodía"
    if minute == 0 and hour == 0:
        return "las 12 de la medianoche"
    if hour < 6:
        period = "de la madrugada"
    elif hour < 12:
        period = "de la mañana"
    elif hour < 19:
        period = "de la tarde"
    else:
        period = "de la noche"
    article = "la" if hour12 == 1 else "las"
    if minute == 0:
        minutes = "en punto"
    elif minute == 15:
        minutes = "y cuarto"
    elif minute == 30:
        minutes = "y media"
    else:
        minutes = f"y {minute}"
    return f"{article} {hour12} {minutes} {period}"


def clock_display(hour, minute):
    """'4:00 pm', as it is shown on screen."""
    return f"{hour % 12 or 12}:{minute:02d} {'am' if hour < 12 else 'pm'}"


def skill_time(_text):
    now = datetime.now(COLOMBIA)
    words = clock_words(now.hour, now.minute)
    return f"{'Es' if words.startswith('la ') else 'Son'} {words}."


def skill_date(_text):
    now = datetime.now(COLOMBIA)
    return f"Hoy es {DAYS[now.weekday()]}, {now.day} de {MONTHS[now.month - 1]} de {now.year}."


# ------------------------------------------------------- skills with their own screen
def mailbox():
    """The newest emails: from the real mailboxes when mail_accounts.json exists, else the samples."""
    if mail_imap.configured():
        try:
            mails = mail_imap.newest()
            if mails:
                return mails
        except Exception as error:
            log.warning("Email failed: %s", error)
    return sample_data.EMAILS


def email_screen():
    mails = mailbox()
    unread = sum(1 for email in mails if email["unread"])
    return {"type": "email_list", "sample": mails is sample_data.EMAILS, "unread": unread, "items": mails}


def email_detail_screen(index):
    mails = mailbox()
    email = mails[min(index, len(mails) - 1)]
    return {"type": "email_detail", "sample": mails is sample_data.EMAILS, **email}


def day_events(offset):
    """Events of today (0) or tomorrow (1): from the ICS links when there are any, else the samples."""
    if calendar_ics.configured():
        return calendar_ics.events_on((datetime.now(COLOMBIA) + timedelta(days=offset)).date())
    return [{**e, "all_day": False} for e in sample_data.EVENTS if e["day_offset"] == offset]


def event_time_words(e):
    return "todo el día" if e["all_day"] else clock_words(e["hour"], e["minute"]).replace(" en punto", "")


def calendar_screen():
    today = datetime.now(COLOMBIA)
    days = []
    for offset, name in ((0, "Hoy"), (1, "Mañana")):
        day = today + timedelta(days=offset)
        events = [
            {"time": "Todo el día" if e["all_day"] else clock_display(e["hour"], e["minute"]),
             "title": e["title"], "place": e["place"]}
            for e in day_events(offset)
        ]
        label = f"{name} · {DAYS[day.weekday()]} {day.day} de {MONTHS[day.month - 1]}"
        days.append({"label": label, "events": events})
    return {"type": "calendar", "sample": not calendar_ics.configured(), "days": days}


def skill_email_list(_text):
    unread = [e for e in mailbox() if e["unread"]]
    if not unread:
        return "No tienes correos sin leer.", "email"
    newest = unread[0]
    count = "un correo sin leer" if len(unread) == 1 else f"{len(unread)} correos sin leer"
    return (f"Tienes {count}. El más reciente es de {newest['from']}, con el asunto: {newest['subject']}. "
            "Dime lee el primero para escucharlo."), "email"


ORDINALS = {"primer": 0, "primero": 0, "primera": 0, "uno": 0, "1": 0,
            "segundo": 1, "segunda": 1, "dos": 1, "2": 1,
            "tercer": 2, "tercero": 2, "tercera": 2, "tres": 2, "3": 2,
            "cuarto": 3, "cuarta": 3, "cuatro": 3, "4": 3}


def skill_email_read(text):
    index = 0
    for word in text.split():
        if word in ORDINALS:
            index = ORDINALS[word]
            break
    mails = mailbox()
    if index >= len(mails):
        return f"Solo tienes {len(mails)} correos.", "email"
    email = mails[index]
    return (f"Correo de {email['from']}. Asunto: {email['subject']}. {email['body']}",
            f"email_detail?i={index}")


def skill_calendar(_text):
    screen = calendar_screen()
    today, tomorrow = screen["days"]
    if not today["events"]:
        spoken = "Hoy no tienes eventos."
    else:
        parts = []
        for e in day_events(0):
            when = event_time_words(e)
            parts.append(f"{when}, {e['title']}" if e["all_day"] else f"a {when}, {e['title']}")
        count = len(parts)
        spoken = f"Hoy tienes {'un evento' if count == 1 else f'{count} eventos'}: " + ", y ".join(parts) + "."
    if tomorrow["events"]:
        count = len(tomorrow["events"])
        spoken += f" Mañana tienes {'uno' if count == 1 else count} más."
    return spoken, "calendar"


def skill_home(_text):
    return "Listo.", "home"


# ------------------------------------------------------------------ weather (real data)
# Open-Meteo is free and needs no account or key. The city comes from location.py.
WEATHER_CACHE = {}  # city query -> (time fetched, data)
WEATHER_CACHE_SECONDS = 600
WEEKDAYS_SHORT = ["Lun", "Mar", "Mié", "Jue", "Vie", "Sáb", "Dom"]


def weather_condition(code):
    """Open-Meteo weather code -> (Spanish description, icon name)."""
    if code in (0, 1):
        return ("Despejado" if code == 0 else "Mayormente despejado"), "clear"
    if code == 2:
        return "Parcialmente nublado", "partly"
    if code == 3:
        return "Nublado", "cloudy"
    if code in (45, 48):
        return "Niebla", "fog"
    if code in (51, 53, 55, 56, 57):
        return "Llovizna", "rain"
    if code in (61, 63, 65, 66, 67):
        return "Lluvia", "rain"
    if code in (80, 81, 82):
        return "Aguaceros", "rain"
    if code in (95, 96, 99):
        return "Tormenta eléctrica", "storm"
    if code in (71, 73, 75, 77, 85, 86):
        return "Nieve", "cloudy"
    return "Nublado", "cloudy"


def fetch_weather(query):
    """Weather for a named city, or for the user's city when none is named. Raises if it can't be found."""
    if query:
        place = geocode(query)
        if not place:
            raise LookupError(f"city not found: {query}")
    else:
        place, _ = current_place()
    key = f"{normalize(place['name'])}/{place['country']}"
    cached = WEATHER_CACHE.get(key)
    if cached and time.time() - cached[0] < WEATHER_CACHE_SECONDS:
        return cached[1]

    forecast = get_json(
        "https://api.open-meteo.com/v1/forecast",
        {
            "latitude": place["lat"],
            "longitude": place["lon"],
            "current": "temperature_2m,weather_code",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
            "timezone": "auto",
            "forecast_days": 4,
        },
    )

    current, daily = forecast["current"], forecast["daily"]
    text, icon = weather_condition(current["weather_code"])
    days = []
    for i in range(1, 4):
        _, day_icon = weather_condition(daily["weather_code"][i])
        weekday = datetime.fromisoformat(daily["time"][i]).weekday()
        days.append({
            "day": WEEKDAYS_SHORT[weekday],
            "icon": day_icon,
            "high": round(daily["temperature_2m_max"][i]),
            "low": round(daily["temperature_2m_min"][i]),
        })
    data = {
        "type": "weather",
        "city": place["name"],
        "temp": round(current["temperature_2m"]),
        "text": text,
        "icon": icon,
        "high": round(daily["temperature_2m_max"][0]),
        "low": round(daily["temperature_2m_min"][0]),
        "rain": daily["precipitation_probability_max"][0] or 0,
        "days": days,
    }
    WEATHER_CACHE[key] = (time.time(), data)
    return data


CITY_RE = re.compile(r"\b(?:clima\w*|tiempo|temperatura\w*|pronostico|llover|llueve)\b\s*(.*)$")
CITY_STOP_WORDS = {"hoy", "manana", "ahora", "esta", "semana", "por", "favor", "tarde", "noche"}
CITY_SKIP_WORDS = {"la", "el", "los", "las", "ciudad"}
# Words around the city that are not part of its name ("clima hace en Cali hoy")
CITY_FILLER = {"hace", "hacen", "estara", "va", "a", "hacer", "como", "que", "tal", "es", "un", "una"}
PREPOSITIONS = ("en", "de", "para")


def extract_city(cleaned):
    """'como esta el clima en medellin hoy' -> 'medellin'. Empty if no city was named.

    Takes what follows the weather word, starting after the first 'en/de/para' if there is one
    (so 'santiago de cali' stays whole) and dropping the filler words around it."""
    match = CITY_RE.search(cleaned)
    if not match:
        return ""
    rest = match.group(1).split()
    for i, word in enumerate(rest):
        if word in PREPOSITIONS:
            rest = rest[i + 1:]
            break
    words = [w for w in rest if w not in CITY_STOP_WORDS | CITY_SKIP_WORDS | CITY_FILLER]
    while words and words[0] in PREPOSITIONS:
        words = words[1:]
    return " ".join(words)


def skill_weather(text):
    city = extract_city(text)
    try:
        data = fetch_weather(city)
    except Exception as error:  # no internet, or a city that doesn't exist
        log.warning("Weather failed for %r: %s", city or DEFAULT_CITY, error)
        where = f" en {city}" if city else ""
        return f"No pude consultar el clima{where} en este momento.", None
    rain = data["rain"]
    # "hay lluvia", but "está nublado"
    is_event = data["text"] in ("Llovizna", "Lluvia", "Aguaceros", "Niebla", "Tormenta eléctrica", "Nieve")
    state = f"{'Hay' if is_event else 'Está'} {data['text'].lower()}"
    spoken = (f"En {data['city']} hay {data['temp']} grados. {state}. "
              f"Hoy la máxima será de {data['high']} y la mínima de {data['low']}, "
              f"con {rain} por ciento de probabilidad de lluvia.")
    return spoken, "weather?q=" + city.replace(" ", "-")


# --------------------------------------------------------------------- the user's city
CITY_SET_RES = [
    re.compile(r"\bciudad\b.*?\b(?:a|en|por|como)\s+(.+)$"),
    re.compile(r"\b(?:mi ciudad es|vivo en|ahora vivo en|me mude a)\s+(.+)$"),
]


def skill_set_city(text):
    name = ""
    for pattern in CITY_SET_RES:
        match = pattern.search(text)
        if match:
            name = " ".join(w for w in match.group(1).split() if w not in CITY_SKIP_WORDS)
            break
    place = geocode(name) if name else None
    if not place:
        return "No encontré esa ciudad. Dime por ejemplo: cambia la ciudad a Cali.", None
    save_place(place)
    WEATHER_CACHE.clear()
    return f"Listo, ahora tu ciudad es {place['name']}.", None


def skill_auto_city(_text):
    forget_place()
    WEATHER_CACHE.clear()
    place, _ = current_place()
    return f"Listo, usaré tu ubicación automática. Detecto que estás en {place['name']}.", None


def skill_which_city(_text):
    place, source = current_place()
    how = {"saved": "la ciudad que me dijiste", "auto": "tu conexión de internet"}.get(source, "mi configuración")
    return f"Tu ciudad es {place['name']}, según {how}.", None


# ------------------------------------------------------------------ music: internet radio
RADIO_STATE = {"stations": [], "index": 0}
MUSIC_FILLER = {"musica", "radio", "emisora", "la", "el", "de", "una", "algo", "por", "favor", "me", "en", "un", "poco", "las"}
MUSIC_TRIGGERS = {"pon", "ponme", "poner", "toca", "tocar", "reproduce", "reproducir", "quiero", "escuchar", "oir",
                  "escucha", "puedes", "podrias", "pone"}


def start_station(station):
    detail = " · ".join(part for part in [station["state"] or station["country"]] + station["tags"][:1] if part)
    return f"Poniendo {station['name']}.", f"music?id={station['id']}", detail


def skill_music_play(text):
    place, _ = current_place()
    skipped = MUSIC_TRIGGERS | MUSIC_FILLER
    request = " ".join(w for w in text.split() if w not in skipped)
    # The same words with the accents Whisper wrote, which the directory's name search needs ("Olímpica")
    original = " ".join(w.strip(",.;:!¡¿?") for w in ORIGINAL_TEXT.get("text", "").split()
                        if normalize(w) and normalize(w) not in skipped)
    try:
        stations = radio.find(request, place["country"], place["name"], original)
    except Exception as error:
        log.warning("Radio search failed: %s", error)
        return "No pude buscar emisoras en este momento.", None
    if not stations:
        return "No encontré esa emisora.", None
    RADIO_STATE.update(stations=stations, index=0)
    answer, screen, _ = start_station(stations[0])
    return answer, screen


LIBRARY_WORDS = MUSIC_TRIGGERS | MUSIC_FILLER | {"mi", "mis", "cancion", "canciones", "tema", "temas", "biblioteca",
                                                  "computador", "computadora", "pc", "compu", "del", "esa", "esta", "que",
                                                  "se", "llama", "llamada", "llamado", "artista", "grupo", "cantante"}


def library_answer(track, prefix="Poniendo"):
    who = f" de {track['artist']}" if track["artist"] else ""
    return f"{prefix} {track['title']}{who}."


BOOK_WORDS = {"lee", "leer", "leeme", "leame", "lea", "quiero", "abre", "abrir", "el", "la", "un", "una", "libro", "libros",
              "novela", "mi", "mis", "de", "del", "por", "favor", "que", "se", "llama", "titulado", "ponme", "pon", "me", "puedes",
              "podrias", "tengo"}


def book_screen_answer(result):
    """Shows the page on the article screen and reads it aloud."""
    if not result:
        return ("No encontré ese libro. Pon archivos de texto o EPUB en la carpeta Documentos, Libros de tu computador.", None)
    title, text, footer = result
    extras.ARTICLE_LAST.update(title=title, text=text, footer=footer)
    return text, "article"


def skill_book_open(text):
    request = " ".join(w for w in text.split() if w not in BOOK_WORDS)
    books = books_module.library()
    if request:
        book = books_module.find(request)
        return book_screen_answer(books_module.page(book)) if book else (f"No encontré un libro llamado {request}.", None)
    if not books:
        return book_screen_answer(None)
    if len(books) == 1 or books_module.current_path():
        return book_screen_answer(books_module.page(books[0] if len(books) == 1 else None))
    names = ", ".join(b["title"] for b in books[:8])
    extras.ARTICLE_LAST.update(title="Tus libros", text="\n".join(b["title"] for b in books[:8]), footer="Dime: lee el libro y el nombre")
    return f"Tienes {len(books)} libros: {names}. Dime cuál quieres que lea.", "article"


def skill_book_next(_text):
    result = books_module.page()
    if not result:
        return "No estás leyendo ningún libro. Dime: lee un libro.", None
    return book_screen_answer(result)


def skill_book_repeat(_text):
    result = books_module.page(repeat=True)
    if not result:
        return "No estás leyendo ningún libro. Dime: lee un libro.", None
    return book_screen_answer(result)


def skill_library_play(text):
    request = " ".join(w for w in text.split() if w not in LIBRARY_WORDS)
    track = library.start(request)
    if not track:
        if not library.count():
            return "No encontré canciones en tu carpeta de música. Pon archivos MP3 en la carpeta Música de tu computador.", None
        return "No encontré esa canción en tu música.", None
    return library_answer(track), "music?lib=1"


def skill_song_next(_text, direction=1):
    track = library.step(direction)
    if not track:
        return "No hay canciones sonando. Dime: pon mi música.", None
    return library_answer(track, "Siguiente:" if direction > 0 else "Anterior:"), "music?lib=1"


def skill_song_previous(text):
    return skill_song_next(text, -1)


def skill_music_next(_text):
    stations = RADIO_STATE["stations"]
    if len(stations) < 2:
        return "No tengo otra emisora para cambiar. Pídeme una, por ejemplo: pon salsa.", None
    RADIO_STATE["index"] = (RADIO_STATE["index"] + 1) % len(stations)
    answer, screen, _ = start_station(stations[RADIO_STATE["index"]])
    return f"Cambiando. {answer}", screen


def skill_music_stop(_text):
    return "Listo, apagué la música.", "music_stop"


def skill_music_sleep(_text):
    """'apaga la música en 30 minutos': the board turns the music off when the time comes."""
    seconds, _ = timers.parse_duration(ORIGINAL_TEXT.get("text", ""))
    if not seconds:
        return "¿En cuánto tiempo? Dime por ejemplo: apaga la música en 30 minutos.", None
    seconds = min(seconds, 12 * 3600)
    return f"Listo, apago la música en {timers.duration_text(seconds)}.", f"music_sleep?s={seconds}"


def skill_firmware_update(_text):
    return "Voy a buscar una actualización del firmware.", "ota?check"


def skill_triqui(_text):
    return "Juguemos al triqui. Tú eres la equis, toca una casilla.", "triqui?cmd=new"


def skill_stopwatch(text):
    """The board keeps the stopwatch; the server only says what to do with it."""
    if re.search(r"\b(reinicia\w*|resetea\w*|reset|en cero|a cero|borra\w*|limpia\w*)\b", text):
        return "Listo, cronómetro en cero.", "stopwatch?cmd=reset"
    if re.search(r"\b(para|parar|deten\w*|pausa\w*|frena\w*|congela\w*)\b", text):
        return "Cronómetro detenido.", "stopwatch?cmd=pause"
    return "Cronómetro en marcha.", "stopwatch?cmd=start"


# ------------------------------------------------------------------ radio favourites
FAVORITES_FILE = Path(__file__).with_name("favorites.json")


def load_favorites():
    try:
        return json.loads(FAVORITES_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []


def save_favorites(items):
    FAVORITES_FILE.write_text(json.dumps(items, ensure_ascii=False, indent=2), encoding="utf-8")


def current_station():
    stations = RADIO_STATE["stations"]
    return stations[RADIO_STATE["index"]] if stations else None


def skill_fav_add(_text):
    station = current_station()
    if not station:
        return "No hay una emisora sonando. Pon una y luego dime: guarda esta emisora.", None
    favorites = load_favorites()
    if any(f["id"] == station["id"] for f in favorites):
        return f"{station['name']} ya está en tus favoritas.", None
    save_favorites(favorites + [{"id": station["id"], "name": station["name"]}])
    return f"Listo, guardé {station['name']} en tus favoritas.", None


def skill_fav_remove(_text):
    station = current_station()
    favorites = load_favorites()
    target = station["id"] if station else None
    kept = [f for f in favorites if f["id"] != target]
    if not station or len(kept) == len(favorites):
        return "Esa emisora no está en tus favoritas.", None
    save_favorites(kept)
    return f"Listo, quité {station['name']} de tus favoritas.", None


def skill_fav_play(_text):
    stations = []
    for fav in load_favorites():
        try:
            station = radio.station_by_id(fav["id"])
        except Exception as error:
            log.warning("Favourite %s failed: %s", fav["name"], error)
            continue
        if station:
            stations.append(station)
    if not stations:
        return "Todavía no tienes emisoras favoritas. Pon una y dime: guarda esta emisora.", None
    RADIO_STATE.update(stations=stations, index=0)
    answer, screen, _ = start_station(stations[0])
    return answer, screen


def skill_fav_list(_text):
    favorites = load_favorites()
    if not favorites:
        return "Todavía no tienes emisoras favoritas.", None
    names = ", ".join(f["name"] for f in favorites[:5])
    return f"Tus emisoras favoritas son: {names}.", "rows?kind=favorites"


def rows_favorites():
    favorites = load_favorites()
    rows = [{"label": f["name"], "note": "", "value": "", "change": "", "trend": "flat"} for f in favorites[:5]]
    footer = f"y {len(favorites) - 5} más" if len(favorites) > 5 else "Dime: pon mis favoritas"
    return {"type": "rows", "title": "Emisoras favoritas", "rows": rows, "footer": footer}


extras.ROWS_BUILDERS["favorites"] = rows_favorites


# ------------------------------------------------------------------ the morning briefing
def briefing_data():
    now = datetime.now(COLOMBIA)
    data = {"now": now, "weather": None, "events": [], "unread": 0}
    try:
        data["weather"] = fetch_weather("")
    except Exception as error:
        log.warning("Briefing weather failed: %s", error)
    try:
        data["events"] = day_events(0)
    except Exception as error:
        log.warning("Briefing calendar failed: %s", error)
    try:
        data["unread"] = sum(1 for m in mailbox() if m["unread"])
    except Exception as error:
        log.warning("Briefing mail failed: %s", error)
    return data


def skill_briefing(_text):
    data = briefing_data()
    now, weather, events, unread = data["now"], data["weather"], data["events"], data["unread"]
    greeting = "Buenos días" if now.hour < 12 else ("Buenas tardes" if now.hour < 19 else "Buenas noches")
    words = clock_words(now.hour, now.minute)
    spoken = [f"{greeting}. {'Es' if words.startswith('la ') else 'Son'} {words}, "
              f"{DAYS[now.weekday()]} {now.day} de {MONTHS[now.month - 1]}."]
    if weather:
        spoken.append(f"Afuera hay {weather['temp']} grados y {weather['text'].lower()}, con máxima de "
                      f"{weather['high']} y mínima de {weather['low']}.")
    if events:
        first = events[0]
        count = len(events)
        spoken.append(f"Hoy tienes {'un evento' if count == 1 else f'{count} eventos'}. El primero: "
                      f"{event_time_words(first)}, {first['title']}." if not first["all_day"] else
                      f"Hoy tienes {'un evento' if count == 1 else f'{count} eventos'}: {first['title']}.")
    else:
        spoken.append("Hoy no tienes eventos.")
    spoken.append("No tienes correos sin leer." if not unread else
                  f"Tienes {'un correo' if unread == 1 else f'{unread} correos'} sin leer.")
    return " ".join(spoken), "rows?kind=briefing"


def rows_briefing():
    data = briefing_data()
    now, weather, events, unread = data["now"], data["weather"], data["events"], data["unread"]
    rows = [{"label": f"{DAYS[now.weekday()].capitalize()} {now.day}", "note": MONTHS[now.month - 1],
             "value": clock_display(now.hour, now.minute), "change": "", "trend": "flat"}]
    if weather:
        rows.append({"label": weather["city"], "note": f"{weather['text']} · {weather['low']}° a {weather['high']}°",
                     "value": f"{weather['temp']}°", "change": "", "trend": "flat"})
    rows.append({"label": "Agenda de hoy",
                 "note": (events[0]["title"] if events else "Sin eventos")[:34],
                 "value": str(len(events)), "change": "", "trend": "flat"})
    rows.append({"label": "Correos sin leer", "note": "", "value": str(unread), "change": "", "trend": "flat"})
    return {"type": "rows", "title": "Tu resumen", "rows": rows, "footer": f"Actualizado {clock_display(now.hour, now.minute)}"}


extras.ROWS_BUILDERS["briefing"] = rows_briefing


TENS = {"diez": 10, "veinte": 20, "treinta": 30, "cuarenta": 40, "cincuenta": 50, "sesenta": 60, "setenta": 70,
        "ochenta": 80, "noventa": 90, "cien": 100}
VOLUME_NUMBER = r"\d{1,3}|" + "|".join(TENS)


def skill_volume_set(text):
    match = re.search(rf"\b({VOLUME_NUMBER})\b", text)
    if not match:
        return None
    value = match.group(1)
    percent = max(5, min(int(value) if value.isdigit() else TENS[value], 100))
    return f"Listo, volumen al {percent} por ciento.", f"volume?v={percent}"


def skill_volume_up(_text):
    return "Listo, subí el volumen.", "volume?d=15"


def skill_volume_down(_text):
    return "Listo, bajé el volumen.", "volume?d=-15"


def music_screen(args):
    if args.get("lib"):
        track = library.current()
        if not track:
            return {"type": "music", "source": "Mi música", "station": "Mi música", "detail": ""}
        return {"type": "music", "source": "Mi música", "station": track["title"], "detail": track["artist"]}
    station = radio.station_by_id(args.get("id", ""))
    if not station:
        return {"type": "music", "station": "Radio", "detail": ""}
    detail = " · ".join(part for part in [station["state"] or station["country"]] + station["tags"][:1] if part)
    return {"type": "music", "station": station["name"], "detail": detail}


SCREENS = {
    **extras.SCREENS,
    "music": music_screen,
    "weather": lambda args: fetch_weather(args.get("q", "").replace("-", " ")),
    "email": lambda args: email_screen(),
    "email_detail": lambda args: email_detail_screen(max(0, min(int(args.get("i", 0)), len(mailbox()) - 1))),
    "calendar": lambda args: calendar_screen(),
}

# Order matters: the first pattern that matches wins.
SKILLS = [
    *trivia.SKILLS,  # an answer only counts while a question is open; otherwise these step aside
    # "cierra la música" is a music command, not "go Home"
    (re.compile(r"^(?!.*\b(musica|radio|emisora|cancion)\b).*\b(inicio|cierra|cerrar|volver|vuelve|atras|salir)\b"), skill_home),
    # Books come before email, so "lee el libro" never reaches "lee el primero"
    (re.compile(r"\brepite\w*\b.*\b(pagina|parte|eso)\b"), skill_book_repeat),
    (re.compile(r"^(?:\w+ )?(sigue|siguen|continua|continuar)\b(?: leyendo| con el libro)?$|\b(siguiente|proxima) pagina\b|"
                r"\bsigue leyendo\b|\bcontinua leyendo\b"), skill_book_next),
    (re.compile(r"\b(lee\w*|leer|quiero leer|abre|abrir)\b.*\b(libros?|novela)\b|\b(mis libros|que libros tengo)\b"), skill_book_open),
    (re.compile(r"\b(lee|leer|leeme|abre|abrir)\b.*\b(primer\w*|segund\w*|tercer\w*|cuart\w*|uno|dos|tres|cuatro|1|2|3|4)\b"),
     skill_email_read),
    (re.compile(r"\b(correos?|emails?|e mails?|mails?|mensajes|bandeja)\b"), skill_email_list),
    (re.compile(r"\b(calendario|agenda|reuniones?|citas?|eventos?)\b|\bque tengo (hoy|manana)\b"), skill_calendar),
    # Morning briefing, lists, radio favourites and the music sleep timer come before the timers,
    # whose words ("temporizador", "recuérdame") would otherwise claim some of these sentences
    (re.compile(r"\b(buenos dias|buen dia)\b|\b(resumen|que hay para hoy|como (va|amanecio) el dia)\b"),
     skill_briefing),
    (re.compile(r"\b(actualiza\w*|busca\w*|instala\w*)\b.*\b(firmware|actualizacion\w*|version)\b|\bactualizate\b"), skill_firmware_update),
    (re.compile(r"\bcronometro\b"), skill_stopwatch),
    (re.compile(r"\b(triqui|tres en raya|tres en linea|ta te ti)\b|\bjuguemos\b|\bjugar\b.*\bgato\b"), skill_triqui),
    *personal.SKILLS,
    (re.compile(r"\b(quita\w*|borra\w*|elimina\w*|saca\w*)\b.*\bfavorit\w*"), skill_fav_remove),
    (re.compile(r"\b(pon|ponme|poner|toca|reproduce|escuchar|oir|abre)\b.*\bfavorit\w*"), skill_fav_play),
    (re.compile(r"\b(cuales|que|muestra\w*|dime|lista)\b.*\bfavorit\w*"), skill_fav_list),
    (re.compile(r"\b(guarda\w*|agrega\w*|anade\w*|marca\w*)\b.*\b(emisora|radio|estacion|favorit\w*)\b|\bme gusta (esta|la) (emisora|radio)\b"),
     skill_fav_add),
    (re.compile(r"\b(apaga\w*|para|parar|deten\w*|quita\w*|pausa\w*)\b.*\b(musica|radio|emisora|cancion)\b.*\b(minutos?|horas?|media hora)\b|"
                r"\b(temporizador|timer) de (sueno|dormir)\b|\bme voy a dormir\b"), skill_music_sleep),
    *daily.SKILLS,
    *timers.SKILLS,
    # The city comes before weather, so "cambia la ciudad a Cali" isn't read as a weather question
    (re.compile(r"\b(detect\w*|usa|usar)\b.*\b(ciudad|ubicacion)\b"), skill_auto_city),
    (re.compile(r"\b(cambia|cambiar|cambiame|pon|configura|establece|actualiza)\b.*\bciudad\b|\bmi ciudad es\b|\b(ahora )?vivo en\b|\bme mude a\b"),
     skill_set_city),
    (re.compile(r"\b(en que ciudad|cual es mi ciudad|donde vivo|donde estoy)\b"), skill_which_city),
    # Music. Stopping and volume come before "pon", so "pon el volumen mas alto" isn't a station request
    # "su\w*" also catches what the recognizer makes of "sube" ("sue el volumen")
    (re.compile(rf"\bvolumen\b.*\b({VOLUME_NUMBER})\b"), skill_volume_set),  # "pon el volumen al 50 por ciento"
    (re.compile(r"\b(su\w*|aumenta\w*)\b.*\bvolumen\b|\bmas (alto|fuerte)\b"), skill_volume_up),
    (re.compile(r"\b(baja|bajar|disminuye\w*)\b.*\bvolumen\b|\bmas (bajo|suave|despacio)\b"), skill_volume_down),
    (re.compile(r"\b(para|parar|deten\w*|apaga\w*|quita\w*|pausa\w*|cierra)\b.*\b(musica|radio|emisora|cancion)\b|\bsilencio\b"),
     skill_music_stop),
    # The user's own files. "otra cancion" is the library; "otra emisora" stays the radio
    (re.compile(r"\b(anterior|previa|devuelve\w*|regresa\w*)\b.*\b(cancion|tema)\b|\b(cancion|tema)\b.*\banterior\b"), skill_song_previous),
    (re.compile(r"\b(otra|siguiente|proxima|salta\w*|pasa\w*|cambia\w*)\b.*\b(cancion|tema)\b|\b(cancion|tema)\b.*\bsiguiente\b"), skill_song_next),
    (re.compile(r"\b(mi musica|mis canciones|mi biblioteca|mi lista)\b|\b(musica|canciones) (de|del) (mi )?(computador\w*|pc|compu)\b|"
                r"\b(pon|ponme|poner|toca|reproduce|escuchar|oir)\b.*\b(cancion|tema)\b"), skill_library_play),
    (re.compile(r"\b(otra|siguiente|cambia de|cambiar de|cambia la)\b.*\b(emisora|radio|estacion)\b"), skill_music_next),
    (re.compile(r"\b(pon|ponme|poner|toca|reproduce|escuchar|oir)\b.*\b(musica|radio|emisora|salsa|vallenato|reggaeton|rock|pop|baladas?|"
                r"clasica|jazz|electronica|merengue|bachata|cumbia|tango|metal|hip hop|rap|boleros?|tropical|urbana|noticias|deportes|"
                r"ranchera|mariachi|tropicana|olimpica|caracol|rcn|los 40|la fm|blu|w radio)\b|\bquiero (escuchar|oir)\b"),
     skill_music_play),
    (re.compile(r"\b(clima\w*|pronostico|temperatura\w*)\b|\bque tiempo hace\b|\b(va a llover|llueve|llover)\b"),
     skill_weather),
    *extras.SPECIFIC_SKILLS,
    (re.compile(r"\bhora\b"), skill_time),
    (re.compile(r"\b(fecha|que dia|a cuantos estamos|a cuanto estamos)\b"), skill_date),
    (re.compile(r"\b(como te llamas|cual es tu nombre|quien eres)\b"),
     lambda _t: "Soy Bug, tu asistente."),
    (re.compile(r"\bcomo estas\b"), lambda _t: "Muy bien, gracias por preguntar."),
    (re.compile(r"\bgracias\b"), lambda _t: "Con gusto."),
    *extras.GENERIC_SKILLS,  # "¿quién fue...?" and similar, only when nothing more specific matched
]
FALLBACK = ("Todavía no sé hacer eso. Puedo decirte la hora, el clima, las noticias o el precio del dólar, "
            "poner música, contar un chiste, hacer cuentas y buscar en Wikipedia.")


ORIGINAL_TEXT = extras.ORIGINAL  # the accented sentence for the skill running now (radio search, math, conversions)
timers.clock_words = clock_words


def run_skill(text):
    """Returns (spoken answer, screen to open or None)."""
    ORIGINAL_TEXT["text"] = text
    cleaned = normalize(text)
    for pattern, skill in SKILLS:
        if pattern.search(cleaned):
            result = skill(cleaned)
            if result is None:  # the skill looked and decided the sentence isn't for it
                continue
            return result if isinstance(result, tuple) else (result, None)
    return FALLBACK, None


# ------------------------------------------------------------- speech in / out
def add_cuda_dll_paths():
    """pip's NVIDIA packages keep their DLLs under site-packages/nvidia/*/bin; Windows needs them on PATH."""
    if os.name != "nt":
        return
    import site
    from pathlib import Path

    for root in site.getsitepackages():
        for folder in Path(root, "nvidia").glob("*/bin"):
            os.add_dll_directory(str(folder))
            os.environ["PATH"] = str(folder) + os.pathsep + os.environ["PATH"]


def load_model():
    """The stronger model on the GPU when it works, the small one on the CPU otherwise."""
    from faster_whisper import WhisperModel

    add_cuda_dll_paths()
    attempts = []
    if WHISPER_DEVICE in ("auto", "cuda"):
        attempts.append(("cuda", "float16", WHISPER_MODEL or "turbo", 5))
    if WHISPER_DEVICE in ("auto", "cpu"):
        attempts.append(("cpu", "int8", WHISPER_MODEL or "small", 1))

    for device, compute, name, beam in attempts:
        try:
            log.info("Loading Whisper '%s' on %s (the first run downloads it)...", name, device)
            loaded = WhisperModel(name, device=device, compute_type=compute)
            # A model can load and still fail on first use when a library is missing, so decode once now
            list(loaded.transcribe(np.zeros(SAMPLE_RATE, dtype=np.float32), language="es")[0])
            MODEL_INFO.update(name=name, device=device, beam=beam)
            log.info("Speech recognition ready: %s on %s", name, device)
            return loaded
        except Exception as error:
            log.warning("Whisper '%s' on %s failed: %s", name, device, error)
    raise RuntimeError("no speech recognition model could be loaded")


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
        beam_size=MODEL_INFO["beam"],
        condition_on_previous_text=False,
        vad_filter=True,
        vad_parameters={"min_silence_duration_ms": 400},
        # The words Bug expects: Whisper spells them right far more often when it has seen them first
        initial_prompt=("Hola Bug. Hey Bug. ¿Qué hora es? Pon salsa. Pon Tropicana. Pon Olímpica Stereo. Cambia de emisora. "
                        "Sube el volumen. Baja el volumen. Para la música. Muéstrame los correos. Lee el primero. "
                        "¿Qué tengo en el calendario? ¿Cómo está el clima en Cali? Cambia la ciudad a Medellín. "
                        "Pon mi música. Siguiente canción. Pon un temporizador de 10 minutos. Despiértame a las 6 y media. "
                        "Inicia el cronómetro. Juguemos al triqui. Juguemos trivia. Agrega leche a la lista de compras. "
                        "Anota que mañana hay reunión. Buenos días. Guarda esta emisora en favoritas. Lee el libro. "
                        "Sigue leyendo. Apaga la música en 30 minutos. ¿Cuánto es 25 más 17? Convierte 5 millas a kilómetros."),
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
def respond(action, heard="", body=b"", reply="", screen=None):
    """Action, then what was understood and answered (percent-encoded so accents survive HTTP).
    Ignored speech is never sent back, so the board can't display room chatter.
    `screen` names the screen to open; the board then fetches its data from /screen/<screen>."""
    headers = {"X-Action": action}
    if action != "ignore":
        headers["X-Heard"] = quote(heard[:120], safe="")
        headers["X-Reply"] = quote(reply[:240], safe="")
    if screen:
        headers["X-Screen"] = quote(screen, safe="?=&")
    return Response(body, mimetype="application/octet-stream", headers=headers)


@app.get("/health")
def health():
    return jsonify(status="ok", model=MODEL_INFO["name"], device=MODEL_INFO["device"], voice=VOICE)


@app.get("/music/stream")
def music_stream():
    """Live radio, or the user's own songs, as 16 kHz mono PCM. The board plays it as it arrives;
    hanging up stops it. A library connection plays one song and ends: the board reconnects and
    gets the next one with its title in X-Title / X-Artist."""
    if request.args.get("lib"):
        track = library.current()
        if not track:
            return jsonify(error="nothing to play"), 404
        log.info("Playing %s", track["title"])

        def play_song():
            try:
                yield from library.pcm_chunks(track)
            except GeneratorExit:
                pass
            finally:
                log.info("Song closed: %s", track["title"])

        return Response(play_song(), mimetype="application/octet-stream",
                        headers={"X-Title": quote(track["title"]), "X-Artist": quote(track["artist"])})
    station = radio.station_by_id(request.args.get("id", ""))
    if not station:
        return jsonify(error="unknown station"), 404
    log.info("Streaming %s", station["name"])

    def generate():
        try:
            yield from radio.pcm_chunks(station["url"])
        except GeneratorExit:
            pass
        except Exception as error:
            log.warning("Stream ended: %s", error)
        finally:
            log.info("Stream closed: %s", station["name"])

    return Response(generate(), mimetype="application/octet-stream")


@app.get("/screen/<name>")
def screen_data(name):
    """The data a skill screen draws. Today this is sample data; later the real accounts."""
    builder = SCREENS.get(name)
    if not builder:
        return jsonify(error="unknown screen"), 404
    try:
        return jsonify(builder(request.args))
    except Exception as error:  # a service being down must not turn into an HTML error page the board can't read
        log.warning("Screen %s failed: %s", name, error)
        return jsonify(error=str(error)), 502


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
            return respond("ignore")
        if not rest:
            return respond("wake", heard)
        command = rest
    else:
        command = rest if woke and rest else heard

    answer, screen = run_skill(command)
    audio = synthesize(answer)
    log.info("  command=%r -> %r screen=%s (total %.1fs)", command, answer, screen, time.time() - started)
    return respond("reply", command, audio, answer, screen)


if __name__ == "__main__":
    model = load_model()
    log.info("Bug server ready on port %d. Voice: %s", PORT, VOICE)
    app.run(host="0.0.0.0", port=PORT, threaded=True)
