#!/usr/bin/env python3
"""Decode or build AppCom datagrams: the messages the unit's programs send each other over UDP.

Usage: appcom.py decode tools/samples/appcom-capture.txt        a capture, one datagram per line
       appcom.py decode -                                       the same from standard input
       appcom.py encode PORT MSGID PAYLOAD [TICKET ORIGIN]      the datagram(s) of one message, as hex

  capture line   "TICK PORT LENGTH HEX" (PORT = the port the datagram arrived on) or just the hex bytes;
                 other lines are copied. The same datagram on several ports is shown once.
  encode         PORT = the sender's port, PAYLOAD = JSON; TICKET and ORIGIN make it a sync message:
                 appcom.py encode 27011 40 0                    "navigation is up", as navigon sends it
                 appcom.py encode 27017 2 '[2, "ger-DEU"]'      language change

The format, as implemented in ACAppCom.dll / NgAppCom.dll (docs/apps/appcom.md has the details):

  datagram   int32 datagrams still to follow, int32 sender's port, up to 4000 bytes of the message
  message    a list: [Int MsgID, payload] or, for a sync message and its reply,
             [Int MsgID, payload, Int ticket, Int port of the asking program]
  value      uint32 type, then
               1 Rect     4 x float32            {"rect": [left, top, right, bottom]}
               2 Point    2 x float32            {"point": [x, y]}
               3 Pair     two values             {"pair": [key, value]}
               4 List     uint32 count, values   [...]
               5, 6 Binary  uint32 size, bytes, padded to 4. Type 6 carries the strings, as UTF-8:
                          "text", or {"hex": "..."} if it is not text; type 5: {"type5": ...}
               7 Float    float64                1.5
               8 Int      int32                  1
               9 Int64    int64                  {"int64": 1}

Little endian. The right-hand column is how a value is written here, in JSON.
"""
import json
import struct
import sys

CHUNK = 4000                        # message bytes per datagram
RECT, POINT, PAIR, LIST, BINARY5, BINARY, FLOAT, INT, INT64 = range(1, 10)

# Short labels for the message ids whose meaning is known; the catalogue is docs/apps/appcom.md §9.
NAMES = {
    2: "setting changed", 3: "volume request", 4: "phone state", 8: "navigation command",
    10: "navigation -> phone request", 11: "show application", 12: "contact address to navigate to",
    14: "voice dialog coordination", 16: "shell priority flag", 18: "low-fuel flag changed",
    19: "car state changed", 20: "radio connected / lost", 21: "app is up", 22: "GPS status",
    23: "media source icon", 27: "low-fuel prompt answered", 28: "power key pressed",
    29: "sound claim / release", 30: "Bluetooth versions", 31: "shell overlay state",
    32: "Bluetooth audio track texts", 34: "outside temperature", 36: "suspend notice",
    37: "resume notice", 38: "standby countdown", 39: "remaining range", 40: "navigation is up",
    41: "battery warning (e-up!)", 42: "steering-wheel phone key", 43: "may the phone start a voice dialog",
    45: "vehicle status", 48: "online services status", 51: "text request / answer",
    52: "developer switches", 1001: "shared bitmap list", 1002: "shared list", 1003: "shared status list",
    1004: "shared workflow list", 11007: "phone state", 11011: "GPS speed", 11012: "position",
    11013: "car speed",
}


def encode(value):
    """A value, written as in the table above, as bytes."""
    if isinstance(value, bool) or isinstance(value, int):
        return struct.pack("<Ii", INT, value)
    if isinstance(value, float):
        return struct.pack("<Id", FLOAT, value)
    if isinstance(value, str):
        return binary(BINARY, value.encode("utf-8"))
    if isinstance(value, list):
        return struct.pack("<II", LIST, len(value)) + b"".join(encode(item) for item in value)
    (tag, content), = value.items()
    if tag == "hex":
        return binary(BINARY, bytes.fromhex(content))
    if tag == "type5":
        return binary(BINARY5, content.encode("utf-8") if isinstance(content, str) else bytes.fromhex(content["hex"]))
    if tag == "int64":
        return struct.pack("<Iq", INT64, content)
    if tag == "pair":
        return struct.pack("<I", PAIR) + encode(content[0]) + encode(content[1])
    if tag == "point":
        return struct.pack("<I2f", POINT, *content)
    if tag == "rect":
        return struct.pack("<I4f", RECT, *content)
    raise ValueError(f"unknown value {value!r}")


def binary(kind, data):
    return struct.pack("<II", kind, len(data)) + data + bytes(-len(data) % 4)


