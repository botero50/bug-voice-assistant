"""Where the user is: found from their internet connection, or the city they chose by voice.

Auto-detect asks ip-api.com which city the connection's public address belongs to (the PC and the
board share the same home network, so it is the same place). A city chosen by voice is saved in
user_settings.json next to this file and wins over auto-detect until the user asks to go back.
"""
import json
import logging
import os
import time
from pathlib import Path

import requests

log = logging.getLogger("bug")

SETTINGS_FILE = Path(__file__).with_name("user_settings.json")
DEFAULT_CITY = os.getenv("BUG_CITY", "Bogotá")  # only used when detection fails and nothing is saved
AUTO_CACHE_SECONDS = 24 * 3600

_auto = {"time": 0.0, "place": None}


def _load():
    try:
        return json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _save(data):
    SETTINGS_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def get_json(url, params=None, timeout=8, attempts=3):
    """A web service sometimes fails once and answers the second time, so try a few times."""
    last_error = None
    for attempt in range(attempts):
        try:
            reply = requests.get(url, params=params, timeout=timeout)
            reply.raise_for_status()
            return reply.json()
        except (requests.RequestException, ValueError) as error:
            last_error = error
            time.sleep(0.6 * (attempt + 1))
    raise last_error


def geocode(name):
    """Looks a city up by name. Returns {'name', 'lat', 'lon', 'country'} or None."""
    for extra in ({"countryCode": "CO"}, {}):  # prefer a city in Colombia
        reply = get_json(
            "https://geocoding-api.open-meteo.com/v1/search",
            {"name": name, "count": 1, "language": "es", "format": "json", **extra},
        )
        if reply.get("results"):
            found = reply["results"][0]
            return {"name": found["name"], "lat": found["latitude"], "lon": found["longitude"],
                    "country": found.get("country_code", "")}
    return None


def detect_place():
    if _auto["place"] and time.time() - _auto["time"] < AUTO_CACHE_SECONDS:
        return _auto["place"]
    try:
        reply = get_json("http://ip-api.com/json/", {"fields": "status,city,countryCode,lat,lon", "lang": "es"}, timeout=6)
    except Exception:
        if _auto["place"]:
            return _auto["place"]  # the last answer is better than none
        raise
    if reply.get("status") != "success" or not reply.get("city"):
        if _auto["place"]:
            return _auto["place"]
        raise LookupError("could not detect the city from the connection")
    place = {"name": reply["city"], "lat": reply["lat"], "lon": reply["lon"], "country": reply["countryCode"]}
    _auto.update(time=time.time(), place=place)
    return place


def current_place():
    """Returns (place, source) where source is 'saved', 'auto' or 'default'."""
    saved = _load().get("place")
    if saved:
        return saved, "saved"
    try:
        return detect_place(), "auto"
    except Exception as error:
        log.warning("City detection failed: %s", error)
    place = geocode(DEFAULT_CITY)
    if not place:
        raise LookupError("no city available")
    return place, "default"


def save_place(place):
    data = _load()
    data["place"] = place
    _save(data)


def forget_place():
    data = _load()
    data.pop("place", None)
    _save(data)
