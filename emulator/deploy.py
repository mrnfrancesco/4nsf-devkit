#!/usr/bin/env python3
"""Copy files to and from the running emulator and start programs in it.

  deploy.py ping
  deploy.py push hello.exe NgAppInfo.xml "\\My Flash Disk\\Apps\\hello" --run     copy into a folder, start the first
  deploy.py put  hello.exe "\\My Flash Disk\\dev\\hello.exe"
  deploy.py get  "\\My Flash Disk\\dev\\log.txt" log.txt
  deploy.py ls   "\\My Flash Disk\\dev"
  deploy.py rm   "\\My Flash Disk\\dev\\hello.exe"
  deploy.py mkdir "\\My Flash Disk\\dev"
  deploy.py run  [--wait SECONDS] "\\My Flash Disk\\dev\\hello.exe" [arguments...]
  deploy.py ps                                                                  running programs
  deploy.py mem                                                                 free RAM (start a program and compare)

Talks to ceagent.exe inside the guest (emulator/guest/ceagent.c, which documents the
protocol). The agent listens on the guest's COM2:, which QEMU exposes as TCP 127.0.0.1:5556.
"""
import argparse
import os
import socket
import struct
import sys

MAGIC = 0x31414543      # "CEA1"
PING, PUT, CHUNK, CLOSE, GET, RUN, LIST, DEL, MKDIR, EXIT, PS, MEM = range(1, 13)
CHUNK_SIZE = 16384
STILL_RUNNING = 0xFFFFFFFF


class Agent:
    def __init__(self, host, port, timeout):
        self.socket = socket.create_connection((host, port), timeout=timeout)
        self.stream = self.socket.makefile("rb")

    def read(self, size):
        data = self.stream.read(size)
        if len(data) < size:
            raise IOError("connection closed")
        return data

    def receive(self):
        """Next frame as (status, payload); bytes before the magic are skipped."""
        while True:
            if self.read(1) != b"C":
                continue
            header = b"C" + self.read(8)
            magic, status, length = struct.unpack("<IBI", header)
            if magic == MAGIC:
                return status, self.read(length)

    def call(self, command, payload=b""):
        self.socket.sendall(struct.pack("<IBI", MAGIC, command, len(payload)) + payload)
        status, data = self.receive()
        if status:
            raise RuntimeError(f"the agent answered with Windows error {status}")
        return data


def text(string):
    return string.encode("utf-16le") + b"\0\0"


def put(agent, local, remote):
    data = open(local, "rb").read()
    agent.call(PUT, text(remote) + struct.pack("<I", len(data)))
    for offset in range(0, len(data), CHUNK_SIZE):
        agent.call(CHUNK, data[offset:offset + CHUNK_SIZE])
    agent.call(CLOSE)
    print(f"{remote}: {len(data)} bytes")


def get(agent, remote, local):
    agent.call(GET, text(remote))
    with open(local, "wb") as out:
        while True:
            kind, data = agent.receive()
            if kind == CLOSE:
                break
            if kind != CHUNK:
                raise RuntimeError(f"unexpected frame {kind} while receiving a file")
            out.write(data)
    print(f"{local}: {os.path.getsize(local)} bytes")


def run(agent, command_line, wait_seconds):
    agent.socket.settimeout(agent.socket.gettimeout() + wait_seconds)       # the answer comes after the wait
    reply = agent.call(RUN, struct.pack("<I", int(wait_seconds * 1000)) + text(command_line))
    pid, exit_code = struct.unpack("<II", reply)
    if exit_code == STILL_RUNNING:
        print(f"started, process id {pid:#x}")
        return 0
    print(f"process {pid:#x} exited with code {exit_code}")
    return exit_code


def listing(agent, folder):
    for line in agent.call(LIST, text(folder)).decode("utf-16le").splitlines():
        name, size, attributes = line.split("\t")
        is_folder = int(attributes, 16) & 0x10
        print(f"{'<folder>' if is_folder else size:>10}  {name}")


def processes(agent):
    for line in agent.call(PS).decode("utf-16le").splitlines():
        name, process_id = line.split("\t")
        print(f"{process_id:>10}  {name}")


def memory(agent):
    load, total, free, virtual_total, virtual_free = struct.unpack("<5I", agent.call(MEM))
    mb = 1024 * 1024
    print(f"RAM {free // mb} of {total // mb} MB free ({load} % in use); "
          f"this process's virtual space: {virtual_free // mb} of {virtual_total // mb} MB free")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5556)
    parser.add_argument("--timeout", type=float, default=30)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("ping")
    commands.add_parser("exit", help="stop the agent")
    commands.add_parser("ps", help="list the running programs")
    commands.add_parser("mem", help="free memory (with and without a program running: its use)")
    for name, arguments in (("put", ["local", "remote"]), ("get", ["remote", "local"]),
                            ("ls", ["remote"]), ("rm", ["remote"]), ("mkdir", ["remote"])):
        sub = commands.add_parser(name)
        for argument in arguments:
            sub.add_argument(argument)
    sub = commands.add_parser("run")
    sub.add_argument("program")
    sub.add_argument("arguments", nargs="*")
    sub.add_argument("--wait", type=float, default=0, help="seconds to wait for the program to finish")
    sub = commands.add_parser("push")
    sub.add_argument("local", nargs="+")
    sub.add_argument("folder")
    sub.add_argument("--run", action="store_true", help="start the first file afterwards")
    sub.add_argument("--wait", type=float, default=0)
    args = parser.parse_args()

    try:
        agent = Agent(args.host, args.port, args.timeout)
    except OSError as error:
        sys.exit(f"no emulator agent on {args.host}:{args.port} ({error}): start the emulator first (make run)")
    if args.command == "ping":
        print(agent.call(PING).decode("utf-16le").rstrip("\0"))
    elif args.command == "exit":
        agent.call(EXIT)
    elif args.command == "ps":
        processes(agent)
    elif args.command == "mem":
        memory(agent)
    elif args.command == "put":
        put(agent, args.local, args.remote)
    elif args.command == "get":
        get(agent, args.remote, args.local)
    elif args.command == "ls":
        listing(agent, args.remote)
    elif args.command == "rm":
        agent.call(DEL, text(args.remote))
    elif args.command == "mkdir":
        agent.call(MKDIR, text(args.remote))
    elif args.command == "run":
        sys.exit(run(agent, " ".join([f'"{args.program}"'] + args.arguments), args.wait))
    elif args.command == "push":
        try:
            agent.call(MKDIR, text(args.folder))
        except RuntimeError:
            pass                            # the folder exists already
        remote = [args.folder.rstrip("\\") + "\\" + os.path.basename(path) for path in args.local]
        for local, target in zip(args.local, remote):
            put(agent, local, target)
        if args.run:
            sys.exit(run(agent, f'"{remote[0]}"', args.wait))


if __name__ == "__main__":
    main()
