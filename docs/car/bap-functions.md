# BAP functions used by the 4NSF (semantics, payloads, shared-memory fields)

What: for each of the six BAP logical devices (LSG) the unit talks to — which functions it uses, how their payloads are laid out, where the data lands in shared memory and which UI feature uses it.
Why: this is the application layer a replacement device (talk to the car) or an FSG simulator (emulated radio) has to speak.

Scope: function semantics only. Transport, opcodes, classes, sizes, segmentation, GetAll format → [bap.md](bap.md) (function table §3.4). Shared-memory container, data ids, request channel → [shared-memory.md](shared-memory.md). Raw CAN → [can-signals.md](can-signals.md).

Legend: ✅ seen in disassembly/data (address given) · 🟡 inferred, or public BAP knowledge recalled from memory (not re-checked online) · ❓ unknown. Static analysis only; no car → unit capture exists, nothing was run.
Addresses: plain = VA in `VwCommunicationExe.exe` ("VwCom"); `S:` = `VwUserShell.exe`. ARM, image base `0x10000`. The binaries have no symbols and no RTTI: every name below is descriptive.
"id n" = shared-memory data id (decimal, [shared-memory.md](shared-memory.md) §4.3). "blk+x" = offset inside the LSG's BAP block ([shared-memory.md](shared-memory.md) §2.1). "ctl+x" = offset in the control block (mapping offset `0x6060`).
"Slot" = offset in the client interface vtable `S:0x2fb880` ([can-signals.md](can-signals.md) §1); the same interface class is linked into all client processes.
Related: [bap.md](bap.md) (transport, function table) · [shared-memory.md](shared-memory.md) (blocks, data ids, requests) · [media-audio.md](../system/media-audio.md) (who uses LSG 0x31 / 0x2D and why) · [user-shell.md](../system/user-shell.md) (OPS, e-up! and charging screens).

## 1. Overview

| LSG | TX / RX CAN id | Block | What it is | Consumer | § |
|---|---|---|---|---|---|
| 0x0A | 0x67A / 0x6DA | 0x00A0 | ✅ park distance (OPS): 2 × 4 sensor distances + display handshake | Shell: `VW_PopUp_OPS` | 3 |
| 0x31 | 0x67C / 0x66C | 0x00CC | ✅ audio: active source, tuner frequency, texts, lists, source switching, volume | MediaPlayer (radio / CD screens), VolumeUI, shell | 4 |
| 0x2D | 0x6C7 / 0x6D8 | 0x1130 | ✅ audio channel claim (4 media / 8 advice / 9 phone / 10 voice) + sound settings (treble, bass, balance, fader); 🟡 "sound / audio management" of the radio | MediaPlayer (sound pop-up), shell (channel claim) | 5 |
| 0x1D | 0x67A / 0x679 | 0x11DC | ✅ read-only status for the electric-drive screens; 🟡 e-up! energy-flow display | Shell: energy monitor | 6 |
| 0x11 | 0x69D / 0x6E7 | 0x1200 | ✅ car clock: date + time, readable and settable | Shell: charging timers | 7 |
| 0x25 | 0x69D / 0x69C | 0x1294 | ✅ e-up! charging: plug / charge / climate state, 3 departure timers, profile list, night-rate list; 🟡 "BatteryControl" | Shell: `VW_Dia_Charging_Main`, `VW_PopUp_Charging*` | 8 |

Control surface (functions the unit **writes**):

| LSG / fct | Effect | Request id |
|---|---|---|
| 0x0A / 0x14 | acknowledge / close the park-distance display | 116 |
| 0x31 / 0x23, 0x2D / 0x11 | switch the radio's audio source, claim / release the audio channel for the unit's own sound | 125, 127 |
| 0x31 / 0x13, 0x14, 0x17, 0x18, 0x1B, 0x20 | radio / CD operation (mute flag, step, seek / remote control, play mode, …) | 117, 118, 120, 126, 121, 123 |
| 0x31 / 0x16, 0x22 | fetch station / track list, source list | 119, 124 |
| 0x2D / 0x17…0x1B | sound settings | 132…136 |
| 0x11 / 0x10, 0x11 | set the car's date / time | 137, 138 |
| 0x25 / 0x0D, 0x13, 0x14…0x16, 0x19, 0x1A | charging: on/off flag, timer activation, timer programming, profiles, night rates | 139, 141, 142, 143 |

## 2. Common mechanics ✅

```
car → BAP stack → indication callback → block setter → data id raised → client getter (slot)
      0x14f5c (u8) · 0x15728 (u16) · 0x15798 (bytes) · 0x166f8 (cache complete: replays every cached property)
client setter (request fields + "pending", raises id 116…144) → VwCom 0x1307c → request API 0x44918…0x44978
```

- Status field = value bytes + 1-byte `valid`. A setter writes and raises its id only when the value changed or was not valid.
- Each block ends with a "running" word, set to 1 by `0x166f8` when GetAll was answered, cleared by the block reset.
- Functions without a handler are accepted by the stack but never stored ("ignored").
- Quirk ✅: the byte-sequence handler `0x15798` checks a hard-coded length per function. For LSG 0x0A fct 0x0E it wants 4 bytes and for LSG 0x25 fct 0x0E 3 bytes, while the stack delivers 3 and 6 → these two are stored only at cache-complete (`0x166f8`), later Status updates are dropped.
- Request numbers used below ([bap.md](bap.md) §3.3): **SetGet** (0), **Get** (2), **StartResult** (6), **Abort** (7). The unit never sends a plain Set or Start.
- "Pending" flags are really "request data valid" flags: the client sets them, `0x1307c` only tests them. VwCom does **not** clear them after sending (exception: the LSG 0x25 timer request mode, cleared at `0x14860`, `0x148f8`, `0x149e0`); they are cleared by the block reset (`0x37dbc`, `0x3b51c`, …) on stop / suspend. The data id is what triggers a request, so a stale flag is harmless.
- Array functions: 3- or 5-byte header as in [bap.md](bap.md) §5 (`mode`, `start`, `elements`); header class `0x40850…0x40c08`, parser `0x40870`, builder `0x40990`.
- Strings ✅ (`0x3ee80`, `0x32954`): length prefix 1 byte (`0x00…0x7F`) or 2 bytes (`0x80 | hi`, `lo`), then that many bytes of **UTF-8**; converted to UTF-16 in the block.

