# Autocast for the remaining spells (static RE, Warcraft II: Remastered 1.0.2.2818)

Read-only analysis of `Warcraft II.exe` (PE timestamp 1771967463, installed at
`C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe`), Ghidra project clone `war2r_b` plus capstone
disassembly of the same file. All VAs are at the preferred base 0x400000 (subtract 0x400000 for the RVAs in
`src/game.h`). The decompile dumps behind every snippet are the `ac_*.c` files in the Ghidra output folder (not in the
repo). Anything not read directly out of the exe or the data files is marked [unverified]; [inferred] marks a
conclusion drawn from code that was read but not executed.

Covered: fireball, flame shield, invisibility, blizzard, holy vision, death and decay, whirlwind, runes. Heal,
exorcism, slow, polymorph, bloodlust, death coil, haste, unholy armor, raise dead (`src/autocast.cpp`) and Eye of
Kilrogg (`src/eye.cpp`) are already automated.

## Short answers

1. **No other spell exists.** The UI button records (24 bytes, byte +16 = ALOW bit, byte +17 = order) list exactly 18
   castable spells; order 0x28 has a handler-table entry but a NULL hit-frame action and no button. The 8 missing
   ones: holy vision 0x26, flame shield 0x2A, fireball 0x2B, invisibility 0x2D, blizzard 0x2F, whirlwind 0x34,
   runes 0x37, death and decay 0x38 (table in section 1).
2. **The spell order handler `0x4E2970` checks nothing.** It sets the order and, for 12 orders, clears the target
   pointer. The real checks happen later: the per-order step action (`0x8C13A0`, walks into range, target alive,
   "not a building" or "fleshy"), and the hit-frame action (`0x8C1590`, mana, target state). Positional spells
   (fireball, blizzard, whirlwind, runes, death and decay, holy vision) are always cast at a TILE: `IssueOrder` copies
   a given target's tile into +0x84/+0x86 and the handler then drops the target. Flame shield and invisibility are
   UNIT spells.
3. **Friendly fire: yes, for every area spell.** Fireball, flame shield, blizzard, death and decay and whirlwind all
   damage through `FUN_004af9e0` -> `FUN_004afb50`, which has no owner or alliance test: every unit (ground AND air
   grid) and every building whose centre is within 42 px (Chebyshev) of an impact is hit, own and allied included.
   The only exemptions: the missile's source unit (the caster; for flame shield the shielded unit) and units under
   unholy armor (`unit+0x46 != 0`, immune to all damage). Walls (no owner) are damaged too. Runes have no owner at all:
   a rune hits the first ground / sea unit that is filed on its tile, whoever owns it, the caster included.
4. **Blizzard and death and decay are channelled.** Their hit-frame actions never set the order back to stop: each
   cast-animation cycle costs 25 / 30 mana and drops a new wave, until mana < cost or the caster gets another order.
   Left alone they burn ALL the caster's mana, even after every enemy has left.
5. **The computer AI casts all eight** (dispatcher `FUN_004ca4a0`, every 0x32 steps). Area spells: the NEAREST enemy on
   the whole map of a given category, only when no unit allied to the caster stands within 3 tiles (blizzard, death
   and decay) / 4 tiles (whirlwind) of it, ground grid only, and mana >= 3 x cost for the channels. Fireball: first
   enemy in a 31x31 scan with no caster-allied unit within 3 tiles (skipped when the caster is below half HP). Flame
   shield: an own melee fighter with nobody friendly within 2 tiles, or an enemy standing next to its own friends.
   Invisibility: allied casters / ranged units while the mage is on an AI attack mission. Runes and holy vision only
   through a "10 random unit slots in a row" helper that rarely fires [frequency unverified].
6. **Crash / corruption:** no new class. Every hit-frame action bounds-checks the tiles it touches (holy vision points,
   rune plus shape, splash windows); the existing `game::IssueOrder` off-map guard covers the walk-into-range code.
   None of the eight spells creates a unit. Pools: missiles 400 slots game-wide (`0x91BFBC`), runes 50 slots
   game-wide; a full pool silently drops the effect after the mana is already spent (runes refund).
7. **Holy vision moves the local player's camera.** For the local player's caster the action calls `FUN_004afd70` for
   each of its 7 reveal points, which rewrites the viewport origin `0x91AD80` / `0x91AD84` [inferred from the clamp
   against the map size in pixels]. An automatic holy vision yanks the view across the map: treat as a blocker.

## Address table

All new unless noted. Order tables are indexed by the order id (unit +0x2E).

