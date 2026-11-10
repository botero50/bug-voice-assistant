"""Guided recording of the owner's voice: the board asks for one phrase at a time and uploads each recording here.
The clips are the training data for the models that run on the board (server/wake/). Kept on this PC only
(server/wake_data/commands, ignored by git)."""
import json
import re
import wave
from datetime import datetime
from pathlib import Path

from flask import Blueprint, jsonify, request

blueprint = Blueprint("train", __name__)
COMMANDS_FILE = Path(__file__).with_name("wake") / "commands.json"
CLIPS = Path(__file__).with_name("wake_data") / "commands"
SAMPLE_RATE = 16000


SESSION_FILE = Path(__file__).with_name("wake_data") / "session.json"
# One "Hola Bug" per prompt, each in a different condition, so a single sitting makes a varied test set (see wake/start_session.py)
SESSION_HINTS = ["voz normal", "bien cerca del aparato", "a un metro", "a tres metros", "bajito", "casi susurrando", "rápido", "despacio",
                 "como cansado", "con la tele puesta", "con música de fondo", "hablando de lado", "desde otra habitación", "con voz fuerte"]


# The hard conditions the wake check struggles with (a raw session, see wake/start_session.py --raw): five of each in turn
SESSION_HINTS_HARD = ["casi susurrando", "con música de fondo (ponla tú)", "con la tele puesta (ponla tú)", "hablando de lado, mirando a otro lado", "voz normal (referencia)"]
RAW_DIR = Path(__file__).with_name("wake_data") / "raw"


def _session():
    """The running recording session, or None: {"id": "wake", "target": 100, "started": "YYYYMMDD_HHMMSS"}."""
    try:
        session = json.loads(SESSION_FILE.read_text(encoding="utf-8"))
        return session if session.get("id") and session.get("started") else None
    except (OSError, ValueError):
        return None


def _session_count(session):
    return sum(1 for p in (CLIPS / session["id"]).glob("*.wav") if p.stem[:15] >= session["started"])


def _counts():
    commands = json.loads(COMMANDS_FILE.read_text(encoding="utf-8"))
    return commands, {c["id"]: len(list((CLIPS / c["id"]).glob("*.wav"))) for c in commands}


@blueprint.get("/screen/train_next")
@blueprint.get("/train/next")
def next_phrase():
    """The phrase with the fewest recordings so far (against its target), so stopping at any time leaves a balanced set."""
    session = _session()
    if session:  # a test-set session: only this phrase, in a new condition each time, until the target is reached
        done = _session_count(session)
        if done >= session["target"]:
            return jsonify(done=True, recorded=done, total=session["target"])
        commands = json.loads(COMMANDS_FILE.read_text(encoding="utf-8"))
        phrases = next(c["text"] for c in commands if c["id"] == session["id"])
        hints = SESSION_HINTS_HARD if session.get("raw") else SESSION_HINTS
        hint = hints[done % len(hints)]
        return jsonify(done=False, id=session["id"], text=f"{phrases[done % len(phrases)]}  ({hint})", have=done, target=session["target"],
                       recorded=done, total=session["target"], raw=bool(session.get("raw")))
    commands, counts = _counts()
    pending = [c for c in commands if counts[c["id"]] < c["target"]]
    done = sum(min(counts[c["id"]], c["target"]) for c in commands)
    total = sum(c["target"] for c in commands)
    if not pending:
        return jsonify(done=True, recorded=done, total=total)
    chosen = min(pending, key=lambda c: counts[c["id"]] / c["target"])
    n = counts[chosen["id"]]
    return jsonify(done=False, id=chosen["id"], text=chosen["text"][n % len(chosen["text"])], have=n,
                   target=chosen["target"], recorded=done, total=total)


@blueprint.post("/train/clip")
def save_clip():
    label = re.sub(r"[^a-z0-9_]", "", request.args.get("label", ""))
    pcm = request.get_data()
    if not label or len(pcm) < 4000:
        return jsonify(error="bad clip"), 400
    if request.args.get("raw") == "1":  # both microphones, 32 kHz, interleaved: for the audio lab (wake/audio_lab.py)
        folder = RAW_DIR / label
        folder.mkdir(parents=True, exist_ok=True)
        with wave.open(str(folder / f"{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.wav"), "wb") as out:
            out.setnchannels(2)
            out.setsampwidth(2)
            out.setframerate(32000)
            out.writeframes(pcm[: len(pcm) // 4 * 4])
        return jsonify(ok=True)
    folder = CLIPS / label
    folder.mkdir(parents=True, exist_ok=True)
    with wave.open(str(folder / f"{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.wav"), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SAMPLE_RATE)
        out.writeframes(pcm)
    return jsonify(ok=True)
