# Unit / upgrade data tables (static RE, Warcraft II: Remastered 1.0.2.2818)

Read-only analysis of `Warcraft II.exe` (PE timestamp 1771967463, copy at `C:\Tools\ghidra_projects\bin\war2r.exe`),
Ghidra project `war2r`. All VAs are at the preferred base 0x400000 (subtract 0x400000 for the RVAs `src/game.h` uses).
Decompile dumps that back every claim are in `C:\Tools\ghidra_projects\out\dat_*.c`.
Anything not read directly out of the exe or the data files is marked [unverified].

## Short answers

1. **Unit data.** One descriptor list at `0x8C0150` (`{ptr, elemSize, count}` triples, zero terminated) drives every
   load/save of the UDTA blob. `FUN_004c4d10(desc, filename)` copies a Rez file into the tables,
   `FUN_004c4d90(desc, len)` copies a map's UDTA section. Costs stay as the **raw byte (cost/10)**; every reader
   multiplies by 10 on the fly. Price paid = `FUN_004ac610` (train, building upgrade, building placement), price shown =
   `FUN_004e7fa0` (classic status line) and `FUN_0052e080` (Remastered tooltip). All three index the byte tables live,
   so a table edit changes both what is paid and what is shown.
2. **Upgrade data.** Descriptor list at `0x8C02E8`, same two loader functions. Costs are plain `u16` (not /10). Index
   list = PUD UGRD order, confirmed against `upgrades.dat` flag/group columns, the exe's group-counter use, and
   `names.h` `UG_*` ids (all three agree).
3. **Reload timing.** Tables are filled at process start, at **every new map load**, and **restored from the
   savegame on every save load** (the save file contains both table sets). A mod that multiplies "after every load"
   WILL double-apply after a save load. Reliable signal: the two call sites of `FUN_004c4ba0` - `0x4D2C46` (new map
   only) and `0x4C4602` (savegame only). Apply at `0x4D2C46`, never at `0x4C4602`.
4. **Sight.** Never cached in the unit. The runtime sight table holds **function pointers**, not radii, and is called
   live when a unit moves a tile and on the periodic full fog refresh (every 100 steps). Editing it affects existing
   dragons/gryphons at their next move or refresh. The new value must be one of the 10 pointers in `0x8C1E28`.
5. **Hit points.** Creation copies `HP[type]` into `unit+0x22` (confirmed). All maths on max HP is 32-bit or ratio
   based and survives x2/x4, but HP is handled as **signed 16-bit** in several places and the UI hides numbers at
   >= 10000, so keep every max HP <= 9999. There is no global per-tick clamp; clamps exist only in construction,
   repair, heal, death coil and berserker regen.
6. **Regen.** Berserker regeneration is in the per-step unit timer pass `FUN_004ef480`: `unit+0x26` (the mana byte,
   unused by berserkers) is incremented every simulation step, at 0xF0 (240) it resets and HP += 1 if below max.
   Step delay comes from `0x83A478[speedSetting]` = 80,60,53,46,40,33,26,20,13 ms.

## Files in `Data\Rez`

`unitdata.dat` (5948 B, full UDTA blob), `unitdato.dat` (5694 B, the same blob without the trailing `word[127]`;
loaded once at start only to capture the four "obsolete" word tables), `upgrades.dat` (780 B, UGRD blob), `ai.bin`
(22377 B, AI scripts), `script.bin` (4998 B), `CheatCmd.bin`, `CDVersion.txt` / `dataversion.txt`. Everything else is
UI: `*.bin` dialog layouts (`gamemenu`, `options`, `spd_dlg`, `snd_dlg`, `savegame`, `loadgame`, `msnstat*`, `glu*`,
chat/network dialogs ...) and `*.tbl` string tables (`stat_txt.tbl`, `tips.tbl`, `objctivs.tbl`, `network.tbl`,
per-mission briefings `human1..14`, `orc1..14`, `2xhum1..12`, `2xorc1..12`, `humand1..3`, `orcd1..3`, credits ...).

Strings in the exe: `rez\unitdato.dat` @ `0x83F1DC`, `rez\unitdata.dat` @ `0x83F1F0`, `rez\upgrades.dat` @ `0x83FC18`.

## Address table

### Unit data (descriptor list `0x8C0150`, source file order = PUD UDTA order)

| VA | What | Elem | Count | Evidence |
|---|---|---|---|---|
| `0x917128` | overlap frames (+0x6E added after load) | u16 | 110 | desc[0]; `FUN_004c4ba0` second loop |
| `0x917208` `0x917308` `0x917408` `0x917508` | obsolete | u16 | 127 each | desc[1..4]; skipped by `FUN_004c4d90`, swapped with backup `0x91C190..` by `FUN_004c4a70` |
| `0x917608` | **sight** - raw index 0..9 while loading, then **function pointer** | u32 | 110 | desc[5]; `FUN_004c4ba0`, `FUN_004ef410` |
| `0x9177C0` | **hit points** | u16 | 110 | desc[6]; `FUN_004edb10`, `FUN_004ee1f0` |
| `0x9178A0` | magic (initial mana flag) | u8 | 110 | desc[7]; `FUN_004edb10` copies to unit+0x26 |
| `0x917910` | **build time** | u8 | 110 | desc[8]; `FUN_004ac610` (`*2`), `FUN_004ed0e0` |
| `0x917980` | **gold cost / 10** | u8 | 110 | desc[9]; `FUN_004ac610` (`*10`) |
| `0x9179F0` | **lumber cost / 10** | u8 | 110 | desc[10] |
| `0x917A60` | **oil cost / 10** | u8 | 110 | desc[11] |
| `0x917AD0` | unit size in tiles (w,h pairs) | u16 | 220 | desc[12]; fog centre maths in `FUN_004ef350` |
| `0x917C88` | box size in pixels (w,h pairs) | u16 | 220 | desc[13]; health bar `FUN_004efb00` |
| `0x917E40` | **attack range** | u8 | 110 | desc[14]; refs `0x4CA763 0x4CAB65 0x4D9455 0x4E5FAF 0x4EE685 0x52DD6C` |
| `0x917EB0` | react range (computer) | u8 | 110 | desc[15] |
| `0x917F20` | react range (human) | u8 | 110 | desc[16] |
| `0x917F90` | **armor** | u8 | 110 | desc[17]; stats panel `FUN_004e4fd0`, 13 code refs |
| `0x918000` | selectable | u8 | 110 | desc[18] |
| `0x918070` | priority | u8 | 110 | desc[19] |
| `0x9180E0` | **basic damage** | u8 | 110 | desc[20]; `FUN_004e4fd0`, 10 code refs |
| `0x918150` | **piercing damage** | u8 | 110 | desc[21]; `FUN_004bda4a`; `FUN_004c49a0` patches [8],[9],[0x12],[0x13] = 7 |
| `0x9181C0` | weapons upgradable | u8 | 110 | desc[22] |
| `0x918230` | armor upgradable | u8 | 110 | desc[23]; `FUN_004e4fd0` |
| `0x9182A0` | missile | u8 | 110 | desc[24] |
| `0x918310` | unit type (land/air/sea) | u8 | 110 | desc[25]; `FUN_004edb10` |
| `0x918380` | decay rate | u8 | 110 | desc[26]; `FUN_004ef480` |
| `0x9183F0` | annoy | u8 | 110 | desc[27] |
| `0x918460` | second mouse action | u8 | 58 | desc[28] |
| `0x9184A0` | point value | u16 | 110 | desc[29] |
| `0x918580` | can target | u8 | 110 | desc[30]; AI strength `FUN_004cc1d0` |
| `0x9185F0` | **flags** | u32 | 110 | desc[31] |
| `0x9187A8` | obsolete tail | u16 | 127 | desc[32]; not in `unitdato.dat`, not saved |

