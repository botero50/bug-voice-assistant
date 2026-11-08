"""Timers, alarms and reminders.

The server understands the request ("despiértame a las 6 y media", "recuérdame en 20 minutos llamar
a Ana") and remembers the list. The board saves each entry in its own memory and rings when one is
due, even if this PC is off. Entries are sent to the board as an action: timer?id=3&at=<epoch>&l=<label>.
"""
import json
import logging
import re
import time
from datetime import datetime, timedelta
from pathlib import Path
from urllib.parse import quote

import extras
from text_utils import normalize

log = logging.getLogger("bug")

STATE_FILE = Path(__file__).with_name("timers.json")
clock_words = None  # set by bug_server: 'las 6 y media de la mañana'

NUMBERS = {
    "un": 1, "uno": 1, "una": 1, "dos": 2, "tres": 3, "cuatro": 4, "cinco": 5, "seis": 6, "siete": 7, "ocho": 8,
    "nueve": 9, "diez": 10, "once": 11, "doce": 12, "trece": 13, "catorce": 14, "quince": 15, "dieciseis": 16,
    "diecisiete": 17, "dieciocho": 18, "diecinueve": 19, "veinte": 20, "veintiuno": 21, "veintidos": 22,
    "veintitres": 23, "veinticuatro": 24, "veinticinco": 25, "treinta": 30, "cuarenta": 40, "cincuenta": 50,
    "sesenta": 60,
}
# Words that belong to the request itself, not to what the user wants to be reminded of
TRIGGER_WORDS = {
    "pon", "ponme", "poner", "programa", "programame", "configura", "crea", "un", "una", "el", "la", "de", "en",
    "a", "las", "los", "para", "que", "me", "te", "dentro", "temporizador", "temporizadores", "alarma", "alarmas",
    "despiertame", "despertador", "recuerdame", "recordatorio", "recordatorios", "avisame", "cuenta", "regresiva",
    "por", "favor", "y", "con", "nombre", "llamado", "llamada", "hola", "bug",
}


# ---------------------------------------------------------------------------- the saved list
def _load():
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []


def _save(entries):
    STATE_FILE.write_text(json.dumps(entries, ensure_ascii=False, indent=2), encoding="utf-8")


def active_entries():
    """Entries that haven't rung yet (a few seconds of grace), soonest first."""
    now = time.time()
    entries = [e for e in _load() if e["at"] > now - 5]
    entries.sort(key=lambda e: e["at"])
    return entries


def add_entry(at, label, kind):
    entries = _load()
    entry_id = max([e["id"] for e in entries] + [0]) + 1
    entry = {"id": entry_id, "at": int(at), "label": label, "kind": kind}
    entries = [e for e in entries if e["at"] > time.time() - 5] + [entry]
    _save(entries)
    return entry


def remove_entries(kind=None):
    """Removes every active entry of that kind (or all). Returns the removed ids."""
    entries = _load()
    now = time.time()
    removed = [e["id"] for e in entries if e["at"] > now - 5 and (kind is None or e["kind"] == kind)]
    _save([e for e in entries if e["id"] not in removed])
    return removed


# --------------------------------------------------------------------------- understanding
def plain_text(text):
    """Lowercase, accents removed, same length as the input (so positions line up with the original)."""
    return extras.plain(text)


def words_to_number(text):
    """'diez' -> 10, 'cuarenta y cinco' -> 45, '7' -> 7. None if it isn't a number."""
    text = text.strip()
    if re.fullmatch(r"\d+", text):
        return int(text)
    total = 0
    for part in text.split():
        if part == "y":
            continue
        if part not in NUMBERS:
            return None
        total += NUMBERS[part]
    return total or None


HALF_QUARTER_RE = re.compile(r"media hora|cuarto de hora")
DURATION_RE = re.compile(
    r"(?P<n>\d+(?:[.,]\d+)?|[a-z]+(?:\s+y\s+[a-z]+)?)\s*(?P<u>horas?|hrs?|minutos?|mins?|segundos?|segs?)\b"
    r"(?P<half>\s*y\s*media)?")


