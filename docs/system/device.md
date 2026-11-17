# Device inventory — Navigon/Garmin 4NSF (VW "maps + more" PID)

What the unit is made of: hardware, programs and DLLs, what is on its flash and in its settings volume, file formats, licence files, and where personal data sits. Based on one real unit (SW 7.16.8), the official package and the user manual.
How the parts work together is in [architecture.md](../architecture.md); each mechanism has its own document, linked from the sections below.

Legend: ✅ verified · 🟡 inferred / single source · ❌ common wrong belief, corrected · ❓ not provable from the files.

Paths: `\My Flash Disk\…` on the unit = `firmware/flash/…` here (official package, byte-identical binaries, see `firmware/README.md`); `\Windows\…` (ROM) = `firmware/rom/…`.
`<dump>/` = a copy of one real unit's storage (user data, crash logs) that was analysed and is **not kept in this repository** — sections 2, 10, 12, 13 describe it.

## 1. Hardware

| Item | Value | Evidence |
|---|---|---|
| CPU | "STM Cartesio 2064 (533MHz)" ✅ = ST STA2064 "Cartesio+" | manual p.169 "Dati tecnici"; ROM registry `Platform\Name = "Microsoft Windows CE for STA2064 CartesioPlus"`; `navigon.exe` imports `openGL_ES_STA2065.dll`; `sgalib.pdb` path `CartesioPlus_SDK_WinCE6.0 (ARMV4I)`; `ADynamics4706.dll` logs `CARTESIO PIO_init`. (`TmcHal.dll`'s PDB path says `SiRFAtlas4_EVB_CE6_SDK`: name of the vendor's SDK only) |
| Core arch | ❓ ARM1176 (ARMv6) per ST's public Cartesio+ data — not in the files | binaries are built ARMV4I (§3) |
| OS | Windows CE 6.0 ✅ | manual says only "Microsoft Windows CE"; 6.0 from PE subsystem 6.0, `udevice.exe` / `servicesd.exe` in crash dumps, `…wince6…` DLL names |
| Display | 5" resistive, 480×272 WQVGA, 320 cd/m² ✅ | manual; 4-wire touch on the SoC ADC, backlight PWM on GPIO 11 ([hardware-layer.md](hardware-layer.md) §6) |
| RAM / storage | 256 MB / 4 GB ✅ | manual. Internal MMC `DSK1:` holds `\My Flash Disk`, `\MN Settings`, a hidden registry volume and the "hidden flash" = raw sectors with brand / pairing / factory data ([hardware-layer.md](hardware-layer.md) §3) |
| GPS | STM STA5630, ST-AGPS ✅ | manual. Software side ✅: the GPS engine runs inside the ROM driver `GPS.dll`, which exposes plain NMEA as `COM2:`; navigon opens it at 57600 8N1 ([navigation.md](navigation.md) §3.2) |
| TMC | own FM / RDS tuner inside the unit ✅: Silicon Labs **Si4706** on I²C | driver `ADynamics4706.dll` is loaded at run time by `TmcHal.dll` as **`COM6:`**; the `RDS` / port 5 / 115200 values in `UserSettings.xml <TMC>` are ignored ❌ ([navigation.md](navigation.md) §4.2, [hardware-layer.md](hardware-layer.md) §6.5: reset GPIO 16, IRQ 17, power 43; an earlier note also named GPIO 52 ❓). Antenna path ❓ (🟡 cradle / charger lead) |
| Other | microSD, 1× USB 2.0, BT, 1 hardware key (power, GPIO 25), LiIon 1200 mAh, powered through the car cradle ✅ | manual; power sources and PMIC variants: [hardware-layer.md](hardware-layer.md) §6.4 |
| E-compass | none fitted 🟡: `UserSettings.xml <HWConfig>` names an AKM AK8973 (`eCompassAKD8973B`), but the ROM has no driver for it | [navigation.md](navigation.md) §3.4 |
| Car link | UART `COM9:` 19200 8N1 over the dock pins to the radio; **no CAN in the unit** | [car-link.md](../car/car-link.md) |
| Bluetooth | nFore module with a CSR BlueCore chip; all profiles run inside the module. Host link: `COM0:`, BCSP, 921600 8E1. Module firmware `H14423-V2R43`, host program `BTCore.exe` `1.1.102.105` | [bluetooth.md](bluetooth.md) §1, §5.1; `version.xml`. Older hints: string `csaPBAP`, a baud table 38400…256000 and `BAUDRATE` key in `BTCore.exe` (the settings file that would use them is not shipped) |
| Audio | one PCM device `WAV1:` (I²S to the DAC, software mixer, no hardware volume); built-in speaker amplifier on GPIOs 13 / 22; analog line-out on the dock; microphone inside the unit | [hardware-layer.md](hardware-layer.md) §6.5, [media-audio.md](media-audio.md) §1 |
| Watchdog | ARM SP805, default 5000 ms, fed by `WatchdogPuppy.exe` | [hardware-layer.md](hardware-layer.md) §6.3 |
| Codenames | platform **Wolf** (`WolfSDK`, `WolfSpecial.dll`, `CHWLWolf`, `CWolfDevice`), older **Topaz** (`TopazSDK`); project **Sheldor** (build tree `MN_VS2008_WolfSDK_…_VW_SHELDOR_CONNECTED_WOLF`); build `KW4514` (week 45/2014) | PDB paths |

