# Workers, gold mines, harvest / repair orders, idle detection

Target: `Warcraft II.exe` 1.0.2.2818 (PE timestamp 1771967463). Static analysis only (Ghidra 12.1.3 headless, project
`war2r_b`, plus capstone spot checks). All VAs are at preferred base 0x400000; RVA = VA - 0x400000.
Raw decompiles are in `C:\Tools\ghidra_projects\out\wrk_*.c` (harvest1, cmd1, rclick, actions, gold, trees, repair,
idle, attack, misc, dist). Anything inferred rather than read is marked `[unverified]`.

## Short answers

**A. Gold mines.** Remaining gold is `uint16` at **unit+0x82**, in units of **100 gold** (UI prints `value * 100`;
PUD loader stores `pudByte * 25`, PUD byte is "x2500"). The only decrement is in the ENTER order action
`FUN_004c9820` (order 25): when a worker finishes entering a mine (or a tanker enters an oil platform, same code) it
sets PEON_LOADED, subtracts **1** (= 100 gold) and, if the result is 0, calls `FUN_004ee6a0(mine,0,0)` which ejects
everyone inside and kills the mine. No code patch is needed: write `uint16 >= 50` to `mine+0x82` every pass for every
live unit of type **0x5C**. The computer AI reads the same field (three places, below) and keeps no cached copy, so
topped-up mines never look exhausted to it either. Alternative patch: VA `0x4C99C8`, 7 bytes `66 01 88 82 00 00 00`
(`add word [eax+0x82], cx` with cx = 0xFFFF).

**B. Harvest.** Mine: `IssueOrder(worker, 0, 0, mine, 0x4D85E0)`. Trees: `IssueOrder(worker, treeX, treeY, NULL,
0x4D85E0)` where (treeX, treeY) is the forest tile itself. A choppable forest tile is `regionMap[y*size+x] == 0xFFFE`
(`0xFFFC` = a tree some worker is chopping right now); the game's own nearest-tree search is
`int __cdecl FUN_004eaab0(Unit*, int16 xy[2])`. A loaded worker has `unit+0x75 & 0x20`; never send it a harvest order
(the handler would relabel its cargo as lumber), send `IssueOrder(worker, 0, 0, NULL, 0x4D8960)` instead.

**C. Repair.** `IssueOrder(worker, 0, 0, building, 0x4D8920)`. The handler only checks that the target type has
flag 0x20 (building) or 0x400 (transport). The player right-click path additionally requires same owner, building flag
and `hp < maxhp`. Nothing checks "construction finished" (repairing a site is the classic power-build). The repair
action needs player gold > 0 AND lumber > 0 every cycle, costs 1 gold + 1 lumber per 8 HP, and on shortage prints a
message and drops the worker to STOP.

**D. Idle.** Idle worker = `order (+0x2E) == 2` (STOP) and `nextOrder (+0x2F) == 0x3C`. There is no idle counter in
the unit struct; keep timestamps in the mod. Inside a mine / hall / construction site = `state (+0x1E) & 0x08`
(HIDDEN); those workers are also in orders 25/26/22 for which IssueOrder is a silent no-op.

**E. AI-only state.** Do not call the AI worker routines (`FUN_004dad80`, `FUN_004da8b0`, `FUN_004daa80`,
`FUN_004db210`, `FUN_004db3b0`) for human units: they bump per-player AI counters and unit AI flags that only the AI
path rebalances, mark map squares, and `FUN_004dad80` calls AI script callbacks through `0x8C5B50` / `0x8C5B10`.
The pure helpers listed in the table are safe.

---

## New addresses and offsets

### Code (all plain cdecl, caller cleans)

