#!/usr/bin/env python3
"""Unpack the self-extracting archives of the VW update package.

Usage: sfx_extract.py ARCHIVE OUTDIR      extract, checking every file's MD5
       sfx_extract.py ARCHIVE --list      size, MD5 and path of every file

The package has two of them: Install_OS.exe (boot files and the Windows CE image) and
UpdateMeContent.content (the whole \\My Flash Disk). Both are a small Windows CE program
followed by the files, a table of contents and a trailer:

  [program][file data, back to back][table of contents][trailer "!NAVIGON_SFX!"]

  table header   u32 0, u32 offset of the first file (= size of the program)
  one record     u64 size, path, 32 hex characters MD5, u32 attributes, two FILETIMEs,
                 u64 offset where the file ends (missing after the last record)
  trailer        u32 0, u8 0, u32 file count, u32 2, u8 1, destination folder, ...

Integers are little endian, except that 64-bit values store their high half first.
Text is UTF-16 and is *followed* by its length in bytes as a u32.
"""
import hashlib
import os
import struct
import sys
from collections import namedtuple

import pefile

MARKER = "!NAVIGON_SFX!".encode("utf-16le")
TABLE_SEARCH_WINDOW = 128 * 1024 * 1024     # the table of contents is within this much of the end
CHUNK = 8 * 1024 * 1024

Entry = namedtuple("Entry", "path size start md5")


def read_u64(buffer, position):
    high, low = struct.unpack_from("<II", buffer, position)
    return high << 32 | low


def read_text(buffer, position):
    """Text has no length in front: try lengths until the u32 after the text confirms one."""
    for characters in range(1, 4096):
        size = 2 * characters
        if struct.unpack_from("<I", buffer, position + size)[0] == size:
            return buffer[position:position + size].decode("utf-16le"), position + size + 4
    raise ValueError(f"no text found at {position:#x}")


def program_size(archive):
    """Where the leading program ends: the end of its last PE section."""
    archive.seek(0)
    program = pefile.PE(data=archive.read(4096), fast_load=True)       # the headers are enough
    return max(section.PointerToRawData + section.SizeOfRawData for section in program.sections)


def table_of_contents(archive):
    """Returns (entries, destination folder)."""
    total = archive.seek(0, os.SEEK_END)
    first_file = program_size(archive)
    window = min(total, TABLE_SEARCH_WINDOW)
    archive.seek(total - window)
    tail = archive.read(window)
    if not tail.endswith(MARKER):
        raise ValueError("not a NAVIGON_SFX archive")

    # The table header is recognisable: u32 0, u32 first_file, then a record whose size has
    # a zero high half and whose path starts with a backslash. Take the last such place.
    header = struct.pack("<III", 0, first_file, 0)
    position = len(tail)
    while True:
        position = tail.rfind(header, 0, position)
        if position < 0:
            raise ValueError("table of contents not found")
        if tail[position + 16:position + 18] == "\\".encode("utf-16le"):
            break
    table_offset = total - window + position

    entries = []
    start = first_file
    position += 8
    while True:
        size = read_u64(tail, position)
        path, position = read_text(tail, position + 8)
        md5 = tail[position:position + 64].decode("utf-16le")
        position += 64 + 4 + 16                     # MD5, attributes, two FILETIMEs
        entries.append(Entry(path, size, start, md5.lower()))
        start += size
        if read_u64(tail, position) != start:       # no end offset: that was the last record
            break
        position += 8

    count, = struct.unpack_from("<I", tail, position + 5)
    if count != len(entries):
        raise ValueError(f"trailer says {count} files, table has {len(entries)}")
    if start != table_offset:
        raise ValueError("file data does not end where the table of contents starts")
    destination, _ = read_text(tail, position + 5 + 4 + 4 + 1)
    return entries, destination


def extract(archive, entry, target):
    """Copy one file out; returns True if its MD5 matches."""
    os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
    digest = hashlib.md5()
    archive.seek(entry.start)
    remaining = entry.size
    with open(target, "wb") as out:
        while remaining:
            data = archive.read(min(remaining, CHUNK))
            if not data:
                raise IOError(f"archive ends inside {entry.path}")
            out.write(data)
            digest.update(data)
            remaining -= len(data)
    return digest.hexdigest() == entry.md5


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, target = sys.argv[1:]
    with open(path, "rb") as archive:
        entries, destination = table_of_contents(archive)
        if target == "--list":
            for entry in entries:
                print(f"{entry.size:12d}  {entry.md5}  {entry.path}")
            return
        damaged = []
        for entry in entries:
            relative = entry.path
            if relative.lower().startswith(destination.lower()):
                relative = relative[len(destination):]
            relative = relative.replace("\\", "/").lstrip("/")
            if not extract(archive, entry, os.path.join(target, relative)):
                damaged.append(relative)
    if damaged:
        sys.exit(f"MD5 mismatch: {', '.join(damaged)}")
    print(f"{len(entries)} files -> {target} (all MD5 sums match)")


if __name__ == "__main__":
    main()
