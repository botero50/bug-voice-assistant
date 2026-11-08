"""Tells the board something: a message from this PC to the board's screen and voice.

Use it from a script:        python server/tools/bug_notify.py --level ok "Las pruebas de humo pasaron"
Or as a Claude Code hook:    the hook's JSON arrives on stdin (see install_claude_hooks.py): Stop becomes "Claude terminó",
                             Notification becomes a short Spanish line.
It never fails loudly: if the server is off, it exits quietly, so it can sit in a hook without getting in the way.
"""
import argparse
import json
import os
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_bridge  # noqa: E402  (the same signing code the server checks with)

SERVER = os.getenv("BUG_SERVER", "http://127.0.0.1:5000")


def from_hook(data):
    """(level, text) for the JSON Claude Code sends to a hook."""
    event = data.get("hook_event_name", "")
    project = Path(data.get("cwd", "")).name or "tu proyecto"
    if event == "Stop":
        return "ok", f"Claude terminó en {project}"
    message = (data.get("message") or "").lower()
    if "permission" in message:
        return "warn", f"Claude necesita tu permiso en {project}"
    if "waiting" in message or "input" in message:
        return "warn", f"Claude espera tu respuesta en {project}"
    return "info", data.get("message") or "Aviso de Claude"


def send(text, level="info", speak=True):
    path = "/dev/notify"
    body = json.dumps({"text": text, "level": level, "speak": speak}).encode()
    request = urllib.request.Request(SERVER + path, data=body, method="POST",
                                     headers={"Content-Type": "application/json", "X-Bug-Auth": dev_bridge.sign("POST", path)})
    with urllib.request.urlopen(request, timeout=4) as reply:
        return json.load(reply)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("text", nargs="?", help="what to say; without it, a Claude Code hook's JSON is read from stdin")
    parser.add_argument("--level", default="info", choices=sorted(dev_bridge.LEVELS))
    parser.add_argument("--quiet", action="store_true", help="show it on the screen only, do not say it")
    args = parser.parse_args()
    if args.text:
        level, text = args.level, args.text
    else:
        try:
            level, text = from_hook(json.load(sys.stdin))
        except (ValueError, OSError):
            return 0
    try:
        send(text, level, not args.quiet)
    except Exception as error:  # the server is off, or not paired yet: a hook must stay silent
        print(f"bug_notify: {error}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
