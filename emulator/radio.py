#!/usr/bin/env python3
"""Play the car radio for the emulator: the other end of the unit's car link (COM3: in the guest).

  radio.py                                        engine idling, parked, doors shut
  radio.py --rpm 2500 --speed 80 --outside 5      driving on a cold day
  radio.py --doors 01 --trace                     driver's door open; print every frame

emulator/emu exposes the guest's COM3: as TCP 127.0.0.1:5557, and emulator/build.sh points the
unit's car-link program (VwCommunicationExe) at that port. This script does what the radio does
on the dock connector (docs/car/car-link.md): acknowledge the unit's frames, send the handshake
with the car's VIN and fuel type (petrol), then forward CAN messages over and over. The menu's
car screens (Car info: trip computer, gauges) then show the values given here.

Not simulated: BAP, the protocol for the radio's own functions (sound source, station lists,
parking sensors, clock). The unit's BAP requests are acknowledged and left unanswered, which
the unit tolerates; screens that need BAP data stay empty.

If the unit restarts the link (it does when it hears nothing for 3 seconds), the handshake is
simply repeated.

Frame format and checksum come from tools/decode_frames.py; the contents of each message from
docs/car/can-signals.md (section numbers in the comments below).
"""
import argparse
import os
import socket
import sys
import time
from collections import deque

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import decode_frames as link                                    # noqa: E402  (needs the path above)

VIN = "WVWZZZAAZFD000000"           # the placeholder of emulator/stubs/WolfSpecial.c: "the car the unit knows"
RETRANSMIT, KEEPALIVE, TICK = 0.2, 1.0, 0.1                     # seconds; car-link.md §2


def message(can_id, data):
    """Payload of one serial frame: the CAN id (little endian), then the CAN data."""
    return can_id.to_bytes(2, "little") + bytes(data)


def bits(length, *fields):
    """Pack (start bit, value) pairs into `length` bytes, the way can-signals.md §1 counts bits."""
    return sum(value << start for start, value in fields).to_bytes(length, "little")


def handshake(vin):
    """F001, what the radio says first (can-signals.md §3.16): text, version 1.0, VIN, fuel, line."""
    petrol, standard_line = 0x01, 0x01
    return message(0xF001, b"VW BCR copyright 2009 Volkswagen AG" + bytes([1, 0]) + vin.encode("ascii")
                   + bytes([petrol, standard_line, 0]))         # the last byte only makes it long enough


def off_by_one(car):
    """The same car with every reading one step off. Sent once after each handshake: the unit
    passes on changes only, and the menu blanks its car screens whenever the link (re)starts,
    so readings that never change would stay blank."""
    changed = dict(rpm=car.rpm + 100, speed=car.speed + 1, coolant=car.coolant + 1, oil=car.oil + 1,
                   outside=car.outside + 0.5, consumption=car.consumption + 0.1, range=car.range + 1)
    return argparse.Namespace(**{**vars(car), **changed})


def fast(car):
    """Messages a car sends every 100 ms (can-signals.md §7)."""
    rpm, coolant = round(car.rpm / 0.25), round((car.coolant + 48) / 0.75)
    return [message(0x35B, bits(5, (8, rpm), (24, coolant)))]               # GwMotor §3.1


def medium(car):
    """Every 200 ms. The unit measures distance by counting GwKombi messages at this rate (§3.5)."""
    speed, outside = round(car.speed / 0.01), round((car.outside + 50) / 0.5)
    return [message(0x575, bits(1, (0, 1), (1, 1))),                        # Bsg3 §3.8: key in, ignition on
            message(0x527, bits(6, (9, speed), (40, outside)))]             # GwKombi §3.5


def trip(consumption, distance, minutes, speed):
    """One memory of the trip computer (§3.6): each value with its unit, here l/100 km, km and km/h."""
    return bits(8, (0, round(consumption * 10)), (12, 1), (16, distance), (30, 1), (32, minutes),
                (48, speed), (62, 1))


def slow(car):
    """Every second: body, trip computer, identification."""
    vin = VIN.encode("ascii")
    return [
        message(0x555, bits(8, (56, round(car.oil + 60)))),                 # Motor7 §3.7
        message(0x470, bits(2, (8, car.doors))),                            # BsgKombi §3.4
        message(0x591, bits(3, (19, 1))),                                   # Zke4 §3.8: unlocked: open doors get shown
        message(0x621, bits(1, (1, car.low_fuel))),                         # KombiK1 §3.8
        message(0x651, bits(4, (8, 1), (12, 0), (16, 0), (29, 0))),         # Systeminfo1 §3.12: class A00 (up!),
                                                                            # Volkswagen, hatchback, not 4 doors
        # Mfa1 §3.6: consumption now in l/100 km, range in km, odometer in km, no second fuel
        message(0x629, bits(8, (0, round(car.consumption * 10)), (12, 1), (16, car.range), (30, 1),
                            (32, 12345), (54, 1), (56, 0xFF))),
        message(0x62B, trip(car.consumption + 0.4, 23, 35, 39)),            # Mfa2: since start
        message(0x62D, trip(car.consumption + 0.7, 2345, 2890, 48)),        # Mfa3: long term
        message(0x65F, bytes([0, 0, 0, 0, 0]) + vin[0:3]),                  # FzgIdent §3.14: the VIN again,
        message(0x65F, bytes([1]) + vin[3:10]),                             # in three parts
        message(0x65F, bytes([2]) + vin[10:17]),
    ]


