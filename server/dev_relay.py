"""The PC's side of the internet mailbox (the bug-gateway Worker in the company project: F:\\Automation company\\bug-gateway).

Away from home the board cannot reach this server, so it talks to the gateway at https://bug.headlessqa.com instead. This PC never accepts
anything from the internet: it only connects OUT to the gateway, to
  * push the messages it makes for the board (the same ones /dev/events gives at home), and
  * collect the orders the board left (approve, reject, "dile a Claude que...", "qué hizo Claude", a question, a screenshot).
Orders go through the same apply_command() as at home, so every rule there still holds (audit log, read-only questions, one session at a time).
The gateway only passes a short list of orders; screenshots are not among them.

Setup: server/dev_relay.json (ignored by git):  {"url": "https://bug.headlessqa.com", "pc_token": "<64 hex characters>"}
The gateway has the same pc_token as a secret, and server/dev_token.txt (the board's token) as BOARD_TOKEN. See the gateway's README.
"""
import json
import logging
import os
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import dev_bridge

log = logging.getLogger("bug")
CONFIG_FILE = Path(__file__).with_name("dev_relay.json")
POLL_SECONDS = 3         # how often the PC asks the gateway for the board's orders (an order waits at most this long)
MAX_AGE = 300           # a message that could not be delivered for five minutes is dropped, so the board is not told old news
_outbox = []
_outbox_changed = threading.Condition()
_state = {"url": "", "token": "", "ok": False}


def load_config():
    try:
        config = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    url, token = str(config.get("url", "")).rstrip("/"), str(config.get("pc_token", ""))
    if not url.startswith("https://") and not url.startswith("http://127.0.0.1") and not url.startswith("http://localhost"):
        log.warning("Relay: the url must be https:// (ignored)")
        return None
    if len(token) < 32:
        log.warning("Relay: pc_token must have at least 32 characters (ignored)")
        return None
    return url, token


def _call(method, path, body=None, timeout=10):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(_state["url"] + path, data=data, method=method, headers={
        "Content-Type": "application/json", "X-Bug-Auth": dev_bridge.sign(method, path.split("?")[0], key=_state["token"]), "User-Agent": "bug-server"})
    with urllib.request.urlopen(request, timeout=timeout) as reply:
        return json.load(reply)


def push_shot(packet):
    """Leaves the newest thumbnail in the gateway (a raw body: 4 bytes of size, then the pixels)."""
    path = "/relay/shot"
    request = urllib.request.Request(_state["url"] + path, data=packet, method="POST", headers={
        "Content-Type": "application/octet-stream", "X-Bug-Auth": dev_bridge.sign("POST", path, key=_state["token"]), "User-Agent": "bug-server"})
    with urllib.request.urlopen(request, timeout=30) as reply:
        return json.load(reply)


def push(event):
    """Called for every message the server makes (dev_bridge.add_event)."""
    with _outbox_changed:
        _outbox.append(dict(event, queued=time.time()))
        del _outbox[:-50]
        _outbox_changed.notify()


def _send_loop():
    delay = 2
    while True:
        with _outbox_changed:
            while not _outbox:
                _outbox_changed.wait()
            now = time.time()
            batch = [e for e in _outbox if now - e["queued"] <= MAX_AGE][:10]
        if not batch:
            with _outbox_changed:
                _outbox[:] = [e for e in _outbox if time.time() - e["queued"] <= MAX_AGE]
            continue
        try:
            _call("POST", "/relay/events", {"events": [{k: v for k, v in e.items() if k != "queued"} for e in batch]})
            with _outbox_changed:
                del _outbox[:len(batch)]
            _state["ok"], delay = True, 2
        except Exception as error:
            _state["ok"] = False
            log.warning("Relay: could not send (%s)", error)
            time.sleep(delay)
            delay = min(delay * 2, 60)


def _command_loop():
    delay = 2
    while True:
        try:
            # Short polls, not a held connection: a held one keeps the gateway's Durable Object busy all day and would use up the free plan
            answer = _call("GET", "/relay/commands?wait=0", timeout=15)
            _state["ok"], delay = True, 2
            for order in answer.get("commands", []):
                run_order(order)
            time.sleep(POLL_SECONDS)
        except Exception as error:
            _state["ok"] = False
            log.warning("Relay: could not ask for orders (%s)", error)
            time.sleep(delay)
            delay = min(delay * 2, 60)


def run_order(order):
    """One order from the board, through the same code as at home but with the limits of an order that came over the internet."""
    allowed = {k: order[k] for k in ("cmd", "id", "allow", "text") if k in order}
    try:
        body, status = dev_bridge.apply_command(allowed, origin="remote")
        dev_bridge._audit("ok" if status == 200 else "refused", f"relay order {allowed.get('cmd')} -> {status}")
        return body, status
    except Exception:
        log.exception("Relay: the order failed")
        return {"ok": False}, 500


def start():
    """Starts the two threads if server/dev_relay.json exists. Does nothing otherwise (and in tests: BUG_NO_RELAY=1)."""
    if os.getenv("BUG_NO_RELAY"):
        return False
    config = load_config()
    if not config:
        return False
    _state["url"], _state["token"] = config
    dev_bridge.relay_sink = push
    dev_bridge.shot_sink = push_shot
    threading.Thread(target=_send_loop, daemon=True, name="relay-send").start()
    threading.Thread(target=_command_loop, daemon=True, name="relay-orders").start()
    log.info("Relay: connected out to %s", _state["url"])
    return True
