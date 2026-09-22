# Spells: mana, spell costs, spell damage (static RE, Warcraft II: Remastered 1.0.2.2818)

Target: `Warcraft II.exe` 1.0.2.2818 (PE timestamp 1771967463, SHA256 `1A396A77...C162`, identical to the copy Ghidra
analysed). Static analysis only: Ghidra 12.1.3 headless (project clone `war2r_a`), capstone disassembly and linear
sweeps of `.text`, raw dumps of `.data` and of `Data\Rez\unitdata.dat`. All VAs are at the preferred base 0x400000;
RVA = VA - 0x400000. Raw decompiles: `spell_costxref.c`, `spell_missiles.c`, `spell_missiles2.c`, `spell_missiles3.c`,
`spell_manaread.c` in the Ghidra output folder (next to the earlier `dat_*.c` / `eye_*.c` / `food_*.c` dumps).
Anything inferred rather than read is marked `[unverified]`. Nothing here was run in the game.

## Short answers

**1. Mana.** `unit+0x26` is a `u8`. Every access in the unit code is byte-sized (86 byte accesses at displacement
0x26 in `0x4A0000..0x540000`: `movzx`, `mov r8`, `sub` / `add` / `inc` / `cmp` byte; list in section 1). **255 is a hard ceiling**: the
storage is a byte, regeneration stops on `cmp cl, 0xff` (0x4EF579), every UI bar draws mana against 0xFF / 0x100, the
magic cheat writes 0xFF, and the computer AI treats `mana == 0xFF` as "full" (holy vision, eye). There is no per-type
maximum mana. A new caster gets **85** (`mov byte [esi+0x26], 0x55` at 0x4EDF1B, overriding the UDTA "magic" byte,
which is only a 0/1 flag), the knight -> paladin and ogre -> ogre-mage conversions reset mana to 85 as well
(0x4ED338). **Regeneration: +1 mana every 40 simulation steps** (per-unit down-counter `unit+0x74`, reload
`mov byte [esi+0x74], 0x28` at 0x4EF586), only for units with type flag 0x20000 (caster) and only below 255. At game
speed setting 4 (40 ms per step) that is 1 mana per 1.6 s. **A regen multiplier is feasible**: patch the reload byte
(and the two initial counter stores) from 40 to `round(40 / k)`. Raising the maximum is not.

