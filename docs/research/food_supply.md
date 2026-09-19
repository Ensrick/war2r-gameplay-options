# Food supply and food used (static RE, Warcraft II: Remastered 1.0.2.2818)

Target: `Warcraft II.exe` 1.0.2.2818 (PE timestamp 1771967463). Static analysis only (Ghidra 12.1.3 headless, project
clone `war2r_a`, plus capstone disassembly and raw table dumps of the exe). All VAs are at preferred base 0x400000;
RVA = VA - 0x400000. Raw decompiles are `food_counters.c`, `food_consumers.c`, `food_events.c` in the Ghidra output
folder, next to the earlier `wrk_*.c` / `dat_*.c` dumps. Anything inferred rather than read is marked `[unverified]`.

## Short answers

**1. Rule and storage.** Supply is one `uint16` per player at **`0x91B50C`** (u16[16]). It is NOT recomputed per
tick: it is kept by relative adds from two "count" callbacks (COUNT.cpp, the assert string `count.c (1): %d %d` sits
in the remove function). A completed **farm / pig farm adds 4**, a completed **town hall / great hall / keep /
stronghold / castle / fortress adds 1**; death, capture, and type change subtract the same amount. Play experience
was right: farm 4, hall 1. The amounts are **instruction constants**, not a table and not a `unitdata.dat` field:
`lea eax,[edx*4]` in the farm callback `FUN_004b50e0`, the bare `+1 / -1` argument in the hall callback
`FUN_004b5160`. Which callback a unit type gets comes from a function-pointer table `0x8C0EF0[type]`.
Food USED is not stored as one number: every reader computes `0x91B38C[p] - 0x91B6AC[p]` = (all non-building units
of the player) minus (units of type 0x37 / 0x38 / 0x39, which are free).

**2. Consumers.** Eight reader functions (nine instructions), all `movzx` (unsigned), all clamp the supply to **200**
at read time (the stored word is never clamped): the train check in `FUN_004ac610` (UnitCost, message `stat_txt` 438 "Not enough food...build
more farms."), the computer AI's farm decision in `FUN_004dad80`, the resource bar (`FUN_004e9ab0`, init
`FUN_004ea0a0`), the Remastered HUD `FUN_0052f5f0`, the farm status panel (classic `FUN_004e56e0` + its redraw test
`0x4E6AE0`, Remastered `FUN_0052c530`). The AI decision is supply-based (`pendingFarms*4 + supply <= used + 3`),
not farm-count-based, so a larger hall supply just makes the AI build its first farm later. **Savegames do not carry
the supply**: every load zeroes the whole counter block (`FUN_004b5390`) and re-counts every live unit through the
same callbacks (`FUN_004ee210`).

**3. Recommended implementation: option (c), a stateless recompute from the tick hook.** Each tick, single player
only, for every player `p`: `food[p] = 4 * farms[p] + N * (halls[p] + keeps[p] + castles[p])`, where the four counts
are the game's own per-type counters (`0x91B48C`, `0x91B52C`, `0x91B54C`, `0x91B56C`, all u16[16]). Write only when
the value differs. No code patch, no new hook, no mod-side state, nothing to restore. With `N = 1` the formula IS the
vanilla value, so "off" and "multiplayer" are trivially inert. Details, ranking of the other options and the exact
bytes of the code-patch alternative are in "Options" below.

**4. What could break.** Nothing overwrites a recompute except the game's own relative `+/-1` on a hall event (fixed
one step later) and the zero-and-recount on a save load (fixed on the first tick). There are no per-race
differences (both races share each callback). The hall upgrade path (`FUN_004aca20`) is remove-count, type change,
add-count: net food change 0, the unit just moves from the town-hall counter to the keep counter to the castle
counter, which is why the formula must sum all three.

---

## Addresses

### Code (cdecl)