| VA | What | Evidence |
|---|---|---|
| `0x8C1590` | hit-frame action table `void(*)(Unit*)` (already in `eye_of_kilrogg.md`). 0x26 `0x4E2720`, 0x2A `0x4E2110`, 0x2B `0x4E20C0`, 0x2D `0x4E22F0`, 0x2F `0x4E19A0`, 0x34 `0x4E27F0`, 0x37 `0x4E25A0`, 0x38 `0x4E2530`; 0x28 = NULL | table dump; dispatcher `FUN_004a8970` |
| `0x8C13A0` | per-order step action. 0x26 0x2B 0x2F 0x34 0x37 0x38 -> `0x4E2840` (positional); 0x2A 0x2D -> `0x4E2900` (unit, not a building) | table dump + decompile |
| `0x8C1744` | order range (tiles, 0xFF = always): 0x26 0xFF, 0x2A 6, 0x2B 10, 0x2D 6, 0x2F 10, 0x34 12, 0x37 10, 0x38 12 | dump; `FUN_004d9420` |
| `0x8C1804` | "clear target" byte read by `0x4E2970`: 1 for 0x26 0x2B 0x2F 0x34 0x37 0x38, 0 for 0x2A 0x2D | dump |
| `0x8C16C8` | order flag words; every spell 0x000C | dump |
| `0x8C1498` | order handler table (already in `workers_and_gold.md`): 0x26..0x38 all `0x4E2970`; entry 2 = stop handler `0x4D8580` | dump |
| `0x4D8580` | stop handler: `SetOrder(unit, 2)` (0x20 for a transport on a coast tile); reads only the unit's own tile | disassembly |
| `0x839BBC` | IssueOrder busy lock (u8[order], checked against the CURRENT order): set for 0, 1, 0x16, 0x19, 0x1A, 0x24, 0x25, 0x39; clear for all spells | dump; `FUN_004ef210` first line |
| `0x8C7488..0x8C7B78` | spell button records, 24 bytes: action `0x4E2B20` at +12, ALOW bit at +16, order at +17 | scan for `0x4E2B20` |
| `0x91C700` | `Missile*` pool, stride 0x40 | every spawner |
| `0x91BFBC` | missile slot count: 400 (0x190) when `0x91C178 != 0`, else 200. Only write: `0x4C498D` | disassembly of `FUN_004c4960` |
| `0x91BFB8` | unit SLOT count: 1600 (0x640) when `0x91C178 != 0`, else 600. Only write: `0x4C4980`. (`kRvaUnitCount`: it is the capacity, not the live count; the mod already filters live units) | same |
| missile +0x00/+0x02 | position, pixels (i16) | spawners |
| missile +0x28/+0x2A | target point, pixels; +0x2C target unit; +0x30 SOURCE unit (exempt from its own splash) | `FUN_004af7b0`, `FUN_004afb50` |
| missile +0x34 / +0x35 / +0x36 | type / flags (bit 0 = free slot) / behaviour class | spawners, `FUN_004ae560` |
| missile +0x37 / +0x38 | damage / counter (chain count, lifetime, burn ticks) | spawners, class handlers |
| `0x8C0ACC` | class by missile type (u8): 2 fireball -> 7, 3 flame shield flight -> 8, 4 flame shield orbiter -> 9, 5 blizzard -> 10, 6 death and decay -> 11, 0xB rune visual -> 5, 0xC whirlwind -> 12 | dump; `FUN_004ae560` |
| `0x8C08B0` | class handler table: 7 `0x4AE7C0`, 8 `0x4AEA40`, 9 `0x4AE8E0`, 10 `0x4AE990`, 11 `0x4AEB30`, 12 `0x4AEB70`, 6 impact `0x4AE6D0`, 5 remove `0x4AEC30` | dump; call at `0x4EEE7B` in `FUN_004eea80` |
| `0x8C0AEC` | speed by missile type (px per update): 2..6 and 0xC = 12 | dump |
| `0x8C0B0C` | "ignore armor" by missile type: 1 for types 2, 4, 5, 6, 0xC (every spell missile) | dump; `FUN_004afb50` |
| `0x4AF9E0` | splash: wall damage at the impact tile + 4 neighbours, units in the 7x7 tiles around it, air grid too when arg 2 != 0 (every spell passes 1) | decompile |
| `0x4AFB50` | per-unit splash damage (no owner test) | decompile |
| `0x4BD850` | wall damage at a tile (bounds-checked, only where square flags & 0x0C) | decompile |
| `0x4BD8F0` | DealDamage(attacker, target, dmg): skipped when `target+0x46 != 0` (unholy armor) | decompile (`hp_damage` notes in `data_tables.md` addendum) |
| `0x4AF0E0` / `0x4AF290` / `0x4AF3A0` | spawn fireball / flame shield flight / flame shield orbiter | decompile |
| `0x4AF040` -> `0x4AEC70` | spawn one blizzard shard chain | decompile |
| `0x4AF500` / `0x4AF5C0` | spawn death and decay / whirlwind | decompile |
| `0x918D14` / `0x918D48` / `0x918D80` | rune x u8[50] / rune y u8[50] / rune timer u16[50], one table for ALL players, no owner field | `FUN_004e2ba0`, `FUN_004e2cd0` |
| `0x4E2BA0` | place one rune (bounds, empty ground tile, no rune there, free slot) | decompile |
| `0x4E2CD0` | rune tick, called once per step at `0x4EF482` (first call of the timer pass `FUN_004ef480`) | decompile, `find_calls` |
| `0x4E2CA0` | clears the three rune arrays; called at `0x4C4464` on the NEW-map branch of `FUN_004c4280` | disassembly |
| `0x4E29C0` | holy vision, one reveal point | decompile |
| `0x4D3850` | reveal with sight 9 (`FUN_004d38f0(x, y, owner, 0x13, ...)`, 19x19) | decompile; `data_tables.md` sight table |
| `0x4AFD70` | move the viewport origin `0x91AD80` / `0x91AD84` to a tile, clamped to map pixels minus `0x91CB08` / `0x91CB0C` [camera meaning inferred] | decompile |
| `0x91C590` | fog refresh countdown (u32), reloaded with 100 each cycle; holy vision writes 150 when `0x91C178 != 0` | disassembly `0x4C51AD..0x4C51D5`, `0x4E27DB` |
| `0x4CB030` | AI "random-10" area-cast helper | disassembly below |
| `0x4CB0E0` / `0x4CB3E0` | AI scan-and-cast / 31x31 ground-grid scan (known, `RE_NOTES.md`) | `r_ai.c`, `r_filters.c` |
| `0x4CB150` | AI "unit allied to A within r tiles of B" (ground grid only, B itself excluded) | decompile |
| `0x4CD320` | AI nearest attackable enemy unit on the whole map (per-player unit lists `0x934848`, next at +0x68) | decompile |
| `0x4CAC50` | AI cast-at-unit wrapper (pending spell around `IssueOrder(unit, 0, 0, target, 0x4E2970)`) | decompile |
| `0x4CA6B0` `0x4CA700` `0x4CAB20` `0x4CA570` `0x4CA520` `0x4CA9D0` `0x4CAA20` `0x4CA900` `0x4CA990` | AI filters: fireball, flame shield, invisibility, blizzard / death and decay (workers + halls), same (buildings), whirlwind, whirlwind (random-10), runes, holy vision | decompile |

UDTA flag bits used by these filters (from `Data\Rez\unitdata.dat` offset 0x1486): 0x100 = types 0x02 / 0x03 only
(peasant, peon); 0x1000 = types 0x4A, 0x4B, 0x58..0x5B only (the town hall / keep / castle families); 0x20 building;
0x2 flyer.

## 1. The eight spells

| Spell | Order | ALOW bit (`0x919250[p]`) | Mana (`0x8C5EB8[o]`) | Range | Target | Step action | Hit-frame action |
|---|---|---|---|---|---|---|---|
| holy vision | 0x26 | 0x00001 | 70 | 0xFF | tile | `0x4E2840` | `0x4E2720` |
| flame shield | 0x2A | 0x00010 | 80 | 6 | unit | `0x4E2900` | `0x4E2110` |
| fireball | 0x2B | 0x00020 | 100 | 10 | tile | `0x4E2840` | `0x4E20C0` |
| invisibility | 0x2D | 0x00080 | 200 | 6 | unit | `0x4E2900` | `0x4E22F0` |
| blizzard | 0x2F | 0x00200 | 25 per wave | 10 | tile | `0x4E2840` | `0x4E19A0` |
| whirlwind | 0x34 | 0x08000 | 100 | 12 | tile | `0x4E2840` | `0x4E27F0` |
| runes | 0x37 | 0x40000 | 200 (40 back per rune not placed) | 10 | tile | `0x4E2840` | `0x4E25A0` |
| death and decay | 0x38 | 0x80000 | 30 per wave | 12 | tile | `0x4E2840` | `0x4E2530` |

Casters (button records + AI dispatcher `FUN_004ca4a0`): mage 0x0A / 0x18 fireball, flame shield, invisibility,
blizzard; death knight 0x0B / 0x15 whirlwind, death and decay; paladin 0x0C / 0x2C holy vision; ogre-mage 0x0D / 0x17
runes. Bits and orders agree three ways: button bytes +16 / +17 (e.g. `05 2B` fireball, `13 38` death and decay,
`12 37` runes, `00 26` holy vision), the AI's research tests below, and the UGRD flag column in `data_tables.md`.

How `IssueSpell` must be called:

- unit spells (0x2A, 0x2D): `IssueSpell(caster, order, 0, 0, target)`, exactly like the existing `TryCast`.
- tile spells (all others): `IssueSpell(caster, order, x, y, nullptr)` with x, y on the map. Passing a target instead
  is equivalent (see `IssueOrder` below) but a tile makes the claim list (`PassImpl` reads +0x84/+0x86) exact.

```c
// FUN_004ef210 IssueOrder (r_issue.c)
if (busyLock_0x839BBC[unit->order] == 0) {
  unit->orderTarget = target;                                   // +0x88
  if (target == 0) { unit->orderX = x; unit->orderY = y; }       // +0x84 / +0x86, no validation
  else *(u32*)(unit + 0x84) = *(u32*)(target + 0x18);            // the target's tile
  ...handler(unit); FUN_004d9b60(unit); ...
}
// FUN_004e2970 spell handler (eye_action.c)
if (cheats & 8) unit->mana = 0xff;
SetOrder(unit, pendingSpell_0x9348BC);
if (clearTarget_0x8C1804[pendingSpell]) unit->orderTarget = 0;
```

Step actions (called every step once the caster has walked into range):

```c
// FUN_004e2840 (positional): in range? then face (+0x84) and keep casting, else stop
if (!FUN_004d9e20(unit)) { SetOrder(unit, 2); return 1; }
unit[10] = FUN_004ce630(unit->xy, unit->orderXY); unit[6] |= 0x20; return 0;
// FUN_004e2900 (flame shield, invisibility, slow, haste, unholy armor)
if (target && target alive && FUN_004d9e20(unit) && !(flags[target->type] & 0x20 /*building*/)) { face; return 0; }
SetOrder(unit, 2); return 1;
```

