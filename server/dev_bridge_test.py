"""Checks of the dev bridge with no hardware: python server/dev_bridge_test.py"""
import json
import os
import threading
import time

os.environ["BUG_NO_RELAY"] = "1"  # the tests must never talk to the real gateway
import bug_server  # noqa: F401  (registers the blueprint on the app)
import dev_bridge

client = bug_server.app.test_client()
failures = 0


def check(condition, message):
    global failures
    if not condition:
        failures += 1
        print("FAIL:", message)


def get(path, **kw):
    return client.get(path, headers={"X-Bug-Auth": dev_bridge.sign("GET", path.split("?")[0], **kw)})


def post(path, body, **kw):
    return client.post(path, json=body, headers={"X-Bug-Auth": dev_bridge.sign("POST", path, **kw)})


check(client.get("/dev/status").status_code == 401, "no signature is refused")
check(client.get("/dev/status", headers={"X-Bug-Auth": "123:abc"}).status_code == 401, "a bad signature is refused")
check(get("/dev/status").status_code == 200, "a good signature is accepted")
check(get("/dev/status", now=time.time() - 3600).status_code == 401, "a request an hour old is refused (no replay)")
check(get("/dev/status", key="0" * 64).status_code == 401, "the wrong token is refused")
check(client.get("/dev/status", headers={"X-Bug-Auth": dev_bridge.sign("POST", "/dev/status")}).status_code == 401, "a signature for another method is refused")

before = get("/dev/events?since=0").get_json()["latest"]
check(post("/dev/notify", {"text": ""}).status_code == 400, "an empty message is refused")
reply = post("/dev/notify", {"text": "Las pruebas pasaron", "level": "ok"}).get_json()
check(reply["ok"] and reply["id"] == before + 1, "a message gets the next number")
events = get(f"/dev/events?since={before}").get_json()["events"]
check(len(events) == 1 and events[0]["text"] == "Las pruebas pasaron" and events[0]["level"] == "ok", "the board gets the message")
check(events[0]["age"] <= 2, "each message says how old it is")
check(get(f"/dev/events?since={reply['id']}").get_json()["events"] == [], "nothing new once it was read")
check(post("/dev/notify", {"text": "x", "level": "nonsense"}).status_code == 200 and
      get(f"/dev/events?since={reply['id']}").get_json()["events"][0]["level"] == "info", "an unknown level becomes info")

# the long poll: a message that arrives while the board is waiting is delivered at once
latest = get("/dev/events?since=0").get_json()["latest"]
holder = {}


def waiter():
    started = time.time()
    holder["events"] = get(f"/dev/events?since={latest}&wait=10").get_json()["events"]
    holder["took"] = time.time() - started


thread = threading.Thread(target=waiter)
thread.start()
time.sleep(0.6)
dev_bridge.add_event("Aviso en vivo", "warn")
thread.join(12)
check(holder.get("events") and holder["events"][0]["text"] == "Aviso en vivo" and holder["took"] < 3, "the long poll answers as soon as a message arrives")
started = time.time()
empty = get(f"/dev/events?since={latest + 99}&wait=1").get_json()["events"]
check(empty == [] and 0.8 < time.time() - started < 3, "an empty long poll ends after the wait")

# the hook text
import importlib.util
import pathlib
spec = importlib.util.spec_from_file_location("bug_notify", pathlib.Path(__file__).parent / "tools" / "bug_notify.py")
notify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(notify)
check(notify.from_hook({"hook_event_name": "Stop", "cwd": "C:/work/shop-tests"}) == ("ok", "Claude terminó en shop-tests"), "the Stop hook text")
check(notify.from_hook({"hook_event_name": "Notification", "message": "Claude needs your permission to use Bash", "cwd": "/x/api"})[0] == "warn", "the permission hook text")

# Claude sessions: state, approvals, commands
dev_bridge.APPROVAL_WAIT = 3
spec = importlib.util.spec_from_file_location("bug_claude_hook", pathlib.Path(__file__).parent / "tools" / "bug_claude_hook.py")
claude_hook = importlib.util.module_from_spec(spec)
spec.loader.exec_module(claude_hook)
check(claude_hook.decision_output("allow")["hookSpecificOutput"]["decision"]["behavior"] == "allow", "allow output")
check(claude_hook.decision_output("deny")["hookSpecificOutput"]["decision"]["behavior"] == "deny", "deny output")
check(claude_hook.decision_output("none") is None, "no answer leaves the question to the terminal")
check(dev_bridge.describe_request({"tool_name": "Bash", "tool_input": {"command": "npm  test"}}) == "ejecutar: npm test", "a Bash request is described")
check(dev_bridge.describe_request({"tool_name": "Edit", "tool_input": {"file_path": "C:/x/app.py"}}) == "modificar app.py", "an edit request is described")

