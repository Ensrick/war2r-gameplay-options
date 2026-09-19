# Tree regrowth feasibility (static RE, Warcraft II: Remastered 1.0.2.2818)

Target: `Warcraft II.exe` 1.0.2.2818 (PE timestamp 1771967463). Static analysis only: Ghidra 12.1.3 headless (project
`war2r_b`), capstone scans of `.text`, and the loose data files under
`C:\Program Files (x86)\Warcraft II Remastered\x86\Data\Art\bgs` and `...\x86\Maps`. All VAs are at the preferred base
0x400000 (RVA = VA - 0x400000). Raw decompiles: `C:\Tools\ghidra_projects\out\tree_*.c` (remove, tables, load, region,
pud2, build, move, draw, render2) plus `tree_cv4_states.txt` and `tree_region_writes.txt`. Nothing was run in the game.
Anything inferred rather than read is marked [unverified].

## Short answers

1. **What felling changes.** Three maps and nothing else. The worker path `FUN_004eb040` (and the sapper blast
   `FUN_004eb330`) run a generic 3x3 brush walker `FUN_004e9240` with callback `FUN_004eb400`. For each of the 9 tiles
   whose region word is 0xFFFE / 0xFFFC the callback looks the tile up in a per-tileset **removal table**
   (`n?_tree.bin`) and writes the new **tile id** to `0x91AD68`. When the table entry carries `0xF000` ("tile is now
   cleared") it also clears **SQ flag 0x80** in `0x91AD58`, writes the **region id of the tile the worker stands on**
   into `0x91AD7C`, and calls `FUN_004ea7b0`, which merges neighbouring land regions by relabelling the whole map. No
   fog / explored write, no minimap call, no counter, no unit grid write. The neighbour fix-up is the same table: it is
   **one-directional** (every transition only removes forest), so it cannot be run backwards.
2. **No engine function places a forest tile.** At map load all three maps are raw copies of the PUD sections MTXM,
   SQM and REGM (flags and regions are NOT derived from tile ids; the map editor baked them), followed by one tile id
   conversion through the tileset `.cv4`. The only "place a tile and fix neighbours" routine is the wall one
   (`FUN_004eb9d0` + `FUN_004eb6e0`), and it is wall-only. The map editor's forest logic is not in the game exe.
   Regrowth has to be raw writes by the mod.
3. **Regions only ever merge.** The only writers of the region map are PUD load, savegame load, and five TILE.cpp
   functions (chop start / cancel, tree, rock and wall removal). A wall or a building placed mid-game does **not**
   touch the region map, so the engine already lives with "same region id but physically cut off". Pathfinding does not
   use regions: it reads the SQ flags live and a blocked step is retried, re-pathed and after 15 failures turned into
   STOP. A regrown tree behaves exactly like a newly built wall. It can trap a unit or split an area only if the mod
   lets it: a local 8-neighbour ring test (recipe, step A6) makes a split or a trap impossible.
4. **Harvested tiles are recognisable.** A felled tree tile gets tile id **0x7E** (= tree base 0x66 + 24, the table's
   only "removed" state) in all four tilesets. No PUD tile converts to 0x7E, so `tileId & 0x7FF == 0x7E` means "a
   forest stood here and was removed in this game". It is stored in savegames.
5. **Recipe.** Per converted tile exactly three writes: tile id (a valid tree state), `sq |= 0x80`, region word =
   `0xFFFE`. No game function has to be called. Details, preconditions and the tile id choice are in the recipe
   section. All three maps are saved and restored verbatim, so regrown trees survive save / load without the mod, and
   a save with regrown trees loads fine without the mod.
6. **Verdict: DOABLE WITH RISKS.** Mechanics are fully understood and engine-native (the states written are states
   the engine produces itself). Open points are visual (which tile art, Remastered HD mode) and behavioural checks that
   need one in-game session. List at the end.

---

## Address table

### Code (all cdecl)

| VA | What | Evidence |
|---|---|---|
| `0x4EB040` | FellTree(worker): `0x9347FC = region[worker tile]`, brush at the order tile (+0x84, +0x86), 3x3, cells `0x8C8D50`, callback `0x4EB400`, then `FUN_004e9240` | decompile; called from the harvest action `FUN_004c9a10` at `0x4C9B00` after 50 chops |
| `0x4EB330` | RemoveTreeAt(x, y): same brush, only when the region word is 0xFFFE / 0xFFFC | decompile; 12 calls from the sapper blast `FUN_004eab10` |
| `0x4EB2B0` / `0x4EA980` | RemoveRockAt(x, y) / rock callback: identical logic with region word 0xFFFD and the rock table | decompile |
| `0x4EAB10` | demolition blast: 12 rock tiles, 12 tree tiles, then walls (`FUN_004eba80`) around the unit; caller `0x4A9D7C` | decompile |
| `0x4E9240` | generic brush walker: clips the w x h rectangle to the map, calls `(*0x934774)(tileIndex)` per tile, advances the cell pointer `0x934770` by 2 per tile | decompile; also used by the fog reveal `FUN_004d38f0` |
| `0x4EB400` | tree removal callback, `bool (int tileIndex)` | decompile + disassembly below |
| `0x4EA7B0` | region fix-up around the brush position: 0xFFFA neighbours join, other land regions (`& 0x4000`, `< 0xFFFA`) are relabelled across the whole map | decompile |
| `0x4EB3B0` / `0x4EAA50` | start chopping (region word = 0xFFFC, worker +0x75 \|= 0x0A) / cancel (back to 0xFFFE) | decompile |
| `0x4EB120` | LoadTileTables (TILE.cpp): loads `n?_wall.bin`, `n?_tree.bin`, `n?_rock.bin` of the current tileset, sets the three base ids. Called at `0x4C45B7` for new maps and savegames alike | decompile |
| `0x4EA900` / `0x4EB0C0` | load one `.bin` (first two words = header, rest = table) / free the three tables | decompile |
| `0x4D2C50` | PUD pass 2: section table `0x8C45A0`, then `FUN_004c5da0(tileset .cv4)` | decompile |
| `0x4D20B0` `0x4D20E0` `0x4D2110` `0x4D21D0` | MTXM, SQM, OILM, REGM handlers: plain copies into `0x91AD68`, `0x91AD58`, (discarded), `0x91AD7C` | table dump + decompile (`FUN_004d2e30` = bounded memcpy) |
| `0x4C5DA0` | converts every PUD tile id to a tileset tile index: `cv4[(id >> 4) * 21 + (id & 15)]` | decompile |
| `0x4EB9D0` | PlaceWallTile(wallUnit): tile id, `sq \|= 0x91` plus 0x04 / 0x08, neighbour art via `FUN_004eb6e0` x5. **No region write.** Called at `0x4ED84C` when a type 0x67 / 0x68 building completes | decompile + disassembly |
| `0x4EBA80` / `0x4EB850` / `0x4EB4E0` / `0x4BD850` | wall tile destroyed / neighbour propagation / region assignment for the freed tile / wall damage (tile id bits 11-15 are the damage counter) | decompile |
| `0x4B4910` / `0x4B4F50` | mark / clear a building footprint: SQ 0x800 and the ground unit grid pointer on **every** footprint tile (type 0x64 marks 0x10 only, oil patch 0x5D nothing) | decompile; called from CreateUnit at `0x4EDEA5` |
| `0x4B4A50` | CanBuildAt(unit, x, y, type): ordinary buildings reject a footprint tile when `sq & 0x09DE != 0` | decompile (`local_c = 0x9de`) |
| `0x4B4A00` / `0x4B5000` | file / unfile a unit on its tile: SQ 0x100 (ground) or 0x200 (air) plus the grid pointer | decompile |
| `0x4EC8D0` | take the next step from the unit's path buffer; refuses when `sq[dest] & 0x8C1AC8[unit+0x2A]` | decompile |
| `0x4DA080` | blocked-step handler: retry counter in the high nibble of +0x1C, re-path (`FUN_004ec9b0`), `SetOrder(2)` after 15 | decompile |
| `0x4EC6B0` / `0x4EC9C0` / `0x4EBF50` | path refill (at most 20 steps), path search, line trace over SQ flags with mask `0x934828` | decompile |
| `0x4C6110` | AllocMaps + savegame map restore | decompile |
| `0x5156D0` | hands the renderer the tile id / visible / explored pointers and the map size, once per game (`0x4C5A1A`) | decompile |
| `0x513330` / `0x511F10` | the two frame callbacks registered in `FUN_004c57f0` (`FUN_005a17d0(FUN_00513330, FUN_00511f10)`, just before the `0x4C5A1A` call): both loop over the whole tile id map every frame; `0x511F10` also fills the pixel buffer `0x939E00` from tile ids (one RGBA texel per sampled tile, read as the minimap texture [unverified]) | decompile |

### Data

| VA | What | Type | Evidence |
|---|---|---|---|
| `0x91AD68` | **pointer** to tile ids. Low 11 bits = tileset tile index (NOT the PUD id), bits 11-15 = wall damage | `u16[size*size]`, buffer always 0x8000 bytes | `FUN_004eb400`, `FUN_004bd850`, `FUN_004c6110` |
| `0x91AD58` | **pointer** to SQ flags | `u16[]` | see flag table below |
| `0x91AD7C` | **pointer** to region words | `u16[]` | see value table below |
| `0x91AD6C` / `0x91AD70` | ground / air unit grids | `Unit*[]` | known |
| `0x91AD74` | pathfinder scratch map (direction bytes, 0xFF = unvisited), reset on every load, not saved | `u8[]` | `FUN_004c6110`, `FUN_004ebef0`, `FUN_004ec2a0` |
| `0x9347E8` | **pointer** to the loaded tree removal table | `u16[]` | `FUN_004eb120`, `FUN_004eb400` |
| `0x9347E0` / `0x9347E2` | tree table stride (10) / number of tree tile ids (header word 2 minus 1) | u16 / u16 | `FUN_004ea900` |
| `0x9347F0` | tree base tile id = `0x10 + (wall header word 2 - 1)` = **0x66** in all four tilesets | u16 | `FUN_004eb120`; file headers |
| `0x9347EC` `0x9347E4` `0x9347F4` | rock table pointer / stride / rock base id | | `FUN_004eb120`, `FUN_004ea980` |
| `0x9347D4` `0x9347DC` | wall table pointer / wall base id (constant 0x10) | | `FUN_004eb120`, `FUN_004eba80` |
| `0x934760` `0x934764` `0x934768` `0x93476C` | brush x, y, width, height (scratch, only valid inside `FUN_004e9240`) | int | `FUN_004eb040` |
| `0x934770` / `0x934774` | brush cell pointer / brush callback | `i16*` / fn | same |
| `0x9347FC` | region id given to cleared tiles | u16 | `FUN_004eb040`, `FUN_004eab10`, `FUN_004eb4e0` |
| `0x8C8D50` | brush cell numbers `1,2,3,4,5,6,7,8,9` (row-major 3x3, 5 = the felled tile) | `i16[9]` | dump |
| `0x8C1AC8` | blocking SQ mask by movement type (unit+0x2A), first four entries: land **0x09CE**, air 0x0200, 0x0903, 0x0901 | `u16[]` | dump; `FUN_004ec8d0` |
| `0x8C1F88` | tileset file names, 7 pointers per tileset (ppl, vx4, vr4, cv4, wall, tree, rock): 0 Forest, 1 Iceland, 2 Swamp, 3 XSwamp | `char*[28]` | dump |
| `0x9191C0` | current tileset index into that table | u16 | `FUN_004eb120` |
| `0x8C45A0` | PUD pass-2 section table `{char[4], handler, flag}`: MTXM, SQM, OILM, REGM, UNIT | | dump |
| `0x8C9590` `0x8C9594` `0x8C9598` `0x8C959C` | renderer's copies: tile id pointer, visible map, explored map, map size | | `FUN_005156d0` |
| unit+0x30 / +0x7E / +0x2A | path buffer (20 direction bytes, 0xFF = end) / next index / movement type | | `FUN_004ec8d0`, `FUN_004ec6b0` |

SQ flag bits seen in this work (`0x91AD58`): 0x01 land (grass 0x0001, dirt 0x0011, forest and rocks 0x0081 in every
stock PUD checked), 0x02 coast, 0x04 / 0x08 wall, 0x10 no-build (dirt; also what unit type 0x64 marks), 0x40 water,
**0x80 unpassable (forest, rocks, walls)**, 0x100 ground unit, 0x200 air unit, 0x400 AI keep-clear (from
`workers_and_gold.md`), 0x800 building footprint, 0x2000 / 0x8000 pathfinder scratch marks (`FUN_004ebf50`).

Region words (`0x91AD7C`): `0x0000 | n` water region, `0x4000 | n` land region, 0xFFFA unassigned coast, 0xFFFC tree
being chopped, 0xFFFD rocks, 0xFFFE forest. Stock PUDs carry them ready-made, e.g. `Gold Rush BNE.pud`: every tile of
groups 0x007x / 0x07xx has SQM 0x0081 and REGM 0xFFFE, land tiles use 44 different ids 0x4000..0x402B (pockets of grass
enclosed by forest have their own id).

---

## 1. What felling a tree does

Entry from the harvest action (`FUN_004c9a10`, chop counter +0x74 reached 0x32):

```c
void FUN_004eb040(int unit) {
  unit[0x75] &= 0xfd;                                        // PEON_CHOPPING off
  DAT_009347fc = region[unit.y * size + unit.x];             // region the WORKER stands in
  DAT_00934760 = (short)unit[0x84];  DAT_00934764 = (short)unit[0x86];   // the tree tile
  DAT_0093476c = 3;  DAT_00934768 = 3;
  DAT_00934770 = &DAT_008c8d50;                              // cells 1..9
  DAT_00934774 = FUN_004eb400;
  FUN_004e9240();                                            // walks the clipped 3x3, calls the callback per tile
}
```

The callback (complete):

```c
bool FUN_004eb400(int tileIndex) {
  off = tileIndex * 2;
  if (region[off] == 0xFFFE || region[off] == 0xFFFC) {
    row = (((tile[off] & 0x7ff) - treeBase) + 1) * stride;                 // 0x9347F0, 0x9347E0
    if ((table[row] & 0xf000) == 0) {                                      // 0x9347E8; tile still holds forest
      e = table[row + *brushCell];                                         // 0x934770 -> 1..9
      tile[off] = ((e & 0xfff) - 1) + treeBase;
      if ((e & 0xf000) != 0) {                                             // tile is now cleared
        sq[off] &= 0xff7f;                                                 // 0x4EB4B3  and word [edx+eax], 0xff7f
        region[off] = DAT_009347fc;                                        // 0x4EB4C3
        FUN_004ea7b0();                                                    // 0x4EB4C7  merge regions
      }
      return 1;
    }
  }
  return 0;
}
```

- Tiles of the 3x3 that are not forest (region word is not 0xFFFE / 0xFFFC) are skipped, so the brush never touches
  grass, buildings or units.
- There is **no bounds check** on `row`. A tile whose region word is 0xFFFE must hold a tile id inside
  `[treeBase, treeBase + count - 1]`, or the callback reads outside the table. This is the one hard rule for the mod.
- Neighbours can disappear too: several rows contain `0x1019` in cells other than 5 (e.g. a tile holding only its
  upper-right forest corner is cleared when the tile above it is felled).
- `FUN_004ea7b0` looks at the 3x3 around the brush position: a neighbour with region word 0xFFFA (and its own 3x3 of
  0xFFFA tiles) takes `0x9347FC`; a neighbour in a different land region (`& 0x4000`, `< 0xFFFA`) has **every tile of
  that id on the whole map** rewritten to `0x9347FC`. That is the only region maintenance the game has.
- Rendering needs no notification. `FUN_00513330` and `FUN_00511f10` (the two frame callbacks) both do
  `for every tile: idx = (tileId - 0x10) & 0x7ff; draw(table[idx])` each frame through the pointer copy at `0x8C9590`,
  and `FUN_00511f10` rebuilds the minimap pixels from the tile ids in the same pass. A changed word is simply drawn
  next frame, which is also the only way the engine's own tree removal becomes visible.
- Fog / explored maps (`0x91AD5C`, `0x91AD60`, `0x91AD64`) are not written. Harvest bookkeeping is per worker
  (+0x74, +0x75, +0x6C..+0x70); there is no "trees left" counter anywhere in these paths.

### The removal table (`n?_tree.bin`)

File = `u16 stride (10)`, `u16 rows`, then `rows * stride` words. Row `s` (1-based "state") belongs to tile id
`treeBase + s - 1`. Column 0 is the state itself (with `0x1000` set for the cleared state), columns 1..9 are the next
state for brush cell 1..9; bit `0xF000` in an entry means "cleared". Findings (scripted over the four shipped files):

| Tileset folder | rows | tree tile ids | cleared tile id | rock base |
|---|---|---|---|---|
| Forest (`nf_`) | 41 | 0x66..0x8D | **0x7E** | 0x8E |
| Iceland (`ni_`) | 36 | 0x66..0x88 | **0x7E** | 0x89 |
| Swamp (`ns_`) | 38 | 0x66..0x8A | **0x7E** | 0x8B |
| XSwamp (`nx_`) | 36 | 0x66..0x88 | **0x7E** | 0x89 |

- Rows 0..25 are byte-identical in all four files. State 24 (tile 0x7D) is solid forest (PUD tile 0x0070), state 25
  (tile 0x7E) is the only cleared state, every row >= 26 is an art variant that has exactly the transitions of one of
  the rows 1..24.
- Mapping the states back through each `.cv4` (16 tile indices + 4 corner words + 1 flags word per PUD group; the
  forest groups end in flags word `0x0081`): states 1..13 and 23 are the 14 PUD boundary types `0x07k0`, state 24 is
  `0x0070`, **states 14..22 and 25 have no PUD tile at all** (they only come into existence through harvesting).
- The table is a 4-corner automaton. Give every tile a mask (TL 1, TR 2, BL 4, BR 8) of corners that are forest;
  the tile at brush cell c loses the corners that touch the felled tile: cell 1 BR, 2 BL+BR, 3 BL, 4 TR+BR, 5 all,
  6 TL+BL, 7 TR, 8 TL+TR, 9 TL. Propagating that from "state 24 = 15" through every row reproduces the table with
  no contradiction in all four files. Independent check: bytes 32..39 of a `.cv4` record are the terrain type of the
  corners along the left, top, right and bottom edge (7 = forest); for all 14 boundary groups 0x70..0x7D in all four
  tilesets they give exactly mask k + 1 for PUD tile `0x07k0`, the same masks the automaton yields.

| mask | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| canonical state | 9 | 1 | 23 | 6 | 8 | 13 | 10 | 3 | 12 | 2 | 11 | 5 | 7 | 4 | 24 |
| PUD tile | 0700 | 0710 | 0720 | 0730 | 0740 | 0750 | 0760 | 0770 | 0780 | 0790 | 07A0 | 07B0 | 07C0 | 07D0 | 0070 |

State to mask, rows 1..25: `1:2 2:10 3:8 4:14 5:12 6:4 7:13 8:5 9:1 10:7 11:11 12:9 13:6 14:8 15:4 16:2 17:1 18:12
19:3 20:0 21:0 22:0 23:3 24:15 25:0`. States 20, 21, 22 are tree tiles with **no** forest corner left (still
harvestable, still SQ 0x80 / region 0xFFFE); 14..19 duplicate a mask of a canonical state with different art.

Every transition only removes corners. There is no row that adds one, so "does the neighbour fix-up work in both
directions": **no**. A regrowth fix-up has to be written by the mod on top of the mask table above.

## 2. Map load, savegames, and what else could place a tile

```c
// FUN_004d2c50 (PUD pass 2)
FUN_004d2ca0(&DAT_008c45a0, 5);      // MTXM 0x4D20B0, SQM 0x4D20E0, OILM 0x4D2110, REGM 0x4D21D0, UNIT 0x4D1D60
FUN_004c5da0(tilesetFiles[DAT_009191c0 * 7 + 3]);                       // "...forest.cv4"
// FUN_004d20b0 / FUN_004d21d0
if (len < 0x8001) return FUN_004d2e30(DAT_0091ad68 /* or DAT_0091ad7c */, len);   // bounded memcpy from the PUD
// FUN_004c5da0
for each tile: *p = cv4[(*p >> 4) * 0x15 + (*p & 0xf)];                 // PUD id -> tileset tile index
```

Nothing computes flags or regions from tile ids; the PUD brings them. The complete list of code that writes the tile
id map (all 18 references to `0x91AD68` checked): MTXM copy, cv4 conversion, savegame restore, tree callback, rock
callback, the wall functions. Complete list of writers of the region map (67 references, linear taint scan for 16-bit
stores, see `tree_region_writes.txt`): REGM copy, savegame restore, `0x4EA86B` + relabel loop (`FUN_004ea7b0`),
`0x4EAA34` (rock callback), `0x4EAA97` (cancel chop), `0x4EB3DB` (start chop), `0x4EB4C3` (tree callback), `0x4EB647`
+ loop (`FUN_004eb4e0`, wall destroyed). Nothing ever writes an obstacle value into it after load.

Savegame (SaveGame `FUN_004e0e10`, SFILE.cpp lines 0x232..0x255) writes, in this order, 0x8000 bytes of region words,
0x8000 of tile ids, 0x8000 of SQ flags, both unit grids (each copy passed through `FUN_004e0370`, not decompiled), and
the three 0x4000-byte fog maps. Load (`FUN_004c6110`, save handle != 0) reads them back in the same order with plain
copies; `FUN_004eb120` reloads the three tables from the tileset files first. `0x91AD74` is only `memset 0xFF`.

Wall placement for comparison (the only "add an obstacle tile" code in the game):

```c
// FUN_004ed4e0, construction finished:  0x4ED838 cmp al,0x67 / 0x4ED83C cmp al,0x68
FUN_004ee380(unit);  unit.state |= 8;  FUN_004eb9d0(unit);
// FUN_004eb9d0
tile[i] = wallBase (+0x12 for the other race);
sq[i] |= 0x91;  sq[i] |= (controller[owner] == 0) ? 4 : 8;
FUN_004eb6e0(x, y-1); FUN_004eb6e0(x-1, y); FUN_004eb6e0(x, y); FUN_004eb6e0(x+1, y); FUN_004eb6e0(x, y+1);
```

`FUN_004eb6e0` recomputes wall art from the four neighbours' wall bits (works in both directions, but only for wall
tile ids). The region word of the wall tile keeps its land region id.