`FUN_004d9b60` (end of `IssueOrder`) also drops a unit target whose byte +0x29 lacks the caster owner's bit
(`FUN_004d9e40` / `FUN_004f03b0`) [meaning of +0x29 unverified]; own units pass in practice (heal etc. work).

## 2. Effects

### 2.1 The shared splash (fireball, flame shield, blizzard, death and decay, whirlwind)

```c
// FUN_004af9e0(missile, alsoAir)  (ac_splash.c)
tx = missile->x >> 5; ty = missile->y >> 5;
FUN_004bd850(tx, ty, dmg);                                                     // WALLS, bounds-checked
FUN_004bd850(tx+1, ty, dmg>>2); FUN_004bd850(tx-1, ty, dmg>>2); FUN_004bd850(tx, ty+1, dmg>>2); FUN_004bd850(tx, ty-1, dmg>>2);
if ((u16)tx < mapSize && (u16)ty < mapSize) FUN_004afb50(missile, groundGrid[ty*size + tx]);
for (y = ty-3 .. ty+3) for (x = tx-3 .. tx+3)                                   // 7x7, (u16) bounds checks
  if ((u16)x < size && (u16)y < size) {
    FUN_004afb50(missile, groundGrid[y*size + x]);
    if (alsoAir) FUN_004afb50(missile, airGrid[y*size + x]);                    // 0x91AD70
  }

// FUN_004afb50(missile, u)
if (u == 0 || u == missile->source /* +0x30 */) return;                          // NO owner / alliance test
if (u already in this splash's hit list 0x91ABC8[0x91AD50]) return;
dx = missile->x - (u->px + halfBoxX_0x91BFC0[type]); dy = missile->y - (u->py + halfBoxY_0x91BFC2[type]);
d2 = max(dx*dx, dy*dy);
if (d2 < 0x700) {                                                               // < 42.3 px
  dmg = (d2 < 0x200) ? missile->dmg : missile->dmg >> 2;                         // full inside 22.6 px
  if (ignoreArmor_0x8C0B0C[missile->type] == 0) { dmg -= armor(u); if (dmg < 1) return; }
  half = (dmg + 1) / 2;
  FUN_004bd8f0(missile->source, u, half + rand() % (half + 1));                 // game RNG
}
// FUN_004bd8f0: ... && target->unholyArmor_0x46 == 0 && dmg != 0 -> hp -= dmg (kill when hp <= dmg)
```

Consequences:

- Own and allied units, flyers, invisible units and buildings are all hit. Buildings are in the ground grid on every
  footprint tile (`FUN_004b4910`: `*piVar3 = building` for each of w x h tiles), but the distance is measured to the
  building's centre, so a building takes splash only when an impact lands within 42 px of its centre.
- Spell missiles ignore armor (`0x8C0B0C` = 1 for types 2, 4, 5, 6, 0xC).
- Damage per splash hit: dmg 40 (fireball) -> 20..40 inside 22 px, 5..10 out to 42 px; dmg 10 (blizzard, death and
  decay) -> 5..10 / 1..2; dmg 4 (whirlwind, flame shield orbiter) -> 2..4 / 1..2.
- Walls take `dmg >> 2` (min 1) per centre hit into bits 11-15 of the tile map word; above 0x13 the wall is destroyed
  (`FUN_004eba80`). Walls have no owner: the player's own walls are damaged too.
- Unholy armor (`+0x46 != 0`) makes a unit immune; the AI never writes that field for this purpose except by accident
  (see invisibility).

### 2.2 Fireball (0x2B)

```c
// FUN_004e20c0 hit frame
if (mana < cost) { SetOrder(unit, 2); return; }
mana -= cost; SetOrder(unit, 2); unit->invis = 0; FUN_004af0e0(unit);
// FUN_004af0e0: type 2, source = caster, dmg = 0x28 (40) unless the caster is a dragon / gryphon rider,
// target point = order tile centre (orderTarget was cleared by the handler), remaining = max(|dx|,|dy|) px
// FUN_004ae7c0 class 7, every missile update:
step = (remaining > 0x31) ? speed : speed >> 1;          // 12, then 6 px
remaining -= step; pos += step * dir;                     // Bresenham FUN_004ce500 keeps the direction
if (remaining < 0x32) {
  if (limit /* 40 when dmg == 40, else 25 */ < m->counter) { class = 6 /* impact */; return; }
  if ((++m->counter & 7) == 0) { explosion visual; FUN_004c7b70(tile); FUN_004af9e0(m, 1); }
}
```

Five splashes (counter 8, 16, 24, 32, 40). From the step sizes: the first lands on the aimed tile (within 7 px), the
next four about 1.5, 3, 4.5 and 6 tiles BEYOND it along the caster -> target line [positions inferred from the step
arithmetic; that the Bresenham stepper keeps the direction past the end point is inferred]. The final impact
(`FUN_004ae6d0`, `0x8C090C[2] == 0`, no target unit) only damages walls at the end tile. Nothing is damaged on the
way in, so own units between the caster and the target are safe; units on or up to ~6 tiles behind the target line
are not.

### 2.3 Flame shield (0x2A)

```c
// FUN_004e2110 hit frame
if (target && target alive) {
  SetOrder(unit, 2); unit->invis = 0;
  if (target->flameTimer_0x4E == 0 && !(flags[target->type] & 2 /*flyer*/)) {
    if (FUN_004e2c60(unit) /* mana check + deduct */) { target->flameTimer = 500; FUN_004af290(unit); sound; }
  }
  return;
}
SetOrder(unit, 2);
```

- Valid targets: any live non-building (step action), non-flyer (action) unit of ANY owner; a target that already has
  a shield or is a flyer costs no mana (the check comes before `FUN_004e2c60`).
- `FUN_004af290` sends a type-3 missile to `caster->orderTarget`; on arrival (`FUN_004aea40`) it spawns 5 orbiters
  (`FUN_004af3a0(target, idx, phase)` with phases 0, 4, 2, 3, 5): type 4, source = the SHIELDED unit, dmg 4,
  lifetime `phase*3 + 0x200` = 512..527 updates. `FUN_004ae8e0` moves each orbiter along a 51-point ring (`0x8C0930` /
  `0x8C0A00`: dx -28..+32 px, dy -34..+12 px from the unit centre), ends it when the unit dies, and splashes every 16
  updates (`(counter & 0xF) == 1`).
- Reach: 32-34 px ring + 42 px splash = about 2.4 tiles around the shielded unit, which is exempt; everyone else,
  friend or foe, flyers included, is burned. The shield moves with the unit for its whole life.

### 2.4 Invisibility (0x2D)

```c
// FUN_004e22f0 hit frame
if (target && target alive && mana >= cost) {
  mana -= cost; SetOrder(unit, 2); unit->invis = 0;
  target->invis_0x44 = 2000; effect; sound;
  return;
}
SetOrder(unit, 2);
```

- Any live non-building unit of any owner. No "already invisible" check: a recast costs 200 and only resets the timer.
- The timer pass decrements +0x44 per step (`RE_NOTES.md`). `FUN_004a8970` zeroes +0x44 before EVERY hit-frame action,
  attacks included, so the unit's next attack or cast ends it. Invisible enemies are skipped by the mod's scans.

### 2.5 Blizzard (0x2F)

