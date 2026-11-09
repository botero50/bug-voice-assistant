"""The dev bridge, phase 0: the board as a status panel for your development work (see docs/DEV_BRIDGE_PLAN.md).

What it does now: tools on this PC (Claude Code hooks, scripts, tests) post a short message with /dev/notify; the board asks for new
messages with /dev/events (a long poll: the answer comes as soon as there is a message, or empty after a while) and shows and says them.

Security, as planned:
  * every request carries X-Bug-Auth: <unix time>:<HMAC-SHA256 of "<time>:<METHOD>:<path>" with the pairing token as the key>;
    a request more than five minutes off the clock is refused, so a copied request cannot be replayed later;
  * the token is a random value made on first start and kept in server/dev_token.txt (ignored by git); it is given to the board over
    the USB cable (server/tools/pair_board.py or the installer page), never over the network;
  * every request, allowed or refused, is written to server/dev_audit.log;
  * nothing here runs a command chosen by the board: the only things that start Claude are the owner's spoken orders, with the permissions set in dev_claude.json.
"""
import hashlib
import hmac
import json
import logging
import os
import secrets
import threading
import time
from datetime import datetime
from pathlib import Path

from flask import Blueprint, Response, has_request_context, jsonify, request

log = logging.getLogger("bug")
blueprint = Blueprint("dev", __name__)

TOKEN_FILE = Path(__file__).with_name("dev_token.txt")
AUDIT_FILE = Path(__file__).with_name("dev_audit.log")
MAX_SKEW = 300          # seconds the clocks may differ
MAX_EVENTS = 50
MAX_WAIT = 25           # the longest a long poll waits
LEVELS = {"info", "ok", "warn", "error"}

shot_sink = None        # set by dev_relay.start(): called with the thumbnail packet
relay_sink = None       # set by dev_relay.start(): called with every new event
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
        who = ({"from": request.remote_addr, "method": request.method, "path": request.path}
               if has_request_context() else {"from": "server", "method": "-", "path": "background"})
        with AUDIT_FILE.open("a", encoding="utf-8") as out:
            out.write(json.dumps({"at": datetime.now().isoformat(timespec="seconds"), **who, "outcome": outcome, "detail": detail},
                                 ensure_ascii=False) + "\n")
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


def add_event(text, level="info", speak=True, ask=0, options=None, full="", shot=False):
    """Queues a message for the board; returns its number. `ask` is the number of an approval the board should offer (0: none)."""
    global _next_id
    level = level if level in LEVELS else "info"
    with _changed:
        event = {"id": _next_id, "level": level, "text": str(text)[:300], "full": str(full)[:1000], "speak": bool(speak), "at": int(time.time()), "ask": int(ask),
                 "options": [str(o)[:120] for o in (options or [])[:4]], "shot": bool(shot)}
        _next_id += 1
        _events.append(event)
        if relay_sink:
            relay_sink(event)  # also leave it in the internet mailbox (dev_relay.py)
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
    options = data.get("options") if isinstance(data.get("options"), list) else None  # 2 to 4 choices make the board show buttons
    event_id = add_event(text, data.get("level", "info"), data.get("speak", True), options=options, full=data.get("full", ""))
    _audit("ok", f"notify {event_id}")
    return jsonify(ok=True, id=event_id)


# ----------------------------------------------------------------------------------------- Claude Code sessions (phase 1)
# Claude Code hooks (server/tools/bug_claude_hook.py) report what each session is doing, and ask here when Claude wants a permission.
# The board answers with /dev/command. Nothing here runs a command of its own: the only thing that can start Claude is the owner's
# spoken "dile a Claude que ...", and it only resumes an idle session of the owner's, with the permissions already set in Claude Code.
APPROVAL_WAIT = 55      # seconds the hook waits for the board before leaving the question to the terminal
_sessions = {}          # session_id -> {"cwd", "project", "state": working|waiting|idle, "since"}
_approvals = {}         # id -> {"id", "text", "decision": None|"allow"|"deny", "at", "session"}
_next_approval = 1
_decided = threading.Condition()


def _project(cwd):
    return Path(cwd or "").name or "tu proyecto"


def _set_state(session_id, cwd, state, typed=False):
    """`typed`: you just wrote to this session. The session you wrote to last is the one Bug's orders go to."""
    if not session_id:
        return
    old = _sessions.get(session_id, {})
    cwd = cwd or old.get("cwd", "")
    _sessions[session_id] = {"cwd": cwd, "project": _project(cwd), "state": state, "since": int(time.time()),
                             "active": int(time.time()) if typed else old.get("active", 0)}


