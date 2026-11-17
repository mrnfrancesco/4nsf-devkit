#!/usr/bin/env python3
"""Decode a capture of the serial link between the unit and the car radio (COM9:, 19200 8N1).

Usage: decode_frames.py tools/samples/dock-capture.txt     one frame per line: "<label>*<hex bytes>"
       decode_frames.py -                                  the same from standard input

The link, as implemented in VwCommunicationExe.exe (docs/car/car-link.md has the details):

  data frame   C2|C3  LEN  PAYLOAD[LEN]  CRC_HI CRC_LO  C1      C2/C3 = sequence number 0/1
  ack          C4|C5  C6                                        C4/C5 = acknowledged sequence 0/1
  escaping     a byte C0..C6 inside LEN, PAYLOAD or CRC is sent as C0, byte - C0
  CRC          CRC-16/ARC over start byte + LEN + PAYLOAD
  payload      ID_LO ID_HI DATA...     ID below 0x800 = CAN id; 0xF001/0xF002 = handshake
               a payload of 0 or 1 bytes carries no id (link sync / keepalive)

CAN ids in BAP_TO_CAR / BAP_FROM_CAR carry BAP, Volkswagen's protocol on top of CAN:
a 16-bit header (3 bits operation, 6 bits device "LSG", 6 bits function), optionally
split over several CAN messages.
"""
import sys

from crccheck.crc import CrcArc

START, END, ESCAPE, ACK_END = (0xC2, 0xC3), 0xC1, 0xC0, 0xC6
ACK = (0xC4, 0xC5)
HANDSHAKE = (0xF001, 0xF002)

BAP_TO_CAR = {0x67C, 0x6C7, 0x67A, 0x69D}
BAP_FROM_CAR = {0x66C, 0x679, 0x69C, 0x6D8, 0x6DA, 0x6E7}
CAN_NAMES = {
    0x35B: "GwMotor", 0x367: "GwAnt1", 0x369: "GwAnt2", 0x470: "BsgKombi",
    0x527: "GwKombi", 0x529: "Mfa4", 0x52D: "Mfa6", 0x52F: "Mfa7", 0x532: "Mfa8",
    0x555: "Motor7", 0x571: "Bsg2", 0x575: "Bsg3", 0x591: "Zke4", 0x5AC: "Hvem2",
    0x5C1: "MflTasten", 0x61A: "MotorEv1", 0x621: "KombiK1", 0x629: "Mfa1",
    0x62B: "Mfa2", 0x62D: "Mfa3", 0x651: "Systeminfo1", 0x653: "Gateway3",
    0x65F: "FzgIdent", 0x668: "Reichweite1", 0x669: "Reichweite2",
    0xF001: "RadioInit(handshake radio->PID)", 0xF002: "PidInit(handshake PID->radio)",
}
BAP_FUNCTIONS = {1: "GetAll", 2: "BAP-Config", 3: "FunctionList", 4: "HeartBeat",
                 0x0E: "FSG-Setup", 0x0F: "FSG-OperationState"}
BAP_OPERATIONS = ["Set/Start", "Get/Abort", "SetGet/StartResult", "HeartbeatStatus/Processing",
                  "Status/Result", "StatusAck", "Ack", "Error"]


def unescape(data):
    out = bytearray()
    data = iter(data)
    for byte in data:
        out.append(ESCAPE + next(data, 0) if byte == ESCAPE else byte)
    return bytes(out)


def encode_frame(sequence, payload):
    """Build a data frame: the opposite of decode(), for a simulated radio."""
    body = bytes([START[sequence & 1], len(payload)]) + bytes(payload)
    body += CrcArc.calc(body).to_bytes(2, "big")
    out = bytearray(body[:1])
    for byte in body[1:]:
        out += bytes([ESCAPE, byte - ESCAPE]) if ESCAPE <= byte <= ACK_END else bytes([byte])
    return bytes(out) + bytes([END])


