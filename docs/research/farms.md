# How the game places and orders a farm (static RE, Warcraft II: Remastered 1.0.2.2818)

Evidence for `[farms] auto_build` (src/farms.cpp). Static analysis only (Ghidra headless, capstone), plus selftest runs
of the game's own placement code on the mapped exe image. VA = RVA + 0x400000.

## Summary

- The computer builds a farm from its worker manager `FUN_004DAD80` (food branch, `0x4DAF80..0x4DAFE6`):
  cost check `FUN_004AC610`, site search `FUN_004DBC30`, then `+0x80 = site`, `+0x7F = type`,
  `IssueOrder(worker, site, NULL, FUN_004D8470)`, and it **pays at once** (`FUN_004AD220`), then does AI bookkeeping
  (`+0x20 |= 8`, `+0x76`, the builder table `0x923218`, keep-clear marks `FUN_004DA7C0`).
- The player's build click (`FUN_004DC2C0`) does the same without the bookkeeping and without paying: placement test
  `FUN_004DC210`, cost check `FUN_004AC610`, walk-to tile `FUN_004C3A20`, `+0x7F`, `+0x80`, `IssueOrder` with
  `FUN_004D8470`, acknowledgement sound `FUN_004C75E0`.
- The build ACTION `FUN_004BDDC0` (action table `0x8C13A0[28]`, pointer at `0x8C1410`) runs when the worker arrives:
  it tests the site again (`FUN_004DC210`) and the price (`FUN_004AC610`), creates the building (`FUN_004EDB10`), and
  for a player whose controller is not 1 (a human) pays the price then (`0x4BDF67 call FUN_004AD220`). If the money is
  gone by then, the local player gets the game's message and the worker stops (order 2).
