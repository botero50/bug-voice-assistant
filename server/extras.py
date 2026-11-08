"""Skills that need no account and no key: crypto and dollar prices, news, Wikipedia, jokes,
quick math and conversions, dice and coin, and this PC's status.

Each skill returns (spoken answer, screen to open or None). Skills with a screen name it, and the
board then fetches the data from /screen/<name>, which the builders at the bottom produce.
"""
import logging
import random
import re
import shutil
import subprocess
import time
import unicodedata
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone

import requests

from text_utils import normalize

log = logging.getLogger("bug")

HEADERS = {"User-Agent": "BugVoiceAssistant/0.1 (personal project)"}
COLOMBIA = timezone(timedelta(hours=-5))
ORIGINAL = {"text": ""}  # the accented sentence being answered; the server sets it before running skills
_cache = {}


# ----------------------------------------------------------------------------- helpers
def cached(key, seconds, fetch):
    """Calls fetch() at most once per `seconds`; reuses the old value if the service fails."""
    entry = _cache.get(key)
    if entry and time.time() - entry[0] < seconds:
        return entry[1]
    try:
        value = fetch()
    except Exception as error:
        if entry:
            log.warning("%s failed (%s), using the previous value", key, error)
            return entry[1]
        raise
    _cache[key] = (time.time(), value)
    return value


def plain(text):
    """Lowercase without accents, keeping punctuation and symbols (math needs the + and the commas)."""
    text = unicodedata.normalize("NFKD", text.lower())
    return "".join(c for c in text if not unicodedata.combining(c))


def now_label():
    now = datetime.now(COLOMBIA)
    return f"{now.hour % 12 or 12}:{now.minute:02d} {'am' if now.hour < 12 else 'pm'}"


def with_dots(n):
    """95420 -> '95.420', the Spanish way to group thousands."""
    return f"{n:,.0f}".replace(",", ".")


def number_text(x):
    """A number as it is read aloud: 42, 3,5, 1.250."""
    if abs(x - round(x)) < 1e-9:
        return with_dots(round(x))
    return f"{x:,.2f}".replace(",", "X").replace(".", ",").replace("X", ".").rstrip("0").rstrip(",")


def first_sentences(text, limit):
    """As many whole sentences as fit in `limit` characters (at least one, cut if it is very long)."""
    sentences = re.split(r"(?<=[.!?])\s+", text.strip())
    out = ""
    for sentence in sentences:
        if out and len(out) + len(sentence) + 1 > limit:
            break
        out = f"{out} {sentence}".strip()
    if len(out) > limit:
        out = out[: limit - 1].rsplit(" ", 1)[0] + "…"
    return out


class OriginalPattern:
    """Matches against the accent-stripped original sentence, so symbols like + and % are still there."""

    def __init__(self, pattern):
        self.regex = re.compile(pattern)

    def search(self, _cleaned):
        return self.regex.search(plain(ORIGINAL["text"]))


# --------------------------------------------------------------------- crypto and dollar
def crypto_data():
    return cached("crypto", 60, lambda: requests.get(
        "https://api.coingecko.com/api/v3/simple/price",
        params={"ids": "bitcoin,ethereum,solana", "vs_currencies": "usd,cop", "include_24hr_change": "true"},
        headers=HEADERS, timeout=8).json())


def usd_text(value):
    return with_dots(value) if value >= 1000 else f"{value:,.2f}".replace(".", ",")


def cop_short(value):
    if value >= 1e6:
        return f"{value / 1e6:.1f}".replace(".", ",").replace(",0", "") + " millones COP"
    return with_dots(value) + " COP"


def trend_of(change):
    return "up" if change > 0.05 else "down" if change < -0.05 else "flat"


def rows_crypto():
    data = crypto_data()
    rows = []
    for key, label in (("bitcoin", "Bitcoin"), ("ethereum", "Ethereum"), ("solana", "Solana")):
        coin = data[key]
        change = coin["usd_24h_change"]
        rows.append({"label": label, "note": cop_short(coin["cop"]), "value": f"US${usd_text(coin['usd'])}",
                     "change": f"{change:+.1f}%".replace(".", ","), "trend": trend_of(change)})
    return {"type": "rows", "title": "Criptomonedas", "rows": rows, "footer": f"CoinGecko · {now_label()}"}