```c
// FUN_004e19a0 hit frame: NO SetOrder(2) on success -> the order stays 0x2F and repeats
if (mana < cost) { SetOrder(unit, 2); return; }
mana -= cost; sound;
FUN_004af040(unit, 10) x5; unit->invis = 0;
// FUN_004af040: pick a free slot or return silently; target pixel =
//   ((orderX + rand()%5) * 32 - 0x30, (orderY + rand()%5) * 32 - 0x30)   = centre of a tile in x-2..x+2, y-2..y+2
// FUN_004aec70(m, caster, 10, tx, ty): type 5, source = caster, dmg 10, chain count 10, starts 110 px left / 170 px up
// FUN_004ae990 class 10: when the shard lands -> FUN_004af9e0(m, 1); if (count) { count--; new shard, same point }
```

- One wave = 5 impact points (game RNG) inside the 5x5 tiles around the order tile, each hit 11 times (chain 10..0),
  dmg 10 per hit. Reach from the order tile: 2 tiles + 42 px = about 3.3 tiles.
- The five points are tile CENTRES, centred on the order tile: `(x + rand()%5) * 32 - 0x30` at `0x4AF097..0x4AF09D`
  is `(x - 2 + rand()%5) * 32 + 16`, the same five centres as death and decay. The shard starts 110 px left / 170 px up
  of that point (jitter `rand()%3 * 11 - 11` per axis, `0x4AECC5..0x4AED0E`), moves 12 px per update (`0x8C0AEC[5]`)
  and lands when its distance counter (+0x20, `max(|dx|, |dy|)`) goes negative (`0x4AE9D8`), so it may land up to one
  12 px step past the point, down and to the right [inferred from the step arithmetic; the Bresenham helper
  `FUN_004ce500` was not traced].
- Channel: the next cast-animation cycle drops the next wave, until mana < 25 or a new order. From 255 mana: up to 10
  waves.

### 2.6 Death and decay (0x38)

```c
// FUN_004e2530 hit frame: same channel pattern as blizzard
if (mana < cost) { SetOrder(unit, 2); return; }
mana -= cost; sound; FUN_004af500(unit, 10) x5; unit->invis = 0;
// FUN_004af500: type 6, source = caster, dmg 10, counter 10, point = order tile centre + (rand()%5 << 5) - 0x40
// FUN_004aeb30 class 11: if (counter == 0) remove; else { counter--; FUN_004af9e0(m, 1); }   // does not move
```

- One wave = 5 fixed points in the 5x5 tiles around the order tile, 10 splash hits each, dmg 10. Same 3.3-tile reach.
- 30 mana per wave; from 255 mana: up to 8 waves.

### 2.6a What one wave takes off a structure (the mod's no-overkill number)

The mod needs to know when another wave is waste. Per impact, `FUN_004afb50` deals `h + rand() % (h + 1)` with
`h = (dmg + 1) / 2`, so a full hit averages **0.75 x dmg** and a quarter hit (22..42 px) **0.1875 x dmg**; armor is
ignored for these missile types. What reaches one structure depends on where each chain or cloud lands:

| | points per wave | impacts per point | offsets from the aim tile | full-damage share | mean per wave |
|---|---|---|---|---|---|
Both spells drop their points on the centres of the 5x5 tiles around the aim tile: -64, -32, 0, +32, +64 px from
the aim tile's centre per axis (section 2.5 for blizzard, `FUN_004af500` + `FUN_004af7b0` for death and decay, whose
positional point is `order tile * 32 + 16`). An earlier version of this table put blizzard at `rand()%5 - 1.5` tiles
= -48 .. +80 px: those numbers are measured from the tile's top-left pixel, not from its centre, and the "half a tile
past the aim" that followed from them was wrong.

The splash measures from a unit's CENTRE: `unit px + half box`, with the half box table `0x91BFC0` filled by
`FUN_004ee2d0` as `size * 16` per type (`0x4EE300..0x4EE31D`, sizes from `0x917AD0`), and a building's pixel position
is its top-left tile * 32 (`CreateUnit` masks it to the tile, `0x4EDBA7..0x4EDBC3`). So a w x h building's centre is
`(2X + w, 2Y + h)` in half tiles. Impact centres sit on odd half tiles, so per axis an impact is 0 or 16 px off (full),
32 px (quarter) or 48 px and more (nothing):

| footprint | impacts that reach its centre | per wave aimed at its middle |
|---|---|---|
| 1x1 unit, 3x3 | its middle tile full, the 8 around it a quarter | 1 of 25 full, 8 of 25 quarter |
| 2x2, 4x4 | its middle 2x2 tiles, all full; a 4x4's outer ring of 12 tiles: **nothing** | 4 of 25 full |

Mean per wave aimed at the middle: blizzard 55 impacts x (1/25 x 0.75 + 8/25 x 0.1875) = ~4.95 x dmg on a 3x3 or a
unit, 55 x 4/25 x 0.75 = ~6.6 x dmg on a 2x2 or 4x4; death and decay (50 impacts) ~4.5 and ~6.0 x dmg.
`src/autocast.cpp` uses **5 x the live damage byte** for both spells (50 at the game's 10, and `[spell_damage]` moves
it); the number only decides whether to spend one more wave, and one wave can roll well above or below it.

### 2.6b How the mod picks the aim (1.21)

The author saw blizzards "target the corner of a building" with most of the wave missing. Two causes in 1.20: when the
straight aim had a friendly near it, the aim was walked up to 2 tiles off (blizzard 3 tiles before) the target, and
the spot was then valued by what stood within 2 tiles of it: any footprint tile of a building counted the whole
building, although the splash only reaches a 4x4 from impacts on its middle 2x2 tiles. The 1.20 selftest shows it: a
castle at 26..29 x 19..22 with a footman beside it got a blizzard at 24,17, whose 22..26 pattern reaches none of the
castle's middle tiles. Likewise a grunt next to a castle corner made "a building plus a unit".

Since then every tile within the spell's reach (`search_radius`, never beyond the spell range) of the caster's current
tile is a candidate aim, scored by what the pattern above would do there:

- value = expected hits in quarter hits: per enemy `3 x full + any` over the 25 impact tiles (a full hit is worth 4
  quarter hits: 0.75 against 0.1875 x dmg), times `area_building_value` for a building. A unit in the middle of the
  pattern scores 12, a 3x3 12, a 2x2 or 4x4 16, so `area_building_value` keeps its meaning: a building fully in the
  blast is worth that many units fully in it, and a building the pattern only grazes counts for what it would take.
- the gate, the overkill rule and the channel's building hit points count an enemy only when some impact can hit it
  at FULL damage (a unit within 2 tiles of the aim, a building whose centre is inside the pattern); a quarter hit
  alone adds value but never makes a spot a target.
- ties: the most enemy footprint tiles inside the 5x5 (what the cast line reports as "building tiles"), then the aim
  with the counted enemies nearest its middle, then the aim nearest the caster. A lone castle is thus hit on its middle
  (16 of 16 tiles) and not on the corner tile nearest the caster, which the splash values the same.
- an aim with fewer than 13 of its 25 impact tiles on the map is skipped. Impacts off the map are not clipped:
  `FUN_004af9e0` takes `x >> 5` of the point and only bounds-checks the grid reads, so an impact one tile outside still
  splashes the edge tile; one further out is wasted.
- unchanged: `area_friendly_clearance` around the aim (checked in score order, first clean aim wins), claims, the
  watchdog, the dry run used by `[priority] save_mana`.

Budget per caster per pass: at most 31 x 31 = 961 aim tiles (`search_radius` <= 15), at most 256 enemies gathered
from the tiles within reach + 6, each adding to the at most (w + 6) x (h + 6) aims its pattern reaches, and at most
256 friendly-fire checks. Whirlwind keeps the per-target aim: it lands on the aim and then wanders at random, so there
is no pattern to cover.

### 2.7 Whirlwind (0x34)