base = {"session_id": "s1", "cwd": "C:/work/shop-tests"}
post("/dev/hook", {"payload": dict(base, hook_event_name="UserPromptSubmit")})
check("trabajando" in dev_bridge.status_sentence(), "a prompt means Claude is working")
post("/dev/command", {"cmd": "say", "text": "continúa"})
check(get(f"/dev/events?since={get('/dev/events?since=0').get_json()['latest'] - 1}").get_json()["events"][-1]["text"].startswith("Tu última sesión de Claude está ocupada"),
      "no command is sent to a working session")


def ask(allow):
    holder = {}
    before_id = get("/dev/events?since=0").get_json()["latest"]
    request_payload = dict(base, hook_event_name="PermissionRequest", tool_name="Bash", tool_input={"command": "rm -rf build"})
    thread = threading.Thread(target=lambda: holder.update(post("/dev/hook", {"payload": request_payload}).get_json()))
    thread.start()
    time.sleep(0.5)
    event = get(f"/dev/events?since={before_id}").get_json()["events"][-1]
    check(event["ask"] > 0 and "rm -rf build" in event["text"], "the approval reaches the board with its number and the exact command")
    if allow is not None:
        check(post("/dev/command", {"cmd": "decide", "id": event["ask"], "allow": allow}).get_json()["ok"], "the decision is accepted")
        check(not post("/dev/command", {"cmd": "decide", "id": event["ask"], "allow": allow}).get_json()["ok"], "a decision counts once")
    thread.join(8)
    return holder.get("decision"), event


check(ask(True)[0] == "allow", "the hook gets allow")
check(ask(False)[0] == "deny", "the hook gets deny")
decision, stale = ask(None)
check(decision == "none", "with no answer the hook leaves the question to the terminal")
check(not post("/dev/command", {"cmd": "decide", "id": stale["ask"], "allow": True}).get_json()["ok"], "a late answer is not applied")
check(post("/dev/command", {"cmd": "nonsense"}).status_code == 400, "an unknown command is refused")
post("/dev/hook", {"payload": dict(base, hook_event_name="Stop"), "text": "Claude terminó en shop-tests", "level": "ok"})
check("esperando" in dev_bridge.status_sentence(), "Stop makes the session idle")
ran = {}
dev_bridge._run_claude = lambda text, session_id, cwd, profile=None: ran.update(text=text, session=session_id, cwd=cwd)
check(post("/dev/command", {"cmd": "say", "text": "corre las pruebas"}).get_json()["ok"], "a command goes to the idle session")
time.sleep(0.3)
check(ran == {"text": "corre las pruebas", "session": "s1", "cwd": "C:/work/shop-tests"}, "the right session and folder are resumed")
post("/dev/hook", {"payload": dict(base, hook_event_name="SessionEnd")})
check("esperando" in dev_bridge.status_sentence(), "an ended session can still be resumed")

cmd = dev_bridge.claude_command("claude", "hola", "s1", {"permission_mode": "acceptEdits", "allowed_tools": ["Read"], "disallowed_tools": ["Bash(git push*)"]})
check(cmd[:3] == ["claude", "-p", "hola"] and "--allowedTools" in cmd and "--disallowedTools" in cmd and cmd[cmd.index("--permission-mode") + 1] == "acceptEdits", "voice runs get explicit permissions")

# streamed runs: progress for the board and the live page
watcher = dev_bridge.RunWatcher("sigue", "shop-tests")
mark = get("/dev/events?since=0").get_json()["latest"]
for item in [{"type": "assistant", "message": {"content": [{"type": "text", "text": "Voy a revisar las pruebas que fallan"}]}},
             {"type": "assistant", "message": {"content": [{"type": "tool_use", "name": "Edit", "input": {"file_path": "C:/x/login.spec.ts"}}]}},
             {"type": "result", "result": "Arreglé dos pruebas. PREGUNTA: ¿subo los cambios, opción A o B?"}]:
    watcher.feed(json.dumps(item))
