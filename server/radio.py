"""Internet radio: finds stations in the Radio Browser directory and turns a stream into audio
the board can play (16 kHz, 16-bit mono PCM).

Radio Browser (radio-browser.info) is a free community directory of stations. No account needed.
"""
import logging

import requests

from text_utils import normalize

log = logging.getLogger("bug")

MIRRORS = ["all.api.radio-browser.info", "de1.api.radio-browser.info", "de2.api.radio-browser.info",
           "fi1.api.radio-browser.info", "nl1.api.radio-browser.info"]
_working_host = MIRRORS[0]
USER_AGENT = "BugVoiceAssistant/0.1"
SAMPLE_RATE = 32000  # music: twice the voice rate
PLAYABLE_CODECS = {"MP3", "AAC", "AAC+", "OGG", "FLAC"}

# What people say -> the tag used in the directory
GENRES = {
    "salsa": "salsa", "vallenato": "vallenato", "reggaeton": "reggaeton", "rock": "rock", "pop": "pop",
    "baladas": "balada", "balada": "balada", "clasica": "classical", "jazz": "jazz",
    "electronica": "electronic", "merengue": "merengue", "bachata": "bachata", "cumbia": "cumbia",
    "tango": "tango", "metal": "metal", "hip hop": "hip hop", "rap": "rap", "boleros": "bolero",
    "bolero": "bolero", "tropical": "tropical", "urbana": "urban", "noticias": "news",
    "deportes": "sports", "ranchera": "ranchera", "mariachi": "mariachi", "popular": "popular",
}

STATIONS = {}  # id -> station, so the stream endpoint can find the address later


def _get(path, params=None):
    """The directory runs on several mirror servers and any one of them can be down. Try each."""
    global _working_host
    hosts = [_working_host] + [h for h in MIRRORS if h != _working_host]
    last_error = None
    for host in hosts:
        try:
            reply = requests.get(f"https://{host}/json/{path}", params=params,
                                 headers={"User-Agent": USER_AGENT}, timeout=8)
            reply.raise_for_status()
            _working_host = host
            return reply.json()
        except requests.RequestException as error:
            last_error = error
            log.warning("Radio directory %s failed: %s", host, error.__class__.__name__)
    raise last_error


def _search(**params):
    base = {"hidebroken": "true", "order": "clickcount", "reverse": "true", "limit": 25}
    return _get("stations/search", {**base, **params})


def _clean(raw):
    return {
        "id": raw["stationuuid"],
        "name": raw["name"].strip(),
        "url": raw.get("url_resolved") or raw.get("url") or "",
        "codec": (raw.get("codec") or "").upper(),
        "tags": [t for t in (raw.get("tags") or "").split(",") if t][:3],
        "country": raw.get("country") or "",
        "state": raw.get("state") or "",
    }


def find(request, country, city, original=""):
    """Stations for what the user asked: a name ('tropicana'), a genre ('salsa') or nothing at all
    (then the most popular stations of the user's city). `original` keeps the accents Whisper wrote,
    which the directory's name search needs ('Olímpica')."""
    request = normalize(request)
    words = f" {request} "
    tag = next((t for spoken, t in GENRES.items() if f" {spoken} " in words), None)

    if tag:
        results = _search(tag=tag, countrycode=country)
    elif request:
        results = (_search(name=original or request, countrycode=country)
                   or _search(name=request, countrycode=country)
                   or _search(tag=request, countrycode=country)
                   or _search(name=original or request))
    else:
        results = _search(name=city, countrycode=country) or _search(countrycode=country)

    stations, seen = [], set()
    for raw in results:
        station = _clean(raw)
        key = normalize(station["name"])
        if station["codec"] not in PLAYABLE_CODECS or not station["url"] or key in seen:
            continue
        seen.add(key)
        stations.append(station)

    # Stations from the user's own city first; the directory's popularity order stays within each group
    city_key = normalize(city)
    stations.sort(key=lambda s: 0 if city_key and (city_key in normalize(s["name"]) or city_key in normalize(s["state"])) else 1)
    for station in stations:
        STATIONS[station["id"]] = station
    return stations


def station_by_id(station_id):
    if station_id in STATIONS:
        return STATIONS[station_id]
    found = _get(f"stations/byuuid/{station_id}")
    if not found:
        return None
    STATIONS[station_id] = _clean(found[0])
    return STATIONS[station_id]


def pcm_chunks(url, chunk_bytes=4096):
    """Opens the stream and yields 16 kHz, 16-bit mono PCM until the caller stops reading."""
    import av

    container = av.open(url, timeout=10, options={"user_agent": USER_AGENT})
    try:
        audio = next(s for s in container.streams if s.type == "audio")
        resampler = av.AudioResampler(format="s16", layout="mono", rate=SAMPLE_RATE)
        pending = bytearray()
        for packet in container.demux(audio):
            for frame in packet.decode():
                converted = resampler.resample(frame)
                for out in converted if isinstance(converted, list) else [converted]:
                    pending += out.to_ndarray().tobytes()
            while len(pending) >= chunk_bytes:
                yield bytes(pending[:chunk_bytes])
                del pending[:chunk_bytes]
    finally:
        container.close()