| VA | RVA | What | Signature / notes | Evidence |
|---|---|---|---|---|
| 0x4D85E0 | 0xD85E0 | Harvest order handler (order 23) | `void (Unit*)`, 5th arg of IssueOrder | table 0x8C1498[23]; AI calls `FUN_004ef210(u,0,0,mine,FUN_004d85e0)` at 0x4DA9A0 |
| 0x4D8960 | 0xD8960 | Return-goods handler (order 24) | `void (Unit*)`; null target = find depot itself | table[24]; `FUN_004dab60` call inside |
| 0x4D8920 | 0xD8920 | Repair handler (order 27) | `void (Unit*)`; target flags & 0x420 else falls back to MOVE | table[27]; AI call at 0x4DB04A |
| 0x4D8690 | 0xD8690 | Move handler (order 3) | what a right-click on a neutral mine actually passes; converts to harvest through `FUN_004d8090` | decompile |
| 0x4D8090 | 0xD8090 | Harvest target resolver | `int (Unit*)`; see B | decompile |
| 0x4EAAB0 | 0xEAAB0 | Find nearest reachable tree for a worker | `int (Unit*, int16 xy[2])`, 1 = found, xy filled; search radius up to mapSize*3/4 | disasm 0x4EAAB0..0x4EAB04, caller 0x4DAA99 pushes (unit, &local) |
| 0x4EAF20 | 0xEAF20 | Find tree within 15 tiles of a point | `int (int16 xy[2])` in/out | used by `FUN_004c9650`, `FUN_004d9370` |
| 0x4EAF60 | 0xEAF60 | Tree search predicate | tile is 0xFFFE and a 3x3 neighbour is in the worker's region with `sqFlags & 0x9CE == 0` | decompile |
| 0x4EB270 / 0x4EB1C0 | | tile_is_tree / tile_is_chopping_tree | `bool (int16 xy[2])`: region word == 0xFFFE / 0xFFFC | decompile |
| 0x4DAB60 | 0xDAB60 | Find nearest depot | `int (Unit* worker, Unit** out)`; completed, same owner, same region, town hall (0x1000) or, when `+0x75 & 0x40`, lumber mill (0x40000) | decompile |
| 0x4B4730 | 0xB4730 | Tile distance point to unit (building-size aware, Chebyshev) | `uint16 (int16 xy[2], Unit*)` | decompile |
| 0x4C9A10 / 0x4C9D30 / 0x4C9820 / 0x4C9BF0 / 0x4BE220 | | Order ACTIONS: harvest 23 / return 24 / enter 25 / leave 26 / repair 27 | `int (Unit*)`, run from the unit tick | action table 0x8C13A0 |
| 0x4C96A0 | | Deposit cargo into depot | adds 100, or 110/120 gold and 125 lumber/oil depending on per-player count tables (which buildings `[unverified]`), clears 0x20 | decompile |
| 0x4EE6A0 | | Destroy building (used for the empty mine) | `void (Unit*, char, char)` | call at 0x4C99D8 |
| 0x4DCC60 | | Right-click command executor (BNE DO_RIGHT_BUTTON) | `(x, y, Unit* target)` over the selection of `0x922F5A` | calls table 0x8C5D90 then `FUN_004ed8c0` |
| 0x4DC800 / 0x4DC7B0 | | Right-click decision for workers / tankers | `uint8 orderId (Unit*, x, y, Unit** target)` | table 0x8C5D90[3] / [4]; unitdata.dat right-click byte is 3 for types 2,3 and 4 for 0x1A,0x1B |
| 0x49BE20 / 0x4F2190 | | Explicit order-button command executors (order id in packet) | per-order legality checks quoted in B | decompile |
| 0x4ED8C0 | | Thin IssueOrder wrapper used by the command path | same 5 args | decompile |
| 0x4EEFC0 | | SetOrderNow(unit, order): writes +0x2E directly, +0x2F = 0x3C | | decompile |
| 0x4EEA80 | | Per-step unit pass (calls `actionTable[order]`) | | decompile |
| 0x4C99C8 | 0xC99C8 | THE gold/oil decrement instruction | `66 01 88 82 00 00 00` | disasm |

### Data