watcher.finish(True)
texts = [e["text"] for e in get(f"/dev/events?since={mark}").get_json()["events"]]
check(texts[0].startswith("Claude: Voy a revisar") and texts[-1].startswith("Claude pregunta: ¿subo los cambios"), "the plan is said first and a question is passed on")
check(len(watcher.run["steps"]) == 2 and watcher.run["steps"][1]["text"] == "edita login.spec.ts", "the steps are kept for the live page")
check(client.get("/dev/live").status_code == 200 and client.get("/dev/live.json").get_json()["runs"][-1]["done"], "the live page works on this computer")
check(client.get("/dev/live.json", environ_overrides={"REMOTE_ADDR": "192.168.1.9"}).status_code == 403, "the live page is closed to other machines")

real = ("Hice una revisión. - **Pruebas:** pasan todas. - **Pendiente mío:** no sigo a ciegas. Las que quedan son las capturas de pantalla "
        "y el servidor MCP. Dime con cuál quieres que siga.")
check(dev_bridge.find_question(dev_bridge.clean_answer(real)) .endswith("Dime con cuál quieres que siga."), "a question without PREGUNTA: is still found")
check(dev_bridge.find_question(dev_bridge.clean_answer("Listo. Las pruebas pasan y no quedó nada pendiente.")) == "", "a plain summary is not a question")
check("*" not in dev_bridge.clean_answer("**Pruebas:** `ok`"), "markdown is removed before speaking")

ask_text = "¿Sigo con la fase 2 (preguntarle a Claude por voz), con las capturas de pantalla (fase 3) o con el cierre de la fase 1?"
check(dev_bridge.find_options(ask_text, ask_text) == ["La fase 2 (preguntarle a Claude por voz)", "Las capturas de pantalla (fase 3)", "El cierre de la fase 1"],
      "the choices are read from a question")
listed = "Hice X. PREGUNTA: ¿Qué sigo? OPCIONES: Fase 2 | Capturas | Cierre"
check(dev_bridge.find_question(listed) == "¿Qué sigo?" and dev_bridge.find_options(listed, "") == ["Fase 2", "Capturas", "Cierre"], "Claude's own list is used")
check(dev_bridge.find_options("Listo", "Dime cuál prefieres") == [], "no choices when there is nothing to choose")
long_answer = "Hice los pantallazos. " + "Detalle. " * 40 + "PREGUNTA: ¿Qué sigo? OPCIONES: " + "A" * 100 + " | Segunda opción"
watcher = dev_bridge.RunWatcher("x", "p")
mark = get("/dev/events?since=0").get_json()["latest"]
watcher.feed(json.dumps({"type": "result", "result": long_answer}))
watcher.finish(True)
last = get(f"/dev/events?since={mark}").get_json()["events"][-1]
check(last["text"] == "Claude pregunta: ¿Qué sigo?" and len(last["options"][0]) == 100 and len(last["full"]) > 300, "the question, the options and the whole answer arrive in full")
post("/dev/command", {"cmd": "last"})
again = get(f"/dev/events?since={last['id']}").get_json()["events"][-1]
check(again["full"].startswith("Hice los pantallazos") and not again["speak"], "\"qué hizo Claude\" brings the last answer back")

check(dev_bridge.blocked_by_privacy("Bitwarden - Mozilla Firefox") == "bitwarden", "a password manager blocks screenshots")
check(dev_bridge.blocked_by_privacy("Mi proyecto - Visual Studio Code") is None, "an ordinary window does not")
check(dev_bridge.blocked_by_privacy("Cuenta - Mi Banco") is not None, "a bank page blocks screenshots")
dev_bridge.foreground_title = lambda: "Visual Studio Code"
path, message = dev_bridge.take_screenshot()
check(path is not None and path.stat().st_size > 1000, "a screenshot is saved: " + message)
if path:
    check(client.get(f"/dev/shot/{path.name}").status_code == 200 and client.get("/dev/shot/..%2Fx.png").status_code == 404, "the picture is served to this computer only by name")
    check(client.get(f"/dev/shot/{path.name}", environ_overrides={"REMOTE_ADDR": "192.168.1.9"}).status_code == 403, "other machines cannot see it")
    path.unlink()
dev_bridge.foreground_title = lambda: "KeePass"
check(post("/dev/command", {"cmd": "shot"}).get_json()["ok"] is False, "the shot command refuses in front of a password manager")