Element sizes and counts are read straight from the descriptor triples; the sum (5948 bytes) equals the size of
`unitdata.dat`, and without the last entry 5694 = `unitdato.dat`.

### Upgrade data (descriptor list `0x8C02E8`)

| VA | What | Elem | Count | Evidence |
|---|---|---|---|---|
| `0x9188A8` | research time | u8 | 52 | `FUN_004ac9d0` (`*2` into the production timer) |
| `0x9188E0` | **gold** | u16 | 52 | `FUN_004ac9d0`, `FUN_004e7fa0`, `FUN_0052e080` |
| `0x918948` | **lumber** | u16 | 52 | same three |
| `0x9189B0` | **oil** | u16 | 52 | same three |
| `0x918A18` | icon | u16 | 52 | refs `0x4E6107 0x52C501` |
| `0x918A80` | group (index into counter pointer table `0x8C03F8`) | u16 | 52 | `FUN_004acca0` |
| `0x918AE8` | flags (ALOW bit of the upgrade/spell) | u32 | 52 | `FUN_004ace10`, `FUN_004acbc0`, `FUN_004acca0` |

### Code and globals

| VA | What | Evidence |
|---|---|---|
| `0x4CD950` | startup init (INIT.cpp): loads `unitdato.dat`, swaps obsolete tables, loads `unitdata.dat`, calls both override functions | decompile |
| `0x4D0F90` | "start a game" - only caller of `FUN_004c4280` (`0x4D10FE`) | caller list |
| `0x4C4280` | LoadGameOrMap(saveHandle, pudBuf, pudLen) (GAME.cpp). `saveHandle != 0` = savegame path | decompile |
| `0x4D2B40` | PUD pass 1: section table `0x8C44E0` (TYPE VER DESC OWNR ERA ERAX SIGN DIM **UDTA UGRD** ALOW SIDE SGLD SLBR SOIL AIPL), then default files if the map did not supply data | decompile |
| `0x4D2440` / `0x4D2500` | UDTA / UGRD section handlers (read the "use default" word into `0x9225A8` / `0x9225AC`) | table `0x8C4540..` |
| `0x4C4D10` | LoadDataFile(desc, filename) (GAMEDATA.cpp) | 4 call sites |
| `0x4C4D90` | LoadDataFromPud(desc, len) | called by both handlers |
| `0x4C49A0` | hard-coded unit balance overrides (default data only) | disassembly below |
| `0x4C49D0` | hard-coded upgrade balance overrides (default data only) | disassembly below |
| `0x4C4BA0` | finalize tables: sight index -> fn pointer, overlap += 0x6E. Exactly two callers | `find_calls` |
| **`0x4D2C46`** | `E8 55 1F FF FF` `call 0x4C4BA0` - **new map path only** | disassembly |
| **`0x4C4602`** | `E8 99 05 00 00` `call 0x4C4BA0` - **savegame load path only** | disassembly |
| `0x4D2C50` | PUD pass 2 (MTXM SQM OILM REGM UNIT); UNIT handler `0x4D1D60` creates units at `0x4D1EF7` - after the tables are final | `find_calls` |
| `0x4E0E10` | SaveGame(file, name) (SFILE.cpp), single caller `0x4E0DE7` | decompile |
| `0x4E0C40` | WriteTables(file, desc) - call sites `0x4E1234` (unit), `0x4E125C` (upgrade) | decompile |
| `0x4C4C60` | ReadTables(1, desc, bufPtr) - call sites `0x4C45EE` (unit), `0x4C45FB` (upgrade) | decompile |
| `0x4C4C10` / `0x4C4BE0` | save helpers: sight pointer -> index, overlap - 0x6E | decompile |
| `0x91BFB0` (u16) | 1 while the current game came from a savegame, 0 for a new map (`mov [0x91bfb0], si` at `0x4C4295`) | disassembly |
| `0x9348B8` (u32) | 1 for the whole duration of `FUN_004c4280` ("loading"), 0 otherwise | decompile |
| `0x922F5B` (u8) | network game flag, already valid at map-load time (`FUN_0049e050` returns it; `0x91C6F4` is copied from it later in `FUN_004c57f0`) | decompile |
| `0x91C184` (u16) | game state, 3 = in game (`FUN_004c4870` getter, `FUN_004c48e0` setter) | disassembly |
| `0x8C1E28` | sight function pointer table, 10 entries (index = sight 0..9) | dump |
| `0x4AC610` | UnitCost(unit, type): fills pending cost globals, checks food, then `FUN_004ad1c0` affordability | decompile |
| `0x4AC9D0` | UpgradeCost(unit, upgradeId): same for upgrades and spell research | decompile |
| `0x4ACE10` | StartProduction(building, id, kind) kind 0 train, 1 spell research, 2 upgrade, 3 building upgrade; tables `0x8C0510` (validate) `0x8C0520` (cost) `0x8C0530` (finish/cancel) | decompile + dump |
| `0x4AD220` / `0x4ACD60` | deduct / refund the pending cost | disassembly |
| `0x91AAB0` u16, `0x91AAB4` `0x91AAB8` `0x91AABC` u32 | pending time, gold, lumber, oil | `FUN_004ac610` |
| `0x919128` / `0x9190E8` / `0x919168` | gold / lumber / oil per player, u32[16] | `FUN_004ad220` |
| `0x4BDDC0` | build order handler (peasant arrives, building is created, cost deducted) | decompile |
| `0x4DC2C0` | human "place building" command (validates cost before issuing the order) | decompile |
| `0x4E7FA0` | classic status-line cost display | decompile |
| `0x52E080` | Remastered tooltip cost display | decompile |
| `0x4EDB10` | CreateUnit(x, y, type, owner) | decompile |
| `0x4EE1F0` / `0x4ED150` | GetMaxHp(unit) / GetMaxHp(type), both return 1 when the table holds 0 | decompile |
| `0x4EF480` | per-step unit timer pass (spell timers, berserker regen, mana regen, decay); single caller `0x4EEF93` at the end of `FUN_004eea80` | decompile |
| `0x4EEA80` | per-step unit update; called from both loop variants `FUN_004c5190` / `FUN_004c4e80` | decompile |
| `0x4EF350` / `0x4EF410` | reveal fog for one unit / pick the unit's sight function | decompile |
| `0x8C03F8` | upgrade group -> per-player counter (u8[16]) pointers: 0 `0x918BEC` missile weapons, 1 `0x918BFC` melee weapons, 2 `0x918C1C` shields, 3 `0x918C2C` ship cannons, 4 `0x918C3C` ship armor, 5 `0x918C4C`, 6 `0x918C5C` catapult/ballista, 7 `0x918C6C` ranger/berserker, 8 `0x918C7C` longbow/light axes, 9 `0x918C8C` scouting, 10 `0x918C9C` marksmanship/regeneration | dump + users |
| `0x964CA8` (u32) | game speed setting 0..8 | `FUN_004c4880`, `FUN_004c4910` |
| `0x83A478` (u8[9]) | ms per simulation step by speed setting (Remastered mode) | dump + `FUN_004c4e80` |
| `0x83A460` (u8[7*3]) | classic-mode delays (unused: `0x91C178` is always 1) | dump |
| `0x91C178` (u32) | "Remastered mode" flag, set to 1 once at startup (`push 1; call 0x4C4960` at `0x4D13B4`); also sets unit cap 1600 | disassembly |