## 3. Consequences of adding an obstacle mid-game

- **Pathing** is SQ-flag based and live. `FUN_004ec8d0` tests only the destination tile of the next stored step
  against `0x8C1AC8[moveType]` (land 0x09CE contains 0x80). A tree that appears on a stored path gives "blocked":
  `FUN_004da080` adds 0x1000 to +0x1C (retry count in the high nibble), sets the action timer +7 to 0xF, from count 8
  on drops the stored path (`FUN_004ec9b0`), and at count 0xF clears the nibble and issues `SetOrder(2)`. Same code
  that handles a unit or a new building in the way.
- A unit's tile changes at the **start** of a step (`FUN_004d9bb0`: `FUN_004b5000`, `+0x18 = dest`, `FUN_004b4a00`),
  so the tile it is walking into already carries SQ 0x100 and its grid pointer; the tile it is leaving is already free
  while the sprite is still partly on it.
- **Regions** are an optimistic reachability hint: different id = unreachable, same id = assumed reachable. Users:
  worker tree search (`FUN_004eaf60`), depot search (`FUN_004dab60`), the AI worker and town code (region reads at
  `0x4DA590`, `0x4DA931`, `0x4DB5CF..0x4DBD39`, `FUN_004b4d50`), build-site adjustment (`FUN_004c3b00`), the
  free-tile search predicates (`FUN_004b4880`, `FUN_004ee080`, the latter used when units leave a transport), the
  enter action `FUN_004be570`. The game never splits a region for walls or buildings, so a closed wall ring already
  produces "same id, not reachable"; the result there is the blocked-step loop above ending in STOP.