**2. Cost.** `u16[order]` at `0x8C5EB8`, populated for orders 0x26..0x38 (19 entries, 18 real spells), plain
initialised `.data` (section flags 0xC0000040, read/write, inside the file-backed part). **Not stored in savegames,
never written by any code, never re-initialised on map load**: an edit persists for the life of the process, so the
mod must keep the pristine values and write absolute values idempotently. 46 code references, all reads (linear sweep
+ raw 4-byte scan agree): the 18 spell actions (the check-and-deduct at the cast animation's hit frame), heal /
exorcism / runes by fixed address, the UI click check `FUN_004e2b20` ("Not enough mana to cast spell."), the classic
status line `FUN_004e7fa0` (reads only the LOW BYTE), the Remastered tooltip `FUN_0052e080` (u16), and the computer
caster AI (`FUN_004cb030`, `FUN_004cb0e0`, `FUN_004cb200`, `FUN_004cb2f0`, `FUN_004cb480`, `FUN_004cac80`). No button
grey-out reads it. The spell order handler `0x4E2970` does NOT read it. **Cost 0 crashes** Heal and Exorcism
(`div` by the cost at 0x4E2284 / 0x4E2AC4) and makes Blizzard / Death and Decay channel forever; **cost > 255 makes a
spell uncastable** (a byte of mana is never >= 256). Valid range: **1..255**.

**3. Damage.** Every damage number is an **instruction immediate**, not a table. Missile spells store their damage in
the missile's byte `+0x37` at creation (fireball 40, blizzard 10, death and decay 10, whirlwind 4, flame shield 4) and
all of them hit through the shared area routine `FUN_004af9e0` -> `FUN_004afb50` -> `FUN_004bd8f0`: armor ignored for
these missile types, full damage within ~22 px, a quarter within ~42 px, then a random 50..100 %, friendly fire on
everyone except the missile's own caster. Death coil: 50 total, split over the enemies in a 5x5 area lowest-HP first,
caster heals the total. Runes: 50 direct HP loss per rune, bypasses the damage function, friendly fire, ground units
only. Heal and Exorcism have no damage number: they cost **mana per HP** (heal 5 mana per HP, capped at 40 HP per cast;
exorcism 4 mana per HP, uncapped) and the per-HP price IS the table cost.

**4. Recommended implementation.** Costs: data write to the u16 table from saved base values, every tick and at map
load, vanilla values in multiplayer. Damage: byte-verified in-place immediate patches (one byte each, the fireball
needs one extra 16-byte rewrite because its damage doubles as a type marker), synced exactly like `SyncRangeBonus`.
Heal / exorcism: the damage multiplier divides their per-HP price (and scales the heal cap); an exact alternative is
to swap their action-table pointers for mod functions. Mana regen: patch three reload bytes (global) or do extra regen
from the tick hook (player-only possible). Details, bytes and ranges in section 4.

**5. What could break.** Only range violations, and every one is listed with its clamp: splash damage > 254 wraps to
0, death coil > 127 flips signed compares, runes > 128 with the one-byte form, cost 0 or > 255, the fireball marker.
None of these code paths gains a new index into a map grid: every spell target scan already bounds-checks with
unsigned compares against the map size (section 3). Two vanilla crash paths in death coil (a 25-entry stack buffer
with no count guard, and an unchecked missile pointer) are NOT caused by the numbers, but a bigger death coil budget
makes the second one slightly more likely.

---

## Facts table

Order ids, research bits, costs, actions, step functions and ranges are read from the exe (`dump_tables`, AI code
pairs every order id with its research bit, e.g. `FUN_004cb030(unit, 0x40000, 0x37, ...)` = runes). Research bit =
`0x919250[player]` (PUD ALOW layout). Names are confirmed by each action's behaviour and by the Remastered string
table (`spell_<bit>` keys in `Data\Strings\enUS.json`).

| Spell | Order | Bit | Cost | Action (0x8C1590) | Range (0x8C1744) | Damage / heal number | Where it lives | Width | Patch |
|---|---|---|---|---|---|---|---|---|---|
| Holy Vision | 0x26 | 0x1 | 70 | `0x4E2720` | 0xFF (anywhere) | none (reveals 7 spots) | - | - | cost only |
| Heal | 0x27 | 0x2 | 5 **per HP** | `0x4E2220` | 6 | `min(mana/cost, 40, maxHp-hp)` HP | cap: `mov eax, 0x28` @ `0x4E2289` (imm32 @ `0x4E228A`) | imm32, 1..255 useful | cap imm + price (table) |
| (unused) | 0x28 | 0x4 | 5 | none (0) | 0 | - | - | - | leave alone |
| Exorcism | 0x29 | 0x8 | 4 **per HP** | `0x4E1EC0` (+ per target `0x4E2A70`) | 10 | `min(mana/cost, hp)` per undead enemy, 7x7 tiles | no immediate | u8 | price (table) |
| Flame Shield | 0x2A | 0x10 | 80 | `0x4E2110` | 6 | 4 per pulse, 5 flames x 32..33 pulses | `mov byte [eax+0x37], 4` @ `0x4AF437` (imm @ `0x4AF43A`) | u8 | imm8, 1..254 |
| Fireball | 0x2B | 0x20 | 100 | `0x4E20C0` | 10 | 40 per explosion, 5 explosions | `mov al, 0x28` @ `0x4AF189` (imm @ `0x4AF18A`) + marker `0x4AE83B` | u8 | imm8 + 16-byte rewrite, 1..254 |
| Slow | 0x2C | 0x40 | 50 | `0x4E2690` | 10 | timer -1000 | - | - | cost only |
| Invisibility | 0x2D | 0x80 | 200 | `0x4E22F0` | 6 | timer 2000 | - | - | cost only |
| Polymorph | 0x2E | 0x100 | 200 | `0x4E2370` | 10 | kills target, creates critter 0x39 | - | - | cost only |
| Blizzard | 0x2F | 0x200 | 25 per wave | `0x4E19A0` | 10 | 10 per impact, 5 chains x 11 impacts per wave | `mov byte [edi+0x37], 0xa` @ `0x4AECAC` (imm @ `0x4AECAF`) | u8 | imm8, 1..254 |
| Eye of Kilrogg | 0x30 | 0x400 | 70 | `0x4E2040` | 0xFF | - | - | - | cost only |
| Bloodlust | 0x31 | 0x800 | 60 | `0x4E1A10` | 6 | timer 750 | - | - | cost only |
| Raise Dead | 0x32 | 0x2000 | 50 per skeleton | `0x4E2420` | 6 | - | - | - | cost only |
| Death Coil | 0x33 | 0x4000 | 100 | `0x4E1A90` | 10 | 50 total, split; caster +total HP | 5 immediates `0x4E1DAA..0x4E1DE7` | imm8 signed | 5 bytes, 1..127 |
| Whirlwind | 0x34 | 0x8000 | 100 | `0x4E27F0` | 12 | 4 per pulse, 400 pulses | `mov byte [esi+0x37], 4` @ `0x4AF62D` (imm @ `0x4AF630`) | u8 | imm8, 1..254 |
| Haste | 0x35 | 0x10000 | 50 | `0x4E2190` | 6 | timer +1000 | - | - | cost only |
| Unholy Armor | 0x36 | 0x20000 | 100 | `0x4E1910` | 6 | halves target HP, immune 500 | - | - | cost only |
| Runes | 0x37 | 0x40000 | 200 (refund `cost/5` per rune not placed) | `0x4E25A0` | 10 | 50 per rune, 5 runes | `mov ecx, 0x32` @ `0x4E2D89` + `add eax, -0x32` @ `0x4E2D9E` | imm32 + imm8 | 2 writes, 1..128 |
| Death and Decay | 0x38 | 0x80000 | 30 per wave | `0x4E2530` | 12 | 10 per pulse, 5 clouds x 10 pulses per wave | `mov byte [esi+0x37], 0xa` @ `0x4AF549` (imm @ `0x4AF54C`) | u8 | imm8, 1..254 |

Order 0x28: cost 5, no action, step function `0x4BE1F0`, ALOW bit 2 has no string in `enUS.json` (`spell_2` is
absent); its name is `[unverified]` (probably a cut spell). Do not give it a config key.

Raw cost table (`0x8C5EB8`, u16, orders 0x00..0x3D):

```
0x8c5eb8: 0 x 38 ...
0x8c5f04 [0x26]: 46 00 05 00 05 00 04 00 50 00 64 00 32 00 c8 00 c8 00 19 00 46 00 3c 00 32 00 64 00 64 00 32 00
0x8c5f24 [0x36]: 64 00 c8 00 1e 00 | 00 00 ... 0x8c5f34: 01 00 00 00 ff ff 00 00   <- a different table (direction steps)
```

`0x8C5F34` / `0x8C5F3C` (read by `FUN_004db970`, `FUN_004e3010`) are direction tables that follow the cost table; the
cost table ends at `0x8C5F29`.

---

## Addresses

### Code

| VA | What | Evidence |
|---|---|---|
| `0x4A8970` | Spell / attack hit-frame dispatcher: `mov eax, [eax*4+0x8C1590]; call eax` at `0x4A8993` (the ONLY reader of the action table) | disasm, `scanwrites` |
| `0x4E2970` | Spell order handler: `if (cheats & 8) mana = 0xFF` (`0x4E2980`), `SetOrder(unit, pendingSpell)`, clear target if `0x8C1804[order]` | decompile |
| `0x4E2B20` | UI spell button: `if (!(cheats & 8) && mana < cost[order])` -> `stat_txt_435` ("Not enough mana to cast spell.") | decompile |
| `0x4E2C60` | Check-and-deduct helper (flame shield only): `if (mana < cost) {SetOrder(2); return 0;} mana -= (char)cost; return 1` | decompile, single caller `0x4E2154` |
| `0x4E2840` / `0x4E2890` / `0x4E2900` | Spell step functions (approach until in range): positional / targeted + target fleshy / targeted + target not a building. None reads mana | decompile |
| `0x4EF480` | Per-step unit timer pass: rune tick, berserker regen, spell timers, **mana regen**, decay | decompile + disasm |
| `0x4EDB10` | CreateUnit: `mana = magic[type]` (`0x4EDC6F`), decaying units `mana = 0xFF` (`0x4EDCF2`), casters `+0x74 = 0x28; mana = 0x55` (`0x4EDF17`, `0x4EDF1B`) | decompile + disasm |
| `0x4ED2C0` | Change unit type (upgrade conversions): `mana = magic[new]`, casters `+0x74 = 0x28; mana = 0x55` (`0x4ED334`, `0x4ED338`) | decompile + disasm |
| `0x4BD8F0` | Damage(attacker, target, u8 dmg): skips dead / flagged targets and targets with unholy armor (`+0x46 != 0`), `hp <= dmg` -> kill, else `hp -= dmg` (u16). Reads the damage as a byte (`mov al, [ebp+0x10]` at `0x4BD960`) | decompile + disasm |
| `0x4AF9E0` | Area hit (missile, alsoAir): walls at the centre tile + 4 neighbours (`FUN_004bd850`), units: centre, then 7x7 tiles of the ground grid (and the air grid if `alsoAir`) through `FUN_004afb50`; every index bounds-checked (`(ushort)x < mapSize`) | decompile |
| `0x4AFB50` | Splash on one unit: skip the missile's own caster (`+0x30`), each unit once per area hit (list `0x91ABC8`, count `0x91AD50`); `d = max(dx², dy²)`: `< 0x200` full `+0x37`, `< 0x700` `+0x37 >> 2`, else nothing; subtract armor only if `0x8C0B0C[missileType] == 0`; `dmg = (d+1)/2 + rand % ((d+1)/2 + 1)` -> `FUN_004bd8f0` | decompile + disasm `0x4AFBD0..0x4AFC2F` |
| `0x4AE6D0` | Missile impact (class 6): non-splash types -> `FUN_004bd8f0(missile+0x30, target, missile+0x37)` (`0x4AE72A`), splash types (`0x8C090C[type] != 0`: 7, 13, 14, 24) -> `FUN_004af9e0` | decompile + disasm |
| `0x4EEE76` | Missile step: `call [0x8C08B0 + 4*missile+0x36]` when the missile's animation step `FUN_00484990` returns nonzero | disasm |
| `0x4AE560` | Missile class init: `+0x36 = 0x8C0ACC[type]` | disasm |
| `0x4AF0E0` / `0x4AE7C0` | Fireball create (type 2, dmg 40) / class 7 flight (fireball, dragon and gryphon missiles) | decompile + disasm |
| `0x4AF040` (+ body `0x4AF06B`) / `0x4AEC70` / `0x4AE990` | Blizzard shard spawn / init (type 5, dmg 10, chain count) / class 10 fall + impact + respawn | decompile + disasm |
| `0x4AF500` / `0x4AEB30` | Death and decay cloud create (type 6, dmg 10, 10 pulses) / class 11 pulse | decompile + disasm |
| `0x4AF5C0` / `0x4AEB70` | Whirlwind create (type 12, dmg 4, 800 ticks) / class 12 wander + pulse | decompile + disasm |
| `0x4AF290` / `0x4AEA40` / `0x4AF3A0` / `0x4AE8E0` | Flame shield bolt (type 3) / arrival: 5 flames / flame create (type 4, dmg 4) / class 9 orbit + pulse | decompile + disasm |
| `0x4AEDF0` | Unit attack missile create (death coil uses it, then overwrites `+0x37`); returns 0 when no slot is free | decompile |
| `0x4E2BA0` / `0x4E2CD0` / `0x4E2CA0` | Rune place / rune tick (start of `FUN_004ef480`) / rune clear (map start, `0x4C4464`) | decompile + disasm |
| `0x4C4960` | Mode setter: unit cap `0x91BFB8` = 1600 and **missile pool `0x91BFBC` = 400** when `0x91C178 != 0` (classic: 600 / 200) | disasm |
| `0x4CAB80` / `0x4CABC0` | Computer caster retreat when `mana < 50` (`cmp byte [reg+0x26], 0x32`) | decompile |

### Data

| VA | What | Type | Evidence |
|---|---|---|---|
| `0x8C5EB8` | **Mana cost by order** | u16[0x39] (0x26..0x38 used) | dump; 46 reads, 0 writes |
| `0x8C1590` | Spell action by order (hit-frame function) | `void(*)(Unit*)`[order] | dump; 1 reader `0x4A8993`, 0 writes. Heal entry `0x8C162C`, exorcism `0x8C1634`, flame shield `0x8C1638` |
| `0x8C1498` | Order handler by order (all spells `0x4E2970`) | fn ptr | dump |
| `0x8C13A0` | Order step action by order | fn ptr | dump |
| `0x8C1744` | Order range in tiles (0xFF = anywhere) | u8[order] | dump |
| `0x8C1804` | "clear target" flag by order | u8[order] | dump, `FUN_004e2970` |
| `0x8C08B0` | Missile class handlers: 0 init `4AE560`, 1 straight `4AE5B0`, 2 arc `4AE620`, 5 end `4AEC30`, 6 impact `4AE6D0`, 7 fireball `4AE7C0`, 8 flame shield bolt `4AEA40`, 9 flame orbit `4AE8E0`, 10 blizzard `4AE990`, 11 death and decay `4AEB30`, 12 whirlwind `4AEB70` | fn ptr[15] | dump; `call` at `0x4EEE7B` |
| `0x8C0ACC` | Missile type -> class: 0:1 1:7 2:7 3:8 4:9 5:10 6:11 7:1 8:1 9:1 10:1 11:5 12:12 13:2 ... 28:13 | u8[32] | dump; read at `0x4AE56F` |
| `0x8C090C` | Missile type is splash (1 for 7, 13, 14, 24) | u8[32] | dump |
| `0x8C0B0C` | Missile type ignores armor on splash (nonzero) / armor subtracted (0: types 7, 13, 14, 24, 31) | u8[32] | dump; read at `0x4AFBF4` |
| `0x91C700` / `0x91BFBC` | Missile array (0x40-byte records) / count (400) | ptr / u32 | allocation `count << 6` at `0x4C6279..0x4C629B`, count at `0x4C498D` |
| `0x918D14` / `0x918D48` / `0x918D80` | Runes: x (u8[50]), y (u8[50]), timer (u16[50], 0x800 at placement) | | `FUN_004e2ba0`, `FUN_004e2cd0`; saved (`0x4AAC74` / `0x4AB697`) |
| `0x9178A0` | UDTA "magic": 1 for 0x0A, 0x0B, 0x15, 0x17, 0x18, 0x33, else 0 | u8[110] | `unitdata.dat` offset 1896 |
| `0x9182A0` | UDTA missile by unit type: death knights (0x0B, 0x15) 10, gryphon rider 0x2A 1, dragon 0x2B 2, Deathwing 0x23 2 | u8[110] | `unitdata.dat` offset 4426 |

Unit types with caster flag 0x20000 (`unitdata.dat` flags at 0x1486): 0x0A mage, 0x0B death knight, 0x0C paladin,
0x0D ogre-mage, 0x15, 0x17, 0x18, 0x2C, 0x31, 0x33, 0x34.

Missile record fields used here: `+0x00/+0x02` pixel x/y, `+0x20` distance left, `+0x28/+0x2A` target point, `+0x2C`
target unit, `+0x30` owner unit (excluded from its own splash, credited with kills), `+0x34` type, `+0x35` flags
(bit 0 = free slot), `+0x36` class, **`+0x37` damage (u8)**, `+0x38` counter (u16).

---

## 1. Mana: evidence

### Storage and every writer

Byte accesses at `[reg+0x26]` in `0x4A0000..0x540000` (capstone linear sweep, `scan26`), grouped:

| Where | Access | Meaning |
|---|---|---|
| `0x4EDC6F`, `0x4EDCF2`, `0x4EDF1B` (CreateUnit) | write | magic flag, 0xFF for decaying units (eye, skeleton), **0x55 for casters** |
| `0x4ED322`, `0x4ED338` (type change) | write | same, 0x55 for casters |
| spell actions `0x4E1910..0x4E27F0`, `0x4E2C60` | read + write | `mana -= (char)cost[order]` after `mana < cost -> SetOrder(2)` |
| `0x4E22C2` heal, `0x4E2B0B` exorcism, `0x4E2511` raise dead | `sub byte` | per-HP / per-skeleton deduction |
| `0x4E265E` runes | `add byte` | refund `(cost/5) * (5 - placed)` |
| `0x4E2980` | write 0xFF | magic cheat (`0x91B270 & 8`) |
| `0x4EF4BF..0x4EF4D4` | read/write | berserker (type 0x13) uses the byte as its regen counter (not a caster) |
| `0x4EF576..0x4EF58A` | read/write | **caster regen** |
| `0x4EF5A7..0x4EF5B0` | read/write | decay (eye / skeleton lifetime) |
| `0x4ABF8B` (`FUN_004abf10`) / `0x4AC2F9` (`FUN_004ac290`) | copy | unit record unpack / pack (`+0x26 <-> record+7`); callers `FUN_004e0ad0` / `FUN_004e1400`, so mana is part of the saved unit `[unverified: caller roles]` |
| `0x4CAB86`, `0x4CABCB`, AI `0x4CAF1A`.. | read | AI checks |
| `0x4E52E3`, `0x4E5354`, `0x4E5A26`, `0x4E673E`, `0x4E68B2`, `0x4E6A24`, `0x4E6A7B`, `0x4E6C3F`, `0x4E6CDF`, `0x4E6D60`, `0x4E6DD5` | read | classic status panel (value, "Magic:" string 0x1A1, bar, redraw tests) |
| `0x4EFCEF` | read | overhead bar `FUN_004efb00` |
| `0x52C340`, `0x52D87F`, `0x52D8AD` | read | Remastered panels |
| `0x4ED5E6..0x4ED67B`, `0x4AD2AF`, `0x4AD2EC` | write | the same byte on BUILDINGS: construction / production progress 0..255, not mana |

Hits at `0x51E526`, `0x51EC0E`, `0x521FA8` are a lobby / settings serializer on a different struct (not units).
Code above `0x540000` was not swept `[unverified]`.

### Regeneration (`FUN_004ef480`, once per simulation step for every live unit)

```
0x4ef569: test dword ptr [eax*4 + 0x9185f0], 0x20000   ; caster flag
0x4ef574: je 0x4ef58f                                  ; not a caster -> decay branch
0x4ef576: 8a 4e 26        mov cl, byte ptr [esi + 0x26]
0x4ef579: 80 f9 ff        cmp cl, 0xff                 ; full -> nothing
0x4ef57c: 74 6f           je 0x4ef5ed
0x4ef57e: 80 6e 74 01     sub byte ptr [esi + 0x74], 1
0x4ef582: 75 69           jne 0x4ef5ed
0x4ef584: fe c1           inc cl                       ; +1 mana
0x4ef586: c6 46 74 28     mov byte ptr [esi + 0x74], 0x28   ; next one in 40 steps
0x4ef58a: 88 4e 26        mov byte ptr [esi + 0x26], cl
```

Initial counters: `0x4EDF17: c6 46 74 28` (create), `0x4ED334: c6 42 74 28` (type change). `+0x74` is the decay
counter for non-casters and a harvest counter for workers (`0x4C9ACA`, `0x4D85EB`, `0x4D8757`); casters use it only
here. The unit record copy in `FUN_004abf10` restores it from a save (`0x4AC134`).

Steps per second by speed setting (`data_tables.md`): 12.5 .. 76.9; 40 steps = 3.2 s (slowest) .. 1.6 s (setting 4)
.. 0.52 s (fastest) per mana; 0 -> 255 takes 10200 steps.

### Initial mana

```c
// FUN_004edb10 (CreateUnit), mobile types (< 0x3A or > 0x68):
if (typeflags[type] & 0x20000) { unit[0x74] = 0x28; unit->mana = 0x55; }   // 0x4EDF17 / 0x4EDF1B
// FUN_004ed2c0 (type change, e.g. knight -> paladin, ogre -> ogre-mage via FUN_004eda50):
unit->mana = magic[newType];
if (typeflags[newType] & 0x20000) { unit[0x74] = 0x28; unit->mana = 0x55; }   // 0x4ED334 / 0x4ED338
```

So researching the paladin / ogre-mage upgrade sets every converted unit to 85 mana. The UDTA magic byte never
survives for a caster.

### UI: 255 is "full" everywhere

```c
// FUN_004e67f0 classic mana bar
FUN_004a5a60(&rect, 0xba, 99, 0xff, selected->mana);
// FUN_0052c270 / FUN_0052d7a0 Remastered
FUN_005b17b0(DAT_0095c7a8, selected->mana, 0x100, 0);
FUN_005b17b0(DAT_0095c7a8, selected->mana, 0xff, 0);
_sprintf_s(buf, 0x14, "%d", (uint)selected->mana);
// FUN_004efb00 overhead bar: FUN_0050e350((char)unit->mana, 0xff, ...)
```

### Verdict

- Ceiling: 255, hard (byte storage, `cmp cl, 0xff`, UI scale, AI "full" tests, cheat). Widening is impossible without
  moving the field.
- Regen multiplier: feasible. In place: reload value `0x28` -> `round(40 / k)` at three sites (exact for k = 2, 4, 5,
  8, 10, 20, 40). Do NOT change `inc cl` into an add: the `== 0xFF` guard only works for +1 steps (254 + 2 wraps to 0).
- Initial mana: the `0x55` byte at two sites, 0..255.

---

## 2. Cost table: evidence

All readers (the linear sweep `scanwrites` over the whole `.text` finds 46 accesses, all reads; the raw 4-byte scan
`find_refs 0x8C5EB8 0x8C5F2A` finds the same 46):

| Reader | How |
|---|---|
| 13 actions via `cost[unit->order]` (`0x4E192E`, `0x4E19AE`, `0x4E1A2E`, `0x4E1AC0`, `0x4E2051`, `0x4E20D1`, `0x4E21AE`, `0x4E230E`, `0x4E238E`, `0x4E253E`, `0x4E25B1`, `0x4E26AE`, `0x4E272F`, `0x4E2801`) + `0x4E2C6E` (flame shield helper) | `movzx edx, word [eax*2+0x8c5eb8]`; `if (mana < cost) SetOrder(2) else mana -= (char)cost` |
| Heal `0x4E2277` (word, divisor), `0x4E22B5` (byte, multiplier) | fixed address `0x8C5F06` |
| Exorcism `0x4E1F24` (ring loop guard), `0x4E2ABD` (divisor), `0x4E2AFA` (byte multiplier) | `0x8C5F0A` |
| Raise dead `0x4E2459` (loop guard), `0x4E250C` (byte, per skeleton) | `0x8C5F1C` |
| Runes `0x4E2637` refund | `0x8C5F26 * 0xCCCCCCCD >> 34` = `cost / 5` (no `div`) |
| UI click `0x4E2B3D` | u16 compare |
| Classic status line `0x4E80BC` | **`movzx eax, byte ptr [eax*2 + 0x8c5eb8]`** (low byte only) |
| Remastered tooltip `0x52E295` | u16, `local_24[3]`; name / text come from `spell_%d` / `spell_%d_tooltip` strings |
| AI `FUN_004cb030` `0x4CB055`, `FUN_004cb0e0` `0x4CB104` | `cost[order] <= mana` before casting |
| AI mage `FUN_004cb480`, paladin `FUN_004cb2f0`, ogre `FUN_004cb200`, death knight `FUN_004cac80` | fixed addresses; **blizzard and death and decay need `3 * cost <= mana`** (`0x4CB496`, `0x4CAC97`) |

The mod's own autocast reads the table too (`src/autocast.cpp` `TryCast` / `TryRaiseDead`), so it follows any edit.

Not saved and not reloaded:

- The table lies outside both UDTA / UGRD descriptor ranges (`0x917128..0x9187A8`, `0x9188A8..0x918AE8`,
  `data_tables.md`) and the savegame pack / unpack functions do not reference it (no reference from `0x4AA6E0`,
  `0x4AB090`, `0x4E0C40`, `0x4C4C60`; the complete reference list is above).
- No instruction writes it, so the exe image is its only initialiser.

Consequence: the table keeps whatever the mod last wrote until the process exits (new maps and loaded saves alike).
The mod must remember the pristine values and always write `f(base, config)`.

Human cast path: the click check (`FUN_004e2b20`) happens once; the step functions do not check mana; the action at
the hit frame checks again and cancels (`SetOrder(unit, 2)`) if mana dropped. Channelled spells (blizzard, death and
decay) do not `SetOrder(2)` after a successful wave, so the next hit frame deducts again until `mana < cost`.

What each cost value does:

| Cost | Effect |
|---|---|
| 0 | Heal / Exorcism: `div ecx` with ecx = 0 at `0x4E2284` / `0x4E2AC4` = integer divide exception, **crash**. Blizzard / Death and Decay: never run out, channel until interrupted. Raise Dead: raises every corpse within `dx²+dy² < 37` tiles. AI: every threshold passes. |
| 1..255 | Normal. Deduction `sub byte, (char)cost` cannot underflow because `mana >= cost` is checked first; heal / exorcism deduct `units * cost <= mana`. Runes refund `floor(cost/5) * failed <= cost`, so mana never exceeds 255. |
| 256+ | `mana < cost` is always true: the spell can never be cast (button shows "Not enough mana"), the AI never casts it, the classic status line shows `cost & 0xFF`. |

Cost changes are safe mid-game: nothing caches a cost in a unit.

---

## 3. Damage and heal: evidence

### Shared path for missile spells

```c
// FUN_004af9e0(missile, alsoAir): the area hit
bd850(tx, ty, dmg); bd850(tx±1, ty, dmg>>2); bd850(tx, ty±1, dmg>>2);   // walls only, each index checked
if (tx < mapSize && ty < mapSize) afb50(missile, groundGrid[ty*mapSize + tx]);
for (y = ty-3 .. ty+3) for (x = tx-3 .. tx+3)                            // (ushort) compares against mapSize
    if (x < mapSize && y < mapSize) { afb50(missile, groundGrid[...]); if (alsoAir) afb50(missile, airGrid[...]); }

// FUN_004afb50(missile, unit)
if (!unit || unit == missile->owner || alreadyHitThisCall(unit)) return;
d = max(dx*dx, dy*dy);                         // missile pixel vs unit centre
if (d >= 0x700) return;                        // ~42 px
dmg = (d < 0x200) ? missile->dmg : missile->dmg >> 2;   // full within ~22 px
if (armorFlag[missile->type] == 0) { dmg -= Armor(unit); if (dmg < 1) return; }
h = (dmg + 1) / 2;  Damage(missile->owner, unit, (u8)(h + rand() % (h + 1)));
```

- No owner or alliance test: **friendly fire** on every unit in range except the missile's own `+0x30` unit.
- Spell missile types 2, 4, 5, 6, 12 have `0x8C0B0C[type] = 1`: **armor ignored**. (Type 3, the flame shield bolt,
  never hits: it only spawns the flames.)
- `h + rand % (h+1)` is at most `2h`; for `dmg = 255`, `h = 128` and the result can be 256, which the damage
  function reads as the byte 0 (`mov al, [ebp+0x10]`) = no damage. **Keep missile damage <= 254.**
- `FUN_004bd8f0`: `hp <= dmg` kills, else `hp -= dmg` (u16). Damage above HP is clean. Units with unholy armor
  (`+0x46 != 0`) take nothing.
- Wall damage (`FUN_004bd850`) grows with the damage byte (`dmg >> 2`, at least 1, wall destroyed above 0x13): more
  spell damage also breaks walls faster.

Missile pacing: the class handler runs when the missile's animation step returns nonzero (`0x4EEE6A..0x4EEE82`), so
"tick" below means one call of the class handler; its real-time rate is animation-driven `[unverified]`. The COUNTS
are exact.

### Fireball (order 0x2B)

```
; FUN_004af0e0 (only caller: the fireball action 0x4E20C0)
0x4af17e: mov al, [edi+0x27]        ; caster type
0x4af181: cmp al, 0x2b / je         ; dragon or gryphon rider caster: damage = FUN_004bdbd0(caster)
0x4af185: cmp al, 0x2a / je
0x4af189: b0 28          mov al, 0x28        ; spell damage 40
0x4af196: 88 46 37       mov byte ptr [esi + 0x37], al
0x4af199: xor eax, eax / mov [esi+0x38], ax  ; counter 0; missile type 2, class 7
```

```
; FUN_004ae7c0, class 7 (fireball spell AND dragon / Deathwing (type 2) and gryphon (type 1) attack missiles)
0x4ae82d: cmp si, 0x32 / jge 0x4ae8da        ; still more than 50 px out: just fly
0x4ae837: movzx edx, word [edi+0x38]
0x4ae83b: b8 28 00 00 00  mov eax, 0x28
0x4ae840: 38 47 37        cmp byte ptr [edi + 0x37], al   ; damage == 40 means "this is the fireball spell"
0x4ae843: b9 19 00 00 00  mov ecx, 0x19
0x4ae848: 0f 44 c8        cmove ecx, eax                  ; trail limit 40 for the spell, 25 for dragons / gryphons
0x4ae84b: movsx eax, dx / cmp eax, ecx / jle 0x4ae86d
          ...  end (state 6)
0x4ae86d: lea eax, [edx+1] / mov [edi+0x38], ax / test al, 7 / jne    ; every 8th tick:
0x4ae8a2: call 0x4af9e0 (push 1)                          ; area hit incl. flyers
```

The missile keeps flying past the target at half speed; the spell explodes at counter 8, 16, 24, 32, 40 = **5 area
hits of 40**. **Trap:** the damage byte doubles as the spell marker. Change 40 without changing the compare and the
fireball drops to the dragon trail (explosions at 8, 16, 24 = 3 hits). Change the compare's `mov eax` immediate and
the trail length changes too, because the same register is the limit. The fix is the rewrite in section 4.

### What the panels print as the mana cost

Both status panels read the **same live table** the mod writes, indexed by the button record's spell order byte
(`+0x11` of the 24-byte record, `0x8C5F48[type]` button sets):

```
classic FUN_004e7fa0:  0x4e80b8  movzx eax, byte [edi + 0x11]        ; the button's order id
                       0x4e80bc  movzx eax, byte [eax*2 + 0x8c5eb8]  ; the cost table, byte wide
                       0x4e80c5  call 0x4e9ca0                       ; the cost widget (hidden when 0)
Remastered FUN_0052e080: 0x52e299 reads the same entry word wide into the format arguments
```

So `[spell_cost]` shows up in the tooltip by itself: there is no display copy to keep in step, unlike the range
upgrade bonus (`0x8C11E4`). The classic panel reads a **byte**, which is another reason to keep costs <= 255.

**Heal and Exorcism are per hit point.** `FUN_004e2220` (heal) computes `hp = mana / [0x8C5F06]`, caps it at 0x28 and
adds it to the target; `FUN_004e2a70` (exorcism) computes `damage = mana / [0x8C5F0A]`. The number in the table, and
therefore the number on the button, is the price of one hit point; a cast costs that times the hit points it moves.
A player who sets `heal = 2` sees "2" on the button and watches 60 mana go into a 30 hit point wound: the display is
right, the pricing is per hit point.

### Blizzard (order 0x2F)

Action `0x4E19A0`: check / deduct 25, five `FUN_004af040(caster, 10)` (`push 0xa` at `0x4E19D9..0x4E19F9` = chain
count, not damage), no `SetOrder(2)` (channel). Each shard: `FUN_004aec70` sets type 5, `0x4aecac: c6 47 37 0a  mov
byte ptr [edi + 0x37], 0xa`, target point = order tile + random 0..4 tiles - 1.5 tiles. `FUN_004ae990` (class 10) on
arrival: area hit (with flyers), then `if (count) { count--; respawn a shard at the same target point }`: counts
10..0 = **11 impacts per chain, 5 chains per wave, 10 damage each** (random 5..10 full, 1..2 quarter). Caster
excluded, everyone else hit.

### Death and Decay (order 0x38)

Action `0x4E2530`: check / deduct 30, five `FUN_004af500(caster, 10)`, channel. `FUN_004af500`: type 6 (`mov word
[esi+0x34], 0x206`), `0x4af549: c6 46 37 0a  mov byte ptr [esi + 0x37], 0xa`, `+0x38 = 10`, position = order tile
+/- 2 tiles random. `FUN_004aeb30` (class 11): `if (+0x38 == 0) end; else { +0x38--; area hit }` = **10 pulses of 10
per cloud, 5 clouds per wave**.

### Whirlwind (order 0x34)

Action `0x4E27F0`: deduct 100, `FUN_004af5c0`: type 12, `+0x38 = 800`, `0x4af62d: c6 46 37 04  mov byte ptr [esi +
0x37], 4`. `FUN_004aeb70` (class 12): `if (+0x38 & 1) area hit; if (--+0x38 == 0) end;` wander to a random point
within +/-64 px when a waypoint is reached = **400 pulses of 4** (2..4 full, 1..2 quarter). Uncontrollable, hits
everyone but the caster.

### Flame Shield (order 0x2A)

Action `0x4E2110`: target alive, `target+0x4E == 0`, target not a flyer (flag 2); `FUN_004e2c60` deducts 80;
`target+0x4E = 500` (the "shielded" timer, blocks recasting); bolt `FUN_004af290` (type 3). On arrival
(`FUN_004aea40`) five `FUN_004af3a0(target, phase, p)` with (phase, p) = (0,0), (10,4), (20,2), (30,3), (40,5):

```
0x4af433: c6 40 34 04   mov byte ptr [eax + 0x34], 4    ; type 4, class 9
0x4af437: c6 40 37 04   mov byte ptr [eax + 0x37], 4    ; damage 4
          +0x30 = the shielded unit (so it is the one excluded and credited), +0x38 = 3*p + 0x200
```

`FUN_004ae8e0` (class 9): while the shielded unit lives, `--counter`, orbit, and when `(counter & 0xF) == 1` an area
hit (with flyers). Counters 512, 524, 518, 521, 527 give **32 / 33 / 33 / 33 / 33 pulses of 4** (164 total).
(`cmp ax, 0x33` at `0x4AE910` is the 51-step orbit table wrap, not damage.)

### Death Coil (order 0x33), `FUN_004e1a90`

```c
deduct 100; SetOrder(caster, 2);
for (y = oy-2 .. oy+2) for (x = ox-2 .. ox+2)                    // (unsigned) x < mapSize && y < mapSize
  for (grid in {ground, air})
    if (u && (!remastered || u->owner > 7 || !allied[caster][u]) && u->owner != caster->owner
        && alive(u) && (flags[u->type] & 0x8000000 /*fleshy*/))
      rec[n++] = {x, y, u->hp, grid};                              // NO bound on n
sort rec by hp ascending;
sum = 0;  // budget 50: give each target min(hp, 50 - sum) until the budget is used
for each chosen rec: caster->target = u; m = FUN_004aedf0(caster); m->dmg(+0x37) = (u8)share;   // no NULL check
caster->hp = min(caster->hp + sum, GetMaxHp(caster));
```

Budget immediates (5 sites, one number):

```
0x4e1daa: 83 f8 32         cmp eax, 0x32          ; imm8, SIGN-EXTENDED
0x4e1dc5: 83 fb 32         cmp ebx, 0x32          ; imm8, sign-extended
0x4e1ddb: 83 f8 32         cmp eax, 0x32          ; imm8, sign-extended
0x4e1de0: b8 32 00 00 00   mov eax, 0x32          ; imm32
0x4e1de7: bb 32 00 00 00   mov ebx, 0x32          ; imm32
0x4e1e52: 88 4e 37         mov byte ptr [esi + 0x37], cl   ; esi = FUN_004aedf0 result, not tested for 0
```

The missiles are unit attack missiles of the death knight's type (UDTA missile 10, non-splash), so impact is
`FUN_004bd8f0(caster, target, share)` directly (`0x4AE72A`): armor ignored, one target each. The caster heals the
full planned total at cast time, before the missiles land, clamped to max HP (32-bit compare at `0x4E1E7A`).

Vanilla crash paths (present before any mod, not moved by changing the budget):
- The record buffer is 25 entries (`ebp-0x194 .. ebp-0x4`, stack cookie at `ebp-4`, frame `sub esp, 0x1bc`), the
  scan can find 50 (25 tiles x 2 grids) and has no count check (`inc dword [ebp-0x198]` at `0x4E1C1C` / `0x4E1CED`).
  26+ eligible enemies in the 5x5 = stack cookie failure.
- `FUN_004aedf0` returns 0 when all 400 missile slots are busy; `0x4E1E52` then writes to address 0x37. A larger
  budget gives more targets per cast (each takes at most its HP), so more missiles per cast: slightly more exposure.

### Runes (order 0x37)

Action `0x4E25A0`: deduct 200, `FUN_004e2ba0` at the order tile and its 4 neighbours (each: on the map, no ground unit
there, no rune already there, a free slot of 50 shared by all players; timer 0x800), then refund
`(cost/5) * (5 - placed)`. Tick `FUN_004e2cd0` (first call in the unit timer pass, every step):

```
0x4e2d39: movzx ecx, word [0x918d10] / mov eax, [0x91ad6c] / imul ecx, edi / add ecx, edx
0x4e2d4a: mov edi, [eax + ecx*4]          ; ground grid only: flyers never trigger runes
0x4e2d51: cmp esi, 0x32 / jae 0x4e2db6    ; rune index guard (never true), -> runtime check abort
          ... clear rune, explosion effect
0x4e2d7e: cmp word ptr [edi + 0x46], 0 / jne   ; unholy armor: immune
0x4e2d85: movzx eax, word ptr [edi + 0x22]
0x4e2d89: b9 32 00 00 00  mov ecx, 0x32        ; rune damage
0x4e2d8e: 66 3b c8        cmp cx, ax
0x4e2d91: 72 0b           jb 0x4e2d9e          ; hp > 50 -> subtract
0x4e2d93: push edi / call 0x4ee380             ; else kill
0x4e2d9e: 83 c0 ce        add eax, -0x32       ; imm8 sign-extended
0x4e2da1: 66 89 47 22     mov word ptr [edi + 0x22], ax
```

**50 per rune**, direct HP loss (no `FUN_004bd8f0`: no attacker credit, no armor), **any owner** including the
caster's own units, ground and sea units only. The rune coordinates were bounds-checked at placement
(`FUN_004e2ba0`); runes are saved with the game and cleared at map start (`FUN_004e2ca0` from `0x4C4464`).

### Exorcism (order 0x29)

`FUN_004e1ec0`: `SetOrder(caster, 2)`; four passes `i = 0..3` over the square ring at Chebyshev distance `i` around the
order tile (7x7 in total, ground grid only), each pass starting with `if (mana < cost) return` (`0x4E1F24`); every
index is tested `(ushort)x < mapSize && (ushort)y < mapSize`. Ring 0 visits the centre tile twice (rows `y+0` and
`y-0`). Per unit, `FUN_004e2a70`:

```
if (!u || !(flags[u->type] & 0x8000 /*undead*/)) return;
if (remastered && u->owner < 8 && allied[caster][u]) return;
0x4e2ab7: movzx eax, byte [edi+0x26] / movzx ecx, word [0x8c5f0a] / div ecx     ; units = mana / cost
0x4e2ae4: cmp dx, bx / cmovbe ...                                                ; dmg = min(units, hp)
0x4e2af5: call 0x4bd8f0(caster, u, dmg)
0x4e2b07: imul ecx, byte [0x8c5f0a] / sub byte [edi+0x26], cl                    ; mana -= dmg * cost
```

**1 HP per 4 mana**, armor ignored, the damage is limited only by mana and the target's HP. Mana is deducted even if
the damage function refuses the hit (unholy armor, already dying) `[unverified: whether a dying unit stays in the grid
for the second centre visit]`.

### Heal (order 0x27), `FUN_004e2220`

```
SetOrder(caster, 2); missing = (u16)(GetMaxHp(t) - t->hp); if (!missing) return;
0x4e2277: movzx ecx, word ptr [0x8c5f06] / xor edx, edx / movzx eax, byte ptr [esi+0x26]
0x4e2284: f7 f1           div ecx                     ; units = mana / cost
0x4e2289: b8 28 00 00 00  mov eax, 0x28               ; per-cast cap 40
0x4e228e: 3b d0 / 0f 47 d0   cmp edx, eax / cmova edx, eax
0x4e2296: cmp [ebp-4], dx / cmovbe ecx, eax           ; min(.., missing)
0x4e22ab: 8d 04 19        lea eax, [ecx + ebx]        ; hp += units
0x4e22b5: movzx eax, byte [0x8c5f06] / imul ecx, eax / sub byte [esi+0x26], cl  ; mana -= units * cost
```

**1 HP per 5 mana, at most 40 HP per cast, never above max HP** (the `missing` clamp). The step function
(`0x4E2890`) requires a fleshy target. Vanilla quirk: `missing` is u16, so a unit whose HP already exceeds its max
(only possible through odd save / table mixes) wraps to a large value and can be overhealed by up to 40.

### Numbers that are NOT damage (for reference)

Timers written by the actions (u16 fields counted down by `FUN_004ef480`): haste `+0x4A = 1000` (or `+1000` over a
slow), slow `-1000`, invisibility `+0x44 = 2000`, bloodlust `+0x48 = 750`, unholy armor `+0x46 = 500` plus
`hp >>= 1` if `hp > 1`, flame shield `+0x4E = 500`. Tooltip texts in `Data\Strings\enUS.json` are static and quote
numbers: heal "1 hit points per 5 mana", exorcism "1 hit point damage per 4 mana", fireball "34 hit point damage"
(the code says 5 x 40 before the random roll), runes "50 hit point damage". They will not follow the mod.

---

## 4. Options and recommendation

### Costs: (a) data write to `0x8C5EB8`, synced every tick and at map load (recommended)

- Write `u16` at `base + 0x4C5EB8 + 2*order` for the 18 orders 0x26, 0x27, 0x29..0x38. `.data` is writable: no
  `VirtualProtect`.
- Base values: capture the 19 words once, before the first write (hook install), and verify them against the vanilla
  list `70,5,5,4,80,100,50,200,200,25,70,60,50,100,100,50,100,200,30`; on mismatch refuse the feature and log (a game
  patch moved something).
- Value: `v = (abs >= 0 ? abs : base)`, then `ceil(v * multiplier)`, clamp **1..255**.
- Sync function `SyncSpellCosts(bool multiplayer)`: desired = base when multiplayer or the feature is off, else the
  computed value; write only when different. Call it from the map-load hook (with `0x922F5B`) and from the tick
  BEFORE the multiplayer early return, exactly like `SyncRangeBonus` (`src/mod.cpp` line 86). Because the table is not
  saved, the tick write can never double-apply, a loaded save gets the current config on its first step, and a hot
  reload takes effect within one step. A multiplayer game started from a savegame never passes the new-map hook, the
  tick-time restore covers it (the AI tick runs before the unit pass in each step, `0x4C5001` vs `0x4C503B`).
- What changes with it: human casting, the AI, the mod's autocast, both tooltips. Nothing else caches a cost.
- AI side effects: blizzard / death and decay need `3 * cost` mana, so with a cost above 85 the computer never casts
  them; the AI's "retreat below 50 mana" rule (`0x4CAB86`, `0x4CABCB`) does not scale with costs.
- Global: computer players pay the same costs.

### Damage: (b) byte-verified in-place immediates (recommended), synced like `SyncRangeBonus`

All bytes below are file bytes at the preferred base; none contains an absolute address, so no relocation fix-up is
involved. Each site: accept the original bytes or the bytes the mod last wrote, otherwise mark the site refused and
log once (same rule as `SyncRangeBonus`). Write with `VirtualProtect` + `FlushInstructionCache`. Desired = original
bytes in multiplayer or when the feature is off.

| Spell (base) | VA of instruction | Original bytes | Write | Range |
|---|---|---|---|---|
| Fireball (40) | `0x4AF189` | `B0 28` | byte `0x4AF18A` = D | 1..254 |
| Fireball marker | `0x4AE83B` (16 bytes) | `B8 28 00 00 00 38 47 37 B9 19 00 00 00 0F 44 C8` | `B0 DD 38 47 37 6A 19 59 75 03 6A 28 59 90 90 90` with `DD` = D | same D |
| Blizzard (10) | `0x4AECAC` | `C6 47 37 0A` | byte `0x4AECAF` | 1..254 |
| Death and Decay (10) | `0x4AF549` | `C6 46 37 0A` | byte `0x4AF54C` | 1..254 |
| Whirlwind (4) | `0x4AF62D` | `C6 46 37 04` | byte `0x4AF630` | 1..254 |
| Flame Shield (4) | `0x4AF437` | `C6 40 37 04` | byte `0x4AF43A` | 1..254 |
| Death Coil (50) | `0x4E1DAA`, `0x4E1DC5`, `0x4E1DDB`, `0x4E1DE0`, `0x4E1DE7` | `83 F8 32`, `83 FB 32`, `83 F8 32`, `B8 32 00 00 00`, `BB 32 00 00 00` | bytes `0x4E1DAC`, `0x4E1DC7`, `0x4E1DDD`, `0x4E1DE1`, `0x4E1DE8` = D (the imm32 upper bytes stay 0) | **1..127** |
| Runes (50) | `0x4E2D89`, `0x4E2D9E` | `B9 32 00 00 00`, `83 C0 CE` | byte `0x4E2D8A` = D, byte `0x4E2DA0` = `(u8)(-D)` | **1..128** |
| Heal cap (40) | `0x4E2289` | `B8 28 00 00 00` | byte `0x4E228A` = C | 1..255 |

Fireball marker rewrite, decoded:

```
0x4ae83b: b0 DD        mov al, D                  ; compare against the new spell damage
0x4ae83d: 38 47 37     cmp byte ptr [edi+0x37], al
0x4ae840: 6a 19        push 0x19                  ; dragon / gryphon trail (flags untouched by push / pop)
0x4ae842: 59           pop ecx
0x4ae843: 75 03        jne 0x4ae848
0x4ae845: 6a 28        push 0x28                  ; spell trail stays 40
0x4ae847: 59           pop ecx
0x4ae848: 90 90 90
0x4ae84b: movsx eax, dx ...                       ; unchanged; eax is overwritten here, so its old value is not needed
```

Same 16 bytes, stack balanced on both paths, `ecx` = 40 / 25 exactly as before. A branch scan of the whole `.text`
(decoded sweep + raw rel32) finds no jump into `0x4AE83C..0x4AE84A`, and none into the runes window. With D = 40 the
rewrite behaves exactly like the original, so it can stay in place whenever the feature is on. Remaining quirk
(vanilla, unchanged): a dragon or gryphon whose attack damage happens to equal D gets the long trail.

Runes alternative for values above 128: replace `83 C0 CE` at `0x4E2D9E` by `2B C1 90` (`sub eax, ecx; nop`); `ecx`
still holds D on that path (the `jb` target is the first byte of this instruction), then only the imm32 matters and
the range is 1..65535 (the compare is 16-bit). One more instruction rewrite for a range nobody needs; the byte form
is the recommendation.

Value rule: `v = (abs >= 0 ? abs : base)`, `round(v * multiplier)`, clamp to the site's range. Values change new
missiles only; a missile in flight keeps its byte (a fireball created before a hot reload may get the short trail
once).

### Heal and Exorcism: price and cap

Their damage per mana and their cost are the same number (the table entry is mana per HP), so the damage multiplier
`k` can act through the price without new code:

- price = `clamp(ceil(costValue / k), 1, 255)`, where `costValue` is the [spell_cost] result for that spell; the
  table gets the price.
- Heal cap = `clamp(round(capValue * k), 1, 255)`, `capValue` = the [spell_damage] heal value (-1 = 40).
- Granularity: heal 5 -> k 2: 3 (x1.67), k 3: 2 (x2.5), k 5: 1 (x5, the maximum); exorcism 4 -> k 2: 2, k 4: 1 (x4,
  the maximum). The tooltip then shows the price.
- Exorcism has no absolute damage number; a per-spell absolute value for it can only mean "mana per HP".

Exact alternative (c): swap the action pointers `0x8C162C` (heal, original `0x004E2220`) and `0x8C1634` (exorcism,
original `0x004E1EC0`) for mod functions `void __cdecl (Unit*)`. The table is plain `.data` (no `VirtualProtect`),
has one reader (`mov eax, [eax*4+0x8C1590]` at `0x4A8993`, `call eax` at `0x4A899A`, no CFG in this exe), is not saved, and is restored by writing
`base + 0xE2220` / `base + 0xE1EC0` back. A mod heal would compute `hp = min(units * k, capValue * k, missing)` and
charge `ceil(hp / k) * cost`, calling `SetOrder` (`0x4EF080`), `GetMaxHp` (`0x4EE1F0`), the effect `0x4AF4B0(t, 9)`
and the sound `0x4C82B0(t, 6)` like the original. An exorcism replacement would have to reproduce the ring scan (or,
narrower, redirect the four `call 0x4E2A70` at `0x4E1F60`, `0x4E1F86`, `0x4E1FD0`, `0x4E1FFB`). More code on a game
path; only worth it if the price granularity is not acceptable.

Not recommended: `lea eax, [ecx+ebx]` -> `lea eax, [ebx+ecx*2]` (`8D 04 19` -> `8D 04 4B`) would double heal per unit
in place, but the `missing` clamp still counts units, so it overheals above max HP by up to `k-1` per cast.

### Mana

- **Regen (in place, global):** bytes `0x4EF589`, `0x4EDF1A`, `0x4ED337` (originals `C6 46 74 28`, `C6 46 74 28`,
  `C6 42 74 28`) = `clamp(round(40 / k), 1, 255)`. Never 0 (the down-counter would wrap to 256 steps).
- **Regen (mod-side, player or hero only):** in the tick, single player, for each own unit with the caster flag and
  `mana < 255`, run `k-1` extra decrements of `+0x74`; each time it reaches 0, `mana += 1` (stop at 255) and reload
  40. Same arithmetic as the game, no code patch, filters possible.
- **Initial mana (in place, global):** bytes `0x4EDF1E`, `0x4ED33B` (originals `C6 46 26 55`, `C6 42 26 55`), 0..255.
  Applies to units created or converted afterwards; existing units keep their saved mana.
- **Maximum mana:** not possible.

### Why no data-table option for damage

There is no damage table: the UDTA / UGRD descriptor lists (`data_tables.md`) have no spell columns, the only per-type
missile tables (`0x8C0ACC`, `0x8C090C`, `0x8C0B0C`, `0x8C08EC`) hold classes and flags, and every damage value is an
immediate in a creation function. The code patches are not in any savegame.

---

## Recipe

1. `game.h`: `kRvaSpellActionTable = 0x4C1590` (fn ptr[order]); cost table already there (`kRvaManaCostByOrder`).
   Add one entry per patch site from the damage table and the mana list above, each with its original bytes.
2. `selftest`: byte-check every site against the exe file: all rows of the damage table; `0x4AE83B` 16 bytes;
   `0x4EF586 C6 46 74 28`, `0x4EF579 80 F9 FF`, `0x4EF584 FE C1`; `0x4EDF17 C6 46 74 28 C6 46 26 55`;
   `0x4ED334 C6 42 74 28 C6 42 26 55`; the 19 cost words at `0x8C5F04`; dwords `0x8C162C = 0x004E2220`,
   `0x8C1634 = 0x004E1EC0`; `0x4A8993 8B 04 85 90 15 8C 00 FF D0`.
3. `datatweaks`: `SyncSpellCosts(multiplayer)` and `SyncSpellDamage(multiplayer)` (and `SyncManaPatches`), each
   idempotent, called from `OnNewMapTablesLoaded` and from `mod::OnTick` next to `SyncRangeBonus`, before the
   multiplayer return.
4. Config: `[spell_damage] multiplier` + per-spell keys fireball, flame_shield, blizzard, death_and_decay,
   whirlwind, death_coil, runes, heal (cap), exorcism; `[spell_cost] multiplier` + the 18 spell keys; -1 = the
   game's value; clamps per the tables; four places per new key (`CLAUDE.md`).
5. Log once per map load: the effective cost and damage per spell, and every refused site.
6. After a game patch: the cost table by value pattern (`RE_NOTES.md`), the action table from the only
   `call [reg*4+table]` in the hit-frame dispatcher, each damage site from its action function (fireball
   `mov al, 0x28` next to `cmp al, 0x2a`, blizzard / death and decay / whirlwind / flame `mov byte [reg+0x37], imm`,
   death coil the five `0x32`, runes `mov ecx, 0x32` in the function called first by the unit timer pass, heal
   `mov eax, 0x28` after the `div`), regen `mov byte [reg+0x74], 0x28` after `cmp cl, 0xff`.

## Risks and traps (complete list for these code paths)

| # | Trap | Where | Guard |
|---|---|---|---|
| 1 | Cost 0: divide by zero | `div ecx` at `0x4E2284` (heal), `0x4E2AC4` (exorcism) | cost >= 1 |
| 2 | Cost 0: endless channel, free raise dead / everything | blizzard / D&D actions, `0x4E2420` | cost >= 1 |
| 3 | Cost > 255: spell uncastable, classic status line shows the low byte | byte mana compare, `0x4E80BC` | cost <= 255 |
| 4 | Missile damage 255: roll can reach 256 = byte 0 | `0x4AFC0D..0x4AFC2A`, `mov al, [ebp+0x10]` at `0x4BD960` | <= 254 |
| 5 | Death coil budget in signed imm8 compares | `0x4E1DAA`, `0x4E1DC5`, `0x4E1DDB` | <= 127 |
| 6 | Death coil share stored as a byte | `0x4E1E52` | follows from 5 |
| 7 | Rune subtract is a signed imm8 | `0x4E2D9E` | <= 128 (or the `sub eax, ecx` form) |
| 8 | Fireball damage doubles as the spell marker | `0x4AE840` | the 16-byte rewrite |
| 9 | Heal: overheal if HP per unit is scaled in place | `0x4E22AB` | use the price route or a replacement |
| 10 | Regen: `+1` step is what makes `== 0xFF` a cap | `0x4EF579`, `0x4EF584` | change only the interval |
| 11 | Regen interval 0 = 256 steps | `+0x74` down-counter | interval >= 1 |
| 12 | Static patches outlive the game in the process | cost table, code bytes | sync every tick and at map load, vanilla in multiplayer, verify bytes before writing |
| 13 | Death coil stack buffer of 25, no count check | `0x4E1C1C`, `0x4E1CED` | vanilla; not affected by any number |
| 14 | Death coil does not test the missile pointer | `0x4E1E52` (pool of 400) | vanilla; more budget = more missiles per cast |
| 15 | AI: blizzard / D&D need 3x cost; retreat at mana < 50 | `0x4CB496`, `0x4CAC97`, `0x4CAB86` | behaviour only |
| 16 | Global effect: computer casters get the same costs, damage, regen | all sites | intended, or use the mod-side regen / replacement functions for player-only rules |

Out-of-map indexing: every spell target walk in this report tests `(unsigned) coordinate < mapSize` before indexing
(death coil `cmp esi, eax / jae` + `cmp edx, eax / jae` at `0x4E1B8D..0x4E1B97`, exorcism `(ushort)` tests, `FUN_004af9e0`, `FUN_004bd850`, `FUN_004e2ba0`,
`FUN_004e29c0`); the rune tick trusts coordinates that `FUN_004e2ba0` validated. None of the proposed writes changes
a coordinate, a loop bound or an index, so no new out-of-map path appears. The positional orders the mod issues
(raise dead) are unaffected.

## [unverified]

- Real-time pacing of missile ticks (animation-script driven); counts per cast are exact.
- Name of order 0x28 (ALOW bit 2, no string, no action).
- That `FUN_004e0ad0` / `FUN_004e1400` are the savegame unit reader / writer (the byte copy itself is decompiled).
- Whether in-flight missiles are stored in savegames (only matters for the fireball marker after a hot reload).
- Mana readers above `0x540000` (not swept); the found UI readers all treat 255 as full.
- Whether a unit killed by the first exorcism hit on the centre tile is still in the grid for the second visit.
- Whether a multi-tile fleshy unit can be recorded more than once by the death coil scan.
- The exact pixel geometry of "full" / "quarter" splash (`max(dx², dy²)` against 0x200 / 0x700 from the missile
  position to the per-type centre offset `0x91BFC0`); the thresholds themselves are read from the code.
- The fireball tooltip's "34" (static text; the code deals up to 5 hits of a 20..40 roll per unit in range).
- Nothing here was run in the game.
