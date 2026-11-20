# The emulator

Runs the unit's software, and yours, in QEMU. The real unit is never involved.

```sh
make run        # or: emulator/emu up --window
make run RADIO=1               # with a simulated car: the menu's car screens show values
make stop
emulator/emu shot NAME         # screenshot -> emulator/work/shots/NAME.png
emulator/emu click 135 90      # tap at a screen position (480x272); dbl X Y = double tap
```

Use the mouse only: typed keys arrive doubled in the guest.

## How it works

QEMU has no model of the unit's processor board (STMicroelectronics Cartesio+), so the unit's own Windows CE 6 system image cannot boot in it. The emulator turns this around: it boots a **generic Windows Embedded Compact 7** on a board QEMU does support, and runs the unit's **applications** on top.

| Layer | Comes from | Notes |
|---|---|---|
| Machine | `qemu-system-arm`, board `vexpress-a15`, 256 MB | stock QEMU, started by `emulator/emu` with `-icount` (see "The clock" below) |
| Kernel | Windows Embedded Compact 7 image prebuilt by [Milek7/wince-qemu](https://github.com/Milek7/wince-qemu) | downloaded by `build.sh`, SHA-256 pinned |
| Disk | 4 GB FAT32 image mounted as `\My Flash Disk` | a copy of `firmware/flash/` with the changes below |
| Unit-specific system DLLs | the unit's own system image (`firmware/rom/`) | copied next to each program instead of living in `\Windows` |

What `build.sh` does, step by step:

1. **Kernel** (`patches/patch_kernel.py`): downloads the image and patches three things in it: the disk is mounted under the name `My Flash Disk`, the display becomes 480×272, and one timer call the VW menu trips over is guarded.
2. **Compile** `guest/` and `stubs/` (below) with the SDK's compiler.
3. **Stage**: a hard-linked copy of `firmware/flash/` to work on. `firmware/flash/` itself is read-only and never changes.
4. **Missing system functions** (`patches/redirect_imports.py`): the unit's programs import about 16 functions from Windows CE 6 that Compact 7 no longer has (dial-up networking, telephony, power status). Those imports are pointed at `cestub.dll`.
5. **System DLLs next to every program**: our stand-ins from `stubs/`, then the unit's own DLLs and fonts from `firmware/rom/files/` (on the unit they live in `\Windows`).
6. **Settings**: GPS on `COM1:`, the car link on `COM3:` (the unit uses `COM9:`), Navigon's software map renderer, the no-GPS restart timer raised, the boot animation disabled (it crashes without the unit's graphics accelerator). Pairing needs no setting: the stand-in's hidden flash says "paired with a Volkswagen", and there is no `BrandDevice.reg` file, which would send the brand program back to waiting for a car.
7. **Guest programs** from `guest/`, copied to `\My Flash Disk\0start` and `\My Flash Disk\dev`.
8. **Disk**: one 4 GB FAT32 partition (`sfdisk`, `mkfs.vfat`, `mcopy`), plus a qcow2 overlay.

The stand-ins in `stubs/` (plain C):