```c
// FUN_004e27f0 hit frame
if (mana < cost) { SetOrder(unit, 2); return; }
mana -= cost; SetOrder(unit, 2); unit->invis = 0; FUN_004af5c0(unit); sound;
// FUN_004af5c0: if (unit == unit->orderTarget) { orderTarget = 0; return; }   // self-target: mana already spent
//   type 0xC, source = caster, dmg 4, lifetime 800; aim = order tile centre; start = aim + rand(-64..63) per axis
// FUN_004aeb70 class 12, every update:
if (m->life & 1) FUN_004af9e0(m, 1);
if (--m->life == 0) { remove; return; }
pos += FUN_004ce500 step;                                  // 1 px per update (no speed multiplier)
if (pos == waypoint) waypoint = pos + rand(-64..63) per axis;   // no map clamp
```

- 400 splash hits of dmg 4 over 800 updates, on a random walk (game RNG): 1 px per update, a new waypoint within
  +/-2 tiles of its current position each time it arrives. Total path at most 800 px (25 tiles); the net drift is
  random and unbounded by the code [typical drift not measured].
- Waypoints are not clamped: the whirlwind can leave the map. The splash bounds-checks every tile it touches (u16
  compares), so this is memory-safe [drawing off the map unverified].

### 2.8 Runes (0x37)

```c
// FUN_004e25a0 hit frame
if (mana < cost) { SetOrder(unit, 2); return; }
mana -= cost; SetOrder(unit, 2); unit->invis = 0;
placed = FUN_004e2ba0(x,y) + FUN_004e2ba0(x,y-1) + FUN_004e2ba0(x+1,y) + FUN_004e2ba0(x,y+1) + FUN_004e2ba0(x-1,y);
mana += (200 / 5) * (5 - placed);                          // 40 back per rune not placed
// FUN_004e2ba0(x, y): (u16)x < size && (u16)y < size && groundGrid[tile] == 0 && no live rune on (x,y)
//   && a free slot among 50 -> x[i] = x; y[i] = y; timer[i] = 0x800; visual; return 1
// FUN_004e2cd0, once per step for all 50 slots:
if (timer[i]) {
  --timer[i];
  if (u = groundGrid[y[i]*size + x[i]]) {                  // ANY ground / sea unit, any owner
    timer[i] = 0; sound; explosion visual;
    if (u->unholyArmor_0x46 == 0) { if (u->hp < 0x33) Kill(u); else u->hp -= 50; }
  }
}
```

- Plus shape: centre + 4 neighbours. A rune is not placed on a tile holding a ground unit (so casting on an enemy's
  tile lays at most the 4 neighbours and refunds 40), off the map, on an existing rune, or when all 50 slots are in
  use.
- Each rune: 50 damage (flat, no armor, no random), kills at hp <= 50, to the first ground / sea unit filed on its
  tile, friend or foe, the caster included. Flyers never trigger (air grid not read). Unholy armor: no damage, rune
  still spent. Buildings do not move onto runes; a building placed over a rune tile would be filed there and trigger
  it [inferred]. Life 0x800 = 2048 steps.
- The table is cleared on every new map (`FUN_004e2ca0` at `0x4C4464`) and restored from savegames (the game-state
  block copy `FUN_004aa6e0` writes `0x918D80..`), so a stale rune never indexes a smaller map.

### 2.9 Holy vision (0x26)

```c
// FUN_004e2720 hit frame (eye_action.c)
mana -= cost; SetOrder(unit, 2); x = orderX; y = orderY; unit->invis = 0;
FUN_004e29c0(x, y); FUN_004e29c0(x, y-8); FUN_004e29c0(x, y+8);
FUN_004e29c0(x+6, y+4); FUN_004e29c0(x+6, y-4); FUN_004e29c0(x-6, y+4); FUN_004e29c0(x-6, y-4);
if (0x91C178) fogCountdown_0x91C590 = 150;
// FUN_004e29c0(x, y, caster): if ((u16)x >= size || (u16)y >= size) return;   // off-map point skipped
//   FUN_004d3850(x, y, owner);                           // sight 9 = 19x19 reveal
//   if (localPlayer_0x918CCD == owner) FUN_004afd70(x, y);   // viewport jump
//   effect missile 0x16, sound 7
```

- Reveals 7 overlapping 19x19 windows: tiles x-15..x+15, y-17..y+17 (a 31x35 hexagon). Explored tiles stay explored;
  current vision lasts until the next fog refresh, which the 150 write pushes to at least 150 steps away (for the
  whole map).
- Range 0xFF: the paladin does not walk. The hit frame ends in stop, dropping what the paladin was doing.
- For the local player, each point moves the viewport origin (`FUN_004afd70` writes `0x91AD80` / `0x91AD84` =
  tile * 32 clamped to `mapSize*32 - 0x91CB08 / 0x91CB0C`, then calls the no-op `0x4857C0`); the last call wins, so
  the view ends at (x-6, y-4) [camera meaning inferred; whether the Remastered renderer follows these globals is
  unverified].

## 3. The computer AI (dispatcher `FUN_004ca4a0`, think loop every 0x32 steps, `RE_NOTES.md`)

Order of attempts per caster type (`r_ai.c`, `r_ai2.c`). "Invisible" = the CASTER's `+0x44 != 0`; the AI makes its
own casters invisible (mage rule below) and then prefers these sneak branches.

| Caster | Order of attempts |
|---|---|
| mage `FUN_004cb480` | [invisible: blizzard on the nearest enemy worker / hall; polymorph random-10] polymorph, **fireball**, **invisibility**, **blizzard** on the nearest enemy building, **flame shield**, slow, fallbacks (retreat to the mage tower when mana < 50) |
| death knight `FUN_004cac80` | [invisible: **death and decay** on the nearest enemy worker / hall; **whirlwind** random-10] raise dead, unholy armor, **death and decay** on the nearest enemy building, death coil, **whirlwind** on the nearest enemy, haste, fallbacks |
| paladin `FUN_004cb2f0` | [invisible: exorcism random-10] heal, exorcism, **holy vision** random-10 at mana 255 |
| ogre-mage `FUN_004cb200` | [invisible: **runes** random-10] bloodlust, **runes** random-10, eye at mana 255 |

Area spells at the nearest enemy (blizzard / death and decay; whirlwind is the same with its own filter):

```c
// FUN_004cb480, blizzard on buildings (visible branch)
if (mana >= 3 * 25 && (researched & 0x200) && mana >= 25 &&
    (t = FUN_004cd320(caster)) && FUN_004ca520(caster, t)) { FUN_004cac50(caster, t, 0x2f); return 1; }
// FUN_004ca520: alliance[caster][t] == 0 && (flags[t] & 0x20 /*building*/) && !FUN_004cb150(caster, t, 3)
// FUN_004ca570 (invisible branch): same with (flags[t] & 0x1100) = peasant / peon / hall families
// FUN_004cac80 uses the same two tests for death and decay with 3 * 30 mana and bit 0x80000
// whirlwind (visible): (researched & 0x8000) && mana >= 100 && (t = FUN_004cd320(caster)) && FUN_004ca9d0(caster, t)
//   FUN_004ca9d0: alliance == 0 && !FUN_004cb150(caster, t, 4) && !(t->aiMarks_0x4C & 0x40)   -> mark |= 0x40
// FUN_004cb150(a, b, r): any live unit != b in groundGrid within r tiles of b with alliance[a->owner][u->owner] != 0
// FUN_004cd320(caster): over players 0..7 not allied with the caster, over each unit list: attackable
//   (FUN_004a9810, can-target table 0x918580), alive, smallest FUN_004b4730 distance [metric unverified]; whole map
```

The random-10 helper (runes, holy vision, and the invisible-branch whirlwind / exorcism / polymorph):

