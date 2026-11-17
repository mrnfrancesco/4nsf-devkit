# File formats

Where every file format of the unit is specified. This file owns the NgFs container (`.nfs` / `.map` / `.afs`); the other formats are specified in the document of the program that uses them (table below).

Legend: ✅ verified by script on the real files · 🟡 inferred · ❓ unknown.

## Formats and where they are documented

| Format | What | Documented in | Status | Tool (`tools/`) |
|---|---|---|---|---|
| `.nfs`, `.map`, `.afs` (NgFs) | container: named entries, paged zlib, Adler-32 | below | ✅ read + verify on every file; no repacker | `nfs_extract.py` |
| `.kbi` | key → blob store inside the MN8 `.nfs` (screens, workflows, bitmaps, colours, fonts, texts, settings map); 32 loose voice-recognition contexts | [ui-framework.md](ui-framework.md) §2 (container), §3 (node trees), §4–§5 (what the nodes mean) | ✅ parsed on all 220 stores; semantics of many class ids 🟡 / ❓; no repacker | `kbi_extract.py` |
| `PNXF` / `.pnx` | Navigon palette bitmap (RGB565 palette, optional alpha per palette entry) | [ui-framework.md](ui-framework.md) §6 | ✅ all 12,482 (`.kbi`) + 7,135 (`.res`) images convert; no writer | `kbi_extract.py`, `res_extract.py` (→ PNG) |
| `.res` | MN7 GUI resource archive (`navigon.exe`, `PhoneApplication.exe`) | [ui-framework.md](ui-framework.md) §10 | ✅ container; inner `.dat` screen definitions ❓ | `res_extract.py` |
| `.RGBFS` | boot animation: frames of 480×272 RGB565, raw or RLE | [brand-and-pairing.md](brand-and-pairing.md) §5.3 | ✅ all 10 files decode | `rgbfs_extract.py` |
| `.lng` | MN7 text tables; `ManualReaderNames.lng` / `AppNamesFile` = UTF-16LE `lc=Name` lines | [ui-framework.md](ui-framework.md) §10, [appcom.md](../apps/appcom.md) §6.2 | 🟡 | – |
| `NgAppCom.xml`, `NgAppInfo.xml` | AppCom app registry | [appcom.md](../apps/appcom.md) §6 | ✅ | – |
| AppCom datagram | UDP payload: header + serialized variants | [appcom.md](../apps/appcom.md) §2–§4 | ✅; ✅ emulator: 989 captured datagrams decode ([appcom.md](../apps/appcom.md) §13) | `appcom.py` |
| Dock serial frames | framing, CRC, handshake | [car-link.md](../car/car-link.md) §2–§3 | ✅ | `decode_frames.py` |
| `VwSharedMemoryFileMapping` | car-data block (C header) | [shared-memory.md](../car/shared-memory.md) §2, §8.1 | ✅ layout; some BAP-block fields ❓ | – |
| `MMDPMemory` | media player ↔ decoder request block | [media-audio.md](media-audio.md) §2 | ✅ | – |
| Hidden flash (user + factory block) | brand, VIN, password lock, device id, BT address | [hardware-layer.md](hardware-layer.md) §3, [brand-and-pairing.md](brand-and-pairing.md) §2 | ✅ | – |
| CE registry hives (`boot.hv`, `default.hv`, `user.hv`) | ROM default registry | [hardware-layer.md](hardware-layer.md) §5.5 | ✅ | `hive_dump.py` |
| `Watchdog_*.ini`, `ErrorReporter.ini`, crash-dump folders | supervision config, dump layout | [boot-and-supervision.md](boot-and-supervision.md) §3.1, §4 | ✅ keys; dump file names 🟡 | – |
| Update files (`update.me`, `NK.nb0`, `*.sum` = CRC-32 text, `UpdateMeContent`) | boot-loader / installer inputs | [boot-and-supervision.md](boot-and-supervision.md) §7 | ✅ names and checks; run-time flow 🟡 | – |
| Settings `.xml` / `.default` / `.old` | per-program settings | [user-shell.md](user-shell.md) §9, [navigation.md](navigation.md) §2, [media-audio.md](media-audio.md) §8.6, [bluetooth.md](bluetooth.md) §8.4 | ✅ | – |
| `device.xml`, `mis.txt`, `version.xml` | key store, LIVE payload, version / brand strings | [navigation.md](navigation.md) §7, [device.md](device.md) §11 | ✅ role; code alphabet ❓ | – |
| BT files (`BTEnv.txt`, `CallLog.txt`, `contacts3.txt`, `reconnect.dat`, `Phones/*.dat`) | pairing list, call log, phonebook | [bluetooth.md](bluetooth.md) §6, §8.4 | 🟡 structure only | – |
| `.route`, `.resy`, `.store` | navigon's binary serializer (routes, favourites) | [navigation.md](navigation.md) §1.7 | ❓ not decoded | – |
| Tables inside `.map` (`*.tbi`, `*.nbx`, `*.idx`), `.pom`, `Dat/*.dat`, `*.bin` | map data, POI, misc | [navigation.md](navigation.md) §10, [device.md](device.md) §9 | ❓ opaque | – |
| VoCon / TTS data (`.ctx`, `acmod3_*`, `ddg2p_*`, `*_tts.nfs` content) | Nuance engine data | [navigation.md](navigation.md) §5 | usable only with the engine DLLs | – |

