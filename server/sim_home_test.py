"""Tests the pretend home (server/tools/sim_home.py) with the same packets the board sends.
Run:  python server/sim_home_test.py"""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "tools"))
import sim_home  # noqa: E402

failed = 0


def check(name, ok):
    global failed
    print(("ok   " if ok else "FAIL ") + name)
    failed += 0 if ok else 1


def connect_packet(user=None, password=None):
    cid = b"test"
    flags = 0x02 | (0x80 if user else 0) | (0x40 if password else 0)
    body = b"\x00\x04MQTT\x04" + bytes([flags]) + b"\x00\x1e" + len(cid).to_bytes(2, "big") + cid
    if user:
        body += len(user).to_bytes(2, "big") + user.encode()
    if password:
        body += len(password).to_bytes(2, "big") + password.encode()
    return bytes([0x10]) + sim_home.encode_length(len(body)) + body


def subscribe_packet(topic):
    body = b"\x00\x01" + len(topic).to_bytes(2, "big") + topic.encode() + b"\x00"
    return bytes([0x82]) + sim_home.encode_length(len(body)) + body


async def session(port, packets, user=None, password=None, read_for=0.4):
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(connect_packet(user, password))
    connack = await reader.readexactly(4)
    got = []
    if connack[3] == 0:
        for packet in packets:
            writer.write(packet)
        try:
            while True:
                header = await asyncio.wait_for(reader.readexactly(2), read_for)
                body = await reader.readexactly(header[1]) if header[1] else b""
                got.append((header[0], body))
        except (asyncio.TimeoutError, asyncio.IncompleteReadError):
            pass
    writer.close()
    return connack, got


async def main():
    broker = sim_home.Broker()
    for state_topic, _n in sim_home.DEVICES.values():
        broker.retained[state_topic] = b"OFF"
    server = await asyncio.start_server(broker.client, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]

    _, got = await session(port, [subscribe_packet("casa/sala/luz/state")])
    publishes = [b for h, b in got if h >> 4 == 3]
    check("a new subscriber gets the retained state (OFF)", len(publishes) == 1 and publishes[0].endswith(b"OFF"))

    await session(port, [sim_home.publish_packet("casa/sala/luz/set", b"ON")])
    _, got = await session(port, [subscribe_packet("casa/sala/luz/state")])
    publishes = [b for h, b in got if h >> 4 == 3]
    check("after ON the device state is ON", len(publishes) == 1 and publishes[0].endswith(b"ON") and not publishes[0].endswith(b"OFF"))

    await session(port, [sim_home.publish_packet("casa/sala/luz/set", b"OFF")])
    _, got = await session(port, [subscribe_packet("casa/sala/luz/state")])
    check("and OFF again", [b for h, b in got if h >> 4 == 3][0].endswith(b"OFF"))

    await session(port, [sim_home.publish_packet("casa/sala/luz/set", b"banana")])
    _, got = await session(port, [subscribe_packet("casa/sala/luz/state")])
    check("a strange command is ignored", [b for h, b in got if h >> 4 == 3][0].endswith(b"OFF"))

    _, got = await session(port, [subscribe_packet("casa/#")])
    check("a wildcard subscription gets every state", len([1 for h, b in got if h >> 4 == 3]) == 3)

    check("topic matching", sim_home.matches("a/+/c", "a/b/c") and not sim_home.matches("a/+/c", "a/b/d") and sim_home.matches("a/#", "a/b/c"))

    locked = sim_home.Broker("bug", "secret")
    server2 = await asyncio.start_server(locked.client, "127.0.0.1", 0)
    port2 = server2.sockets[0].getsockname()[1]
    connack, _ = await session(port2, [], "bug", "wrong")
    check("a wrong password is refused", connack[3] == 5)
    connack, _ = await session(port2, [], "bug", "secret")
    check("the right one is accepted", connack[3] == 0)


asyncio.run(main())
print("all passed" if not failed else f"{failed} failed")
sys.exit(1 if failed else 0)