def split_frame(frame):
    """A data frame as (sequence number, payload, True if length and CRC are right)."""
    body = unescape(frame[1:-1])
    length, payload, crc = body[0], body[1:-2], int.from_bytes(body[-2:], "big")
    return frame[0] & 1, payload, length == len(payload) and crc == CrcArc.calc(frame[:1] + body[:-2])


class Bap:
    """Decodes BAP messages, joining the ones that span several CAN messages."""

    def __init__(self):
        self.unfinished = {}            # (CAN id, channel) -> [total length, header, data so far]

    @staticmethod
    def header(high, low):
        """(operation, device, function) from the two header bytes."""
        return (high >> 4) & 7, ((high & 0x0F) << 2) | (low >> 6), low & 0x3F

    @staticmethod
    def describe(header, data):
        operation, device, function = header
        name = BAP_FUNCTIONS.get(function, "")
        return (f"BAP op={operation}({BAP_OPERATIONS[operation]}) LSG=0x{device:02X}({device}) "
                f"FCT=0x{function:02X}{'(' + name + ')' if name else ''} data=[{data.hex(' ')}]")

    def feed(self, can_id, data):
        if len(data) < 2:
            return "BAP? short"
        if not data[0] & 0x80:                                  # whole message in one CAN message
            return self.describe(self.header(data[0], data[1]), data[2:])

        channel = (data[0] >> 4) & 3
        key = (can_id, channel)
        if not data[0] & 0x40:                                  # first part of a long message
            total = ((data[0] & 0x0F) << 8) | data[1]
            header, received = self.header(data[2], data[3]), bytearray(data[4:])
            start = f"BAP-START ch={channel} len={total} "
            if len(received) >= total:
                return start + self.describe(header, bytes(received[:total]))
            self.unfinished[key] = [total, header, received]
            return start + self.describe(header, bytes(received)) + " (incomplete)"

        part = data[0] & 0x0F                                   # a following part
        if key not in self.unfinished:
            return f"BAP-CONT ch={channel} seq={part} data=[{data[1:].hex(' ')}] (no start seen)"
        total, header, received = self.unfinished[key]
        received += data[1:]
        if len(received) < total:
            return f"BAP-CONT ch={channel} seq={part} ({len(received)}/{total})"
        del self.unfinished[key]
        return f"BAP-CONT ch={channel} seq={part} -> complete: " + self.describe(header, bytes(received[:total]))


def decode(frame, bap):
    """One captured frame as a line of text."""
    if len(frame) == 2 and frame[0] in ACK and frame[1] == ACK_END:
        return f"ACK seq={frame[0] & 1}"
    if frame[0] not in START or frame[-1] != END:
        return "?? not a frame"
    sequence, payload, intact = split_frame(frame)
    length = len(payload)
    line = f"DATA seq={sequence} len={length} crc={'ok' if intact else 'BAD'} "
    if length == 0:
        return line + "link sync (empty)"
    if length == 1:
        return line + f"keepalive [{payload.hex()}]"

    message_id, data = int.from_bytes(payload[:2], "little"), payload[2:]
    if message_id in BAP_TO_CAR | BAP_FROM_CAR:
        direction = "PID->car" if message_id in BAP_TO_CAR else "car->PID"
        return line + f"CAN 0x{message_id:03X} {direction} " + bap.feed(message_id, data)
    if message_id in HANDSHAKE:
        text = data[:35].decode("ascii", "replace")
        return line + (f"0x{message_id:04X} {CAN_NAMES[message_id]} '{text}' "
                       f"ver={data[35]}.{data[36]} id/rest=[{data[37:].hex(' ')}]")
    return line + f"CAN 0x{message_id:03X} {CAN_NAMES.get(message_id, '?')} data=[{data.hex(' ')}]"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    capture = sys.stdin if sys.argv[1] == "-" else open(sys.argv[1])
    bap = Bap()
    for line in capture:
        if line.strip():
            label, _, hex_bytes = line.strip().rpartition("*")
            print(f"{label:>3}  {decode(bytes.fromhex(hex_bytes), bap)}")


if __name__ == "__main__":
    main()
