#!/usr/bin/env python3
"""Decompile functions of one of the unit's programs or DLLs to C, with readable names.

Usage: decompile.py FILE ADDRESS [...]        the functions that start at these addresses
       decompile.py FILE all > FILE.c         every function of the program (minutes for the big ones)
       decompile.py FILE list                 the start address and size of every function

  decompile.py firmware/flash/VwUserShell/VwCommunicationExe.exe 0x4391c     opens the serial port to the radio

Needs radare2 (`r2`) with the r2ghidra plugin on the host: optional programs, not installed
by `make setup`. radare2 does the decompiling; this script adds what it cannot know:

  functions   their start addresses, from the exception table (.pdata) of the program
  imports     Windows CE programs import COREDLL.dll by number; the names come from the exports
              of firmware/rom/modules/coredll.dll (`make firmware`), read with pefile.
              The small stubs that jump to an import are named after it
  constants   ARM code loads addresses from "literal pools" next to the function; `*0x43b3c`
              in radare2's output becomes the value stored there, or the text it points to
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

import pefile

REPOSITORY = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COREDLL = os.path.join(REPOSITORY, "firmware", "rom", "modules", "coredll.dll")
EXECUTABLE = 0x20000000             # section flag
R2 = ["r2", "-q", "-e", "scr.color=0", "-e", "bin.relocs.apply=true"]


class Program:
    """What pefile knows about a program: functions, imports, import stubs, contents by address."""

    def __init__(self, path):
        self.pe = pe = pefile.PE(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.image = pe.get_memory_mapped_image()
        self.code = []              # (start, end) of the sections that hold code
        for section in pe.sections:
            if section.Characteristics & EXECUTABLE:
                start = self.base + section.VirtualAddress
                self.code.append((start, start + section.Misc_VirtualSize))

        # .pdata: one row per function {u32 start, u32 prologue size : 8, size in instructions : 22, flags : 2}
        self.functions = {}
        for section in pe.sections:
            if section.Name.startswith(b".pdata"):
                for start, sizes in struct.iter_unpack("<II", section.get_data()[:section.Misc_VirtualSize & ~7]):
                    if start:
                        self.functions[start] = 4 * ((sizes >> 8) & 0x3FFFFF)

        ordinals = {}
        if os.path.exists(COREDLL):
            coredll = pefile.PE(COREDLL, fast_load=True)
            coredll.parse_data_directories([pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
            ordinals = {symbol.ordinal: symbol.name.decode() for symbol in coredll.DIRECTORY_ENTRY_EXPORT.symbols
                        if symbol.name}
        self.imports = {}           # address of the import's slot -> name
        self.coredll = {}           # ordinal -> name, for the imports of this program
        for library in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = library.dll.decode()
            for entry in library.imports:
                name = entry.name.decode() if entry.name else f"{dll.split('.')[0]}_{entry.ordinal}"
                if not entry.name and dll.lower() == "coredll.dll":
                    name = self.coredll[entry.ordinal] = ordinals.get(entry.ordinal, name)
                self.imports[entry.address] = re.sub(r"\W", "_", name)

        # import stubs: "ldr ip, [pc, 4]; ldr ip, [ip]; bx ip; .word slot" or "ldr ip, [pc]; ldr pc, [ip]; .word slot"
        self.stubs = {}             # address -> name of the import
        for start, end in self.code:
            for address in range(start, end - 15, 4):
                words = struct.unpack_from("<4I", self.image, address - self.base)
                if words[:3] == (0xE59FC004, 0xE59CC000, 0xE12FFF1C) and words[3] in self.imports:
                    self.stubs[address] = self.imports[words[3]]
                elif words[:2] == (0xE59FC000, 0xE59CF000) and words[2] in self.imports:
                    self.stubs[address] = self.imports[words[2]]

    def word(self, address):
        offset = address - self.base
        return struct.unpack_from("<I", self.image, offset)[0] if 0 <= offset <= len(self.image) - 4 else None

    def text(self, address):
        """The UTF-16 or ASCII text at `address` as a C literal, or None."""
        data = self.image[max(address - self.base, 0):][:400] if address >= self.base else b""
        wide = re.match(rb"(?:[\x20-\x7e\r\n\t]\0){3,}", data)
        if wide:
            return 'L"' + wide.group().decode("utf-16-le").encode("unicode_escape").decode() + '"'
        narrow = re.match(rb"[\x20-\x7e\r\n\t]{4,}", data)
        return '"' + narrow.group().decode().encode("unicode_escape").decode() + '"' if narrow else None

    def literal(self, match):
        """Replacement for `*0xADDRESS` when ADDRESS is in a literal pool (constant data inside the code)."""
        address = int(match.group(1), 16)
        value = self.word(address)
        if value is None or not any(start <= address < end for start, end in self.code):
            return match.group()
        if value in self.imports:
            return "&" + self.imports[value]
        return self.text(value) or f"0x{value:x}"

    def tidy(self, line):
        """One line of radare2's output with literals resolved and imports named."""
        line = re.sub(r"\*0x([0-9a-f]{5,8})\b", self.literal, line)
        line = re.sub(r"(?:sym\.)?imp\.COREDLL\.dll_Ordinal_(\d+)",
                      lambda match: self.coredll.get(int(match.group(1)), match.group()), line)
        return re.sub(r"\b(?:fcn\.|func_0x)([0-9a-f]{8})\b",
                      lambda match: self.stubs.get(int(match.group(1), 16), match.group()), line)


def decompile(path, program, addresses):
    """Run radare2 on `path` and print the tidied C of the functions at `addresses`."""
    if not shutil.which("r2"):
        raise SystemExit("radare2 (`r2`) not found: install radare2 and its r2ghidra plugin to use this tool")
    plugins = subprocess.run(R2 + ["-c", "Lc", "--"], capture_output=True, text=True).stdout
    if "r2ghidra" not in plugins:
        raise SystemExit("radare2 has no r2ghidra plugin (the `pdg` command): install it to use this tool")

    commands = [f"af @ 0x{address:x}; afn {name} @ 0x{address:x}" for address, name in program.stubs.items()]
    commands += [f"af @ 0x{address:x}" for address in addresses]
    commands += [f"?e; ?e // ======== 0x{address:x}; pdg @ 0x{address:x}" for address in addresses]
    with tempfile.NamedTemporaryFile("w", suffix=".r2") as script:
        script.write("\n".join(commands) + "\n")
        script.flush()
        r2 = subprocess.Popen(R2 + ["-i", script.name, path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              text=True, errors="replace")
        for line in r2.stdout:
            sys.stdout.write(program.tidy(line))
        return r2.wait()


def main(arguments):
    if len(arguments) < 2:
        print(__doc__)
        return 2
    path, addresses = arguments[0], arguments[1:]
    program = Program(path)
    if addresses == ["list"]:
        for start, size in sorted(program.functions.items()):
            print(f"0x{start:x} {size}")
        return 0
    try:
        addresses = sorted(program.functions) if addresses == ["all"] else [int(address, 16) for address in addresses]
    except ValueError:
        print(__doc__)
        return 2
    return decompile(path, program, addresses)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except BrokenPipeError:
        pass