| VA | RVA | What | Type | Evidence |
|---|---|---|---|---|
| unit+0x82 | | Resources left (gold mine 0x5C, oil patch 0x5D, oil platforms flag 0x800) | uint16, x100 | `FUN_004c9820`, UI `FUN_004e5d10` `* 100`, PUD loader `FUN_004d1d60` `* 0x19` |
| unit+0x81 | | Workers currently inside the mine / platform | uint8 | inc in `FUN_004c9820`, dec in `FUN_004c9bf0` |
| unit+0x74 | | Tree chop counter (0..50) | uint8 | `FUN_004c9a10` |
| unit+0x75 | | Worker flags: 0x80 gold job, 0x40 lumber job, 0x20 LOADED, 0x10 entered, 0x08 saved location, 0x04 in town hall, 0x02 chopping | uint8 | names.h PEON_* + every function below |
| unit+0x6C / +0x6E / +0x70 | | Saved harvest x / y / mine pointer (resume point after deposit) | int16, int16, Unit* | `FUN_004c9820` tail, `FUN_004c9bf0` |
| unit+0x20 | | AI worker flags (1 gold, 2 lumber, 8/0x10 builder, 0x20 repair) | uint16 | `FUN_004db420`; AI only |
| unit+0x8D | | Remastered persistent order (5 patrol, 10 attack-move, 0x3C none) | uint8 | `FUN_004ef080`; command paths reset it to 0x3C |
| unit+0x1E | | State word: 0x01 free, 0x02 dying, 0x04 dead, 0x08 HIDDEN, 0x80 construction COMPLETE, 0x4000 order pending | uint16 | `FUN_004ed4e0` sets 0x80 on completion |
| 0x91AD7C | 0x51AD7C | **Pointer** to region map | `uint16[size*size]`, index `y*size + x`; 0xFFFE tree, 0xFFFC tree being chopped, else region id (equal ids = mutually reachable) | `FUN_004eb270`, `FUN_004eb3b0`, `FUN_004dab60` |
| 0x91AD58 | 0x51AD58 | **Pointer** to map square flags (BNE SQ_*) | `uint16[size*size]`; 0x800 building, 0x400 AI keep-clear, 0x100 unit, 0x80 unpassable, 0x40 water, 0x08/0x04 walls, 0x02 shore | `FUN_004eaf60` mask 0x9CE, `FUN_004da8b0` `|= 0x400` |
| 0x91AD68 | 0x51AD68 | **Pointer** to terrain tile ids | `uint16[size*size]`, low 11 bits index the tileset | `FUN_004eb400` (tree removal) |
| 0x91AD70 | 0x51AD70 | **Pointer** to second unit grid: the AIR layer (verified 2026-09-18, `FUN_004b4a00` picks it when `unit+0x1C & 4`) | `Unit*[size*size]` | `FUN_004da4d0` |
| 0x919128 | 0x519128 | Player gold | int32[16] | `FUN_004c96a0`, `FUN_004be220` |
| 0x9190E8 | 0x5190E8 | Player lumber | int32[16] | same |
| 0x919168 | 0x519168 | Player oil | int32[16] | same |
| 0x934848 | 0x534848 | Per-player unit list heads, next at unit+0x68; neutral list = 0x934884 | Unit*[16] | `FUN_004dab60`, `FUN_004da8b0` |
| 0x8C13A0 | 0x4C13A0 | Order ACTION table | fn[61] by order id | `FUN_004eea80` |
| 0x8C1498 | 0x4C1498 | Order HANDLER table (already known) | fn[61] | |
| 0x839BBC | 0x439BBC | IssueOrder lock table: current order 0, 1, 22, 25, 26, 36, 37, 57 = order silently ignored | uint8[61] | first line of `FUN_004ef210`; dumped |
| 0x8C1744 | | Required range by order (23/24/25/27 = 1 tile, 0xFF none, 0xFE weapon) | uint8[61] | `FUN_004d9420`; dumped |
| 0x841698 | | Required unit-type flag mask by order (23,24 = 0x300; 27 = 0x100) | uint32[61] | `FUN_004f2190`; dumped |
| 0x8C5D90 | | Right-click decision table, indexed by `0x918460[type]` | fn[7] | `FUN_004dcc60` |
| 0x91C178 | 0x51C178 | Remastered feature flag gating +0x8C..+0x97 (waypoints) `[unverified meaning]` | uint32 | `cmp dword [0x91c178],0` at 0x4DCCD7 |
| 0x9231B8 / 0x9231D8 / 0x9231F8 | | AI gold / lumber / repair worker counts | uint16[16] | `FUN_004db420`; AI only |
| 0x922F5A | | Player whose command is being executed | uint8 | `FUN_004dcc60` |