- Regrowth can recreate a forest barrier that harvesting removed. The two sides were merged into one id by
  `FUN_004ea7b0` and would stay merged, the same optimistic state as behind a wall. For this mod that matters more
  than for the base game because the idle-worker automation re-issues orders: an unreachable target would loop
  order -> STOP -> order. The ring test below removes the case entirely instead of handling it.
- Building placement (`FUN_004b4a50`, mask 0x09DE), the AI's site search and the placement cursor read the SQ flags
  live, so a regrown tile is "not buildable" immediately and a stump tile (flags 0x0001) is buildable.

## 4. Detecting candidate tiles

`(tile[i] & 0x7FF) == treeBase + 24` (0x7E), read `treeBase` from `0x9347F0`. Robust form that needs no constant:
the cleared state is the row whose column 0 has `0xF000` set. Evidence that it cannot be original terrain: no
`.cv4` record of any tileset maps a PUD tile to index 0x7E (`tree_cv4_states.txt`), and the only writer is the tree
callback. Rocks cleared by a demolition blast get a different id (0xA6 / 0xA1 / 0xA3 / 0xA1). A wall built on a stump
overwrites the id (the tile stops being a candidate); a building on a stump leaves it and the footprint flag 0x800
excludes it until the building is gone.

## 5. Recipe

