"""The dev bridge, phase 0: the board as a status panel for your development work (see docs/DEV_BRIDGE_PLAN.md).

What it does now: tools on this PC (Claude Code hooks, scripts, tests) post a short message with /dev/notify; the board asks for new
messages with /dev/events (a long poll: the answer comes as soon as there is a message, or empty after a while) and shows and says them.

Security, as planned:
  * every request carries X-Bug-Auth: <unix time>:<HMAC-SHA256 of "<time>:<METHOD>:<path>" with the pairing token as the key>;
    a request more than five minutes off the clock is refused, so a copied request cannot be replayed later;
  * the token is a random value made on first start and kept in server/dev_token.txt (ignored by git); it is given to the board over
    the USB cable (server/tools/pair_board.py or the installer page), never over the network;
  * every request, allowed or refused, is written to server/dev_audit.log;
  * nothing here runs commands. Running things comes in phase 1, only as named actions.
"""
import hashlib
import hmac
import json
import logging
import secrets
import threading
import time
from datetime import datetime
from pathlib import Path

from flask import Blueprint, Response, jsonify, request

log = logging.getLogger("bug")
blueprint = Blueprint("dev", __name__)

TOKEN_FILE = Path(__file__).with_name("dev_token.txt")
AUDIT_FILE = Path(__file__).with_name("dev_audit.log")
MAX_SKEW = 300          # seconds the clocks may differ
MAX_EVENTS = 50
MAX_WAIT = 25           # the longest a long poll waits
LEVELS = {"info", "ok", "warn", "error"}

_events = []            # {"id", "level", "text", "speak", "at"}
_next_id = 1
_changed = threading.Condition()


def token():
    """The pairing token; made on first use."""
    if not TOKEN_FILE.exists():
        TOKEN_FILE.write_text(secrets.token_hex(32), encoding="utf-8")
        log.info("Dev bridge: a pairing token was made in %s", TOKEN_FILE.name)
    return TOKEN_FILE.read_text(encoding="utf-8").strip()


def sign(method, path, now=None, key=None):
    """The X-Bug-Auth value for a request (used by the board, the scripts and the tests)."""
    stamp = str(int(now if now is not None else time.time()))
    mac = hmac.new((key or token()).encode(), f"{stamp}:{method.upper()}:{path}".encode(), hashlib.sha256).hexdigest()
    return f"{stamp}:{mac}"


def _audit(outcome, detail=""):
    try:
        with AUDIT_FILE.open("a", encoding="utf-8") as out:
            out.write(json.dumps({"at": datetime.now().isoformat(timespec="seconds"), "from": request.remote_addr, "method": request.method,
                                  "path": request.path, "outcome": outcome, "detail": detail}, ensure_ascii=False) + "\n")
    except OSError:
        pass


def _authorized():
    header = request.headers.get("X-Bug-Auth", "")
    stamp, _, mac = header.partition(":")
    if not stamp.isdigit() or not mac:
        _audit("refused", "no signature")
        return False
    if abs(time.time() - int(stamp)) > MAX_SKEW:
        _audit("refused", "old or future time")
        return False
    wanted = hmac.new(token().encode(), f"{stamp}:{request.method}:{request.path}".encode(), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(wanted, mac):
        _audit("refused", "bad signature")
        return False
    return True


def add_event(text, level="info", speak=True):
    """Queues a message for the board; returns its number."""
    global _next_id
    level = level if level in LEVELS else "info"
    with _changed:
        event = {"id": _next_id, "level": level, "text": str(text)[:140], "speak": bool(speak), "at": int(time.time())}
        _next_id += 1
        _events.append(event)
        del _events[:-MAX_EVENTS]
        _changed.notify_all()
    return event["id"]


@blueprint.get("/dev/status")
def status():
    if not _authorized():
        return jsonify(error="not paired"), 401
    _audit("ok", "status")
    return jsonify(ok=True, latest=_next_id - 1, time=int(time.time()))


@blueprint.post("/dev/notify")
def notify():
    if not _authorized():
        return jsonify(error="not paired"), 401
    data = request.get_json(silent=True) or {}
    text = (data.get("text") or "").strip()
    if not text:
        _audit("refused", "empty text")
        return jsonify(error="text is required"), 400
    event_id = add_event(text, data.get("level", "info"), data.get("speak", True))
    _audit("ok", f"notify {event_id}")
    return jsonify(ok=True, id=event_id)


@blueprint.get("/dev/events")
def events():
    """Messages after `since`. With `wait`, holds the answer until there is one (at most 25 s) so the board hears about it at once."""
    if not _authorized():
        return jsonify(error="not paired"), 401
    try:
        since = int(request.args.get("since", "0"))
        wait = min(float(request.args.get("wait", "0")), MAX_WAIT)
    except ValueError:
        return jsonify(error="bad numbers"), 400
    deadline = time.time() + wait
    with _changed:
        while True:
            fresh = [e for e in _events if e["id"] > since]
            left = deadline - time.time()
            if fresh or left <= 0:
                break
            _changed.wait(timeout=left)
    # A board that was off while messages came gets only the newest few, not a flood
    fresh = [dict(e, age=int(time.time()) - e["at"]) for e in fresh[-5:]]  # the board ignores old ones when it first connects
    if fresh:
        _audit("ok", f"events to {fresh[-1]['id']}")
    body = json.dumps({"events": fresh, "latest": _next_id - 1}, ensure_ascii=False)
    return Response(body, mimetype="application/json")
