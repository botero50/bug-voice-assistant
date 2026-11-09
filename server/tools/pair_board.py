"""Pairs the board with this PC: sends it the dev bridge token over the USB cable (never over the network).

    python server/tools/pair_board.py            finds the board's port by itself
    python server/tools/pair_board.py COM5       or name it
    python server/tools/pair_board.py --relay https://bug.headlessqa.com --ssid2 "My phone"    also set the internet mailbox and a hotspot

The same thing can be done from the installer page (Set up the board, "Dev bridge token"). Needs pyserial: pip install pyserial
"""
import argparse
import getpass
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import dev_bridge  # noqa: E402

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("This needs pyserial:  pip install pyserial")


def find_port():
    for port in list_ports.comports():
        if port.vid == 0x303A:  # Espressif's USB id (the board's built-in USB)
            return port.device
    ports = [p.device for p in list_ports.comports()]
    return ports[0] if len(ports) == 1 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", nargs="?", help="the board's serial port, e.g. COM5 (found by itself when there is only one)")
    parser.add_argument("--relay", help="address of the internet mailbox, e.g. https://bug.headlessqa.com (for use away from home)")
    parser.add_argument("--ssid2", help="name of a second Wi-Fi, e.g. your phone's hotspot")
    args = parser.parse_args()
    port = args.port or find_port()
    if not port:
        sys.exit("I could not tell which port is the board. Run it again with the port, for example COM5.")
    line = ("BUGCFG " + json.dumps({"dev_token": dev_bridge.token()}) + "\n").encode()
    with serial.Serial(port, 115200, timeout=0.3) as board:
        board.dtr = False
        board.rts = False
        end = time.time() + 25
        heard = ""
        while time.time() < end:
            board.write(line)  # the board may restart when the port opens, so ask again until it answers
            time.sleep(1.2)
            heard += board.read(board.in_waiting or 1).decode(errors="replace")
            if "BUGCFG OK" in heard:
                print(f"Paired: the board on {port} now has the token.")
                return
    sys.exit("The board did not answer. Is Bug installed and the cable plugged in?")


if __name__ == "__main__":
    main()
