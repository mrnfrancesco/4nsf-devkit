#!/usr/bin/env python3
"""Point the imports that the emulator's Windows cannot satisfy at a stub DLL.

Usage: redirect_imports.py --kernel DIR --unit DIR --stub STUB.dll PROGRAM...
       redirect_imports.py --only NAME[,NAME...] --stub STUB.dll PROGRAM...

  --kernel DIR   system DLLs of the emulator's Windows   (emulator/work/kernel/rom/modules)
  --unit DIR     system DLLs of the unit's Windows       (firmware/rom/modules)
  --stub FILE    DLL that provides the missing functions (emulator/work/stubs/cestub.dll)
  --only NAMES   instead: take exactly these functions (imported by name) from STUB.dll, whether
                 the system has them or not; the old slots keep their requests. apps/navit uses
                 it to put its own waveOutWrite in front of Navit's (apps/navit/navitsnd.c).

The unit's programs were built for Windows CE 6 and ask its COREDLL for a few functions
that Windows Embedded Compact 7 no longer exports. A program with an unresolved import
does not start, so each PROGRAM (exe or dll) is rewritten to take those from the stub.

Background. A program's import table has one descriptor per DLL. A descriptor points to an
array of slots; at load time Windows writes the address of each wanted function into its
slot, and the code calls through the slots. A slot names its function by number (ordinal)
or by name.

For every slot whose function the emulator's version of the DLL lacks:

 1. the slot is changed to ask for a function that does exist, so that loading succeeds;
 2. a new descriptor for the stub DLL, with a slot of its own, is added. The descriptor
    table cannot grow where it is, so the whole table moves to a new ".shim" section;
 3. every reference to the old slot inside the program is changed to the new slot.

Step 3 is needed because Windows CE resolves imports only through the slots themselves: a
slot cannot simply be reassigned to another DLL.

Programs are replaced, never modified in place (they may be hard links to firmware/).
"""
import argparse
import os
import struct
import sys

import pefile

DESCRIPTOR_SIZE = 20                # IMAGE_IMPORT_DESCRIPTOR
SECTION_HEADER_SIZE = 40
BY_ORDINAL = 0x80000000             # flag in a slot: "the low 16 bits are an ordinal"
SHIM_SECTION_FLAGS = 0xC0000040     # initialised data, readable, writable


def load(path, directory):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories([pefile.DIRECTORY_ENTRY[directory]])
    return pe


def exports(path):
    """{ordinal: name or None} of everything a DLL exports."""
    table = getattr(load(path, "IMAGE_DIRECTORY_ENTRY_EXPORT"), "DIRECTORY_ENTRY_EXPORT", None)
    if not table:
        return {}
    return {s.ordinal: s.name.decode() if s.name else None for s in table.symbols if s.address}


def exports_of_all(folder):
    """{dll name in lower case: exports} for every DLL in a folder."""
    result = {}
    for name in os.listdir(folder):
        if name.lower().endswith((".dll", ".cpl")):
            try:
                result[name.lower()] = exports(os.path.join(folder, name))
            except pefile.PEFormatError:
                pass
    return result


def align(value, boundary):
    return (value + boundary - 1) // boundary * boundary


def pad(blob, boundary):
    blob.extend(bytes(-len(blob) % boundary))