| VA | RVA | What | Notes | Evidence |
|---|---|---|---|---|
| 0x4B52D0 | 0xB52D0 | CountAdd(unit) | unit count or building count +1; if `state & 0x80` (complete): per-type counter +1, `callback(unit, +1)` | decompile + disasm below; 8 callers |
| 0x4B5730 | 0xB5730 | CountRemove(unit) | mirror image, `callback(unit, -1)`; prints `count.c (1): %d %d` on underflow | decompile; 7 callers |
| 0x4B50E0 | 0xB50E0 | Farm callback `(unit, int16 delta)` | `food[owner] += delta*4` | disasm below; table entries 0x3A, 0x3B |
| 0x4B5160 | 0xB5160 | Hall callback `(unit, int16 delta)` | `food[owner] += delta`; also sets bit `owner` in `0x918D47` | disasm below; table entries 0x4A, 0x4B, 0x58..0x5B |
| 0x4B5090 / 0x4B50B0 / 0x4B5120 | | Other callbacks (units / plain buildings / 0x3C 0x3D 0x48 0x49 0x60..0x63) | none touches `0x91B50C` | disasm |
| 0x4B5390 | 0xB5390 | ZeroCounters(saveBuf) | zeroes `0x91B38C..0x91B86B` always; score stats only when `saveBuf == 0` | decompile; single caller `0x4C45CB` |
| 0x4EE210 | 0xEE210 | InitUnits / LoadUnits(saveBuf), then re-count | loop `if ((state & 7) == 0) CountAdd(unit)` | decompile below |
| 0x4AC610 | 0xAC610 | UnitCost: the food check | `0x4AC668..0x4AC697` | disasm below |
| 0x4DAD80 | 0xDAD80 | AI worker manager: farm decision | `0x4DAF00..0x4DAF80` | disasm below |
| 0x4E4970 | 0xE4970 | AI "may build farm" callback (`*0x8C5B10`) | `(0x918D47 >> owner) & 1` and ALOW bit 0x10000 | decompile |
| 0x4E9AB0 / 0x4EA0A0 | | Resource bar update / init | `(used << 16) | min(supply,200) | 0x80000000` into widget 4 | decompile |
| 0x52F5F0 | | Remastered HUD | `min(supply,200)` at `0x52FCF1` | decompile |
| 0x4E56E0 / 0x4E6AE0 | | Classic farm panel / its redraw test | strings 0x1A3 "Food Usage", 0x1A4 "Grown:", 0x1A5 "Used:"; classic panel table `0x8C6308`, stride 0x10, `+8` redraw test, `+0xC` draw: entries 0x3A, 0x3B only | decompile, table dump, `stat_txt.tbl` |
| 0x52C530 | | Remastered farm panel | Remastered panel table `0x8C9F48[0x3A]`, `[0x3B]` (table base from the code ref at `0x52FFE6`) | decompile + dump |
| 0x4ACA20 | | Building-upgrade finish (kind 3 handler, `0x8C053C`) | CountRemove, `type = new`, CountAdd | decompile below |
| 0x4ED4E0 | | Construction progress | on completion: CountRemove, `state |= 0x80`, CountAdd | `dat_hp_xref.c` |
| 0x4EE380 | | Kill unit | `state |= 2`, then CountRemove (0x80 still set) | decompile |
| 0x4ED1B0 | | Change owner (capture / rescue) | CountRemove, owner change, CountAdd | decompile |
| 0x4EDA50 | | Convert all units of type A to B (ranger, paladin ... upgrades) | CountRemove, type change, CountAdd | decompile |
| 0x4AA6E0 / 0x4AB090 | | Unpack / pack the savegame globals block | reference `0x91B86C..` and `0x91B9EC..` (score stats), nothing in `0x91B38C..0x91B86B` | `find_refs` scan |

### Data

