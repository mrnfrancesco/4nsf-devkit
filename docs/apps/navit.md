# Navit: open-source navigation with OpenStreetMap maps

[Navit](https://github.com/navit-gps/navit) is an open-source navigation program that also runs on Windows CE. `apps/navit/` turns it into an app of the VW menu: you find it under **more → Navit**, next to the unit's own navigation, which keeps working as before. The maps come from [OpenStreetMap](https://www.openstreetmap.org/): you build them on your computer, and each time Navit starts you choose which ones to load.

![Choosing the maps](../img/navit-picker.png) ![Navit with a map of Rome, night colours](../img/navit-map.png)

Status: works in the emulator ✅ (2026-11-17). Navit loads two maps at once, draws them, and follows the GPS position while the unit's own navigation is running. Never tried on a real unit; routing and voice output not tried.

The idea comes from [hupog/seat-portable-system-revival](https://github.com/hupog/seat-portable-system-revival), who first ran Navit on this hardware (a Seat Portable System), there as a *replacement* for the navigation.

## Try it in the emulator

```sh
make app APP=navit                    # the launcher, plus Navit itself (downloaded and configured)
make map MAP=roma BBOX=12.35,41.80,12.65,42.00   # a map of Rome (downloads Italy once, 2.2 GB)
make run GPS=1                        # emulator with a simulated GPS receiver
make deploy APP=navit MAPS="roma"     # Navit, the map and the launcher into the emulator
make stop && make run GPS=1           # the VW menu looks for new apps only when it starts
```

Then tap **more → Navit**, tick the maps, and tap **Start Navit**.

Be patient: in the emulator Navit takes 2–3 minutes to appear, because QEMU has to emulate every instruction of the CPU (inside the emulated unit it is about 7 seconds). The map is drawn at the first touch. The simulated GPS starts in Wolfsburg; to drive around Rome instead, restart it with `.venv/bin/python emulator/gps.py --lat 41.9028 --lon 12.4964 --speed 30`.

## Using it

- **Choosing maps.** The first screen lists every map it finds, on the flash disk and on the SD card. Tick the ones you need: Navit uses all of them together, also for routes that cross from one to the next. Your choice is remembered for next time.
- **Navit's menu.** Tap the map. *Actions* is where you set a destination; *Settings → Maps* switches the loaded maps on and off; **Quit** takes you back to the VW menu.
- **Getting back to the VW menu.** Navit fills the screen and has no button for the VW menu, so Quit is the way back. If Navit is still running when you tap its menu entry, the VW menu brings it to the front instead of starting it twice.
- **Day and night.** Navit switches between light and dark map colours by itself, from the sun's position.

## Maps

`make map` builds a map from OpenStreetMap data. It downloads a region from [Geofabrik](https://download.geofabrik.de/), optionally cuts out a smaller area, keeps only what a car map needs, and converts the result into Navit's format. Without options it builds all of Italy.

```sh
make map                                          # all of Italy  -> apps/navit/maps/italy.bin
make map MAP=nordovest REGION=europe/italy/nord-ovest
make map MAP=roma BBOX=12.35,41.80,12.65,42.00    # a cut-out of Italy
make map MAP=italy-full FILTER=full               # keep everything (slower to draw)
```

| Option | Meaning |
|---|---|
| `MAP` | name of the map: the file is `apps/navit/maps/<MAP>.bin`, and the picker shows this name |
| `REGION` | the region to download, as it appears in Geofabrik's addresses (without `-latest.osm.pbf`). Italy is `europe/italy`; its parts are `europe/italy/nord-ovest`, `nord-est`, `centro`, `sud` and `isole`. Default: `europe/italy` |
| `BBOX` | optional: cut out a rectangle, `left,bottom,right,top` in degrees |
| `FILTER` | `roads` (default) keeps roads, ferries, turn restrictions, place names, water and borders, and drops buildings, shops, points of interest and land use, so the map loads and draws faster. `full` keeps everything |
| `MEMORY` | memory for the build container (default `8g`) |

The download is kept in `apps/navit/maps/src/` (Italy: 2.2 GB). Each region is downloaded only once, and again only when Geofabrik publishes a newer one; its checksum is verified.

Where to put the maps:

- **SD card** for large maps: `\Storage Card\Navit\maps\`. The flash disk is nearly full: the update package alone puts 3.5 GB on it ✅, most of it the stock maps (2.9 GB). Italy, for example, gives a 510 MB map.
- **Flash disk** for a city or a small region: `\My Flash Disk\Navit\maps\`.
- **Not larger than 2 GB** per file: this Navit build cannot read bigger ones. Split a big country into regions and tick them all.
- **In the emulator**, use small maps. `make deploy` copies them over an emulated serial line at about 60 KB/s (Rome takes 2 minutes), and the emulated disk is as full as the real one.

Map data © OpenStreetMap contributors, under the [ODbL](https://opendatacommons.org/licenses/odbl/). Navit does not show this on screen: if you share maps, say where the data comes from.

## Installing on a unit

Read [building-apps.md](building-apps.md) ("Rules of the house") and [boot-and-supervision.md](../system/boot-and-supervision.md) §8.1 first.

1. On your computer: `make app APP=navit`, and `make map …` for the maps you want.
2. Copy to the unit:
   - the folder `apps/navit/navit/` → `\My Flash Disk\Navit\`
   - `navit.exe`, `NgAppInfo.xml` and `navit.png` from `apps/navit/` → `\My Flash Disk\Apps\navit\`
   - the maps → `\Storage Card\Navit\maps\` (or `\My Flash Disk\Navit\maps\`)
3. Restart the unit: the VW menu looks for new apps when it starts.

### Keep a Navit crash from restarting the unit

On the stock system, a program that crashes **restarts the whole unit**, unless it is listed in `\My Flash Disk\ErrorReporter.ini` under `[NoResetAfterException]` ([boot-and-supervision.md](../system/boot-and-supervision.md) §4.1). Only the unit's own programs are listed there. To have a crash close only Navit, add Navit and its launcher after the eight existing lines:

```ini
[NoResetAfterException]
Application1 = \My Flash Disk\NAVIGON\navigon.exe
...
Application8 = \My Flash Disk\BT\BTCore.exe
Application9 = \My Flash Disk\Navit\navit.exe
Application10 = \My Flash Disk\Apps\navit\navit.exe
```

- Keep a copy of the original file first.
- Leave the other lines as they are, and keep the file's Windows line endings (CR LF).
- After a crash, a report is still written to `\My Flash Disk\NAVIGON\ErrorReporter\`; Navit is closed and not restarted.

### Unknowns on a real unit

- **Memory** ❓: Navit runs alongside the navigation, the menu, the phone and the media player. hupog, who ran Navit *instead of* the navigation, only reported slow drawing with big maps. Start with a region rather than a whole country.
- **Speed** ❓: drawing a map on a 533 MHz processor. Keep `FILTER=roads`.
- **What Navit does not do here**: speak (that needs the unit's audio arbitration, [media-audio.md](../system/media-audio.md)), traffic messages, car data.

## How it works

### The pieces

| On the unit | Comes from | What it is |
|---|---|---|
| `\My Flash Disk\Apps\navit\navit.exe` | `apps/navit/navit.c` | the **launcher**: the map picker. This is what the VW menu starts |
| `\My Flash Disk\Apps\navit\NgAppInfo.xml`, `navit.png` | `apps/navit/` | the menu entry and its icon |
| `\My Flash Disk\Navit\` | `apps/navit/navit/`, made by `fetch.sh` | **Navit** itself: `navit.exe` (8 MB, needs no other DLL), its settings `navit.xml`, day and night map styles, icons. English menus only, no voices |
| `\My Flash Disk\Navit\maps.xml` | written by the launcher | the maps ticked in the picker; Navit's settings include this file |
| `\My Flash Disk\Navit\navit.log`, `data\` | written by Navit | Navit's log; last position, bookmarks, recent destinations |

When you tap **Start Navit**, the launcher writes `maps.xml`, starts Navit, waits for its window and brings it to the front (otherwise the VW menu would stay on top). When Navit exits, so does the launcher. The menu entry names Navit's window (class `NAVGRA`, title `Navit`), which is how the VW menu finds a Navit that is already running.

### Navit's settings

`fetch.sh` downloads Navit 0.5.7 for Windows CE from Navit's GitHub page (the download is checked against a fixed SHA-256) and changes its `navit.xml` with `apps/navit/navit.xml.diff`:

| Change | Why |
|---|---|
| GPS from `COM4:` | `COM2:`, where the GPS normally is, belongs to the unit's navigation (next section) |
| maps and map styles loaded by full path | on Windows CE, Navit does not expand its path variables or wildcards (hupog found this first) |
| first position: Rome | Navit later remembers the last position in `data\center.txt`; delete that file to go back |
| full screen, larger text, icons and buttons; Navit's second, small pair of zoom buttons switched off | 480×272 touch screen, operated with a finger |
| flat map, no automatic tilt | faster drawing |

Navit always reads `navit.xml` from its own folder.

### The GPS: why `COM4:`

The unit's GPS delivers its positions on the serial port `COM2:`. Only one program at a time can open that port, and the unit's navigation opens it at start-up and never lets go ✅. So Navit cannot read the GPS there.

But the GPS driver also copies every position message to a second, debugging port, `COM4:`, which nothing on the unit uses ✅. Navit reads that copy: same positions, no conflict with the navigation. 🟡 Found by reading the driver's code; not yet tried on a unit. Details and addresses: [hardware-layer.md](../system/hardware-layer.md) §6.5.

The emulator copies its simulated GPS to a `COM4:` in the same way, so Navit sees the same thing there as on the unit ([emulator/README.md](../../emulator/README.md)).

### Emulator details

- Navit opens text windows for its console output; in the emulator they sit behind it and show up on the Windows taskbar while Navit closes. The unit's settings turn console output off (`HKLM\Drivers\Console\OutputTo = -1` ✅), so they should not appear there 🟡.
- Navit shows the Windows taskbar when it exits; the launcher hides it again. The unit has no taskbar.
- The launcher writes `NAVIT: window after … ms` to `emulator/work/serial.log`; Navit's own log is `\My Flash Disk\Navit\navit.log` (fetch it with `.venv/bin/python emulator/deploy.py get`).

## Licences

Navit is GPL-2.0. It is downloaded by `fetch.sh` (its source code is on the same release page) and never stored in this repository. `apps/navit/navit.xml.diff` modifies Navit's own `navit.xml`, so it is GPL-2.0 as well. The launcher and the scripts are MPL-2.0, like the rest of the kit. The maps are under the ODbL. See [NOTICE.md](../../NOTICE.md).