All reads / writes go through the three pointers (ASLR: rebase `0x91AD68`, `0x91AD58`, `0x91AD7C`, `0x9347E8`,
`0x9347E0`, `0x9347E2`, `0x9347F0`, map size `0x918D10`), index `i = y * size + x`. Run from the existing per-step
hook (same thread and same place in the step as the engine's own unit actions; the brush scratch globals are idle
there). Single-player only, behind the existing `kRvaNetGame` gate: the writes change simulation state.

### A. Preconditions for turning a stump tile (x, y) into a tree tile

1. `x < size && y < size`.
2. `(tile[i] & 0x7FF) == treeBase + 24` (a forest stood here).
3. `(sq[i] & 0x0FFF) == 0x0001`: plain land and nothing else. This one test covers ground unit 0x100, air unit
   0x200, AI keep-clear 0x400, building 0x800, walls, water, coast, no-build. Leave bits 0x1000..0x8000 alone.
4. `groundGrid[i] == NULL` (and `airGrid[i] == NULL` if flyers should count; trees do not block movement type 1).
   Corpses and units inside buildings are not in the grids: for strictness also reject when any unit slot with
   `(state & 1) == 0` has +0x18 / +0x1A equal to (x, y).
5. No building within 3 tiles: no tile in the clipped box `[x-3, x+3] x [y-3, y+3]` has `sq & 0x0800`. Finished
   walls are tiles, not units: test `sq & 0x080C` instead if walls should keep the distance too. Recommended extra:
   no `sq & 0x0100` in the 3x3 around the tile (a unit that just left the tile is still drawn over it).