def skill_crypto(_text):
    try:
        data = crypto_data()
    except Exception as error:
        log.warning("Crypto prices failed: %s", error)
        return "No pude consultar los precios en este momento.", None

    def phrase(name, key):
        coin = data[key]
        change = coin["usd_24h_change"]
        move = "sin cambios" if abs(change) < 0.05 else (
            f"{abs(change):.1f}".replace(".", ",") + (" por ciento más" if change > 0 else " por ciento menos"))
        return f"{name}, {usd_text(coin['usd'])} dólares, {move}"

    spoken = (f"El {phrase('bitcoin', 'bitcoin')} que ayer. El {phrase('ethereum', 'ethereum')}. "
              f"Y {phrase('solana', 'solana')}.")
    return spoken, "rows?kind=crypto"


def rates():
    return cached("rates", 600, lambda: requests.get("https://open.er-api.com/v6/latest/USD", timeout=8).json()["rates"])


def rows_dollar():
    r = rates()
    cop = r["COP"]
    rows = [
        {"label": "Dólar", "note": "pesos por dólar", "value": f"${with_dots(cop)}", "change": "", "trend": "flat"},
        {"label": "Euro", "note": "pesos por euro", "value": f"${with_dots(cop / r['EUR'])}", "change": "", "trend": "flat"},
    ]
    return {"type": "rows", "title": "Dólar y euro", "rows": rows, "footer": f"Tasa de mercado · {now_label()}"}


def skill_dollar(_text):
    try:
        r = rates()
    except Exception as error:
        log.warning("Exchange rates failed: %s", error)
        return "No pude consultar el dólar en este momento.", None
    return (f"El dólar está a {with_dots(r['COP'])} pesos y el euro a {with_dots(r['COP'] / r['EUR'])} pesos.",
            "rows?kind=dollar")


# ----------------------------------------------------------------------------------- news
NEWS_LAST = {"type": "headlines", "title": "Noticias", "items": [], "footer": ""}


def fetch_news():
    def fetch():
        xml = requests.get("https://news.google.com/rss", params={"hl": "es-419", "gl": "CO", "ceid": "CO:es-419"},
                           headers=HEADERS, timeout=8).content
        items = []
        for item in ET.fromstring(xml).findall("./channel/item")[:8]:
            title = (item.findtext("title") or "").strip()
            source = (item.findtext("source") or "").strip()
            if source and title.endswith(f" - {source}"):
                title = title[: -len(source) - 3].strip()
            if title:
                items.append({"title": title, "source": source})
        return items

    return cached("news", 300, fetch)


def skill_news(_text):
    try:
        items = fetch_news()
    except Exception as error:
        log.warning("News failed: %s", error)
        return "No pude consultar las noticias en este momento.", None
    if not items:
        return "No encontré noticias en este momento.", None
    NEWS_LAST.update(items=items[:4], footer=f"Google Noticias · {now_label()}")
    spoken = "Estas son las noticias principales de Colombia. "
    for position, item in zip(("Primera", "Segunda", "Tercera"), items[:3]):
        spoken += f"{position}: {item['title'].rstrip('.')}. "
    return first_sentences(spoken, 560), "headlines"


# ----------------------------------------------------------------------------- Wikipedia
ARTICLE_LAST = {"type": "article", "title": "", "text": "", "footer": ""}
WIKI_RE = re.compile(
    r"\b(?:quien (?:fue|es|era|eran|son)|que (?:es|fue|son|significa)|cuentame (?:de|sobre|acerca de)|"
    r"hablame (?:de|sobre|acerca de)|dime (?:algo )?(?:de|sobre|acerca de)|busca(?:r)?|wikipedia)\s+(.+)$")
WIKI_ARTICLES = {"el", "la", "los", "las", "un", "una", "de", "del", "en", "wikipedia", "sobre", "acerca"}