GPIO map, serial-port map, drivers, registry: [hardware-layer.md](hardware-layer.md) §5–§6.

## 2. Dump contents / what is missing

| Dir | Device volume | Files | Size |
|---|---|---|---|
| `<dump>/NAVIGON/` | `\My Flash Disk` (DSK1: MYFLASH/Part00) | 847 | 3.57 GB |
| `<dump>/MN Settings/` | `\MN Settings` (DSK1: MNSET/Part01) | 52 | 471 KB (620 KB on disk) |
| `<dump>/NAVIGONSD/` | `\Storage Card` (DSK2:) | **0** — only empty dirs `Navigon/SimulationFiles/LIVE` | 0 |

A USB copy never contains `\Windows` (ROM), the boot loader, the registry or the hidden flash. Since then ✅: the ROM image (version 2.33) of the official package is unpacked in `firmware/rom/` — `nk.exe`, `coredll.dll`, drivers, `WolfSpecial.dll` (`WolfSpecialAPI.dll` is only a fallback name in the wrappers), `openGL_ES_STA2065.dll`, `mmtimer.dll`, `Ciao.exe`, `explorer.exe`, `GarminTestMode.exe`, 14 fonts, the three default registry hives — and analysed in [hardware-layer.md](hardware-layer.md) and [boot-and-supervision.md](boot-and-supervision.md). `svoxdll.dll` (imported by `spxdec.dll`) is in neither the ROM nor the flash ✅.
Still not available ❓: the live registry of a unit (`\HIVEREG`), the hidden-flash content of a real unit, the BT module firmware, a disassembly of the boot-loader stages (`firmware/os/*.bin`, strings only).

OS processes seen in crash dumps: `nk.exe`, `udevice.exe` ×4, `servicesd.exe`, `Ciao.exe`.

Size by type: `.map` 2239 MB (45) · `.nfs` 1105 MB (74) · `.zip` 53 MB (manuals) · `.dat` 38 MB · `.exe` 33 MB (23) · `.res` 33 MB (6) · `.dll` 21 MB (70, 30 unique names).

**Timestamps:** bulk mtime 2026-08-11 13:11 (NAVIGON), 2026-08-10 03:13 (MN Settings) = copy time ✅. Exception: `ErrorReporter/*_20260814|15*` carry device times → dump refreshed ≥ 2026-08-17.

**macOS artifacts:** `<dump>/NAVIGON/.fseventsd/no_log` + `.metadata_never_index` have the same mtime / mode as device files → came from the device volume (❓ still on the device; harmless, not in `content.md5`). `<dump>/NAVIGON/.DS_Store` (Nov 15, mode 644) = local Finder file, not from the device. None in `MN Settings`.

## 3. Binaries (PE census, 93 files)

All PE32, subsystem 9 (WinCE GUI). No x86.

| PE machine field | Count | Which |
|---|---|---|
| `0x1c2` = `IMAGE_FILE_MACHINE_THUMB` (ARMV4I build) | 86 | everything first-party |
| `0x1c0` = `IMAGE_FILE_MACHINE_ARM` (ARMV4) | 7 | `MFC80U.DLL` ×2, `MSVCR80.DLL` ×2, `spxdec.dll`, `MortScript.exe`, `autorunce.exe` |

The field only names the build target: the code itself is **ARM mode, not Thumb**, in every program analysed ✅ (all `.pdata` entries carry the 32-bit flag; e.g. [user-shell.md](user-shell.md), [navigation.md](navigation.md)). The loader accepts both values. None of the first-party programs carries RTTI or symbols.