```
0x4cb061: mov edi, [0x91c704]; call 0x4a0200 (game RNG); div dword [0x91bfb8]   ; slot = rand % 1600
0x4cb07e: test byte [esi+0x1e], 8 ; jne exit(0)                                 ; dead slot -> give up
0x4cb089: call [ebp+0x14] (filter) ; test eax,eax ; je exit(0)                  ; ANY failing pick -> give up
0x4cb093: inc ebx ; cmp ebx, 0xa ; jl 0x4cb061                                   ; 10 passes in a row
0x4cb0a5: push 0x4e2970; push 0; push y; push x; push caster; call 0x4ef210      ; cast at the 10th unit's tile
```

Filters: runes `FUN_004ca900` = enemy and flags & 0x100 (peasant / peon); holy vision `FUN_004ca990` = caster not
invisible, enemy, not marked 4 (paladin requires mana == 255); whirlwind `FUN_004caa20` = enemy, no caster-allied unit
within 4 tiles, not marked 0x40, flags & 0x1100. Ten consecutive random slots of 1600 must all pass, so these casts are
rare [actual frequency unverified].

Fireball, flame shield, invisibility (31x31 ground-grid scan `FUN_004cb3e0`, first hit in scan order):

```c
// FUN_004ca6b0 fireball: enemy; reject when a caster-allied unit is within 3 tiles of it,
//   UNLESS the caster itself is below half HP (then fire anyway)
if (alliance[caster][t] != 0) return 0;
if (FUN_004cb150(caster, t, 3) && GetMaxHp(caster) / 2 <= caster->hp) return 0;
return 1;
// FUN_004ca700 flame shield
if (caster->invis == 0 && t->flameTimer == 0 && !FUN_004cb150(caster, t, 2) && !(flags[t] & 0x20)) {
  if (alliance[caster][t] == 0) return FUN_004cb150(t, t, 1);          // enemy next to one of ITS friends
  if (t->order == 12 && range_0x917E40[t->type] == 1) return 1;         // own melee unit in combat
}
return 0;                                                              // after the cast: t->flameTimer = 1 (claim)
// FUN_004cab20 invisibility
if (caster->invis == 0 && alliance[caster][t] != 0 && !(flags[t] & 0x20) && caster->aiOrder_0x5E == 2 /*attack*/ &&
    ((flags[t] & 0x20000 /*caster*/) || range[t->type] != 1)) return 1;
// after the cast the AI writes t+0x46 = 1: the UNHOLY ARMOR timer, not +0x44 (original bug) -> 1 step of immunity
```

Notes for parity: every AI friendly-fire check reads the ground grid only (own flyers are not protected), the AI never
targets flyers with these scans, and the AI's own "claims" are writes into the target (+0x4E, +0x46, +0x4C marks);
the mod must keep using its own claim list instead of writing unit fields.

## 3a. The computer's paladin, and where the mod's cooldown goes in

`FUN_004cb2f0(caster)` is the whole paladin AI. The dispatcher `FUN_004ca4a0` calls it for unit types 0x0C (paladin)
and 0x2C (Turalyon), so these call sites are paladins and nothing else. It tries, in order:

| # | Attempt | Gate | Where it casts |
|---|---|---|---|
| 1 | exorcism while invisible (`+0x44 != 0`) | upgrade bit 8 of `0x919250[owner]`, mana `+0x26` >= `0x8C5EB8[0x29]` | `FUN_004cb030` at `0x4CB309`: ten random unit slots in a row must pass `FUN_004caae0`, then order 0x29 at the tenth one's tile |
| 2 | **heal** | upgrade bit 2, mana >= `0x8C5EB8[0x27]` (which is the price of ONE hit point) | `FUN_004cb0e0` at `0x4CB323`: 31x31 ground-grid scan `FUN_004cb3e0`, first match wins, order 0x27, target marked `+0x4C \|= 0x10` |
| 3 | exorcism | upgrade bit 8, mana >= `[0x8C5F0A]` (the per-hit-point byte, not the cost word) | scan `FUN_004cb3e0` at `0x4CB35E`, then `FUN_004ef210` inline at `0x4CB3B8`, target marked `+0x4C \|= 8` |
| 4 | holy vision | mana exactly 255 | `FUN_004cb030` at `0x4CB382`, order 0x26 |

The heal filter `FUN_004ca7c0(caster, target)` is the answer to "when does a computer paladin heal": the caster is not
invisible, `0x919578[caster.owner * 16 + target.owner] != 0` (allied, its own units included), the target's type has
flag `0x8000000` (fleshy), `target->hp < GetMaxHp(target)` - **any** missing hit point, a scratch is enough - and the
target is not already marked `+0x4C & 0x10` by another paladin this pass. The exorcism filter `FUN_004caae0` is the
same shape: not allied, type flag `0x8000` (undead), not marked `+0x4C & 8`.

So the AI has no cooldown and no wound threshold at all, and with a cheap `[spell_cost] heal` it will cast on every
think step it can afford. `[heal] cooldown_for_computer` hooks the three `call rel32` of attempts 1 to 3 (never 4):
each stub asks `autocast::ComputerHealAllowed` / `ComputerExorcismAllowed` and either returns 0 in EAX without
calling the game's function - which the AI reads as "found nothing" and follows with its next attempt - or calls it
and starts the timer when it reports a cast. Only EAX carries a result at all three sites (the game re-tests it right
after each call), the arguments are left where the game put them, and the caster is the first stack argument at the
first two sites and the second at `0x4CB35E`, whose callee takes the filter first.

## 4. Proposed safe autocast rules (human caster, single player)

Shared rules for all eight (in addition to the existing research, mana, invisible-caster and
`OrderAllowsAutocast` gates):

- Default OFF, one `[spells]` switch each: `fireball`, `flame_shield`, `invisibility`, `blizzard`,
  `death_and_decay`, `whirlwind`, `runes`, `holy_vision`.
- Friendly test = any live unit whose owner is the local player or allied to it (`Allied`), in EITHER grid, plus own
  buildings (they sit in the ground grid), within the clearance radius of the tile. Units under unholy armor may be
  treated as safe (immune, `FUN_004bd8f0`). Also refuse when a wall tile (square flags & 0x0C) is within 1 tile of an
  impact point (walls have no owner).
- Enemy count = `IsEnemy` units, not invisible, not under unholy armor (immune), both grids; optionally only on tiles
  the player currently sees (`0x91AD5C[tile] != 0x10`) so the mod does not use hidden information.
- Tile choice: always a live unit's own tile (`+0x18/+0x1A`, on the map by construction) or a value clamped to
  `[0, size-1]`; the `game::IssueOrder` guard stays the backstop. Pick area-spell targets within the SPELL range of
  the caster (10 / 12), so the caster does not walk off toward a far cluster.
- Claims: tile claims already work for positional orders (`PassImpl` copies +0x84/+0x86). For area spells use an
  AREA claim: skip a tile within the clearance radius of any live 0x2F / 0x34 / 0x38 claim (the player's manual casts
  included).
- Global cap on mod-issued area spells alive at once (missile pool 400 shared with every arrow and cannon ball).

