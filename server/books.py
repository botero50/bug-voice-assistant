"""Book reader: Bug reads books aloud, one page at a time, from files on this PC.

Books are .txt and .epub files in ~/Documents/Libros (or the folders in BUG_BOOKS_DIR, separated by
';'). The place in each book is remembered in reading_state.json, so "sigue leyendo" works tomorrow.
"""
import html
import json
import logging
import os
import re
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

from text_utils import normalize

log = logging.getLogger("bug")

STATE_FILE = Path(__file__).with_name("reading_state.json")
PAGE_CHARS = 330  # what fits the board's article screen, and about 25 seconds of speech
EXTENSIONS = {".txt", ".epub"}

_text_cache = {}  # path -> (mtime, full text)


def folders():
    raw = os.environ.get("BUG_BOOKS_DIR", "")
    docs = os.path.join(os.path.expanduser("~"), "Documents")
    return [p for p in raw.split(";") if p.strip()] or [os.path.join(docs, "Libros")]


def library():
    """[{path, title, key}] of every book found."""
    found = []
    for folder in folders():
        for root, _dirs, files in os.walk(folder):
            for name in sorted(files):
                if os.path.splitext(name)[1].lower() in EXTENSIONS:
                    title = os.path.splitext(name)[0].replace("_", " ").strip()
                    found.append({"path": os.path.join(root, name), "title": title, "key": normalize(title)})
    return found


def _epub_text(path):
    """Plain text of an EPUB: its chapters in reading order, tags removed."""
    with zipfile.ZipFile(path) as book:
        container = ET.fromstring(book.read("META-INF/container.xml"))
        opf_path = next(e.get("full-path") for e in container.iter() if e.tag.endswith("rootfile"))
        opf = ET.fromstring(book.read(opf_path))
        base = opf_path.rsplit("/", 1)[0] + "/" if "/" in opf_path else ""
        manifest = {e.get("id"): e.get("href") for e in opf.iter() if e.tag.endswith("}item") or e.tag == "item"}
        order = [e.get("idref") for e in opf.iter() if e.tag.endswith("itemref")]
        parts = []
        for idref in order:
            href = manifest.get(idref)
            if not href:
                continue
            raw = book.read(base + href.split("#")[0]).decode("utf-8", errors="replace")
            raw = re.sub(r"(?is)<(script|style|head).*?</\1>", " ", raw)
            raw = re.sub(r"(?i)</(p|div|h\d|li|br)\s*>|<br\s*/?>", "\n", raw)
            parts.append(html.unescape(re.sub(r"<[^>]+>", " ", raw)))
    return "\n".join(parts)


def read_text(path):
    mtime = os.path.getmtime(path)
    cached = _text_cache.get(path)
    if cached and cached[0] == mtime:
        return cached[1]
    if path.lower().endswith(".epub"):
        text = _epub_text(path)
    else:
        raw = Path(path).read_bytes()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("latin-1")
    text = re.sub(r"[ \t ]+", " ", text)
    text = re.sub(r"\s*\n\s*", "\n", text).strip()
    _text_cache[path] = (mtime, text)
    return text


def _page_end(text, start):
    """Where the page that begins at `start` ends: a sentence break near PAGE_CHARS."""
    end = min(start + PAGE_CHARS, len(text))
    if end >= len(text):
        return len(text)
    window = text[start:end]
    cut = max(window.rfind(". "), window.rfind("? "), window.rfind("! "), window.rfind("\n"))
    if cut > PAGE_CHARS * 0.4:
        return start + cut + 1
    space = window.rfind(" ")
    return start + (space if space > 0 else len(window))


def _load_state():
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"current": None, "offsets": {}}


def _save_state(state):
    STATE_FILE.write_text(json.dumps(state, ensure_ascii=False), encoding="utf-8")


def current_path():
    """The book being read, if it is still there."""
    path = _load_state().get("current")
    return path if path and os.path.exists(path) else None


def find(request):
    """The book whose title contains the words asked for, or None."""
    words = [w for w in normalize(request).split() if len(w) > 2]
    books = library()
    if not words:
        return None
    for book in books:
        if all(w in book["key"] for w in words):
            return book
    return None


def page(book=None, repeat=False):
    """The page at the saved place (the next unread one), and the place moves on. Returns
    (title, page text, 'Página n de N') or None when nothing can be read. `repeat` re-reads the
    page read last instead. Past the end it starts over."""
    state = _load_state()
    path = book["path"] if book else state.get("current")
    if not path or not os.path.exists(path):
        return None
    text = read_text(path)
    if not text:
        return None
    title = (book or {"title": os.path.splitext(os.path.basename(path))[0].replace("_", " ")})["title"]
    offsets = state["offsets"]
    start = offsets.get(path, 0)
    last = state.get("last", {}).get(path, start)  # start of the page read most recently
    if repeat:
        start = last
    if start >= len(text):
        start = 0
    end = _page_end(text, start)
    piece = text[start:end].strip()
    state["current"] = path
    state["offsets"][path] = end
    state.setdefault("last", {})[path] = start
    _save_state(state)
    pages_total = max(1, round(len(text) / PAGE_CHARS))
    number = min(pages_total, start // PAGE_CHARS + 1)
    return title, piece, f"Página {number} de {pages_total}"
