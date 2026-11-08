"""The user's own music: the files in a folder on this PC, played in order as one continuous
16 kHz mono stream. The board reconnects after Bug talks, so the playlist remembers the song and
the second it was at, and moves on to the next song by itself when one ends.

Folders: the BUG_MUSIC_DIR environment variable (several separated by ';'), or ~/Music.
"""
import difflib
import logging
import os
import random
import threading

from text_utils import normalize

log = logging.getLogger("bug")

SAMPLE_RATE = 32000  # music: twice the voice rate
EXTENSIONS = {".mp3", ".m4a", ".aac", ".wav", ".flac", ".ogg", ".opus", ".wma"}
CHUNK_BYTES = 4096
BYTES_PER_SECOND = SAMPLE_RATE * 2

_lock = threading.Lock()
_tracks = []          # [{"path", "title", "artist", "key"}]
_tags_cache = {}      # path -> (mtime, title, artist)
_queue = []           # indexes into _tracks, in play order
_position = 0         # where in the queue
_seconds = 0.0        # how far into the current song


def folders():
    raw = os.environ.get("BUG_MUSIC_DIR", "")
    paths = [p for p in raw.split(";") if p.strip()] or [os.path.join(os.path.expanduser("~"), "Music")]
    return paths


def _read_tags(path):
    """Title and artist from the file's tags, or the file name when there are none."""
    mtime = os.path.getmtime(path)
    cached = _tags_cache.get(path)
    if cached and cached[0] == mtime:
        return cached[1], cached[2]
    title, artist = "", ""
    try:
        import av
        with av.open(path) as container:
            meta = {k.lower(): v for k, v in container.metadata.items()}
        title = meta.get("title", "").strip()
        artist = (meta.get("artist") or meta.get("album_artist") or "").strip()
    except Exception as error:
        log.warning("Could not read tags of %s: %s", os.path.basename(path), error.__class__.__name__)
    if not title:
        title = os.path.splitext(os.path.basename(path))[0]
    _tags_cache[path] = (mtime, title, artist)
    return title, artist


def scan():
    """Looks at the folders again (cheap: tags are cached by modification time)."""
    found = []
    for folder in folders():
        for root, _dirs, files in os.walk(folder):
            for name in sorted(files):
                if os.path.splitext(name)[1].lower() in EXTENSIONS:
                    path = os.path.join(root, name)
                    title, artist = _read_tags(path)
                    found.append({"path": path, "title": title, "artist": artist,
                                  "key": normalize(f"{artist} {title} {os.path.basename(root)}")})
    with _lock:
        _tracks[:] = found
    return len(found)


def count():
    return len(_tracks)


def _match(request):
    """Tracks whose artist/title/folder contain every word asked for."""
    words = [w for w in normalize(request).split() if len(w) > 1]
    if not words:
        return []

    def has(word, key):
        # Whisper spells names its own way ("Sakira"), so a close spelling counts too
        return word in key or bool(difflib.get_close_matches(word, key.split(), n=1, cutoff=0.78))

    return [i for i, t in enumerate(_tracks) if all(has(w, t["key"]) for w in words)]


def start(request=""):
    """Starts a new playlist: what was asked for, or everything shuffled. Returns the first track
    or None when nothing matches."""
    global _queue, _position, _seconds
    scan()
    with _lock:
        if request.strip():
            picked = _match(request)
        else:
            picked = list(range(len(_tracks)))
        if not picked:
            return None
        if len(picked) > 1 and not request.strip():
            random.shuffle(picked)
        _queue, _position, _seconds = picked, 0, 0.0
        return _tracks[_queue[0]]


def current():
    with _lock:
        if not _queue:
            return None
        return _tracks[_queue[_position % len(_queue)]]


def step(direction=1):
    """Next (1) or previous (-1) song, from its start."""
    global _position, _seconds
    with _lock:
        if not _queue:
            return None
        _position = (_position + direction) % len(_queue)
        _seconds = 0.0
        return _tracks[_queue[_position]]


def _finished():
    global _position, _seconds
    with _lock:
        _position = (_position + 1) % max(len(_queue), 1)
        _seconds = 0.0


def pcm_chunks(track):
    """The given song from the saved second. One song per connection: the board reconnects when it
    ends and gets the next one, together with that song's title in the response headers. If the
    song cannot be read, the playlist moves on so the board does not retry it forever."""
    try:
        yield from _play(track)
    except GeneratorExit:
        raise
    except Exception as error:
        log.warning("Could not play %s: %s", os.path.basename(track["path"]), error)
        _finished()


def _play(track):
    """One song from the saved second. Moves the playlist on if it plays to its end."""
    global _seconds
    import av

    container = av.open(track["path"])
    try:
        audio = next(s for s in container.streams if s.type == "audio")
        start_at = _seconds
        if start_at > 1:
            try:
                container.seek(int(start_at / audio.time_base), stream=audio)
            except Exception:
                start_at = 0.0
        resampler = av.AudioResampler(format="s16", layout="mono", rate=SAMPLE_RATE)
        pending = bytearray()
        sent = 0
        for packet in container.demux(audio):
            for frame in packet.decode():
                converted = resampler.resample(frame)
                for out in converted if isinstance(converted, list) else [converted]:
                    pending += out.to_ndarray().tobytes()
            while len(pending) >= CHUNK_BYTES:
                chunk = bytes(pending[:CHUNK_BYTES])
                del pending[:CHUNK_BYTES]
                sent += len(chunk)
                _seconds = start_at + sent / BYTES_PER_SECOND
                yield chunk
        if pending:
            yield bytes(pending)
    finally:
        container.close()
    _finished()  # only reached when the song played to its end; a hang-up raises GeneratorExit above