| Spell | AI rule (section 3) | Proposed rule | Risk left |
|---|---|---|---|
| fireball | first enemy in 31x31, no caster-ally within 3 tiles (ignored below half HP) | enemy tile within range 10; splash points = aim + 0, 1.5, 3, 4.5, 6 tiles along the caster -> aim direction; no friendly within 2 tiles of any of them, no wall within 1; score = enemies within 1 tile of the points, need >= N (N = 1 is AI parity; 100 mana) | overshoot line estimated, not measured |
| flame shield | own melee fighter with no friendly within 2 tiles; or enemy next to its own friends | own unit only; attack range 1; not flyer, not building; flame timer 0; fighting (`IsFighting`); no OTHER friendly (both grids) within 3 tiles; >= 2 enemies within 2 tiles. Never the enemy variant | the shield follows the unit for ~512 updates; friendlies that come within ~2.4 tiles later are burned |
| invisibility | allied caster / ranged unit while the mage is on an AI attack mission (`+0x5E` is always 0 for human units, so not reproducible) | own non-building caster or ranged unit, invis timer 0, hp <= 50 %, enemy within `combat_radius`, its own order is move (3) (the player is pulling it back) | 200 mana; any attack by the target ends it |
| blizzard | nearest enemy building (or worker / hall when invisible); mana >= 75; no caster-ally within 3 tiles (ground only) | tile of a visible enemy unit / building within range 10; >= `area_min_enemies` (proposed 3) enemies within 2 tiles, or an enemy building; mana >= 75; no friendly within 4 tiles (3.3-tile reach + 1 for units moving between tiles); channel watchdog (below) | a friendly walking in between two watchdog passes |
| death and decay | same as blizzard, mana >= 90 | same as blizzard, mana >= 90, range 12 | same |
| whirlwind | nearest enemy with no caster-ally within 4 tiles | tile within range 12; >= 3 enemies within 2 tiles; no friendly within 6 tiles; one mod whirlwind at a time | random drift cannot be bounded: cannot be made fully safe |
| runes | random-10 enemy peasants (rare) | centre = tile of an enemy GROUND unit within range 10 that is fighting; >= 2 enemy ground units within 2 tiles; no friendly GROUND unit within 6 tiles; skip if a live rune is within 2 tiles (`0x918D80` > 0) | runes stay 2048 steps and hurt the player's own units that walk over them later |
| holy vision | random-10 enemy tile at mana 255 | paladin idle (stop / stand) at mana 255; tile with the most never-explored tiles (`0x91AD60 == 0x10`) in its 31x35 window, sampled and clamped like `eye.cpp`; skip when nothing is unexplored | camera jump on every cast (section 2.9): blocker |

Channel watchdog for blizzard / death and decay (the only way to stop "burns all mana" and late friendly fire): each
pass, for every caster the mod started a channel with (remember caster serial +0x14, order, tile), while
`EffectiveOrder` is still 0x2F / 0x38: issue stop, `IssueOrder(caster, caster.x, caster.y, nullptr, 0x4D8580)` (own
tile keeps the positional guard happy), when a friendly enters the clearance radius of the order tile, when no enemy
is left within 3 tiles of it, or when mana falls below a reserve. The stop lands in the next-order slot, so one more
wave may still fall [inferred from `SetOrder` semantics in `RE_NOTES.md`, not traced for casting units]. Casts the
player gave by hand are left alone.

As implemented in `src/autocast.cpp` (differences from the table above): fireball line points every half tile from
0.5 before to 7 tiles past the aimed tile, `fireball_min_enemies` default 2; runes need no "fighting" test (enemy ground
units >= 2 within 2 tiles, nothing friendly within 6, the ogre-mage included); whirlwind uses `area_min_enemies`
(default 3) and counts as "in flight" while a type-0x0C missile with the caster at +0x30 is in the pool (or when the pool
cannot be read); walls are refused within 1 (fireball), 3 (blizzard, death and decay, flame shield) and 6 (whirlwind)
tiles; unit targets / tiles are claimed in the module only. The watchdog also runs while autocasting is switched off.

