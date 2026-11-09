"""Prepares this PC for the internet mailbox (the bug-gateway Worker in F:\\Automation company\\bug-gateway).

    python server/tools/gateway_setup.py                          uses https://bug.headlessqa.com
    python server/tools/gateway_setup.py https://other.example    another address

It makes a random PC token the first time (server/pc_token.txt), writes server/dev_relay.json for the server, and prints the two
commands that give the gateway its secrets. The tokens are never printed here: the commands read them from the files, so they do not
appear in your screen, your history or this chat. Both files are ignored by git.
"""
import json
import secrets
import sys
from pathlib import Path

SERVER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SERVER))
import dev_bridge  # noqa: E402


def main():
    url = (sys.argv[1] if len(sys.argv) > 1 else "https://bug.headlessqa.com").rstrip("/")
    if not url.startswith("https://"):
        sys.exit("The address must start with https://")
    pc_file = SERVER / "pc_token.txt"
    if not pc_file.exists():
        pc_file.write_text(secrets.token_hex(32), encoding="utf-8")
    dev_bridge.token()  # makes server/dev_token.txt (the board's token) if it is not there yet
    (SERVER / "dev_relay.json").write_text(json.dumps({"url": url, "pc_token": pc_file.read_text(encoding="utf-8").strip()}, indent=2), encoding="utf-8")
    print(f"Written: {SERVER / 'dev_relay.json'}  (the server connects out to {url} when it starts)\n")
    print("Now give the gateway its two secrets. In PowerShell, from F:\\Automation company\\bug-gateway :\n")
    print(f'  Get-Content "{SERVER / "dev_token.txt"}" -Raw | npx wrangler secret put BOARD_TOKEN')
    print(f'  Get-Content "{pc_file}" -Raw | npx wrangler secret put PC_TOKEN\n')
    print("Then:  npx wrangler deploy   and restart the Bug server. Details: F:\\Automation company\\bug-gateway\\README.md")


if __name__ == "__main__":
    main()