Subsystem version: 6.0 ×52, 5.0 ×25, 4.10 / 4.20 ×15, 3.0 ×1 (MortScript).
Toolchain (PDB paths + linker version): **VS2008 (linker 9.0) + CMake / nmake + `WolfSDK (ARMV4I)`**, config RelWithDebInfo; first-party build date 2014-11-14. Linker 8.0 (VS2005) only for third-party blobs (BT stack, VoCon, Cidana, SyncML, TmcHal).

Two program generations ✅: **MN8** (`VwUserShell`, `VwBrandDevice`, `MediaPlayer`, `PictureViewer`: data-driven "NgGUI" toolkit, [ui-framework.md](ui-framework.md)) and **MN7** (`navigon`, `PhoneApplication`: older toolkit with `.res` archives). Both toolkits are statically linked and draw with plain GDI (DIB + `BitBlt`), text through FreeType; ❌ not SDL, not DirectDraw, not `sgalib`. Only navigon's map uses OpenGL ES.

Static imports (non-COREDLL):

| Exe | Imports |
|---|---|
| `navigon.exe` (10.5 MB) | NgAlloc, Png, Hardwarelayer, NgAppCom, toolhelp, WS2, fl_core, vocon_ext_*, WININET, iphlpapi, **openGL_ES_STA2065**, TmcHal, SDL (audio only), vauto_* |
| `VwUserShell.exe` | NgAlloc, HWLHardwareLayer, ACAppCom, Png, MMTimer, WINSOCK |
| `VwCommunicationExe.exe` | NgAlloc, HWLHardwareLayer only (no AppCom) |
| `VwBrandDevice.exe` | NgAlloc, HWLHardwareLayer, Png, MMTimer, WINSOCK (no AppCom: the `ACAppCom.dll` in its folder is unused) |
| `MediaPlayer.exe` / `PictureViewer.exe` | NgAlloc, HWLHardwareLayer, ACAppCom, (Png), MMTimer, WINSOCK |
| `MultiMediaDominationProcess.exe` | NgAlloc, `ci_playback_sdk_wince6_r.dll` |
| `PhoneApplication.exe` | NgAlloc, Hardwarelayer, NgAppCom, fl_core, vocon_ext_*, toolhelp, SDL (audio), vauto_*, Png; loads `nfBT.dll` / `nfVR.dll` at run time |
| `BTCore.exe`, `APBTDll.exe` | MFC80U, msvcr80 (+ SyncBL, MAPBL) |
| `VolumeUI.exe` | NgAlloc, Hardwarelayer, NgAppCom; MFC 9.0 **statically linked** (no MFC DLL import) |
| `ScreenShot.exe` | NgAppCom |
| `VwPaint.exe` (not in the package, §7) | NgAlloc, Png, WS2, MMTimer — **AppCom statically linked** (exports 168 `AppCom::` symbols); no SDL |
| `ManualReader.exe` | Png, ole32, commctrl, commdlg — a wxWidgets HTML reader; no NgAlloc / SDL / vauto / AppCom import |
| `WatchdogPuppy.exe`, `ErrorReporter.exe` | toolhelp |
| `SplashAnimation.exe` | sgalib (its only user) |

DLL identities (from exports / strings):