| VA | RVA | What | Type | Evidence |
|---|---|---|---|---|
| **0x91B50C** | 0x51B50C | **Food supply** per player | u16[16] | exactly two writers (`0x4B50F5`, `0x4B5182`) + the zeroing `movups` at `0x4B54AC`; nine `movzx` readers |
| 0x91B38C | 0x51B38C | Non-building units per player (counted from creation) | u16[16] | `inc word [eax*2+0x91b38c]` at `0x4B531F` when type flag 0x20 is clear |
| 0x91B3AC | 0x51B3AC | Buildings per player (complete or not) | u16[16] | same branch, flag 0x20 set |
| 0x91B6AC | 0x51B6AC | Food-free units: per-type counter of types 0x37, 0x38, 0x39 | u16[16] | `0x8C0B80[0x37..0x39]` all point here; every "used" reader subtracts it |
| 0x91B48C | 0x51B48C | Completed farms + pig farms | u16[16] | `0x8C0B80[0x3A]`, `[0x3B]`; only other ref is a `cmp ..., 4` mission trigger at `0x4F4917` |
| 0x91B52C | 0x51B52C | Completed town halls + great halls | u16[16] | `0x8C0B80[0x4A]`, `[0x4B]`; no direct code ref except the zeroing |
| 0x91B54C | 0x51B54C | Completed keeps + strongholds | u16[16] | `0x8C0B80[0x58]`, `[0x59]`; all direct refs are `cmp` (requirements, 110-gold deposit bonus) |
| 0x91B56C | 0x51B56C | Completed castles + fortresses | u16[16] | `0x8C0B80[0x5A]`, `[0x5B]`; all direct refs are `cmp` |
| 0x8C0B80 | 0x4C0B80 | Per-type counter pointer table | `u16*`[110], 0 = not counted | dump; 4 code refs, all in CountAdd / CountRemove / `FUN_004b5220` |
| 0x8C0EF0 | 0x4C0EF0 | Per-type count callback table | `void (*)(Unit*, int)`[110] | dump; exactly 2 code refs (`0x4B536B`, `0x4B57F8`) |
| 0x918D47 | 0x518D47 | Bitmask "player has had a hall" | u8 | set by the hall callback; `0xFF` in single player (`FUN_004ee210`); saved (`0x4AAE7C` / `0x4AB8AF`) |
| 0x9342B8 / 0x9342BC | | Classic panel's cached used / supply | u16 | `FUN_004e56e0`, `FUN_004e6a60` |
| 0x923218 | | AI build requests, `[owner*0x2F + (type-0x3A)]` | u16 | `FUN_004dad80` |
| 0x919458 | | Per-player counter bumped when a building callback runs with `delta <= 0` | u16[16] | all four building callbacks; buildings lost, score screen `[unverified]` |
| 0x91B3CC / 0x91B4EC | | Completed buildings / completed production-type buildings | u16[16] | building callbacks; meaning `[unverified]`, `0x91B4EC` is tested `== 0` in `FUN_004f4240` |

Callback table dump (file bytes, preferred-base pointers), index = unit type:

```
0x8c0fd0 [0x38]: 0x004b5090 0x004b5090 0x004b50e0 0x004b50e0 0x004b5120 0x004b5120 0x004b50b0 0x004b50b0
0x8c1010 [0x48]: 0x004b5120 0x004b5120 0x004b5160 0x004b5160 0x004b50b0 0x004b50b0 0x004b50b0 0x004b50b0
0x8c1050 [0x58]: 0x004b5160 0x004b5160 0x004b5160 0x004b5160 0x00000000 0x00000000 0x00000000 0x00000000
```

Counter pointer table dump, same indexing:

```
0x8c0c40 [0x30]: 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x00000000 0x0091b6ac
0x8c0c60 [0x38]: 0x0091b6ac 0x0091b6ac 0x0091b48c 0x0091b48c 0x0091b4cc 0x0091b4cc 0x0091b4ac 0x0091b4ac
0x8c0ca0 [0x48]: 0x0091b60c 0x0091b60c 0x0091b52c 0x0091b52c 0x0091b42c 0x0091b42c 0x0091b5ac 0x0091b5ac
0x8c0ce0 [0x58]: 0x0091b54c 0x0091b54c 0x0091b56c 0x0091b56c 0x00000000 0x00000000 0x00000000 0x00000000
```

Type names for 0x37 skeleton, 0x38 daemon, 0x39 critter come from the PUD unit list, not from the exe
(`data_tables.md` already has `HP[0x37]` = skeleton from the hard-coded override).

---

## 1. How the supply is kept: evidence

### The two callbacks

