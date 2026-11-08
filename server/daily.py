"""Everyday helpers that need no account or key: Colombian public holidays, days until a date,
sunrise and sunset, and translation.

  "cuándo es el próximo festivo"   "mañana es festivo"   "cuántos días faltan para Navidad"
  "a qué hora se oculta el sol"    "cómo se dice casa en inglés"   "traduce buenos días al francés"
"""
import logging
import re
from datetime import date, datetime, timedelta, timezone

import extras
from location import current_place, get_json
from text_utils import normalize

log = logging.getLogger("bug")

COLOMBIA = timezone(timedelta(hours=-5))
DAYS = ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"]
MONTHS = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"]


def today():
    return datetime.now(COLOMBIA).date()


# ------------------------------------------------------------------------------ holidays
def easter(year):
    """Easter Sunday (the usual Gregorian computation)."""
    a, b, c = year % 19, year // 100, year % 100
    d, e = b // 4, b % 4
    f = (b + 8) // 25
    g = (b - f + 1) // 3
    h = (19 * a + b - d - g + 15) % 30
    i, k = c // 4, c % 4
    l = (32 + 2 * e + 2 * i - h - k) % 7
    m = (a + 11 * h + 22 * l) // 451
    month = (h + l - 7 * m + 114) // 31
    day = (h + l - 7 * m + 114) % 31 + 1
    return date(year, month, day)


def to_monday(d):
    """Colombia moves many holidays to the following Monday (Ley Emiliani)."""
    return d + timedelta(days=(7 - d.weekday()) % 7)


def holidays(year):
    """[(date, name)] of the year's public holidays in Colombia, in order."""
    pascua = easter(year)
    found = [
        (date(year, 1, 1), "Año Nuevo"),
        (to_monday(date(year, 1, 6)), "Día de los Reyes Magos"),
        (to_monday(date(year, 3, 19)), "Día de San José"),
        (pascua - timedelta(days=3), "Jueves Santo"),
        (pascua - timedelta(days=2), "Viernes Santo"),
        (date(year, 5, 1), "Día del Trabajo"),
        (to_monday(pascua + timedelta(days=39)), "Ascensión del Señor"),
        (to_monday(pascua + timedelta(days=60)), "Corpus Christi"),
        (to_monday(pascua + timedelta(days=68)), "Sagrado Corazón"),
        (to_monday(date(year, 6, 29)), "San Pedro y San Pablo"),
        (date(year, 7, 20), "Día de la Independencia"),
        (date(year, 8, 7), "Batalla de Boyacá"),
        (to_monday(date(year, 8, 15)), "Asunción de la Virgen"),
        (to_monday(date(year, 10, 12)), "Día de la Raza"),
        (to_monday(date(year, 11, 1)), "Todos los Santos"),
        (to_monday(date(year, 11, 11)), "Independencia de Cartagena"),
        (date(year, 12, 8), "Inmaculada Concepción"),
        (date(year, 12, 25), "Navidad"),
    ]
    return sorted(found)


def upcoming_holidays(count, start=None):
    start = start or today()
    out = []
    for year in (start.year, start.year + 1):
        out += [h for h in holidays(year) if h[0] >= start]
    return out[:count]


def date_words(d):
    return f"{DAYS[d.weekday()]} {d.day} de {MONTHS[d.month - 1]}"


def days_text(n):
    return "hoy" if n == 0 else "mañana" if n == 1 else f"en {n} días"


def skill_next_holiday(text):
    start = today()
    if re.search(r"\b(hoy|manana)\b", text) and re.search(r"\bfestivo\b", text) and "proximo" not in text:
        target = start + timedelta(days=1 if "manana" in text else 0)
        name = next((n for d, n in holidays(target.year) if d == target), None)
        label = "mañana" if target != start else "hoy"
        if name:
            return f"Sí, {label} es festivo: {name}.", None
        return f"No, {label} no es festivo.", None
    items = upcoming_holidays(5)
    first_date, first_name = items[0]
    left = (first_date - start).days
    ROWS["holidays"] = items
    return (f"El próximo festivo es {first_name}, el {date_words(first_date)}, {days_text(left)}. "
            f"Después viene {items[1][1]}, el {date_words(items[1][0])}."), "rows?kind=holidays"


ROWS = {"holidays": []}


def rows_holidays():
    start = today()
    items = ROWS["holidays"] or upcoming_holidays(5)
    rows = [{"label": name, "note": date_words(d), "value": days_text((d - start).days).replace("en ", ""), "change": "", "trend": "flat"}
            for d, name in items[:5]]
    return {"type": "rows", "title": "Próximos festivos", "rows": rows, "footer": "Colombia"}


extras.ROWS_BUILDERS["holidays"] = rows_holidays


