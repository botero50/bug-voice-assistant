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


def _counts():
    commands = json.loads(COMMANDS_FILE.read_text(encoding="utf-8"))
    return commands, {c["id"]: len(list((CLIPS / c["id"]).glob("*.wav"))) for c in commands}


@blueprint.get("/screen/train_next")
@blueprint.get("/train/next")
def next_phrase():
    """The phrase with the fewest recordings so far (against its target), so stopping at any time leaves a balanced set."""
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
    folder = CLIPS / label
    folder.mkdir(parents=True, exist_ok=True)
    with wave.open(str(folder / f"{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.wav"), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SAMPLE_RATE)
        out.writeframes(pcm)
    return jsonify(ok=True)
