# Third-party code and files

This project: code under MPL-2.0 ([LICENSE](LICENSE)), documentation under CC BY-SA 4.0 ([LICENSE-DOCS](LICENSE-DOCS)). The files in the first table below are derived from MIT-licensed code: the upstream parts stay under the MIT licence reproduced at the end of this file, our changes are under MPL-2.0. The downloaded components keep their own licences.

## Derived from smn/4nsf-toolbox

The emulator approach and the code below come from [smn/4nsf-toolbox](https://github.com/smn/4nsf-toolbox) (commit `8c04ac0`, 2026-11-08), a fork of [jilleb/4nsf-toolbox](https://github.com/jilleb/4nsf-toolbox). MIT licence, reproduced at the end of this file.

| File here | Origin | What was changed |
|---|---|---|
| `emulator/guest/pl011poll.c` | `Devkit/drivers/pl011poll/pl011poll.c` | unchanged apart from one comment |
| `emulator/gps.py` | `Emulator/tools/nmea.py` | rewritten on the `nmeasim` library; same purpose |
| `firmware/cerom.py` | `Emulator/ceromdump.py` + `celzx.py` | restructured; decompresses while dumping, with `libarchive` instead of upstream's own LZX decoder (which got one ROM file, `WindowsCE.jpg`, wrong) |
| `firmware/sfx_extract.py` | `Emulator/sfx_extract.py` | restructured; same algorithm; PE header read with `pefile` |
| `emulator/deploy.py`, `emulator/guest/ceagent.c` | `Devkit/deploy.py`, `Devkit/agent/ceagent.c` | rewritten; same protocol; serial-only (TCP and pyserial paths removed) |
| `emulator/guest/launch.c` | `Emulator/guest-tools/mklaunch.py`, `Devkit/drivers/pl011poll/addport.c` | rewritten in C as one program |
| `emulator/patches/patch_kernel.py` | `Emulator/patch_image.py` | restructured; same patch tables; output byte-identical |
| `emulator/patches/redirect_imports.py` | `Emulator/cestub.py` (`patch`) | rewritten on `pefile`; same method; output byte-identical |
| `emulator/stubs/*.c` | `Emulator/cestub.py` (`make-stub`), `mkflash.py`, stub lists in `build-navigon.sh` | rewritten in C; same exported functions. `WolfSpecial.c` was then reworked after the unit's real DLL: its prototypes and return values, hidden flash kept in a file |
| `emulator/build.sh`, `emulator/emu` | `Emulator/build-navigon.sh`, `run-emulator.sh`, `start-vw.sh` | rewritten |

The findings these files encode (hidden-flash layout, board API signatures, kernel patch tables, the import-slot quirk of the Windows CE loader) are upstream's work.

## Other third-party content in the repository

- `apps/navit/navit.xml.diff`: changes to `navit.xml` of [Navit](https://github.com/navit-gps/navit) 0.5.7; like that file, GPL-2.0.
- `apps/navit/navit.c`: the table of ISO 3166 country codes (three and two letters) comes from Navit's `country.c`; the codes themselves are an international standard.
- `emulator/tracks/roma-nomentana.gpx`: a road in Rome taken from OpenStreetMap (© OpenStreetMap contributors, ODbL 1.0), test drive for `emulator/gps.py --track`.
- `tools/samples/dock-capture.txt`: 53 serial frames posted by "robomrburns" on seatcupra.net (2016, thread "Garmin PID / Garmin mod skin tool / reverse engineering"). Included as test data; not covered by this project's licences.

## Downloaded at setup time, never stored in the repository

- Python libraries: `requirements.txt` (host, into `.venv`) and the `apt-get install` line of `sdk/Containerfile` (dev container).

- Windows Embedded Compact 7 kernel image for QEMU: [Milek7/wince-qemu](https://github.com/Milek7/wince-qemu), release 2022-04-17 (contains Microsoft code; no licence stated upstream).
- CeGCC cross-compiler image: `ghcr.io/enlyze/windows-ce-build-environment-arm` ([enlyze/cegcc-build](https://github.com/enlyze/cegcc-build)).
- The unit's software: Volkswagen's public update package (© Garmin / Navigon / Volkswagen).
- Navit for Windows CE, `navit-wince-v0.5.7.zip` from [navit-gps/navit](https://github.com/navit-gps/navit/releases/tag/v0.5.7) (GPL-2.0; source in the same release), by `apps/navit/fetch.sh` (`make app APP=navit`), with the eSpeak speech data it contains (`espeak-data/`, GPL). `fetch.sh` changes the downloaded `navit.exe` locally (debug information removed, `waveOutWrite` taken from `apps/navit/navitsnd.c`); nothing of it is stored in this repository.
- OpenStreetMap extracts from [Geofabrik](https://download.geofabrik.de/) (© OpenStreetMap contributors, ODbL), by `apps/navit/build_map.sh` (`make map`); the maps built from them stay under the ODbL.
- Python packages in `requirements.txt` (`pefile`, `vncdotool`, `nmeasim`, `gpxpy`, `crccheck`, `dissect.cstruct`, `Pillow`), Debian packages in `sdk/Containerfile`.

Not downloaded, optional: `tools/decompile.py` calls [radare2](https://github.com/radareorg/radare2) with the [r2ghidra](https://github.com/radareorg/r2ghidra) plugin if they are installed on the host.

## Licence of smn/4nsf-toolbox and jilleb/4nsf-toolbox

```
MIT License

Copyright (c) 2024 Chillout

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