# ---------------------------------------------------------------------------- countdowns
NAMED_DATES = {
    "navidad": (12, 25), "ano nuevo": (1, 1), "fin de ano": (12, 31), "nochebuena": (12, 24), "halloween": (10, 31),
    "dia de la independencia": (7, 20), "independencia": (7, 20), "dia del trabajo": (5, 1), "san valentin": (2, 14),
    "dia de reyes": (1, 6), "dia de los ninos": (4, 30),
}
MONTH_NUMBERS = {normalize(m): i + 1 for i, m in enumerate(MONTHS)}
DATE_RE = re.compile(r"\b(?:el )?(\d{1,2}) de (" + "|".join(MONTH_NUMBERS) + r")\b")


def skill_countdown(text):
    target = None
    match = DATE_RE.search(text)
    if match:
        month, day = MONTH_NUMBERS[match.group(2)], int(match.group(1))
    else:
        found = next(((k, v) for k, v in NAMED_DATES.items() if k in text), None)
        if not found:
            if "semana santa" in text:
                d = next(d for d, n in upcoming_holidays(40) if n == "Jueves Santo")
                n = (d - today()).days
                return f"La Semana Santa empieza el {date_words(d)}, {days_text(n)}.", None
            return None
        month, day = found[1]
    start = today()
    for year in (start.year, start.year + 1):
        try:
            target = date(year, month, day)
        except ValueError:
            return "Esa fecha no existe.", None
        if target >= start:
            break
    n = (target - start).days
    name = next((k for k, v in NAMED_DATES.items() if v == (month, day) and k in text), None)
    if n == 0:
        return f"¡Es hoy! {date_words(target).capitalize()}.", None
    unit = "día" if n == 1 else "días"
    return f"Falta{'n' if n != 1 else ''} {n} {unit} para el {date_words(target)}" + (f", {name}." if name else "."), None


# ------------------------------------------------------------------------------ sun times
def skill_sun(_text):
    try:
        place, _ = current_place()
        data = get_json("https://api.open-meteo.com/v1/forecast",
                        {"latitude": place["lat"], "longitude": place["lon"], "daily": "sunrise,sunset",
                         "timezone": "auto", "forecast_days": 1})
        sunrise = datetime.fromisoformat(data["daily"]["sunrise"][0])
        sunset = datetime.fromisoformat(data["daily"]["sunset"][0])
    except Exception as error:
        log.warning("Sun times failed: %s", error)
        return "No pude consultar la hora del sol en este momento.", None

    def clock(d):
        return f"{d.hour % 12 or 12}:{d.minute:02d} {'am' if d.hour < 12 else 'pm'}"

    return f"Hoy en {place['name']} el sol sale a las {clock(sunrise)} y se oculta a las {clock(sunset)}.", None


# ----------------------------------------------------------------------------- translation
LANGUAGES = {"ingles": ("en", "inglés"), "frances": ("fr", "francés"), "portugues": ("pt", "portugués"),
             "aleman": ("de", "alemán"), "italiano": ("it", "italiano")}
TRANSLATE_RES = [
    re.compile(r"\bcomo se dice\s+(.+?)\s+en\s+(ingles|frances|portugues|aleman|italiano)\b"),
    re.compile(r"\b(?:traduce|traducir|traduceme|traduzca)\s+(.+?)\s+(?:al|a|en)\s+(ingles|frances|portugues|aleman|italiano)\b"),
]


def skill_translate(text):
    plain = extras.plain(extras.ORIGINAL["text"])
    phrase, language = None, None
    for pattern in TRANSLATE_RES:
        match = pattern.search(plain)
        if match:
            # Same positions in the original sentence, so the accents are kept for the translator
            phrase = extras.ORIGINAL["text"][match.start(1):match.end(1)].strip(" ,.;:!¡¿?\"'")
            language = match.group(2)
            break
    if not phrase:
        return None
    code, name = LANGUAGES[language]
    try:
        data = get_json("https://api.mymemory.translated.net/get", {"q": phrase, "langpair": f"es|{code}"})
        translated = data["responseData"]["translatedText"]
    except Exception as error:
        log.warning("Translation failed: %s", error)
        return "No pude traducir en este momento.", None
    if not translated or "MYMEMORY" in translated.upper():
        return "No pude traducir eso.", None
    extras.ARTICLE_LAST.update(title=f"En {name}", text=f"{phrase}\n\n{translated}", footer="Traducción")
    return f"En {name} se dice: {translated}.", "article"


SKILLS = [
    (re.compile(r"\b(festivo|festivos|puente festivo)\b"), skill_next_holiday),
    (re.compile(r"\b(cuantos dias (faltan|quedan|hay)|cuanto falta|cuanto tiempo falta|dias para)\b.*\b(para|hasta)\b|"
                r"\bcuando es (navidad|halloween|san valentin|semana santa)\b|\bfaltan\b.*\bpara\b"), skill_countdown),
    (re.compile(r"\b(sale el sol|se oculta el sol|se pone el sol|amanecer|atardecer|puesta del sol|salida del sol)\b"), skill_sun),
    (re.compile(r"\b(como se dice|traduce|traducir|traduceme)\b"), skill_translate),
]