6. **Ring test (no split, no trap).** Walk the 8 neighbours in the cyclic order N, NE, E, SE, S, SW, W, NW and call
   a neighbour open when it is on the map and `(sq & 0x08CE) == 0` (land passable, units ignored). Allow the tile
   only if the open neighbours form **at most one contiguous run** in that cycle. Consecutive ring tiles are
   orthogonally adjacent, so every route that went through (x, y) can be rerouted along the run; the set of
   mutually reachable tiles cannot change, region ids stay truthful, and no unit can be enclosed. When several tiles
   are converted in one pass, evaluate them one after another with the earlier ones already counted as closed.
7. `(region[i] & 0x4000) != 0 && region[i] < 0xFFFA` (it currently is ordinary land).

### B. The three writes per converted tile

| Order | Write | Value |
|---|---|---|
| 1 | `tile[i]` (u16) | `treeBase + state - 1` with `1 <= state <= 24` (never 25, never outside the table) |
| 2 | `sq[i]` (u16) | `sq[i] \| 0x0080` |
| 3 | `region[i]` (u16) | `0xFFFE` |

Tile id first: a region word of 0xFFFE with a non-tree tile id is the one inconsistent combination (section 1). That
is everything the engine's own state holds for a tree; harvesting, the AI tree search, the demolition blast, building
placement, drawing and the minimap all pick the tile up from these words.

