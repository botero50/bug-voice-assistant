"""Your own lists, by voice: a shopping list and notes. Kept on this PC in lists.json (ignored by git)
and shown on the same list screen as prices and timers.

  "agrega leche y huevos a la lista de compras"   "qué hay en mi lista"   "quita la leche de la lista"
  "anota que debo llamar a Ana"                   "mis notas"             "borra las notas"
"""
import json
import logging
import re
from pathlib import Path

import extras
from text_utils import normalize

log = logging.getLogger("bug")

STATE_FILE = Path(__file__).with_name("lists.json")
LISTS = {"compras": "Lista de compras", "notas": "Notas"}
MAX_ITEMS = 40
ADD_WORDS = {"agrega", "agregar", "agregame", "anade", "anadir", "anademe", "pon", "ponme", "poner", "mete", "apunta",
             "apuntame", "anota", "anotame", "incluye", "por", "favor", "hola", "bug", "a", "al", "en", "la", "mi", "el",
             "un", "una", "unos", "unas"}
LIST_TAIL_RE = re.compile(r"\b(?:a|en|de|para)\s+(?:la|mi|el|mis)?\s*(?:lista|compras|mercado|supermercado)\b.*$")


def _load():
    try:
        data = json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        data = {}
    return {name: list(data.get(name, [])) for name in LISTS}


def _save(data):
    STATE_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def _items_from(sentence):
    """'agrega leche y huevos a la lista' -> ['leche', 'huevos']."""
    words = [w.strip(",.;:!¡¿?") for w in sentence.split()]
    text = " ".join(w for w in words if w)
    # Drop the list name at the end (matched on the accent-free copy; both have the same length)
    plain = extras.plain(text)
    match = LIST_TAIL_RE.search(plain)
    if match:
        text = text[: match.start()].strip()
    parts = [p.strip() for p in re.split(r",|\by\b|\be\b", text) if p.strip()]
    cleaned = []
    for part in parts:
        pieces = part.split()
        while pieces and normalize(pieces[0]) in ADD_WORDS:
            pieces.pop(0)
        while pieces and normalize(pieces[-1]) in ADD_WORDS:
            pieces.pop()
        if pieces:
            cleaned.append(" ".join(pieces)[:40])
    return cleaned


def skill_list_add(_text):
    items = _items_from(extras.ORIGINAL["text"])
    if not items:
        return "¿Qué quieres agregar? Por ejemplo: agrega leche a la lista de compras.", None
    data = _load()
    have = {normalize(i) for i in data["compras"]}
    new = [i for i in items if normalize(i) not in have]
    data["compras"] = (data["compras"] + new)[-MAX_ITEMS:]
    _save(data)
    if not new:
        return "Eso ya estaba en tu lista de compras.", "rows?kind=compras"
    return f"Listo, agregué {', '.join(new)} a tu lista de compras. Ahora tiene {len(data['compras'])}.", "rows?kind=compras"


def skill_list_remove(text):
    plain = extras.plain(extras.ORIGINAL["text"])
    match = re.search(r"\b(?:quita|quitar|saca|sacar|borra|borrar|elimina|eliminar|tacha)\w*\s+(?:la |el |los |las )?(.+?)\s+(?:de|e|y|a)\s+(?:la|mi)\s+lista", plain)
    if not match:
        return "¿Qué quito? Por ejemplo: quita la leche de la lista.", None
    wanted = match.group(1).strip()
    data = _load()
    kept = [i for i in data["compras"] if wanted not in normalize(i)]
    if len(kept) == len(data["compras"]):
        return f"No encontré {wanted} en tu lista.", "rows?kind=compras"
    data["compras"] = kept
    _save(data)
    return f"Listo, quité {wanted} de la lista.", "rows?kind=compras"


def skill_list_clear(text):
    which = "notas" if "nota" in text else "compras"
    data = _load()
    if not data[which]:
        return f"{LISTS[which]} ya estaba vacía.", None
    data[which] = []
    _save(data)
    return f"Listo, borré {'las notas' if which == 'notas' else 'la lista de compras'}.", None


def skill_list_show(text):
    which = "notas" if "nota" in text else "compras"
    items = _load()[which]
    if not items:
        return ("No tienes notas." if which == "notas" else "Tu lista de compras está vacía."), None
    spoken_items = items[-6:]
    intro = f"Tienes {len(items)} " + ("notas" if which == "notas" else "cosas en la lista de compras") if len(items) != 1 else \
        ("Tienes una nota" if which == "notas" else "Tienes una cosa en la lista de compras")
    return f"{intro}: " + ", ".join(spoken_items) + ".", f"rows?kind={which}"


def skill_note_add(_text):
    original = extras.ORIGINAL["text"]
    match = re.search(r"\b(?:que|nota)\b[:,]?\s+(.+)$", original, re.I)
    note = (match.group(1) if match else "").strip(" ,.;:!¡¿?")
    if not note:
        return "¿Qué anoto? Por ejemplo: anota que debo llamar a Ana.", None
    data = _load()
    data["notas"] = (data["notas"] + [note[:60]])[-MAX_ITEMS:]
    _save(data)
    return f"Listo, anoté: {note}.", "rows?kind=notas"


def rows(which):
    items = _load()[which]
    shown = items[-5:][::-1]  # newest first
    rows_ = [{"label": item, "note": "", "value": "", "change": "", "trend": "flat"} for item in shown]
    extra = len(items) - len(shown)
    footer = f"y {extra} más" if extra > 0 else ("Dime: agrega algo a la lista" if which == "compras" else "Dime: anota que...")
    return {"type": "rows", "title": LISTS[which], "rows": rows_, "footer": footer}


extras.ROWS_BUILDERS["compras"] = lambda: rows("compras")
extras.ROWS_BUILDERS["notas"] = lambda: rows("notas")

SKILLS = [
    (re.compile(r"\b(quita|quitar|saca|sacar|borra|borrar|elimina|eliminar|tacha)\w*\b.+\b(de|e|y|a) (la|mi) lista\b"), skill_list_remove),
    (re.compile(r"\b(borra|borrar|elimina|eliminar|vacia|vaciar|limpia|limpiar)\w*\b.*\b(lista|notas)\b"), skill_list_clear),
    # Asking what is on the list comes before adding: "qué hay en mi lista de compras" also has "en mi lista"
    (re.compile(r"\b(que (hay|tengo|dice)|muestra\w*|lee\w*|dime|abre|ver|cual es)\b.*\b(lista|notas?)\b|\bmis notas\b|\bmi lista\b"),
     skill_list_show),
    (re.compile(r"\b(anota\w*|apunta\w*|toma nota|nueva nota|crea una nota)\b.*\bque\b|\b(toma nota|nueva nota)\b"), skill_note_add),
    (re.compile(r"\b(agrega\w*|anade\w*|anadir|pon|ponme|mete|apunta\w*|anota\w*|incluye)\b.*\b(a|en) (la|mi) (lista|compras)\b|"
                r"\b(a|en) (la|mi) lista de compras\b"), skill_list_add),
]