def parse_duration(sentence):
    """Total seconds in 'diez minutos', '2 horas y media', 'media hora', '1 hora 30 minutos'.
    Returns (seconds, spans) where spans are the (start, end) positions of what was understood."""
    text = plain_text(sentence)
    total, spans = 0.0, []
    for match in HALF_QUARTER_RE.finditer(text):
        total += 1800 if match.group(0) == "media hora" else 900
        spans.append(match.span())
    masked = HALF_QUARTER_RE.sub(lambda m: " " * len(m.group(0)), text)  # same length, so positions stay valid
    for match in DURATION_RE.finditer(masked):
        raw = match.group("n")
        number = float(raw.replace(",", ".")) if re.fullmatch(r"\d+(?:[.,]\d+)?", raw) else words_to_number(raw)
        if number is None:
            continue
        unit = match.group("u")
        seconds = 3600 if unit.startswith("h") else 60 if unit.startswith("m") else 1
        total += number * seconds + (1800 if match.group("half") and seconds == 3600 else 0)
        spans.append(match.span())
    return int(total), spans


CLOCK_RE = re.compile(
    r"\ba las?\s+(?P<h>\d{1,2}|[a-z]+)(?:\s*[:.]\s*(?P<m>\d{2}))?\s*(?P<extra>y media|y cuarto|menos cuarto|en punto)?\s*"
    r"(?P<p>a\.? ?m\.?|p\.? ?m\.?|de la manana|de la tarde|de la noche|de la madrugada|del mediodia)?")


def parse_clock(sentence):
    """The clock time asked for ('a las 6:30 de la mañana'): (datetime in Colombia, span) or None."""
    text = plain_text(sentence)
    match = CLOCK_RE.search(text)
    if not match:
        return None
    hour = words_to_number(match.group("h"))
    if hour is None or hour > 24:
        return None
    minute = int(match.group("m") or 0)
    extra = match.group("extra") or ""
    if extra == "y media":
        minute = 30
    elif extra == "y cuarto":
        minute = 15
    elif extra == "menos cuarto":
        hour, minute = (hour - 1) % 24, 45

    period = (match.group("p") or "").replace(" ", "").replace(".", "")
    now = extras.datetime.now(extras.COLOMBIA)
    tomorrow = "manana" in text.replace("de la manana", "")
    day = (now + timedelta(days=1 if tomorrow else 0)).replace(second=0, microsecond=0)

    if period in ("am", "delamanana", "delamadrugada"):
        candidates = [hour % 12]
    elif period in ("pm", "delatarde", "delanoche"):
        candidates = [hour % 12 + 12]
    elif period == "delmediodia":
        candidates = [12]
    elif hour == 0 or hour > 12:
        candidates = [hour % 24]
    else:
        candidates = [hour % 12, hour % 12 + 12]  # no am/pm said: the next time that happens

    options = [day.replace(hour=h, minute=minute) for h in candidates]
    future = [o for o in options if o > now + timedelta(seconds=30)]
    if not future:  # already passed today: the same time tomorrow
        future = [o + timedelta(days=1) for o in options]
    return min(future), match.span()


def extract_label(original, spans):
    """What is left once the time and the request words are taken out: 'llamar a Ana'.
    Only the words at the start and end are dropped, so 'llamar a Ana' keeps its 'a'."""
    chars = list(original)
    for start, end in sorted(spans, reverse=True):
        chars[start:end] = " "
    words = [w.strip(",.;:!¡¿?\"'") for w in "".join(chars).split()]
    words = [w for w in words if w]
    while words and normalize(words[0]) in TRIGGER_WORDS:
        words.pop(0)
    while words and normalize(words[-1]) in TRIGGER_WORDS:
        words.pop()
    return " ".join(words)[:40]


def duration_text(seconds):
    hours, rest = divmod(int(seconds), 3600)
    minutes, secs = divmod(rest, 60)
    parts = []
    if hours:
        parts.append(f"{hours} hora" + ("s" if hours != 1 else ""))
    if minutes:
        parts.append(f"{minutes} minuto" + ("s" if minutes != 1 else ""))
    if secs and not hours:
        parts.append(f"{secs} segundo" + ("s" if secs != 1 else ""))
    return " y ".join(parts) or "0 segundos"


def short_remaining(seconds):
    seconds = max(0, int(seconds))
    if seconds >= 3600:
        return f"{seconds // 3600} h {seconds % 3600 // 60} min"
    if seconds >= 60:
        return f"{seconds // 60} min"
    return f"{seconds} s"


