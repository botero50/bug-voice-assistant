"""bug-dev: an MCP server that lets Claude Code use your Bug board (stdio; add it once with `claude mcp add`, see docs/DEV_BRIDGE_PLAN.md).

Tools Claude gets:
  ask_user(question, options)   shows the question on the board (with a button per option) and WAITS for your tap or your voice
  notify_user(text, level)      puts a short message on the board and says it

It talks to the Bug server on this PC with the same signed requests as the hooks, so it only works with the pairing token on this computer.
It cannot run commands, read files or reach anything else. If the Bug server is off, the tools say so and Claude carries on.
"""
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_bridge  # noqa: E402  (signing)

from mcp.server.fastmcp import FastMCP  # noqa: E402

SERVER = os.getenv("BUG_SERVER", "http://127.0.0.1:5000")
mcp = FastMCP("bug-dev")


def _post(path, body, timeout):
    request = urllib.request.Request(SERVER + path, data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json", "X-Bug-Auth": dev_bridge.sign("POST", path)})
    with urllib.request.urlopen(request, timeout=timeout) as reply:
        return json.load(reply)


@mcp.tool()
def ask_user(question: str, options: list[str] | None = None) -> str:
    """Ask the user a question on their Bug desk board and wait for the answer (up to 10 minutes). Give 2 to 4 short options when the answer is a choice.
    Use it when you need a decision and the user is away from the keyboard. Returns the chosen option or what the user said."""
    try:
        answer = _post("/dev/mcp/ask", {"question": question[:300], "options": [o[:120] for o in (options or [])][:4]}, timeout=620)
    except (urllib.error.URLError, OSError, ValueError) as error:
        return f"The board could not be reached ({error}). Ask in the terminal instead."
    return "The user did not answer in time." if answer.get("timed_out") else str(answer.get("answer", ""))


@mcp.tool()
def notify_user(text: str, level: str = "info") -> str:
    """Show a short message (max 140 characters) on the user's Bug board and say it aloud. level: info, ok, warn or error."""
    try:
        _post("/dev/notify", {"text": text[:140], "level": level if level in ("info", "ok", "warn", "error") else "info"}, timeout=10)
    except (urllib.error.URLError, OSError, ValueError) as error:
        return f"The board could not be reached ({error})."
    return "Shown on the board."


if __name__ == "__main__":
    mcp.run()