### C. Which state: corner painting (recommended, coherent with the engine's automaton)

The inverse of "felling clears the four corners of a tile" is "set one corner point to forest". A corner point
(vx, vy) is shared by the tiles `(vx-1, vy-1)` as its BR, `(vx, vy-1)` as BL, `(vx-1, vy)` as TR, `(vx, vy)` as TL
(tiles outside the map are ignored).

1. The point is paintable when each of those tiles is either a tree tile (region word 0xFFFE or 0xFFFC) or a stump
   tile that passes A. If any of them is anything else, skip the point: that keeps regrowth inside the set of tiles
   that once were forest.
2. For each of the tiles: `mask = maskOf[state(tile)] | corner`, new `state = canonical[mask]`, write
   `tile[i] = treeBase + state - 1`. `maskOf` for states >= 26: find the row 1..24 with identical columns 1..9.
3. Tiles that were stumps additionally get writes 2 and 3 of B. Tiles that already were trees get the tile id only
   (never touch the region word of a 0xFFFC tile: a worker is chopping it).

"Regrow tile (x, y)" = paint whichever of its four corner points are paintable. A hole inside a forest gets all
four and returns to solid forest with its neighbours' edges closed again; a tile on a harvest front gets a partial
state (same kind of tile the map editor puts on a forest edge, and like those it is fully unpassable and
harvestable). An offline simulation against the four real tables (random corner-consistent forests, random felling
through the real automaton, then painting every paintable point; 300 maps per tileset) produced only states 1..24 on
former tree tiles and never touched a tile that was not forest.