def wiki_lookup(subject):
    """(title, summary) of the best Wikipedia match, or None."""
    api = "https://es.wikipedia.org"
    found = requests.get(f"{api}/w/api.php", params={"action": "opensearch", "search": subject, "limit": 3,
                                                       "namespace": 0, "format": "json"},
                         headers=HEADERS, timeout=8).json()
    for title in found[1]:
        summary = requests.get(f"{api}/api/rest_v1/page/summary/{requests.utils.quote(title.replace(' ', '_'))}",
                               headers=HEADERS, timeout=8).json()
        if summary.get("type") == "standard" and summary.get("extract"):
            return summary.get("title", title), summary["extract"]
    return None


def skill_wikipedia(text):
    match = WIKI_RE.search(text)
    if not match:
        return None
    words = match.group(1).split()
    while words and words[0] in WIKI_ARTICLES:
        words = words[1:]
    while words and words[-1] in WIKI_ARTICLES:
        words = words[:-1]
    subject = " ".join(words)
    if not subject:
        return "¿Sobre qué quieres saber? Por ejemplo: quién fue Simón Bolívar.", None
    try:
        result = wiki_lookup(subject)
    except Exception as error:
        log.warning("Wikipedia failed: %s", error)
        return "No pude consultar Wikipedia en este momento.", None
    if not result:
        return f"No encontré nada sobre {subject} en Wikipedia.", None
    title, extract = result
    ARTICLE_LAST.update(title=title, text=first_sentences(extract, 330), footer="Wikipedia")
    return first_sentences(extract, 380), "article"


# ------------------------------------------------------------------------------------ jokes
JOKES = [
    ("¿Por qué los pájaros vuelan hacia el sur en invierno?", "Porque caminando tardarían mucho."),
    ("¿Qué le dice un cero a otro cero?", "Nada, no valemos nada."),
    ("¿Cuál es el colmo de un electricista?", "Que su mujer se llame Luz y sus hijos le sigan la corriente."),
    ("¿Qué hace una abeja en el gimnasio?", "¡Zum-ba!"),
    ("¿Por qué estaba triste el libro de matemáticas?", "Porque tenía muchos problemas."),
    ("¿Cuál es el colmo de un jardinero?", "Que su novia se llame Rosa y lo deje plantado."),
    ("¿Qué le dice una iguana a su hermana gemela?", "Somos iguanitas."),
    ("¿Qué le dice un techo a otro techo?", "Techo de menos."),
    ("¿Por qué los esqueletos no pelean entre ellos?", "Porque no tienen agallas."),
    ("¿Cuál es el café más peligroso del mundo?", "El ex-preso."),
    ("¿Qué le dice un gusano a otro gusano?", "Voy a dar una vuelta a la manzana."),
    ("¿Cómo se llama el campeón de buceo japonés?", "Tokofondo. ¿Y el subcampeón? Kasitoko."),
    ("¿Por qué las focas del circo miran siempre hacia arriba?", "Porque es donde están los focos."),
    ("¿Cuál es el animal más antiguo?", "La cebra, porque está en blanco y negro."),
    ("¿Qué le dice una pared a otra pared?", "Nos vemos en la esquina."),
    ("¿Qué le dice un semáforo a otro?", "No me mires, que me estoy cambiando."),
    ("¿Por qué el programador se quedó sin amigos?", "Porque siempre andaba buscando errores en los demás."),
    ("¿Qué hace un pez en el cine?", "Ver una película de acuario."),
    ("¿Cuál es el colmo de un matemático?", "Que lo dejen plantado y tenga que hacer cálculos."),
    ("¿Cómo se despiden los químicos?", "Ácido un placer."),
]
_joke_state = {"last": -1}


def skill_joke(_text):
    index = random.choice([i for i in range(len(JOKES)) if i != _joke_state["last"]])
    _joke_state["last"] = index
    setup, punchline = JOKES[index]
    ARTICLE_LAST.update(title="Chiste", text=f"{setup}\n\n{punchline}", footer="")
    return f"{setup} ... {punchline}", "article"


# ------------------------------------------------------------------- math and conversions
NUMBER = r"\d+(?:[.,]\d+)?"
PERCENT_RE = re.compile(rf"({NUMBER})\s*(?:por ?ciento|%)\s*de\s*({NUMBER})")
TOKEN_RE = re.compile(rf"{NUMBER}|multiplicado por|dividido (?:por|entre)|\bmas\b|\bmenos\b|\bpor\b|\bentre\b|[+\-*/×÷]|"
                      r"(?<=\d)\s*x\s*(?=\d)")  # an 'x' only counts as times when it sits between two digits