Start-up requests of the application (after the stack's own Get BAP-Config + GetAll per LSG):

| When | Request | Evidence |
|---|---|---|
| LSG 0x31 cache complete | Get fct 0x22, header `80 00 00 06 00` (source list, 6 elements from 0) | `0x166f8` → `0x33f10`, `0x33f44` |
| LSG 0x31 cache complete | both list machines reset (tuner `0x357f8` event 5, CD `0x362d0` event 7); then event 0 to the one matching the active source (1…3 / 6) → Get fct 0x16 (capture: `80 01 00 06 00`, then `90 06 00 06 00`) | `0x166f8` |
| LSG 0x31 cache complete | source-change machine event 0x13 → LSG 0x2D Get fct 0x11 `00 00 01` | `0x166f8` → `0x37810(0x13)` |
| LSG 0x2D cache complete | Get fct 0x17, 0x19, 0x18, 0x1A, 0x1B, each `00 00 01` | `0x166f8` → `0x3abf8…0x3b078` → ids 132…136 |
| LSG 0x25 cache complete | none from VwCom: id 70 is raised; the shell's profile state machine then syncs date/time (LSG 0x11) and fetches fct 0x19 / 0x1A | `0x39e80`; shell class names `CVWUShProfilesState*`, `CVWUShNightRatesState*` |
| LSG 0x0A, 0x11, 0x1D | nothing | `0x166f8` |

Everything else is passive: the cached properties arrive with GetAll and later as unsolicited Status.
Cross-check ✅: `tools/samples/dock-capture.txt` (unit → radio) contains exactly these requests after the GetAlls — lines 17…29, 31, 32, 35, 36 — plus, from the UI, SetGet LSG 0x31 fct 0x13 `00` (lines 44, 47) and SetGet LSG 0x2D fct 0x11 `00 00 01 04` then `00 00 01 FF` (lines 48…51): claim and release of the audio channel (§4.5).

## 3. LSG 0x0A — park distance (OPS)

### 3.1 Functions

| Fct | Type | Unit sends | Payload → block | Setter | id | Meaning |
|---|---|---|---|---|---|---|
| 0x0E | 3 B | – | `b0 b1 b2` → blk+0x24…0x26, valid +0x27 | `0x382b0` | 45 | ✅ `b1 + b2` = number of sensors; must be 3, 4 or 8 (`S:0x28e08`) and equal the count of the loaded car picture (`S:0xcbf1c`). 🟡 front / rear count (order ❓); `b0` ❓ |
| 0x0F | u8 | – | blk+0x1C, valid +0x1D | `0x37f50` | – | FSG operation state; no reader found |
| 0x12 | 4 B | – | `b0…b3` → blk+0x08…0x0B, valid +0x0C | `0x38134` | 43 | ✅ distances, sensor group B |
| 0x13 | 4 B | – | `b0…b3` → blk+0x00…0x03, valid +0x04 | `0x37fb8` | 42 | ✅ distances, sensor group A |
| 0x14 | 2 B | SetGet 2 B | Status `b0` → blk+0x11, `b1` → blk+0x10, valid +0x12 | `0x383b0` | – | ✅ display handshake (§3.2) |
| 0x16 | u8 | – | blk+0x20, valid +0x21 | `0x384fc` | – | ✅ must be ≠ 0 for the pop-up; 🟡 "system ready" |
| 0x18 | u8 | – | bit 0 → blk+0x18, valid +0x19 | `0x37ed4` | 44 | ✅ boolean shown as a pop-up element (`S:0xcc3a8`, slot 0x1EC); meaning ❓ |
| 0x10, 0x11, 0x15, 0x17, 0x19 | – | – | ignored | – | – | ❓ |

Other block bytes: blk+0x14 / 0x15 / 0x16 = request · blk+0x29 = running (slot 0x1E8). Reset `0x37dbc` raises ids 41…45.

Distances ✅ (`S:0x897fc`): one byte per sensor, `0xFF` = initial "nothing"; the widget picks a bar picture by comparing the byte with a threshold list from the layout (`t[i] < value <= t[i+1]`). Unit 🟡 cm. Widget codes `0x3E81…0x3E84` ← fct 0x13 `b0…b3` (slots 0x54, 0x5C, 0x60, 0x58); `0x3E85…0x3E88` ← fct 0x12 `b0…b3` (slots 0x64, 0x6C, 0x70, 0x68). Which group is front / rear ❓ (the up! has rear sensors only 🟡 → fct 0x13 = rear 🟡). Setting `OpsSimulation` replaces the car values.

### 3.2 Display handshake (fct 0x14) ✅

Status `b1 == 1` = the car asks for the display. The unit answers SetGet `{state, 0xFF}` (`0x37e8c`: blk+0x15 = state, blk+0x14 = `0xFF`, blk+0x16 = 1, id 116 → `BAP(0x0A, 0x14, SetGet, [blk+0x15, blk+0x14])`).
State machine `0x37178(event)`: state `ctl+0x68`, flags `ctl+0x6A`, timer slot `ctl+0x6C`. Runs in VwCom (events 0, 1, 3, 4) and in the shell (event 2).

| Event | Source | | State | Meaning / entry action |
|---|---|---|---|---|
| 0 / 1 | Status fct 0x14, `b1 == 1` / `b1 != 1` | | 0 | idle: cancel timer, clear flags, raise id 42 |
| 2 | user closes the pop-up (slot 0x1E0, `S:0x297a4`) | | 1 | request seen: SetGet `01 FF` |
| 3 | Status fct 0x16 | | 2 | active and fct 0x16 ≠ 0: slot 0x1E4 returns true → `S:0x28e08` opens `VW_PopUp_OPS`; after event 2: SetGet `00 FF` |
| 4 | timer slot expired, 2000 ms (`0x18e44`) | | 3 | active, fct 0x16 = 0 / unknown: SetGet `01 FF` again, timer 2000 ms |
| | | | 4 | car withdrew: SetGet `00 FF` |

Transitions: 0 or 4 —ev 0→ 1 —ev 0→ 2 / 3; 2 / 3 —ev 3→ 2 / 3; 2 / 3 —ev 1→ 4; other events in 0 / 1 / 4 → 0.

## 4. LSG 0x31 — audio (radio)

Identity: ✅ tuner frequency formula, source list, texts, volume; consumers = MediaPlayer screens `MMP_Dia_Radio`, `MMP_Dia_Cd`, `MMP_PopUp_BrowseRadioPreset`, `MMP_PopUp_TA` and `VolumeUI.exe`. 🟡 FSG = the radio (RCD 215); public name of LSG 0x31 = "Audio".

### 4.1 Functions

| Fct | Type | Unit sends | Payload → block | Setter | id | Meaning |
|---|---|---|---|---|---|---|
| 0x0E | u8 | – | blk+0x00, valid +0x01 | `0x3c360` | 80 | FSG setup ❓ (slot 0x1F4) |
| 0x0F | u8 | – | blk+0x04, valid +0x05 | `0x3c3d8` | 49 | FSG operation state (slot 0x1F8) |
| 0x10 | 4 B | – | `b0` → +0x08, `b3` → +0x09, `b1` bit 0 → +0x0A, `b2` → +0x0B, valid +0x0C | `0x3cf54` | 89 (source), 94 (`b3`) | ✅ **active source** (§4.2) |
| 0x11 | string ≤ 48 B | – | UTF-8 kept as bytes, blk+0x10, valid +0x40 | `0x3c50c` | 84 | 🟡 short info text (slot 0x20C) |
| 0x12 | 2 B | – | `i = b1 >> 4` (0…4): `b0` → +0x43+4i, `b1` bit 0 → +0x45+4i, valid +0x46+4i; current `i` → +0x57, valid +0x58 | `0x3b664` | 86; + 153 if `b1` bit 0 | ✅ **volume**: `b0` = level of volume type `i`; bit 0 = "show it" → VolumeUI (subscribes 86, 153). Slots 0x210…0x220. Types ✅ (texts shown by VolumeUI, [media-audio.md](../system/media-audio.md) §6.1): 0 Media · 1 Traffic program · 2 Phone · 3 Navigation · 4 Voice operation; level 0…30 🟡 |
| 0x13 | u8 | SetGet u8 | bits 0, 1, 2 → +0x5B…0x5D, valid +0x5E; request bits +0x5F…0x61, pending +0x62 | `0x3b7e8` | 87 / 117 | ✅ **three mute flags** ("radio muted" = any flag set); the unit sets / clears only the first one (bit 0 🟡) and keeps the other two, which belong to the radio 🟡 ([media-audio.md](../system/media-audio.md) §6.2); slots 0x224 (get), 0x228 / 0x238 (set), 0x230 (local write) |
| 0x14 | u8 | SetGet u8 | +0x63, valid +0x64; request +0x67, pending +0x68 | `0x3b9e0` | 88 / 118 | ❓ one-byte selector; set only allowed when the source is 1, 2, 3, 6 or 0x13 (`0x3b944`, slot 0x240). Status 1 / other feeds the seek state machine (`0x34cd0`); 🟡 scan / seek state (MediaPlayer events on id 88, [media-audio.md](../system/media-audio.md) §8.2) |
| 0x15 | ≤ 150 B | – | 3 strings → +0x6C, +0xAE, +0xF0 (33 UTF-16 each); then `u8` → +0x134, `u16` LE → +0x136; valid +0x138 | `0x3c63c` | 89 | 🟡 **now playing**: three text lines (station / title / artist …), flag bits 0…3 (slot 0x250), number (slot 0x258). Slots 0x244…0x24C = texts |
| 0x16 | Array | Get 5 B | §4.3 | `0x3d540` Status, `0x3db94` Changed | 90…92 (list state machines) / 119 | ✅ **list of the current source** (stations / tracks). A Status that leaves the window incomplete raises request id 119 itself → next chunk is fetched without the UI |
| 0x17 | Method | StartResult 3 B / Abort | request +0x87C (`0xFFFF` = abort), +0x87E, +0x87F, pending +0x882; Processing u8 → +0x880; Result u8 → +0x881 | `0x3dfe4`, `0x3df58` | 93 / 120 | ✅ **radio remote control** (next / previous, fast forward / backward while held, manual step, scan, select list entry): command 0…7 from the UI (slot 0x25C → `0x3de6c` → state machine `0x35ab4`, state `ctl` via `0x2f3c4`), Abort on release / timers ≈ 1.0…1.2 s. Command codes as MediaPlayer uses them: [media-audio.md](../system/media-audio.md) §8.3 (names 🟡) |
| 0x18 | u8 | SetGet u8 | bits 0…3 → +0x886…0x889, valid +0x88A; request bits +0x88E…0x891, pending +0x892 | `0x3bbf8` | 95 / 126 | 🟡 four play-mode flags (UI texts: Scan, Mix, Repeat); slots 0x26C (get), 0x268 (set) |
| 0x1A | ≤ 32 B | – | `b0` → +0x896; string → +0x898 (11 UTF-16); valid +0x8AE | `0x3cae4` | 52 | 🟡 announcement: flag (bit 0, slot 0x27C) + short text (slot 0x280) — `MMP_PopUp_TA` |
| 0x1B | Method | StartResult, no data | Result u8 → +0x8B0, valid +0x8B1 | `0x3bf04` | 53 / 121 | 🟡 cancel announcement ("Deactivate TP"); slot 0x284 |
| 0x1C | u8 | – | values > 0x21 stored as 0; +0x8B4, valid +0x8B5 | `0x3bd4c` | 96 | ❓ 0…33 (slot 0x28C); 🟡 radio info / error code: MediaPlayer opens pop-ups such as "Please check CD.", "No CD available.", "The radio is locked…" by value ([media-audio.md](../system/media-audio.md) §8.2) |
| 0x1D | u8 | – | +0x8B8, valid +0x8B9 | `0x3bf40` | 97 | ✅ value 5 switches the CD list to "window" handling (`0x33580`); 🟡 media type |
| 0x20 | Method | StartResult 4 B | request +0x8BC…0x8BF, pending +0x8C1; Result u8 → +0x8C0, valid +0x8C2 | `0x3bfe8` | 99 / 123 | ❓ 4-byte command (slot 0x294); 🟡 select / store list entry |
| 0x22 | Array | Get 5 B | §4.4 | `0x3c024` Status, `0x3c1e8` Changed | 85 (complete) / 124 (fetch more) | ✅ **source list** |
| 0x23 | Method | StartResult u16 | request handle +0xED0, pending +0xED3; Result u8 → +0xED2, valid +0xED4 | `0x3e0ac` | 51 / 125 | ✅ **activate source** by handle; Result 0 = accepted (event 2), else failed (event 3) of §4.5; slot 0x2B0 reads the result |
| 0x25 | 3 B | – | `u16` LE → +0xED8, `b2` → +0xEDA; valid +0xEDB, cleared when `u16 == 0 && b2 != 0` | `0x3bdc4` | 100 | 🟡 `u16` = **RDS PI code** of the tuned station, byte = flag (slot 0x2B4): MediaPlayer uses it to pick the station logo `PI_<ECC>_<PI>.png` ([media-audio.md](../system/media-audio.md) §8.5) |
| 0x26 | string ≤ 194 B | – | → +0xEDC (193 UTF-16), valid +0x105E | `0x3cd30` | 84 | 🟡 long info text (radio text / "DAB information"); slot 0x2BC |
| 0x19, 0x1F, 0x21, 0x24 | – | – | ignored | – | – | ❓ |

Running flag: blk+0x1060 (slot 0x1F0). Reset: `0x3b51c`.

### 4.2 Active source (fct 0x10) ✅

| Byte | Field | Evidence |
|---|---|---|
| `b0` | source type | enum below |
| `b1` bit 0 | flag (slot 0x204) | ❓ |
| `b2` | → blk+0x0B (slot 0x208) | ❓ |
| `b3` | tuner position (slot 0x200) | source 1: frequency = `875 + b3` × 100 kHz (FM 87.5 MHz + 0.1 × b3) · source 2: `(59 + b3) × 9` kHz (MW 531 kHz + 9 × b3) · source 3: number 1…150 — `0x32c00`, `0x32c68`, slot 0x264 |

| Source type | Meaning | Evidence |
|---|---|---|
| 1 | FM | ✅ frequency formula |
| 2 | AM (MW, 9 kHz raster) | ✅ frequency formula |
| 3 | DAB (`VWST_DAB`) | ✅ treated as tuner (`0x13698`, `0x3db94`); name ✅ from MediaPlayer's workflow log texts ([media-audio.md](../system/media-audio.md) §4) |
| 6 | CD (`VWST_CD`) | ✅ own list state machine (`0x362d0`); MediaPlayer opens `MMP_Dia_Cd` |
| 0x0E | AUX-in of the radio (`VWST_AUX_IN`) | ✅ name; MediaPlayer opens `MMP_Dia_Aux` |
| 0x13 | AM, traffic information (`VWST_AM_TI`) | ✅ name; handled as tuner |
| 0x0C | ❓ further radio-internal source | ✅ same class as 1, 2, 3, 6 (`0x3e9b4`) |
| 0x0F | **the unit itself** (`VWST_PORTABLE_DEVICE`; audio from the dock line-in) | ✅ `0x3e9fc`; built-in list entry `{0x0F, 0, 0x0F, fixed name}` (`0x34b10`) |

### 4.3 List of the current source (fct 0x16) ✅

Request (Get, 5 bytes) = array header only, always 16-bit form: `0x80 | shift << 4 | direction << 5`, `start` u16 LE, `elements` u16 LE (≤ 6). Four request slots, chosen by `0x1307c` from the source and the list state (`ctl` states via `0x2f218` tuner, `0x2f268` CD):

| Source | Request fields (start, shift, forward, elements) | Used when |
|---|---|---|
| 1, 2, 3 | blk+0x86C, +0x86E, +0x86F, +0x870 | tuner list state 1 |
| 1, 2, 3 | blk+0x874, +0x876, +0x877, +0x878 | tuner list state 4 (after a Changed) |
| 6 | blk+0x4F0, +0x4F2, +0x4F3, +0x4F4 | CD list state 1, 2, 3 |
| 6 | blk+0x4F8, +0x4FA, +0x4FB, +0x4FC | CD list state 5 (after a Changed) |

Capture examples ([car-link.md](car-link.md) §6): `80 01 00 06 00` (start 1, 6 elements), `90 06 00 06 00` (shift, start 6).

Status payload: 2 bytes (not read by the unit; 🟡 transaction / total count), array header, then `elements` records. Longer than 317 bytes → dropped.

```c
/* wire record of fct 0x16 (0x418b8) */
[pos]        /* only if header bit 6 (transmit position): u8, or u16 LE with bit 7. Otherwise pos = start (+1 if shift) + index */
u8  type;    /* high nibble → entry+2, low nibble → entry+3 (🟡 kind: station / preset / track / folder) */
string name; /* → entry+4, 33 UTF-16 */

/* block entry, 0x48 bytes */
struct ListEntry { u16 pos; u8 typeHi, typeLo; wchar name[33]; u8 first; };
```

- Tuner (source 1…3): 12 entries at blk+0x500, valid flags blk+0x860[12] (`0x333d8`; reader `0x34984`, slot 0x278).
- CD (source 6): ring of 12 entries at blk+0x13C (head +0x49C, tail +0x4A0, empty +0x4A4), one extra entry at +0x4A6, valid +0x4EE (`0x33580`, ring class `0x41bb8…0x41ecc`; reader `0x34dbc`, slots 0x270 / 0x274; name of the extra entry: slot 0x2B8).
- Changed (op 3): header only. Start / direction are copied into the second request slot and the list state machine is told to re-fetch (`0x3db94`).
- Supervision of this function is longer than for all others: 200 ticks × 3 ([bap.md](bap.md) §2).

### 4.4 Source list (fct 0x22) and source switching ✅

Request (Get, 5 bytes): header only, 16-bit form, from blk+0xEC4 (start), +0xEC6 (shift), +0xEC7 (forward), +0xEC8 (elements); pending +0xECC (`0x33f44`). Sent at start (`80 00 00 06 00`) and on demand (slot 0x29C → `0x3c2d8`); a full answer of 6 elements triggers the next chunk (`0x34610`).

Status payload: array header, then records (`0x41fdc`):

```c
u8 | u16 handle;   /* u16 LE when header bit 7, else u8 */
u8  type;          /* source type, §4.2; also the index of the entry in the block (0…31) */
u8  b;             /* ❓ */
u8  c;             /* ❓ */
string name;       /* 21 UTF-16 */

/* block: 32 entries × 0x30 at blk+0x8C4, indexed by type */
struct SourceEntry { u16 handle; u8 type, b, c, pad; wchar name[21]; };
```

- An entry is valid when `handle != 0 && entry.type == index` (`0x34c08`, slots 0x2A0 / 0x2A4).
- Changed with more than 32 elements clears the list (`0x33eb4`); otherwise the unit re-requests from the changed start (`0x3c1e8`).
- **Activate a source** ✅: client calls slot 0x2AC with a source type → `0x3cc64` looks up its handle in the list → blk+0xED0 → id 125 → `BAP(0x31, 0x23, StartResult, u16 handle)`. Slot 0x2A8 (`0x3c330`) takes a raw handle.

### 4.5 Source-change state machine (control block) ✅ mechanics, 🟡 meaning

`0x37938(source, channel, flag)` (slot 0x2FC) → `0x37810(event)` → `0x36924`; state via `0x2f2b8`, flags via `0x2f2f8`, queued events via `0x2f750 / 0x2f77c`.

| State | Entry action |
|---|---|
| 1 | activate the wanted source: fct 0x23 StartResult (`0x3cc64`) |
| 4 | timer 500 ms |
| 5, 6, 10 | LSG 0x2D fct 0x11 SetGet with the wanted channel value (`0x3ab44`) |
| 11 | LSG 0x2D fct 0x11 SetGet `0xFF` (release) |
| 12 | LSG 0x2D fct 0x11 Get (`0x3ab7c`) |
| 8, 9 | raise id 82 (done) |
| others | raise id 83 (failed / idle), cancel timer |

Events: 0x13 = LSG 0x31 became available · 0x15 / 0x16 = timer slots expired (`0x18c04`) · 2 / 3 = fct 0x23 Result ok / failed · 4 / 5 = Status fct 0x10 shows / does not show the requested source (`0x3cf54`) · 0x11 / 0x12 = Status LSG 0x2D fct 0x11 equals / differs from the requested value (`0x3b310`) · 6…0x10 = decisions of `0x37938` from (current source, current channel, wanted source, wanted channel).
Who drives it ✅: only the shell calls slot 0x2FC (`S:0x94458`), from its AppCom "sound manager" (msg 29 winner) — MediaPlayer, navigon and the phone app never do. Mapping winner → (source, channel) ✅ ([media-audio.md](../system/media-audio.md) §5.1): media from SD / BT → source 0x0F + channel 4; phone call → 0x0F + channel 9; voice dialog → 0x0F + channel 10; navigation advice → channel 8 only (the radio keeps its own source); radio sources (FM, AM, DAB, CD, AUX) → that source, no channel; nobody → channel `0xFF` (release), after which the radio returns to its own source 🟡.

## 5. LSG 0x2D — audio channel and sound settings

Identity: ✅ behaviour below; 🟡 the radio's sound / audio-management function block. Name of LSG 0x2D ❓.

| Fct | Type | Unit sends | Payload → block | Setter | id | Meaning |
|---|---|---|---|---|---|---|
| 0x03 | 8 B | – | first 6 bytes → blk+0x00, valid +0x06 | `0x3b1a0` | 104 | ✅ function list, read by the UI: "is function n present" (slot 0x2C8, `S:0xccd44`) |
| 0x0D | u8 | (SetGet allowed, never sent) | blk+0x08, valid +0x09 | `0x3aa94` | 105 | ❓ |
| 0x0E | 2 B | – | blk+0x0C, +0x0D, valid +0x0E | `0x3a974` | 106 | FSG setup ❓ |
| 0x0F | u8 | – | blk+0x10, valid +0x11 | `0x3aa1c` | 74 | ✅ operation state; the shell accepts 0, 1, 2 (slot 0x2C0, `S:0x1dc2c`) |
| 0x11 | Array, 1 element | Get `00 00 01` · SetGet `00 00 01 vv` | Status: header (start 0, 1 element, no shift) + `vv` → blk+0x14, valid +0x15. Request: value +0x16, mode +0x17 (1 Get, 2 SetGet), pending +0x18 | `0x3b310` | – / 127 | ✅ **audio channel** owned / requested by the unit (slot 0x2D0). Values seen: `04` and `FF` (capture); compared in code: 4, 8, 9, 10, `FF` = none (`0x37938`). ✅ from the shell's request table ([media-audio.md](../system/media-audio.md) §5.1): **4 = entertainment from the unit, 8 = navigation announcement, 9 = phone, 10 = voice dialog**, `FF` = none |
| 0x12 | Array | Get / SetGet ≤ 5 B (id 128) | request: start +0x5C, shift +0x5E, forward +0x5F, elements +0x60, data `[+0x5C, +0x64]`, pending +0x66; Status ignored | `0x32780` | 128 | ❓ no client setter for it among the interface slots |
| 0x13 | u8 | – | blk+0x68, valid +0x69 | `0x3ab80` | 108 | ❓ |
| 0x17 | Array, 1 element | Get · SetGet | value → blk+0x80, valid +0x81; request +0x82 / +0x83 / +0x84 | `0x3ac58` | 111 / 132 | ✅ sound setting, UI index 2 = **balance** |
| 0x18 | same | Get · SetGet | blk+0x88, valid +0x89; request +0x8A…0x8C | `0x3ad78` | 112 / 133 | ✅ sound setting (not used by the shell) = **fader** 🟡 (MediaPlayer's 2-D pad, Y axis) |
| 0x19 | same | Get · SetGet | blk+0x90, valid +0x91; request +0x92…0x94 | `0x3ae98` | 113 / 134 | ✅ sound setting, UI index 1 = **bass** |
| 0x1A | same | Get · SetGet | blk+0x98, valid +0x99; request +0x9A…0x9C | `0x3afb8` | 114 / 135 | ✅ sound setting; fetched at start, used by no program ❓ |
| 0x1B | same | Get · SetGet | blk+0xA0, valid +0xA1; request +0xA2…0xA4 | `0x3b0d8` | 115 / 136 | ✅ sound setting, UI index 0 = **treble** |
| 0x15, 0x16, 0x1E, 0x26 | – | – | ignored | – | – | ❓ |

- One-element arrays: request = 3-byte header `00 00 01` (8-bit form, start 0, 1 element), SetGet appends the value byte; Status = header + value (`0x31d8c`, `0x3ac58`).
- Sound values ✅ (`S:0xc9bbc`, `S:0xcca34`): **signed byte, −108…+108**; the shell steps by a configured amount and draws a scale of marks 12 apart (+108, +96, …). UI index 0 / 1 / 2 ↔ fct 0x1B / 0x19 / 0x17 (slots 0x2E0 / 0x2D8 / 0x2F0 read, 0x2DC / 0x2D4 / 0x2EC write). Names ✅ from MediaPlayer's sound pop-up `MMP_PopUp_SettingsSound` (labels next to the sliders, [media-audio.md](../system/media-audio.md) §6.4): fct 0x1B treble, 0x19 bass, 0x17 balance; fct 0x18 fader 🟡 (shown only when the function list says it exists, "4 boxes" variant).
- Running flag blk+0xA8 (slot 0x2C4).

## 6. LSG 0x1D — electric-drive status (read-only)

Identity: ✅ consumers are the shell's electric / hybrid screens; the unit never sends a request (no TX opcode besides Get). 🟡 e-up! energy-flow / power display. Restarted after BAP errors like 0x11 and 0x25 ([bap.md](bap.md) §4.5).

| Fct | Type | Payload → block | Setter | id | Meaning |
|---|---|---|---|---|---|
| 0x03 | 8 B | blk+0x00, valid +0x08 | `0x38a9c` | – | function list |
| 0x0D | u8 | bit 0 → blk+0x10, bit 4 → blk+0x11, valid +0x12 | `0x38bc8` | 54 | ✅ bit 4 must be set for the level display (`S:0x89324`, `S:0xcaa64`; slot 0x3E4); bit 0 ❓ |
| 0x0F | u8 | blk+0x0C, valid +0x0D | `0x38b50` | 55 | ✅ operation state; display blanked for 1 and 0x0F (slot 0x3E8) |
| 0x10 | 2 B | `b0` → blk+0x14, `b1` → blk+0x15, valid +0x16 | `0x38c70` | 56, 57 | ✅ `b0` = drive state selecting the energy-flow animation (`S:0x3b94c`, slot 0x3EC): 2, 0x11 → one picture set; 6, 7, 8 → another (split by a raw-CAN flag); 0x0B → third. Names ❓. `b1` (slots 0x3F0 / 0x3F4) ❓ |
| 0x11 | u8 | blk+0x18, valid +0x19 | `0x38d78` | 58 | ✅ level shown as a bar by threshold list, only with ignition on (`S:0x89324`, slot 0x3F8); 🟡 power / recuperation level |
| 0x13 | u16 | blk+0x1C, valid +0x1E | `0x38df0` | 59 | ✅ value × 0.1, fed into an averaging / accumulating filter (`S:0x34364`, slot 0x3FC); 🟡 consumption or energy |
| 0x0E, 0x12, 0x14 | – | ignored | – | – | ❓ |

Running flag blk+0x20. Reset `0x389e8` raises ids 54…59.

## 7. LSG 0x11 — clock (date / time) ✅

| Fct | Type | Unit sends | Payload → block | Setter | id | Meaning |
|---|---|---|---|---|---|---|
| 0x03 | 8 B | – | blk+0x00, valid +0x08 | `0x38614` | 60 | function list |
| 0x0F | u8 | – | blk+0x18, valid +0x19 | `0x386e4` | 61 | operation state (slot 0x410) |
| 0x10 | 3 B | Get · SetGet 3 B | `b0 b1 b2` → blk+0x1C…0x1E, valid +0x1F. Request: mode +0x20 (1 Get, 2 SetGet), bytes +0x21…0x23, pending +0x24 | `0x387c4` | 62 / 137 | ✅ **date**: `b0` = year − 2000, `b1` = month, `b2` = day (getter `0x31880` adds 2000; setter `0x3875c` stores year mod 100) |
| 0x11 | 3 B | Get · SetGet 3 B | → blk+0x28…0x2A, valid +0x2B. Request: +0x2C, +0x2D…0x2F, +0x30 | `0x38900` | 63 / 138 | ✅ **time**: hour, minute, second |
| 0x0E, 0x14, 0x18, 0x1A…0x1E | – | – | ignored | – | – | ❓ |

- Read: `S:0x48cfc` (slots 0x400 time, 0x408 date) builds a timestamp; if the date is implausible it falls back to the unit's own clock.
- Write: `S:0xa0c10` sends the unit's local time (`GetLocalTime`: hour, minute, second → slot 0x404), `S:0xa09e8` the date (slot 0x40C). These are the `CVWUShProfilesStateSyncTime` / `…SyncDate` steps: the unit pushes its (GPS-disciplined) clock to the car before programming timers. The shell also pushes time and date whenever the navigation reports a valid GPS time (AppCom msg 22, state 2 — [user-shell.md](../system/user-shell.md) §3.3).
- Running flag blk+0x90.

## 8. LSG 0x25 — charging (e-up!)

Identity: ✅ data model below matches the shell's charging screens (texts: "Departure time", "Charging station", "Upper / Lower charge limit", "Night current", "Start / Finish charging at", "Internal aircon temperature"). 🟡 public name "BatteryControl"; field names in *italics* are recalled from public e-Golf write-ups and only checked for size, not meaning.

### 8.1 Properties

| Fct | Type | Unit sends | Payload → block | Setter | id | Meaning |
|---|---|---|---|---|---|---|
| 0x0D | u8 | SetGet u8 | Status bit 0 → blk+0x10, valid +0x11. Request byte blk+0x0C (bit 0), pending +0x0D | `0x38f50` | 64 / 139 | ✅ one on/off flag, settable (slots 0x41C read, 0x418 write, `S:0x48908`); 🟡 immediate charging / climatisation switch |
| 0x0E | 6 B | – | `b0 & 3` → +0x14; `b1` bits 0…3 → +0x15…0x18; `b2` bit 0 → +0x19, bit 1 → +0x1A; valid +0x1B | `0x38fd4` | 65 | FSG setup: capability flags (slot 0x420) ❓ |
| 0x0F | u8 | – | blk+0x1C, valid +0x1D | `0x391cc` | 66 | operation state (slot 0x414) |
| 0x10 | 2 B | (SetGet allowed, never sent) | four nibbles: `b0` hi → +0x20, lo → +0x21, `b1` hi → +0x22, lo → +0x23; valid +0x24 | `0x39244` | 67 | 🟡 *plug state* (lock setup, lock state, supply state, plug colour); slot 0x424, `S:0x4bae4` |
| 0x11 | ≤ 9 B | – | `b0` hi → +0x28, lo → +0x29; `b1` → +0x2A; u16 LE `b2,b3` → +0x2C; u16 LE `b4,b5` → +0x2E; `b6 & 0x0F` → +0x30; `b7` → +0x31; `b8` hi → +0x32, lo → +0x33; valid +0x34 | `0x398d8` | 73 | 🟡 *charge state*: mode / state nibbles, level %, remaining time, range, …; slot 0x454 reads the `b0` low nibble |
| 0x12 | ≤ 7 B | – | `b0` bit 0 → +0x38, bit 1 → +0x39; `b1` → +0x3A; `b2` → +0x3B; (u16 LE `b3,b4`) − 120 → +0x3C; `b5` nibbles → +0x48 / +0x49; `b6` bits 0…3 → +0x40…0x43; valid +0x4A | `0x39b20` | 72 | 🟡 *climate state*: on / off flags, temperatures (offset 120 ✅, unit ❓), reason bits; slots 0x44C / 0x450 |
| 0x13 | 2 B | Get · SetGet 2 B | per timer i = 0…2: state = bit i of `b0` + 2 × bit i of `b1` → blk+0x60+0x1C·i, valid +0x64+0x1C·i | `0x39364` | 68 / 141 | ✅ **timer states** 0…3. SetGet `{mask, 00}`: mask bit i set when timer i's wanted state is 1 or 3 (`0x302d0`) → 🟡 `b0` = programmed / enabled, `b1` = currently running |
| 0x14, 0x15, 0x16 | ≤ 8 B | Get · SetGet 8 B | timer 0, 1, 2 — §8.2 | `0x39454` | 68 / 141 | ✅ **departure timers** |
| 0x18 | Method | (never started) | Result u8 → blk+0xC5 | `0x39858` | – | ❓ |
| 0x19 | Array | Get · SetGet | §8.3 | `0x3a09c`, `0x3a2c0` | 70 / 142 | ✅ **profile list** ("charging stations") |
| 0x1A | Array | Get · SetGet | §8.3 | `0x3a4a4`, `0x3a6b0` | 71 / 143 | ✅ **night-rate list** |
| 0x17 | ≤ 8 B | – | ignored | – | – | 🟡 a fourth timer the unit does not use |

Running flag blk+0x4DC8.

### 8.2 Departure timer (fct 0x14 + i) ✅

```c
/* wire, 8 bytes (0x39454 parse, 0x3018c / 0x302d0 build) */
u8 year;      /* year - 2000;  FF FF FF in year/month/day when the timer is weekly */
u8 month;
u8 day;
u8 hour;
u8 minute;
u8 flags;     /* bit 0 = single date (year/month/day valid) · bits 1..7 = seven weekday flags */
u8 profile;   /* id of the profile (fct 0x19 entry) used by this timer */
u8 nibbles;   /* high / low nibble → two small values (❓; UI keys suggest charge / climate schedule) */

/* block: blk+0x4C + 0x1C*i */
struct Timer { u8 day, month, year, _; u8 hour, minute, _[2]; u8 weekday[7], _; u8 single; u8 _[3];
               u8 state /* fct 0x13 */, profile, nibHi, nibLo; u8 stateValid, dataValid; };
```

- Field order hour / minute and Monday-first weekday order 🟡: taken from the order of the shell's key strings `timeHour, timeMinute, timeDay, timeMonth, timeYear, weekDayMonday … weekDaySunday, profileId` next to the getter's argument order (`0x30050`).
- Request staging area blk+0xA0…0xB7 (same layout), timer index +0xBC, pending +0xBD, mode +0xBE (1 Get, 2 SetGet; cleared by VwCom after sending), +0xBF = 1 → send fct 0x13 instead of the timer record. Client setters: slots 0x42C (timer data, `0x397a0`), 0x430 (state only, `0x3981c`); reader slot 0x428.

### 8.3 Profile and night-rate arrays ✅

Both use the same envelope, different from LSG 0x31:

```
request (Get / SetGet) : [asg << 4 | transaction] [array header] [records…]        (0x30820, 0x30f08)
Status                 : [asg << 4 | transaction] [1 byte, not read] [array header] [records…]
Changed                : [array header] [records if header bit 6]
```

- Status is only accepted when the `asg` nibble is 0, 1 or ≥ 8 (`0x3a09c`).
- ≤ 11 records per message (night-rate Status: only the first 4 are kept, `0x315cc`). Status goes into a two-slot buffer (blk+0x818 / blk+0x29F4, so two answers can queue), Changed into its own buffer (blk+0x16B0 / blk+0x41D4); flags "status arrived / changed arrived / fetch needed" at blk+0x1DFD…0x1DFF and blk+0x4DC5…0x4DC7 (slot 0x434 reads all six, optionally clearing them).
- The header's record address (low nibble of the mode byte) selects a subset of fields.

Profile record, fct 0x19 (`0x3f1a4` parse, `0x3fc4c` build; block entry 0xA8 bytes; ≤ 333 B received, ≤ 86 B sent):

| Record address | Wire bytes after the optional position |
|---|---|
| 0 (full, ≥ 20 B) | `f0` 8 flag bits · `f1` bits 0, 1 · `u8` · `u8` · `u16` LE · `u8` · `u16` LE · `u16` LE · nibbles (lo, hi) · `u8` · `u8 u8 u8 u8` · `u16` LE · string name (63 UTF-16) |
| 1 (≥ 6 B) | `f0` · `u8` (3rd field of the full record) · `u8` (6th) · `u16` LE (last) · string name |
| 2 (≥ 3 B) | `f0` · `u8` · `u8` (same two as in 1) |
| 3, 4, 5, … | further subsets (`0x3f1a4` continues; not transcribed) ❓ |

🟡 Meaning, from the shell's key strings `profileId / handle, charging, airCondition, chargeSchedule, climateSchedule, targetChargeLevel, maxCurrentValue, nightRate, nightRateHandle` and the screens (name, upper / lower charge limit, charging current, night current on/off, air-conditioning temperature): flags = charge / climatise / night-rate switches; bytes = limits in %, current, temperature; `u16` = handles. Exact assignment ❓.

Night-rate record, fct 0x1A (`0x40da8` parse, `0x41260` build; block entry 0x114 bytes; ≤ 531 B received, ≤ 137 B sent):

| Record address | Wire bytes after the optional position |
|---|---|
| 1 (≥ 5 B) | flags: bits 0…6 = seven booleans · `u8 u8` · `u8 u8` |
| 2 (≥ 4 B) | `u8 u8` · `u8 u8` |
| 0, 0x0E, … | variants with two strings (64 UTF-16 each) ❓ |

🟡 Meaning: weekday mask + start hour / minute + end hour / minute ("Start charging at" / "Finish charging at"); the UI copy keeps only position and the four time bytes (`0x3ec78`).

Client API: profiles — slot 0x43C take a queued Status (`0x314d4`), 0x440 read the last Changed (`0x30678`), 0x444 SetGet with records (`0x39ed8`), 0x448 Get (`0x39f00`); night rates — 0x458 take a queued Status (`0x30b8c`), 0x464 read the last Changed (`0x30d50`), 0x45C SetGet (`0x39f6c`), 0x468 Get (`0x39f94`), 0x438 mark "fetch needed" (id 71). The shell drives them with two state machines whose state names are in the binary: `Idle → SyncDate → SyncTime → GetChangedItems → ProcessChangedArray / ProcessStatusArray → SendProfile / Set → SetWaitForResponse → ProcessGetResponse`, error state `Error`, events `Add, Set, StatusAll, StatusArray, ChangedArray, StatusDate, StatusTime, Continue, Done, TimeOut`.

## 9. What an FSG simulator must answer

Precondition for every LSG: BAP-Config Status, then GetAll Status built as in [bap.md](bap.md) §4.2 with **all** cached properties of that LSG (also the ones marked ignored here; zeros are fine).

| Feature | Minimum |
|---|---|
| OPS pop-up | LSG 0x0A: fct 0x0E `00 00 04` (sum must match the car picture: 3, 4 or 8), fct 0x16 = 1, fct 0x13 / 0x12 distances; then Status fct 0x14 `00 01`. Answer the unit's SetGet `01 FF` with Status `01 01`. Close with Status `00 00` |
| Radio screen (MediaPlayer `-Mode=RADIO`) | LSG 0x31: fct 0x0F, fct 0x10 `01 00 00 nn` (FM, 87.5 + 0.1 × nn MHz), fct 0x15 with three strings + `00` + `00 00`; answer Get fct 0x22 with a source list containing at least the active type and type 0x0F with non-zero handles; answer Get fct 0x16 with `00 00` + the echoed header + ≤ 6 `{type, string}` records. 🟡 screen logic of MediaPlayer itself not traced |
| Volume pop-up | LSG 0x31 Status fct 0x12 `vv t1` (`t` = 0…4, bit 0 set) → ids 86 + 153 → VolumeUI. Only on a unit branded as XL1: on every other car `VolumeUI.exe` quits at start ([media-audio.md](../system/media-audio.md) §6.3) |
| Unit's own audio (MP3 / BT / prompts) | Answer StartResult fct 0x23 with Result `00`, then Status fct 0x10 with `b0 = 0F`; answer LSG 0x2D SetGet fct 0x11 `00 00 01 vv` with Status `00 00 01 vv`. Otherwise the source-change machine ends in "failed" (id 83) |
| Sound settings | LSG 0x2D: answer Get / SetGet of fct 0x17…0x1B with Status `00 00 01 vv`; fct 0x03 with the function bits set; fct 0x0F ∈ {0, 1, 2} |
| Energy-flow / level display | LSG 0x1D: fct 0x0D with bit 4, fct 0x0F ∉ {1, 0x0F}, fct 0x10, fct 0x11, fct 0x13 — plus the raw CAN messages of an electric car ([can-signals.md](can-signals.md)) |
| Charging screens | LSG 0x11: echo SetGet of fct 0x10 / 0x11 as Status. LSG 0x25: properties 0x0D…0x13, timers 0x14…0x16 (8 bytes each), and array answers for fct 0x19 / 0x1A in the envelope of §8.3 (an empty list = header with 0 elements) |

## 10. How to reimplement / interoperate

- Replacement device: §1's control-surface table is the complete set of writes the stock unit performs; payloads are in §3…§8. Reading is passive after GetAll. Only fct 0x22 (LSG 0x31), the five sound arrays (LSG 0x2D) and the two charging arrays must be requested explicitly.
- To take the audio path like the stock unit: get the source list, StartResult fct 0x23 with the handle of type 0x0F, SetGet LSG 0x2D fct 0x11 = 4; release with `FF` (§4.5). Whether the RCD 215 also needs the dock's "audio enable" pin ❓: no shipped program ever drives the candidate GPIO 19 ([media-audio.md](../system/media-audio.md) §7), so the stock unit does not toggle it; electrical side [car-link.md](car-link.md) §1.
- Third-party app on the stock unit: read the block fields listed here under `BapAllMutex`; to send a request write the request fields + pending flag and raise the request id ([shared-memory.md](shared-memory.md) §8.3). State machines in the control block (OPS, source change, seek, list fetch) run inside whichever process calls the library, so prefer the block-level requests that do not involve them (sound arrays, date / time, timers).
- Block field tables in this file are enough to write a C header for the six BAP blocks; offsets not listed are padding or internal bookkeeping of the lists.

## 11. Open questions

1. Names: all function names are behavioural. Public BAP function catalogues for LSG 0x31 / 0x2D / 0x1D / 0x11 / 0x25 were not consulted online; 🟡 items need that cross-check.
2. LSG 0x31: which radio conditions set mute flags 2 and 3 of fct 0x13; meaning of fct 0x14, 0x18 bits, 0x1C, 0x1D values, 0x20 bytes, of `b1` / `b2` in fct 0x10 and `b` / `c` in the source list; source type 0x0C; action table `0x35c3c` of the fct 0x17 state machine `0x35ab4` (not transcribed). MediaPlayer's consumers are now traced ([media-audio.md](../system/media-audio.md) §8.2, §8.3: source names, mute flags, volume types, remote-control codes, PI code); the rest needs a car trace.
3. LSG 0x2D: fct 0x0D, 0x12, 0x13, 0x1A (channel values and treble / bass / balance / fader: answered in §5, fader still 🟡).
4. LSG 0x1D: real names and units of fct 0x10, 0x11, 0x13; which ECU.
5. LSG 0x25: exact field names and scaling of fct 0x10, 0x11, 0x12; profile and night-rate fields; record addresses ≥ 3; the unread second byte of array Status; timer nibbles; what fct 0x0D switches.
6. Source-change state machine `0x36924`: full transition table (only entry actions and event sources are documented).
7. OPS: front / rear assignment of fct 0x12 / 0x13 and of FSG-Setup `b1` / `b2`; distance unit; fct 0x18 flag.
8. Everything here is from static reading; no payload was verified against a car or the emulator.