### D. Minimal single-tile variant

Skip the neighbours and write one state to the stump tile: state 24 (solid) or state 20 (a tree tile with no forest
corner, the engine's own "leftover tree"). Mechanically complete and safe; the art may not line up with the
neighbours' edges. Whether that is acceptable is a visual decision for an in-game look, not something the data can
answer.

### E. Savegames

Nothing to do. Tile ids, SQ flags and region words are saved and restored verbatim (section 2), the stump marker 0x7E
too, and all states written are engine-native, so a save made with regrown trees also loads without the mod. The mod
needs no persistent state of its own: candidates are found by scanning the tile map (16384 words at most).

### F. Suggested selftest pins

`0x4EB400` prologue `55 8B EC 8B 45 08 B9 FE FF 00 00`; `0x4EB4AE` `B9 7F FF 00 00`; `0x4EB436`
`8B 1D E8 47 93 00` (table pointer); `0x4EB450` `66 2B 0D F0 47 93 00` (tree base); at run time assert
`*(u16*)0x9347E0 == 10`, `*(u16*)0x9347F0 == 0x66`, and that row 25 column 0 of the loaded table is `0x1019`
before enabling the feature for a map.

## 6. Verdict: DOABLE WITH RISKS

Risks and side effects:

- **Wrong tile id under a 0xFFFE region word = out-of-bounds table read** in `FUN_004eb400` the next time that tile
  or a neighbour is felled. Only ever write `treeBase + 0 .. treeBase + 23`.
- **Topology.** Without the ring test a regrown tile can close a corridor: region ids stay merged (as behind walls),
  units stop after 15 blocked retries, and the mod's own worker automation could re-issue the same order forever.
- **AI.** Computer players find regrown trees through the same search, so their lumber never runs out either, and a
  regrown tile can sit where the AI wanted to build (it reads the flags live, so it just picks another site).
  Tiles flagged 0x400 are excluded by A3.
- A worker that was sent to build on a stump arrives after the tile regrew: `FUN_004b4a50` refuses and the normal
  "cannot build here" path runs. Same for a unit ordered to move onto the tile: blocked-step handling.
- Multiplayer: never (state change outside the command queue). Keep the existing gate.
- Trees appear on explored-but-fogged terrain immediately, exactly like the engine's own tree removal does.
- Balance: lumber becomes infinite; rate and distance rules are the author's design decision.

Only an in-game test can settle:

1. How the regrown tile and its neighbours look in classic graphics and in Remastered graphics (variant C vs D), and
   that the minimap follows.
2. Harvesting a regrown tile end to end: 50 chops, tile back to 0x7E, neighbours re-edged, lumber delivered.
3. A unit with a stored path across a tile that regrows: re-paths, does not freeze.
4. Save with regrown trees, load with and without the mod.
5. A computer player harvesting regrown trees, and a demolition blast on them.
6. Placement cursor over a regrown tile shows "blocked".

## Addendum: what the implementation (`src/trees.cpp`) added to the recipe

Found while writing the module and its offline tests (`test/selftest.cpp`, which fells trees with a port of
`FUN_004eb400` over the real Forest table and checks every tile afterwards):

- **Completion pass.** Painting only the four corner points of the due stump is not enough. A due neighbour stump
  that gets converted along with it receives only the corner points the two share, and it never has a turn of its own
  afterwards (it is no stump any more), so its far corners stayed clear: an even-sized pocket of stumps grew back with a
  permanent notch. Fix: right after the four paints, every stump taken along gets its other corner points painted where
  all tiles around the point are already trees (no further stump is converted in that step, so nothing cascades).
  Points next to a remaining stump are painted when that stump's own turn comes.
- **Every tile waits for its own timer.** A corner point is only paintable when every stump around it is due and
  eligible, not just the one whose turn it is. Otherwise the first due stump of a cluster would drag up to eight
  younger neighbours along early.
- **A tree that already holds the corner is not rewritten**, so art variants (states >= 26) next to regrowth keep
  their tile.
- **Occupancy rules as built (they replace A3 / A4 where they differ):** the author wanted ground units to keep
  regrowth away and flyers not to. `[trees] unit_distance` is a box like the building one; a tile in it counts when it
  carries SQ 0x100 **or** has a pointer in the first unit grid. Both are written by the same two functions
  (`FUN_004b4a00` files, `FUN_004b5000` unfiles), so either alone would do; reading both costs nothing and keeps the
  rule right if one of them is ever stale. That grid also holds buildings (`FUN_004b4910` puts the pointer on every
  footprint tile), so around a building the larger of the two distances applies. The air grid and SQ 0x200 are not
  consulted at all, not even for the tile itself: the blocking mask of flyers is 0x0200 alone (table `0x8C1AC8`), they
  cross forest tiles in the unmodded game, so a tree under a flyer changes nothing for it. The tile-itself flag test is
  therefore `(sq & 0x0DFF) == 0x0001`. Corpses are in neither grid: a unit slot that is not free / dead, is not a
  flyer type and has the tile's coordinates blocks the tile.
- **Random wait per stump.** At first sight a stump gets its first-seen play time and one random byte (the module's
  own xorshift32; the game's generator is simulation state and is not touched). Wait = min + (max - min) * byte / 255,
  computed at every check, so a config reload applies to waiting stumps and min = max is a fixed wait.
- **Load detection without a new hook.** New maps come through the existing new-map hook. A savegame loaded into the
  same buffers is caught two ways: a change of any map pointer, the table pointer, the unit array, the map size or the
  "came from a save" word; and, after every gap of more than 500 ms between two steps, a check that no stump timer sits
  on a tile that is no stump (in a running game a stump never changes by itself, and the module clears the timer of
  every tile it regrows). A stale timer can only ever make a stump regrow early, never in a wrong place: every
  precondition is evaluated at the moment of the write.
- **Validation is by content.** The module embeds rows 0..25 of the removal table and refuses the map when the loaded
  table differs, the stride is not 10, the tree base is not 0x66, or the map is larger than the 0x8000-byte buffers
  (128 x 128). Corner sets of art variant rows are resolved at map start by matching their nine transitions against
  rows 1..24.

## [unverified]

- Which PUD era / in-game tileset name each folder is (Forest, Iceland, Swamp, XSwamp = table index 0..3); only the
  index order is read from the exe.
- What the in-game-only states 14..22 show. Their corner masks come from the automaton; the art was not interpreted.
- Whether the path search allows a diagonal step between two blocked orthogonal tiles (`FUN_004ec8d0` tests only the
  destination tile; the search itself was not fully decompiled). The ring test is valid either way.
- Whether the frame callbacks run on the simulation thread. The engine's own tile writes are unsynchronised, so the
  mod's single-word writes from the step hook are equivalent in either case.
- Which of `0x513330` / `0x511F10` serves classic and which Remastered graphics, and whether Remastered mode keeps any
  terrain cache besides the per-frame tile id walk seen in both.
- SQ bit names 0x01 (land) and 0x10 (no-build) are inferred from stock PUD SQM values and the build mask 0x09DE.
- That no AI script state caches tree positions (none was found in the harvest paths; the AI script engine was not
  read).
- The region-writer list comes from a linear 60-instruction scan after each pointer load, not from full data flow.
