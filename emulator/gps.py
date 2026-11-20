#!/usr/bin/env python3
"""Feed a simulated GPS receiver into the emulator (COM1: in the guest).

  gps.py                                                     parked in Wolfsburg
  gps.py --lat 52.42 --lon 10.78 --speed 50 --heading 90     driving east at 50 km/h
  gps.py --track route.gpx --speed 40                        along a GPX track (or route), then stop

emulator/emu exposes the guest's COM1: as TCP 127.0.0.1:5555. The receiver itself (NMEA 0183
sentences, satellites, movement along the heading, one fix per second) is the `nmeasim` library.
With --track the position is moved along the GPX file's points (`gpxpy`) at --speed, starting
when the emulator connects; at the last point the car stops. A GPX of a real road route: let
Navit drive one with its demo vehicle and log it (docs/apps/navit.md, "Emulator details").
"""
import argparse
import socket
import threading
import time

import gpxpy
import gpxpy.geo
from nmeasim.models import GpsReceiver
from nmeasim.simulator import Simulator


def load_track(path):
    """[(lat, lon)] of the GPX file's tracks, else its routes."""
    with open(path) as file:
        gpx = gpxpy.parse(file)
    points = [(p.latitude, p.longitude) for track in gpx.tracks for segment in track.segments for p in segment.points]
    if not points:
        points = [(p.latitude, p.longitude) for route in gpx.routes for p in route.points]
    if len(points) < 2:
        raise SystemExit(f"{path}: fewer than two points")
    return points


def bearing(a, b):
    """Course from a to b, degrees from north."""
    return gpxpy.geo.get_course(a[0], a[1], b[0], b[1]) % 360


def follow(simulator, points, kph):
    """Keep the receiver on the track: where it is after (time since start) x speed."""
    distances = [0.0]
    for a, b in zip(points, points[1:]):
        distances.append(distances[-1] + gpxpy.geo.haversine_distance(a[0], a[1], b[0], b[1]))
    print(f"track: {len(points)} points, {distances[-1] / 1000:.1f} km, {distances[-1] / (kph / 3.6) / 60:.0f} min")
    start = time.monotonic()
    segment = 0
    while True:
        travelled = (time.monotonic() - start) * kph / 3.6
        while segment < len(points) - 2 and distances[segment + 1] <= travelled:
            segment += 1
        a, b = points[segment], points[segment + 1]
        length = distances[segment + 1] - distances[segment]
        part = min(1.0, (travelled - distances[segment]) / length) if length else 1.0
        arrived = travelled >= distances[-1]
        with simulator.lock:
            simulator.gps.lat = a[0] + (b[0] - a[0]) * part
            simulator.gps.lon = a[1] + (b[1] - a[1]) * part
            if length:
                simulator.gps.heading = bearing(a, b)
            simulator.gps.kph = 0.0 if arrived else kph
        if arrived:
            print("track: end reached, stopped")
            return
        time.sleep(0.5)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--lat", type=float, default=52.4227)
    parser.add_argument("--lon", type=float, default=10.7865)
    parser.add_argument("--speed", type=float, default=0.0, help="km/h")
    parser.add_argument("--heading", type=float, default=0.0, help="degrees from north")
    parser.add_argument("--track", help="GPX file to drive along at --speed (default 40 km/h)")
    args = parser.parse_args()

    points = load_track(args.track) if args.track else None
    if points:
        args.lat, args.lon = points[0]
        args.heading = bearing(points[0], points[1])
        args.speed = args.speed or 40.0
    receiver = GpsReceiver(lat=args.lat, lon=args.lon, kph=args.speed, heading=args.heading,
                           altitude=80.0, geoid_sep=46.9, pdop=1.8, vdop=1.5)
    simulator = Simulator(receiver, heading_variation=0)        # 0: drive in a straight line
    follower = None
    while True:
        try:
            with socket.create_connection((args.host, args.port)) as connection:
                print(f"connected to {args.host}:{args.port}")
                if points and not follower:
                    follower = threading.Thread(target=follow, args=(simulator, points, args.speed), daemon=True)
                    follower.start()
                simulator.serve(connection.makefile("wb", buffering=0))     # returns when the link breaks
        except OSError as problem:
            print(f"serial port not ready ({problem}), retrying")
        time.sleep(2)


if __name__ == "__main__":
    main()