Census (counts, magic bytes) of the files on flash: [device.md](device.md) §9.

## `.nfs` / `.map` / `.afs` — "NgFs" container ✅

Recovered from the Java source of the community extractor (jilleb/4nsf-toolbox `NgFs-Extract.jar`), re-implemented in `tools/nfs_extract.py`, **verified on all 74 `.nfs` on the unit: every entry decodes, every Adler-32 matches** (`verify` mode; 8,119 entries / 657 MB in the largest, `PhonemeV3_EU.nfs`). No encryption, no XOR.

All little-endian unless noted; checksums = zlib Adler-32.

```
0x0000 u32 magic 0x0BD5FE11 (11 FE D5 0B)     0x0004 u32 version (=5 everywhere)
0x0008 u32 dataOffset (=0x1000)
0x0018 u32 infoPackedLen    0x001C u32 infoUnpackedLen
0x0024 u32 numEntries
0x002C u32 namesPackedLen   0x0030 u32 namesUnpackedLen
0x0040 u32 infoOffset (=0x48)                 0x0044 u32 adler32(header[0:0x44])
       other header words constant here: [0x10]=4|18 [0x14]=3 [0x28]=3 [0x34]=8 [0x38]=2 [0x3C]=0x13  ❓ meaning
0x0048 zlib(info) + u32 adler32(packed info)
       info = repeated { u16 BIG-endian keyLen, key, u8 valLen, val }
       keys seen: NgFs.Guid, NgFs.User (=buildserver), NgFs.Host, NgFs.CreationDate
       zero padding up to dataOffset
0x1000 numEntries × { u32 nameOffset (into unpacked names blob), u32 packedSize }
       u32 adler32(table)
       zlib(names) + u32 adler32(packed names)      names = repeated { u16 len, bytes }
       entries, back to back, each packedSize bytes:
  +0  u32 magic 0x05D30D17 (17 0D D3 05)
  +4  u8 kind: 0 = stored, 3 = paged zlib            (+5..7 zero)
  +8  u32 unknown (constant per container, e.g. 0x1750, 0xB8, 0x1F58; repackers preserve it) ❓
  +12 u32 unpackedLen
  +16 u16 pageSize (0x1000)
  +20 u32 numBlocks (ceil(unpackedLen/pageSize); 0 if stored)
  +24 u32 ABSOLUTE file offset of block-length table (kind 3)
  +32 u32 adler32(payload)        +36 u32 adler32(entry header[0:36])
  +40 payload: raw (kind 0) | numBlocks independent zlib streams (≤ pageSize each when inflated)
      kind 3 trailer: u16 blockLen[numBlocks] + u32 adler32(blockLen table) (+ optional zero pad)
```

Quirks ✅: some files end with 1 stray zero byte (MapDrawer_*, poicats, poiwarner_live); `TMC.nfs` last table size overshoots EOF by 40. The Java tool reads `numBlocks` as one byte when repacking (breaks > 255 pages) — our reader uses the u32.