Unit type ids confirmed from `unitdata.dat` flags: peasant 0x02 / peon 0x03 are the only types with 0x100; gold mine
**0x5C** is the only type with 0x400000 (flags 0x00400020, so it also has the building bit, HP 25500); oil patch 0x5D;
oil platforms 0x56/0x57 (0x800); depots 0x4A,0x4B,0x58..0x5B (0x1000); lumber mills 0x4C,0x4D (0x40000). The alliance
table is `memset(1)` then cleared pairwise for players 0..14 only (`FUN_004d6a60`), so neutral player 15 counts as
allied with everyone.

---

## A. Gold mines: evidence

The decrement (tail of the ENTER action `FUN_004c9820`, order 25; `piVar8` = &unit->orderTarget, `pbVar1` = &unit[0x75]):

```c
iVar7 = FUN_004c96a0(param_1);                  // deposit attempt; 0 = target is not a depot
if (iVar7 == 0) {
  iVar7 = *piVar8;
  *(char *)(iVar7 + 0x81) += 1;                 // workers inside
  ...
  if (*(short *)(*piVar8 + 0x82) != 0) {
    *pbVar1 = *pbVar1 | 0x20;                   // PEON_LOADED
    FUN_004c95d0(param_1);                      // carrying sprite
    psVar2 = (short *)(*piVar8 + 0x82);
    *psVar2 = *psVar2 + -1;
    if (*psVar2 == 0) {
      FUN_004ee6a0(*piVar8,0,0);                // mine destroyed, occupants ejected
      *pbVar1 = *pbVar1 & 0xf7;
      return 0;
    }
    *pbVar1 = *pbVar1 | 8;                      // PEON_SAVED_LOCATION
    *(unit + 0x6c) = *(unit + 0x84);  *(unit + 0x70) = *piVar8;
  }
}
```

```
0x4c99a8: 66 83 b8 82 00 00 00 00   cmp word ptr [eax+0x82], 0
0x4c99b0: 74 49                     je  0x4c99fb
0x4c99c0: b9 ff ff 00 00            mov ecx, 0xffff
0x4c99c5: 83 c4 04                  add esp, 4
0x4c99c8: 66 01 88 82 00 00 00      add word ptr [eax+0x82], cx     ; <- the decrement
0x4c99cf: 8b 0b                     mov ecx, dword ptr [ebx]
0x4c99d1: 75 19                     jne 0x4c99ec                    ; ZF set -> destroy mine
0x4c99d8: e8 c3 4c 02 00            call 0x4ee6a0
```

Units: UI `FUN_004e5d10`: `FUN_004e6e50(param_1,1,0x1aa,(uint)*(ushort *)(DAT_009342ec + 0x82) * 100);`
PUD loader `FUN_004d1d60`: `if (b == 0) b = 0x78; *(ushort *)(unit + 0x82) = b * 0x19;` for types 0x5C, 0x5D and flag
0x800 (only the low byte of the PUD word is used, so map values top out at 6375 = 637,500 gold).

Complete list of accesses to `[reg+0x82]` in game code (capstone scan of .text 0x400000..0x520000):
`0x4AC199` / `0x4AC50E` unit (de)serialisation, save game `[unverified]`, `0x4C99A8` + `0x4C99C8` enter action (above), `0x4D1FB4` PUD loader,
`0x4DA90C` AI send-worker-to-gold `FUN_004da8b0` (`!= 0`), `0x4DB277` AI tanker `FUN_004db210` (`!= 0`), `0x4DB526`
AI town-hall placement `FUN_004db500` (`!= 0`), `0x4DBE4A` AI expansion site `FUN_004dbe10` (`> 0x31`, i.e. at least
5000 gold, and no town hall in the 13x13 box around the mine), `0x4E5D1D..0x4E5F4E` + `0x4E6B5C` status panel,
`0x4EDCB4` / `0x4EE4E3` copy patch <-> platform. There is no other consumer, so the AI's notion of "exhausted" is
exactly this field (a mine at 0 no longer exists as a live unit at all).