| DLL | Really is |
|---|---|
| `SDL.dll` (52 KB, 113 exports) | SDL 1.2 **without video**: audio, events, threads, timers, RWops only. Used only for audio (`SDL_OpenAudio`…) |
| `NgAppCom.dll` (navigon, phone, VolumeUI, ScreenShot) / `ACAppCom.dll` (shell, media player, picture viewer) | the AppCom IPC library, two builds (160 vs 170 exports), wire-identical ✅ → [appcom.md](../apps/appcom.md) |
| `Hardwarelayer.dll` (207 exports, MN7) / `HWLHardwareLayer.dll` (49 exports, MN8) | C++ wrappers over the ROM's `WolfSpecial.dll` (36 plain-C exports: hidden flash, ids, brightness, speaker, cradle, power source, watchdog) → [hardware-layer.md](hardware-layer.md) |
| `nfBT.dll`, `nfVR.dll` | both export-named `BTDLL.dll` (nFore). Thin clients of `BTCore.exe`: nfBT = phone / phonebook / SMS / music API (113 exports), nfVR = mic / voice-path control (7) → [bluetooth.md](bluetooth.md) §3, §4 |
| `BTCore.exe` | **host driver** for the Bluetooth module, not a Bluetooth stack (no L2CAP / RFCOMM / profile code) → [bluetooth.md](bluetooth.md) §5 |
| `MAPBL.dll`, `SyncBL.dll`, `SyncMLDll.dll` | BT MAP (SMS) parser and SyncML phonebook sync, used by `BTCore.exe` |
| `TmcHal.dll` | Amaryllo "TMC Hal 1.0.0.132" ("TMCHal for Wolf"): TMC HAL that loads and drives the tuner driver → [navigation.md](navigation.md) §4 |
| `ADynamics4706.dll` | WinCE **stream driver** (`COM_Init/Open/Read…`) for the Si4706 tuner — not an audio DSP |
| `ci_playback_sdk_wince6_r.dll`, `LibARPresent.dll` | Cidana media playback SDK + audio renderer (`CI_Player_*`, `CiAr_*`) → [media-audio.md](media-audio.md) §3 |
| `spxdec.dll` | Speex decoder for TTS (`decodeSpeexTTS`), imports `svoxdll.dll` |
| `vocon3200_*`, `vocon_ext_*`, `fl_core.dll` | Nuance VoCon 3200 speech recognition |
| `vauto*.dll` | Nuance Vocalizer for Automotive 5 (TTS) |
| `sgalib.dll` | ST "SGA" 2D accelerator library (imports DDRAW) |
| `NgAlloc.dll` | Navigon allocator (17 exports) |
| `MFC80U` / `MSVCR80` | MS runtimes, used only by the nFore BT binaries |
| ROM: `Ciao.exe` | Garmin boot manager + driver → app event bridge → [boot-and-supervision.md](boot-and-supervision.md) §2.2 |

Copies differ: `NgAlloc.dll` 3 builds, `Png.dll` 5 builds, `SDL.dll` 2 builds — do not assume they are interchangeable.

## 4. Boot (summary)

Owner: [boot-and-supervision.md](boot-and-supervision.md) §2.

