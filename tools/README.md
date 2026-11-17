# tools/

Scripts you run by hand to study the unit's software. (Scripts that belong to one step of the workflow live next to it: `firmware/` for downloading and unpacking, `emulator/` for building and driving the test bench.)

Run them with `.venv/bin/python` (created by `make venv`; third-party packages are listed in `requirements.txt`). Each prints its usage when run without arguments. None of them writes inside `firmware/`.

| Script | Purpose |
|---|---|
| `peinfo.py FILE…` | Summarise a Windows CE program or DLL: CPU type, subsystem, imports, exports (`pefile`) |
| `decompile.py FILE ADDRESS…\|all\|list` | Decompile functions of a program to C, with import names and strings filled in (`pefile` + radare2, see below) |
| `nfs_extract.py list\|verify\|extract FILE.nfs` | Navigon resource containers (`.nfs`, `.map`, `.afs`); format in `docs/system/file-formats.md` (`dissect.cstruct`) |
| `kbi_extract.py list\|tree\|extract FILE.kbi` | `.kbi` stores inside the UI `.nfs` containers: screens and workflows as text, bitmaps as PNG; format in `docs/system/ui-framework.md` (`dissect.cstruct`, `Pillow`) |
| `res_extract.py list\|extract FILE.res` | `.res` archives of the navigation and phone programs, images as PNG (`dissect.cstruct`, `Pillow`) |
| `rgbfs_extract.py list\|extract FILE.RGBFS` | Boot animations → PNG frames; format in `docs/system/brand-and-pairing.md` (`dissect.cstruct`, `Pillow`) |
| `hive_dump.py FILE.hv` | Registry hives of the OS ROM as `.reg`-style text; format in `docs/system/hardware-layer.md` (`dissect.cstruct`) |
| `appcom.py decode FILE` / `encode …` | Messages between the unit's programs (AppCom, UDP): decode a capture, build datagrams; try `samples/appcom-capture.txt`; format in `docs/apps/appcom.md`. `apps/appcom` records captures in the emulator and sends the built datagrams |
| `decode_frames.py FILE` | Decode a capture of the unit ↔ car-radio serial link; try `samples/dock-capture.txt` (`crccheck`). Its frame functions are also what `emulator/radio.py` speaks with |
| `bap_tables.py VwCommunicationExe.exe` | Print the BAP tables built into the car-link program, to check `docs/car/bap.md` §3 (`pefile`, `dissect.cstruct`) |

Examples (after `make firmware`; `/tmp/x` stands for any folder outside the repository):

```sh
PY=.venv/bin/python
$PY tools/peinfo.py firmware/flash/VwUserShell/*.exe
$PY tools/decompile.py firmware/flash/VwUserShell/VwCommunicationExe.exe 0x4391c   # opens the serial port

$PY tools/nfs_extract.py extract firmware/flash/VwUserShell/VW_UserShell.nfs /tmp/x/shell
$PY tools/kbi_extract.py list /tmp/x/shell/*.kbi
$PY tools/kbi_extract.py tree /tmp/x/shell/Controls_480x272.kbi 1                  # the main menu
$PY tools/nfs_extract.py extract firmware/flash/VwUserShell/VW_UserShell_VwSkin.nfs /tmp/x/skin
$PY tools/kbi_extract.py extract /tmp/x/skin/Bitmaps_480x272.kbi /tmp/x/bitmaps    # 2889 PNG files

$PY tools/res_extract.py extract firmware/flash/BT/PhoneApplication.res /tmp/x/phone
$PY tools/rgbfs_extract.py extract firmware/flash/ChangeStartupScreens/0/SPLASH.RGBFS /tmp/x/splash
$PY tools/hive_dump.py firmware/rom/files/default.hv > /tmp/x/default.reg

$PY tools/appcom.py decode tools/samples/appcom-capture.txt
$PY tools/appcom.py encode 27017 2 '[2, "ger-DEU"]'                                # a language change, as hex
$PY tools/decode_frames.py tools/samples/dock-capture.txt
$PY tools/bap_tables.py firmware/flash/VwUserShell/VwCommunicationExe.exe
```

`decompile.py` needs [radare2](https://github.com/radareorg/radare2) (`r2`) with its r2ghidra plugin on your machine. Both are optional and not installed by `make setup`; no other tool uses them.

Samples:

- `samples/dock-capture.txt`: 53 frames sent by a unit to the radio, posted by "robomrburns" on seatcupra.net in 2016.
- `samples/appcom-capture.txt`: 200 datagrams (66 different ones) sent by the unit's menu, navigation, phone and media programs while they ran in the emulator; one line per datagram and receiving port: `TICK PORT LENGTH HEX`.

Text strings in a binary: Windows CE uses UTF-16, which plain `strings` misses. Use GNU strings from the dev container:
`sdk/run arm-mingw32ce-strings -e l FILE` (UTF-16) and `sdk/run arm-mingw32ce-strings FILE` (ASCII).