OPERATORS = {"+": "+", "mas": "+", "-": "-", "menos": "-", "*": "*", "x": "*", "×": "*", "por": "*",
             "multiplicado por": "*", "/": "/", "÷": "/", "entre": "/", "dividido por": "/", "dividido entre": "/"}


def to_number(text):
    return float(text.replace(",", "."))


def evaluate(tokens):
    """+ - * / with the usual precedence. tokens alternate number, operator, number..."""
    values, ops = [tokens[0]], []
    for op, value in zip(tokens[1::2], tokens[2::2]):
        if op in "*/":
            left = values.pop()
            if op == "/" and value == 0:
                raise ZeroDivisionError
            values.append(left * value if op == "*" else left / value)
        else:
            ops.append(op)
            values.append(value)
    result = values[0]
    for op, value in zip(ops, values[1:]):
        result = result + value if op == "+" else result - value
    return result


def skill_math(_text):
    sentence = plain(ORIGINAL["text"])
    percent = PERCENT_RE.search(sentence)
    if percent:
        part, whole = to_number(percent.group(1)), to_number(percent.group(2))
        return f"El {number_text(part)} por ciento de {number_text(whole)} es {number_text(whole * part / 100)}.", None

    tokens = []
    for raw in TOKEN_RE.findall(sentence):
        raw = raw.strip()
        if re.fullmatch(NUMBER, raw):
            if tokens and isinstance(tokens[-1], float):
                tokens[-1] = to_number(raw)  # two numbers in a row: the later one wins
            else:
                tokens.append(to_number(raw))
        elif tokens and isinstance(tokens[-1], float):
            tokens.append(OPERATORS[raw])
    while tokens and isinstance(tokens[-1], str):
        tokens.pop()
    if len(tokens) < 3:
        return "No entendí la cuenta. Dime por ejemplo: cuánto es 25 más 17.", None
    try:
        result = evaluate(tokens)
    except ZeroDivisionError:
        return "No se puede dividir entre cero.", None
    spoken = " ".join(number_text(t) if isinstance(t, float) else {"+": "más", "-": "menos", "*": "por", "/": "entre"}[t]
                      for t in tokens)
    return f"{spoken} es {number_text(result)}.", None


# unit -> (kind, size in the base unit). Temperature and currency are handled separately.
UNITS = {
    "kilometro": ("length", 1000), "kilometros": ("length", 1000), "km": ("length", 1000),
    "metro": ("length", 1), "metros": ("length", 1), "centimetro": ("length", 0.01), "centimetros": ("length", 0.01),
    "milla": ("length", 1609.344), "millas": ("length", 1609.344), "pie": ("length", 0.3048), "pies": ("length", 0.3048),
    "pulgada": ("length", 0.0254), "pulgadas": ("length", 0.0254),
    "kilo": ("mass", 1), "kilos": ("mass", 1), "kilogramo": ("mass", 1), "kilogramos": ("mass", 1),
    "gramo": ("mass", 0.001), "gramos": ("mass", 0.001), "libra": ("mass", 0.45359237), "libras": ("mass", 0.45359237),
    "onza": ("mass", 0.0283495), "onzas": ("mass", 0.0283495),
    "litro": ("volume", 1), "litros": ("volume", 1), "mililitro": ("volume", 0.001), "mililitros": ("volume", 0.001),
    "galon": ("volume", 3.78541), "galones": ("volume", 3.78541),
}
TEMPERATURES = {"celsius": "C", "centigrados": "C", "fahrenheit": "F", "kelvin": "K"}
CURRENCIES = {"dolar": "USD", "dolares": "USD", "euro": "EUR", "euros": "EUR", "peso": "COP", "pesos": "COP"}
ALL_UNITS = "|".join(sorted(list(UNITS) + list(TEMPERATURES) + list(CURRENCIES), key=len, reverse=True))
CONVERT_A = re.compile(rf"({NUMBER})\s*(?:grados\s+)?({ALL_UNITS})\s+(?:a|en|hasta)\s+(?:grados\s+)?({ALL_UNITS})\b")
CONVERT_B = re.compile(rf"cuantos?\s+(?:grados\s+)?({ALL_UNITS})\s+(?:son|es|hay en|equivalen a)\s+({NUMBER})\s*(?:grados\s+)?({ALL_UNITS})\b")
UNIT_NAMES = {"C": "grados Celsius", "F": "grados Fahrenheit", "K": "kelvin", "USD": "dólares", "EUR": "euros",
              "COP": "pesos"}