- **The site search is safe to call for a human's peasant.** It reads no per-player AI state: only the per-player
  unit list `0x934848` (kept for every player by `FUN_004ED030`), the region, square-flag, explored and size tables,
  and it writes only scratch globals (`0x923808..0x92381C`, and the placement preview cells `0x923820..0x92382F`,
  which the computer's own searches overwrite all the time).
- The mod first **called the computer's search**; it now **chooses the site itself** (next section) from
  the same candidates, because the computer's first ring site can sit between the hall and a gold mine. It then
  **does what the player's click does**. It never pays (the build action does that) and never touches the AI
  bookkeeping.

## The site search `FUN_004DBC30(Unit* worker, int16 out[2], uint32 type)`, cdecl, returns nonzero if found

`jmp [0x8C5BA0 + type * 4]`. Farm and pig farm (0x3A, 0x3B) go to `FUN_004DB6D0`:

1. `FUN_004DBC50(worker)`: the worker owner's nearest unit with type flag 0x1000 (town hall family) whose region
   (`0x91AD7C`) is the worker's own (`FUN_004DB1C0`). No hall means the search centres on the worker.
2. `FUN_004DBCE0(worker, out, centre, type, 2)`: rings around the centre, stepping 2 tiles (`FUN_004DB970`, ring
   direction tables `0x8C5F34` / `0x8C5F3C`). A tile is taken when:
   - its region is the worker's (`0x923818`);
   - no square of the footprint (`0x917AD0` size, plus a 1-tile margin only when `0x923808` is set, which happens
     only inside the computer's first build pass) has any of the square-flag bits `0x0DDE`. Those are building,
     AI keep-clear 0x400, ground unit, unpassable, water, walls and shore, and `0x0D11` for coast buildings;
   - `FUN_004B4A50(worker, x, y, type) == 0`. This is the same test the player's placement uses. For the local
     player it also refuses unexplored tiles (explored map `0x91AD60` == 0x10) and ignores units the player cannot
     see (bits at `0x91AD64`).

Selftest (`FarmTests`): with a 4x4 hall at 20,20 on an open, explored map, the real search returns **18,18**, the
2x2 spot touching the hall's corner. The real `FUN_004DC210` accepts it, and `FUN_004C3A20` makes the peasant walk to
19,19.

## The build order, as the mod issues it

1. `FUN_004DBC30(seed, site, farm)`. The site must lie on the map with its full footprint, and
   `FUN_004DC210(builder, site, farm)` must return 0 (the player's own placement test, which also clears and restores
   the worker's own 0x100 square).
2. `walkTo = site; FUN_004C3A20(builder, walkTo, farm)`.
3. `builder+0x7F = farm; builder+0x80 = site` (int16 x, int16 y).
4. `game::IssueOrder(builder, walkTo, NULL, 0x4D8470)`. That clears the resume byte and refuses off-map tiles. The
   handler copies +0x80 to +0x84 and sets order 28.

The farm type is 0x3A for a peasant (type 2) and 0x3B for a peon (type 3): `0x3A | (worker type & 1)`.

## Other inputs

| Item | Address | Use |
|---|---|---|
| Farm button condition | `FUN_004E4330` | `(0x918D47 & (1 << local player)) && (0x919210[player] & 0x10000)` |
| 0x918D47 | `FUN_004EE210` | 0xFF unless a network game sets it from `0x922F5B` |
| Price | `0x917980` / `0x9179F0` / `0x917A60` | gold / lumber / oil per type, x10 (what `FUN_004AC610` reads) |
| Food | `0x91B50C` supply, `0x91B38C` counted, `0x91B6AC` food-free, `0x9193F0` in training | as auto-production reads them |

## The mod's own site choice (src/farms.cpp ChooseSite)

The author does not want farms in the path of the gold mines, and the computer's first ring site is often exactly
there: with a mine up-left of a hall at 20,20, the game's own search returns 18,18, the tile against the hall's corner
facing the mine (selftest 9a). The rule:

1. Centre: the worker's nearest complete town hall (type flag 0x1000) in its own region, as `FUN_004DBC50` picks it.
   No such hall: no farm.
2. Candidates: every top-left tile on the step-2 lattice of the hall's corner (the computer's farm step,
   `FUN_004DB6D0` -> `FUN_004DBCE0(.., 2)`), within 16 tiles of the hall, footprint on the map, anchor tile in the
   worker's region.
3. Rejected when the footprint has `mine_clearance` or fewer tiles of gap to any live gold mine (gap 1 = touching).
   With the default of 3, that means at least 3 free tiles between the farm and the mine.
4. Rejected when any tile within 2 of the footprint lies in the band between the hall and a gold mine at most 12 tiles
   from it. The band is the convex hull of both footprints (tile edges), minus the hall's own tiles, because nobody
   walks through the hall and a farm against its far side is fine.
5. Rank: 0 = touches one of the player's buildings (gap 1) and lies on the side away from the mines (centre offset
   from the hall's centre has a dot product of 0 or less with the sum of hall-to-mine vectors), 1 = away only,
   2 = touching only, 3 = the rest. Within a rank, the smallest gap to the hall wins, then search order.
6. The winner must pass the player's placement test `FUN_004DC210` (the computer's own search is no longer called).

No candidate: nothing is built, and `farm: food is low but ...` is logged at most once a minute of play.

## Which workers (`[farms] workers`)

`+0x75` job bits (docs/research/workers_and_gold.md): 0x80 gold, 0x40 lumber, 0x20 carrying, 0x02 chopping. Idle
(`STOP` with nothing queued) always comes first. `idle_then_lumber` then allows a harvester (order 23) with 0x40 set
and none of 0x80 / 0x20 / 0x02: a wood cutter walking back to the trees empty-handed, never a gold miner. `idle_only`
allows no harvester. `any` allows every harvester carrying nothing (the first version's behaviour).

## Interplay with auto-production

The trigger is free food (supply up to 200, minus units counted, minus food-free units, minus units in training) at or
below the HIGHER of `free_min` and `free_percent` % of the supply, rounded up. That is the author's choice, and the
same rule `[auto_production]` uses for the food it keeps free (`production.cpp` `FoodAllows`). With equal numbers,
auto-production stops training at exactly the point where the farm is started. With smaller `[farms]` numbers,
auto-production stops first and only the player's own training calls for a farm. The two never share a worker
(auto-production gives no worker orders), but they spend the same bank. If gold or lumber is gone when the peasant
arrives, the build action refuses, the peasant stops, and the next farm pass (once a second) tries again.