## 1. Unit data tables

Loader (GAMEDATA.cpp), walks the descriptor triples and memcpy's consecutive slices of the file:

```c
void FUN_004c4d10(int *desc, char *filename) {
  buf = FUN_00484e70(filename, 0, &len, "...GAMEDATA.cpp", 0x1c0);
  for (p = desc; p[0] != 0; p += 3) { n = p[2] * p[1]; memcpy(p[0], src, n); src += n; }
  free(buf);
}
```

Startup (`FUN_004cd950`):

```c
PTR_DAT_008c02d0 = 0;                                   // drop the last descriptor entry (file is shorter)
FUN_004c4d10(&PTR_DAT_008c0150, "rez\\unitdato.dat");
PTR_DAT_008c02d0 = saved;
FUN_004c4a70(&PTR_DAT_008c0150);                        // swap the 4 obsolete tables into backup 0x91C190..
FUN_004c4d10(&PTR_DAT_008c0150, "rez\\unitdata.dat");
FUN_004c49a0();  FUN_004c49d0();
```

New map (`FUN_004d2b40`, disassembly `0x4D2C04..0x4D2C4B`):

```c
DAT_009225a8 = 1; DAT_009225ac = 1;                     // "use default data" until a section says otherwise
if (!FUN_004d2ca0(&DAT_008c44e0, 0x10)) return 0;       // runs the UDTA / UGRD handlers among others
if (DAT_009225a8 != 0) { FUN_004c4d10(&PTR_DAT_008c0150, "rez\\unitdata.dat"); FUN_004c49a0(); }
if (DAT_009225ac != 0) { FUN_004c4d10(&PTR_DAT_008c02e8, "rez\\upgrades.dat"); FUN_004c49d0(); }
FUN_004c4ba0();                                         // 0x4D2C46
```

UDTA handler `FUN_004d2440`: reads the leading word into `DAT_009225a8`; nonzero = skip the section (defaults are
loaded afterwards), zero = `FUN_004c4d90(&PTR_DAT_008c0150)` copies the map's own data (and then the hard-coded
overrides are NOT applied). In a network game with certain game types the section is always skipped.

Hard-coded overrides on top of the default file (so `unitdata.dat` is not the final word):

```
0x4c49a5: mov word ptr [0x9179aa], 0xe1e1   ; gold[0x2A], gold[0x2B] = 225 -> gryphon rider / dragon cost 2250 (file: 2500)
0x4c49ae: mov word ptr [0x91782e], ax(0x3c) ; HP[0x37] skeleton = 60 (file: 40)
0x4c49b4: mov word ptr [0x918158], 0x707    ; piercing[8],[9] archer / axethrower = 7 (file: 6)
0x4c49bd: mov word ptr [0x918162], 0x707    ; piercing[0x12],[0x13] ranger / berserker = 7 (file: 6)
```

Costs stay raw bytes. The pay path:

```c
void FUN_004ac610(int unit, byte type) {
  DAT_0091aab0 = (ushort)BUILD_TIME[type] * 2;
  DAT_0091aab4 = (uint)GOLD[type]   * 10;
  DAT_0091aab8 = (uint)LUMBER[type] * 10;
  DAT_0091aabc = (uint)OIL[type]    * 10;
  if (type < 0x3a) { ...food check, "stat_txt_438"... }
  FUN_004ad1c0(owner);                                  // returns an error string when gold/lumber/oil are short
}
```

