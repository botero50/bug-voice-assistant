"""Tests of the free-chat fallback without Ollama: a tiny fake server stands in for it.
Run:  python server/llm_chat_test.py"""
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import llm_chat  # noqa: E402

seen = []


class Fake(BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        seen.append(body)
        text = "<think>pienso</think>**Hola**, esto es *una* respuesta.\n\n- de prueba" if len(seen) == 1 else "Segunda respuesta."
        data = json.dumps({"message": {"role": "assistant", "content": text}}).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


server = HTTPServer(("127.0.0.1", 0), Fake)
threading.Thread(target=server.serve_forever, daemon=True).start()
llm_chat.URL = f"http://127.0.0.1:{server.server_port}/api/chat"
failed = 0


def check(name, condition):
    global failed
    print(("ok   " if condition else "FAIL ") + name)
    failed += 0 if condition else 1


answer = llm_chat.ask("dime algo")
check("markdown and thinking are removed", answer == "Hola, esto es una respuesta. - de prueba")
check("thinking is turned off and the answer is short", seen[0]["think"] is False and seen[0]["options"]["num_predict"] <= 200)
check("the system prompt asks for spoken Spanish", "español" in seen[0]["messages"][0]["content"])
llm_chat.ask("y otra")
roles = [m["role"] for m in seen[1]["messages"]]
check("the second question carries the first exchange", roles == ["system", "user", "assistant", "user"])
check("an empty sentence is not sent", llm_chat.ask("   ") is None and len(seen) == 2)
llm_chat.ENABLED = False
check("disabled means no call", llm_chat.ask("hola") is None and len(seen) == 2)
llm_chat.ENABLED = True
llm_chat.URL = "http://127.0.0.1:9/api/chat"  # nothing listens here
check("Ollama away gives None (the caller says 'No te entendí')", llm_chat.ask("hola") is None)
print("all passed" if not failed else f"{failed} failed")
sys.exit(1 if failed else 0)
