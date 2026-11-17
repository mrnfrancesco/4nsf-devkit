#!/usr/bin/env python3
"""List, decode or unpack a ".kbi" store of the unit's UI toolkit (screens, workflows, bitmaps, texts).

Usage: kbi_extract.py list    FILE.kbi [...]              one line per record: key, name, what it holds
       kbi_extract.py tree    FILE.kbi [KEY ...]          the node trees (screens, workflows, ...) as indented text
       kbi_extract.py extract FILE.kbi OUTDIR [KEY ...]   one file per record: bitmaps as .png, the rest as it is

Most stores sit inside the .nfs containers of VwUserShell, VwBrandDevice, MediaPlayer and
PictureViewer; unpack those first:  nfs_extract.py extract FILE.nfs OUTDIR  (not inside firmware/).

A store maps numeric keys to values through a small index tree:

  0x00  header and description of the tree (`kbi.header` below)
  0x56  root index node: u8 level, then slots {first key, last key, u32 offset, u32 size, u32 size}
          level 1 slots point to data pages, higher levels to index nodes; size = bytes + 1
        data page: u8 0, then records {key, size, value}, keys ascending
  key 0x10000000 | id holds "<id>,<Name>.dat", the designer's name of record id

What a value is depends on the store: a node tree (Controls, Workflows, Startup, Shutdown,
Datastorage, Fonts), a PNXF image (Bitmaps), a colour (Colors), a text (<language>).

  node  u16 type, u16 key, then   type 1 int32 · type 2 float32 · type 3 bool (u32)
                                  type 4 string: u16 size, UTF-8 bytes (NUL-padded to 4)
                                  others object: u16 class id, u16 size, child nodes
  PNXF  header (`kbi.pnxf` below), palette (RGB565), one alpha byte per palette entry (if flagged),
        pixels (u8 or u16 palette indices, rows top-down). In a .res archive everything after
        the header is one zlib stream behind a u32 size; pnxf_image() reads both.

Little endian, no checksums. Read-only: there is no repacker. PNG output uses Pillow.
Full layout and the meaning of node types and class ids: docs/system/ui-framework.md.
"""
import os
import re
import struct
import sys
import zlib
from collections import namedtuple

from dissect.cstruct import cstruct
from PIL import Image

MAGIC = b"tnatxes\0"                # "sextant" backwards
NAME_FLAG = 0x10000000

kbi = cstruct("""
struct header {
    char    magic[8];
    uint8   unknown_08[18];         // 16 x 0, then 00 ff
    uint8   junk[15];               // stale memory of the build tool
    uint8   version;                // 3
    uint64  key_min;                // smallest and largest key stored
    uint64  key_max;
    uint32  page_size;
    uint32  index_nodes;
    uint32  unknown_42;
    uint32  data_pages;
    uint32  size_width;             // bytes of a record's size field: 1, 2 or 3
    uint32  key_width;              // bytes of a key: 1, 2 or 4
    uint32  root_size;              // size field of the root index node, which follows
};

struct slot {                       // of an index node, after the first and last key of the child
    uint32  offset;                 // of the child, from the start of the file
    uint32  size;                   // of the child, plus 1
    uint32  size_again;
};

struct pnxf {
    char    magic[4];               // "PNXF"
    uint16  stride;                 // pixels per stored row
    uint16  height;
    uint16  width;                  // visible pixels per row
    uint16  colours;                // palette entries; more than 256 = 16-bit pixels
    uint32  alpha;                  // 1 = alpha table present
};
""")

# Node types: 1-4 are plain values, every other type is an object {class id, children}.
VALUES = {1: "int", 2: "float", 3: "bool", 4: "str"}
OBJECTS = {5: "ctl", 6: "bmp", 7: "cmd", 8: "then", 9: "on", 10: "bind", 11: "source", 12: "txt",
           15: "font", 17: "item", 18: "upd", 19: "prov", 21: "list", 22: "row", 24: "handler",
           25: "filter", 29: "fx", 33: "wf"}
# Class ids of the controls (type 5) of the framework core.
CONTROLS = {1: "container", 2: "list box", 3: "bitmap", 5: "button", 9: "include", 11: "label",
            12: "item set", 13: "row", 18: "gauge", 20: "value"}

Node = namedtuple("Node", "type key id value")          # id: None for plain values; value: list of Node for objects


class KbiError(Exception):
    pass