```
; FUN_004b50e0, farms 0x3A / 0x3B                      ; FUN_004b5160, halls 0x4A 0x4B 0x58..0x5B
0x4b50e3: mov edx, [ebp+0xc]        ; delta            0x4b5163: mov edx, [ebp+8]           ; unit
0x4b50e7: mov esi, [ebp+8]          ; unit             0x4b5166: movzx ecx, byte [0x918d47]
0x4b50ea: 8d 04 95 00 00 00 00  lea eax, [edx*4]       0x4b516d: movzx eax, byte [edx+0x2c] ; owner
0x4b50f1: movzx ecx, byte [esi+0x2c]                   0x4b5171: bts ecx, eax
0x4b50f5: 66 01 04 4d 0c b5 91 00                      0x4b5174: mov byte [0x918d47], cl
          add word [ecx*2+0x91b50c], ax  ; += 4*delta  0x4b517a: 0f b6 42 2c           movzx eax, byte [edx+0x2c]
0x4b5101: add word [eax*2+0x91b3cc], dx                0x4b517e: 66 8b 4d 0c           mov cx, [ebp+0xc]   ; delta
0x4b5109: test dx, dx / jg +0xc                        0x4b5182: 66 01 0c 45 0c b5 91 00
0x4b5112: inc word [eax*2+0x919458]                              add word [eax*2+0x91b50c], cx  ; += delta
                                                       0x4b518a: 0f b6 42 2c           movzx eax, byte [edx+0x2c]
                                                       0x4b518e: add word [eax*2+0x91b4ec], cx
                                                       0x4b519a: add word [eax*2+0x91b3cc], cx
                                                       0x4b51a2: test cx, cx / jg +0xc
                                                       0x4b51ab: inc word [eax*2+0x919458]
```

Every instruction in `.text` that touches `0x91B50C` (4-byte operand scan, decoded with capstone):

```
0x4ac66f: movzx edx, word ptr [esi*2 + 0x91b50c]     UnitCost
0x4b50f5: add word ptr [ecx*2 + 0x91b50c], ax        farm callback      <- writer
0x4b5182: add word ptr [eax*2 + 0x91b50c], cx        hall callback      <- writer
0x4b54ac: movups xmmword ptr [0x91b50c], xmm0        ZeroCounters (xmm0 = 0)
0x4daf00: movzx eax, word ptr [eax*2 + 0x91b50c]     AI farm decision
0x4e571a: movzx eax, word ptr [ecx*2 + 0x91b50c]     classic farm panel
0x4e6b17: movzx eax, word ptr [edx*2 + 0x91b50c]     classic panel redraw test
0x4e9bda / 0x4ea2e4: movzx eax, word ptr [...]       resource bar update / init
0x52c5a5 / 0x52c60c: movzx eax, word ptr [...]       Remastered farm panel
0x52fcf1: movzx eax, word ptr [ecx*2 + 0x91b50c]     Remastered HUD
```

### CountAdd (`FUN_004b52d0`; CountRemove `FUN_004b5730` is the mirror with -1)

```c
if ((typeflags[type] & 0x20) == 0) units[owner]++;        // 0x91B38C, counted even while incomplete
else                               buildings[owner]++;    // 0x91B3AC
if (unit->state & 0x80) {                                 // +0x1E: construction complete
  cnt = PTR_DAT_008c0b80[type];
  if (cnt) { old = cnt[owner]; cnt[owner] = old + 1; if (old == 1 && DAT_008c10a8[type]) FUN_004e8150(); }
  if (PTR_FUN_008c0ef0[type]) (*PTR_FUN_008c0ef0[type])(unit, 1);
  if ((unit[0x5f] & 2) == 0) FUN_004b5220(unit, 1);       // score statistics only
}
```

```
0x4b5364: movzx eax, byte ptr [esi + 0x27]
0x4b5368: mov eax, dword ptr [eax*4 + 0x8c0ef0]
0x4b536f: test eax, eax / je 0x4b537b
0x4b5373: push 1 / push esi
0x4b5376: ff d0                call eax                   ; plain indirect call, the exe has no CFG (DllCharacteristics 0x8140)
```

The per-type counter and the callback sit inside the same `state & 0x80` block in both functions, so at every
moment `food[p] == 4 * 0x91B48C[p] + 0x91B52C[p] + 0x91B54C[p] + 0x91B56C[p]` in the unmodded game. This identity is
what the recommended recompute relies on.

### When the counts run (all callers of CountAdd / CountRemove)