### Recipe: unlimited mines without a code patch

Every pass, for every unit slot with `type == 0x5C` and `(state & 7) == 0`:
`if (u16[+0x82] < floor) u16[+0x82] = floor;`

- Any value 1..65535 is safe: all readers are unsigned or `!= 0`, the UI multiplies in 32 bits, saves store the word.
- Use `floor >= 50` so the AI expansion test (`> 0x31`) never flips. Nicer for the UI: remember the highest value seen
  per mine (key on unit pointer + x/y) and restore to that, so "Gold left" stays at e.g. 40000 instead of jumping.
- One trip removes 1 and the worker then waits inside on an action timer of 0x96 (`unit+7`), so with `floor >= 50`
  a per-step or even per-second pass cannot miss.
- A live mine never shows 0 (it dies in the same instruction sequence), so there is nothing to resurrect.
- Side effect to decide on: mines are neutral, so this also makes the AI's mines endless. To limit it to the human,
  top up only mines referenced by a local-player worker (`worker+0x88 == mine` or `worker+0x70 == mine`).
- Same trick gives unlimited oil: units with type flag 0x800 (and patch 0x5D).

### Alternative: NOP the decrement

Rebase for ASLR and VirtualProtect first. Either NOP the 7 bytes at VA `0x4C99C8` (`66 01 88 82 00 00 00`; the
following `jne` then sees ZF=0 from `add esp,4`, so the "mine survives" branch is taken), or the smaller patch:
VA `0x4C99C1` bytes `FF FF` -> `00 00` (`mov ecx,0`; `add word [..],0` leaves ZF=0 because the value was just checked
non-zero). Both also stop oil platforms from draining because tankers share this code.

---

## B. Harvest order: evidence

### What the player's click produces

Right-click goes through `FUN_004dcc60(x, y, target)`: for each selected unit of the commanding player it calls
`decide = table_0x8C5D90[ byte_0x918460[type] ]`, gets an order id back, and then

```c
if (DAT_0091c178 != 0) *(undefined1 *)(iVar2 + 0x8d) = 0x3c;
FUN_004dfa30(iVar2,&local_10,&local_c);                       // group-move offset, no-op for a single click
FUN_004ed8c0(iVar2,local_10,local_c,local_8,*(undefined4 *)(&DAT_008c1498 + (uint)bVar1 * 4));   // = IssueOrder
```

Worker decision function `FUN_004dc800`:

```c
iVar4 = *param_4;                                            // clicked unit or NULL
if (iVar4 == 0) {
  pt = (x,y);
  if (!FUN_004eb270(&pt) && !FUN_004eb1c0(&pt)) return 3;    // not a tree tile -> MOVE
  if ((*(byte *)(param_1 + 0x75) & 0x20) == 0) { *piVar2 = 0; return 0x17; }   // HARVEST, target NULL, x,y = tree tile
} else {
  if (alliance[owner*16 + targetOwner] == 0) return 8;       // ATTACK
  if (owner == targetOwner) {
    if (!loaded) { if (type == 0x5c) return 0x17; }
    else if (townhall flag 0x1000) return 0x18;
    else if (lumber mill 0x40000 && (flags75 & 0x40)) return 0x18;
    if ((typeflags & 0x20) && *(ushort *)(target + 0x22) < FUN_004ee1f0(target)) return 0x1b;   // REPAIR
  }
}
return 3;
```

1. **Gold mine (neutral, player 15):** not same owner and "allied", so the result is order 3: effectively
   `IssueOrder(worker, x, y, mine, 0x4D8690 /*move*/)`. IssueOrder then copies the mine's x,y into +0x84 (it does that
   whenever target != NULL) and the move handler converts: `FUN_004d8090` sees a worker, not loaded, target type 0x5C,
   so `+0x74 = 0; +0x75 = (+0x75 & 0x3F) | 0x80; SetOrder(0x17)`. The Harvest button (`FUN_0049be20` /
   `FUN_004f2190`, order id 0x17 in the packet, legal only for `typeflags & 0x300` and `!(+0x75 & 0x20)`) and the AI
   (`FUN_004da8b0`: `FUN_004ef210(iVar2,0,0,iVar6,FUN_004d85e0)`) use the harvest handler directly with the same end
   state. Use that form.
