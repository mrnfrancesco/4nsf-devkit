#!/usr/bin/env python3
"""List, check or unpack a Navigon ".nfs" container (.map and .afs files are the same thing).

Usage: nfs_extract.py list    FILE.nfs [...]
       nfs_extract.py verify  FILE.nfs [...]     unpack everything in memory, check the checksums
       nfs_extract.py extract FILE.nfs OUTDIR    (OUTDIR must not be inside firmware/)

A container is a flat list of named files, each one stored as it is or as zlib pages:

  0x0000  header
  0x0048  information texts (who built the container, and when), zlib-compressed
  0x1000  entry table: one row per file, then its checksum
          the names of all files, zlib-compressed, then a checksum
          the entries, back to back. Each one is an entry header followed by
            kind 0   the file, as it is
            kind 3   one zlib stream per page of the file, then the table of the
                     streams' sizes (u16 each) and the checksum of that table

The structures are spelled out below and read with dissect.cstruct. Numbers are little
endian, checksums are Adler-32 (the one in zlib). Read-only: there is no repacker.
Format recovered from the Java sources in 4nsf-toolbox (Tools/NgFs-Extract.jar);
more in docs/system/file-formats.md.
"""
import io
import os
import sys
import zlib
from collections import namedtuple

from dissect.cstruct import cstruct

MAGIC = 0x0BD5FE11
ENTRY_MAGIC = 0x05D30D17
STORED = 0                      # entry kind; the other one seen is 3 = zlib pages

nfs = cstruct("""
struct header {
    uint32  magic;
    uint32  version;                // 5 in every file seen
    uint32  table_offset;           // 0x1000
    uint32  unknown_0c[3];
    uint32  info_packed_size;
    uint32  info_size;
    uint32  unknown_20;
    uint32  entries;                // number of files
    uint32  unknown_28;
    uint32  names_packed_size;
    uint32  names_size;
    uint32  unknown_34[3];
    uint32  info_offset;            // 0x48
    uint32  checksum;               // of the header up to here
};

struct table_row {
    uint32  name_offset;            // where the name starts in the unpacked names
    uint32  packed_size;            // size of the entry: header, contents and padding
};

struct name {
    uint16  size;
    char    text[size];
};

struct entry_header {
    uint32  magic;
    uint8   kind;
    uint8   zero[3];
    uint32  flags;                  // meaning unknown, the same for a whole container
    uint32  size;                   // of the unpacked file
    uint16  page_size;
    uint16  zero2;
    uint32  pages;                  // 0 if stored
    uint32  page_table;             // position in the container of the page size table
    uint32  unknown_1c;
    uint32  contents_checksum;      // of the stored bytes, without the page size table
    uint32  checksum;               // of the entry header up to here
};
""")

# The information texts are the one place with a big-endian number (the key's size).
nfs_info = cstruct("""
struct text {
    uint16  key_size;
    char    key[key_size];
    uint8   value_size;
    char    value[value_size];
};
""", endian=">")

Entry = namedtuple("Entry", "name offset packed_size header")      # header: None if damaged


class NfsError(Exception):
    pass


class Container:
    """What `data` holds: header, info (list of key, value), entries, warnings, end."""

    def __init__(self, data):
        self.data = data
        self.warnings = []
        if len(data) < len(nfs.header) or nfs.uint32(data[:4]) != MAGIC:
            raise NfsError("bad magic")
        self.header = header = nfs.header(data)
        if not checksum_follows(data, 0, len(nfs.header) - 4):
            self.warnings.append("header adler mismatch")

        self.info = []
        try:                                # nothing below needs the information texts
            start = header.info_offset or len(nfs.header)
            texts = zlib.decompress(data[start:start + header.info_packed_size])
            stream = io.BytesIO(texts)
            while stream.tell() + 3 <= len(texts):
                text = nfs_info.text(stream)
                self.info.append((text.key.decode("latin1"), text.value.decode("latin1")))
        except Exception as problem:
            self.warnings.append(f"info block: {problem!r}")

        table_size = header.entries * len(nfs.table_row)
        table = nfs.table_row[header.entries](data[header.table_offset:header.table_offset + table_size])
        if not checksum_follows(data, header.table_offset, table_size):
            self.warnings.append("table adler mismatch")
        names_start = header.table_offset + table_size + 4
        names = zlib.decompress(data[names_start:names_start + header.names_packed_size])
        if len(names) != header.names_size:
            self.warnings.append("names length mismatch")
        names = io.BytesIO(names)

        self.entries = []
        position = names_start + header.names_packed_size + 4
        for number, row in enumerate(table):
            names.seek(row.name_offset)
            name = nfs.name(names).text.decode("latin1")
            entry_header = None
            if position + len(nfs.entry_header) <= len(data):
                entry_header = nfs.entry_header(data[position:position + len(nfs.entry_header)])
            if entry_header is None or entry_header.magic != ENTRY_MAGIC:
                entry_header = None
                self.warnings.append(f"entry {number} ({name}): bad magic at 0x{position:x}")
            self.entries.append(Entry(name, position, row.packed_size, entry_header))
            position += row.packed_size
        self.end = position

    def contents(self, entry):
        """The unpacked file of one entry, with all its checksums checked."""
        data, header, name = self.data, entry.header, entry.name
        if header is None:
            raise NfsError("bad entry")
        start = entry.offset + len(nfs.entry_header)
        if header.kind == STORED:
            end = start + header.size
            contents = data[start:end]
        else:
            end = header.page_table                 # the pages end where the table of their sizes starts
            table_size = 2 * header.pages
            if not (start <= end and end + table_size + 4 <= entry.offset + entry.packed_size):
                raise NfsError(f"block table out of range in {name}")
            if not checksum_follows(data, end, table_size):
                raise NfsError(f"block table adler mismatch in {name}")
            pages = []
            position = start
            for size in nfs.uint16[header.pages](data[end:end + table_size]):
                pages.append(zlib.decompress(data[position:position + size]))
                position += size
            if position != end:
                raise NfsError(f"block lengths do not reach block table in {name}")
            contents = b"".join(pages)
        if len(contents) != header.size:
            raise NfsError(f"size mismatch in {name}: {len(contents)} != {header.size}")
        if zlib.adler32(data[start:end]) != header.contents_checksum:
            raise NfsError(f"payload adler mismatch in {name}")
        if not checksum_follows(data, entry.offset, len(nfs.entry_header) - 4):
            raise NfsError(f"entry header adler mismatch in {name}")
        return contents


