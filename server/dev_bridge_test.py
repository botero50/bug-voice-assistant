"""Checks of the dev bridge with no hardware: python server/dev_bridge_test.py"""
import threading
import time

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

print("dev bridge:", "all checks passed" if not failures else f"{failures} failed")
raise SystemExit(1 if failures else 0)
