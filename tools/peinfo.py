#!/usr/bin/env python3
"""Summarise Windows CE programs and DLLs: CPU type, subsystem, imports, exports.

Usage: .venv/bin/python tools/peinfo.py [-e N] FILE...     (-e N: list the first N exports, default 0)
Built on the `pefile` library; for a full dump use  .venv/bin/python -m pefile FILE
"""
import sys
import time

import pefile

MACHINE = {0x1C0: "ARM", 0x1C2: "ARM Thumb", 0x1C4: "ARM Thumb-2", 0x14C: "x86"}
SUBSYSTEM = {9: "Windows CE GUI", 2: "Windows GUI", 3: "console"}


def summarise(path, n_exports):
    pe = pefile.PE(path)
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    print(f"== {path}")
    print(f"   cpu       {MACHINE.get(fh.Machine, 'unknown')} ({fh.Machine:#x})")
    print(f"   subsystem {SUBSYSTEM.get(oh.Subsystem, oh.Subsystem)} "
          f"{oh.MajorSubsystemVersion}.{oh.MinorSubsystemVersion}, "
          f"linker {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}, "
          f"built {time.strftime('%Y-%m-%d', time.gmtime(fh.TimeDateStamp))}")
    print(f"   sections  {' '.join(s.Name.rstrip(bytes(1)).decode('latin1') for s in pe.sections)}")
    for imp in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        names = [i.name.decode() for i in imp.imports if i.name]
        how = f"{len(names)} by name" if names else "all by ordinal"
        print(f"   imports   {imp.dll.decode()}: {len(imp.imports)} ({how}) {', '.join(names[:6])}")
    exp = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    if exp:
        names = [s.name.decode() for s in exp.symbols if s.name]
        mangled = sum(n.startswith("?") for n in names)
        print(f"   exports   {len(exp.symbols)} ({len(names)} named, {mangled} C++ mangled)")
        for n in names[:n_exports]:
            print(f"             {n}")


def main(argv):
    n = 0
    if argv[:1] == ["-e"]:
        n, argv = int(argv[1]), argv[2:]
    if not argv:
        print(__doc__)
        return 2
    for path in argv:
        summarise(path, n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
