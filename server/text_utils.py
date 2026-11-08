"""Small text helpers shared by the server modules."""
import re
import unicodedata


def normalize(text):
    """Lowercase, drop accents and punctuation: 'Qué hora es?' -> 'que hora es'."""
    text = unicodedata.normalize("NFKD", text.lower())
    text = "".join(c for c in text if not unicodedata.combining(c))
    return " ".join(re.sub(r"[^a-z0-9 ]+", " ", text).split())