| Event | Function | Calls |
|---|---|---|
| Unit or building created | `FUN_004edb10` CreateUnit, `0x4EDF63` | Add. Units (`type < 0x3A` or `> 0x68`) always get `state |= 0x80` first; buildings only while a map is loading (`if (DAT_009348b8 != 0) state |= 0x80`), so a building placed during play adds no food yet |
| Construction finished | `FUN_004ed4e0`, `0x4ED67F` / `0x4ED68E` | Remove, `state |= 0x80`, Add: this is when a new farm / hall starts to feed |
| Building upgrade finished | `FUN_004aca20`, `0x4ACA96` / `0x4ACA9F` | Remove, `unit[0x27] = newType`, Add |
| Unit type conversion | `FUN_004eda50`, `0x4EDAA0` / `0x4EDACD` | Remove, type change, Add |
| Owner change | `FUN_004ed1b0`, `0x4ED217` / `0x4ED288` | Remove, new owner, Add |
| Death | `FUN_004ee380`, `0x4EE5D2` | Remove (after `state |= 2`; 0x80 is still set, so the food goes away at once) |
| Mission scripts | `FUN_004f4520`, `0x4F4653..0x4F4799` | Remove / flag change / Add, net 0 |
| After a savegame load | `FUN_004ee210`, `0x4EE35A` | Add for every unit with `(state & 7) == 0` |

Nothing runs per tick.

### Hall upgrade path (`FUN_004aca20`)

```c
sVar3 = FUN_004ed150(unit[0x27]);  sVar4 = FUN_004ed150(newType);
*(short *)(unit + 0x22) += sVar4 - sVar3;          // hp += newMax - oldMax
FUN_004b5730(unit);                                // hall callback -1, town-hall counter -1
*(undefined1 *)(unit + 0x27) = newType;
FUN_004b52d0(unit);                                // hall callback +1, keep counter +1
```

Same callback before and after, so the supply does not move in the unmodded game. The recompute sees the unit leave
`0x91B52C` and enter `0x91B54C` inside one call, so the sum never dips.

### Savegames: recomputed, not stored

`FUN_004c4280` (LoadGameOrMap), both paths:

```
0x4c43aa: call 0x4e0ab0        ; save path only: unpack the globals block (FUN_004aa6e0)
...
0x4c45ca: push esi / 0x4c45cb: call 0x4b5390        ; ZeroCounters(saveBuf): food, unit counts, per-type counters = 0
...
0x4c4602: call 0x4c4ba0        ; (save path) finalize tables
0x4c460d: call 0x4ee210        ; load the unit array from the save, then:
```

```c
// tail of FUN_004ee210
while (n != 0) { n--;
  if ((*(byte *)(u + 0x1e) & 5) == 0) FUN_004f0bc0(u);
  if ((*(byte *)(u + 0x1e) & 7) == 0) FUN_004b52d0(u);    // re-count, callbacks included
  u += 0x98; }
```

The zeroing comes AFTER the globals are unpacked and BEFORE the units are re-counted, so whatever a save file may
contain, the supply after a load is always the callbacks' result. In addition, a 4-byte operand scan of the pack /
unpack functions (`0x4AB090`, `0x4AA6E0`) finds references only to `0x91B86C..0x91B9DC` and `0x91B9EC..0x91BA3C`
(the score blocks that `FUN_004b5390` leaves alone when `saveBuf != 0`), none into `0x91B38C..0x91B86B`.

---

## 2. Consumers: evidence

### Train check (`FUN_004ac610`, runs for `type < 0x3A` only)

```
0x4ac668: cmp dword ptr [0x91c178], 0               ; Remastered mode flag, always 1 (data_tables.md)
0x4ac66f: movzx edx, word ptr [esi*2 + 0x91b50c]    ; supply
0x4ac677: je 0x4ac683
0x4ac679: mov eax, 0xc8 / cmp edx, eax / cmova edx, eax      ; cap 200
0x4ac683: movzx eax, word ptr [esi*2 + 0x91b6ac]
0x4ac68b: movzx ecx, word ptr [esi*2 + 0x91b38c]
0x4ac693: sub ecx, eax                              ; used
0x4ac695: cmp edx, ecx / jg ok                      ; train only while supply > used
0x4ac699: push "stat_txt_438" / call 0x58ab00       ; "Not enough food...build more farms."
```

The same function is what the AI calls before it trains or builds, so the computer obeys the same number. The
train-finished handler `FUN_004acb00` does not check food again.

### Computer AI (`FUN_004dad80`, the idle-worker manager)

