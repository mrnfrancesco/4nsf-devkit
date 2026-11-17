#!/usr/bin/env python3
"""Take a Windows CE system image (ROM) apart.

Usage: cerom.py IMAGE OUTDIR      write OUTDIR/modules/ and OUTDIR/files/
       cerom.py IMAGE --list      only list what is inside

A Windows CE ROM (NK.nb0, or a flat NK.bin) is the content of the \\Windows folder, laid
out so it can run straight from flash memory. It holds two kinds of things:

  modules   programs and DLLs stored "execute in place": sections already loaded at their
            final addresses, PE headers thrown away. This tool rebuilds a normal PE file
            around each one, good for disassemblers and for reading export tables. They
            cannot be run elsewhere: the relocation information is gone. Sections that
            are copied to RAM at load time (initialised data, resources) may be stored
            compressed, like files; they are written out decompressed.

  files     ordinary files (fonts, registry, extra DLLs), some of them compressed (LZX).
            They are written out decompressed and are fully usable.

Structures: ROMHDR / TOCentry / FILESentry / e32_rom / o32_rom, from Microsoft's public
Windows CE headers (romldr.h, pehdr.h).
"""
import os
import struct
import sys
from collections import namedtuple

import libarchive

SIGNATURE = b"ECEC"             # at offset 0x40, followed by the address of the ROM header
MACHINE_THUMB = 0x1C2
FILE_ALIGNMENT, SECTION_ALIGNMENT, HEADERS_SIZE = 0x200, 0x1000, 0x400

RomHeader = namedtuple("RomHeader", "dllfirst dlllast physfirst physlast nummods ramstart ramfree ramend "
                       "copyentries copyoffset profilelen profileoffset numfiles kernelflags fsrampercent "
                       "drivglobstart drivgloblen cputype miscflags extensions trackingstart trackinglen")
ROM_HEADER = struct.Struct("<17I2H3I")

Module = namedtuple("Module", "attributes filetime size name e32 o32 load")
MODULE = struct.Struct("<IQ5I")

File = namedtuple("File", "attributes filetime size stored_size name load")
FILE = struct.Struct("<IQ4I")

# e32: what is left of a module's PE header
E32 = struct.Struct("<HHIIHHIIIII18IH")
Image = namedtuple("Image", "sections flags entry base subsystem_major subsystem_minor stack size "
                   "com_descriptor timestamp directories subsystem")
# the nine data directories an e32 keeps, in its own order = PE directory indexes 0..8
EXPORT, IMPORT, RESOURCE, EXCEPTION, SECURITY, FIXUP, DEBUG, IMPORT_DESCRIPTION, MACHINE = range(9)
COM_DESCRIPTOR = 14

# o32: one section of a module
O32 = struct.Struct("<6I")
Section = namedtuple("Section", "virtual_size rva stored_size data_address real_address flags")
CODE, INITIALISED_DATA, DISCARDABLE, READABLE, WRITABLE = 0x20, 0x40, 0x02000000, 0x40000000, 0x80000000
COMPRESSED = 0x2000             # stored like a compressed file; virtual_size counts the zeroes after it too
IMAGE_IS_DLL = 0x2000


