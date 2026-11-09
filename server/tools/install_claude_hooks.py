"""Makes Claude Code tell the board when it finishes or needs you.

    python server/tools/install_claude_hooks.py            shows what would be added to ~/.claude/settings.json
    python server/tools/install_claude_hooks.py --apply    adds it (a copy of the old file is kept next to it)
    python server/tools/install_claude_hooks.py --remove    takes it out again

Five hooks are added (UserPromptSubmit, Stop, Notification, PermissionRequest, SessionEnd); each runs bug_claude_hook.py, which
stays quiet if the server is off. It lets the board say "Claude terminó", answer "¿cómo va Claude?" and approve or reject a
permission request by touch or voice. Hooks of yours that are already there are left alone.
"""
import json
import shutil
import sys
from pathlib import Path

SETTINGS = Path.home() / ".claude" / "settings.json"
SCRIPT = Path(__file__).with_name("bug_claude_hook.py").resolve()
MARKS = ("bug_notify.py", "bug_claude_hook.py")


def command():
    python = Path(sys.executable).as_posix()
    return f'"{python}" "{SCRIPT.as_posix()}"'


def wanted():
    """Each event runs bug_claude_hook.py; PermissionRequest waits up to a minute for your answer on the board (timeout is in seconds)."""
    def entry(timeout):
        return {"hooks": [{"type": "command", "command": command(), "timeout": timeout}]}
    return {"UserPromptSubmit": [entry(10)], "Stop": [entry(10)], "Notification": [entry(10)],
            "PermissionRequest": [entry(75)], "SessionEnd": [entry(10)]}


def is_ours(group):
    return any(m in h.get("command", "") for h in group.get("hooks", []) for m in MARKS)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--show"
    settings = json.loads(SETTINGS.read_text(encoding="utf-8")) if SETTINGS.exists() else {}
    hooks = settings.setdefault("hooks", {})
    if mode == "--show":
        print(json.dumps({"hooks": wanted()}, indent=2))
        print(f"\nRun again with --apply to add this to {SETTINGS}")
        return
    if mode in ("--apply", "--remove"):
        for event in list(hooks):  # our own entries, including the older bug_notify.py ones, are replaced; yours stay
            hooks[event] = [g for g in hooks[event] if not is_ours(g)]
            if not hooks[event]:
                del hooks[event]
    if mode == "--apply":
        for event, groups in wanted().items():
            hooks.setdefault(event, []).extend(groups)
    else:
        sys.exit(__doc__)
    if SETTINGS.exists():
        shutil.copy2(SETTINGS, SETTINGS.with_suffix(".json.bak"))
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(settings, indent=2, ensure_ascii=False), encoding="utf-8")
    print("Done:", "hooks added" if mode == "--apply" else "hooks removed", "in", SETTINGS)


if __name__ == "__main__":
    main()