class Radio:
    """The link layer of car-link.md §2, seen from the radio: one frame at a time, each acknowledged."""

    def __init__(self, connection, trace):
        self.connection, self.trace, self.started = connection, trace, time.monotonic()
        self.received = bytearray()
        self.bap = link.Bap()                                   # only to print BAP frames readably
        self.up = False                                         # the unit has started the link
        self.restart()

    def restart(self):
        self.queue = deque()                                    # payloads waiting to be sent
        self.sequence = self.expected = 0                       # next frame to send / to accept
        self.waiting, self.sent_at = None, 0.0                  # frame not acknowledged yet

    def log(self, direction, frame):
        if self.trace:
            print(f"{time.monotonic() - self.started:8.3f} {direction} {link.decode(frame, self.bap)}")

    def write(self, frame):
        self.connection.sendall(frame)
        self.sent_at = time.monotonic()
        self.log("radio", frame)

    def frames(self):
        """The complete frames received so far."""
        try:
            data = self.connection.recv(4096)
            if not data:
                raise ConnectionError("closed by the emulator")
            self.received += data
        except socket.timeout:
            pass
        while self.received:
            if self.received[0] in link.ACK:
                size = 2
            elif self.received[0] in link.START:
                size = self.received.find(link.END) + 1
            else:                                               # noise: skip to something recognisable
                del self.received[0]
                continue
            if size == 0 or size > len(self.received):
                break
            yield bytes(self.received[:size])
            del self.received[:size]

    def receive(self):
        """Handle what has arrived; returns the payloads of the unit's new data frames."""
        payloads = []
        for frame in self.frames():
            self.log(" unit", frame)
            if frame[0] in link.ACK:
                if self.waiting and frame[0] & 1 == self.sequence:
                    self.waiting, self.sequence = None, self.sequence ^ 1
                continue
            sequence, payload, intact = link.split_frame(frame)
            if not intact:
                continue                                        # no acknowledgement: the unit sends it again
            if payload and not self.up:
                continue                                        # a link left over from an earlier radio: stay
                                                                # silent, the unit restarts it after 3 seconds
            self.write(bytes([link.ACK[sequence], link.ACK_END]))
            if not payload:                                     # empty frame: the unit (re)starts the link
                self.restart()
                self.up = True
                payloads.append(payload)
            elif sequence == self.expected:                     # else a repeat of one we have
                self.expected ^= 1
                payloads.append(payload)
        return payloads

    def transmit(self):
        """Send the next waiting frame, or the unacknowledged one again."""
        now = time.monotonic()
        if self.waiting:
            if now - self.sent_at > RETRANSMIT:
                self.write(self.waiting)
        elif self.up and (self.queue or now - self.sent_at > KEEPALIVE):
            self.waiting = link.encode_frame(self.sequence, self.queue.popleft() if self.queue else b"\0")
            self.write(self.waiting)


def serve(connection, car, trace):
    connection.settimeout(0.02)
    radio = Radio(connection, trace)
    tick, next_tick = 0, time.monotonic()
    while True:
        for payload in radio.receive():
            if not payload:
                print("link up: sending the handshake")
                radio.queue.append(handshake(VIN))
                radio.queue += fast(off_by_one(car)) + medium(off_by_one(car)) + slow(off_by_one(car))
        if radio.up and time.monotonic() >= next_tick:
            tick, next_tick = tick + 1, max(next_tick + TICK, time.monotonic())
            if len(radio.queue) < 20:                           # else the unit is slow to acknowledge: skip a round
                radio.queue += fast(car)
                radio.queue += medium(car) if tick % 2 == 0 else []
                radio.queue += slow(car) if tick % 10 == 0 else []
        radio.transmit()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5557)
    parser.add_argument("--rpm", type=float, default=850, help="engine speed")
    parser.add_argument("--speed", type=float, default=0, help="km/h")
    parser.add_argument("--coolant", type=float, default=90, help="°C")
    parser.add_argument("--oil", type=float, default=95, help="°C")
    parser.add_argument("--outside", type=float, default=18.5, help="°C")
    parser.add_argument("--consumption", type=float, default=5.2, help="l/100 km, now")
    parser.add_argument("--range", type=int, default=420, help="km")
    parser.add_argument("--doors", type=lambda text: int(text, 16), default=0,
                        help="open doors, hex: 01 driver, 02 passenger, 10 bonnet, 20 boot")
    parser.add_argument("--low-fuel", action="store_const", const=1, default=0, help="the low-fuel warning is on")
    parser.add_argument("--trace", action="store_true", help="print every frame in both directions")
    car = parser.parse_args()

    while True:
        try:
            with socket.create_connection((car.host, car.port)) as connection:
                print(f"connected to {car.host}:{car.port}, waiting for the unit to start the link")
                serve(connection, car, car.trace)
        except OSError as problem:
            print(f"serial port not ready ({problem}), retrying")
        time.sleep(2)


if __name__ == "__main__":
    main()