2. **Trees:** target NULL, x,y = the clicked forest tile, handler = harvest. AI equivalent in `FUN_004daa80`:
   `FUN_004eaab0(unit,&pt)` then `FUN_004ef210(unit, pt.x, pt.y, 0, FUN_004d85e0)` (pushes at 0x4DAAAA..0x4DAAB8).

### What `FUN_004d8090` checks

```c
iVar1 = *(int *)(param_1 + 0x88);                                     // explicit target, else the unit standing on
if (iVar1 != 0 || (iVar1 = unitGrid[orderY * size + orderX]) != 0) {  // the order tile (0x91AD6C)
  if (worker 0x100) { if (flags75 & 0x20) return 0;  ok = (target.type == 0x5C); }
  else if (tanker 0x200) { if (flags75 & 0x20) return 0; if (!(targetflags & 0x800)) return 0; ok = same owner; }
  else return 0;
  if (ok) { *(int *)(param_1 + 0x88) = iVar1; return 1; }
}
return 0;
```

It does NOT check that the mine is alive or has gold. The harvest handler `FUN_004d85e0` then sets 0x80 (gold) when
it returned 1, otherwise clears the target and sets 0x40 (lumber). Consequence: a harvest order on a LOADED worker
always takes the lumber branch and turns carried gold into lumber; the command path blocks this, the mod must too.

### Forest tiles

`FUN_004eb270`: `return *(short *)(DAT_0091ad7c + (y * size + x) * 2) == -2;` and `FUN_004eb1c0` the same with `-4`.
`FUN_004eb3b0` (start chopping) writes 0xFFFC to the tile and sets `+0x75 |= 0x0A`, `+0x6C = tile`;
`FUN_004eaa50` (any order change) restores 0xFFFE and clears 0x0A; `FUN_004eb400` (tree felled) rewrites the terrain
tile in `0x91AD68`, clears SQ 0x80 and gives the tile the worker's region id. So the region map is the authoritative
"can be chopped" source; terrain tile ids are not needed.

The game's search predicate `FUN_004eaf60` accepts a tile when it is 0xFFFE AND at least one of its 8 neighbours is
in the worker's region with `squareFlags & 0x9CE == 0` (free, passable land). `FUN_004e3010` walks square rings
outward and stops growing once a whole ring had no tile of the worker's region. `FUN_004eaab0` only touches two
scratch globals (0x9347F8 region, 0x9341E0 ring flag), no AI player state, so calling it for a human worker is safe.

Harvest action `FUN_004c9a10`, lumber branch: walk until within 1 tile of (+0x84,+0x86); if that tile is still
0xFFFE start chopping, otherwise `FUN_004c9650` looks for another tree within 15 tiles of the worker and re-issues,
or `SetOrder(2)` when none. 50 chops (`+0x74 < 0x32`), then `+0x75 |= 0x20` and it issues return-goods itself.
Gold branch `FUN_004c9e60`: target must be alive and COMPLETE, then `SetOrder(0x19)`; dead target -> `SetOrder(2)`.
After a deposit the LEAVE action `FUN_004c9bf0` resumes harvesting by itself when `+0x75 & 0x08` (saved location):
`IssueOrder(u, +0x6C, +0x6E, +0x70, harvest)`. So one order starts an endless loop until the resource is gone.

### Carrying worker

`+0x75 & 0x20` set; `0x80|0x20` = gold, `0x40|0x20` = lumber (sprite +0x2B becomes 0x70/0x71 or 0x6E/0x6F in
`FUN_004c95d0`). Correct order: `IssueOrder(u, 0, 0, NULL, 0x4D8960)`; the handler runs `FUN_004dab60` and either
sets target + `SetOrder(0x18)` or `SetOrder(2)` when no depot exists. The command path only allows order 0x18 for
loaded workers (`FUN_0049be20`: `(+0x75 & 0x20) != 0`).

---

## C. Repair order: evidence

Handler `FUN_004d8920`: `if (target == 0 || (typeflags[target.type] & 0x420) == 0) order = 3; else order = 0x1b;`
Action `FUN_004be220` (order 27):

