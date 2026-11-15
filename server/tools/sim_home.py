"""A pretend smart home for trying Bug's MQTT commands with no devices: a tiny MQTT broker plus simulated lights and a fan.

    python server/tools/sim_home.py                 listens on port 1883, anyone on your network may connect
    python server/tools/sim_home.py --port 18830    another port (for tests)
    python server/tools/sim_home.py --user bug --password-file server/mqtt_pass.txt     asks for a login

Say "enciende la luz de la sala" to the board and watch this window: the device switches and publishes its state
(retained, so "¿está encendida la luz de la sala?" gets an answer). It is a small QoS 0 broker, enough for Bug; use Mosquitto
for a real home (scripts/Instalar.bat -SmartHome).
"""
import argparse
import asyncio
import time

# command topic -> (state topic, name)
DEVICES = {
    "casa/sala/luz/set": ("casa/sala/luz/state", "luz de la sala"),
    "casa/cuarto/luz/set": ("casa/cuarto/luz/state", "luz del cuarto"),
    "casa/cuarto/ventilador/set": ("casa/cuarto/ventilador/state", "ventilador"),
}


def matches(pattern, topic):
    p, t = pattern.split("/"), topic.split("/")
    for i, part in enumerate(p):
        if part == "#":
            return True
        if i >= len(t) or (part != "+" and part != t[i]):
            return False
    return len(p) == len(t)


def encode_length(n):
    out = bytearray()
    while True:
        byte = n % 128
        n //= 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def publish_packet(topic, payload, retain=False):
    body = len(topic.encode()).to_bytes(2, "big") + topic.encode() + payload
    return bytes([0x30 | (1 if retain else 0)]) + encode_length(len(body)) + body


class Broker:
    def __init__(self, user=None, password=None):
        self.clients = {}  # writer -> [filters]
        self.retained = {}
        self.user, self.password = user, password

    def log(self, text):
        print(time.strftime("%H:%M:%S"), text, flush=True)

    def deliver(self, topic, payload, retain):
        if retain:
            if payload:
                self.retained[topic] = payload
            else:
                self.retained.pop(topic, None)
        for writer, filters in list(self.clients.items()):
            if any(matches(f, topic) for f in filters):
                writer.write(publish_packet(topic, payload))

    def device_acts(self, topic, payload):
        if topic in DEVICES:
            state_topic, name = DEVICES[topic]
            value = payload.decode(errors="replace").strip().upper()
            if value in ("ON", "OFF"):
                self.log(f"[casa] {name}: {value}")
                self.deliver(state_topic, value.encode(), True)

    async def read_packet(self, reader):
        first = await reader.readexactly(1)
        multiplier, length = 1, 0
        while True:
            byte = (await reader.readexactly(1))[0]
            length += (byte & 0x7F) * multiplier
            multiplier *= 128
            if not byte & 0x80:
                break
        return first[0], await reader.readexactly(length) if length else b""

    async def client(self, reader, writer):
        self.clients[writer] = []
        peer = writer.get_extra_info("peername")
        try:
            while True:
                header, body = await self.read_packet(reader)
                kind = header >> 4
                if kind == 1:  # CONNECT
                    flags = body[7]
                    o = 10
                    o += 2 + int.from_bytes(body[o:o + 2], "big")  # client id
                    user = password = None
                    if flags & 0x80:
                        n = int.from_bytes(body[o:o + 2], "big")
                        user = body[o + 2:o + 2 + n].decode()
                        o += 2 + n
                    if flags & 0x40:
                        n = int.from_bytes(body[o:o + 2], "big")
                        password = body[o + 2:o + 2 + n].decode()
                    if self.user and (user != self.user or password != self.password):
                        writer.write(bytes([0x20, 2, 0, 5]))  # not authorized
                        await writer.drain()
                        self.log(f"refused {peer}: wrong login")
                        return
                    writer.write(bytes([0x20, 2, 0, 0]))
                elif kind == 3:  # PUBLISH
                    n = int.from_bytes(body[:2], "big")
                    topic = body[2:2 + n].decode()
                    o = 2 + n + (2 if (header >> 1) & 3 else 0)
                    payload = body[o:]
                    self.deliver(topic, payload, bool(header & 1))
                    self.device_acts(topic, payload)
                elif kind == 8:  # SUBSCRIBE
                    packet_id = body[:2]
                    o, granted = 2, b""
                    while o < len(body):
                        n = int.from_bytes(body[o:o + 2], "big")
                        flt = body[o + 2:o + 2 + n].decode()
                        o += 2 + n + 1
                        self.clients[writer].append(flt)
                        granted += b"\x00"
                        for topic, value in self.retained.items():
                            if matches(flt, topic):
                                writer.write(publish_packet(topic, value, True))
                    writer.write(bytes([0x90]) + encode_length(2 + len(granted)) + packet_id + granted)
                elif kind == 12:  # PINGREQ
                    writer.write(bytes([0xD0, 0]))
                elif kind == 14:  # DISCONNECT
                    return
                await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            self.clients.pop(writer, None)
            writer.close()


async def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user")
    parser.add_argument("--password-file")
    args = parser.parse_args()
    password = None
    if args.password_file:
        with open(args.password_file, encoding="utf-8") as f:
            password = f.read().strip()
    broker = Broker(args.user, password)
    for state_topic, _name in DEVICES.values():
        broker.retained[state_topic] = b"OFF"
    server = await asyncio.start_server(broker.client, "0.0.0.0", args.port)
    broker.log(f"Pretend home listening on port {args.port}; devices: " + ", ".join(name for _s, name in DEVICES.values()))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