Proposed caster order (the AI's, minus its invisible-caster branches, which the mod never takes):

- mage: polymorph, fireball, invisibility, blizzard, flame shield, slow
- death knight: raise dead, unholy armor, death and decay, death coil, whirlwind, haste
- paladin: heal, exorcism, holy vision
- ogre-mage: bloodlust, runes (eye stays in `eye.cpp`)

## 4a. The resume order, and how to give it back

The player's command path `FUN_004dcc60` walks the selection, asks a per-type function what a click on that tile
means (`0x8C5D90[0x918460[type]]`, 0x3C = nothing), and then:

```c
if (remastered /* 0x91C178 */) unit->+0x8D = 0x3C;      // clear the resume order first
FUN_004dfa30(unit, &x, &y);                             // snap the destination
FUN_004ed8c0(unit, x, y, target, 0x8C1498[order]);      // issue through the order handler table
```

`FUN_004ed8c0` is a thin wrapper that clears a flag for some types and calls the engine's IssueOrder
(`FUN_004ef210`), the same function `game::IssueOrder` wraps.

What the resume order is, read from `SetOrder` `FUN_004ef080` (`0x4EF0D2..0x4EF1CB`):

| Field | Meaning |
|---|---|
| `+0x8D` | the order to resume after a stop: `0x3C` none, `10` attack-move, `5` patrol |
| `+0x8E` | state beside it; the resume only fires while this is 0, and the engine writes `0x14` after resuming an attack-move and `0x28` after a patrol |
| `+0x90` / `+0x92` | the attack-move destination (`SetOrder` reads the dword at `+0x90` as x in the low half, y in the high half) |
| `+0x94` / `+0x96` | the patrol destination |

On `SetOrder(unit, Stop)` with a resume order pending and `+0x8E == 0`, the engine re-issues it itself: patrol
through `FUN_004d8bd0` (entry 5 of the handler table, which clears `+0x88` and `+0x70`, copies `+0x90` to `+0x6C`
and calls `SetOrder(unit, 5)`), attack-move through `FUN_004d82e0` (entry 10, `SetOrder(unit, 10)`) but only when the
unit is still more than a tile from the stored destination; then it writes the word `0x140A` at `+0x8D`.

`src/resume.cpp` mirrors that: `game::IssueOrder` files {unit, serial, order, destination, `+0x8E`} before it clears
the byte, and one pass per tick gives the order back through the same two handlers once the caster is idle, writing
`+0x8D`, `+0x8E` and the destination afterwards exactly as the engine does. The record is dropped when the player
gives the unit anything of their own, when a resume byte appears that the mod did not write, when the unit dies or
leaves the array, and after 30 seconds. The eye of Kilrogg is never filed, and a re-entry guard keeps the hand-back
itself from being filed as a new record.

## 5. Crash and corruption review

| Item | Finding | Evidence |
|---|---|---|
| positional x, y before validation | `IssueOrder` stores +0x84/+0x86 unchecked; the walk-into-range code paths to it [pathfinder not decompiled]. Covered by the existing off-map refusal in `game::IssueOrder` | `FUN_004ef210`, `FUN_004d9bb0` |
| holy vision points off the map | each point skipped by `(u16)x >= size` | `FUN_004e29c0` |
| rune plus shape at the edge | each rune bounds-checked; failure refunds 40 | `FUN_004e2ba0`, `FUN_004e25a0` |
| rune tick | indexes the ground grid with the stored x, y, unchecked; safe because runes are only created on the map, the table is cleared on each new map (`0x4C4464`) and restored with savegames | `FUN_004e2cd0`, `FUN_004e2ca0` |
| splash near / off the edge | all tile indexes pass `(u16) < mapSize` (whirlwind off the map, fireball overshoot) | `FUN_004af9e0`, `FUN_004bd850`, sound `FUN_004c7e40` |
| missile pool full (400) | every spawner used here searches for a free slot and returns silently; mana is already spent | `FUN_004af040`, `FUN_004af500`, `FUN_004af5c0`, `FUN_004af0e0`, `FUN_004af290`, `FUN_004af3a0` |
| rune slots (50, all players) | full table = rune not placed, 40 refunded | `FUN_004e2ba0` |
| unit creation | none of the eight creates a unit (polymorph and raise dead do, already handled) | actions above |
| wrong target kind | flame shield on a flyer or an already shielded unit: stop, no mana; building targets: stop in the step action; invisibility on an invisible unit: full cost | `FUN_004e2110`, `FUN_004e2900`, `FUN_004e22f0` |
| whirlwind self-target | mana spent, no whirlwind (`unit == unit->orderTarget`); impossible with a tile order | `FUN_004af5c0` |
| game RNG | fireball, blizzard, death and decay, whirlwind and the splash use `FUN_004a0200`; same as a manual cast, single-player only | spawners |
| writing unit fields | the AI's claim writes include `+0x46 = 1` after invisibility (grants a step of unholy armor); the mod must not copy that pattern | `FUN_004cb480` |

## Recipe

1. `src/game.h`: `kRvaStopHandler = 0xD8580`; order constants 0x26, 0x2A, 0x2B, 0x2D, 0x2F, 0x34, 0x37, 0x38; rune
   table RVAs `0x518D14` / `0x518D48` / `0x518D80` (read-only, for "rune nearby"); square flag `kSqWalls` exists.
2. `src/autocast.cpp`: extend `kSpells` with {order, bit, kind: unit / tile / channel}; add
   `FriendlyNearTile(w, me, x, y, r)` (both grids, own + allied, own buildings), `EnemiesNearTile`, `WallNearTile`;
   one `Try*` per spell following section 4; area claims; the channel watchdog; the caster order above.
3. Holy vision: only behind its switch, with the camera caveat in the config text, or leave it out.
4. Config (four places, `CLAUDE.md`): the eight `[spells]` keys, default false; tuning keys `area_min_enemies` (3),
   `area_clearance` (4), `whirlwind_clearance` (6), `rune_clearance` (6), `max_area_casts` (2). Regenerate the default
   TOML, update `docs/CONFIG_TUTORIAL.md`, extend `tools/migrate_config.py` only if a key changes meaning.
5. `selftest`: synthetic grids with a friendly flyer in the air grid inside the clearance (must refuse), an enemy
   cluster at the map edge (tile must stay on the map), and a channel whose area a friendly enters (watchdog stop).

## 6. Raise Dead: where corpses live (added after 1.6.0: zero casts in a real session)

Short answers: a corpse is the dead unit's own slot retyped to 0x69; it is in NEITHER unit grid; the computer's own
Raise Dead search (ground grid) therefore never finds one; Raise Dead is a research (0x919250 starts at 0x4020 on every
new map). `TryRaiseDead` scans the unit array since then.

| VA | What | Evidence |
|---|---|---|
| `0x4EE380` | Kill(unit). Mobile unit (flags & 0x1F): `FUN_004b5000(unit)` takes it off its grid tile, then order = 1 (`*(u16*)(unit+0x2E) = 0x3C01`), `state = (state & 0xE7FF) \| 2` | decompile |
| `0x4B5000` | Unfile: clears the tile's grid entry only while it still points at this unit, clears square flag 0x100 / 0x200 | decompile |
| `0x4B4A00` | File into a grid. Callers: `0x4D9794`, `0x4D98F3`, `0x4D9DCC` (movement), `0x4EDF26` (CreateUnit), `0x4EE90B`, `0x4EEA60`: none on the death / corpse path | `find_calls` |
| `0x8C13A4` -> `0x4BDFC0` | Step action of order 1, run when the unit's animation script ends (`FUN_004eea80`, `call [order*4+0x8C13A0]` at `0x4EEDF2`). Unit type < 0x3A with corpse animation `0x8C1208[type] != 0`: `type = 0x69` (the only write of 0x69 into +0x27 in the exe, `0x4BE0A5`), `state \|= 2`, corpse animation. Type 0x69 on a later call: footprint cleared (`FUN_004b4f50`, zeroes the grid entries of the type's footprint unconditionally) and the slot becomes type 0x6A ("jjklm"[w], `0x8C1200`) | decompile, capstone scan |
| `0x8C1208` | Corpse animation by type: 1 human, 2 orc, 3 ship (0x1A..0x27: a sunk ship becomes type 0x69 too), 0 = no corpse: mage 0x0A, death knight 0x0B, ballista, catapult, dwarves, sappers, every flyer, most heroes, skeleton, daemon, critter | dump |
| `0x4EF480` | Timer pass skips every unit with `(state & 7) != 0`: corpses do not decay there; their life is the corpse animation script (runtime data, length not decoded) | decompile |
| `0x4E2420` | Raise Dead hit frame: walks the UNIT ARRAY, every corpse (type 0x69, `(state & 0x0F) == 2`) with `dx*dx + dy*dy < 0x25` from the order tile becomes a skeleton of the caster (CreateUnit type 0x37), `state \|= 8`, 50 mana each while mana lasts | decompile (section 2 era dump) |
| `0x4CB3E0` / `0x4CA8D0` | The computer's search: ground grid `0x91AD6C` only, 31 x 31 box. With corpses never filed there, its Raise Dead branch in `FUN_004cac80` cannot fire [inferred from the code; not observed in game] | decompile |
| `0x4D2B9E` | New map (`FUN_004d2b40`): `mov [eax+0x919290], 0x4020` with eax = -0x40..-4 fills `0x919250[16]` with 0x4020 (fireball 0x20, death coil 0x4000). Raise Dead 0x2000 / Haste 0x10000 only through research (`FUN_004acbc0` ORs the flag in at `0x4ACC24`); holy vision 1 / eye 0x400 come with the paladin / ogre-mage upgrade (`0x4ACC41`, `0x4ACC6D`) | disassembly |
| `0x4D19D0` | PUD ALOW handler (length 0x180): copies six player tables `0x919210` units, `0x919250` known spells, `0x919290` allowed spells, `0x9192D0` spells being researched, `0x919310`, `0x919350` upgrades: a map can grant Raise Dead from the start | decompile |

Consequences for the mod (`src/autocast.cpp`): corpses are searched in the unit array (type 0x69, state nibble 2) in the
computer's 15-tile box, corpses on water tiles (wrecks) are skipped, a raise already under way claims every corpse within
its `0x25` reach, and with `log_casts` a death knight that did not raise the dead logs why (once per 30 s of play).

## [unverified]

- How long a corpse stays raisable in steps (the corpse animation script is runtime data).
- Whether a skeleton raised from a wreck would stand in water (`FUN_004edb10` places at the pixel; not traced).
- How often the random-10 helper actually fires (depends on what free unit slots hold; free-slot contents not read).
- Missile updates per simulation step: class handlers run when the missile's animation interpreter reports a frame end
  (`0x4EEE6A`); frame lengths come from the missile scripts, not decoded. Durations above are in missile updates.
- Fireball splash positions beyond the target (derived from step arithmetic; that the Bresenham stepper continues past
  the end point is inferred).
- Whirlwind typical drift and whether off-map drawing is harmless.
- That `0x91AD80` / `0x91AD84` are the camera the Remastered renderer uses (holy vision camera jump).
- Meaning of unit byte +0x29 (`FUN_004f03b0`, drops unit targets lacking the owner's bit).
- `FUN_004b4730` distance metric used by the AI's nearest-enemy search.
- Whether `alliance[p][p]` is nonzero (the AI's heal and friendly-near checks only make sense if it is; not read at
  runtime). The proposed rules test the owner explicitly as well.
- The UI targeting validator for spells (`FUN_004e9420` path) was not examined; the mod bypasses it.
- Whether a stop issued during a channel prevents the next wave or lets one more fall.
- Where exactly a blizzard shard lands relative to its target pixel (up to one 12 px step down-right, section 2.5):
  it can turn a 16 px full hit into a 26 px quarter hit on one side. The mod scores both spells on the exact centres.
- A building placed on a live rune tile triggering it (inferred from the grid filing, not traced).
