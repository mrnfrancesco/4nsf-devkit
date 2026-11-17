# UI framework ("NgGUI") and its resource formats

What: the proprietary Navigon/Garmin toolkit statically linked into the "MN8" programs (`VwUserShell.exe`, `VwBrandDevice.exe`, `MediaPlayer.exe`, `PictureViewer.exe`) and the data files that drive it. The older "MN7" programs (`navigon.exe`, `PhoneApplication.exe`) use `.res` archives instead (§10).
Why: every screen, action sequence, text, colour, font and bitmap of the MN8 programs lives in `.kbi` stores inside `.nfs` containers. The C++ code only supplies numbered building blocks; the data wires them together. Reading the data is enough to redraw, reskin or extend the UI.

Legend: ✅ verified (parser run on the real files, or disassembly — `S:` = VA in `VwUserShell.exe`) · 🟡 inferred from structure / one source · ❓ unknown. Nothing tested on a real unit.
Related: NFS container → [file-formats.md](file-formats.md) · `.RGBFS` splash → [brand-and-pairing.md](brand-and-pairing.md) §5.3 · what the shell's screens do → [user-shell.md](user-shell.md) · BrandDevice's workflows → [brand-and-pairing.md](brand-and-pairing.md) §4 · watchdog heartbeats → [boot-and-supervision.md](boot-and-supervision.md).
The MN8 programs carry **no RTTI**: class names below are descriptive, identity = class id + constructor / vtable address.

## 1. Overview

```
<App>.nfs ──────────── Controls_480x272.kbi   screens / pop-ups: trees of controls          ┐
                       Workflows.kbi          named command trees ("workflows")              │ node trees (§3)
                       Startup.kbi            start sequence, steps run in key order         │ = serialized C++ objects,
                       Shutdown.kbi           stop sequence                                  │ created by class factories
Datastorage.nfs ────── Datastorage.kbi        settings files + keys, screen history, lists   ┘ from (type, class id)
<App>_<Brand>Skin.nfs  Bitmaps_480x272.kbi    PNXF images (§6)      Colors_480x272.kbi  4-byte colours
                       Fonts_480x272.kbi      TrueType file + pixel size
TextResources.nfs ──── <lang>_480x272.kbi     UTF-8 strings
```

| Runtime piece | Role | Evidence |
|---|---|---|
| App thread (process main thread) | window, message pump, paint; heartbeat event 1 | S:0x17db80 |
| `NgGUIEvntThread` | raised events → subscribed commands; heartbeat event 3 | S:0x1024bc |
| `NgGUICmdThread` | executes commands one at a time (two FIFO queues + timers); heartbeat event 2 | S:0x1054f8 |
| `NgGUIDrawThread`, `NgGUIImageCache`, `NgGUIRemoteControlThread` | repaint, bitmap cache, test remote control | S:0xf4038, 0x15690c, 0x182af4 |
| 6 class factories | create every object named in the data | S:0x915f4, §4 |
| Resource manager | opens the `.nfs` / `.kbi` files, loads records by id | S:0xfe5a0, 0xfdc7c, 0xff30c, 0xfcab0 |

## 2. `.kbi` container ✅