class Rom:
    def __init__(self, data):
        self.data = data
        if data[0x40:0x44] != SIGNATURE:
            raise ValueError("not a Windows CE ROM image (no signature at 0x40)")
        header_address, = struct.unpack_from("<I", data, 0x44)
        # The header stores the address of the image's first byte (physfirst). Its own
        # position is found as the one place where both numbers agree.
        for offset in range(0, len(data) - 0x60, 4):
            first, = struct.unpack_from("<I", data, offset + 8)
            if first and header_address - first == offset:
                break
        else:
            raise ValueError("ROM header not found")
        self.header = RomHeader(*ROM_HEADER.unpack_from(data, offset))
        self.tables = offset + ROM_HEADER.size      # module table, then file table

    def offset(self, address):
        offset = address - self.header.physfirst
        if not 0 <= offset < len(self.data):
            raise ValueError(f"address {address:#x} is outside the image")
        return offset

    def text(self, address):
        start = self.offset(address)
        return self.data[start:self.data.index(b"\0", start)].decode("latin-1")

    def modules(self):
        for i in range(self.header.nummods):
            entry = Module(*MODULE.unpack_from(self.data, self.tables + i * MODULE.size))
            yield entry._replace(name=self.text(entry.name))

    def files(self):
        table = self.tables + self.header.nummods * MODULE.size
        for i in range(self.header.numfiles):
            entry = File(*FILE.unpack_from(self.data, table + i * FILE.size))
            yield entry._replace(name=self.text(entry.name))

    def file_contents(self, entry):
        start = self.offset(entry.load)
        stored = self.data[start:start + entry.stored_size]
        return decompress(stored) if entry.stored_size != entry.size else stored

    def module_as_pe(self, module):
        """Rebuild a PE file from a module's e32/o32 headers and its sections."""
        fields = E32.unpack_from(self.data, self.offset(module.e32))
        image = Image(*fields[:8], com_descriptor=fields[8:10], timestamp=fields[10],
                      directories=list(zip(fields[11:29:2], fields[12:29:2])), subsystem=fields[29])
        sections = [Section(*O32.unpack_from(self.data, self.offset(module.o32) + i * O32.size))
                    for i in range(image.sections)]
        bodies = [self.section_body(s) for s in sections]
        names = section_names(sections, image.directories)

        is_code = [bool(s.flags & CODE) for s in sections]
        code_size = sum(len(b) for b, code in zip(bodies, is_code) if code)
        data_size = sum(len(b) for b, code in zip(bodies, is_code) if not code)
        code_start = next((s.rva for s, code in zip(sections, is_code) if code), 0)
        data_start = next((s.rva for s, code in zip(sections, is_code) if not code), 0)

        directories = [(0, 0)] * 16
        directories[EXPORT:MACHINE + 1] = image.directories
        directories[COM_DESCRIPTOR] = image.com_descriptor

        pe = bytearray()
        pe += b"MZ" + bytes(0x3A) + struct.pack("<I", 0x40)                     # DOS header -> PE header
        pe += b"PE\0\0"
        pe += struct.pack("<HHIIIHH", MACHINE_THUMB, len(sections), image.timestamp, 0, 0, 0xE0,
                          0x0102 | (image.flags & IMAGE_IS_DLL))                # executable, 32 bit
        pe += struct.pack("<HBBIIIIIII", 0x10B, 8, 0, code_size, data_size, 0,
                          image.entry, code_start, data_start, image.base)
        pe += struct.pack("<IIHHHHHHIIIIHHIIIIII", SECTION_ALIGNMENT, FILE_ALIGNMENT, 4, 0, 0, 0,
                          image.subsystem_major, image.subsystem_minor, 0, image.size, HEADERS_SIZE, 0,
                          image.subsystem, 0, image.stack, 0x1000, 0x100000, 0x1000, 0, len(directories))
        for rva, size in directories:
            pe += struct.pack("<II", rva, size)

        position = HEADERS_SIZE
        for section, name, body in zip(sections, names, bodies):
            stored = -(-len(body) // FILE_ALIGNMENT) * FILE_ALIGNMENT
            pe += struct.pack("<8sIIIIIIHHI", name.encode(), section.virtual_size, section.rva, stored,
                              position if stored else 0, 0, 0, 0, 0, pe_section_flags(section))
            position += stored
        pe += bytes(HEADERS_SIZE - len(pe))
        for body in bodies:
            pe += body + bytes(-len(body) % FILE_ALIGNMENT)
        return bytes(pe)

    def section_body(self, section):
        if not section.stored_size or not section.data_address:
            return b""
        try:
            start = self.offset(section.data_address)
        except ValueError:          # lives in RAM only (uninitialised data)
            return b""
        stored = self.data[start:start + section.stored_size]
        return decompress(stored) if section.flags & COMPRESSED else stored


def section_names(sections, directories):
    """ROM modules have no section names: invent the usual ones from flags and contents."""
    names, seen = [], {}
    for s in sections:
        if s.flags & DISCARDABLE and s.rva == directories[FIXUP][0]:
            name = ".reloc"
        elif s.rva == directories[RESOURCE][0] and directories[RESOURCE][1]:
            name = ".rsrc"
        elif s.rva == directories[IMPORT][0] and directories[IMPORT][1] and not s.flags & CODE:
            name = ".idata"
        elif s.flags & CODE:
            name = ".text"
        elif s.flags & WRITABLE:
            name = ".data"
        else:
            name = ".rdata"
        if name in seen:            # a second .text becomes .text1, and so on
            seen[name] += 1
            name = f"{name[:6]}{seen[name]}"
        else:
            seen[name] = 0
        names.append(name)
    return names


def pe_section_flags(section):
    flags = 0
    if section.flags & CODE:
        flags |= CODE | 0x20000000                              # code, executable
    if section.flags & WRITABLE:
        flags |= INITIALISED_DATA | READABLE | WRITABLE
    elif not section.flags & CODE:
        flags |= INITIALISED_DATA | READABLE
    return flags


def decompress(stored):
    """A ROM-compressed file or section: u24 size, one u24 end offset per 4 KB block, then the blocks.

    Each block starts with u32 window bits, u32 size, 8 unused bytes, then an LZX stream.
    """
    size = int.from_bytes(stored[0:3], "little")
    blocks = (max(size, 1) - 1 >> 12) + 1
    ends = [int.from_bytes(stored[3 + 3 * i:6 + 3 * i], "little") for i in range(blocks)]
    start = 3 * (blocks + 1)
    out = bytearray()
    for end in ends:
        window_bits, block_size = struct.unpack_from("<II", stored, start)
        out += unlzx(stored[start + 16:end], block_size, window_bits)
        start = end
    if len(out) < size:
        raise ValueError(f"decompressed {len(out)} bytes, expected {size}")
    return bytes(out[:size])


def unlzx(stream, size, window_bits):
    """Decode one LZX stream (both sizes below 64 KB) with libarchive.

    libarchive reads LZX only inside a cabinet (.cab) file, so the smallest possible cabinet
    is built around the stream: Microsoft's CFHEADER, CFFOLDER, CFFILE and CFDATA records.
    """
    header_size, folder_size, lzx = 36, 8, 3
    file = struct.pack("<IIHHHH2s", size, 0, 0, 0, 0, 0, b"-")              # one file, named "-"
    data = struct.pack("<IHH", 0, len(stream), size) + stream               # one data block, no checksum
    data_offset = header_size + folder_size + len(file)
    folder = struct.pack("<IHH", data_offset, 1, lzx | window_bits << 8)    # one folder holding one block
    header = struct.pack("<4sIIIIIBBHHHHH", b"MSCF", 0, data_offset + len(data), 0,
                         header_size + folder_size, 0, 3, 1, 1, 1, 0, 0, 0) # version 1.3, 1 folder, 1 file
    with libarchive.memory_reader(header + folder + file + data, format_name="cab") as cabinet:
        return b"".join(part for entry in cabinet for part in entry.get_blocks())


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rom = Rom(open(sys.argv[1], "rb").read())
    target = sys.argv[2]

    if target == "--list":
        for module in rom.modules():
            print(f"module {module.size:9d}  {module.name}")
        for file in rom.files():
            print(f"file   {file.size:9d}  {file.name}{'  (compressed)' if file.stored_size != file.size else ''}")
        return

    for folder in ("modules", "files"):
        os.makedirs(os.path.join(target, folder), exist_ok=True)
    modules = files = 0
    for module in rom.modules():
        try:
            contents = rom.module_as_pe(module)
        except (ValueError, struct.error) as problem:
            print(f"module {module.name} skipped: {problem}")
            continue
        open(os.path.join(target, "modules", module.name), "wb").write(contents)
        modules += 1
    for file in rom.files():
        open(os.path.join(target, "files", file.name), "wb").write(rom.file_contents(file))
        files += 1
    print(f"{modules} modules and {files} files -> {target}")


if __name__ == "__main__":
    main()