- ✅ There is no CE shell. `HKLM\init\Launch99` starts the ROM program **`Ciao.exe`**, which decides once per boot: test mode → format / factory restore → USB mass-storage mode (only when a host is attached at boot) → `DFUWizardCE.exe` → `UpdateMe.exe` → **`AutoRunCE.exe` from the SD card if there is one, else from flash** (no check of any kind) → else the "no software" picture.
- ✅ `autorunce.exe` (stock MortRunner) runs `MortScript.exe autorunce.mscr`: JIT debugger registry keys, marker files (`CopyMNSettings.txt`, `CleanMNSettings.txt`), font-link registry, `BT\ReadWriteBTAddress.exe` (copies the module's address into the factory block, only while that is blank), clear the read-only bit of `BrandDevice.reg`, start `WatchdogPuppy.exe Watchdog_HW.ini`, 5 s later `CheckForStorageCard2.exe`, delete update leftovers. Step table: [boot-and-supervision.md](boot-and-supervision.md) §2.4.
- ✅ Plugging USB into a computer while the software runs resets the unit (USB mode is entered at the next boot).

## 5. Supervision and crash handling (summary)

Owner: [boot-and-supervision.md](boot-and-supervision.md) §3–§4.

- ✅ `WatchdogPuppy.exe` starts and supervises 8 children in this order: `VwCommunicationExe` (restarted in place up to 3 times, then reset) → `VwBrandDevice` (❌ not "run once": it runs **at every boot**, the others wait until it exits) → `VwUserShell`, `navigon`, `BTCore` (restart, unlimited), `PhoneApplication`, `MediaPlayer` (adopted, not started: the shell starts it), `VolumeUI` (exit ignored; quits at once except on an XL1).
- ✅ Action codes: 0 ignore, 1 device reset, 2 restart. Heartbeat = named auto-reset event created by the watchdog; the program must `SetEvent` it within the timeout (30 s, navigon 90 s). Net effect: a crash or hang of the shell, navigation, phone app or media player reboots the unit.
- ✅ Also in `Watchdog_HW.ini`: hardware watchdog (`[HWWatchdog] WolfSafety`), partition checker (`MN Settings`, `My Flash Disk`). `[AdditionalExecutable]` = extra processes whose stack traces go into a freeze dump.
- ✅ `ErrorReporter.exe` (JIT debugger): dump to `NAVIGON\ErrorReporter\` (max 15), then device reset — except for 8 listed first-party programs, which the watchdog handles. **A crashing third-party program reboots the unit** ([building-apps.md](../apps/building-apps.md)).
- Not supervised: PictureViewer, ManualReader, VWPaint, ScreenShot, SplashAnimation, anything under `Apps\`.

## 6. AppCom (summary)

Owner: [appcom.md](../apps/appcom.md).

- ✅ App-to-app messages over UDP on `127.0.0.1`, one port per app; no car data. Wire format, variant encoding, message catalogue and a Python codec are documented there.
- ✅ Registry = `NgAppCom.xml` next to each exe: **6 byte-identical copies** (md5 `e20fb4ea…`) in `NAVIGON/`, `VwUserShell/`, `BT/`, `MediaPlayer/`, `PictureViewer/`, `VwBrandDevice/`, plus one `NgAppInfo.xml` per folder under `\My Flash Disk\Apps`. Ports: 27011 navigation, 27015 phone, 27016 / 27103 manual, 27017 shell, 27018 media player, 27019 picture viewer, 27021 volume overlay; VWPaint 27106.
- ✅ AppCom also starts and finds apps (`CreateProcess`, `FindWindow(ClassName, WindowName)`, `ShowWindow`); an app needs no AppCom traffic to be listed, started or kept in front.
- ✅ `VwCommunicationExe`, `VwBrandDevice`, `BTCore` and `ManualReader` do not use AppCom. Car data travels through shared memory ([shared-memory.md](../car/shared-memory.md)), Bluetooth through window messages ([bluetooth.md](bluetooth.md) §2).

## 7. App packaging (`\My Flash Disk\Apps\<Name>\`)

How to build one: [building-apps.md](../apps/building-apps.md). Registry keys of `NgAppInfo.xml`: [appcom.md](../apps/appcom.md) §6.2. The mechanism is ✅ verified in the emulator with `apps/hello`.

The official package ships only `Apps/ManualReader`. VWPaint was a later add-on on VW units; it is not in `firmware/flash/` (public copy: jilleb/4nsf-toolbox `Apps/VWPaint`). Its `NgAppInfo.xml` names its display name with 31 `<AppName lc=…>` lines and points `<Version>` to a `version.xml` that does not exist; ManualReader uses an `<AppNamesFile>` instead.

| | VWPaint | ManualReader |
|---|---|---|
| Built | 2012-09, **TopazSDK** | 2014-11, WolfSDK |
| In `content.md5` | no (post-install; listed in `NAVIGON/updates.xml`) | yes |
| Local DLLs | NgAlloc, Png | NgAlloc, Png, SDL, vauto* (none but Png statically imported) |
| Resources | `VW_Paint*.nfs` (base + Vw/Seat/Skoda skins), `Datastorage.nfs`, `TextResources.nfs`, `paint.bmp`, `Settings/*.default` | `DocReaderConfig.xml`, `manuals/MAN_{VW,SEAT,SKODA}.zip` (53 MB), `skin_272x470_zip/*.zip` → [navigation.md](navigation.md) §9 |

## 8. What is on the flash

`\My Flash Disk\NAVIGON\` (3.4 GB) — the navigation app; interfaces in [navigation.md](navigation.md):

- `navigon.exe` (PDB `MobileNavigator.pdb`), `VolumeUI.exe`, `ScreenShot.exe`, `CheckForStorageCard2.exe`.
- `version.xml`: 7.16.8 build 314, `NAVIGON_EU`, `VW_PID_V2`, style `VW`, prefix `N7`, ROM image 2.33, BT versions, `expirationdate` 2016-08-07, content list (RealityView, PoiWarner, PremiumTMC…, "VW Paint 1.0.0 installed"). Contains product id + VIN (PII). `platform` and `style` are written by the branding program ([brand-and-pairing.md](brand-and-pairing.md) §4.4). `version.old`, `device.old` = previous copies.
- GUI: `MobileNavigator.res` (11 MB, VW) + `skin_SEAT/`, `skin_SKODA/` `MobileNavigator.res`. `BinaryConfig.xml` → `ini_files/…` paths that **do not exist** (legacy; resources live inside `.res`, internal paths `images/480x272/Sheldor/…`).
- `map/` 2.98 GB: 45 country `.map` (largest France 393 MB, Germany 246, Spain 209, Italy 199, UK 173) + 13 `.nfs` (Terrain_EU 297 MB, PhonemeV3_EU 295, ECO_EU 81, RealityView 48, TMC 39, sat_EU, MapDrawer_{VW,Seat,Skoda}, Brunnel_EU, CountryProfiles, poicats, BasicPhonemeFetcher).
- Map vintage: embedded compiler log → dataset `EUPSF20130201` (Q1 2013), compiled 2013-05 with Navigon "Sextant" compilers v3.4.8.
- `sounds/` 327 MB (26 voices `<lc>_<Name>_tts.nfs` / `_def_sound.nfs`), `VoiceRecognition/` 19 MB, `language_files/` 29 `.lng`, `poi/` 48 `.pom` + 5 other, `tmc/` 34 small bmp, `ExampleRoutes/` 18 `.route`, `Dat/` 14, `Settings/` 72 (`Core/`, `Routing/` RT_*/SP_* ini, `Connectivity/` Query_*/Reply_* xml for the dead LIVE services).
- `Settings/ApplicationSettingsPathes.xml` = path map. SD overrides: `/Storage Card/NAVIGON/map/GCM`, `/Storage Card/NAVIGON/Routes`, `/Storage Card/Navigon/SimulationFiles/{GPS,TMC,LIVE,TMCuner}` (GPS / TMC log replay).
- Dev switches in the settings: `DevModeSimGps`, `DevModeLogUserSettingChanges`, `ShowFrameRate`, `ScreenShot`, `LogPath/LogLevel`.