def checksum_follows(data, start, size):
    """True if the `size` bytes at `start` are followed by their Adler-32 checksum."""
    end = start + size
    return zlib.adler32(data[start:end]) == nfs.uint32(data[end:end + 4])


def list_containers(paths):
    failed = False
    for path in paths:
        data = open(path, "rb").read()
        try:
            container = Container(data)
        except Exception as problem:
            print(f"{path}: ERROR {problem!r}")
            failed = True
            continue
        header = container.header
        print(f"== {path}  version={header.version} entries={header.entries} "
              f"size={len(data)} parsed_end={container.end}")
        for warning in container.warnings:
            print("   WARN", warning)
        for key, value in container.info:
            print(f"   info {key} = {value}")
        for entry in container.entries:
            if entry.header is None:
                print(f"   ?? {entry.name}")
                continue
            entry_header = entry.header
            kind = "stored" if entry_header.kind == STORED else f"zlib/k{entry_header.kind}"
            print(f"   {kind:9s} {entry_header.size:9d}  flags=0x{entry_header.flags:08x} "
                  f"page={entry_header.page_size} blocks={entry_header.pages}  {entry.name}")
    return 1 if failed else 0


def verify(paths):
    """Unpack every entry in memory, check all checksums, write nothing."""
    failed = False
    for path in paths:
        data = open(path, "rb").read()
        try:
            container = Container(data)
            kinds = {}                      # kind -> number of entries
            unpacked = 0
            for entry in container.entries:
                unpacked += len(container.contents(entry))
                kinds[entry.header.kind] = kinds.get(entry.header.kind, 0) + 1
        except Exception as problem:
            print(f"FAIL {problem!r}  {path}")
            failed = True
            continue
        status = "OK"
        if container.warnings:
            status = "WARN " + "; ".join(container.warnings[:2])
            failed = True
        print(f"{status}  v{container.header.version} entries={len(container.entries)} unpacked={unpacked} "
              f"kinds={kinds} trailing={len(data) - container.end}  {path}")
    return 1 if failed else 0


def extract(path, target):
    target = os.path.realpath(target)
    if os.sep + "firmware" + os.sep in target + os.sep:
        raise SystemExit("refusing to extract inside firmware/")
    container = Container(open(path, "rb").read())
    os.makedirs(target, exist_ok=True)
    written = 0
    for entry in container.entries:
        # names are flat in practice; never let one lead outside the target folder
        relative = entry.name.replace("\\", "/").lstrip("/")
        file = os.path.realpath(os.path.join(target, relative))
        if not file.startswith(target + os.sep) or ".." in relative.split("/"):
            print(f"skip unsafe name {entry.name!r}")
            continue
        try:
            contents = container.contents(entry)
        except Exception as problem:
            print(f"FAIL {entry.name}: {problem!r}")
            continue
        os.makedirs(os.path.dirname(file), exist_ok=True)
        open(file, "wb").write(contents)
        written += 1
    with open(os.path.join(target, "_nfs_info.txt"), "w") as notes:
        header_words = nfs.uint32[17](container.data[:0x44])
        notes.write(f"source: {os.path.basename(path)}\n")
        notes.write(f"header words: {' '.join(f'{word:08x}' for word in header_words)}\n")
        for key, value in container.info:
            notes.write(f"{key}\t= {value}\n")
    print(f"extracted {written}/{len(container.entries)} entries to {target}")
    return 0 if written == len(container.entries) else 1


def main(arguments):
    if len(arguments) >= 2 and arguments[0] == "list":
        return list_containers(arguments[1:])
    if len(arguments) >= 2 and arguments[0] == "verify":
        return verify(arguments[1:])
    if len(arguments) == 3 and arguments[0] == "extract":
        return extract(arguments[1], arguments[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
