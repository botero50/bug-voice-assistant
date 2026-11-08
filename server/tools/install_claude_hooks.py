"""Makes Claude Code tell the board when it finishes or needs you.

    python server/tools/install_claude_hooks.py            shows what would be added to ~/.claude/settings.json
    python server/tools/install_claude_hooks.py --apply    adds it (a copy of the old file is kept next to it)
    python server/tools/install_claude_hooks.py --remove    takes it out again

Two hooks are added: Stop ("Claude terminó en <project>") and Notification (permission or input needed). Each one runs bug_notify.py,
which stays quiet if the server is off. Hooks of yours that are already there are left alone.
"""
import json
import shutil
import sys
from pathlib import Path

SETTINGS = Path.home() / ".claude" / "settings.json"
SCRIPT = Path(__file__).with_name("bug_notify.py").resolve()
MARK = "bug_notify.py"


def command():
    python = Path(sys.executable).as_posix()
    return f'"{python}" "{SCRIPT.as_posix()}"'


def wanted():
    entry = {"hooks": [{"type": "command", "command": command()}]}
    return {"Stop": [entry], "Notification": [entry]}


def has_ours(groups):
    return any(MARK in h.get("command", "") for g in groups for h in g.get("hooks", []))


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "--show"
    settings = json.loads(SETTINGS.read_text(encoding="utf-8")) if SETTINGS.exists() else {}
    hooks = settings.setdefault("hooks", {})
    if mode == "--show":
        print(json.dumps({"hooks": wanted()}, indent=2))
        print(f"\nRun again with --apply to add this to {SETTINGS}")
        return
    if mode == "--apply":
        for event, groups in wanted().items():
            if not has_ours(hooks.get(event, [])):
                hooks.setdefault(event, []).extend(groups)
    elif mode == "--remove":
        for event in list(hooks):
            hooks[event] = [g for g in hooks[event] if not any(MARK in h.get("command", "") for h in g.get("hooks", []))]
            if not hooks[event]:
                del hooks[event]
    else:
        sys.exit(__doc__)
    if SETTINGS.exists():
        shutil.copy2(SETTINGS, SETTINGS.with_suffix(".json.bak"))
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.write_text(json.dumps(settings, indent=2, ensure_ascii=False), encoding="utf-8")
    print("Done:", "hooks added" if mode == "--apply" else "hooks removed", "in", SETTINGS)


if __name__ == "__main__":
    main()