```c
supply = food[owner];  if (DAT_0091c178 != 0 && supply > 200) supply = 200;
if ((*PTR_FUN_008c5b10)(owner) != 0) {              // FUN_004e4970: hall bit in 0x918D47 and farm allowed (ALOW 0x10000)
  farm = (workerType & 1) | 0x3a;
  if (supply < 200 &&
      (ushort)(aiRequests[owner*0x2f + (farm-0x3a)] * 4 + supply) <= (ushort)(units[owner] - freeUnits[owner] + 3) &&
      FUN_004ac610(worker, farm) == 0 && FUN_004dbc30(worker, &pos, farm) != 0)
    -> build a farm
}
```

`0x4daf76: shl ax, 2` is the hard-coded "a farm under way will give 4". The AI never looks at the farm count for
this decision, only at the supply word, so a hall worth N simply delays its first farm until `used + 3 >= N`, and an
AI whose halls already give 200 never builds one. The only reader of the farm counter `0x91B48C` outside COUNT.cpp is
`0x4F4917` (`cmp word [...], 4`, a mission-objective trigger inside `FUN_004f4520`), which a supply change does not
touch.

### UI

```c
// FUN_004e9ab0 (every frame from the game loops) and FUN_004ea0a0 (init): resource bar widget 4
uVar2 = 200;  if (food[local] < 0xc9) uVar2 = food[local];
*(uint *)(widget + 0x26) = (units[local] - freeUnits[local]) * 0x10000 | uVar2 | 0x80000000;

// FUN_004e56e0 classic farm panel (strings 0x1a3 / 0x1a4 / 0x1a5), FUN_0052c530 Remastered farm panel
DAT_009342b8 = units[local] - freeUnits[local];
DAT_009342bc = food[local];  if (DAT_0091c178 != 0 && 200 < DAT_009342bc) DAT_009342bc = 200;
```

All of them read the live word each time, so the bar and the farm panel show the modded supply with no extra work.
Only farms have the food panel, so a hall will not say how much food it gives: Remastered table `0x8C9F48[0x3A]`,
`[0x3B]` = `0x52C530` while the halls' entry `[0x4A]` is `0x52C680`; in the classic table the value `0x004E56E0`
exists in exactly two `.data` slots, `0x8C66B4` and `0x8C66C4` (entries 0x3A and 0x3B):

```
0x8c66a8 [farm 0x3A]: 0x00010026 0x0000003c 0x004e6ae0 0x004e56e0
0x8c67a8 [hall 0x4A]: 0x00030028 0x0000004c 0x004e6a40 0x004e62b0
```

---

## 3. Options

Ranked for "halls give N food, off by default, inert in multiplayer".

### 1st: (c) stateless recompute from the existing tick hook (recommended)

```cpp
// single player only (the tick already returns early when kRvaNetGame != 0)
const uint16_t* farms = At<uint16_t>(0x51B48C);   // all u16[16], index = player
const uint16_t* hall  = At<uint16_t>(0x51B52C);
const uint16_t* keep  = At<uint16_t>(0x51B54C);
const uint16_t* castl = At<uint16_t>(0x51B56C);
uint16_t*       food  = At<uint16_t>(0x51B50C);
for (int p = 0; p < 16; ++p) {
    const uint32_t halls = hall[p] + keep[p] + castl[p];
    if (farms[p] > 1600 || halls > 1600) continue;          // a counter that underflowed (count.c assert) is garbage
    const uint32_t want = std::min<uint32_t>(4u * farms[p] + N * halls, 0xFFFF);
    if (food[p] != want) food[p] = static_cast<uint16_t>(want);
}
```

- **Addresses written:** only `0x91B50C + 2*p`. Plain `.data` (section flags 0xC0000040, writable), no
  `VirtualProtect`.
- **Value range:** the word is `uint16`, every reader zero-extends it and clamps to 200, so anything from 0 to 65535
  is storage-safe and everything above 200 behaves as 200. Sensible setting range: **1..200** (1 = the game's own
  value). `N = 0` works but has a one-step artefact: when the last hall of a player without farms dies, the game's own
  `-1` takes the word from 0 to 0xFFFF (reads as 200) until the next tick corrects it.
- **Why it is safe to write an absolute value:** the supply has exactly two writers and both are mirrored by the
  per-type counters inside the same `state & 0x80` block (section 1), so the formula with `N = 1` reproduces the
  game's value bit for bit. There is no third contributor to lose.
