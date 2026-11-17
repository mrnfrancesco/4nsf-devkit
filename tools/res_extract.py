#!/usr/bin/env python3
"""List or unpack a ".res" archive: the resources of navigon.exe and PhoneApplication.exe.

Usage: res_extract.py list    FILE.res [...]
       res_extract.py extract FILE.res OUTDIR     .pnx images are written as .png (OUTDIR not inside firmware/)

An archive is a list of named files, stored as they are:

  0x00  header: u32 version (1), u32 offset of the first file, u32 number of files, u32 archive size
  0x10  one row per file: its path (NUL-terminated, zero-padded to 4 bytes), u32 size, u32 offset
        the files, back to back

Little endian, no checksums. The .pnx images are PNXF bitmaps (zlib-packed here); they are
converted with the decoder of kbi_extract.py (Pillow). More in docs/system/ui-framework.md.
"""
import os
import struct
import sys
from collections import namedtuple

from dissect.cstruct import cstruct

from kbi_extract import pnxf_image

res = cstruct("""
struct header {
    uint32  version;                // 1
    uint32  data_offset;            // where the first file starts = end of the rows
    uint32  entries;
    uint32  size;                   // of the archive
};

struct row {
    char    name[];                 // followed by 0 to 3 more zero bytes
};
""")

Entry = namedtuple("Entry", "name offset size")


class ResError(Exception):
    pass


def read_entries(data):
    """The rows of an archive, checked against its header."""
    if len(data) < len(res.header):
        raise ResError("too short")
    header = res.header(data)
    if header.version != 1 or header.size != len(data):
        raise ResError("not a .res archive")
    entries, position = [], len(res.header)
    for _ in range(header.entries):
        name = res.row(data[position:position + 512]).name
        position = (position + len(name) + 4) & ~3              # at least one NUL, then padding
        size, offset = struct.unpack_from("<II", data, position)
        position += 8
        if offset + size > len(data):
            raise ResError(f"{name.decode('latin1')} ends beyond the archive")
        entries.append(Entry(name.decode("latin1"), offset, size))
    if position != header.data_offset:
        raise ResError("rows do not end where the files start")
    return entries


def list_archives(paths):
    failed = False
    for path in paths:
        try:
            data = open(path, "rb").read()
            entries = read_entries(data)
        except (ResError, OSError) as problem:
            print(f"{path}: ERROR {problem}")
            failed = True
            continue
        end = max(entry.offset + entry.size for entry in entries)
        print(f"== {path}  entries={len(entries)} size={len(data)} trailing={len(data) - end}")
        for entry in entries:
            print(f"   {entry.size:9d}  {entry.name}")
    return 1 if failed else 0


def extract(path, target):
    target = os.path.realpath(target)
    if os.sep + "firmware" + os.sep in target + os.sep:
        raise SystemExit("refusing to extract inside firmware/")
    data = open(path, "rb").read()
    entries = read_entries(data)
    written = images = 0
    for entry in entries:
        # never let a name lead outside the target folder
        file = os.path.realpath(os.path.join(target, entry.name.lstrip("/")))
        if not file.startswith(target + os.sep) or ".." in entry.name.split("/"):
            print(f"skip unsafe name {entry.name!r}")
            continue
        contents = data[entry.offset:entry.offset + entry.size]
        os.makedirs(os.path.dirname(file), exist_ok=True)
        if contents[:4] == b"PNXF":
            try:
                pnxf_image(contents).save(os.path.splitext(file)[0] + ".png")
                images += 1
            except Exception as problem:
                print(f"FAIL {entry.name}: {problem!r}")
                continue
        else:
            open(file, "wb").write(contents)
        written += 1
    print(f"extracted {written}/{len(entries)} entries ({images} as .png) to {target}")
    return 0 if written == len(entries) else 1


def main(arguments):
    if len(arguments) >= 2 and arguments[0] == "list":
        return list_archives(arguments[1:])
    if len(arguments) == 3 and arguments[0] == "extract":
        return extract(arguments[1], arguments[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