| DLL | Replaces | Behaviour |
|---|---|---|
| `cestub.dll` | functions missing from the emulator's COREDLL | dial-up and telephony calls return 0; power status says "on charger, battery full" |
| `WolfSpecial.dll` | the unit's board-support DLL | answers like a unit in its cradle in a petrol VW up!, with the real DLL's prototypes and return values. The hidden flash (brand, pairing, VIN) is a small file on the flash disk, `dev\hidden-flash.bin`, so all programs share it and it survives restarts; until something writes it, the defaults apply. Layout and details: `stubs/WolfSpecial.c` |
| `ddraw.dll` | DirectDraw | `DirectDrawCreate` fails, which is enough for the unit's OpenGL ES DLL to load |
| `svoxdll.dll` | the SVOX speech decoder (part of the unit's system image) | four functions returning 0 |

The programs in `guest/`:

| Program | Role |
|---|---|
| `launch.exe` | what `emu up` double-clicks: hides the taskbar, adds `COM2:`/`COM3:`, takes over `COM1:` and adds `COM4:` (the GPS copy, below), starts the agent and the VW processes (car link, brand program, menu, phone hidden; like the unit's watchdog it waits for the brand program to finish before starting the menu); then sends the phone its language (AppCom msg 2, `LANGUAGE` in `launch.c`, default `eng-GBR`), which on the unit comes from navigon |
| `ceagent.exe` | deploy agent on `COM2:`; receives files, starts and lists programs for `deploy.py` |
| `pl011poll.dll` | serial-port driver behind `COM1:`, `COM2:` and `COM3:`; on `COM1:` it also copies each received line into the message queue `TX_NMEA`, and it serves `COM4:` from that queue, as the unit's GPS driver and `LoopbackCOM.dll` do ([docs/apps/navit.md](../docs/apps/navit.md#the-gps-why-com4)). The unit's own `LoopbackCOM.dll` is not used: the copy extracted from the ROM does not load (error 193) |

After editing `guest/`, `stubs/` or `build.sh`: `rm -rf emulator/work/stage && make emulator`. To use another VIN than the placeholder, add `VIN=<17 characters>` to that `make emulator`.

## Connections to the host

| Guest | Host | Use |
|---|---|---|
| screen | QEMU window (the 480×272 screen scaled to fit; resize it freely), or VNC `127.0.0.1:5901` (`open vnc://127.0.0.1:5901`) | `emu shot/click/dbl` use it through [vncdotool](https://github.com/sibson/vncdotool) |
| `COM1:` | TCP 5555 | GPS (simulated by `nmeasim`): `emulator/emu up --gps`, or `.venv/bin/python emulator/gps.py --lat .. --lon .. --speed ..` |
| `COM4:` | – | the same GPS sentences for a second reader (Navit), like the unit's `COM4:` |
| `COM2:` | TCP 5556 | deploy agent (`emulator/deploy.py`: `push`, `put`, `get`, `ls`, `rm`, `run`, `ps`) |
| `COM3:` | TCP 5557 | car link: the unit's car-link program talks here; `emulator/emu up --radio` or `.venv/bin/python emulator/radio.py --rpm .. --speed ..` plays the radio |
| kernel debug output | `work/serial.log` | `OutputDebugStringW` from any program ends up here |
| CPU debugger | gdb remote on `127.0.0.1:1234` | |

## The simulated car

`emulator/radio.py` is the radio's end of the dock link ([docs/car/car-link.md](../docs/car/car-link.md)): it acknowledges the unit's frames, sends the handshake (placeholder VIN, petrol) and then repeats 15 vehicle messages: ignition, engine speed, coolant / oil / outside temperature, speed, doors, central locking, low-fuel flag, trip computer, make and body of the car. Values come from its command line (`--help`); `--trace` prints every frame in both directions. For your own values, start the emulator without `RADIO=1` and run the script yourself (`.venv/bin/python emulator/radio.py --rpm 3000 --speed 120`); to change them, stop it and start it again: the unit restarts the link within 3 seconds.

Not simulated: BAP, the protocol for the radio's own functions (sound source, station lists, parking sensors, clock). The unit's requests go unanswered and the screens that need them stay empty.

Without the radio the car-link program runs but reports "no radio"; `apps/cardata` can then write single values into the shared memory ([docs/car/shared-memory.md](../docs/car/shared-memory.md) §8.5).

## The clock

QEMU runs with `-icount shift=auto`: guest time follows the instructions executed instead of the host's clock. Without it this kernel's tick count jumps ahead by 1 to 4 seconds several times a minute, and time-outs in the unit's software fire early: the car link, which gives up after 3 silent seconds, restarted about every 15 seconds. With it the jumps are gone (a rare link restart remains, and the radio handles it). The price is a slower guest processor; menus and Navigon's first screens behave as before.

## What works

Verified: Windows CE desktop, VW main menu, brand program taking the "paired unit" path and exiting, Car info: trip computer, gauges and door picture with the simulated radio's values, Navigon starts into its first-start wizard, deploy agent, own programs, own apps listed under "more" and started from there, the three apps in `apps/` (vehicle data through the shared memory, AppCom messages received and sent).
Reported by the upstream project, not re-checked here: Navigon menus and map with a fake GPS fix; phone app starts.
Missing: BAP (radio functions, parking sensors, clock, e-up! charging), Bluetooth, media playback, the accelerated map renderer, sound, the watchdog and crash handler.

## Files

```
while it runs
  emu            start / stop / up / shot / click
  deploy.py      copy files in and out, start and list programs (talks to guest/ceagent.c)
  gps.py         simulated GPS receiver on COM1:
  radio.py       simulated car radio on COM3:

to build it (make emulator)
  build.sh       builds work/ from firmware/ (the eight steps above)
  patches/       patch_kernel.py, redirect_imports.py: the two binary patchers build.sh uses (apps/navit/fetch.sh also uses redirect_imports.py --only)
  guest/         programs that run inside the emulator
  stubs/         stand-ins for DLLs the emulator's Windows lacks

work/          generated (about 8 GB): kernel/, stage/, disk.img, disk.qcow2, logs, shots/
```

The guest's own writes go to `work/disk.qcow2`. To reset the disk: `rm emulator/work/disk.*` then `make emulator`.

Credits: the approach and the findings behind every patch and stub are from [smn/4nsf-toolbox](https://github.com/smn/4nsf-toolbox); see [NOTICE.md](../NOTICE.md).
