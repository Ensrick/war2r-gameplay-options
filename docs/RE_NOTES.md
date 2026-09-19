# Reverse-engineering notes

Target: `Warcraft II.exe` 1.0.2.2818, SHA256 `1A396A77B123BBAE46C6A2D92ADC2EC6714134C24023ED8218F7A160A80EC162`,
PE timestamp 1771967463, 32-bit, ASLR on, preferred base 0x400000. All VAs below are at the preferred base.
The exe is the 1999 BNE source recompiled (`D:\Jenkins\workspace\warcraft2-pipeline\War2BNE\...` assert paths), so
the classic `Data\Files\Warcraft II BNE.exe` shipped next to it is the Rosetta stone. Tooling: Ghidra 12.1.3 headless
(`C:\Tools\ghidra_projects`, projects `war2bne` / `war2r`, script `scripts\XrefDecomp.java`).

## How the functions were found

1. Spell mana-cost table located by value pattern in both exes: BNE `0x49F830`, Remastered `0x8C5F04`
   (uint16 per spell; the code indexes it by order id from `0x8C5EB8` = table - 0x26*2).
   Remastered values: 70,5,5,4,80,100,50,200,200,25,70,60,50,100,100,50,100,200,30.
2. Xrefs to the table give the computer AI's caster routines. BNE `FUN_00424960` = Remastered `FUN_004ca4a0`
   dispatches on unit type: 0x0D/0x17 ogre-mage `004cb200`, 0x0C/0x2C paladin `004cb2f0`, 0x0B/0x15 death knight
   `004cac80`, 0x0A/0x18 mage `004cb480`.
3. The AI think loop `FUN_004ccce0` calls the dispatcher for every unit of a computer player
   (`controller[owner] == 1`) whose type has IS_CASTER, once every 0x32 steps, from the AI tick `FUN_004e89a0`.

## Addresses used by the mod (see src/game.h)

| What | VA | Evidence |
|---|---|---|
| Tick hook call site | `0x4E89A6` `call 0x4CA440` | first call in `FUN_004e89a0`; that function runs once per simulation step from both game-loop variants (`FUN_004c5190`, `FUN_004c4e80`) and only when not paused (`DAT_0091c596 == 0`) |
| IssueOrder | `0x4EF210` | `cdecl (unit, x, y, target, handler)`; every AI cast helper calls it, caller cleans 0x14 |
| Spell order handler | `0x4E2970` | passed to IssueOrder by all AI casts; reads the pending spell global |
| Pending spell order | `0x9348BC` (u16) | set before / cleared after IssueOrder in `FUN_004cb0e0` |
| Unit array / count | `0x91C704` / `0x91BFB8` | think loop, stride 0x98 |
| Unit tile grid / map size | `0x91AD6C` / `0x918D10` | AI target search `FUN_004cb3e0`, index `y*size + x`. LAND / SEA units only |
| Air unit tile grid | `0x91AD70` | same shape. `FUN_004b4a00(unit)`: `test byte [unit+0x1C], 4` -> `cmovne eax, [0x91AD70]`, then `mov [eax+tile*4], unit` (and map flag 0x200 instead of 0x100). Flyers are filed ONLY here, so every scan must read both grids |
| Controller table | `0x918CAC` (u8[16]) | 0 = human (cheat handler grants humans all spells), 1 = computer, 3 = left (WC2R_Mods research) |
| Local player | `0x918CCD` (u8) | selection code `cmp al, [0x918ccd]` (WC2R_Mods research) |
| Alliance table | `0x919578` (u8[16][16]) | every AI spell filter |
| Unit type flags | `0x9185F0` (u32[type]) | filters; bit names from Mistral's War2Mod `defs.h` |
| Spells researched | `0x919250` (u32[16]) | PUD ALOW bit layout; filters and cheat handler |
| Max HP by type | `0x9177C0` (u16[type]) | body of `FUN_004ee1f0` |
| Net game flag | `0x91C6F4` (u32) | game loop takes the `DONETWORKTURN` branch when nonzero |
| ShowMessage | `0x4D3160` | `cdecl (text, 8, duration, 0)`, used for the cheat banner |

Unit struct offsets are unchanged from BNE (0x18 x, 0x1A y, 0x1E state flags, 0x22 hp, 0x26 mana, 0x27 type,
0x2C owner, 0x2E order, 0x44 invis, 0x46 unholy armor, 0x48 bloodlust, 0x4A haste/slow, 0x4C AI spell marks,
0x4E flame shield, 0x88 order target). The unit timer update at `0x4EF4A0..0x4EF565` decrements 0x44-0x4E and zeroes
0x4C for every unit. Order ids (2 stop, 3 move, 8-11 attack, 12 defend, 13 stand, 38+ spells) match Mistral's
`names.h` and the constants the AI passes (heal 0x27, exorcism 0x29, slow 0x2C, polymorph 0x2E, bloodlust 0x31,
death coil 0x33, haste 0x35, unholy armor 0x36).

