# firmware/ — the unit's official software

`make firmware` downloads Volkswagen's public update package for the Garmin-labelled unit (software 7.16.9, maps Q4/2020, 3.5 GB) and unpacks it here. Only this file and the three scripts are part of the repository:

| Script | What it does |
|---|---|
| `fetch.sh` | The whole of `make firmware`: download, verify, unpack. Runs in the dev container |
| `sfx_extract.py ARCHIVE OUTDIR` | Unpacks the package's two self-extracting archives |
| `cerom.py IMAGE OUTDIR` | Takes a Windows CE system image apart into modules and files |

What appears:

| Folder | What it is |
|---|---|
| `package/` | The package as published: installer programs and two archives |
| `flash/` | `\My Flash Disk`, the unit's main storage: every application, DLL, config file, skin and map. **This is what to study.** |
| `os/` | Boot loader files and the Windows CE 6 system image `NK.nb0` |
| `rom/` | `NK.nb0` taken apart into `modules/` (kernel, drivers, system DLLs) and `files/` (fonts, extra DLLs, registry) |

Where to start in `flash/`: `autorunce.mscr` (boot script), `Watchdog_*.ini` (which processes run), `VwUserShell/` (main menu and car link), `NAVIGON/` (navigation), `Apps/` (add-on apps). Guide: [docs/system/device.md](../docs/system/device.md). Scripts that read these files (containers, screens, bitmaps, registry hives, programs): [tools/README.md](../tools/README.md).

Other regions: `REGION=South_Europe make firmware` or `North_Europe`. Only the maps differ; the programs are identical.

These files are copyrighted by Garmin, Navigon and Volkswagen. Do not commit or redistribute them. Treat `flash/` as read-only: the emulator works on a copy.