```c
iVar7 = *(int *)(param_1 + 0x88);
if (iVar7 != 0) {
  if (FUN_004d9e20(param_1) == 0) { FUN_004ef210(param_1,0,0,iVar7,FUN_004d8920); return 1; }   // not adjacent yet
  if (*(short *)(iVar7 + 0x22) != FUN_004ee1f0(iVar7)) {                                      // hp != max
    if (0 < lumber[owner] && 0 < gold[owner]) {
      *(short *)(iVar7 + 0x22) += 4;  clamp to max;
      sVar3 = *(short *)(iVar7 + 0x84);  *(short *)(iVar7 + 0x84) = sVar3 + 1;
      if (sVar3 + 1 < 2) return 1;
      *(short *)(iVar7 + 0x84) = sVar3 - 1;  lumber[owner] -= 1;  gold[owner] -= 1;           // every 2nd cycle
      return 1;
    }
    if (owner == localPlayer) ShowMessage("message_lumber_needed" / "message_gold_needed");
  }
}
FUN_004ef080(param_1,2);
```

- Valid target in the player path: same owner (NOT merely allied), type flag 0x20, `hp < maxhp`
  (`FUN_004ee1f0` = `0x9177C0[type]`, 1 if zero). The Repair button path only checks the worker (`typeflags & 0x100`)
  and the handler's 0x420 test.
- Construction state: a building is under construction while `state (+0x1E) & 0x80 == 0`; `FUN_004ed4e0` sets 0x80
  when it completes. Neither path tests it. Repairing a site uses the site's `+0x84`, which is its build-progress
  counter there, so it is the power-build exploit, costs resources, and should be excluded by the mod
  (require `& 0x80`).
- Resource check: gold > 0 AND lumber > 0 each cycle; about 1 gold + 1 lumber per 8 HP. On failure the worker goes to
  STOP, which an auto-repair loop would see as idle and re-send, spamming the message. Gate on both counters.
- AI parity (`FUN_004da4d0`): own, alive, complete, not an oil platform, same region as the worker,
  `hp < 3/4 max`, no enemy attacker within 3 tiles (ground grid 0x91AD6C or second grid 0x91AD70), best
  `0x9183F0[type]` priority; when gold < 200 and lumber < 100 only town halls.

---

## D. Idle detection: evidence

- Every job end / failure lands in STOP: `FUN_004c9bf0` `FUN_004eefc0(u,2)` on leaving a building, `FUN_004ed4e0`
  writes `+0x2E = 0x3C02` for the builder ejected from a finished site, `FUN_004c9650` / `FUN_004c9e60` /
  `FUN_004be220` / `FUN_004d8960` call `SetOrder(2)` when there is no tree, mine, damage or depot.
- The game's own idle hook confirms it: the STOP action `FUN_004a98d0` -> `FUN_004a8a10` runs the AI worker manager
  `FUN_004dad80` only when `controller[owner] == 1`; for humans STOP does nothing for workers.
- STAND (13) shares the action function but is only entered by the explicit Stand Ground command
  (handler 0x4D89E0); treat it as "player said stay", not idle.
- No idle counter: +0x07 is the animation/action timer (150 inside a mine, random 0x2C..0x3B AI think delay),
  +0x7A a retarget delay, +0x51 / +0x8E small cooldowns. Keep `firstSeenIdleTick[unitIndex]` in the mod and reset it
  whenever the unit is not idle or the slot's type/owner changes.
- Must skip: `state & 0x0F != 0` (0x08 = inside mine, depot or construction site; the unit pass reveals the map
  around the mine for human workers with `state & 8` and target type 0x5C, which confirms the encoding), and current
  order in {22, 25, 26} where IssueOrder is a no-op anyway (lock table 0x839BBC). `+0x75 & 0x10` = entered.
- Workers under attack flee by themselves (`FUN_004a8bb0`, reads `unit+0x54`) when in STOP / lumber harvest /
  repair: a just-fled worker sits in MOVE then STOP, so a short idle delay (1 to 2 s) avoids fighting that.

---

## E. AI-only state touched in these paths