# orders go to the session you wrote to last, never to another one
dev_bridge._sessions.clear()
post("/dev/hook", {"payload": {"hook_event_name": "UserPromptSubmit", "session_id": "old", "cwd": "C:/a/old"}})
post("/dev/hook", {"payload": {"hook_event_name": "Stop", "session_id": "old", "cwd": "C:/a/old"}})
time.sleep(1.1)
post("/dev/hook", {"payload": {"hook_event_name": "UserPromptSubmit", "session_id": "mine", "cwd": "C:/a/mine"}})
check(not post("/dev/command", {"cmd": "say", "text": "sigue"}).get_json()["ok"], "if your last session is busy the order does not jump to an older idle one")
post("/dev/hook", {"payload": {"hook_event_name": "Stop", "session_id": "mine", "cwd": "C:/a/mine"}})
ran.clear()
check(post("/dev/command", {"cmd": "say", "text": "sigue"}).get_json()["ok"], "the order is accepted once it is idle")
time.sleep(0.3)
check(ran.get("session") == "mine", "the order goes to the session you wrote to last")

# read-only questions
asked = dev_bridge.ask_command("claude", "por qué falló el login", "")
check("--permission-mode" in asked and asked[asked.index("--permission-mode") + 1] == "dontAsk" and "Edit" not in asked and "Write" not in asked
      and "PowerShell" not in " ".join(asked) and "--resume" not in asked, "a question gets read-only tools and no write tools")
followed = dev_bridge.ask_command("claude", "y el siguiente", "abc")
check(followed[followed.index("--resume") + 1] == "abc", "a follow-up resumes the question's own session")
check(not post("/dev/command", {"cmd": "ask", "text": ""}).get_json()["ok"], "an empty question is not sent")

mark = get('/dev/events?since=0').get_json()['latest']

# ---- phase 4: Claude Code asks you something through the board
answer = {}
asker = threading.Thread(target=lambda: answer.update(post("/dev/mcp/ask", {"question": "¿Qué rama uso?", "options": ["main", "develop"], "wait": 20}).get_json()))
asker.start()
time.sleep(0.5)
question_event = get(f"/dev/events?since={mark}").get_json()["events"][-1]
check(question_event["options"] == ["main", "develop"] and "¿Qué rama uso?" in question_event["text"], "the question reaches the board with its options")
check(post("/dev/command", {"cmd": "say", "text": "Elijo: develop"}).get_json()["ok"], "tapping an option answers it")
asker.join(5)
check(answer.get("answer") == "develop" and not answer.get("timed_out"), "the waiting tool gets the chosen option")
check(client.post("/dev/mcp/ask", json={"question": "x"}).status_code == 401, "an unsigned question is refused")

withmcp = dev_bridge.claude_command("claude", "x", "s1", {"permission_mode": "acceptEdits", "mcp_config": "C:/x/mcp.json"})
check(withmcp[withmcp.index("--mcp-config") + 1] == "C:/x/mcp.json", "an MCP config file reaches the voice runs")

# the small picture for the board
w, h, pixels = dev_bridge.make_thumbnail(bytes([255, 0, 0] * 16), (4, 4))
check((w, h, pixels[:2]) == (4, 4, bytes([0xF8, 0x00])), "red becomes RGB565 with the bytes in the display library's order")
w, h, pixels = dev_bridge.make_thumbnail(bytes([10, 20, 30]) * (1920 * 1080), (1920, 1080))
check((w, h, len(pixels)) == (320, 180, 320 * 180 * 2), "a 1920x1080 screen becomes 320x180")
dev_bridge._thumb.update(w=w, h=h, data=pixels)
packet = dev_bridge.thumbnail_packet()
check(packet[:4] == bytes([0x40, 0x01, 0xB4, 0x00]) and len(packet) == 4 + 320 * 180 * 2, "the packet is width, height, then the pixels")
check(client.get("/dev/shot").status_code == 401, "the picture is not given without a signature")
check(get("/dev/shot").status_code == 200 and get("/dev/shot").data == packet, "the board downloads exactly that packet")
dev_bridge._thumb.update(w=0, h=0, data=b"")
check(get("/dev/shot").status_code == 404, "no picture yet is a 404")
dev_bridge.take_screenshot = lambda: (pathlib.Path("x.png"), "Pantallazo listo, está en la página en vivo")
dev_bridge._thumb.update(w=w, h=h, data=pixels)
sent = []
dev_bridge.shot_sink = sent.append
mark2 = get("/dev/events?since=0").get_json()["latest"]
check(post("/dev/command", {"cmd": "shot"}).get_json()["ok"], "the shot order is accepted")
time.sleep(0.6)
announced = get(f"/dev/events?since={mark2}").get_json()["events"][-1]
check(announced["shot"] is True and not announced["speak"] and len(sent) == 1 and sent[0] == packet, "the mailbox gets the picture first, then the board is told")
dev_bridge.shot_sink = None