## Game AI target filters (for parity reference)

| Spell | VA | Rule |
|---|---|---|
| Heal | `004ca7c0` | allied, fleshy, hp < max, not already marked |
| Exorcism | `004caae0` | enemy, undead |
| Bloodlust | `004ca5c0` | allied, fleshy, no bloodlust, order == 12 |
| Haste | `004ca780` | allied, no haste/slow, order == 12 |
| Slow | `004ca940` | enemy, not building, not slowed, order != 2 |
| Polymorph | `004ca820` | enemy, fleshy + attacker |
| Death Coil | `004ca660` | enemy, fleshy |
| Unholy Armor | `004caa80` | allied, no armor, order == 12, hp <= max/2 |

The AI search window is 31x31 tiles, first hit in scan order. The mod uses its own scored scan (nearest / most hurt)
and widens "fighting" to explicit attack orders with an enemy nearby, because a human's attack orders are not 12.

## Why direct orders are single-player only

The AI issues orders by calling IssueOrder directly because it runs identically on every peer. A human player's
commands travel through the network turn queue. Calling IssueOrder for the local human in a network game would
change state on one peer only, so the mod refuses to run when `0x91C6F4 != 0`.

## Correction: orders land in the next-order slot

SetOrder (`0x4EF080`) writes the NEXT-order byte (+0x2F); it becomes the current order (+0x2E) at `0x4ED9C3` when the
running action step ends, and +0x2F goes back to 0x3C ("none"). The game's UI reads "next if set, else current" at
`0x4E85B2`, and so does every order check in the mod (`game::EffectiveOrder`).

## Later research (one report per feature)

| Report | Covers |
|---|---|
| `research/eye_of_kilrogg.md` | spell action table `0x8C1590`, unit creation, AI order table `0x8C3E10`, explored / fog maps `0x91AD60` / `0x91AD5C`, player command path, Remastered resume-order byte +0x8D |
| `research/workers_and_gold.md` | order handler table `0x8C1498` and action table `0x8C13A0`, gold left at unit+0x82, region map `0x91AD7C` (0xFFFE = tree), worker flags +0x75, repair rules, idle detection, IssueOrder lock table |
| `research/tree_regrowth.md` | terrain maps: tile ids `0x91AD68`, square flags `0x91AD58`, region words; tree felling `FUN_004eb400` and the per-tileset removal table (`0x9347E8`, stride `0x9347E0`, base `0x9347F0`), stump tile 0x7E, the 4-corner model, why regions only ever merge, what savegames store, the regrowth recipe behind `[trees]` |
| `research/data_tables.md` | every UDTA / UGRD runtime table, loaders, the new-map-only `call FinalizeTables` at `0x4D2C46` vs the savegame one at `0x4C4602`, sight function pointers, HP readers, game speed table |
| `research/spells.md` | mana byte (+0x26, hard cap 255, +1 per 40 steps), the cost table `0x8C5EB8` (not saved, never reloaded), spell action table `0x8C1590`, missile classes, every damage immediate and its limit (behind `[spell_cost]` / `[spell_damage]` / `[mana]`, `src/spells.cpp`) |

## Attack range and the range upgrade (found 2026-09-18)

- `FUN_004ee660(unit)` = GetAttackRange, cdecl, two callers (`0x4A8E65`, `0x4D94D1`). It returns
  `range[type]` (u8[110] at `0x917E40`), and for type 0x12 / 0x13 (ranger / berserker) whose owner has the group-8
  counter `0x918C7C[owner]` set, `range[type] + 1`. The `+ 1` is the 2-byte `inc al` (`FE C0`) at `0x4EE689`,
  followed by `pop ebp; ret` (`5D C3`). `add al, imm8` (`04 nn`) is the same size, so the bonus is patched in place.
- Per-upgrade-group effect bytes at `0x8C11DC` (index = group): 2 missile damage, 2 melee damage, 2 shields, 5 ship
  cannons, 5 ship armor, 10, 15 catapult / ballista, 0, **1 range (`0x8C11E4`)**, 0xFF, 3. Both status panels
  (`FUN_004e4d70` classic, `FUN_0052dd20` Remastered) print counter x this byte, so the mod writes the configured bonus
  there too. The other entries are what the damage / armor code reads (`0x4BDA8E`, `0x4BDBA4`, `0x4BD7CD` ...):
  a future "upgrade strength" setting would edit them.
- More per-type tables used by `[unit.NAME]`: armor u8 `0x917F90`, basic damage u8 `0x9180E0`, piercing damage u8
  `0x918150`, build time u8 `0x917910`, oil cost u8 `0x917A60`, research time u8[52] `0x9188A8`.