Other directories:

| Dir | Content | Doc |
|---|---|---|
| root | `autorunce.exe` + `.mscr`, `MortScript.exe`, `WatchdogPuppy.exe` + 9 `Watchdog_*.ini`, `ErrorReporter.exe` + `.ini`, `Reset.exe`, `PowerOff.exe`, `SplashAnimation.exe`, `sgalib.dll`, `content.md5` | [boot-and-supervision.md](boot-and-supervision.md) |
| `VwUserShell/` | main menu / car screens, `VwCommunicationExe.exe` + `VwCommunicationManager.ini`, skins Vw / Seat / SeatCupra / Skoda `.nfs`, `EULA*.ini`, 19 `.default` | [user-shell.md](user-shell.md), [shared-memory.md](../car/shared-memory.md) |
| `VwBrandDevice/` | brand selection at first contact with a car; runs at every boot | [brand-and-pairing.md](brand-and-pairing.md) |
| `BT/` | `BTCore.exe`, phone UI, VoCon data, own `PhoneApplication.res` + Seat / Skoda skins, 29 `.lng` | [bluetooth.md](bluetooth.md) |
| `MediaPlayer/` | player + `MultiMediaDominationProcess.exe` (Cidana decode process), `logos.afs` (station logos) | [media-audio.md](media-audio.md) |
| `PictureViewer/` | picture browser | [media-audio.md](media-audio.md) §9 |
| `ChangeStartupScreens/{0..3}[/Fender\|/Cupra]` | per-brand `SPLASH.RGBFS`, `NOSW.BMP`, `wallpaper.bmp`, `usb.bmp` | [brand-and-pairing.md](brand-and-pairing.md) §5.2 |
| `Apps/` | add-on apps (§7) | [building-apps.md](../apps/building-apps.md) |

`VwCommunicationExe` ✅: owner of the car link on `COM9:` (raw CAN + BAP), publishes to shared memory. The log switches `VwLogVerbosity{Serial,Can,Bap}`, `VwLogPathCE=/Storage Card/` in its ini are **inert** (strings absent from the binary); the Vector `xl*` names belong to a PC bench transport. ✅ CAN itself is not on the unit side ([car-link.md](../car/car-link.md)).

## 9. File formats (census)

Where each format is specified, and how well: [file-formats.md](file-formats.md).

| Ext | Count | What | Magic / notes |
|---|---|---|---|
| `.map`, `.nfs`, `.afs` | 45 / 74 / 1 | **same container** ("NgFs") | `11 FE D5 0B 05 00 00 00 00 10 00 00` |
| `.res` | 6 | MN7 GUI resource archive (navigon, phone) | `01 00 00 00` = version; then data offset, count, file size |
| `.rgbfs` | 10 | splash animation | `20 00 00 00 "SA" 01 01`; 32-byte file = header only (empty) |
| `.kbi` | 32 | Navigon key → blob store. The 32 loose ones are **voice-recognition contexts** (`*_Control_Context.kbi`, in `VoiceRecognition/`); the UI stores live inside `.nfs` | `tnatxes` |
| `.ctx` | 84 | VoCon context templates (`Template_*_ID_*.ctx`), all in `VoiceRecognition/` | |
| `.dat` | 82 | 67 VoCon models (`acmod3_*`, `ddg2p_*`, `clc_*`); `Dat/GD*.dat` (magic `04 0B 08 11`, zlib chunks, purpose ❓); `Dat/MN.dat`, `BT.dat` (64 KB, mostly 0xFF, purpose ❓) | |
| `.pom` | 48 | POI data (`CNG_<ISO3>`, POI / hotspot warner) in `poi/` | format ❓ |
| `.lng` | 59 | UTF-8 text, `<lc>_480_272.lng` | |
| `.default` | 61 | XML settings seeds → `\MN Settings` | |
| `.store` | 2 (+ copies) | **binary** serialized favourites / recents (`FF FF 00 00 01…`) | |
| `.route` | 18 | example routes (same serializer) | |
| `.ini` | 43 | watchdogs, routing profiles, EULA text | |
| `.bin` | 2 | `Dat/DD_Car.bin`, `Connectivity/ConnectionConfigurationFile.bin` | ❓ |

