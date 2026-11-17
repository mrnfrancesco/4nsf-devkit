#!/usr/bin/env python3
"""Print a Windows CE 6 registry hive as text, in the style of a .reg file.

Usage: hive_dump.py firmware/rom/files/default.hv     (also boot.hv, user.hv; made by `make firmware`)

A hive is a header ("EKIM" at offset 8) and, from 0x5000 to the end of the file, cells back to back:

  cell   u32 type (top 4 bits) and size of the contents, u32 0, u32 id, contents
  type 0xB  root:   first key of HKEY_CLASSES_ROOT, of HKEY_CURRENT_USER, of HKEY_LOCAL_MACHINE
  type 0xC  key:    next key on the same level, first key below, first value, name
  type 0xD  value:  next value of the key, registry type, name, data
  other types (0x2 = table of all ids) are not needed

A reference to a cell is 0x20000000 | id, 0 = none. Little endian, names are UTF-16.
The structures are spelled out below and read with dissect.cstruct. Keys and values are
printed sorted by name. Format worked out from the three hives of the unit; more in
docs/system/hardware-layer.md.
"""
import sys

from dissect.cstruct import cstruct

CELLS = 0x5000                      # where the cells start
ROOT, KEY, VALUE = 0xB, 0xC, 0xD    # cell types
ROOTS = ("HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE")
SZ, EXPAND_SZ, DWORD, MULTI_SZ = 1, 2, 4, 7

hive = cstruct("""
struct cell {
    uint32  size : 28;              // of the contents
    uint32  type : 4;
    uint32  zero;
    uint32  id;
};

struct root {
    uint32  first_key[3];
};

struct key {
    uint32  next;
    uint32  first_child;
    uint32  first_value;
    uint32  name_size;              // in characters
    wchar   name[name_size];
};

struct value {
    uint32  next;
    uint16  type;                   // REG_SZ = 1, REG_DWORD = 4, ...
    uint16  data_size;              // in bytes
    uint16  name_size;              // in characters
    wchar   name[name_size];
    char    data[data_size];
};
""")


def read_cells(data):
    """(first key of each root, {id: key or value}) of a hive."""
    if data[8:12] != b"EKIM":
        raise SystemExit("not a Windows CE hive")
    roots, cells, position = None, {}, CELLS
    while position + len(hive.cell) <= len(data):
        cell = hive.cell(data[position:position + len(hive.cell)])
        if cell.size == 0:
            break
        contents = data[position + len(hive.cell):position + len(hive.cell) + cell.size]
        if cell.type == ROOT:
            roots = hive.root(contents).first_key
        elif cell.type == KEY:
            cells[cell.id] = hive.key(contents)
        elif cell.type == VALUE:
            cells[cell.id] = hive.value(contents)
        position += len(hive.cell) + cell.size
    if roots is None:
        raise SystemExit("no root cell found")
    return roots, cells


def chain(cells, reference):
    """The cells of a list: `reference` names the first one, each cell the next one."""
    while reference:
        cell = cells[reference & 0x0FFFFFFF]
        yield cell
        reference = cell.next


def text(value):
    data = value.data
    if value.type in (SZ, EXPAND_SZ):
        return '"' + data.decode("utf-16-le", "replace").rstrip("\0") + '"'
    if value.type == MULTI_SZ:
        strings = data.decode("utf-16-le", "replace").rstrip("\0").split("\0")
        return "multi_sz:" + ",".join(f'"{string}"' for string in strings)
    if value.type == DWORD and len(data) == 4:
        return f"dword:{int.from_bytes(data, 'little'):08x}"
    return f"hex({value.type}):" + data.hex(",")


def print_keys(cells, reference, path):
    for key in sorted(chain(cells, reference), key=lambda key: key.name.lower()):
        print(f"\n[{path}\\{key.name}]")
        for value in sorted(chain(cells, key.first_value), key=lambda value: value.name.lower()):
            print(f"  {'@' if not value.name else chr(34) + value.name + chr(34)} = {text(value)}")
        print_keys(cells, key.first_child, f"{path}\\{key.name}")


def main(arguments):
    if len(arguments) != 1:
        print(__doc__)
        return 2
    roots, cells = read_cells(open(arguments[0], "rb").read())
    keys = sum(hasattr(cell, "first_child") for cell in cells.values())
    print(f"; {arguments[0]}: {keys} keys, {len(cells) - keys} values")
    for name, first_key in zip(ROOTS, roots):
        print_keys(cells, first_key, name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