ACCENTED = {"kilometro": "kilómetro", "kilometros": "kilómetros", "centimetro": "centímetro",
            "centimetros": "centímetros", "galon": "galón", "dolar": "dólar", "dolares": "dólares",
            "mililitro": "mililitro", "km": "kilómetros"}


def say_unit(name):
    return ACCENTED.get(name, name)


def convert(value, source, target):
    if source in TEMPERATURES and target in TEMPERATURES:
        s, t = TEMPERATURES[source], TEMPERATURES[target]
        celsius = value if s == "C" else (value - 32) * 5 / 9 if s == "F" else value - 273.15
        result = celsius if t == "C" else celsius * 9 / 5 + 32 if t == "F" else celsius + 273.15
        return result, UNIT_NAMES[s], UNIT_NAMES[t]
    if source in CURRENCIES and target in CURRENCIES:
        r = rates()
        s, t = CURRENCIES[source], CURRENCIES[target]
        return value / r[s] * r[t] if s != "USD" else value * r[t] if t != "USD" else value, UNIT_NAMES[s], UNIT_NAMES[t]
    if source in UNITS and target in UNITS and UNITS[source][0] == UNITS[target][0]:
        return value * UNITS[source][1] / UNITS[target][1], source, target
    return None


def skill_convert(_text):
    sentence = plain(ORIGINAL["text"])
    match = CONVERT_A.search(sentence)
    if match:
        value, source, target = to_number(match.group(1)), match.group(2), match.group(3)
    else:
        match = CONVERT_B.search(sentence)
        if not match:
            return "No entendí la conversión. Dime por ejemplo: convierte 5 millas a kilómetros.", None
        target, value, source = match.group(1), to_number(match.group(2)), match.group(3)
    try:
        converted = convert(value, source, target)
    except Exception as error:
        log.warning("Conversion failed: %s", error)
        return "No pude consultar la tasa de cambio en este momento.", None
    if not converted:
        return "No sé convertir entre esas dos medidas.", None
    result, source_name, target_name = converted
    return f"{number_text(value)} {say_unit(source_name)} son {number_text(result)} {say_unit(target_name)}.", None


def skill_dice(_text):
    sentence = plain(ORIGINAL["text"])
    between = re.search(rf"entre\s+({NUMBER})\s+y\s+({NUMBER})", sentence)
    if between:
        low, high = sorted((int(to_number(between.group(1))), int(to_number(between.group(2)))))
        return f"Salió el {random.randint(low, high)}.", None
    return f"Salió el {random.randint(1, 6)}.", None


def skill_coin(_text):
    return f"Salió {random.choice(['cara', 'sello'])}.", None


# ------------------------------------------------------------------------- this computer
def gpu_info():
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,utilization.gpu,temperature.gpu,memory.used,memory.total",
             "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=5).stdout.strip()
        name, util, temp, used, total = [part.strip() for part in out.splitlines()[0].split(",")]
        return {"name": name.replace("NVIDIA GeForce ", ""), "util": int(util), "temp": int(temp),
                "used": int(used), "total": int(total)}
    except Exception:
        return None


def pc_status():
    import psutil

    gpu = gpu_info()
    memory = psutil.virtual_memory()
    disk = shutil.disk_usage("C:\\")
    return {
        "cpu": psutil.cpu_percent(interval=0.3),
        "cores": psutil.cpu_count(logical=False) or psutil.cpu_count(),
        "memory_percent": memory.percent,
        "memory_used": memory.used / 1e9,
        "memory_total": memory.total / 1e9,
        "disk_percent": disk.used / disk.total * 100,
        "disk_free": disk.free / 1e9,
        "uptime_hours": (time.time() - psutil.boot_time()) / 3600,
        "gpu": gpu,
    }