class Redirector:
    def __init__(self, kernel, unit, stub_name, only=()):
        self.kernel = kernel            # what the emulator's Windows offers
        self.unit = unit                # what the unit's Windows offers: names for ordinals
        self.stub_name = stub_name
        self.only = set(only)           # if given: exactly these functions, by name

    def missing_slots(self, pe):
        """Yield (file offset of the slot's request, address of the slot, function name,
        ordinal to request in the old slot instead, or None to leave the request)."""
        base = pe.OPTIONAL_HEADER.ImageBase
        for descriptor in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            dll = descriptor.dll.decode().lower()
            if self.only:
                for slot in descriptor.imports:
                    if slot.name and slot.name.decode() in self.only:
                        yield None, slot.address, slot.name.decode(), None
                continue
            if dll not in self.kernel:
                continue                # not a system DLL: it ships next to the program
            available = self.kernel[dll]
            available_names = set(available.values())
            # the requests are in the lookup table if there is one, else in the slots themselves
            requests = descriptor.struct.OriginalFirstThunk or descriptor.struct.FirstThunk
            for slot in descriptor.imports:
                if slot.import_by_ordinal:
                    if slot.ordinal in available:
                        continue
                    name = self.unit.get(dll, {}).get(slot.ordinal) or f"{dll}_ord{slot.ordinal}"
                else:
                    name = slot.name.decode()
                    if name in available_names:
                        continue
                index = (slot.address - base - descriptor.struct.FirstThunk) // 4
                yield pe.get_offset_from_rva(requests) + 4 * index, slot.address, name, min(available)

    def redirect(self, path):
        """Rewrite one program. Returns the names of the functions now taken from the stub."""
        pe = load(path, "IMAGE_DIRECTORY_ENTRY_IMPORT")
        missing = list(self.missing_slots(pe))
        if not missing:
            return []
        data = bytearray(pe.__data__)
        optional, last = pe.OPTIONAL_HEADER, pe.sections[-1]
        directory = optional.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]]

        # The new section follows the last one, so its address is known before its contents are.
        shim_rva = align(last.VirtualAddress + max(last.Misc_VirtualSize, last.SizeOfRawData), optional.SectionAlignment)

        # Its contents: the descriptor table (the old descriptors, one more per function, a
        # terminator), then the stub's name, then each function's name, request and slot.
        old_table = self.read_descriptors(data, pe.get_offset_from_rva(directory.VirtualAddress))
        if len(old_table) != len(pe.DIRECTORY_ENTRY_IMPORT):
            raise ValueError("import table not fully understood")
        table = bytearray()
        for lookup, name, slots in old_table:
            table += struct.pack("<IIIII", lookup, 0, 0, name, slots)
        table_size = DESCRIPTOR_SIZE * (len(old_table) + len(missing) + 1)
        rest_rva = shim_rva + table_size
        rest = bytearray(self.stub_name.encode() + b"\0")
        for request_offset, old_slot, function, existing_ordinal in missing:
            pad(rest, 4)
            name_rva = rest_rva + len(rest)
            rest += b"\0\0" + function.encode() + b"\0"        # hint (unused) + name
            pad(rest, 4)
            lookup_rva = rest_rva + len(rest)
            slot_rva = lookup_rva + 8
            rest += struct.pack("<IIII", name_rva, 0, name_rva, 0)      # request + terminator, slot + terminator
            table += struct.pack("<IIIII", lookup_rva, 0, 0, rest_rva, slot_rva)

            if existing_ordinal is not None:
                struct.pack_into("<I", data, request_offset, BY_ORDINAL | existing_ordinal)  # step 1
            if not self.repoint(pe, data, old_slot, optional.ImageBase + slot_rva):         # step 3
                print(f"  warning: {function}: nothing in {os.path.basename(path)} refers to its slot")

        self.append_section(pe, data, b".shim", shim_rva, table + bytes(DESCRIPTOR_SIZE) + rest)    # step 2
        struct.pack_into("<II", data, directory.get_file_offset(), shim_rva, table_size)

        replacement = path + ".new"
        with open(replacement, "wb") as f:
            f.write(data)
        os.replace(replacement, path)
        return [function for _, _, function, _ in missing]

    @staticmethod
    def read_descriptors(data, offset):
        """[(lookup table rva, dll name rva, slots rva)] of the existing import table."""
        table = []
        while True:
            lookup, _, _, name, slots = struct.unpack_from("<IIIII", data, offset)
            if not name and not slots:
                return table
            table.append((lookup, name, slots))
            offset += DESCRIPTOR_SIZE

    @staticmethod
    def append_section(pe, data, name, rva, contents):
        """Add a section at the end of the file, at the given address."""
        header, optional = pe.FILE_HEADER, pe.OPTIONAL_HEADER
        pad(data, optional.FileAlignment)
        offset = len(data)
        data += contents
        pad(data, optional.FileAlignment)

        entry = pe.sections[-1].get_file_offset() + SECTION_HEADER_SIZE
        if entry + SECTION_HEADER_SIZE > optional.SizeOfHeaders:
            raise ValueError("no room for another section header")
        struct.pack_into("<8sIIIIIIHHI", data, entry, name, len(contents), rva,
                         len(data) - offset, offset, 0, 0, 0, 0, SHIM_SECTION_FLAGS)
        struct.pack_into("<H", data, header.get_field_absolute_offset("NumberOfSections"),
                         header.NumberOfSections + 1)
        struct.pack_into("<I", data, optional.get_field_absolute_offset("SizeOfImage"),
                         rva + align(len(contents), optional.SectionAlignment))

    @staticmethod
    def repoint(pe, data, old_address, new_address):
        """Replace every aligned occurrence of an address in the original sections."""
        old, new = struct.pack("<I", old_address), struct.pack("<I", new_address)
        count = 0
        for section in pe.sections:
            end = section.PointerToRawData + section.SizeOfRawData
            position = data.find(old, section.PointerToRawData, end)
            while position >= 0:
                if position % 4 == 0:
                    data[position:position + 4] = new
                    count += 1
                position = data.find(old, position + 1, end)
        return count


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--kernel")
    parser.add_argument("--unit")
    parser.add_argument("--only", type=lambda names: names.split(","), default=[])
    parser.add_argument("--stub", required=True)
    parser.add_argument("programs", nargs="+")
    args = parser.parse_args()
    if not args.only and not (args.kernel and args.unit):
        parser.error("give --kernel and --unit, or --only")

    redirector = Redirector(exports_of_all(args.kernel) if args.kernel else {},
                            exports_of_all(args.unit) if args.unit else {},
                            os.path.basename(args.stub), args.only)
    needed = set()
    for path in args.programs:
        functions = redirector.redirect(path)
        if functions:
            print(f"{path}: {', '.join(functions)}")
            needed.update(functions)

    lacking = sorted(needed - set(exports(args.stub).values()))
    if lacking:
        sys.exit(f"{args.stub} does not export: {' '.join(lacking)}"
                 + ("" if args.only else "\nAdd them to emulator/stubs/cestub.c"))


if __name__ == "__main__":
    main()
