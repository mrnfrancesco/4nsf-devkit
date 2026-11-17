#!/usr/bin/env python3
"""List or unpack a ".RGBFS" boot animation (firmware/flash/ChangeStartupScreens/*/SPLASH.RGBFS).

Usage: rgbfs_extract.py list    FILE.RGBFS [...]      timing, and a check of every frame
       rgbfs_extract.py extract FILE.RGBFS OUTDIR     frame_NN.png (OUTDIR not inside firmware/)

An animation is an intro, played once, and a loop, repeated until the menu is ready:

  0x00  header (`rgbfs.header` below)
  0x20  the intro frames, then the loop frames. Each one: "IM", u32 size in 16-bit words, words

A frame is 480x272 pixels, RGB565, rows from the top. With compression 1 the word 0x3412
is followed by a count and the pixel to repeat; every other word is one pixel.

Little endian. Format read from SplashAnimation.exe; more in docs/system/brand-and-pairing.md.
PNG output uses Pillow.
"""
import os
import struct
import sys

from dissect.cstruct import cstruct
from PIL import Image

WIDTH, HEIGHT = 480, 272
REPEAT = 0x3412

rgbfs = cstruct("""
struct header {
    uint32  size;                   // of this header: 0x20
    char    magic[2];               // "SA"
    uint8   version;                // 1
    uint8   compression;            // 0 = none, 1 = runs of equal pixels
    uint32  intro_frames;
    uint32  intro_ms;               // duration of the whole intro
    uint32  intro_fade_steps;       // cross-fade steps between two frames
    uint32  loop_frames;
    uint32  loop_ms;                // duration of one pass of the loop
    uint32  loop_fade_steps;
};

struct frame {
    char    magic[2];               // "IM"
    uint32  words;
};
""")


class RgbfsError(Exception):
    pass


def read_animation(data):
    """(header, frames) of an animation; a frame is a sequence of WIDTH * HEIGHT RGB565 pixels."""
    if len(data) < len(rgbfs.header):
        raise RgbfsError("too short")
    header = rgbfs.header(data)
    if header.size != len(rgbfs.header) or header.magic != b"SA" or header.version != 1:
        raise RgbfsError("not an RGBFS file")
    frames, position = [], header.size
    for number in range(header.intro_frames + header.loop_frames):
        frame = rgbfs.frame(data[position:position + len(rgbfs.frame)])
        if frame.magic != b"IM":
            raise RgbfsError(f"frame {number}: no IM tag at 0x{position:x}")
        position += len(rgbfs.frame)
        words = struct.unpack_from(f"<{frame.words}H", data, position)
        position += 2 * frame.words
        pixels = words
        if header.compression:
            pixels, words = [], iter(words)
            for word in words:
                if word == REPEAT:
                    count = next(words)
                    pixels.extend([next(words)] * count)
                else:
                    pixels.append(word)
        if len(pixels) != WIDTH * HEIGHT:
            raise RgbfsError(f"frame {number}: {len(pixels)} pixels instead of {WIDTH * HEIGHT}")
        frames.append(pixels)
    if position != len(data):
        raise RgbfsError(f"{len(data) - position} bytes after the last frame")
    return header, frames


def list_animations(paths):
    failed = False
    for path in paths:
        try:
            header, frames = read_animation(open(path, "rb").read())
        except (RgbfsError, StopIteration, struct.error, OSError) as problem:
            print(f"{path}: ERROR {problem!r}")
            failed = True
            continue
        print(f"== {path}  compression={header.compression} frames={len(frames)} size={WIDTH}x{HEIGHT}")
        print(f"   intro {header.intro_frames} frames in {header.intro_ms} ms, {header.intro_fade_steps} fade steps")
        print(f"   loop  {header.loop_frames} frames in {header.loop_ms} ms, {header.loop_fade_steps} fade steps")
    return 1 if failed else 0


def extract(path, target):
    target = os.path.realpath(target)
    if os.sep + "firmware" + os.sep in target + os.sep:
        raise SystemExit("refusing to extract inside firmware/")
    _, frames = read_animation(open(path, "rb").read())
    os.makedirs(target, exist_ok=True)
    for number, pixels in enumerate(frames):
        image = Image.frombytes("RGB", (WIDTH, HEIGHT), struct.pack(f"<{len(pixels)}H", *pixels), "raw", "BGR;16")
        image.save(os.path.join(target, f"frame_{number:02d}.png"))
    print(f"extracted {len(frames)} frames to {target}")
    return 0


def main(arguments):
    if len(arguments) >= 2 and arguments[0] == "list":
        return list_animations(arguments[1:])
    if len(arguments) == 3 and arguments[0] == "extract":
        return extract(arguments[1], arguments[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