def read_store(data):
    """({key: value}, {key: name}, header) of a store. Walks the whole index tree and checks it."""
    if len(data) < len(kbi.header) or data[:8] != MAGIC:
        raise KbiError("bad magic")
    header = kbi.header(data)
    if header.version != 3:
        raise KbiError(f"unsupported version {header.version}")
    key_width, size_width = header.key_width, header.size_width
    slot_size = 2 * key_width + len(kbi.slot)
    records, pages = {}, []

    def number(position, width):
        return int.from_bytes(data[position:position + width], "little")

    def walk(offset, size, keys):
        end = offset + size - 1
        if end > len(data):
            raise KbiError(f"node at 0x{offset:x} ends beyond the file")
        level, position = data[offset], offset + 1
        if level == 0:                                  # data page
            pages.append(offset)
            found = []
            while position < end:
                key, value_size = number(position, key_width), number(position + key_width, size_width)
                position += key_width + size_width
                if key in records:
                    raise KbiError(f"duplicate key 0x{key:x}")
                records[key] = data[position:position + value_size]
                position += value_size
                found.append(key)
            if position != end or (keys and (found[0], found[-1]) != keys):
                raise KbiError(f"page at 0x{offset:x}: records do not match its size or key range")
            return
        if (size - 1) % slot_size:
            raise KbiError(f"index node at 0x{offset:x}: odd size")
        for position in range(position, end, slot_size):
            first, last = number(position, key_width), number(position + key_width, key_width)
            slot = kbi.slot(data[position + 2 * key_width:position + slot_size])
            if slot.offset == 0 and slot.size == 0:     # unused slot of a fixed-size node
                continue
            if slot.size != slot.size_again:
                raise KbiError(f"child at 0x{slot.offset:x} is compressed: not supported")
            if data[slot.offset] != level - 1:
                raise KbiError(f"child at 0x{slot.offset:x} is not one level below its parent")
            walk(slot.offset, slot.size, (first, last))

    walk(len(kbi.header), header.root_size, None)
    if records and (min(records), max(records)) != (header.key_min, header.key_max):
        raise KbiError("key range differs from the header")
    if len(pages) != header.data_pages:
        raise KbiError("number of pages differs from the header")

    names = {}
    for key in [key for key in records if key & NAME_FLAG]:
        name = records.pop(key).rstrip(b"\0").decode("utf-8", "replace").partition(",")[2]
        names[key & ~NAME_FLAG] = name[:-4] if name.endswith(".dat") else name
    return records, names, header


def parse_nodes(data):
    """The node tree in `data` as a list of Node. Raises ValueError if it is not one."""
    nodes, position = [], 0
    while position < len(data):
        if position + 8 > len(data):
            raise ValueError("truncated node")
        kind, key = struct.unpack_from("<HH", data, position)
        if kind == 0:
            raise ValueError("node type 0")
        if kind == 1:
            value, size = struct.unpack_from("<i", data, position + 4)[0], 8
        elif kind == 2:
            value, size = struct.unpack_from("<f", data, position + 4)[0], 8
        elif kind == 3:
            value, size = struct.unpack_from("<I", data, position + 4)[0], 8
            if value > 1:
                raise ValueError("bool that is not 0 or 1")
            value = bool(value)
        elif kind == 4:
            size = 6 + struct.unpack_from("<H", data, position + 4)[0]
            if position + size > len(data) or size % 4:
                raise ValueError("bad string")
            value = data[position + 6:position + size].split(b"\0", 1)[0].decode("utf-8", "replace")
        else:
            class_id, size = struct.unpack_from("<HH", data, position + 4)
            size += 8
            if position + size > len(data):
                raise ValueError("object larger than its parent")
            nodes.append(Node(kind, key, class_id, parse_nodes(data[position + 8:position + size])))
            position += size
            continue
        nodes.append(Node(kind, key, None, value))
        position += size
    return nodes


def node_name(node):
    return OBJECTS.get(node.type) or VALUES.get(node.type) or f"t{node.type}"


def print_tree(nodes, depth=1):
    indent = "  " * depth
    for node in nodes:
        if node.id is None:
            value = node.value
            if node.type == 1:
                value = f"{value}" if -10 < value < 10 else f"{value} (0x{value & 0xffffffff:x})"
            elif node.type == 3:
                value = "true" if value else "false"
            elif node.type == 4:
                value = repr(value)
            print(f"{indent}{node_name(node)}[{node.key}] = {value}")
            continue
        control = CONTROLS.get(node.id) if node.type == 5 else None
        line = f"{indent}{node_name(node)}[{node.key}] #{node.id}{' ' + control if control else ''} {{"
        if not node.value:
            print(line + "}")
            continue
        print(line)
        print_tree(node.value, depth + 1)
        print(indent + "}")


def count_nodes(nodes):
    return sum(1 + (count_nodes(node.value) if node.id is not None else 0) for node in nodes)