`FUN_004ace10` (StartProduction) calls `PTR_FUN_008c0520[kind]` = {`004ac610`, `004ac9d0`, `004ac9d0`, `004ac610`}
and, when it returns 0, subtracts `DAT_0091aab4/8/c` from the player's `0x919128 / 0x9190E8 / 0x919168`. The
finish/cancel handlers (`0x8C0530`: `004acb00`, `004acbc0`, `004acca0`, `004aca20`) refund the same globals, which are
recomputed from the table at cancel time. Building placement: `FUN_004dc2c0` validates with `FUN_004ac610` when the
order is given, `FUN_004bddc0` calls `FUN_004ac610` again when the worker arrives, creates the building and calls
`FUN_004ad220(owner)` to deduct (computer players prepay in `FUN_004da300` and are refunded by `FUN_004acd60`).
`FUN_004ee6a0` refunds `cost*30/4` (75 %) of gold and lumber when an unfinished building dies.

UI readers, both live:

```c
// FUN_004e7fa0 (classic status line), button kind 1 = unit, 2 = upgrade, 3 = spell
FUN_004e9c20(GOLD[id]*10, LUMBER[id]*10, OIL[id]*10);              // written as (b + b*4)*2
FUN_004e9c20(UPG_GOLD[id], UPG_LUMBER[id], UPG_OIL[id]);
// FUN_0052e080 (Remastered tooltip)
local_24[0] = GOLD[id] * 10;  local_24[1] = LUMBER[id] * 10;  local_24[2] = OIL[id] * 10;
local_24[0] = UPG_GOLD[id];   local_24[1] = UPG_LUMBER[id];   local_24[2] = UPG_OIL[id];
```

The tables are global, not per player: the computer pays the same modified prices.

## 2. Upgrade data tables

Same loader functions with descriptor `0x8C02E8`. `upgrades.dat` is only loaded at map load (startup only runs the
override function). Overrides applied after the default file (`FUN_004c49d0`, widths checked in the disassembly):

| idx | upgrade | file time / gold / lumber | runtime time / gold / lumber |
|---|---|---|---|
| 4, 6 | arrows 1 / throwing axes 1 | 200 / 300 / 300 | 150 / 200 / 200 |
| 5, 7 | arrows 2 / throwing axes 2 | 250 / 900 / 500 | 200 / 600 / 300 |
| 25, 29 | longbow / lighter axes | 250 / 2000 / 0 | 200 / 1000 / 300 |
| 26, 30 | ranger / berserker scouting | 250 / 1500 / 0 | 150 / 500 / 200 |
| 27 | ranger marksmanship | 250 / 2500 / 0 | 200 / 2000 / 0 |
| 31 | berserker regeneration | 250 / 3000 / 0 | 150 / 1000 / 0 |
| 35 | healing | 200 / 1000 / 0 | time 100 |
| 36 | exorcism | 200 / 2000 / 0 | time 150 |

Index list. Sources: (a) `upgrades.dat` parsed with the UGRD layout - the flags column is the ALOW bit and the group
column is the counter group; (b) exe: `FUN_004ace10` kind 2 requires counter level 0 for even ids and 1 for odd ids
below 24 and maps ids 0-3 -> `0x918BFC`, 4-7 -> `0x918BEC`, 8-11 -> `0x918C1C`, 12-15 -> `0x918C2C`, 16-19 ->
`0x918C3C`, 20-23 -> `0x918C5C`; `FUN_004bda4a` adds counter `0x918BEC` (group 0) to piercing damage only for types
8, 9, 0x12, 0x13; `FUN_004ef410` gives types 0x12/0x13 sight 9 when counter `0x918C8C` (group 9) is set;
`FUN_004ef480` regenerates type 0x13 when `0x918C9C` (group 10) is set; `FUN_004acca0` on flag 0x10000 converts 8 ->
0x12 and 9 -> 0x13; `FUN_004acbc0` on flag 0x100000 converts 6 -> 0xC and 7 -> 0xD; (c) `names.h` `UG_*`. All agree.

| idx | meaning | file gold/lumber/oil | group | flag |
|---|---|---|---|---|
| 0, 1 | swords 1, 2 | 800/0/0, 2400/0/0 | 1 | 0x4, 0x8 |
| 2, 3 | battle axes 1, 2 | 500/100/0, 1500/300/0 | 1 | 0x4, 0x8 |
| 4, 5 | arrows 1, 2 | 300/300/0, 900/500/0 (runtime 200/200, 600/300) | 0 | 0x1, 0x1 |
| 6, 7 | throwing axes 1, 2 | same as arrows | 0 | 0x1, 0x2 |
| 8, 9 | human shields 1, 2 | 300/300/0, 900/500/0 | 2 | 0x10, 0x20 |
| 10, 11 | orc shields 1, 2 | 300/300/0, 900/500/0 | 2 | 0x10, 0x20 |
| 12, 13 | human ship cannons 1, 2 | 700/100/1000, 2000/250/3000 | 3 | 0x40, 0x80 |
| 14, 15 | orc ship cannons 1, 2 | same | 3 | 0x40, 0x80 |
| 16, 17 | human ship armor 1, 2 | 500/500/0, 1500/900/0 | 4 | 0x100, 0x200 |
| 18, 19 | orc ship armor 1, 2 | same | 4 | 0x100, 0x200 |
| 20, 21 | catapult 1, 2 | 1500/0/0, 4000/0/0 | 6 | 0x1000, 0x2000 |
| 22, 23 | ballista 1, 2 | 1500/0/0, 4000/0/0 | 6 | 0x1000, 0x2000 |
| 24 | ranger upgrade | 1500/0/0 | 7 | 0x10000 |
| 25 | longbow | 2000/0/0 (runtime 1000/300) | 8 | 0x20000 |
| 26 | ranger scouting | 1500/0/0 (runtime 500/200) | 9 | 0x40000 |
| 27 | ranger marksmanship | 2500/0/0 (runtime 2000) | 10 | 0x80000 |
| 28 | berserker upgrade | 1500/0/0 | 7 | 0x10000 |
| 29 | lighter axes | 2000/0/0 (runtime 1000/300) | 8 | 0x20000 |
| 30 | berserker scouting | 1500/0/0 (runtime 500/200) | 9 | 0x40000 |
| 31 | berserker regeneration | 3000/0/0 (runtime 1000) | 10 | 0x80000 |
| 32, 33 | ogre-mage upgrade, paladin upgrade | 1000/0/0 | 20 | 0x100000 |
| 34 | holy vision | 0 | - | spell bit 0 |
| 35 | healing | 1000 | - | bit 1 |
| 36 | exorcism | 2000 | - | bit 3 |
| 37 | flame shield | 1000 | - | bit 4 |
| 38 | fireball | 0 | - | bit 5 |
| 39 | slow | 500 | - | bit 6 |
| 40 | invisibility | 2500 | - | bit 7 |
| 41 | polymorph | 2000 | - | bit 8 |
| 42 | blizzard | 2000 | - | bit 9 |
| 43 | eye of kilrogg | 0 | - | bit 10 |
| 44 | bloodlust | 1000 | - | bit 11 |
| 45 | raise dead | 1500 | - | bit 13 |
| 46 | death coil | 0 | - | bit 14 |
| 47 | whirlwind | 1500 | - | bit 15 |
| 48 | haste | 500 | - | bit 16 |
| 49 | unholy armor | 2500 | - | bit 17 |
| 50 | runes | 1000 | - | bit 18 |
| 51 | death and decay | 2000 | - | bit 19 |