Extractor for the container: `tools/nfs_extract.py`.

## 10. `\MN Settings`

Root: `UserSettings.xml` (15 KB master, + `.old`), `UserSettingsLanguage.xml` (ita-ITA, voice Francesca), `UserData.xml`, `UserDataLocalSearch.xml`, `RouteProfilesSettings.xml`, `Favorites.store`, `RecentTargets.store`, `gpsData.dat` (256 KB; ✅ backup written by the GPS driver, [hardware-layer.md](hardware-layer.md) §5.3), `GD.dat`, `ITISessionData.dat`. At run time also `NavigationRoute.resy` (route to resume after a reset, [navigation.md](navigation.md) §1.7).
Subdirs: `BT/` (phone-app settings + `Phones/<MAC>/{PhoneBook,device,Presets}.dat`), `BT_A/` (files of `BTCore.exe`: `BTEnv.txt`, `CallLog.txt`, `contacts3.txt`, `reconnect.dat`; [bluetooth.md](bluetooth.md) §6), `Connectivity/fcdcounter.ini`, `MMP/` ([media-audio.md](media-audio.md) §8.6), `PV/`, `VwUSh/` (`ApplicationSettings.xml`, `UserSettingsTrip/Tubes/ThinkBlueStorage.xml`; all files and keys: [user-shell.md](user-shell.md) §9), `Backup/{MMP,PV,VwUSh}/*.old`, `Logs/LIVE/` (empty — no logs present).
Each `.xml` has a `.old` twin (previous version); seeds are the `.default` files next to each program.
`VwUSh/UserSettingsTubes.xml` documents the car gauges: RPM, coolant, oil temperature, gear shift, outside temperature.
Structure of navigon's `UserSettings.xml`: [navigation.md](navigation.md) §2.

## 11. Integrity / licensing

- `content.md5`: 771 lines ✅ = header `# MD5 Checksums generated with SCP` + blank + 769 entries `md5 *Flash\<path>`. Result vs dump: **766 match, 2 differ (`NAVIGON\device.xml`, `NAVIGON\version.xml` — per-device), 1 missing (`BrandDevice.reg`, 0-byte)**. → flash is factory-clean except per-device files.
- Not covered (79 files): VWPaint, `NAVIGON\Dat\*`, ErrorReporter dumps, `updates.xml`, `*.old`, `BT\VoiceRecognition\ITISessionData.dat`, `content.md5` itself, macOS artifacts.
- ✅ **Nothing verifies `content.md5`**: no program on flash, in the ROM or in the update package references the name; it is a build artefact ([boot-and-supervision.md](boot-and-supervision.md) §7.4). Changed or added files are not detected at boot (VWPaint and the community toolbox apps, none of them listed, run fine).
- ✅ **No VIN lock and no unlock code.** The VIN of the car is stored in the hidden flash at branding; shell and navigon only use it to recognise the "home car" and silently adopt a new one. Separate and optional: the user-set Navigon password lock ([brand-and-pairing.md](brand-and-pairing.md) §6).
- `NAVIGON/device.xml` ✅ = navigon's **key store** ([navigation.md](navigation.md) §7; XML root `/device/`; strings `device.xml` / `RC-CE` at file offsets `0x950608`, `0x9470dc` of `navigon.exe`); `PhoneApplication.exe` also names the file.
  - 14 codes `C1P3, C1P4, C1P18, C3P1 … C3P19` (three groups of six characters), name = `C<class>P<product>`. ✅ **Identical** on a real unit and in the official package's generic file → product-level keys shipped with the software, not tied to a unit.
  - Validation ✅: a code is accepted when its decoded class equals the requested class and the date packed into it is not before today (default 2038-12). No device id enters that test. Class 1 = list of valid TMC providers; classes 5 / 7 gate "JunctionView"-style content; class 3 and the product ↔ feature table ❓.
  - `RC` and `RC-CE` (13 characters each): ✅ only on a used unit, absent from the package; `device.old` has `RC` but not `RC-CE` → written by the software after installation, in two steps. Unit-specific. ✅ Not stored anywhere else on flash or in `\MN Settings`. ✅ They are the unit's registration code towards the discontinued Navigon Fresh / LIVE back end; nothing local depends on them.