def pnxf_image(data):
    """A PNXF bitmap (as stored in a .kbi, or zlib-packed as in a .res archive) as a Pillow image."""
    header = kbi.pnxf(data)
    if header.magic != b"PNXF" or header.alpha > 1:
        raise ValueError("not a PNXF image")
    colours, pixels = header.colours, header.stride * header.height
    index_size = 2 if colours > 256 else 1
    body = data[len(kbi.pnxf):]
    expected = colours * (3 if header.alpha else 2) + pixels * index_size
    if len(body) != expected:                           # .res archives: u32 size, zlib stream
        body = zlib.decompress(body[4:])
        if len(body) != expected or struct.unpack_from("<I", data, len(kbi.pnxf))[0] != expected:
            raise ValueError("PNXF size mismatch")

    palette = Image.frombytes("RGB", (colours, 1), body[:2 * colours], "raw", "BGR;16").convert("RGBA")
    position = 2 * colours
    if header.alpha:
        palette.putalpha(Image.frombytes("L", (colours, 1), body[position:position + colours]))
        position += colours
    palette = palette.tobytes()
    indices = body[position:] if index_size == 1 else struct.unpack_from(f"<{pixels}H", body, position)
    image = Image.frombytes("RGBA", (header.stride, header.height),
                            b"".join(palette[4 * index:4 * index + 4] for index in indices))
    return image.crop((0, 0, header.width or header.stride, header.height))


def kind_of(path, value):
    """What a record holds: image, colour, nodes, text or blob."""
    if value[:4] == b"PNXF":
        return "image"
    if os.path.basename(path).startswith("Colors") and len(value) == 4:
        return "colour"
    if len(value) >= 8 and len(value) % 4 == 0:
        try:
            parse_nodes(value)
            return "nodes"
        except (ValueError, struct.error):
            pass
    if value.endswith(b"\0") and b"\0" not in value[:-1]:
        try:
            value.decode("utf-8")
            return "text"
        except UnicodeDecodeError:
            pass
    return "blob"


def describe(kind, value):
    """One record as the text after its key and name in a listing."""
    if kind == "image":
        header = kbi.pnxf(value)
        return (f"PNXF {header.width}x{header.height} colours={header.colours}"
                f"{' alpha' if header.alpha else ''} bytes={len(value)}")
    if kind == "colour":                                # stored as blue, green, red, 0
        return f"rgb #{value[2]:02x}{value[1]:02x}{value[0]:02x}" + (f" (byte3=0x{value[3]:x})" if value[3] else "")
    if kind == "text":
        return repr(value[:-1].decode("utf-8"))
    if kind == "nodes":
        tree = parse_nodes(value)
        roots = " ".join(f"{node_name(node)}#{'' if node.id is None else node.id}" for node in tree[:4])
        return f"nodes={count_nodes(tree)} bytes={len(value)} root={roots}"
    return f"blob[{len(value)}] {value[:32].hex()}{'…' if len(value) > 32 else ''}"


def show(paths, keys=None, trees=False):
    failed = False
    for path in paths:
        try:
            records, names, header = read_store(open(path, "rb").read())
        except (KbiError, OSError) as problem:
            print(f"{path}: ERROR {problem}")
            failed = True
            continue
        print(f"== {path}  records={len(records)} named={len(names)} key_width={header.key_width} "
              f"size_width={header.size_width} pages={header.data_pages}")
        for key in sorted(records):
            if keys and key not in keys:
                continue
            value = records[key]
            title = f"{key}" + (f' "{names[key]}"' if key in names else "")
            kind = kind_of(path, value)
            if trees and kind == "nodes":
                print(f"== {title}  ({len(value)} bytes)")
                print_tree(parse_nodes(value))
            else:
                print(f"{title}  {describe(kind, value)}")
    return 1 if failed else 0


def extract(path, target, keys=None):
    target = os.path.realpath(target)
    if os.sep + "firmware" + os.sep in target + os.sep:
        raise SystemExit("refusing to extract inside firmware/")
    records, names, _ = read_store(open(path, "rb").read())
    os.makedirs(target, exist_ok=True)
    written = images = 0
    for key in sorted(records):
        if keys and key not in keys:
            continue
        value = records[key]
        written += 1
        # bitmaps are named after the designer's file: "D:\Work\...\Back.png"
        name = re.sub(r"[^A-Za-z0-9_.-]", "_", names.get(key, "").replace("\\", "/").split("/")[-1])[:60]
        file = os.path.join(target, f"{key}_{name}" if name else f"{key}")
        if value[:4] == b"PNXF":
            pnxf_image(value).save((file[:-4] if file.lower().endswith(".png") else file) + ".png")
            images += 1
        else:
            open(file + ".bin", "wb").write(value)
    print(f"extracted {written} records ({images} as .png) to {target}")
    return 0


def main(arguments):
    command, arguments = (arguments[0], arguments[1:]) if arguments else (None, [])
    if command == "list" and arguments:
        return show(arguments)
    paths = {"tree": 1, "extract": 2}.get(command, 0)   # the arguments after these are keys
    if paths and len(arguments) >= paths and all(key.isdigit() for key in arguments[paths:]):
        keys = {int(key) for key in arguments[paths:]}
        if command == "tree":
            return show(arguments[:1], keys, trees=True)
        return extract(arguments[0], arguments[1], keys)
    print(__doc__)
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except BrokenPipeError:
        pass