Spell rows 34-51 use the group column for something else (it just repeats the bit number); spell research goes
through kind 1 (`FUN_004acbc0`), which ORs the flag into `0x919250[owner]` (the "spells researched" dword the autocast
code already reads).

## 3. Reload timing and savegames

`FUN_004c4280(saveHandle, pudBuf, pudLen)` is the only game-start function (single caller `0x4D10FE`). Its first
instruction block stores `saveHandle != 0` into the word at `0x91BFB0`.

- **Process start:** `FUN_004cd950` as shown above.
- **New map** (`saveHandle == 0`; campaign map, custom PUD, restart): `FUN_004d2b40` refills both table sets from
  the PUD or from the Rez files + overrides, then `FUN_004c4ba0` at `0x4D2C46`. Units are created later, in the
  second PUD pass `FUN_004d2c50` (UNIT handler `0x4D1D60` -> `call 0x4EDB10` at `0x4D1EF7`), so they already see the
  final tables. Nothing accumulates between maps: every new map starts from fresh file data.
- **Savegame load** (`saveHandle != 0`):

```c
if (bVar7) {                                            // FUN_004c4280, saveHandle != 0
  uVar3 = FUN_004c4c60(1, &PTR_DAT_008c0150, uVar3);    // 0x4C45EE  memcpy every unit table out of the save buffer
  uVar3 = FUN_004c4c60(1, &PTR_DAT_008c02e8, uVar3);    // 0x4C45FB  same for the upgrade tables
  FUN_004c4ba0();                                       // 0x4C4602
  ...
```

  `FUN_004c4c60` calls `FUN_004e0610(dst, len, src)` = memcpy for every table except the four obsolete ones
  (skipped with `FUN_004e0b20`) and `0x9187A8`.
- **Savegame write** (`FUN_004e0e10`, SFILE.cpp):

```c
FUN_004c4a70(&PTR_DAT_008c0150);  PTR_DAT_008c02d0 = 0;
iVar15 = FUN_004e0c40(file, &PTR_DAT_008c0150);         // 0x4E1234  all unit tables
PTR_DAT_008c02d0 = saved;  FUN_004c4a70(&PTR_DAT_008c0150);
... FUN_004e0c40(file, &PTR_DAT_008c02e8) ...           // 0x4E125C  all upgrade tables
```

  `FUN_004e0c40` writes each table verbatim, except sight (pointer -> index via `FUN_004c4c10`) and overlap frames
  (`FUN_004c4be0`, minus 0x6E).

Consequences:

- A save made while the mod's values are in the tables carries those values. Multiplying again after that save is
  loaded double-applies. Current unit HP lives in the unit structs and is saved separately, so it is never rescaled
  by a table edit in either direction.
- **Reliable signal:** hook the `call 0x4C4BA0` at **`0x4D2C46`**. It runs exactly once per new map, after both table
  sets are final, before any unit exists, and never on a save load. `0x4C4602` is the matching "a save was just
  loaded" signal. If a single hook on the function entry `0x4C4BA0` is preferred, the word at `0x91BFB0`
  distinguishes the two (0 = new map, 1 = save).
- `0x91C6F4` (the net flag in `game.h`) is NOT yet valid at that moment; it is copied from byte `0x922F5B` at the top
  of `FUN_004c57f0`, which runs after the load. Gate table edits on `0x922F5B == 0`, otherwise a multiplayer game
  would desync.

## 4. Sight

`FUN_004c4ba0` replaces each raw sight value with a function pointer:

```c
for (p = 0x9177C0; p != 0x917608; ) { p -= 4; *p = PTR_FUN_008c1e28[*p]; }
```

`0x8C1E28` = { `0x4D3870`, `0x4D3870`, `0x4D3890`, `0x4D38B0`, `0x4D38D0`, `0x4D37D0`, `0x4D37F0`, `0x4D3810`,
`0x4D3830`, `0x4D3850` } for sight 0..9. Each is `FUN_004d38f0(x, y, owner, diameter, mask)` with diameter
3,3,5,7,9,11,13,15,17,19 (= 2*sight+1).

Readers, all live, nothing stored in the unit:

```c
// FUN_004ef350(unit): called right after a unit's tile position changes (unit+0x18 = new tile) in the order
// handlers FUN_004d9670 / FUN_004d97d0 and in FUN_004d9bb0
if (building && under construction)            fn = FUN_004d38b0;          // sight 3
else if ((type == 0x12 || type == 0x13) && SCOUTING[owner]) { FUN_004d3850(cx, cy, owner); return; }  // sight 9
else                                           fn = *(code **)(0x917608 + type*4);
fn(cx, cy, owner);

// FUN_004eea80 (every step), only when DAT_0091c594 != 0, i.e. once every 100 steps right after the fog reset
// FUN_004d39d0:   (**(code **)(&DAT_00917608 + type*4))(cx, cy, owner);
```