Usage:

```sh
.venv/bin/python tools/nfs_extract.py list    firmware/flash/VwUserShell/Datastorage.nfs
.venv/bin/python tools/nfs_extract.py verify  $(find firmware/flash -iname '*.nfs')
.venv/bin/python tools/nfs_extract.py extract firmware/flash/VwUserShell/VW_UserShell.nfs <outdir>
```

What is inside ✅:

| Container | Entries |
|---|---|
| `<App>.nfs` | `Controls_480x272.kbi`, `Startup.kbi`, `Shutdown.kbi`, `Workflows.kbi` (UI definition + workflow state machines) |
| `<App>_<Brand>Skin.nfs` | `Bitmaps_480x272.kbi`, `Colors_480x272.kbi`, `Fonts_480x272.kbi` |
| `TextResources.nfs` | one `<lang>_480x272.kbi` per language (35 in VwUserShell incl. `chi-CHN`) |
| `Datastorage.nfs` | `Datastorage.kbi` |
| `map/MapDrawer_<Brand>.nfs` | ~810 plain **PNG** icons (road shields, POI) |
| `sounds/*_tts.nfs` | TTS voice data (`apdb_*.dat`, `clmpackeddata.dat`, `Properties.ini`…; stored, not compressed) |
| `sounds/*_def_sound.nfs` | 425–681 prerecorded prompts |
| `Settings/Core/Advisor.nfs` | `*.csv.comp` decision tables / flow charts / per-language greeting tables |
| `map/TMC.nfs`, `PhonemeV3_EU.nfs`, `sat_EU.nfs`, `Terrain_EU.nfs` (297 MB stored), … | map-side data |

Repacking (not implemented in Python): mirror `rescanFromDisk()` in the jar source — deflate level 1, recompute all five Adler-32s, fix absolute block-table offsets. `content.md5` on flash does not need regenerating: no program checks it ([boot-and-supervision.md](boot-and-supervision.md) §7.4).

## Other formats (pointers)

- **`.kbi`** — magic `tnatxes\0` ("sextant" reversed): paged key → blob store with a small index tree; header 42 bytes, tree descriptor at 0x2A, root index node at 0x56; index slot = `{first key, last key, u32 offset, u32 len, u32 len2}` with a key width of 1, 2 or 4 bytes; no checksum, no compression; an empty store is 3158 bytes. `Bitmaps_*.kbi` values are `PNXF` images with **no prefix**. Full layout, node-tree encoding and the meaning of the records: [ui-framework.md](ui-framework.md) §2–§5.
- **PNX (`PNXF`)** — palette image: RGB565 palette, optional alpha table of **one byte per palette entry**, 8- or 16-bit pixel indices (the community converter's symbol names `ConvertColorTo565`, `_AlphaPalette`, `_IndexAlpha` agree). Layout: [ui-framework.md](ui-framework.md) §6.
- **`.res`** (6 files: `NAVIGON/MobileNavigator.res` 11 MB, `BT/PhoneApplication.res` 1.5 MB, + SEAT / SKODA variants) — MN7 archive: header `{u32 version = 1, u32 data offset, u32 count, u32 file size}`, then a name table (paths like `images/480x272/Sheldor/BTn_….pnx`) with size and offset per entry. Layout and content: [ui-framework.md](ui-framework.md) §10.
- **`.RGBFS`** — boot animation played by `SplashAnimation.exe`: [brand-and-pairing.md](brand-and-pairing.md) §5.3.
- Community tools (Windows binaries, in jilleb/4nsf-toolbox `Tools/`): `KBI_Extractor` / `KBI_Compressor`, `PNX-Converter`, `PNG-Converter`, `ResEntpacker`, `NgFs-Extract.jar`.
- Programs and DLLs (PE): `.venv/bin/python tools/peinfo.py [-e N] file…` (machine, linker, subsystem, imports by name / ordinal, exports + MSVC-mangled count); `tools/decompile.py file address…` (decompiled functions; needs radare2 + r2ghidra).
- Usage of every tool: [`tools/README.md`](../../tools/README.md). All are read-only: none writes a `.nfs`, `.kbi`, PNXF, `.res` or hive.