def rows_pc():
    s = pc_status()
    rows = [
        {"label": "Procesador", "note": f"{s['cores']} núcleos", "value": f"{s['cpu']:.0f}%", "change": "", "trend": "flat"},
        {"label": "Memoria", "note": f"{s['memory_used']:.0f} de {s['memory_total']:.0f} GB",
         "value": f"{s['memory_percent']:.0f}%", "change": "", "trend": "flat"},
    ]
    if s["gpu"]:
        gpu = s["gpu"]
        rows.append({"label": "Gráfica", "note": f"{gpu['temp']}°C · {gpu['name']}", "value": f"{gpu['util']}%",
                     "change": "", "trend": "flat"})
    rows.append({"label": "Disco C:", "note": f"{s['disk_free']:.0f} GB libres", "value": f"{s['disk_percent']:.0f}%",
                 "change": "", "trend": "flat"})
    hours = s["uptime_hours"]
    uptime = f"{hours:.0f} h" if hours >= 1 else f"{hours * 60:.0f} min"
    return {"type": "rows", "title": "Este computador", "rows": rows, "footer": f"Encendido hace {uptime}"}


def skill_pc(_text):
    try:
        s = pc_status()
    except Exception as error:
        log.warning("PC status failed: %s", error)
        return "No pude leer el estado del computador.", None
    spoken = f"El procesador está al {s['cpu']:.0f} por ciento y la memoria al {s['memory_percent']:.0f} por ciento. "
    if s["gpu"]:
        spoken += f"La tarjeta gráfica está a {s['gpu']['temp']} grados. "
    spoken += f"El disco tiene {s['disk_free']:.0f} gigas libres."
    return spoken, "rows?kind=pc"


# ------------------------------------------------------------------- what the server uses
ROWS_BUILDERS = {"crypto": rows_crypto, "dollar": rows_dollar, "pc": rows_pc}

SCREENS = {
    "rows": lambda args: ROWS_BUILDERS[args.get("kind", "crypto")](),
    "headlines": lambda args: NEWS_LAST,
    "article": lambda args: ARTICLE_LAST,
}

# Order matters: the first match wins. These go before the time and date skills.
SPECIFIC_SKILLS = [
    (OriginalPattern(r"\d\s*(?:por ?ciento|%)\s*de\s*\d|\d(?:[.,]\d+)?\s*(?:[+\-*/×÷x]|mas|menos|por|entre|"
                     r"dividido|multiplicado)\s*\d"), skill_math),
    (OriginalPattern(r"\b(convierte|convertir|pasa)\b.*\d|\bcuantos?\b.*\b(son|es|hay en|equivalen a)\b.*\d|"
                     r"\d+(?:[.,]\d+)?\s*(?:grados )?(?:km|kilometros?|metros?|millas?|pies|pulgadas?|centimetros?|kilos?|"
                     r"kilogramos?|gramos?|libras?|onzas?|litros?|galones?|celsius|fahrenheit|dolares?|euros?)\s+(?:a|en)\b"),
     skill_convert),
    (re.compile(r"\b(dado|dados|numero aleatorio|numero al azar)\b"), skill_dice),
    (re.compile(r"\b(moneda|cara o sello|cara o cruz)\b"), skill_coin),
    (re.compile(r"\b(bitcoin|btc|ethereum|ether|solana|cripto\w*)\b"), skill_crypto),
    (re.compile(r"\b(dolar\w*|euros?|trm)\b|\btasa de cambio\b"), skill_dollar),
    (re.compile(r"\b(noticias?|titulares|que paso hoy|que esta pasando)\b"), skill_news),
    (re.compile(r"\b(chistes?|hazme reir|algo gracioso)\b"), skill_joke),
    (re.compile(r"\b(mi (computador|pc|servidor)|estado del (computador|pc|servidor)|"
                r"como esta (el|mi) (computador|pc|servidor)|uso de (cpu|memoria))\b"), skill_pc),
]

# Broad patterns that must come last, after everything more specific had its chance
GENERIC_SKILLS = [(WIKI_RE, skill_wikipedia)]