def board_action(entry):
    return f"timer?id={entry['id']}&at={entry['at']}&l={quote(entry['label'] or 'Temporizador', safe='')}"


# --------------------------------------------------------------------------------- skills
def skill_cancel(plain_sentence):
    if "temporizador" in plain_sentence:
        kind, name, plural = "timer", "el temporizador", "temporizadores"
    elif re.search(r"alarma|despertador", plain_sentence):
        kind, name, plural = "alarm", "la alarma", "alarmas"
    elif "recordatorio" in plain_sentence:
        kind, name, plural = "reminder", "el recordatorio", "recordatorios"
    else:
        kind, name, plural = None, "lo que tenías programado", "cosas programadas"
    removed = remove_entries(kind)
    if not removed:
        return "No tienes nada programado para cancelar.", None
    spoken = f"Listo, cancelé {name}." if len(removed) == 1 else f"Listo, cancelé {len(removed)} {plural}."
    return spoken, "timer_cancel?ids=" + ",".join(str(i) for i in removed)


def skill_timers(_text):
    original = extras.ORIGINAL["text"]
    plain_sentence = plain_text(original)

    if re.search(r"\b(cancela\w*|quita\w*|elimina\w*|borra\w*|apaga\w*)\b", plain_sentence):
        return skill_cancel(plain_sentence)

    seconds, spans = parse_duration(original)
    clock = parse_clock(original)

    # "¿Qué alarmas tengo?", "¿cuánto falta?"
    asks_status = re.search(r"\bcuanto falta\b", plain_sentence) or (
        re.search(r"\b(que|cuales|cuantos|cuantas|mis|hay)\b", plain_sentence) and not seconds and not clock)
    if asks_status:
        entries = active_entries()
        if not entries:
            return "No tienes temporizadores ni alarmas.", None
        now = time.time()
        parts = [f"{e['label'] or 'Temporizador'}, en {duration_text(e['at'] - now)}" for e in entries[:3]]
        count = len(entries)
        intro = "Tienes una cosa programada" if count == 1 else f"Tienes {count} cosas programadas"
        return f"{intro}. " + "; ".join(parts) + ".", "rows?kind=timers"

    # A clock time, "a las 6:30": an alarm
    if clock and not seconds:
        when, span = clock
        label = extract_label(original, [span])
        entry = add_entry(when.timestamp(), label or "Alarma", "alarm")
        day = "mañana " if when.date() != extras.datetime.now(extras.COLOMBIA).date() else ""
        start = f"te recordaré {label} " if label else "te despierto "
        return f"Listo, {start}{day}a {clock_words(when.hour, when.minute)}.", board_action(entry)

    # A length of time, "10 minutos": a timer, or a reminder if something is to be remembered
    if seconds:
        label = extract_label(original, spans)
        entry = add_entry(time.time() + seconds, label or "Temporizador", "reminder" if label else "timer")
        if label:
            return f"Listo, te recordaré {label} en {duration_text(seconds)}.", board_action(entry)
        return f"Listo, temporizador de {duration_text(seconds)}.", board_action(entry)

    return "¿Para cuándo? Dime por ejemplo: pon un temporizador de 10 minutos, o despiértame a las 6 y media.", None


def rows_timers():
    now = time.time()
    rows = []
    for entry in active_entries()[:4]:
        when = datetime.fromtimestamp(entry["at"], extras.COLOMBIA)
        rows.append({"label": entry["label"] or "Temporizador",
                     "note": f"suena a las {when.hour % 12 or 12}:{when.minute:02d} {'am' if when.hour < 12 else 'pm'}",
                     "value": short_remaining(entry["at"] - now), "change": "", "trend": "flat"})
    return {"type": "rows", "title": "Temporizadores y alarmas", "rows": rows,
            "footer": "La placa los guarda y suena aunque el PC esté apagado"}


extras.ROWS_BUILDERS["timers"] = rows_timers

SKILLS = [
    (re.compile(r"\b(temporizador\w*|alarmas?|despiertame|despertador|recuerdame|recordatorios?|avisame|cuenta regresiva|"
                r"cuanto falta)\b"), skill_timers),
]
