# Dodging Blizzard and Death and Decay (static RE, Warcraft II: Remastered 1.0.2.2818)

The author (2026-09-25): "My units naturally wander into blizzards, if you could have them avoid them, and move out of
blizzards and death and decay, that'd be great." Built as `[dodge]` (`src/dodge.cpp`), off by default.

## Where an area is known from

Both spells put one missile record per impact point in the missile pool (`0x91C700`, `0x40` bytes a slot, slot count
`0x91BFBC`, bit 0 of +0x35 = free). What the records hold (disassembly in `autocast_all_spells.md` 2.5 / 2.6 / 2.6b):

| Spell | Missile type (+0x34) | Aim tile | Impact point | Lifetime |
|---|---|---|---|---|
| Blizzard | 5 (`FUN_004aec70`, `0x4AECA8`) | the caster's order tile +0x84 / +0x86 while its order is still 0x2F; the caster is +0x30 | +0x28 / +0x2A: the pixel the shard falls on (`0x4AEC92`, `0x4AECBD`), one of the 5x5 tile centres around the aim | a chain respawns on the same point 11 times (`FUN_004ae990`) |
| Death and decay | 6 (`FUN_004af500`, word 0x206 at `0x4AF543`) | +0x28 / +0x2A = aim tile * 32 + 16 (`FUN_004af7b0` for a positional order, `0x4AF7F0..0x4AF815`) | +0x00 / +0x02 (aim + `rand()%5 * 32 - 64`) | 10 pulses, then the slot is freed (`FUN_004aeb30`) |

So an area is known from the first missile of the first wave, for both spells, whoever cast it:
- Death and decay: every live type-6 record gives the aim directly: the 5x5 around it, plus one tile for the quarter
  ring (a unit on the tile next to an impact tile is 32 px from it: a quarter hit, `FUN_004afb50`), is dangerous.
- Blizzard while the caster is still on the channel (its order is 0x2F): the same box around the caster's order tile.
- Blizzard after the channel ended (caster stopped, died or was re-ordered): only the chains still falling, each on
  its own impact point plus the ring (3x3).
An area is forgotten when its last missile slot is freed. Whirlwind is not handled (it wanders; not asked for).

## Rules as built

Every 250 ms of play, single player only (the tick returns before any module in a network game), the local player's
units only:
- Exempt: buildings, a unit busy with a spell (order 0x26 and up: above all the channelling caster, whom its own
  blizzard never hurts, `FUN_004afb50` spares the source), a unit under Unholy Armor (immune, `FUN_004bd8f0`). Workers
  are NOT exempt: a harvesting peasant in a blizzard steps out too; harvest / repair / build orders are not given back
  (the unit is left idle; `[workers] auto_harvest` picks it up if on).
- Dodge: a unit on a tile inside an area (flyers too) is moved to the nearest tile outside every area by one more
  tile, the one most straight away from the nearest aim (step dotted with aim-to-unit), then nearest to where it wants to be (its attack target or
  attack-move / patrol destination). A ground unit only picks ground it can stand on (the tile's water bit matches its
  own tile's, no forest, rock, wall or building bits, `kRvaSquareFlags`); a flyer any tile.
- Player's orders win: a unit on a move the player gave is never touched (walking through is his call). While the mod
  has a unit, any order that is not the mod's own move, its hold or idleness is the player's, and the mod lets go
  (the scouts' rule).
- Avoid (the cheapest safe rule): a unit outside every area that would walk into one by itself - its order target, or
  the enemy it picked up by itself (+0x54, written by the acquisition code at `0x4A92C5`), stands inside an area out
  of its attack range, or its attack-move / patrol's next 2 tiles toward the destination lie in one - gets Stand
  Ground (handler `0x4D89E0`, order 13): it stops at the edge and still shoots whatever comes in range. Ranged units
  already in range keep firing.
- Given back: once the way back has been clear of every area (with the extra tile) for a full second: the attack on
  the same target (handler `0x4D8250` with the target, order 9), the attack-move (`0x4D82E0`) or patrol (`0x4D8860`)
  to the same destination, or a walk back to the old spot (and Stand Ground again if it was standing).
- Oscillation guard: in = inside the box, safe = one tile beyond it; a give-back needs 1 s clear; a unit on its way
  out is not re-sent; a saved order older than 60 s is dropped.
- The player's own Blizzard pushes his units out too, which is what he asked for. It does not interfere with
  `[priority] hold_for_blocked_area`: that waits BEFORE the cast for units within `area_friendly_clearance` of the aim;
  once a blizzard falls, dodge clears the rest out.

All orders go through `game::IssueOrder` (resume byte rule). `resume.cpp` does not fit (it only restores to idle
casters and drops a record as soon as the unit moves); dodge keeps its own record per unit slot, checked by serial.