| Function | Touches | Verdict for human units |
|---|---|---|
| `FUN_004da8b0` (AI to gold) | `unit+0x20 |= 1`, `0x9231B8[owner]++`, marks squares 0x400 between hall and mine | do not call |
| `FUN_004daa80` (AI to lumber) | `unit+0x20 |= 2`, `0x9231D8[owner]++` | do not call |
| `FUN_004dad80` (AI worker manager) | calls `(*0x8C5B50)(owner)` / `(*0x8C5B10)(owner)` AI script callbacks, AI build counters `0x923218[owner*0x2F + n]`, `0x9237F8[owner]` | never call; per-player AI script state is not set up for humans `[unverified]` |
| `FUN_004db420` | the only place those counters are decremented, reached from the AI manager only | so any increment for a human leaks |
| IssueOrder, the three handlers, `FUN_004dab60`, `FUN_004eaab0`, `FUN_004eaf20`, `FUN_004eb270`, `FUN_004b4730` | unit fields, region map read, scratch globals | safe |

---

## Recommended implementation plan

1. `game.h`: add RVAs 0xD85E0 harvest, 0xD8960 return, 0xD8920 repair handlers; 0xEAAB0 FindTree; 0xDAB60 FindDepot
   (optional); data 0x51AD7C region map ptr, 0x519128 gold, 0x5190E8 lumber; offsets +0x82 resources, +0x75 worker
   flags, +0x70 saved mine; type 0x5C; type flags 0x100 worker, 0x1000 hall, 0x40000 mill, 0x400000 mine;
   state 0x80 complete, 0x08 hidden. Extend `selftest` with byte checks: `0x4C99C8` = `66 01 88 82 00 00 00`,
   table `0x8C1498[23,24,27]` = the three handlers, `0x4EAAB0` prologue `55 8B EC 8B 45 08`.
2. Mine refill pass (cheap, every tick or every N ticks): loop unit slots, type 0x5C, `(state & 7) == 0`, restore.
3. Worker pass (local player, single-player gate as today): candidate = type flag 0x100, owner == local,
   `(state & 0x0F) == 0`, `order == 2 && nextOrder == 0x3C`, idle for >= configured delay. Then in priority order:
   loaded -> return goods; repair target available and gold > 0 and lumber > 0 -> repair; mine -> harvest gold;
   tree -> harvest lumber. Before each IssueOrder mirror the command path: `if (*(uint32*)0x91C178) unit[0x8D] = 0x3C`.
4. Mine choice: alive 0x5C, `+0x82 != 0`, `region[mine] == region[worker]`, smallest Chebyshev distance (AI weighs
   `dist(depot, mine) + dist(mine, worker)/2`). Also require a depot to exist in that region or the worker will mine
   once and then stop loaded.
5. Tree choice: call `FUN_004eaab0(worker, xy)`; if it returns 0 there is no reachable tree. Optional own scan:
   region word 0xFFFE plus a free neighbour in the worker's region.

### Risks

- IssueOrder is silently ignored for locked orders (0, 1, 22, 25, 26, 36, 37, 57): verify with `EffectiveOrder`
  after the call if the mod counts on it.
- Harvest on a loaded worker converts gold to lumber (handler clears 0xC0, sets 0x40, keeps 0x20). Always branch on
  `+0x75 & 0x20` first.
- Repair with an empty treasury loops STOP -> re-issue -> on-screen message. Gate on gold and lumber > 0 and add a
  per-worker retry backoff.
- Unreachable mine (different region id) makes the harvest action re-issue the walk forever; keep the region test.
- A gold mine owned by a real player (custom PUD) is legal in the engine (`FUN_004dc800` same-owner branch);
  scan the unit array rather than only the neutral list 0x934884.
- Patching 0x4C99C8 needs ASLR rebasing, page protection changes and also freezes oil; the top-up needs none of that.
- Everything here bypasses the network command queue, same as the existing autocast: keep the `kRvaNetGame` gate.
- `[unverified]`: meaning of 0x91C178, the air-layer reading of 0x91AD70, and that the AI script callbacks fault for
  humans (they were not decompiled; the mod has no reason to call them).