So a table edit shows up for an existing dragon on its next tile move, or within 100 steps if it stands still. The
fog is only revealed for human-controlled players (`controller[owner] == 0`) in single player.

Constraints: the stats panel (`FUN_004e6190`, `FUN_004e6f20`) and the save writer (`FUN_004c4c10`) find the sight
number by searching the pointer in `0x8C1E28`. A pointer that is not in that table makes the panel show 16 and makes
the save writer emit an uninitialised dword that the next load uses as an index into `0x8C1E28` (crash). So: only
table pointers, maximum sight 9. Gryphon rider (0x2A) and dragon (0x2B) are 6 in `unitdata.dat`; 6 + 2 = 8 =
`*(0x8C1E28 + 8*4)`, in range.

## 5. Hit points

Creation (`FUN_004edb10`, `_Dst` is `ushort*`, so `[0x11]` = offset 0x22):

```c
uVar4 = 1;
if (HP[type] != 0) uVar4 = HP[type];
_Dst[0x11] = uVar4;                                     // unit+0x22
*(byte *)(_Dst + 0x13) = MAGIC[type];                   // unit+0x26
...
// building placed during play (not during map load):
_Dst[0x11] = min(_Dst[0x11] / 10, 0x28);  _Dst[0x45] = maxHp - _Dst[0x11];  _Dst[0x44] = buildTime;
```

Direct readers of `0x9177C0` (16 code refs) and what they do:

| Ref | Function | Use | x2 / x4 risk |
|---|---|---|---|
| `0x4EDC58` `0x4EDE6E` | `FUN_004edb10` CreateUnit | initial HP; construction HP budget (u16 at +0x8A) | fine below 32767 |
| `0x4EE1FE` | `FUN_004ee1f0` GetMaxHp(unit), returns `short` | 22 callers, below | sign if > 32767 |
| `0x4ED15B` | `FUN_004ed150` GetMaxHp(type) | `FUN_004aca20` building upgrade done (`hp += new - old`); `FUN_004da4d0` AI repair when `hp < max*3/4`; `FUN_004cc1d0` AI force strength `hp * weight / GetMaxHp(1)` | see note |
| `0x4ED2D3` `0x4ED2EC` | `FUN_004ed2c0` type change (archer -> ranger ...) | `hp += newMax - oldMax` | fine |
| `0x4ED5A6` `0x4ED5ED` | `FUN_004ed4e0` construction progress | `hp*255/max` in 32-bit; **clamps hp to max** when done | fine |
| `0x4EEEFB` | `FUN_004eea80` | building fire state `hp < max/2`, `hp < max*3/4` | fine |
| `0x4EF913` `0x4EFE2C` `0x4F0F60` `0x4F10E0` `0x4F1196` | draw code | only `max != 0` -> draw a health bar | fine |
| `0x52D5FF` | `FUN_0052d5b0` Remastered panel | prints `"%d/%d"`, **skipped when hp or max >= 10000** | keep < 10000 |
| `0x4EF4DB` | `FUN_004ef480` | berserker regen reads `HP[0x13]` as the fixed global `0x9177E6`; increments only while `hp < max` | fine |
| `0x4C4BA1` | `FUN_004c4ba0` | not an HP read (loop bound of the sight table) | - |
| `0x4C49B0` | `FUN_004c49a0` | writes `HP[0x37] = 60` | - |

Callers of `FUN_004ee1f0`: overhead health bar `FUN_004efb00` (passes hp and max as `short` to the bar renderer
`FUN_0050e350`, and treats hp or max >= 10000 as "no numbers / special"), bar colour `FUN_004e4c90`
(`hp*100/max`, 32-bit), classic status panel `FUN_004e6940` / `FUN_004e5b00` (`"%d/%d"`, hidden at >= 10000),
Remastered panels `FUN_0052e940` / `FUN_0052ec90` / `FUN_0052d5b0`, repair `FUN_004be220` (**+4 HP per swing, clamp to
max**, 1 gold + 1 lumber every second swing), right-click-to-repair test `FUN_004dc800` (`hp < max`), AI spell filters
`FUN_004ca6b0` / `FUN_004ca7c0` (heal: `hp < max`) / `FUN_004caa80` (unholy armor: `hp <= max/2`), death-coil gain at
the end of `FUN_004e1a90` (**clamp to max**), heal `FUN_004e2220` (heals `min(mana/cost, 40, max - hp)`).

Findings:

- No arithmetic overflows at x2 or x4 for real units: the largest results are Deathwing 800 -> 3200, castle/fortress
  1600 -> 3200.
- HP is signed 16-bit in several paths (`FUN_004ee1f0` returns `short`, health bar arguments are `short`, the
  construction Bresenham in `FUN_004ed4e0` is `short`). Never let a max exceed 32767; the >= 10000 UI cut-off is the
  practical ceiling. Types to exclude from any blanket multiplier: **0x5C gold mine (25500)**, **0x65 dark portal
  (5000)**, **0x66 runestone (5000)**.
- There is no global "hp = min(hp, max)" pass. Clamps: construction complete, repair, death coil, and the
  heal/regen paths that stop at max. A unit whose hp is above its table max (possible only if a modded save is loaded
  without the mod) just keeps it; heal computes `(ushort)(max - hp)` there, which would wrap, so it could add up to 40
  more. Cosmetic.
- AI strength (`FUN_004cc1d0`) divides by the max HP of unit type 1 (grunt), so a uniform x2 cancels out and x4 heroes
  count double. Harmless.