- `NAVIGON/mis.txt`: 45 MD5-sized hex strings separated by `;`. ✅ Byte-identical on a 2013-map unit and in the 2020-map package, not the MD5 of any map file or map name. ✅ Role: payload of the LIVE login / activation request — **not a local check**.
- `expirationdate` 2016-08-07 (`/config/software/expirationdate`): ✅ state of the LIVE subscription only; the "Expired! please update" text belongs to the assisted-GPS ephemeris info page. No effect on navigation (the unit still runs in 2026).
- Maps are plain NgFs containers without crypto; whether a map needs a class-3 key to be listed ❓ (the shipped `device.xml` covers the shipped maps).
- `.DeviceConfigurationBackupFile/version_bkp` = byte-identical to `version.xml` ✅.

## 12. Crash dumps (`NAVIGON/ErrorReporter/`)

7 entries: Exceptions 2021-05-15, 2021-06-12, 2025-04-17 = **same kernel fault** `nk.exe` 0xC0000005 @ `0xC09924B4` (driver bug); Freezes 2024-08-05, 2025-12-29 (no stack saved), 2026-08-14 (names `PhoneApplication.exe`); Exception 2026-08-15 = `\Storage Card\dumpcoredll.exe` (user test binary, AV @ 0x11098; not in dump).
Each dump = stack trace + process memory / thread times + **snapshot of `\MN Settings`** (PII). Free RAM at crash ≈ 21–32 MB; `navigon.exe` ≈ 115–129 MB private.
Who writes which dump type, and the folder layout: [boot-and-supervision.md](boot-and-supervision.md) §4.

## 13. PII locations (do not publish)

- `NAVIGON/version.xml`, `version.old`, `.DeviceConfigurationBackupFile/version_bkp`: VIN, product id.
- `NAVIGON/device.xml`, `device.old`: the `RC` / `RC-CE` values are unit-specific (the 14 `CxPy` codes are public).
- `MN Settings/UserData.xml`, `gpsData.dat`, `Favorites.*`, `RecentTargets.*`, `UserDataLocalSearch.xml`: positions, destinations.
- `MN Settings/BT/Phones/<MAC>/`, `BT_A/BTEnv.txt`, `CallLog.txt`, `contacts3.txt`, `reconnect.dat`: phone names, BT MACs, call log.
- `MN Settings/UserSettings.xml`: paired phone model.
- `NAVIGON/ErrorReporter/**` (dirs + 4 zips): full copies of the above + `*_GPS.txt`.
- Hidden flash (not reachable over USB): VIN (two copies), device id, BT address ([hardware-layer.md](hardware-layer.md) §3).

## Former open questions

| Question | Answer |
|---|---|
| How CE starts `autorunce.exe`; ROM / registry / driver contents | ✅ `Ciao.exe` via `HKLM\init\Launch99`, SD card first — [boot-and-supervision.md](boot-and-supervision.md) §2; ROM and registry — [hardware-layer.md](hardware-layer.md) |
| Watchdog action codes; `[AdditionalExecutable]` | ✅ §5 — [boot-and-supervision.md](boot-and-supervision.md) §3.1, §3.3 |
| AppCom wire protocol; must an app speak AppCom to stay in front | ✅ documented; no — [appcom.md](../apps/appcom.md) §2–§4, §7 |
| How `device.xml` codes are validated; `mis.txt`; `expirationdate`; are maps bound to a unit | ✅ §11 — [navigation.md](navigation.md) §7 (class-3 detail still ❓) |
| Does the ROM or the updater check `content.md5` | ✅ no — [boot-and-supervision.md](boot-and-supervision.md) §7.4 |
| Car-side transport behind `COM9:` | ✅ serial protocol to the radio, which gateways CAN — [car-link.md](../car/car-link.md) (electrical side ❓) |
| CPU core / ISA level; purpose of `Dat/MN.dat`, `Dat/BT.dat`, `GD*.dat` | ❓ still open → [architecture.md](../architecture.md) "What is still unknown" |
