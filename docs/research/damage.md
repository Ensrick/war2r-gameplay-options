# Attack damage, armor and the upgrade bonuses

Exe build 1.0.2.2818, preferred base 0x400000. Every address is a VA; the mod stores RVAs (VA - 0x400000).
Decompiles are from the Ghidra project of that exe. Anything not read out of the code is marked `[unverified]`.

## Short answers

1. **One formula, two damage channels.** `basic` is reduced by the target's armor, `piercing` is not; the sum is
   rolled once between half and full. There is **no armor-type matrix and no per-target-class bonus**: apart from the
   armor number and the shields / ship-armor upgrade branch, nothing in the damage path looks at what the target is.
2. **Siege feels strong against buildings** because its basic damage is huge against a fixed armor (a catapult's
   basic beats a building's armor 20 by a wide margin), because its missiles splash, and because buildings never
   dodge. Not because of a "vs structures" rule, which does not exist in this build.
3. **Upgrade strength is one byte per upgrade group** at `0x8C11DC`, multiplied by the player's level counter. Nine
   of the eleven entries are real; two are dead and two are display-only.
4. The narrowest place for "unit X does N times damage to class Y" is the damage computation, **five `call`
   sites reaching three different callees**, all in unit-attack code and none of them on a spell path.

## 1. The formula

```c
// FUN_004bd770(attacker) -> roll for a melee hit or a direct missile   (the value is used as a BYTE)
basic  = FUN_004bdad0(attacker);                       // per-type basic damage + weapon upgrades
if (attacker->target /* +0x88 */) {
    t     = target->type;                              // +0x27
    armor = armorByType[t];                            // u8 table 0x917F90
    if (armorUpgradable[t] /* u8 0x918230 */) {
        if ((typeFlags[t] /* u32 0x9185F0 */ & 8) == 0)
            armor += shieldLevel[target->owner]        // group 2 counter 0x918C1C
                     * effect[2] /* 0x8C11DE = 2 */;
        else
            armor += shipArmorLevel[target->owner]     // group 4 counter 0x918C3C
                     * effect[4] /* 0x8C11E0 = 5 */;
    }
    basic -= armor;                                    // signed short
}
if (basic < 0) basic = 0;                              // armor never heals
pierce = FUN_004bda20(attacker);                       // per-type piercing + missile upgrade, armor NEVER applies
if (attacker->bloodlust /* +0x48 */) basic *= 2;       // piercing is doubled inside FUN_004bda20
total = pierce + basic;
if (total == 0) return 0;                              // no roll, no minimum: zero damage is a real outcome
half  = (total + 1) / 2;
roll  = rand() % (half + 1) + half;                    // uniform in [half, 2*half] = "50 % to 100 %"
return roll > 255 ? 255 : roll;                        // the caller stores it in a byte
```

```c
// FUN_004bdad0(attacker) -> basic damage with the weapon upgrades
if (cheatFlag(0x91B270 & 4) && 0x918CAC[owner]) return 0;
if (type == 0x12 /* ranger */ && marksmanship(0x918C9C[owner])) return 0;   // moves into the piercing channel
d = basicByType[type];                                 // u8 0x9180E0
if (weaponsUpgradable[type] /* u8 0x9181C0 */) {
    f = typeFlags[type];
    if (f & 8) return d + shipCannonLevel[owner] /* 0x918C2C */ * effect[3] /* 0x8C11DF = 5  */;
    if (f & 4) return d + siegeLevel[owner]      /* 0x918C5C */ * effect[6] /* 0x8C11E2 = 15 */;
    if (type is not 8, 9, 0x12, 0x13)                  // archers / rangers use the missile group instead
        return d + meleeLevel[owner] /* 0x918BFC */ * effect[1] /* 0x8C11DD = 2 */;
}
return d;

// FUN_004bda20(attacker) -> piercing damage with the missile upgrade
if (cheatFlag) return 0 or 200;
p = pierceByType[type];                                // u8 0x918150
if (type is 8, 9, 0x12, 0x13) {
    p += missileLevel[owner] /* 0x918BEC */ * effect[0] /* 0x8C11DC = 2 */;
    if (type == 0x12 && marksmanship) p += basicByType[0x12];   // the ranger's basic byte, read at 0x9180F2
}
return attacker->bloodlust ? p * 2 : p;
```

Two more entry points share those two halves:

```c
// FUN_004bdbd0(attacker) -> roll-free damage for a SPLASH missile: no target is known yet, so no armor
total = piercing + (bloodlust ? 2 * basic : basic);  clamp 255;      // armor is applied per victim later

// FUN_004bdc20(attacker, target) -> like FUN_004bd770 but with the target passed in (towers)
```

**Where the damage is applied.** `FUN_004bd8f0(attacker, target, damage)`: skips dying / hidden targets and units
under Unholy Armor (+0x46), then `hp <= damage ? kill : hp -= damage` on the u16 at +0x22. Called from the melee
site `0x4A89EA`, the tower path `0x4AA02D` / `0x4AA056`, the direct missile impact `0x4AE736`, the splash
`0x4AFC2A` (`FUN_004afb50`, which subtracts armor itself for missile types whose `0x8C0B0C` entry is 0) and death
coil `0x4E2AF5`. Walls take their damage through `FUN_004bd850` instead.

## 2. Is there an armor-type or damage-type system?

**No type-versus-type matrix exists in this build.** The evidence is the formula above: the only things read about
the *target* are `armorByType`, `armorUpgradable`, `typeFlags & 8` (ship armor or shields) and, in
`FUN_004bd8f0`, the state bits. There is no table indexed by (attacker type, target type), no "damage class" byte
next to the damage bytes, and no building flag in the damage path. Searching the effect table's readers finds only
the sites listed here.

What does exist, and is easy to mistake for one:

- **Two channels.** Piercing ignores armor completely, basic is reduced by it. A catapult (basic 80, piercing 0)
  against armor 20 loses a quarter; an archer (basic 3, piercing 6) against armor 20 loses its whole basic part and
  keeps its piercing. That is the whole "damage type" system.
- **Buildings have high armor (20) and never move**, so splash always lands and the same roll repeats.
- `typeFlags & 4` (siege) and `& 8` (ship) pick which *upgrade* applies to the attacker, not what it is good against.

So "cannons do more damage to buildings" cannot be read out of the game's own numbers: it has to be added.

## 3. The upgrade effect table `0x8C11DC`

Bytes as shipped, index = upgrade group (the group -> per-player counter pointers are at `0x8C03F8`,
`data_tables.md`), with every reference found by scanning `.text` for the absolute address:

| i | byte | group / counter | read by | what it does |
|---|---|---|---|---|
| 0 | 2 | missile weapons `0x918BEC` | `0x4BDA8E`, panels `0x4E4DA1` / `0x52D196` | + per level on piercing, archers / rangers only |
| 1 | 2 | melee weapons `0x918BFC` | `0x4BDBA4` | + per level on basic, everything that is not an archer / ship / siege |
| 2 | 2 | shields `0x918C1C` | `0x4BD7CD`, `0x4BDA03`, `0x4BDC7A`, UI | + per level on the **target's** armor (land) |
| 3 | 5 | ship cannons `0x918C2C` | `0x4BDB40` | + per level on basic, types with `typeFlags & 8` |
| 4 | 5 | ship armor `0x918C3C` | `0x4BD7BD`, `0x4BD9E5`, `0x4BDC6A`, UI | + per level on the target's armor (ships) |
| 5 | 10 | `0x918C4C` | **no reference at all** | dead in this build `[unverified what it was]` |
| 6 | 15 | catapult / ballista `0x918C5C` | `0x4BDB6A` | + per level on basic, types with `typeFlags & 4` |
| 7 | 0 | ranger / berserker `0x918C6C` | no reference | the upgrade converts the unit type instead |
| 8 | 1 | longbow / light axes `0x918C7C` | panel `0x52DD50` | range; the code adds it as an **immediate** `inc al` at `0x4EE689`, which is why `[range] upgrade_bonus` patches an instruction and writes this byte only for the panel |
| 9 | 0xFF | scouting `0x918C8C` | `0x4E6228`, `0x52DDD4` | sight; display / reveal only `[unverified]` |
| 10 | 3 | marksmanship / regeneration `0x918C9C` | panels `0x4E4DC0`, `0x52D1B1` | display only: the ranger's real bonus is the basic byte moved into piercing (above), and the berserker's regeneration is a separate path (`FUN_004ef480`) |

- **Counter x byte everywhere except the range bonus.** Entries 0, 1, 2, 3, 4 and 6 are all read as
  `counter[owner] * effect[i]` in the three damage / armor functions; only entry 8 is an immediate in code.
- **Safe range.** The products are summed into a `ushort`, the roll is clamped to 255, and the byte that reaches
  `FUN_004bd8f0` is a `u8`, so nothing wraps inside the damage path. The two status panels print `counter x byte`
  as a plain number. Counters reach 2 for the weapon / armor / ship / siege lines, so a byte up to 127 can never
  overflow a 255 clamp on its own; **0..100 is a safe configurable range**, the game's own numbers being 1..15.
- **Storage: nothing in the game ever writes it.** A disassembly sweep of `.text` for any instruction with an
  absolute destination in `0x8C1100..0x8C1300`, and for any instruction using such an address as an immediate (a
  loader or `memcpy` destination), finds **zero** hits; the twenty references above are all reads. So neither the
  data loader nor a savegame load restores this table - only the mod writes it. The per-player level counters
  (`0x918BEC` + group offsets) are runtime state and savegames do restore those, which is fine. A map-load sync
  plus the config hot reload is therefore enough; the mod still re-syncs on the tick, the way `spells.cpp` does for
  the cost table, because six byte compares cost nothing and a multiplayer game started from a savegame is then
  restored before its first unit update.

## 4. Multipliers by attacker and target class

**Target classes come from the type-flags table `0x9185F0`** (the same `w.typeFlags` the mod already reads):
`0x20` building, `0x02` flyer, `0x08` ship (the bit the armor branch uses), everything else is a land unit. So
`vs_structures / vs_air / vs_ships / vs_land` needs no new table.

**Recommended hook: the damage roll, five call sites.** All five are inside unit-attack code; no spell reaches
them, so spell damage stays untouched without any filtering:

**Three different callees, two of them not the roll.** Exactly two `E8` calls in `.text` reach `FUN_004BD770`;
the other three sites call one of its two siblings, so a hook has to remember the original callee per site and may
never assume `0x4BD770`:

| # | site | callee | what the callee computes | path |
|---|---|---|---|---|
| 1 | `0x4A89D0` | `FUN_004BD770` | roll, armor of `attacker+0x88` subtracted from the basic half | melee hit (`FUN_004a89b0`), also the wall hit through `FUN_004bd850` |
| 2 | `0x4AEEFB` | `FUN_004BD770` | the same roll | direct missile (`FUN_004aedf0`), stored in missile +0x37 |
| 3 | `0x4AEEF4` | `FUN_004BDBD0` | **no roll and no armor**: `piercing + (bloodlust ? 2 x basic : basic)`, clamped 255. The victim is not known yet, so `FUN_004afb50` subtracts each victim's armor when the splash lands | splash missile (same function) |
| 4 | `0x4AF8F3` | `FUN_004BDBD0` | as above | tower splash missile (`FUN_004af860`, from the tower order handler `FUN_004a9f60`) |
| 5 | `0x4AF8FF` | `FUN_004BDC20` | like the roll but with the target passed in as the second argument instead of read from `+0x88` | tower direct missile (same function) |

Which of the two a missile site uses is decided one instruction earlier:

```
0x4aeee6: movzx eax, byte ptr [esi + 0x34]        ; the missile's type
0x4aeeeb: cmp byte ptr [eax + 0x8c090c], 0        ; 0x8C090C = "this missile type splashes" (1 for types 7, 13, 14, 24)
0x4aeef2: je 0x4aeefb                             ; not splash -> the roll
0x4aeef4: call 0x4bdbd0                           ; splash -> no armor, no roll
0x4aef00: add esp, 4
0x4aef03: mov byte ptr [esi + 0x37], al           ; the damage byte the missile carries
```

All five are `cdecl` with the attacker pushed and the result used as a byte (`0x4AEF03: mov byte [esi+0x37], al`),
which makes a thunk trivial: call the original, scale, clamp, return. The bytes at each site are a plain `E8 rel32`,
the same shape the mod already byte-verifies in `src/hook.cpp`.

- Sites 1 and 5 know the target directly (`attacker+0x88`, or the second argument). Sites 2, 3 and 4 can read
  `attacker+0x88`, which is the unit the attack was ordered at; `[unverified]` whether a tower's `+0x88` is always
  set when `FUN_004af860` runs, so the thunk must treat a null target as "no class multiplier".
- **Splash victims other than the intended target get the intended target's multiplier**, because the byte is fixed
  when the missile is created. That is a deliberate simplification; the alternative is hooking the four unit-side
  call sites of `FUN_004bd8f0` instead, where attacker and victim are both known per hit, at the price of having to
  exclude spell missiles by type (and death coil shares a normal missile type, so that filter is not clean).
- **What a multiplier does to the roll:** applied after the roll, so the spread stays proportional (a x2 catapult
  rolls 2 x [half, full]). The game has no minimum damage - a roll of 0 is legal - so the mod should keep 0 at 0,
  round to nearest otherwise, and clamp to 1..255 so a multiplier below 1 cannot make a unit immune by rounding.
- **HP arithmetic stays u16**: the scaled byte is still a byte, and `FUN_004bd8f0` compares before subtracting.
- The **status panels** compute their numbers from `FUN_004bdbd0` / `FUN_004bdc20` through `FUN_004af860`'s own
  path, so a thunk on the five sites above does not change what the panel prints `[unverified, panel path not
  traced]`.

## 5. "Change the damage type"

With no type system, this reduces to moving numbers between the two channels, which
`[unit.NAME] basic_damage` / `piercing_damage` already does: put a catapult's 80 into `piercing_damage` and armor
stops mattering to it. Nothing else in the code reads a "type" of damage.

## 6. Proposed config

```toml
[upgrades]                 # -1 = the game's own number
missile_damage = -1        # effect[0], the game's 2
melee_damage   = -1        # effect[1], 2
shields        = -1        # effect[2], 2
ship_damage    = -1        # effect[3], 5
ship_armor     = -1        # effect[4], 5
siege_damage   = 30        # effect[6], 15: the author's "double the catapult / ballista bonus"
# range lives in [range] upgrade_bonus, it is an instruction immediate, not this table

[unit.ballista]            # and [building.human_cannon_tower], [unit.elven_destroyer], ...
vs_land       = 1.0
vs_air        = 1.0
vs_ships      = 1.5
vs_structures = 2.0
```

Four keys per attacker in the tables that already exist, so "ballista, catapult, cannon tower, guard tower, ships"
are just names; and one key per real effect-table entry. Entries 5, 7, 9 and 10 are not exposed: two are dead and
two are display-only.

## 7. Risks

- **The computer gets the same numbers.** Both the effect table and any multiplier keyed by unit type are global;
  the AI's catapults hit buildings exactly as hard as the player's. That is how every other multiplier in this mod
  works, and it must be said in the tutorial.
- **Multiplayer**: the damage roll is part of the simulation. Every client would have to run the same numbers, so
  the feature belongs behind the existing single-player gate, like the data tweaks.
- **No crash surface** beyond the clamps: the byte stays a byte, armor is clamped at zero damage, HP compares
  before subtracting. The one thing to keep is the 255 clamp, because the game itself clamps there
  (`FUN_004bd770`, `FUN_004bdbd0`).
- The AI's own strength estimate (`FUN_004cc1d0`, `data_tables.md`) does not read damage, so a multiplier does not
  confuse it.

## [unverified]

- What upgrade group 5 (effect byte 10) was for; nothing reads it in this build.
- What entry 9 (0xFF) does at `0x4E6228` beyond display.
- Whether a tower's `+0x88` target pointer is valid at `FUN_004af860` time.
- The status-panel damage path was not traced end to end.
