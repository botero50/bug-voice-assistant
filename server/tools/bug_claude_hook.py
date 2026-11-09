"""Claude Code hook for the board: reports what Claude is doing and lets you approve a permission from the board.

Installed for these events by install_claude_hooks.py: UserPromptSubmit, Stop, Notification, PermissionRequest, SessionEnd.
  * PermissionRequest: asks the PC server, which asks the board ("Claude pide permiso ... ¿Aprobar?"); the answer you give on the board
    (touch or voice) is printed as Claude Code's decision. With no answer in ~55 s, or if the server or board is off, nothing is
    printed and Claude Code asks you in the terminal as usual.
  * The other events only update the state the board can speak ("¿cómo va Claude?") and say "Claude terminó" or "necesita tu permiso".
It never fails loudly: a hook must not get in the way of Claude Code.
"""
import json
import os
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import dev_bridge  # noqa: E402
from bug_notify import from_hook  # noqa: E402

SERVER = os.getenv("BUG_SERVER", "http://127.0.0.1:5000")


def report(payload, timeout):
    path = "/dev/hook"
    event = payload.get("hook_event_name", "")
    body = {"payload": payload}
    if event in ("Stop", "Notification"):
        body["level"], body["text"] = from_hook(payload)
    request = urllib.request.Request(SERVER + path, data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json", "X-Bug-Auth": dev_bridge.sign("POST", path)})
    with urllib.request.urlopen(request, timeout=timeout) as reply:
        return json.load(reply)


def decision_output(decision):
    """What Claude Code expects from a PermissionRequest hook, or None to leave the question to the terminal."""
    if decision == "allow":
        return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": {"behavior": "allow"}}}
    if decision == "deny":
        return {"hookSpecificOutput": {"hookEventName": "PermissionRequest",
                                       "decision": {"behavior": "deny", "message": "Rechazado desde la pantalla de Bug"}}}
    return None


def main():
    if os.getenv("BUG_VOICE_ASK"):  # a read-only question started by voice: not a session of yours, and nothing to announce
        return 0
    try:
        payload = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0
    asks = payload.get("hook_event_name") == "PermissionRequest"
    try:
        answer = report(payload, dev_bridge.APPROVAL_WAIT + 8 if asks else 4)
    except Exception as error:  # the server is off, or not paired yet
        print(f"bug_claude_hook: {error}", file=sys.stderr)
        return 0
    if asks:
        output = decision_output(answer.get("decision"))
        if output:
            print(json.dumps(output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