- Repairing a x2 building costs twice the resources and time for a full repair (cost is per HP).
- Heroes cannot be recognised from the flags table: bit 0x00800000 is set only for 0x31-0x35 (Cho'gall, Lothar,
  Gul'dan, Uther, Zuljin). Use the explicit id list in `src/units.h`.

## 5a. Which table the fight reads, and which one the panel prints

Every reference below comes from a scan of `.text` for the table's absolute address.

| Stat | The fight reads | The panel prints | Same? |
|---|---|---|---|
| attack range | `0x917E40` through `GetAttackRange` `FUN_004ee660` (`0x4EE685` / `0x4EE692`), its two callers `0x4A8E65` (target search) and `0x4D9455`, AI `0x4CA763` / `0x4CAB65` | `0x917E40`: classic `0x4E5FAF`, Remastered `0x52DD6C` | **yes** |
| armor | `0x917F90`: `0x4BD79C`, `0x4BD9C4`, `0x4BDC49` | `0x917F90`: `0x4E50DA`, `0x4E514F`, `0x4E53FD`, `0x4E5A93`, Remastered `0x52BC0A`.. | **yes** |
| basic damage | `0x9180E0`: `0x4BDAB0`, `0x4BDB18` (AI `0x4CBCC3`) | `0x9180E0`: `0x4E50C3`, `0x4E51D6`, `0x4E53E6`, `0x4E5684`, `0x4E5A7C`, `0x4E6620`, Remastered `0x52D11C` | **yes** |
| piercing damage | `0x918150`: `0x4BDA59` (AI `0x4CBCCA`) | `0x918150`: `0x4E50BB`, `0x4E51CE`, `0x4E53DE`, `0x4E567C`, `0x4E5A74`, `0x4E6618`, Remastered `0x52D123` | **yes** |
| sight | `0x917608`, but `FinalizeTables` turns each entry into a reveal-function pointer at load (`0x4C4BC1`, `0x4C4C20`); the movement / reveal code uses the converted entries (`0x4EEB75`, `0x4EF3F4`, `0x4EF450`) | `0x917608` read at `0x4E61A9` | same address, different meaning after the conversion `[unverified what the panel makes of it]` |
| react range | `0x917EB0` (computer, `0x4A8EB8`; AI `0x4CC762`, `0x4CD74F`) and `0x917F20` (human, `0x4A8ECB`) | **nothing prints them** | no |

### Why a bigger `range` does not make a ship open fire sooner

`FUN_004a8e00(unit, explicit)` builds the box it searches for a target from `GetAttackRange`, but when `explicit`
is 0 it first replaces that distance:

```c
DAT_00918bd4 = FUN_004ee660(unit);                     // the attack range, 0x917E40 (+1 for an upgraded ranger)
range = DAT_00918bd4;
if (explicit == 0) {
    if (0x918CAC[unit->owner] == 1) range = 0x917EB0[type];          // a computer player: react range (computer)
    else if ((0x8C16C8[unit->order] & 0x404) == 4) range = 0x917F20[type];  // the player's, on a guard-ish order
}
// the search box is built from `range`, and the attack itself uses DAT_00918bd4
```

So `[unit.NAME] range` decides how far a unit **can shoot**, and the panel shows that number, while how far it
**notices** an enemy on its own comes from the react ranges, which the mod does not touch. Raise a juggernaught's
range to 7 and it will still wait until something is inside its unchanged react range, then fire from there: in play
that looks exactly like the setting having no effect. Giving `range` a companion write into both react tables (or a
`react_range` key) is the fix; it is not implemented yet.

## 6. Regeneration pacing and game speed

`FUN_004ef480` runs once per simulation step for every live unit (`FUN_004c4e80` / `FUN_004c5190` -> `FUN_004eea80`
-> tail call at `0x4EEF93`). The berserker block:

```
0x4ef4ae: cmp al, 0x13                      ; unit type == berserker
0x4ef4b2: movzx eax, byte ptr [esi + 0x2c]
0x4ef4b6: cmp byte ptr [eax + 0x918c9c], 0  ; owner's group-10 counter (regeneration researched)
0x4ef4bf: mov al, [esi + 0x26] / inc al / mov [esi + 0x26], al
0x4ef4c7: cmp al, 0xf0 / jb skip            ; 240 steps
0x4ef4d4: mov byte ptr [esi + 0x26], 0
0x4ef4d8: movzx edx, word ptr [0x9177e6]    ; HP[0x13], 1 if zero
0x4ef4e7: cmp di, cx / jae skip             ; unsigned: only while hp < max
0x4ef4ec: lea eax, [edi + 1] / mov [esi + 0x22], ax
```

So: counter = the unit's own mana byte `+0x26` (free because berserkers have no mana), +1 per step, 1 HP per **240
steps**. Caster mana uses the same pass with its own down-counter at `+0x74` reloaded to 0x28: 1 mana per 40 steps.
For heroes the `+0x26` byte is NOT free on casters (Khadgar, Teron, Dentarg, Turalyon, Gul'dan, Cho'gall, Uther), so a
hero regen needs its own counter.

Step timing (`FUN_004c4e80`, single-player branch): after each step `DAT_0091c6c0 += delay` and the loop returns
until `now >= DAT_0091c6c0` (at most 3 catch-up steps per call). With `0x91C178 == 1` (always, see address table):

```c
else if (DAT_0091bcd4 == 0) bVar1 = (&DAT_0083a478)[DAT_00964ca8];
else                        bVar1 = 0x1a;               // 26 ms forced while 0x91BCD4 is set (purpose [unverified])
```

| Speed setting `0x964CA8` | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| ms per step `0x83A478[n]` | 80 | 60 | 53 | 46 | 40 | 33 | 26 | 20 | 13 |
| steps per second | 12.5 | 16.7 | 18.9 | 21.7 | 25.0 | 30.3 | 38.5 | 50.0 | 76.9 |
| seconds per berserker HP (240 steps) | 19.2 | 14.4 | 12.7 | 11.0 | 9.6 | 7.9 | 6.2 | 4.8 | 3.1 |

Which menu label each setting carries is [unverified]. `FUN_004c4910(speed, flag)` is the setter and stores the value
unchanged in this build; the 7 <-> 9 level maps at `0x8C1E50` / `0x8C1E58` and the classic 3-phase delay table
`0x83A460` (60/60/60, 50/50/60, 40/50/50, 40/40/40, 30/30/40, 20/30/30, 20/20/20) are only used when `0x91C178 == 0`,
which never happens here. `DAT_008c1e68` / `DAT_008c1ed8` are command-turn length tables (steps per turn, rebuilt at
the start of `FUN_004c57f0`); they set how often `FUN_004d8d70` runs, not the step rate.

Hero regen "1 HP per second in the same style": in the existing tick hook (`0x4E89A6`, same step cadence, not called
while paused) keep one accumulator: `acc += *(uint8_t*)(0x83A478 + *(uint32_t*)0x964CA8)` (use 26 when byte
`0x91BCD4` is set); when `acc >= 1000`, subtract 1000 and give every live hero with `hp < GetMaxHp` one HP. That is
exactly one HP per real second at any speed. A fixed step count is the literal "same style" alternative (25 steps =
1 s at setting 4), but then the rate scales with game speed like berserker regen does.

## Recommended approach

**Where to apply: hook the `call 0x4C4BA0` at `0x4D2C46`** (same call-site patch style as the tick hook; the `je` at
`0x4D2C2D` targets the start of this instruction, which stays a call, so it is safe). In the thunk, if byte
`0x922F5B == 0`, edit the tables FIRST and then call the original `0x4C4BA0`:

- the sight table still holds plain integers there, so "+2" is `sight[t] = min(sight[t] + 2, 9)`;
- it covers default-data maps and maps with their own UDTA/UGRD alike, after the game's own hard-coded overrides;
- it never runs on a save load, so a save that already carries modified tables is left alone: **no double apply, no
  marker needed**; existing units in the save keep their saved HP and the saved tables match them;
- units are created after it, so map-placed units get the new max HP.

Leave `0x4C4602` unhooked (or hook it only to log). Do not apply from the tick hook "on first tick after load": that
is exactly the double-apply trap, and sight would then need pointer writes.

Edits:

| Goal | Edit | Notes |
|---|---|---|
| x2 unit HP, x4 hero HP | `u16 0x9177C0[t]`: `min(hp * k, 9999)` | k = 4 for the hero ids in `units.h`, 2 otherwise. Skip `hp == 0` rows and types 0x5C, 0x65, 0x66 (or restrict to `t < 0x3A` if buildings are not wanted). Berserker regen and every clamp read the table, so they follow automatically. |
| half unit gold + lumber | `u8 0x917980[t]`, `u8 0x9179F0[t]`: `(b + 1) / 2` | unit of 10: archer lumber 50 -> 30 (or 20 with plain `/2`); gryphon/dragon runtime 2250 -> 1130. Decide whether `t >= 0x3A` (buildings, walls 20/10) are included. Oil `0x917A60` untouched. Pay path and both UIs follow. The AI pays the same prices. |
| half specific upgrade costs | `u16 0x9188E0[i]`, `0x918948[i]`, `0x9189B0[i]`: `/ 2` | indices from the section 2 table; remember the runtime values of 4-7, 25-27, 29-31 are already the overridden ones. Spell research rows 34-51 share the tables. |
| +2 sight dragon / gryphon | `u32 0x917608[0x2B]`, `[0x2A]`: `+ 2`, clamp 9, **before** the original call | becomes `0x4D3830` (diameter 17). If ever done after finalize: `*(u32*)(base + 0x4C1E28 + 4*8)` (ASLR-relocated), never a custom function. |

Risks:

- **Multiplayer:** table edits change simulation results; gate on byte `0x922F5B` at load time (`0x91C6F4` is still
  stale there). Hard rule, same reason as the IssueOrder gate.
- **Saves persist the edit.** A save made with the mod keeps the modified tables when loaded without the mod or with a
  changed config (the old values stay until the next new map). A save made without the mod stays unmodified when
  loaded with the mod. If that second case must be covered, write a marker into a saved but unused row at apply time
  (rows 0x22, 0x24, 0x25, 0x30, 0x36 are all-zero "nothing" types; e.g. point value `0x9184A0 + 2*0x22`) and, at
  `0x4C4602`, apply only when the marker is missing - then also decide whether to scale existing units' current HP.
  [unverified that nothing sums the point-value row of an unused type; kills of type 0x22 cannot occur]
- **Alternative if saves must stay clean:** wrap the two `FUN_004e0c40` calls (`0x4E1234`, `0x4E125C`) to restore the
  pristine tables around the write and apply at both `0x4C4BA0` call sites. More hooks, and units in such a save
  exceed the unmodded max HP when loaded without the mod.
- **Restart / campaign flow:** every new map, restart included, goes through `FUN_004c4280(0, ...)` ->
  `FUN_004d2b40`, which reloads fresh data before the hook, so nothing compounds across missions.
- **HP ceiling:** signed 16-bit handling and the >= 10000 UI cut-off; clamp to 9999 and skip the three huge neutral
  types.
- **Cost granularity:** unit costs are bytes of 10 gold, max 2550; halving an odd byte rounds.
- **Refund drift:** cancel refunds are recomputed from the table at cancel time; irrelevant as long as the tables are
  only edited at load.
- **Game patch:** every address here is for PE timestamp 1771967463. Fast re-derivation: find the strings
  `rez\unitdata.dat` / `rez\upgrades.dat`, xref to the loader, read the two descriptor lists.

## Addendum 2026-09-18: the HP ceiling is 65535, not 9999

Checked after the report, because the user wanted the real engine cap:

- Capstone scan of game code 0x4A0000..0x530000 for 16-bit loads of `[reg+0x22]`: 50 `movzx`, 0 `movsx`. Every direct
  read of a unit's HP is unsigned.
- The damage function `FUN_004bd8f0(attacker, target, damage)`:
  `if (*(ushort*)(target+0x22) <= (ushort)damage) kill; else *(ushort*)(target+0x22) -= (ushort)damage;` - unsigned
  compare and subtract. (Its damage argument is a single byte, so one hit never exceeds 255.)
- `FUN_004ee1f0` (GetMaxHp) ends in `movzx edx, word [...]` and returns the zero-extended value; the `short` in the
  decompile is Ghidra's typing, not a sign extension.

So the table word and the unit word both hold 0..65535 correctly in combat. What remains above 9999 is cosmetic: the
status panels skip the "%d/%d" numbers when hp or max >= 10000, and the overhead bar renderer takes `short` arguments,
so a bar above 32767 may draw wrong [unverified, not decompiled]. The mod therefore clamps health at 65535 and only
scales unit types (< 0x3A), which also keeps the construction maths (the one real signed-16 path) out of play.