- **Lag:** a hall event changes the word by the game's `+/-1` first; the mod corrects it on the next simulation step
  (13..80 ms). Inside a step the hooked AI tick runs before the unit pass that completes and kills buildings
  (`0x4c5001: call 0x4e89a0` ... `0x4c503b: call 0x4eea80`, same order at `0x4C51A8` / `0x4C51E4` in the other loop
  variant), so the correction always lands at the start of the following step.
- **Multiplayer:** never runs (existing `kRvaNetGame` gate). Nothing persists between games because
  `FUN_004b5390` zeroes the block at the start of every game, single or multi, new or loaded.
- **Savegames:** the supply is not in the save. A load gives the vanilla value until the first tick, then the
  modded one; a save made with the option on and loaded without the mod simply shows the vanilla supply (units above
  the cap stay alive, training is blocked until farms exist, which is the game's normal over-cap state).
- **Config hot reload / option switched off mid-game:** the next pass writes the formula with the new `N` (or with
  `N = 1`), so there is no drift and nothing to undo. If the option was never on in this process, skip the pass
  entirely; if it was on and is now off, run one pass with `N = 1`.
- **Computer players:** apply to all 16 slots so the rule is symmetric. The AI reads the same word (section 2) and
  needs no other change. Restricting it to human players (`controller[p] == 0`) is a one-line filter if wanted.

### 2nd: (d) swap the six hall entries of the callback table `0x8C0EF0`

Data write, not a code patch: entries 0x4A, 0x4B, 0x58..0x5B (VAs `0x8C1018`, `0x8C101C`, `0x8C1050`, `0x8C1054`,
`0x8C1058`, `0x8C105C`; file value `0x004B5160`, relocated at runtime) point to a mod function
`void __cdecl (Unit*, int delta)` that calls the original and then adds `delta * (N - 1)`. The table has only two
readers (`0x4B536B`, `0x4B57F8`), the call is a plain `call eax`, and the wrapper can test the load-time network
byte `0x922F5B` itself, so it is correct even for the re-count of a multiplayer save load. It is exact at the event,
with no lag. Against it: `+N` at completion and `-N` at death must use the same `N`, and the config hot-reloads, so
it needs a latched per-game value plus a reconciliation when the setting changes, which is option (c) again. The
original must still be called (it also sets the `0x918D47` hall bit and two other counters). More state for no
visible gain.

### 3rd: (b) in-place code patch of the hall callback

20 bytes at **VA `0x4B517A`** (RVA 0xB517A). No branch lands inside the window (rel32 scan of `.text`: none; the
function's only local branch is `0x4B51A5 jg 0x4B51B3`). `eax` already holds the owner on entry to the window (set
at `0x4B516D`, untouched by `bts` and the byte store), and the two `movzx eax, [edx+0x2c]` that are dropped are
redundant reloads.

```
original  0f b6 42 2c | 66 8b 4d 0c | 66 01 0c 45 0c b5 91 00 | 0f b6 42 2c
patched   66 6b 4d 0c NN | 66 01 0c 45 0c b5 91 00 | 0f 1f 00 | 66 8b 4d 0c
          imul cx, [ebp+0xc], NN   add [eax*2+0x91b50c], cx   nop3       mov cx, [ebp+0xc]   ; delta restored for the
                                                                                              ; two adds and the test that follow
```

`NN` is a sign-extended imm8, so 0..127. The `0c b5 91 00` inside the patch is an absolute address: the image is
relocated under ASLR, so the mod must write `base + 0x51B50C` there (the original operand sits at `0x4B5186` and is
fixed up by the loader; the patched one sits at `0x4B5183` and is not). Problems: (1) the re-count after a save load
runs at `0x4C460D`, before any tick and without passing the new-map hook, so restoring the bytes "in multiplayer"
the way `SyncRangeBonus` does is too late for a multiplayer save loaded right after a single-player session; it
would need another hook at `0x4C4602`; (2) a hot-reloaded `N` between a hall's `+N` and its `-N` leaves the word
wrong for the rest of the game (or wraps it to 65535 = 200 food); (3) page-protection changes and a 20-byte window
instead of 2. Usable only together with (c), and then (c) alone does the job.

### Not possible: (a) a data-table edit at map start

There is no food table. The 33-entry UDTA descriptor list (`data_tables.md`) has no food column, `unitdata.dat`
therefore has no such field, and the amounts are the instruction constants shown in section 1. The only tables
involved hold pointers (`0x8C0EF0` callbacks, `0x8C0B80` counters); they are static `.data`, not part of the
savegame, and editing them is option (d).

---

## Recipe

1. `game.h`: add RVAs `kRvaFoodSupply = 0x51B50C`, `kRvaFarmCount = 0x51B48C`, `kRvaTownHallCount = 0x51B52C`,
   `kRvaKeepCount = 0x51B54C`, `kRvaCastleCount = 0x51B56C` (all u16[16]); optionally `kRvaUnitCount16 = 0x51B38C`
   and `kRvaFoodFreeUnits = 0x51B6AC` if the mod ever wants "food used".
2. `selftest` byte checks against the exe file (preferred-base values):
   `0x4B50EA` = `8D 04 95 00 00 00 00`, `0x4B50F5` = `66 01 04 4D 0C B5 91 00` (farm gives 4, into `0x91B50C`);
   `0x4B5182` = `66 01 0C 45 0C B5 91 00` (hall gives 1);
   dword `0x8C0EF0 + 4*0x3A` = `0x004B50E0`, `+ 4*0x4A` and `+ 4*0x58..0x5B` = `0x004B5160`;
   dword `0x8C0B80 + 4*0x3A` = `0x0091B48C`, `+ 4*0x4A` = `0x0091B52C`, `+ 4*0x58` = `0x0091B54C`,
   `+ 4*0x5A` = `0x0091B56C`, `+ 4*0x37` = `0x0091B6AC`.
3. Tick (after the multiplayer gate): the loop from option (c). Run it every tick; it is 16 iterations of five
   reads.
4. Setting: off by default, integer 1..200. When off and never applied in this process, do nothing at all.
5. After a game patch: find the string `count.c (1): %d %d` (CountRemove), its twin CountAdd is the other reader of
   the two tables; the hall callback is the table entry for type 0x4A, the supply array is the operand of its first
   `add word`.

## Risks

- **One-step lag** after a hall completes, dies, changes owner, or after a save load. A train command that lands in
  exactly that step is judged with the vanilla hall value. Harmless, not a desync source in single player.
- **Counter underflow.** `FUN_004b5730` logs `count.c (1)` and still decrements when a per-type counter is already
  0. That is a game bug path; the `> 1600` guard in the loop keeps a wrapped counter from turning into 65535 food.
- **`N = 0`:** the one-step 200-food blip described above. Keep the minimum at 1 unless the author wants "halls give
  nothing".
- **A future "farms give M" setting** fits the same formula (`M * farms`), but the AI's pending-farm estimate stays
  at 4 (`shl ax, 2` at `0x4DAF76`), so with `M < 4` the AI would under-build by one farm at a time; with `M > 4` it
  over-builds slightly. Not an issue for the hall setting.
- **Mission objectives** that count farms (`0x4F4917`, 4 farms) are unaffected: they read the farm counter, not the
  supply.
- **Everything here is single-player only**, same reason as every other feature: a local write to simulation state
  desyncs a network game. The tick gate already covers it; do not add a second path around it.

## [unverified]

- Meaning of `0x91B3CC`, `0x91B4EC` and `0x919458` (completed buildings / production buildings / buildings lost):
  inferred from which callbacks touch them, not from their readers.
- `0x918D47` as "player has had a hall": read from the hall callback, `FUN_004ee210` (`= 0xFF` unless Remastered
  multiplayer) and `FUN_004e4970`; the roughly twenty other references at `0x4E427F..0x4E4B9F` were not decompiled.
- Unit names for types 0x37 / 0x38 / 0x39 (skeleton / daemon / critter) come from the PUD format.
- Field meanings of the classic status panel table `0x8C6308` beyond `+8` / `+0xC`.
- Multiplayer savegame loading exists in this build: assumed from the BNE lineage and from the network branches in
  `FUN_004c4280`; it only matters for the rejected options (b) and (d).
- Nothing in this report was run in the game. The recompute has to be confirmed in play: one peasant, build a hall,
  train without farms, save, load, destroy the hall.
