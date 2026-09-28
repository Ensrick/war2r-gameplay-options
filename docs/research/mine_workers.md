# Mine crews: who is mining which gold mine (static RE, Warcraft II: Remastered 1.0.2.2818)

Evidence for `src/mineworkers.cpp` ([workers] mine_workers). Static analysis only (capstone over a copy of the exe),
building on `docs/research/workers_and_gold.md`. VA = RVA + 0x400000.

## The author's rule

Town halls within 12 tiles of a gold mine keep workers mining it: 3 while the mine holds 10000 gold or more, 2 from
5000, 1 below that. The hall trains workers until that many are mining, and idle workers go there.

## Gold left

`mine+0x82`, uint16, in hundreds of gold (workers_and_gold.md section A: the UI prints `value * 100`, the PUD loader
stores `byte * 25`, the only decrement is `0x4C99C8`, one per trip). A mine that reaches 0 is destroyed in the same
step (`0x4C99D8 call 0x4EE6A0`), so a live mine never shows 0. The thresholds compare `field * 100` with the config
numbers: 10000 gold = 100, 5000 = 50.

## Which mine a worker is mining

The order action table `0x8C1498` / `0x8C13A0` gives the four orders of the round trip: 23 harvest `0x4C9A10`,
24 return goods `0x4C9D30`, 25 enter `0x4C9820`, 26 leave `0x4C9BF0`.

| Stage | Order | Where the mine is |
|---|---|---|
| Walking to the mine | 23 harvest | `+0x88` = the mine (the harvest handler `0x4D85E0` keeps the target only when the resolver `0x4D8090` found a type 0x5C) |
| Inside the mine (hidden, state 0x08) | 25 enter | `+0x88` = the mine |
| Leaving it loaded, walking to the hall | 24 return goods | `+0x70` = the mine, `+0x75` bit 0x08 set |
| Inside the hall, stepping out | 25 / 26 | `+0x70`, bit 0x08, gold job bit 0x80 |

The enter action saves the mine when a worker comes out loaded:

```
0x4C99CF  mov ecx, dword [ebx]          ; the order target: the mine
0x4C99EC  mov eax, dword [edi + 0x84]
0x4C99F2  or  byte [esi], 8             ; esi = &worker[0x75]
0x4C99F5  mov dword [edi + 0x6C], eax   ; the tile to come back to
0x4C99F8  mov dword [edi + 0x70], ecx   ; the mine
```

The leave action clears the bit and re-issues the harvest to the saved mine
(`0x4C9CED and al, 0xF7`, `0x4C9CF4 push dword [esi + 0x70]`, `0x4C9D05 call 0x4EF210`), so the next stage is a
harvest order on the mine again. The harvest handler keeps bit 0x08 (`0x4D85FD and byte [esi+0x75], 0x3F`) but clears
the gold bit 0x80 when the worker is sent to a tree, so a stale `+0x70` alone does not make a miner: the mod also
requires the gold bit and one of the orders 24 / 25 / 26. A worker moved elsewhere (order 3) never counts.

The mod reads the order through `EffectiveOrder`, so a worker it has just sent (harvest in the next-order slot)
counts at once and a second idle worker is not sent for the same place.

## Halls

The six depots 0x4A / 0x4B (town hall / great hall), 0x58 / 0x59 (keep / stronghold), 0x5A / 0x5B (castle /
fortress): type flag 0x1000 (workers_and_gold.md, the depot finder `0x4DAB60`), the same six auto-production trains
workers at. Only finished ones (`state & 0x80`) count.

## Distance and reachability

"Within 12 squares" is the number of free tiles between the hall's and the mine's footprints (Chebyshev), sizes from
the unit-size table `0x917AD0` (uninitialised in the exe file, filled at run time, so the sizes in play are used). A mine belongs to the nearest hall of the local player only, so
a mine between two halls is counted once. The hall must reach the mine by land: the hall's region id (the region map
`0x91AD7C` at its top-left tile, as the farm feature uses) must appear on the mine's footprint or the ring around it,
where the miners stand.

## What the mod does

- `production.cpp` step 1b: an idle, unselected hall whose mines have fewer miners than they want, minus the idle
  workers within the radius of the hall (they are about to be sent), trains one worker through the same
  StartProduction path as step 1, with the same class switch, food and money rule, and without the count target. It
  runs inside the production pass, so the "all enemies defeated" guard (docs/research/victory.md) covers it.
- `workers.cpp`: a worker of the local player idle for 2 s, not carrying, within the radius of such a hall and in its
  region, gets `IssueOrder(worker, 0, 0, mine, 0x4D85E0)` on the nearest short mine, before the plain auto-harvest.

## Caveats

- Training needs `[auto_production]` on. The assignment works without it.
- A player-owned gold mine (custom maps) is handled like a neutral one.
- The count does not see a worker the player has told to wait inside the hall with its cargo; that worker is back on
  a harvest order within one trip.