def describe_request(data):
    """A short Spanish line for what Claude wants to do, from a PermissionRequest hook payload."""
    tool = data.get("tool_name", "una herramienta")
    args = data.get("tool_input") or {}
    if tool == "Bash":
        return "ejecutar: " + " ".join(str(args.get("command", "")).split())[:80]
    target = args.get("file_path") or args.get("path") or args.get("url") or ""
    if tool in ("Edit", "MultiEdit", "Write", "NotebookEdit"):
        return "modificar " + (Path(str(target)).name or "un archivo")
    if tool == "WebFetch":
        return "abrir " + str(target)[:70]
    return f"usar {tool}"


def status_sentence():
    """What the Claude sessions are doing, in one spoken sentence."""
    now = time.time()
    states = {"working": "está trabajando", "waiting": "espera tu permiso", "idle": "está esperando tus órdenes"}
    parts = []
    for info in sorted(_sessions.values(), key=lambda s: -s["since"]):
        minutes = int((now - info["since"]) // 60)
        since = f" desde hace {minutes} minutos" if minutes >= 1 else ""
        parts.append(f"en {info['project']} Claude {states.get(info['state'], info['state'])}{since}")
    return ("; ".join(parts) + ".").capitalize() if parts else "No veo ninguna sesión de Claude abierta."


@blueprint.post("/dev/hook")
def hook():
    """A Claude Code hook reports an event (JSON: {"payload": <the hook's stdin>, "text", "level"}). For PermissionRequest this waits
    for the board's answer and returns {"decision": "allow"|"deny"|"none"}."""
    global _next_approval
    if not _authorized():
        return jsonify(error="not paired"), 401
    data = request.get_json(silent=True) or {}
    payload = data.get("payload") or {}
    event, session_id, cwd = payload.get("hook_event_name", ""), payload.get("session_id", ""), payload.get("cwd", "")
    if event == "UserPromptSubmit":
        _set_state(session_id, cwd, "working", typed=True)
    elif event == "SessionEnd":
        # Some apps close the session process after every turn, so an ended session is still one that can be resumed
        _set_state(session_id, cwd, "idle")
    elif event == "Stop":
        _set_state(session_id, cwd, "idle")
    elif event == "Notification" and data.get("text"):
        add_event(data["text"], data.get("level", "warn"))
    if event == "Stop" and data.get("text"):
        add_event(data["text"], data.get("level", "ok"))
    if event != "PermissionRequest":
        _audit("ok", f"hook {event}")
        return jsonify(ok=True)
    with _decided:
        approval_id = _next_approval
        _next_approval += 1
        tool_input = payload.get("tool_input") or {}
        _approvals[approval_id] = {"id": approval_id, "decision": None, "at": time.time(), "session": session_id, "tool": payload.get("tool_name", ""),
                                   "path": str(tool_input.get("file_path") or tool_input.get("path") or ""), "cwd": cwd}
        for old in [k for k, v in _approvals.items() if time.time() - v["at"] > 600]:
            del _approvals[old]
    _set_state(session_id, cwd, "waiting")
    text = f"Claude pide permiso en {_project(cwd)} para {describe_request(payload)}. ¿Aprobar?"
    add_event(text, "warn", True, ask=approval_id)
    _audit("ok", f"approval {approval_id} asked: {describe_request(payload)}")
    deadline = time.time() + APPROVAL_WAIT
    with _decided:
        while _approvals[approval_id]["decision"] is None and time.time() < deadline:
            _decided.wait(timeout=max(0.1, deadline - time.time()))
        decision = _approvals[approval_id]["decision"] or "none"
        _approvals[approval_id]["decision"] = decision if decision != "none" else "expired"
    _set_state(session_id, cwd, "working")
    _audit("ok", f"approval {approval_id}: {decision}")
    return jsonify(ok=True, decision=decision)


# Orders that arrive through the internet mailbox ("remote") are limited, so a stolen board token cannot run commands on this PC:
#   * "dile a Claude que ..." runs with a fixed profile: it can read and edit files in the project, but Bash, PowerShell and web fetches are denied;
#   * a permission can be approved from outside only when it is a read or an edit of a file inside the session's own folder and not in a protected place.
# Everything else (every shell command, every other tool) is approved only by touching or speaking to the board at home. The profile is in code on purpose:
# a file the run itself could edit (dev_claude.json) must not be able to widen it.
REMOTE_PROFILE = {
    "permission_mode": "acceptEdits",
    "allowed_tools": ["Read", "Glob", "Grep", "Edit", "Write"],
    "disallowed_tools": ["Bash", "PowerShell", "WebFetch", "WebSearch",
                         # the bridge's own code, its hooks and its secrets cannot be changed from outside, whatever the run is told:
                         "Edit(**/server/tools/**)", "Edit(**/server/dev_*)", "Edit(**/server/bug_server.py)", "Edit(**/server/*token*)",
                         "Edit(**/.claude/**)", "Edit(**/.git/**)", "Edit(**/.env)"],
}
REMOTE_READ_TOOLS = {"Read", "Glob", "Grep"}
REMOTE_EDIT_TOOLS = {"Edit", "MultiEdit", "Write", "NotebookEdit"}
PROTECTED_PARTS = {".claude", ".git", ".github", ".husky", ".vscode", ".ssh", "startup", "appdata", "node_modules"}
PROTECTED_NAMES = ("dev_claude.json", "dev_token.txt", "pc_token.txt", "dev_relay.json", "settings.json", "settings.local.json", ".env")


def remote_can_approve(item):
    """True when this permission may be answered through the internet mailbox."""
    tool, path, cwd = item.get("tool", ""), item.get("path", ""), item.get("cwd", "")
    if tool in REMOTE_READ_TOOLS:
        return True
    if tool not in REMOTE_EDIT_TOOLS or not path or not cwd:
        return False
    try:
        target, base = Path(path).resolve(), Path(cwd).resolve()
        target.relative_to(base)  # raises when the file is outside the session's folder
    except (ValueError, OSError):
        return False
    parts = {p.lower() for p in target.relative_to(base).parts}
    return not (parts & PROTECTED_PARTS) and target.name.lower() not in PROTECTED_NAMES


def _decide(approval_id, allow):
    with _decided:
        item = _approvals.get(approval_id)
        if not item or item["decision"] is not None:
            return False
        item["decision"] = "allow" if allow else "deny"
        _decided.notify_all()
    return True


def _claude_executable():
    import os
    import shutil
    return os.getenv("BUG_CLAUDE") or shutil.which("claude")


CLAUDE_FILE = Path(__file__).with_name("dev_claude.json")
RUNS_DIR = Path(__file__).with_name("dev_runs")
CLAUDE_DEFAULTS = {
    "permission_mode": "acceptEdits",   # edits in the project are accepted; other things follow allowed_tools
    "allowed_tools": ["Read", "Glob", "Grep", "Bash(git status*)", "Bash(git diff*)", "Bash(git log*)", "Bash(ls*)"],
    "disallowed_tools": ["Bash(git push*)", "Bash(git commit*)", "Bash(git reset*)", "Bash(rm -rf*)", "PowerShell(Remove-Item*)"],
}


def claude_settings():
    """Permissions for the Claude runs started by voice (server/dev_claude.json overrides the defaults)."""
    config = dict(CLAUDE_DEFAULTS)
    try:
        config.update(json.loads(CLAUDE_FILE.read_text(encoding="utf-8")))
    except (OSError, ValueError):
        pass
    return config


BRIEFING = ("Te controla por voz una placa Bug y el usuario no mira la pantalla. Antes de empezar di en una frase corta, en español, "
            "qué vas a hacer. Al terminar resume en máximo dos frases qué hiciste y qué falta. Si necesitas una decisión del usuario, "
            "detente y termina tu respuesta con dos líneas: 'PREGUNTA: <la pregunta>' y 'OPCIONES: <opción 1> | <opción 2> | <opción 3>' "
            "(entre 2 y 4 opciones de menos de 40 caracteres).")


def claude_command(exe, text, session_id, config=None):
    config = config or claude_settings()
    command = [exe, "-p", text, "--resume", session_id, "--permission-mode", str(config["permission_mode"]),
               "--output-format", "stream-json", "--verbose", "--append-system-prompt", BRIEFING]
    if config.get("allowed_tools"):
        command += ["--allowedTools", *map(str, config["allowed_tools"])]
    if config.get("disallowed_tools"):
        command += ["--disallowedTools", *map(str, config["disallowed_tools"])]
    if config.get("mcp_config"):  # an MCP config file (extra tools) for the voice runs
        command += ["--mcp-config", str(config["mcp_config"])]
    return command


# ----------------------------------------------------------------------------------------- live view of a run
PROGRESS_EVERY = 25     # seconds between progress notices on the board (they are shown, not spoken, except the first plan and the end)
_runs = []              # newest last: {"started", "text", "project", "steps": [{"at", "kind", "text"}], "done", "answer"}


def describe_step(block):
    """A short Spanish line for a tool call of a streamed Claude run."""
    args = block.get("input") or {}
    name = block.get("name", "una herramienta")
    if name in ("Bash", "PowerShell"):
        return "ejecuta " + " ".join(str(args.get("command", "")).split())[:60]
    target = Path(str(args.get("file_path") or args.get("path") or "")).name
    return {"Edit": "edita ", "MultiEdit": "edita ", "Write": "escribe ", "Read": "lee ", "NotebookEdit": "edita "}.get(name, f"usa {name} ") + target


def answer_from_logs():
    """The final answer of the newest saved run in dev_runs/ (so "qué hizo Claude" still works after the server restarts)."""
    try:
        for log_file in sorted(RUNS_DIR.glob("*.log"), reverse=True):
            for line in reversed(log_file.read_text(encoding="utf-8", errors="replace").splitlines()):
                if line.startswith("{") and '"type":"result"' in line.replace(" ", ""):
                    result = json.loads(line).get("result", "")
                    if result:
                        return str(result)
    except (OSError, ValueError):
        pass
    return ""


def clean_answer(text):
    """Claude's final text on one line, without the markdown that sounds odd when spoken."""
    import re
    text = re.sub(r"[*`#_]+", "", str(text))
    text = re.sub(r"(^|\s)-\s+", r"\1", text)
    return " ".join(text.split())


def find_question(answer):
    """The question Claude leaves for you, or "". It is the text after "PREGUNTA:", or, when Claude did not follow that rule,
    the closing sentence(s) if they ask something ("¿...?", "dime con cuál...", "quieres que...")."""
    import re
    if "PREGUNTA:" in answer:
        return answer[answer.index("PREGUNTA:") + 9:].split("OPCIONES:")[0].strip()[:300]
    sentences = [s for s in re.split(r"(?<=[.?!:])\s+", answer) if s.strip()]
    tail = sentences[-2:]
    ask = re.compile(r"\?|\b(dime|quieres que|prefieres|me confirmas|confirma|cuál|con cuál|cuando quieras)\b", re.IGNORECASE)
    if tail and ask.search(tail[-1]):
        joined = " ".join(tail)
        return (joined if len(joined) <= 300 else tail[-1])[-300:].strip()
    return ""


def _split_top(text):
    """Splits on commas and on " o " outside parentheses."""
    import re
    parts, depth, current = [], 0, ""
    for piece in re.split(r"(,\s+|\s+o\s+|[()])", text):
        if piece == "(":
            depth += 1
        elif piece == ")":
            depth -= 1
        if depth == 0 and piece and (piece.startswith(",") or piece.strip() == "o"):
            parts.append(current)
            current = ""
        else:
            current += piece
    parts.append(current)
    return [p.strip() for p in parts if p.strip()]


def find_options(answer, question):
    """2 to 4 short choices to show on the board's screen. Claude's own list after "OPCIONES:" (separated by |) when there is one;
    otherwise the alternatives written inside the question ("¿Sigo con A, con B o con C?")."""
    if "OPCIONES:" in answer:
        listed = [o.strip(" .;") for o in answer.split("OPCIONES:")[1].split("|")]
    else:
        text = question.strip().lstrip("¿").rstrip("?. ")
        if " con " in text.lower():
            text = text[text.lower().index(" con ") + 5:]  # drops "Sigo con"
        listed = [o.strip() for o in _split_top(text)]
        listed = [o[4:] if o.lower().startswith("con ") else o for o in listed]
    listed = [(o[:1].upper() + o[1:]) for o in listed if 2 < len(o) <= 120]
    return listed[:4] if 2 <= len(listed) <= 4 else []


class RunWatcher:
    """Turns the stream-json lines of `claude -p` into progress for the board and the live page."""

    def __init__(self, text, project):
        self.run = {"started": int(time.time()), "text": text, "project": project, "steps": [], "done": False, "answer": ""}
        self.last_progress = 0.0
        self.said_plan = False
        _runs.append(self.run)
        del _runs[:-10]

    def step(self, kind, text):
        self.run["steps"].append({"at": int(time.time()), "kind": kind, "text": text[:300]})
        del self.run["steps"][:-200]

    def feed(self, line):
        try:
            event = json.loads(line)
        except ValueError:
            return
        if event.get("type") == "assistant":
            for block in (event.get("message") or {}).get("content") or []:
                if block.get("type") == "text" and block.get("text", "").strip():
                    said = " ".join(block["text"].split())
                    self.step("say", said)
                    if not self.said_plan and len(said) > 12:
                        self.said_plan = True
                        self.last_progress = time.time()
                        add_event("Claude: " + said, "info", True)
                elif block.get("type") == "tool_use":
                    step = describe_step(block)
                    self.step("tool", step)
                    if time.time() - self.last_progress >= PROGRESS_EVERY:
                        self.last_progress = time.time()
                        add_event("Claude " + step, "info", False)
        elif event.get("type") == "result":
            self.run["answer"] = str(event.get("result", ""))

    def finish(self, ok):
        self.run["done"] = True
        answer = clean_answer(self.run["answer"])
        question = find_question(answer)
        if question:
            add_event("Claude pregunta: " + question, "warn", True, options=find_options(answer, question), full=answer)
        elif answer:
            add_event(("Claude terminó: " + answer)[:140], "ok" if ok else "error", True, full=answer)
        else:
            add_event("Claude terminó" if ok else "Claude terminó con un error", "ok" if ok else "error", True)


def _run_claude(text, session_id, cwd, profile=None):
    """Resumes a session with a new instruction, tells the board what Claude is doing as it works, and keeps the whole stream in dev_runs/."""
    import subprocess
    exe = _claude_executable()
    if not exe:
        add_event("No encuentro Claude Code en el computador", "error")
        return
    watcher = RunWatcher(text, _project(cwd))
    lines = []
    try:
        process = subprocess.Popen(claude_command(exe, text, session_id, profile), cwd=cwd or None, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        timer = threading.Timer(1800, process.kill)
        timer.start()
        try:
            for line in process.stdout:
                lines.append(line)
                watcher.feed(line)
            process.wait()
        finally:
            timer.cancel()
        ok = process.returncode == 0
        if process.returncode and process.returncode < 0:
            add_event("Claude tardó más de treinta minutos", "warn")
        else:
            watcher.finish(ok)
    except OSError as error:
        add_event(f"No pude llamar a Claude: {error}", "error")
    finally:
        watcher.run["done"] = True
        _set_state(session_id, cwd, "idle")
        try:
            RUNS_DIR.mkdir(exist_ok=True)
            (RUNS_DIR / f"{datetime.now():%Y%m%d-%H%M%S}.log").write_text("> " + text + "\n\n" + "".join(lines), encoding="utf-8")
        except OSError:
            pass


ASK_BRIEFING = ("Te hace una pregunta por voz una placa Bug. Es solo de lectura: investiga el proyecto y responde, no modifiques nada. "
                "Responde en español, en máximo cuatro frases claras, empezando por la respuesta.")
ASK_TOOLS = ["Read", "Glob", "Grep", "Bash(git log*)", "Bash(git diff*)", "Bash(git status*)", "Bash(git show*)"]
_ask = {"session": "", "at": 0.0}   # the last read-only conversation, so "y el siguiente" can continue it for a while
ASK_FOLLOW_SECONDS = 1800


def ask_command(exe, text, resume=""):
    config = claude_settings()
    command = [exe, "-p", text, "--output-format", "json", "--permission-mode", "dontAsk", "--append-system-prompt", ASK_BRIEFING,
               "--allowedTools", *ASK_TOOLS]
    if resume:
        command += ["--resume", resume]
    return command


def ask_folder():
    """Where the question is asked: the folder of the session you wrote to last, or ask_cwd in dev_claude.json."""
    newest = sorted(_sessions.values(), key=lambda s: (-s.get("active", 0), -s["since"]))
    return (newest[0]["cwd"] if newest else "") or claude_settings().get("ask_cwd", "")


def _run_ask(text, cwd, resume):
    """One read-only question to Claude (runs in a thread). The answer goes to the board as a short line plus the whole text."""
    import subprocess
    exe = _claude_executable()
    if not exe:
        add_event("No encuentro Claude Code en el computador", "error")
        return
    try:
        # BUG_VOICE_ASK tells the Claude Code hooks to stay out of this run: it must not become "the session you wrote to last"
        done = subprocess.run(ask_command(exe, text, resume), cwd=cwd or None, capture_output=True, text=True, timeout=300,
                              encoding="utf-8", errors="replace", env={**os.environ, "BUG_VOICE_ASK": "1"})
        data = json.loads(done.stdout or "{}")
        answer = clean_answer(data.get("result") or done.stderr or "")
        if data.get("session_id"):
            _ask.update(session=data["session_id"], at=time.time())
    except subprocess.TimeoutExpired:
        add_event("Claude tardó más de cinco minutos en responder", "warn")
        return
    except (OSError, ValueError) as error:
        add_event(f"No pude preguntarle a Claude: {error}", "error")
        return
    if not answer:
        add_event("Claude no dio respuesta", "warn")
        return
    _runs.append({"started": int(time.time()), "text": "pregunta: " + text, "project": _project(cwd), "steps": [], "done": True, "answer": answer})
    del _runs[:-10]
    add_event(answer[:140], "info", True, full=answer)


def start_ask(text):
    text = str(text or "").strip()[:500]
    if not text:
        add_event("¿Qué quieres preguntarle a Claude?", "info")
        return False
    follow = _ask["session"] and time.time() - _ask["at"] < ASK_FOLLOW_SECONDS
    folder = ask_folder()
    _audit("ok", f"ask{' (follow-up)' if follow else ''}: {text}")
    add_event("Preguntándole a Claude…", "info", False)
    threading.Thread(target=_run_ask, args=(text, folder, _ask["session"] if follow else ""), daemon=True).start()
    return True


LIVE_PAGE = """<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Bug · Claude en vivo</title>
<style>:root{color-scheme:light dark}body{font:15px system-ui;max-width:760px;margin:0 auto;padding:16px;background:Canvas;color:CanvasText}
h1{font-size:18px}.run{border:1px solid #8884;border-radius:12px;padding:12px;margin:12px 0}.tool{color:#888}.say{margin:6px 0}.done{color:#22a559}
.working{color:#d98a00}small{color:#888}</style><h1>Claude en vivo</h1><div id=s></div><div id=p></div><div id=r></div>
<script>async function t(){try{const d=await (await fetch('/dev/live.json')).json();
s.innerHTML=d.sessions.map(x=>`<div>${x.project}: <b class=${x.state}>${x.label}</b></div>`).join('')||'<small>Sin sesiones abiertas</small>';
p.innerHTML=(d.shots||[]).map(n=>`<a href=/dev/shot/${n} target=_blank><img src=/dev/shot/${n} style="height:90px;border-radius:8px;margin:4px 4px 0 0"></a>`).join('');
r.innerHTML=d.runs.slice().reverse().map(x=>`<div class=run><b>${x.text}</b> <small>(${x.project}, ${x.done?'terminó':'trabajando…'})</small>`+
x.steps.slice(-40).map(p=>`<div class=${p.kind}>${p.kind=='tool'?'· ':''}${p.text.replace(/</g,'&lt;')}</div>`).join('')+
(x.answer?`<div class=done>${x.answer.replace(/</g,'&lt;')}</div>`:'')+`</div>`).join('')}catch(e){}}t();setInterval(t,2000)</script>"""


# ----------------------------------------------------------------------------------------- eyes: screenshots (phase 3)
# "toma un pantallazo": one monitor of this computer is saved as a PNG in dev_shots/ and shown on the live page (this computer only).
# Privacy: it refuses while a password manager, a bank or a private window is in front, keeps only the last 20 pictures, and the picture
# is never sent to the board or to anyone else; only "Listo" is said. Set the monitor and the words to avoid in dev_claude.json
# ("screenshot_monitor": 1 is the main screen, "screenshot_block": ["keepass", ...]).
SHOTS_DIR = Path(__file__).with_name("dev_shots")
SHOT_BLOCK = ("bitwarden", "1password", "keepass", "lastpass", "dashlane", "password", "contraseña", "banco", "bank", "incógnito", "incognito", "private browsing")


def foreground_title():
    """The title of the window in front (Windows); "" when it cannot be read."""
    try:
        import ctypes
        user32 = ctypes.windll.user32
        handle = user32.GetForegroundWindow()
        buffer = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(handle, buffer, 512)
        return buffer.value
    except Exception:
        return ""


def blocked_by_privacy(title, extra=()):
    lowered = title.lower()
    return next((word for word in (*SHOT_BLOCK, *map(str.lower, extra)) if word in lowered), None)


THUMB_WIDTH, THUMB_MAX_HEIGHT = 320, 240
_thumb = {"w": 0, "h": 0, "data": b""}   # the newest small picture, in the board's own pixel format


def make_thumbnail(rgb, size):
    """A small picture for the board's screen: 320 pixels wide at most, RGB565 with the bytes in the order the board's display library expects
    (big-endian). Returns (width, height, bytes)."""
    import numpy
    from PIL import Image
    picture = Image.frombytes("RGB", size, rgb)
    scale = min(THUMB_WIDTH / picture.width, THUMB_MAX_HEIGHT / picture.height, 1.0)
    width, height = max(1, round(picture.width * scale)), max(1, round(picture.height * scale))
    small = numpy.asarray(picture.resize((width, height), Image.LANCZOS), dtype=numpy.uint16)
    packed = ((small[:, :, 0] >> 3) << 11) | ((small[:, :, 1] >> 2) << 5) | (small[:, :, 2] >> 3)
    return width, height, packed.astype(">u2").tobytes()


def thumbnail_packet():
    """What the board downloads: width and height as two little-endian 16-bit numbers, then the pixels. b"" when there is none."""
    if not _thumb["data"]:
        return b""
    return _thumb["w"].to_bytes(2, "little") + _thumb["h"].to_bytes(2, "little") + _thumb["data"]


def take_screenshot():
    """Saves the screen and returns (path, message). path is None when it was refused or failed."""
    config = claude_settings()
    word = blocked_by_privacy(foreground_title(), config.get("screenshot_block", ()))
    if word:
        return None, "No tomo el pantallazo: hay una ventana privada al frente"
    try:
        import mss
        import mss.tools
        SHOTS_DIR.mkdir(exist_ok=True)
        path = SHOTS_DIR / f"{datetime.now():%Y%m%d-%H%M%S}.png"
        with mss.mss() as grabber:
            monitors = grabber.monitors  # [0] is all screens together; 1 is the main one
            index = int(config.get("screenshot_monitor", 1))
            shot = grabber.grab(monitors[index if 0 < index < len(monitors) else 1])
            mss.tools.to_png(shot.rgb, shot.size, output=str(path))
            try:
                _thumb["w"], _thumb["h"], _thumb["data"] = make_thumbnail(shot.rgb, shot.size)
            except Exception as error:  # the full picture is still saved
                log.warning("Thumbnail failed: %s", error)
                _thumb["data"] = b""
        for old in sorted(SHOTS_DIR.glob("*.png"))[:-20]:
            old.unlink(missing_ok=True)
        return path, "Pantallazo listo, está en la página en vivo"
    except Exception as error:  # no display, mss missing...
        log.warning("Screenshot failed: %s", error)
        return None, "No pude tomar el pantallazo"


@blueprint.get("/dev/shot")
def shot_thumbnail():
    """The small picture for the board (signed like every board request)."""
    if not _authorized():
        return jsonify(error="not paired"), 401
    packet = thumbnail_packet()
    if not packet:
        return jsonify(error="no picture yet"), 404
    _audit("ok", f"thumbnail {len(packet)} bytes")
    return Response(packet, mimetype="application/octet-stream", headers={"Cache-Control": "no-store"})


@blueprint.get("/dev/shot/<name>")
def shot_file(name):
    if request.remote_addr not in ("127.0.0.1", "::1"):
        return "only on this computer", 403
    path = SHOTS_DIR / Path(name).name
    if path.suffix != ".png" or not path.exists():
        return "not found", 404
    return Response(path.read_bytes(), mimetype="image/png")


@blueprint.get("/dev/live")
def live():
    """A page that follows the Claude runs started by voice. Only this computer can open it (http://127.0.0.1:5000/dev/live)."""
    if request.remote_addr not in ("127.0.0.1", "::1"):
        return "only on this computer", 403
    return Response(LIVE_PAGE, mimetype="text/html")


@blueprint.get("/dev/live.json")
def live_json():
    if request.remote_addr not in ("127.0.0.1", "::1"):
        return jsonify(error="only on this computer"), 403
    labels = {"working": "trabajando", "waiting": "espera tu permiso", "idle": "esperando órdenes"}
    sessions = [{"project": v["project"], "state": v["state"], "label": labels.get(v["state"], v["state"])} for v in _sessions.values()]
    shots = sorted((p.name for p in SHOTS_DIR.glob("*.png")), reverse=True)[:6] if SHOTS_DIR.exists() else []
    return jsonify(sessions=sessions, runs=_runs, shots=shots)


@blueprint.post("/dev/command")
def command():
    """The board's orders: {"cmd": "decide", "id": N, "allow": bool} | {"cmd": "say", "text": ...} | {"cmd": "status"} | ..."""
    if not _authorized():
        return jsonify(error="not paired"), 401
    body, status = apply_command(request.get_json(silent=True) or {})
    return jsonify(body), status


def _announce_shot(message):
    """Sends the thumbnail to the internet mailbox first (when it is set up), so a board that is away finds it there, then tells the board."""
    has_picture = bool(_thumb["data"])
    if has_picture and shot_sink:
        try:
            shot_sink(thumbnail_packet())
        except Exception as error:
            log.warning("Could not leave the picture in the mailbox: %s", error)
    add_event("Pantallazo listo" if has_picture else message, "ok", False, shot=has_picture)


def apply_command(data, origin="local"):
    """Runs one order from the board, coming straight from it (/dev/command, origin "local") or through the internet mailbox
    (dev_relay.py, origin "remote", with the limits above). Returns (answer dict, HTTP status)."""
    cmd = data.get("cmd")
    remote = origin == "remote"
    if cmd == "decide":
        item = _approvals.get(int(data.get("id", 0)))
        if remote and data.get("allow") and item and not remote_can_approve(item):
            _audit("refused", f"decide {data.get('id')}: only at home ({item.get('tool')})")
            add_event("Ese permiso solo se aprueba en casa, tocando la placa", "warn", True)
            return {"ok": False}, 200
        ok = _decide(int(data.get("id", 0)), bool(data.get("allow")))
        _audit("ok", f"decide {data.get('id')} allow={bool(data.get('allow'))} applied={ok}")
        if not ok:
            add_event("Esa solicitud ya no está pendiente", "info", False)
        return {"ok": ok}, 200
    if cmd == "ask":  # read-only question to Claude, continued by later questions for a while
        return {"ok": start_ask(data.get("text", ""))}, 200
    if cmd == "shot":
        path, message = take_screenshot()
        _audit("ok" if path else "refused", f"screenshot {path.name if path else message}")
        if not path:
            add_event(message, "warn")
            return {"ok": False}, 200
        threading.Thread(target=_announce_shot, args=(message,), daemon=True).start()
        return {"ok": True}, 200
    if cmd == "last":  # "qué hizo Claude": the board shows the last answer in full
        done = [r for r in _runs if r["answer"]]
        saved = "" if done else answer_from_logs()
        if done or saved:
            answer = clean_answer(done[-1]["answer"] if done else saved)
            add_event(answer[:140], "info", False, full=answer)
        else:
            add_event("Todavía no hay una respuesta de Claude", "info", True)
        _audit("ok", "last answer asked")
        return {"ok": True}, 200
    if cmd == "status":
        _audit("ok", "status asked")
        add_event(status_sentence(), "info")
        return {"ok": True}, 200
    if cmd == "say":
        text = str(data.get("text", "")).strip()[:500] or "continúa"
        if answer_pending_question(text.removeprefix("Elijo: ")):  # Claude Code asked you something through the bug-dev tool
            _audit("ok", "answer to a question from Claude")
            add_event("Respuesta enviada a Claude", "ok", False)
            return {"ok": True}, 200
        # The target is the session you wrote to last; if it is busy the order waits, it does not jump to another one
        newest = sorted(_sessions.items(), key=lambda s: (-s[1].get("active", 0), -s[1]["since"]))
        if not newest or newest[0][1]["state"] != "idle":
            add_event("Tu última sesión de Claude está ocupada, espera a que termine" if newest else "No hay una sesión de Claude lista", "warn")
            _audit("refused", "say: last session is not idle")
            return {"ok": False}, 200
        session_id, info = newest[0]
        _set_state(session_id, info["cwd"], "working")
        _audit("ok", f"say to {info['project']}{' (remote profile)' if remote else ''}: {text}")
        threading.Thread(target=_run_claude, args=(text, session_id, info["cwd"], REMOTE_PROFILE if remote else None), daemon=True).start()
        return {"ok": True}, 200
    _audit("refused", f"unknown command {cmd}")
    return {"error": "unknown command"}, 400


# ----------------------------------------------------------------------------------------- Claude asks YOU something (phase 4: bug-dev MCP)
_questions = {}            # id -> {"answer": None|str, "at"}
_question_changed = threading.Condition()
QUESTION_WAIT = 600


@blueprint.post("/dev/mcp/ask")
def mcp_ask():
    """Used by server/tools/bug_dev_mcp.py: shows a question with options on the board and waits for your tap or voice (up to 10 minutes)."""
    if not _authorized():
        return jsonify(error="not paired"), 401
    data = request.get_json(silent=True) or {}
    question = str(data.get("question", "")).strip()[:300]
    options = [str(o)[:120] for o in (data.get("options") or [])][:4]
    if not question:
        return jsonify(error="question is required"), 400
    with _question_changed:
        qid = max(_questions, default=0) + 1
        _questions[qid] = {"answer": None, "at": time.time()}
    add_event("Claude pregunta: " + question, "warn", True, options=options if len(options) >= 2 else None, full=question)
    _audit("ok", f"mcp question {qid}")
    deadline = time.time() + min(float(data.get("wait", QUESTION_WAIT)), QUESTION_WAIT)
    with _question_changed:
        while _questions[qid]["answer"] is None and time.time() < deadline:
            _question_changed.wait(timeout=max(0.1, deadline - time.time()))
        answer = _questions.pop(qid)["answer"]
    return jsonify(answer=answer, timed_out=answer is None)


def answer_pending_question(text):
    """True when a question from Claude Code was waiting and this text answered it (so "Elijo: ..." is not sent to a session)."""
    with _question_changed:
        open_ones = [q for q, v in _questions.items() if v["answer"] is None]
        if not open_ones:
            return False
        _questions[min(open_ones)]["answer"] = text
        _question_changed.notify_all()
    return True


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