# ---- orders that came over the internet are limited
project = str(pathlib.Path(__file__).parent)
inside = str(pathlib.Path(project) / "notes.md")
check(dev_bridge.remote_can_approve({"tool": "Read", "path": "", "cwd": project}), "a read can be approved from outside")
check(dev_bridge.remote_can_approve({"tool": "Edit", "path": inside, "cwd": project}), "an edit inside the session's folder can be approved from outside")
for bad in ({"tool": "Bash", "path": "", "cwd": project}, {"tool": "PowerShell", "path": "", "cwd": project}, {"tool": "WebFetch", "path": "http://x", "cwd": project},
            {"tool": "mcp__x__run", "path": "", "cwd": project}, {"tool": "Edit", "path": str(pathlib.Path(project).parent.parent / "other.txt"), "cwd": project},
            {"tool": "Write", "path": str(pathlib.Path(project) / ".claude" / "settings.json"), "cwd": project},
            {"tool": "Edit", "path": str(pathlib.Path(project) / "dev_claude.json"), "cwd": project}, {"tool": "Edit", "path": inside, "cwd": ""},
            {"tool": "Write", "path": str(pathlib.Path(project) / ".git" / "hooks" / "pre-commit"), "cwd": project}):
    check(not dev_bridge.remote_can_approve(bad), f"not from outside: {bad['tool']} {pathlib.Path(bad['path']).name}")
profile = dev_bridge.claude_command("claude", "x", "s1", dev_bridge.REMOTE_PROFILE)
check("Bash" in profile[profile.index("--disallowedTools") + 1:] and "PowerShell" in profile[profile.index("--disallowedTools") + 1:] and "Bash" not in profile[profile.index("--allowedTools") + 1:profile.index("--disallowedTools")],
      "a remote run cannot use the shell")
check(any("server/tools" in a for a in profile) and any("dev_" in a for a in profile), "a remote run cannot edit the bridge's code or secrets")

# a permission for a shell command: refused from outside, accepted from the board itself
approval_payload = dict(base, hook_event_name="PermissionRequest", tool_name="Bash", tool_input={"command": "dir"})
dev_bridge.APPROVAL_WAIT = 4
holder = {}
thread = threading.Thread(target=lambda: holder.update(post("/dev/hook", {"payload": approval_payload}).get_json()))
thread.start()
time.sleep(0.5)
ask_id = get(f"/dev/events?since={mark}").get_json()["events"][-1]["ask"]
body, status = dev_bridge.apply_command({"cmd": "decide", "id": ask_id, "allow": True}, origin="remote")
check(body == {"ok": False}, "a shell permission cannot be approved from outside")
check(dev_bridge._approvals[ask_id]["decision"] is None, "and it stays waiting for the board")
body, status = dev_bridge.apply_command({"cmd": "decide", "id": ask_id, "allow": False}, origin="remote")
check(body == {"ok": True}, "but rejecting from outside is always allowed")
thread.join(6)
check(holder.get("decision") == "deny", "the hook got the rejection")
thread = threading.Thread(target=lambda: holder.update(post("/dev/hook", {"payload": approval_payload}).get_json()))
thread.start()
time.sleep(0.5)
ask_id = get(f"/dev/events?since={mark}").get_json()["events"][-1]["ask"]
body, status = dev_bridge.apply_command({"cmd": "decide", "id": ask_id, "allow": True})
thread.join(6)
check(body == {"ok": True} and holder.get("decision") == "allow", "at home the same permission can be approved")

# the voice order from outside uses the fixed profile
used = {}
dev_bridge._run_claude = lambda text, session_id, cwd, profile=None: used.update(profile=profile)
post("/dev/hook", {"payload": dict(base, hook_event_name="UserPromptSubmit")})
post("/dev/hook", {"payload": dict(base, hook_event_name="Stop")})
dev_bridge.apply_command({"cmd": "say", "text": "sigue"}, origin="remote")
time.sleep(0.3)
check(used.get("profile") is dev_bridge.REMOTE_PROFILE, "a say that came from outside runs with the remote profile")
post("/dev/hook", {"payload": dict(base, hook_event_name="Stop")})
dev_bridge._sessions[base["session_id"]]["state"] = "idle"
used.clear()
dev_bridge.apply_command({"cmd": "say", "text": "sigue"})
time.sleep(0.3)
check("profile" in used and used["profile"] is None, "a say from the board at home keeps your own profile")
import dev_relay
check(dev_relay.run_order({"cmd": "decide", "id": 99999, "allow": True})[1] == 200, "the relay runs its orders as remote")

print("dev bridge:", "all checks passed" if not failures else f"{failures} failed")
raise SystemExit(1 if failures else 0)