Paged key → blob store with a B-tree-like index (magic = "sextant" reversed; version resource string `Sextant`). Little-endian, unaligned, **no checksum, no compression** in any file on the unit.
Verified with `tools/kbi_extract.py` (§12) on all 220 stores (188 inside the four programs' `.nfs`, 32 loose voice-recognition contexts): every index node and page walks cleanly, key ranges and page counts match the header.

```c
#pragma pack(1)
struct KbiHeader {            /* 0x00, 42 bytes; parsed at S:0x23e54c */
    char  magic[8];           /* "tnatxes\0" */
    u32   zero[4];
    u8    b18;                /* 0 */
    u8    b19;                /* 0xff */
    u8    junk[15];           /* stale memory of the PC build tool; differs between otherwise equal files */
    u8    version;            /* 3; anything else is rejected (S:0x232dc8) */
};
struct KbiTree {              /* 0x2a, 44 bytes; parsed at S:0x23e300 */
    u64   key_min, key_max;   /* smallest / largest key stored */
    u32   page_size;          /* 0x400 (UI stores), 0x800 (voice contexts) */
    u32   index_nodes;        /* number of index nodes (one file says 6 and has 5) */
    u32   unk;                /* 0x400; 0x80 in text stores; 0x2000 in voice contexts ❓ */
    u32   data_pages;         /* number of data pages */
    u32   len_width;          /* bytes of a record length field: 1, 2 or 3 */
    u32   key_width;          /* bytes of a key: 1, 2 or 4 */
    u32   root_len;           /* length field of the root index node */
};
struct IndexNode {            /* root at 0x56; level 1 points to data pages, level 2 to level-1 nodes, level 3 seen once */
    u8    level;
    struct {
        key  first, last;     /* key_width bytes each: key range of the child */
        u32  offset;          /* absolute file offset of the child */
        u32  len;             /* child length + 1 */
        u32  len2;            /* = len in every file ("raw-inflater"/"sfs-zip-inflater" strings exist, unused here) */
    } slot[];                 /* root: (root_len-1)/slot size; other index nodes: len 0x3fd, unused slots all zero */
};
struct DataPage {
    u8    level;              /* 0 */
    struct { key k; uint(len_width) n; u8 value[n]; } rec[];   /* ascending keys, up to offset + len - 1 */
};
```

- The root node sits in a region of `page_size` bytes at 0x56; the first child starts at `0x56 + page_size`. An empty store is 3158 bytes.
- `len` counts one byte more than the node holds (nodes are stored back to back or on `page_size` steps, so that byte is padding or the next node's level byte).
- A record is never split: a page simply grows past `page_size` when one record is big (bitmaps up to 263 KB).
- **Name table**: key `0x10000000 | id` → ASCII `"<id>,<Name>.dat\0"` = the designer's name of record `id` (`1,VW_Dia_MainMenu.dat`; bitmaps keep the full PC path `D:\Work\T1_Sheldor_KW4514_Seat\…\Back.png`). The runtime only uses numeric ids.

| Store | Key | Value |
|---|---|---|
| `Controls_480x272.kbi` | screen / pop-up id | node tree, root = one control |
| `Workflows.kbi`, `Startup.kbi`, `Shutdown.kbi` | workflow id / step number | node tree, root = one command (key field 0xffff) |
| `Datastorage.kbi` | config id (1001, 1002, 1003, …) | node tree (§5.5) |
| `Fonts_480x272.kbi` | font id | node `t15 { int[1] = pixel size, str[2] = "NeoTechVWMedium.ttf" }` |
| `Colors_480x272.kbi` | colour id | 4 bytes `B G R 00` (= u32 `0x00RRGGBB`) ✅ (`EcoBleuPoints` = `ff b1 00 00`) |
| `Bitmaps_480x272.kbi` | bitmap id | `PNXF` image (§6), no prefix |
| `<lang>_480x272.kbi` | text id (key width 1 or 2) | UTF-8, NUL-terminated; placeholders `%s1`, `%s2`; line break written as the two characters `\n` |
| `*_Control_Context.kbi` (BT + NAVIGON voice recognition) | 30…37 | VoCon binary context ❓ (not UI) |

## 3. Node tree encoding ✅

Reader: S:0x11499c (recursive). Every tree is a serialized object graph.

```c
struct Leaf      { u16 type; u16 key; u32 value; };                 /* type 1 = s32, 2 = float32, 3 = bool (0/1) */
struct String    { u16 type /*4*/; u16 key; u16 n; char s[n]; };    /* UTF-8, NUL-terminated, padded: 6+n is a multiple of 4 */
struct Container { u16 type; u16 key; u16 id; u16 n; u8 children[n]; };   /* every other type = an object */
```

For a container the reader asks the factories for an object `(type, id)`; unknown → the subtree is skipped. Each child is then handed to the new object through five virtual setters, `key` saying which property it is:

| Child type | Call on the parent (vtable offset) |
|---|---|
| 1 int | `+0x18 SetInt(&key, &value)` |
| 2 float | `+0x1c SetFloat` |
| 3 bool | `+0x20 SetBool` |
| 4 string | `+0x24 SetString` |
| object | `+0x28 SetObject(&key, &child)` — the setter checks the child's type with `+0x0c IsType(n)` |

Every class starts with the same vtable: `+0 dtor, +4 AddType, +8 AddClassId, +0xc IsType, +0x10 IsClass, +0x14 GetId`, then the five setters ✅ (base ctor S:0xf1d78). A constructor = base ctor, store vtable, `AddClassId(id)` / `AddType(type)` — this is how ids were matched to code. Same key may repeat (lists). Keys < 100 belong to the family's base class, ≥ 100 to the concrete class.

## 4. Object families and class ids

Factories registered at start (S:0x915f4), each a `Create(type, id)` plus one creator per family:

| Factory vtable | Id range | Content |
|---|---|---|
| S:0x324ef8 | 1…52, 701 | framework core (commands 1…52 + 701, controls 1…25, providers 1…27, …) |
| S:0x34f3d0 | 1001…1079, 1501…1504 | application layer: settings, history, message boxes, speller, texts (+ node types 1005…1039) |
| S:0x3238a8 | 2001…2013 | device: battery, power, reset, cradle, card (`HardwareLayer`) |
| S:0x357378 | 10001…10025 | AppCom layer 🟡: shared status lists (GPS, phone… icons of the status bar), shared workflows / bitmaps, application switching |
| S:0x2f9fd0 | 16001…16198 | VwUserShell features ([user-shell.md](user-shell.md)) |
| S:0x2fd668 | 20001…20040 | VW car data (shared memory, [can-signals.md](../car/can-signals.md)) |

BrandDevice adds 17001…17010 ([brand-and-pairing.md](brand-and-pairing.md) §4); MediaPlayer has 18001…18023 / 19001…19065 (the audio-relevant ones: [media-audio.md](media-audio.md)), PictureViewer its own range (not analysed). The shell's full table id → constructor → vtable has 893 entries (not reproduced; the ids that matter are in [user-shell.md](user-shell.md)).

Node types (✅ = setter / consumer read in the code, 🟡 = from keys and usage in the data):

| Type | Name used here | Children (key: meaning) | |
|---|---|---|---|
| 5 | **control** | §5.1 | ✅ base ctor S:0xf942c |
| 6 | bitmap element | 1,2,3,4 x y w h · 100 / 102 / 103 bitmap id normal / pressed / disabled (`Btn_x.png`, `_p`, `_d`) · 101 4th state 🟡 · 9: effect (29) | ✅ names |
| 12 | text element | 1…4 box · 8 text id · 11 font id · 100 / 101 / 103 colour id per state · 9, 10 alignment 🟡 · 6, 12 ❓ | ✅ ids resolve |
| 17 | item (cell of control #12) | 3,4 size · 13 item value · 9: action (9) · 11: text elements · 12: bitmap elements · 15: updater (18) | ✅ S:0x114d0c |
| 7 | **command** | 1: follow-up (8) · ≥100 parameters | ✅ base ctor S:0xf66c8 |
| 8 | follow-up | 1: one command or workflow call · 2: result label(s) · 3 bool: remove after first run | ✅ S:0x113c98… |
| 33 | workflow call (a command) | 100: workflow id · 1: follow-ups | ✅ S:0x156ea4: loads the record once (S:0xfd8d0), runs it, returns its result |
| 9 | action binding | 1: command · 2: trigger kind (§5.1) | ✅ S:0xf8358 |
| 10 / 11 | value binding / value source | 10 = { 1: source (11), 2: target property }; 11 = class with parameters | 🟡 |
| 18 | **updater** | 1: providers (19) · 3: event ids that re-evaluate it · 4: period ms (−1 = none) | ✅ S:0x108090, 0x107ee0 |
| 19 | **provider** (condition / value) | 200: nested providers · 100, 101 bool modifiers (negate 🟡) · ≥201 parameters | ✅ |
| 24 | handler | 1: command · 2: event id(s) · 3: timer interval ms · 5: timer mode · 6: handle key · 4: filters (25) | ✅ S:0x17c2bc, 0x17be78 |
| 21 / 22 / 32 | list template / row template / decoration | list box contents (control #2 key 300) | 🟡 |
| 29 | effect of an element | ids 3, 5, 6, 7; 100…104 | ❓ |
| 13, 20, 30 | — | 13 = { 1, 2 } at control key 10 → `+0x110(a, b)` ❓ · 20 at key 15 ❓ · 30 = gauge model (VW ids) 🟡 | |
| 15 | font | 1: pixel size · 2: file | ✅ S:0x158828 |
| 1009…1012, 1013 / 1014, 1016…1018, 1001…1008 | data-storage records | §5.5, speller layouts | ✅ / 🟡 |

## 5. Semantics

### 5.1 Controls (`Controls_480x272.kbi`)

Record 0 `Application` = root container that pre-declares the screen ids; every other record is one screen / pop-up / reusable part (153 in the shell: `VW_Dia_*` screens, `VW_PopUp_*`, `VW_RollMenu_*`, `Speller_*`, `VW_Tubes_*` gauges, message boxes).

Base properties of every control ✅ (S:0xf6ab8, 0xf6be4, 0xf8358):

| Key | Meaning | Key | Meaning |
|---|---|---|---|
| int 1, 2, 3, 4 | x, y, width, height (relative to parent) | obj 8 → type 9 | action: run command on trigger kind |
| int 11 | control id (unique in the program; commands address controls by it) | obj 9 → type 10 | value binding |
| int 12, 14 | ❓ | obj 13 → type 18 | updater |
| bool 5 | visible (`+0xa4 SetVisible`) | obj 10 → 13, 15 → 20, 16 → 26, 17 → 27, 18 → 29 | ❓ |
| bool 6 | enabled (`+0x9c`) · bool 7 ❓ | obj 200 → type 5 | child control (containers) |

Trigger kinds of an action: **4 = tap** on a button / item (1,216 of 1,459 uses) 🟡 · **8 = shown, 9 = hidden** ✅ (fired by `SetVisible`, S:0xf99bc; also 21 / 22) · 5, 6, 7 other touch phases of a button ❓ · 200, 201, 301 list box events ❓.

Control classes (uses counted over the four programs):

| Id | Class (descriptive) | Own keys | Uses |
|---|---|---|---|
| 1 | container / screen | 200 children | 408 |
| 9 | include: inserts record `int[11]` of the same store ✅ data (ids 535 / 537 / 539 / 542 in the main menu = `VW_RollMenu_*` records) | — | 111 |
| 13 | row container (lays children out) 🟡 | 300, 301 | 65 |
| 3 | bitmap | 100 bitmap id | 257 |
| 5 | button | 100 / 102 / 103 background normal / pressed / disabled ✅ · 104 text element · 105 bitmap element (icon) | 827 |
| 11 | label | 101 text elements (several = one per character cell) · 102 bitmap elements | 672 |
| 12 | item set: N items, shown as cells or one item selected by value 🟡 | 100 items (17) · 101 mode · 102 | 498 |
| 2 | list box / roll menu | 300 template (21) · 301…312 | 31 |
| 20 | value / formatted text 🟡 | 100…128 | 196 |
| 4, 8, 18, 19, 24, 25 | 4 ❓ · 8 ❓ (25 ints) · 18 gauge with model (30) · 25 table of 97 ints | | 30, 2, 7, 5, 0, 31 |
| 1001 / 1002 | speller key / keyboard (layout 1001…1003) | | 90 / 14 |
| 1003, 1005, 1016, 1020 | 🟡 option list, slider, date-time picker, chart | | 5, 4, 2, 12 |
| 16001 / 16002 | tube gauge: 401 pre-rendered frames in key 110 / 3 × 81 frames | | 30 / 5 |

**Updaters** keep a control in sync with the program state. An updater is itself a command: when activated it subscribes to its events (`int[3]`) at the event manager and, if `int[4]` is set, to a timer of the command thread ✅ (S:0x107ee0); each run evaluates its providers and applies the result. The updater class says what is applied ✅: #1 visible + enabled of any control (S:0x175ef4); #2 the same plus text / bitmap of a button (S:0x175bb0); #3, #4… the variants for item sets, labels, lists (S:0x1757b4…0x173f40). Providers come in kinds: state (visible, enabled), content (text, bitmap), integer.

### 5.2 Commands and workflows

A command is an object with `Execute()` (vtable `+0x2c`) returning an integer, plus follow-ups ✅ (S:0xf6390):

```
run(cmd):  r = cmd.Execute()
           for f in cmd.followups (in file order):
               if f.labels is empty or r in f.labels:  run(f.command);  if f.once: remove f
```

- `Workflows.kbi` record = one root command; a **workflow call** (type 33, `int[100]` = id) runs it wherever a command may stand. There is no separate state machine: "states" are screens, transitions are commands bound to actions and handlers.
- The "if" command #23 returns 1 / 0 for a state provider, or the provider's integer (so it is also a switch) ✅ S:0x15e090. Typical: `then { cmd…, int[2] = 1 }` = true branch, `int[2] = 0` = false branch, no label = always.
- Commands run on the command thread; a command may queue further commands.

Most used commands (✅ = `Execute` read):

| Id | Meaning | Parameters |
|---|---|---|
| 1 | show / hide control ✅ S:0x160814 | **101 control id · 100: 0 show, 1 hide**, default 2 = toggle (ctor S:0x160760; key assignment ✅ from the shell's data, [user-shell.md](user-shell.md)) · returns 100 / 101 |
| 5 / 6 | next shutdown / start-up step (✅ calls the sequence managers S:0x10c600 / 0x10c20c, 🟡 name) | — (ends each `Shutdown` / `Startup` record) |
| 7 / 8 | close / open screen or pop-up; #8 loads the record through the resource manager when it is not in the tree yet (✅ S:0x15fc8c, 0x15fd90; details 🟡) | 100 record id · 101 handlers (24) |
| 12 | begin shutdown sequence (✅ S:0x15f5c0 → 0x10c490, 🟡 name) | — |
| 22 | raise event ✅ S:0x10ff00 | 100 event id |
| 23 | if / switch ✅ | 100 provider(s) |
| 27 / 29 | unregister / register handler ✅ S:0x15dae8, 0x15d98c | 100 handler (24) |
| 16 | select item of an item set 🟡 | 100 control id · 101 item |
| 45 | set run-time variable 🟡 | 101 variable id · 102 / 103 / 106 / 108 value |
| 44 | call `Execute` of control `int[100]` ✅ S:0x15c210 (used as "start", 5 uses). Not a workflow start: workflows are started by node type 33 | |
| 701 | create the three heartbeat events ✅ S:0x15b274 | 101, 102, 103 names: app, command, event thread |
| 1003, 1026, 1028, 1035 | app layer 🟡: set setting · switch screen (history) · — · choose by setting | |
| 1004 | message box 🟡 | 100 pop-up record · 101 / 103 text ids · 105 buttons (1008) |
| 1502 | 🟡 text / log with `str[101]` | |

### 5.3 Events, handlers, timers ✅

- Event = integer id (same numbering scheme as class ids: 10002, 16030, 20013…). `cmd #22` raises one; C++ code raises others (car data changed, power, AppCom).
- Handler (type 24) `Register()` (S:0x17be78): subscribes its command to every `int[2]` event at the event manager, and, if `int[3]` ≠ 0, starts a timer on the command thread (interval ms, mode `int[5]`). `int[6]` names the handler so that it can be removed later.
- The event thread pops raised events and queues the subscribed commands; updaters listening to the event are re-evaluated 🟡 (loop S:0x1024bc read, dispatch not traced).

### 5.4 Start-up and shutdown

`Startup.kbi`: records 0…N executed in key order; each ends with follow-up `cmd #6` ("continue"). Shell: 110 steps — load settings, text resources, create heartbeat events (step 8), attach car shared memory, register handlers, open the first screen. `Shutdown.kbi`: same with `cmd #5`. Step lists for BrandDevice: [brand-and-pairing.md](brand-and-pairing.md) §4.1.

### 5.5 Data storage (`Datastorage.kbi`) ✅ structure

| Record | Content |
|---|---|
| 1001 `SettingsConfig` | `t1010 { str 1 = "/MN Settings/VwUSh/", 2 = "./Settings/", 3 = "/MN Settings/Backup/VwUSh/" }` (user, default, backup directory) · 23 × `t1011 { int 1 = file id, int 2 = 1001, str 3 = "<Name>.xml", 4 = "<Name>.default", 5 = "<Name>.old", bool 6, 7 }` · 282 × `t1012 { int 1 = key id, str 2 = XPath }` e.g. 16510 → `/Settings/NoNavigation` |
| 1002 `FeatureListConfig` | `t1013 { 1, 2, 3 }` |
| 1003 `HistoryConfig` | screen-history rules: `t1017 { t1018 { screen, … }, enter / leave commands, handlers }` 🟡 |
| 16056, 16086, 20001 (shell) | trainer exercise list · `LanguageInfo` (`eng-JPN` → text 948, `chi-CHN` → 949) · content groups |

A setting is addressed as (file id, key id): provider #1016 `{201 file, 202 key, 203 value}` = "setting equals value" ✅ ([brand-and-pairing.md](brand-and-pairing.md)). Files are XML: the user copy in `\MN Settings\<App>\`, seeded from `<App>\Settings\*.default`, previous version kept as `.old` in the backup directory. Run-time variables (provider #25, command #45) live in memory only.

## 6. `PNXF` bitmap ✅

Palette image, RGB565 colours, optional 8-bit alpha per palette entry. Loader S:0x257908. Verified: all 12,482 images of the 11 bitmap stores satisfy the size formula and convert to PNG (`tools/kbi_extract.py extract`); samples look right.

```c
struct Pnx {
    char magic[4];            /* "PNXF" */
    u16  stride;              /* pixels per stored row, multiple of 4 */
    u16  height;
    u16  width;               /* visible width (stride-3 … stride) */
    u16  ncolors;             /* palette entries, multiple of 4 */
    u32  alpha;               /* low u16 != 0: alpha table present (only 0 / 1 seen) */
    u16  palette[ncolors];    /* RGB565, ascending */
    u8   alpha_tab[ncolors];  /* only if alpha: 0 transparent … 255 opaque */
    idx  pixel[stride*height];/* u8 if ncolors <= 256, else u16; rows top-down */
};                            /* size = 16 + 2n (+ n) + stride*height*(1|2) */
```

Inside the MN7 `.res` archives the same header is followed by `u32 raw_len` + one zlib stream of (palette, alpha, pixels) ✅ (7,135 images in the 6 archives, converted by `tools/res_extract.py extract`).

## 7. Texts, fonts, colours

- Language store = `<lang>_480x272.kbi` in `TextResources.nfs` (35 languages in the shell, 956 texts each). Language comes from the `Language` command-line option ✅ S:0x915f4, else from the settings 🟡; `LanguageInfo` maps special languages.
- Fonts: 3 files, `NeoTechVWNG.ttf`, `NeoTechVWMedium.ttf`, `NeoTechVWLight.ttf`, sizes 9…90 px, all in the OS ROM (`\Windows\`, `firmware/rom/files/`). Fallbacks registered in code: `ZYHei_GB18030_c.ttf` (CJK), `Arial.ttf` ✅. Rasteriser = **FreeType**, statically linked (its module names are in the binary).
- The four skins of a program have the same ids; only bitmaps and a few colours differ (highlight colour 16: VW `#ff0000`, Skoda `#5bc700`).

## 8. Runtime ✅

| Topic | Finding |
|---|---|
| Command line | `-Key=Value` or `-Key="Value"` (S:0x10bcdc). Read: `ShowWindow` (0 → start hidden; watchdog starts the shell with `-ShowWindow=1`), `Language`, `Size` (window size, default = screen). The shell starts MediaPlayer / PictureViewer with ` -Language="<lang>"` (+ `-Mode=PICTURE`; MediaPlayer's first, hidden start: `-Mode=NOCONN -ShowWindow=0`, [user-shell.md](user-shell.md) §7.3), and `Apps\` programs with ` language=<lang> skin=VW|Seat|Skoda` (S:0x1c15c, 0x1cffc) |
| Skin | shell reads hidden flash `0x2F` (brand): 2 → `…_SeatSkin.nfs` (`…_SeatCupraSkin.nfs` if `0x32 == 2` and `0x33 == 1`), 3 → `…_SkodaSkin.nfs`, else `…_VwSkin.nfs` (S:0x915f4; field meanings in [brand-and-pairing.md](brand-and-pairing.md)) |
| File names | `Controls_` / `Bitmaps_` / `Colors_` / `Fonts_` + `480x272` + `.kbi`; `Workflows.kbi`, `Startup.kbi`, `Shutdown.kbi`, `Datastorage.kbi` (S:0xfdc7c, 0xfe0a8, 0xfe5a0, 0xfcab0) |
| Window | one top-level window, class = window name = program name (`VwUserShell`), style `WS_VISIBLE`, full screen (S:0x111a98) |
| Drawing | software only: back buffer = `CreateDIBSection` (16 or 32 bpp, S:0x17e2d4), shown with `BitBlt` / `StretchBlt`. **No DirectDraw, no sgalib, no GL** in these programs (import table). `Png.dll` decodes the PNG icons of `Apps\` |
| Input | `WM_LBUTTONDOWN / UP / MOUSEMOVE`, `WM_CHAR`, hot keys; private messages `WM_USER+0xc…0x14` (S:0x17db80). Code for `MouseDown x y`, `MouseUp`, `MouseMove`, `Sleep n` script lines exists (S:0x103f34; recorder or player 🟡) |
| Heartbeats | `cmd #701` creates / opens three named events (`VwUshAppThreadAlive`, `…CommandThreadAlive`, `…EventThreadAlive`); each thread `SetEvent`s its own on every loop turn (S:0x104950, 0x1023ec; loops wait ≤ 1000 ms) |
| Threads | common thread class, `CreateThread` + priority + `CeSetThreadQuantum` (S:0xde290) |
| Object lifetime | intrusive reference count at `+4` of every object |

## 9. Examples

Shell main menu (`Controls` record 1; text and bitmap ids resolved against the VW skin / `eng-GBR`):

```
Container id=1 480x272                       on shown → cmd #1028
  Bitmap id=514 0,0 480x272  bitmap 1 (Back.png)
  Row id=213 @120,0 357x21                   status bar
    ItemSet id=215  updater #3(provider #10001)   items 1 / 2 → bitmap 1201 / 1202 (icon_status-gps-on)
    ItemSet id=216  updater #3(provider #2001)    7 battery icons 1178…1184 (icon_status-bat-*)
  ItemSet id=544  updater #3(provider #1018)      tiles 214x104:
    item 0: bitmap 1134 (icon_navigation-shell) @62,11 94x60 · text 216 "Navigation" font 5
    item 1: bitmap 1146 (icon_phone-shell)                   · text 78  "Phone"
  Button id=547 @102,243 344x29  bitmaps 83 / 85 / 84 (Btn_settings-shell, _p, _d)  on tap → cmd #8 (open pop-up)
```

Trip computer (`Controls` record 2): `Label id=702 @46,53`, three text cells 21x36 with font 30, `updater #4(provider #20002, #16011)` — a car value from the VW factory formatted digit by digit; the cells of label 708 hold bitmap 1951 (`pct_layer-numbers-tripcomp.png`) as digit background; screen background = bitmap 16 `Back_tripcomputer.png`.

A workflow (`Workflows` record 0 `USH_CupraSportChronoResetWorkflow`):

```
cmd #23 if (provider #1502 {200: 15})
  then [1]: cmd #16086 {100: 2}
  then [0]: cmd #23 if (provider #16149)
              then [1]: cmd #16086 {100: 0}
              then [2]: cmd #16086 {100: 1}
              then    : cmd #23 if (provider #1502 {200: 20}) …
```

## 10. MN7 resources (`navigon.exe`, `PhoneApplication.exe`)

`.res` archive ✅ (`tools/res_extract.py`: all 6 files parse, entries contiguous, end = file size):

```c
struct ResHeader { u32 version /*1*/; u32 data_offset; u32 count; u32 file_size; };
struct ResEntry  { char name[];  /* NUL-terminated path, zero-padded to 4 */  u32 size; u32 offset; };   /* count times, from 0x10 */
```

Entries are stored as they are (only `.pnx` are zlib-packed inside, §6). `BT/PhoneApplication.res` = 478 entries: 373 `images/480x272/Sheldor/*.pnx`, 59 `ini_files/480x272/*.dat` (binary screen definitions: `BTApp_MainMenu.dat`, `Keyboard-Layout_QWERTZ.dat`…), `ini_files/fonts|images|properties|sounds/*.dat` (tables "FontID n" → face + size, "ImageID n" → path, "PropID n" → colour / value, "SoundID n" → wav), 2 `.wav`, `msvcp71.dll`. `MobileNavigator.res`: 2,243 entries. The `.dat` layouts are a different, older serialization (not decoded here ❓).

`.lng` (MN7 texts): UTF-8 with BOM, CRLF, one text per line, line number = text id 🟡. `ManualReaderNames.lng`: UTF-16, `xxx-XXX=Name` lines. `.RGBFS`: see [brand-and-pairing.md](brand-and-pairing.md) §5.3.

## 11. How to reimplement / interoperate

Render an original screen from the original resources:

1. Unpack the `.nfs` (`tools/nfs_extract.py`), read the stores (§2), parse the trees (§3) — `tools/kbi_extract.py` does both and can be imported (`read_store`, `parse_nodes`, `pnxf_image`).
2. Pick the skin (§8), load `Bitmaps` (§6), `Colors`, `Fonts` (FreeType, files from `firmware/rom/files/`), the language store.
3. Walk the control tree of a record depth-first: position = parent + (x, y); expand includes (#9); draw bitmap controls / elements (state normal unless disabled), text elements (text id 8, font 11, colour 100, box 1…4, centred by default 🟡).
4. Static content is then right; live content needs the updaters: implement the providers you care about (class id ranges in §4, shell meanings in [user-shell.md](user-shell.md)) and apply visible / enabled / text / item selection.
5. Behaviour: run `Startup.kbi` with the executor of §5.2 and a table id → function for the commands used; unknown commands can return 1.

Reskin / add a screen with the original program:

- Bitmaps, colours, fonts and texts are plain id tables: replace a record value, keep the id. A replacement bitmap must be written as PNXF (§6). The store has to be rewritten (no in-place growth): one data page per group of records, level-1 index, `len = bytes + 1`, header counters; then repack the `.nfs` ([file-formats.md](file-formats.md)). No checksum to fix in the `.kbi`. `content.md5` is not checked by any program on flash or in ROM ([boot-and-supervision.md](boot-and-supervision.md) §7.4).
- A new screen = a new `Controls` record (unused id + name entry) built from existing classes, opened with `cmd #8 {100: id}` from an action of an existing control. New behaviour must be composed from existing command / provider ids — new C++ classes are not possible without patching the factories.
- Easier for a new feature: a separate program listed under `Apps\` ([building-apps.md](../apps/building-apps.md)); it receives `language=… skin=…`.

## 12. Tools

Read-only; there is no repacker and no PNG → PNXF writer. Usage of each: `tools/README.md`.

```sh
PY=.venv/bin/python                                                   # OUT = a folder outside the repository
$PY tools/nfs_extract.py extract firmware/flash/VwUserShell/VW_UserShell.nfs OUT/shell
$PY tools/kbi_extract.py list OUT/shell/Workflows.kbi                 # key, designer's name, what the record holds
$PY tools/kbi_extract.py tree OUT/shell/Controls_480x272.kbi 1        # node tree of record 1 (main menu), as in §3
$PY tools/kbi_extract.py extract OUT/skin/Bitmaps_480x272.kbi OUT/png # PNXF → PNG (from VW_UserShell_VwSkin.nfs)
$PY tools/res_extract.py extract firmware/flash/BT/PhoneApplication.res OUT/phone      # MN7 archive, .pnx → .png
```

`tree` prints `name[key] = value` for values and `name[key] #class-id { … }` for objects, with short names for the node types of §4 (`ctl` control, `bmp` / `txt` element, `cmd`, `then` follow-up, `on` action, `upd` updater, `prov` provider, `handler`, `wf` workflow call, …; `tN` = no name yet) and the control classes of §5.1. The class-id tables, property keys and `Execute` bodies quoted in §4–§5 were read from a full decompile of the shell (`tools/decompile.py … all`).

## 13. Open questions

- `KbiTree.unk`; header bytes 0x18…0x19; whether any reader path uses the inflaters.
- Node types 13, 20, 26, 27, 29; trigger kinds other than 4, 8, 9; text element keys 6, 9, 10, 12; bitmap element key 101.
- Exact layout rules of row container #13 and item set #12; control classes 4, 8, 19, 20, 25.
- Meaning of most command / provider ids beyond the tables above (shell-specific ones → [user-shell.md](user-shell.md)). Ids 701 and 1501…1504 are special cases in front of the switch tables: 701 in the core command creator (S:0x10eeb8), 1501…1504 in the application-layer creators (S:0x1dc6dc commands, S:0x1dcfa8 providers).
- Event manager internals (queue order, re-entrancy), timer mode values.
- MN7 `.dat` screen format; `.lng` line ↔ id rule.
- PictureViewer class id range; MediaPlayer ids beyond the audio-relevant ones ([media-audio.md](media-audio.md) §12).
