#!/usr/bin/env python3
"""Feed a simulated GPS receiver into the emulator (COM1: in the guest).

  gps.py                                                     parked in Wolfsburg
  gps.py --lat 52.42 --lon 10.78 --speed 50 --heading 90     driving east at 50 km/h

emulator/emu exposes the guest's COM1: as TCP 127.0.0.1:5555. The receiver itself (NMEA 0183
sentences, satellites, movement along the heading, one fix per second) is the `nmeasim` library.
"""
import argparse
import socket
import time

from nmeasim.models import GpsReceiver
from nmeasim.simulator import Simulator


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--lat", type=float, default=52.4227)
    parser.add_argument("--lon", type=float, default=10.7865)
    parser.add_argument("--speed", type=float, default=0.0, help="km/h")
    parser.add_argument("--heading", type=float, default=0.0, help="degrees from north")
    args = parser.parse_args()

    receiver = GpsReceiver(lat=args.lat, lon=args.lon, kph=args.speed, heading=args.heading,
                           altitude=80.0, geoid_sep=46.9, pdop=1.8, vdop=1.5)
    simulator = Simulator(receiver, heading_variation=0)        # 0: drive in a straight line
    while True:
        try:
            with socket.create_connection((args.host, args.port)) as connection:
                print(f"connected to {args.host}:{args.port}")
                simulator.serve(connection.makefile("wb", buffering=0))     # returns when the link breaks
        except OSError as problem:
            print(f"serial port not ready ({problem}), retrying")
        time.sleep(2)


if __name__ == "__main__":
    main()
