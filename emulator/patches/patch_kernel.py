#!/usr/bin/env python3
"""Adapt the generic Windows Embedded Compact 7 kernel image to the 4NSF software.

Usage: patch_kernel.py IN.raw OUT.raw

Three changes, all in place (the image is a flat memory dump with no checksum). They are
specific to the Milek7/wince-qemu image of 2022-04-17: every word is checked before it is
replaced, so a different image fails loudly instead of being corrupted.

1. Disk name. The unit's software hardcodes "\\My Flash Disk\\..." paths; the image mounts
   its disk as "\\Mounted Volume". The new name is one character shorter, so it fits.

2. Display. The kernel reports a fixed 1024x600 mode and programs the display controller
   (PL111) from constants. The unit's screen is 480x272 and its skins are made for that.

3. Timer bug. The image's mmtimer.dll only initialises itself in timeSetEvent; calling
   timeKillEvent first jumps through a null pointer. VwUserShell.exe does that (harmless on
   the unit's Windows CE 6). timeKillEvent is rerouted through a 7-instruction guard, placed
   in the section's zero padding, that returns MMSYSERR_INVALPARAM while uninitialised.
"""
import struct
import sys

LOAD_ADDRESS = 0x80000000       # the image's first byte lives here in the guest's memory

OLD_DISK_NAME = "Mounted Volume"
NEW_DISK_NAME = "My Flash Disk"

# (guest address, expected instruction, replacement)
DISPLAY_480x272 = [
    (0x80002134, 0xE3A08996, 0xE3A08702),   # frame buffer size 0x258000 -> 0x80000
    (0x80002138, 0xE3A09A01, 0xE3A09D1E),   # bytes per line 4096 -> 1920
    (0x80002148, 0xE3A02B01, 0xE3A02E1E),   # width 1024 -> 480
    (0x8000214C, 0xE3A01F96, 0xE3A01E11),   # height 600 -> 272
    (0x800021BC, 0xE3A020FC, 0xE3A02074),   # PL111 timing 0: pixels per line
    (0x800021C4, 0xE3001259, 0xE300110F),   # PL111 timing 1: lines per panel
    (0x8000222C, 0xE3A03B01, 0xE3A03E1E),   # width, second query
    (0x80002230, 0xE3A02F96, 0xE3A02E11),   # height, second query
]

MMTIMER = LOAD_ADDRESS + 0x85F000           # where mmtimer.dll's code sits in the image
TIMER_GUARD = [
    (MMTIMER + 0x0B4C, 0xE92D4FF0, 0xEA0001EB),     # timeKillEvent:  b guard
    (MMTIMER + 0x1300, 0x00000000, 0xE59F3014),     # guard: ldr r3, =initialised_flag
    (MMTIMER + 0x1304, 0x00000000, 0xE5933000),     #        ldr r3, [r3]
    (MMTIMER + 0x1308, 0x00000000, 0xE3530000),     #        cmp r3, #0
    (MMTIMER + 0x130C, 0x00000000, 0x03A0000B),     #        moveq r0, #MMSYSERR_INVALPARAM
    (MMTIMER + 0x1310, 0x00000000, 0x012FFF1E),     #        bxeq lr
    (MMTIMER + 0x1314, 0x00000000, 0xE92D4FF0),     #        push {r4-r11, lr}   (the displaced instruction)
    (MMTIMER + 0x1318, 0x00000000, 0xEAFFFE0C),     #        b timeKillEvent+4
    (MMTIMER + 0x131C, 0x00000000, 0x404833B4),     #        address of initialised_flag
]


def replace_words(image, table, what):
    for address, expected, replacement in table:
        offset = address - LOAD_ADDRESS
        found, = struct.unpack_from("<I", image, offset)
        if found != expected:
            sys.exit(f"{what}: found {found:#010x} at {address:#x}, expected {expected:#010x}. "
                     "Is this the 2022-04-17 image?")
        struct.pack_into("<I", image, offset, replacement)


def rename_disk(image):
    old = OLD_DISK_NAME.encode("utf-16le")
    new = NEW_DISK_NAME.encode("utf-16le").ljust(len(old), b"\0")
    count = image.count(old)
    if not count:
        sys.exit(f'disk name "{OLD_DISK_NAME}" not found in the image')
    image[:] = image.replace(old, new)
    return count


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source, target = sys.argv[1:]
    image = bytearray(open(source, "rb").read())
    replace_words(image, DISPLAY_480x272, "display")
    replace_words(image, TIMER_GUARD, "timer guard")
    renamed = rename_disk(image)
    open(target, "wb").write(image)
    print(f"kernel patched: display 480x272, timer guard, disk name ({renamed} places)")


if __name__ == "__main__":
    main()