def decode(data, position=0):
    """(value, position after it) of the value that starts at `position`."""
    kind, = struct.unpack_from("<I", data, position)
    position += 4
    if kind == INT:
        return struct.unpack_from("<i", data, position)[0], position + 4
    if kind == FLOAT:
        return struct.unpack_from("<d", data, position)[0], position + 8
    if kind == INT64:
        return {"int64": struct.unpack_from("<q", data, position)[0]}, position + 8
    if kind in (BINARY, BINARY5):
        size, = struct.unpack_from("<I", data, position)
        contents = data[position + 4:position + 4 + size]
        if len(contents) != size:
            raise ValueError("binary longer than the message")
        try:
            value = contents.decode("utf-8")
        except UnicodeDecodeError:
            value = {"hex": contents.hex()}
        return (value if kind == BINARY else {"type5": value}), position + 4 + size + -size % 4
    if kind == LIST:
        count, = struct.unpack_from("<I", data, position)
        position += 4
        items = []
        for _ in range(count):
            item, position = decode(data, position)
            items.append(item)
        return items, position
    if kind == PAIR:
        key, position = decode(data, position)
        value, position = decode(data, position)
        return {"pair": [key, value]}, position
    if kind == POINT:
        return {"point": list(struct.unpack_from("<2f", data, position))}, position + 8
    if kind == RECT:
        return {"rect": list(struct.unpack_from("<4f", data, position))}, position + 16
    raise ValueError(f"unknown value type {kind}")


def datagrams(sender_port, message):
    """The datagrams that carry `message` (the list [MsgID, payload] or [MsgID, payload, ticket, origin])."""
    data = encode(message)
    last = len(data) // CHUNK
    return [struct.pack("<ii", last - number, sender_port) + data[number * CHUNK:(number + 1) * CHUNK]
            for number in range(last + 1)]


def describe(message, end, size):
    """A decoded message as text: the list, then what is known about it."""
    notes = []
    if isinstance(message, list) and message and isinstance(message[0], int):
        if message[0] in NAMES:
            notes.append(NAMES[message[0]])
        if len(message) == 4:
            notes.append(f"sync, ticket {message[2]}, asked by {message[3]}")
        elif len(message) != 2:
            notes.append("not a message: 2 or 4 elements expected")
    else:
        notes.append("not a message: no MsgID")
    if end != size:
        notes.append(f"{size - end} bytes left over")
    return json.dumps(message, ensure_ascii=False) + (f"   # {'; '.join(notes)}" if notes else "")


def decode_capture(lines):
    """Print the messages of a capture. Returns the number of datagrams that could not be decoded."""
    unfinished = {}                 # (sender's port, port of arrival) -> message bytes so far
    shown = None                    # the output line being collected: [datagram, tick, sender, ports, text]
    failed = 0

    def flush():
        if shown:
            _, tick, sender, ports, text = shown
            arrival = f"{tick:>9} {sender} -> {','.join(ports):<35}" if ports else f"{sender}"
            print(f"{arrival} {text}")

    for line in lines:
        fields = line.split()
        try:
            datagram = bytes.fromhex("".join(fields) if len(fields) != 4 else fields[3])
            remaining, sender = struct.unpack_from("<ii", datagram)
        except (ValueError, struct.error):
            flush()
            shown = None
            if line.strip():
                print(line.rstrip())
            continue
        tick, port = (fields[0], fields[1]) if len(fields) == 4 else ("", "")
        if shown and shown[0] == datagram and port and port not in shown[3]:
            shown[3].append(port)
            continue
        flush()
        data = unfinished.pop((sender, port), b"") + datagram[8:]
        if remaining > 0:
            unfinished[sender, port] = data
            shown = None
            continue
        try:
            message, end = decode(data)
            text = describe(message, end, len(data))
        except (ValueError, struct.error) as problem:
            text = f"?? cannot decode: {problem if isinstance(problem, ValueError) else 'message too short'}"
            failed += 1
        shown = [datagram, tick, sender, [port] if port else [], text]
    flush()
    return failed


def main(arguments):
    if len(arguments) == 2 and arguments[0] == "decode":
        capture = sys.stdin if arguments[1] == "-" else open(arguments[1], errors="replace")
        return 1 if decode_capture(capture) else 0
    if len(arguments) in (4, 6) and arguments[0] == "encode":
        try:
            port, message_id = int(arguments[1]), int(arguments[2])
            message = [message_id, json.loads(arguments[3])] + [int(number) for number in arguments[4:]]
            for datagram in datagrams(port, message):
                print(datagram.hex())
            return 0
        except (ValueError, TypeError, AttributeError, KeyError, struct.error) as problem:
            print(f"cannot encode: {problem!r}\n")
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
