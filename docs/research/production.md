# Production: training, research, requirements, counts (static RE, Warcraft II: Remastered 1.0.2.2818)

Target: `Warcraft II.exe` 1.0.2.2818 (PE timestamp 1771967463). Static analysis only: Ghidra 12.1.3 headless (project
clone `war2r_a`), capstone disassembly, raw dumps of `.data` / `.rdata` and of `Data\Rez\unitdata.dat`. All VAs at
the preferred base 0x400000; RVA = VA - 0x400000. Raw decompiles: `prod_callers.c`, `prod_core.c`, `prod_buttons.c`,
`prod_misc.c` in the Ghidra output folder, plus the earlier `dat_pay.c` / `dat_costs.c`. Anything inferred rather than
read is marked `[unverified]`. Nothing here was run in the game.

## Short answers

**1. Training.** Every production start in the game (unit, spell research, upgrade, building upgrade) goes through
one function, **`StartProduction` = `FUN_004ace10(Unit* building, u8 id, u8 kind)`**, cdecl, returns 1 when started.
`kind` 0 = train unit (`id` = unit type), 1 = spell research incl. the paladin / ogre-mage upgrade (`id` = upgrade
index 32..51), 2 = upgrade (`id` = upgrade index 0..31), 3 = building upgrade (`id` = target building type). It checks
"not busy, construction complete, per-kind validator", deducts gold / lumber / oil, sets the building busy and gives it
order **0x25** (produce). The **computer AI calls it directly** from its per-building think functions (24 of the 26
call sites). The **human path** is button -> `FUN_004f1350(id, kind)` -> command 0x21 in the command queue -> executor
(`FUN_004f16f0` for the local queue; `FUN_0049bff0` reads a second packet layout, presumably the network one
[unverified]) -> `StartProduction` on the commanding player's selected building. There is no pending-unit-type global: `id` and `kind` travel as arguments and end up in the building
(`+0x6D`, `+0x6C`). **A mod can call `StartProduction` from the tick hook exactly like the AI does** (single player
only, same thread, same moment of the step the AI uses), after its own checks (section 4 lists what the function does
NOT check).

**2. What can be trained where.** A "trained at" byte table at `0x838248` (`.rdata`, one byte per unit type) plus one
requirement function per unit type at `0x8C0428` is what `StartProduction` checks. The game's buttons additionally
check the map's **ALOW "units allowed" mask `0x919210[player]`** and "building complete, not busy". The per-building
button lists, button conditions and requirement rules are all in section 2. The engine function to call or mirror is
the kind-0 validator `FUN_004ac840(building, type)`, plus the ALOW bit, plus the cost / food check `FUN_004ac610`.

**3. Counting.** Per-player `u16[16]` counters per type pair (table `0x8C0B80`), counted only for **complete**
units / buildings. Units in training are **not** in any per-type counter and **not** in "food used"; there is one
per-player total of units in training, **`0x9193F0` (u16[16])**. Hall tiers: `0x91B52C` halls, `0x91B54C` keeps,
`0x91B56C` castles. Several counters include campaign heroes (footman counter includes Grom, Danath, Korgath...):
for exact per-type numbers scan the unit array.

**4. Research reserve.** Available research = the button rules: allowed in ALOW (`0x919310` upgrades, `0x919290`
spells), not done (group level counter `0x918BEC + ...`, or spell bit in `0x919250`), **not in progress**
(`0x919350` upgrades, `0x9192D0` spells), prerequisites (keep / castle for ranger, paladin upgrade for church / altar
spells, buildings for keep / castle / towers), and a researching building of the right type that is idle. Costs:
`0x9188E0 / 0x918948 / 0x9189B0` (u16, plain gold) for upgrades and spells, the unit cost tables (bytes of tens) for
building upgrades. **`StartProduction` checks none of "done" or "in progress"**: a duplicate start takes the money and
the second completion silently does nothing (no refund).

**5. Costs and food.** Unit prices: `0x917980 / 0x9179F0 / 0x917A60` (u8, x10), time `0x917910` (u8, x2 = steps of
the production counter). Food: every non-building unit costs 1 except skeleton, daemon, critter (types 0x37..0x39);
supply `0x91B50C` (read clamped to 200), used = `0x91B38C - 0x91B6AC`. The game's food check is `supply > used`, units
in training ignored, so parallel starts can overshoot supply (vanilla behaviour). A rule "keep 4 food free" must use
`min(supply, 200) - (used + 0x9193F0[p]) - 1 >= 4`.

**6a. Game speed.** `0x964CA8` (u32, 0..8 in Remastered mode; ms per step `0x83A478[speed]` = 80..13). Setter
`FUN_004c4910(speed, 0)`, getter `FUN_004c4880`. The options dialog changes it through the command queue:
`FUN_004f2650(speed)` = command 0x27, executed by `FUN_004f16f0` -> `FUN_004c4910`. From the tick in single player,
calling `FUN_004f2650(speed)` is exactly what the game's own menu does; a direct write of `0x964CA8` also takes effect
at the next step. Never in multiplayer (the mod's gate).

**6b. Rally points.** None in this build: no "rally" / "gather" / "waypoint" string in the exe, and the finish
handler `FUN_004acb00` only calls `CreateUnitNear`. A new unit appears on the first free tile of its movement layer
within 12 tiles around the building (`FUN_004edfa0` -> `FUN_004e2e40(..., 0xC, ...)`) with order 2 (stop) and no
next order (`_Dst[0x17] = 0x3c02` in `FUN_004edb10`). A computer player's unit then gets an AI order; a human's does
not.

---

## Addresses

### Code (all cdecl)

| VA | What | Evidence |
|---|---|---|
| `0x4ACE10` | **StartProduction(building, id, kind)**, returns 1 = started | decompile (`dat_pay.c`), 26 call sites |
| `0x4AC840` | kind 0 validator (train): trained-at table + requirement fn `0x8C0428[type]` | decompile |
| `0x4AC8C0` | kind 1 / 2 validator (research): research-at table `0x838284[id] == building type` | decompile |
| `0x4AC8F0` | kind 3 validator (building upgrade): upgrade-from table + building prerequisites | decompile |
| `0x4AC610` | UnitCost(unit, type): fills the pending cost, food check for types < 0x3A, returns an error string or 0 | decompile |
| `0x4AC9D0` | UpgradeCost(unit, id): same for upgrades / spells (no food) | decompile |
| `0x4AD1C0` | Affordability: gold / lumber / oil `>=` pending cost (signed compare) | decompile |
| `0x4AD250` | Order 0x25 step: advance progress, finish or cancel | decompile (`spell_manaread.c`) |
| `0x4ACB00` / `0x4ACBC0` / `0x4ACCA0` / `0x4ACA20` | Finish handlers kind 0..3 (`0x8C0530`); refund on cancel / failure | decompile |
| `0x4AD120` | Cancel: `+0x1C |= 0x20` if busy (command 0x22) | decompile |
| `0x4BE4F0` | Order 0x21 step (idle building): for `controller == 1` only, calls the AI production fn `0x8C0540[type - 0x3A]` | decompile |
| `0x4AD700` `0x4AD900` `0x4ADB30` `0x4ADC40` `0x4ADD00` `0x4ADDC0` `0x4ADE90` `0x4ADF50` `0x4AE070` `0x4AE0E0` `0x4AD8D0` | AI production per building (barracks, shipyard, hall, lumber mill, foundry, blacksmith, church / altar, mage tower / temple, aviary / roost, inventor / alchemist, scout tower) | decompile (`prod_callers.c`), table `0x8C0540` |
| `0x4ACD90` / `0x4ACDD0` / `0x4AD140` / `0x4AD180` | Button actions: train / spell research / upgrade / building upgrade. Each runs the cost fn for the selected unit `0x9342EC`, shows the error or calls `FUN_004f1350(id, kind)` | decompile |
| `0x4F1350` | Queue command 0x21 `{kind, id}` (network: `FUN_0049c5f0`) | decompile |
| `0x4F16F0` / `0x49BFF0` | Command executors (two packet layouts: `+8` kind `+9` id / `+2` kind `+1` id): command 0x21 -> StartProduction on `FUN_004dfac0()` (commanding player's selected unit) | decompile |
| `0x4EDFA0` | CreateUnitNear(x, y, type, owner, sizeEntry*): free tile within 12 on the type's layer, then CreateUnit | decompile |
| `0x4C4910` / `0x4C4880` / `0x4F2650` | Game speed setter / getter / "change speed" command (0x27) | decompile + disasm |

### Data

| VA | What | Type | Evidence |
|---|---|---|---|
| `0x8C0510` / `0x8C0520` / `0x8C0530` | Per-kind validator / cost / finish fn tables | fn ptr[4] | dump: `4AC840 4AC8C0 4AC8C0 4AC8F0` / `4AC610 4AC9D0 4AC9D0 4AC610` / `4ACB00 4ACBC0 4ACCA0 4ACA20` |
| `0x838248` | Trained-at building type per unit type (`'n'` = cannot be trained); `+0x70` = upgrade-from table for building types 0x3A..0x68 | u8 | `.rdata` string, validators |
| `0x838284` | Research-at building type per upgrade index 0..51 | u8[52] | `.rdata` string, `FUN_004ac8c0` |
| `0x8C0428` | Requirement fn per unit type (0 = never trainable) | fn ptr[0x3A] | dump |
| `0x8C0540` | AI production fn per building type - 0x3A | fn ptr | dump, read at `0x4BE558` |
| `0x8C5F48` | Button set per unit type: `{u32 count, record*}`, records 24 bytes | | dump; sets listed in section 2 |
| `0x919210` | ALOW units / buildings allowed | u32[16] | PUD ALOW handler `FUN_004d19d0` (`0x4D19E7`) |
| `0x919250` | Spells known (researched) | u32[16] | same, `0x4D19FA` |
| `0x919290` | Spells allowed | u32[16] | same, `0x4D1A0D` |
| `0x9192D0` | Spells in research | u32[16] | same, `0x4D1A20`; set by StartProduction kind 1 |
| `0x919310` | Upgrades allowed | u32[16] | same, `0x4D1A33` |
| `0x919350` | Upgrades in research | u32[16] | same, `0x4D1A46`; set by StartProduction kind 2 |
| `0x9193F0` | Units in training per player | u16[16] | `+1` at start (`0x4AD079`), `-1` at finish / cancel (`0x4ACB8D`, `0x4ACBB2`); saved (`0x4AAA42` / `0x4AB442`) |
| `0x8C03F8` | Upgrade group -> per-player level counter pointer (u8[16] each) | ptr[11] | `data_tables.md` |
| `0x918A80` / `0x918AE8` | Upgrade group / flag per upgrade index | u16[52] / u32[52] | `data_tables.md`, `FUN_004acca0` |
| `0x9188A8` / `0x9188E0` / `0x918948` / `0x9189B0` | Upgrade time (u8, x2) / gold / lumber / oil (u16) | | `FUN_004ac9d0` |
| `0x91B03D` | 1 = building requirements enforced by the validators (set at `0x4D10ED`, `0x52916F`; cleared at `0x4B1194` under a player-slot condition) | u8 | disasm |
| `0x91AAB0..0x91AABC` | Pending time (u16) / gold / lumber / oil (u32): scratch of the cost functions | | `FUN_004ac610` |
| `0x964CA8` | Game speed 0..8 | u32 | `FUN_004c4910`, loop `FUN_004c4e80` |

### A building while producing

| Field | Meaning | Evidence |
|---|---|---|
| `+0x1C & 0x10` | busy (producing); `& 0x20` cancel requested | StartProduction, `FUN_004ad120`, `FUN_004ad250` |
| `+0x1E & 0x80` | construction complete; low nibble 0 = alive | StartProduction precondition |
| `+0x2E` | order 0x25 while producing, 0x21 when idle | `SetOrder(b, 0x25)` at the end of StartProduction, `SetOrder(b, 0x21)` in `FUN_004ad250` |
| `+0x6C` / `+0x6D` | kind / id | StartProduction |
| `+0x6E` / `+0x70` | progress / total (u16); total = 2 x time byte. `+0x6E` doubles as the AI idle timer when idle | StartProduction, `FUN_004ad250`, `FUN_004be4f0` |
| `+0x25` / `+0x26` | display type 0x75 / progress bar 0..255 while producing; back to type / 0 at the end | StartProduction (`+0x25 = 0x175` as a word), `FUN_004ad250` |
| `+0x09` | 1 while a building upgrade is under way | StartProduction kind 3 |

**Idle building (mod test):** `(state & 0x0F) == 0`, `state & 0x80`, `(+0x1C & 0x10) == 0`, owner = local player.
All saved with the game: the unit packer `FUN_004ac290` copies `+0x1C`, `+0x6C`, `+0x6D`, `+0x6E`, `+0x70`, `+0x72`
for buildings; `0x9193F0` and the ALOW masks are in the pack / unpack functions.

---

## 1. Training: evidence

### StartProduction (`FUN_004ace10`)

```c
if (!(flags[b->type] & 0x20)) debug_print("bldg: %d %d %d\n", ...);       // not a building: prints, validator fails
if ((b[0x1C] & 0x10) == 0 && (b[0x1E] & 0x80) && kind < 4 && validate[kind](b, id)) {
  // kind 2, id < 0x18: the line's counter must be 0 for an even id, 1 for an odd id. Lines: 0-3 melee 0x918BFC,
  // 4-7 missile 0x918BEC, 8-11 shields 0x918C1C, 12-15 ship cannons 0x918C2C, 16-19 ship armor 0x918C3C,
  // 20-23 siege 0x918C5C (a 24-entry {counter*, level} table built on the stack)
  if (kind == 2 && id < 0x18 && lineCounter[id][owner] != (id & 1)) return 0;
  err = cost[kind](b, id);                   // fills 0x91AAB0..0x91AABC, food check for units
  if (err == 0) {
    gold[owner] -= pendingGold; lumber[owner] -= pendingLumber; oil[owner] -= pendingOil;
    switch (kind) {
      case 0: inTraining[owner]++;                          // 0x9193F0
      case 1: spellsInResearch[owner] |= flag[id];          // 0x9192D0
      case 2: upgradesInResearch[owner] |= flag[id];        // 0x919350
      case 3: graphics of the target type, b[9] = 1;
    }
    b[0x1C] |= 0x10;  b[0x6E] = 0;  b[0x25..0x26] = 0x0175;  b[0x6C] = kind;  b[0x6D] = id;  b[0x70] = pendingTime;
    SetOrder(b, 0x25);
    return 1;
  }
  if (owner == localPlayer) ShowMessage(err, 8, 0x14, 0);   // "Not enough gold..." / food / lumber / oil
}
return 0;
```

AI call site, cdecl confirmed (`FUN_004ae2d0`, the plain footman / grunt case):

```
0x4ae2d6: 6a 00       push 0                 ; kind 0
0x4ae2d8: mov al, [ecx+0x27] / and al, 1 / movzx eax, al
0x4ae2e0: 50          push eax               ; id = race bit: footman 0 / grunt 1
0x4ae2e1: 51          push ecx               ; building
0x4ae2e2: call 0x4ace10
0x4ae2e7: add esp, 0xc
```

### Production step and finish (`FUN_004ad250`, order 0x25)

```c
if (!(b[0x1C] & 0x20)) {                                   // not cancelled
  if (b->progress < b->total) { b->progress += (cheats & 2) ? min(total - progress, 10) : 1;
                                b[0x26] = progress * 255 / total; return 0; }
  done = 1;
} else done = 0;
b[0x25] = b->type;  b[0x1C] &= ~0x30;  SetOrder(b, 0x21);  b->progress = 0;
cost[b->kind](b, b->id);                                   // recomputes the pending cost (for a refund only)
b[0x26] = 0;
finish[b->kind](b, done);
```

Finish of a unit (`FUN_004acb00`): `done` -> `CreateUnitNear(b->x, b->y, b->id, owner, unitSize[b->type])`; success ->
"ready" voice / minimap ping for the local player (`FUN_004c79f0`), `inTraining--`; no free tile or no unit slot ->
message for the local player, refund, `inTraining--`. Cancelled -> refund, `inTraining--`. The refund uses the prices
of the table at that moment.

### Human path

```c
// button action FUN_004acd90(type) (spell: 004acdd0, upgrade: 004ad140, building upgrade: 004ad180)
err = cost[kind](selectedUnit /*0x9342EC*/, id);
if (err) ShowMessage(err); else FUN_004f1350(id, kind);
// FUN_004f1350: network ? FUN_0049c5f0(id, kind) : queue {0x0B, ..., [6] = 0x21, [8] = kind, [9] = id} (FUN_00517b30)
// executor FUN_004f16f0 case 0x21 (local queue) / FUN_0049bff0 (other packet layout, network [unverified]):
u = FUN_004dfac0();                                        // first selected unit of the commanding player 0x922F5A
if (u && building(u) && complete(u) && u->owner == commandingPlayer && kind < 4) StartProduction(u, id, kind);
```

### Computer AI

`FUN_004be4f0` is the order-0x21 step (idle building). For a complete building of a player with `controller == 1`
it runs a per-type timer (`+0x6E` against a per-player delay table) and then `0x8C0540[type - 0x3A](b)`. Those
functions pick what to build from per-player AI targets (`0x91D664 + p*0x30 ...`) and call `StartProduction`
directly, e.g. barracks `FUN_004ad700`, shipyard `FUN_004ad900`, hall `FUN_004adb30` (peasants while
`0x91B66C[p] < target`, then keep / castle as kind 3), research buildings through the 12-byte tables `0x8C05F8`,
`0x8C06A0`, `0x8C0718`, `0x8C07C0`, `0x8C0820`. They read AI state that only exists for computer players: never call
them for human buildings.

---

## 2. What can be trained where

### Units (`0x838248` trained-at, `0x8C0428` requirement, button condition)

Keep / castle count as their hall for the trained-at test (`FUN_004ac840` maps 0x58 / 0x5A to 0x4A, 0x59 / 0x5B to
0x4B). "ALOW bit" is `0x919210[p]`, checked only by the buttons.

| Unit (human / orc) | Trained at | Requirement fn and rule | ALOW bit | Button condition |
|---|---|---|---|---|
| footman / grunt 0x00 / 0x01 | barracks 0x3C / 0x3D | `4A1F70` (none) | 0x1 | `4E3B00` |
| peasant / peon 0x02 / 0x03 | hall 0x4A / 0x4B (and keep, castle) | none | 0x2 | `4E3A30` |
| ballista / catapult 0x04 / 0x05 | barracks | `4AC6C0`: blacksmith (`0x91B44C`) and lumber mill (`0x91B42C`) | 0x4 | `4E3B30` |
| knight / ogre 0x06 / 0x07 | barracks | `4AC6F0`: blacksmith, stables / ogre mound (`0x91B46C`), paladin upgrade NOT done (`0x919250 & 0x100000`) | 0x8 | `4E3B80` |
| paladin / ogre-mage 0x0C / 0x0D | barracks | `4AC730`: same with the upgrade done | 0x8 | `4E3BE0` |
| archer / axethrower 0x08 / 0x09 | barracks | `4AC770`: lumber mill, ranger upgrade NOT done (`0x918C6C == 0`) | 0x10 | `4E3A60` |
| ranger / berserker 0x12 / 0x13 | barracks | `4AC7A0`: lumber mill, ranger upgrade done | 0x10 | `4E3AB0` |
| mage / death knight 0x0A / 0x0B | mage tower 0x50 / temple 0x51 | none | 0x20 | `4E3C40` |
| tanker 0x1A / 0x1B | shipyard 0x48 / 0x49 | none | 0x40 | `4E3C70` |
| destroyer 0x1E / 0x1F | shipyard | none | 0x80 | `4E3CA0` |
| transport 0x1C / 0x1D | shipyard | `4AC7F0`: foundry (`0x91B5AC`) | 0x100 | `4E3CD0` |
| battleship / juggernaught 0x20 / 0x21 | shipyard | `4AC7F0`: foundry | 0x200 | `4E3D20` |
| submarine / turtle 0x26 / 0x27 | shipyard | `4AC7D0`: inventor / alchemist (`0x91B5CC`) | 0x400 | `4E3D70` |
| flying machine / zeppelin 0x28 / 0x29 | inventor 0x44 / alchemist 0x45 | `4AC810`: lumber mill and inventor | 0x800 | `4E3DC0` |
| dwarves / sappers 0x0E / 0x0F | inventor / alchemist | `4AC7D0`: inventor | 0x4000 | `4E3E10` |
| gryphon rider / dragon 0x2A / 0x2B | aviary 0x46 / roost 0x47 | none | 0x1000 | `4E3E60` |

Every unit button condition also requires the selected building complete and not busy. The second button function
(record `+8`) is the "show greyed with its requirement" test: it checks that the prerequisite buildings are ALLOWED
(ALOW building bits seen there: 0x8000 aviary, 0x20000 barracks, 0x40000 lumber mill, 0x80000 stables, 0x200000
foundry, 0x800000 inventor, 0x8000000 keep, 0x10000000 castle, 0x20000000 blacksmith). Types with a null requirement
fn (attack peasants 0x10 / 0x11, heroes, 0x22..0x25, 0x2C..) can never be trained.

When a requirement fn returns 0, `FUN_004ac840` still returns true if `0x91B03D == 0` ("requirements off").

### Button sets (`0x8C5F48[type] = {count, records}`, record `{u16 pos, u16 icon, cond, greyCond, action, u8 a, u8 b, u16 string}`)

| Building | Buttons (b = unit type / upgrade index / target type) |
|---|---|
| human barracks 0x3C | footman 0x00, archer 0x08 / ranger 0x12 (same slot), ballista 0x04, knight 0x06 / paladin 0x0C (same slot) |
| orc barracks 0x3D | grunt, axethrower / berserker, catapult, ogre / ogre-mage |
| hall 0x4A / 0x4B, keep, castle | peasant / peon; keep 0x58 (on a hall, `4E36F0`: ALOW 0x8000000, barracks), castle 0x5A (on a keep: ALOW 0x10000000, stables, lumber mill, blacksmith) |
| shipyard 0x48 / 0x49 | tanker, destroyer, transport, battleship, submarine |
| inventor 0x44 / alchemist 0x45 | flying machine / zeppelin, dwarves / sappers |
| aviary 0x46 / roost 0x47 | gryphon rider / dragon |
| mage tower 0x50 | mage; spells slow 39, flame shield 37, invisibility 40, polymorph 41, blizzard 42 (`4E38E0`) |
| temple 0x51 | death knight; haste 48, raise dead 45, whirlwind 47, unholy armor 49, death and decay 51 |
| church 0x3E | paladin upgrade 33 (`4E38E0`), healing 35, exorcism 36 (`4E3920`: paladin upgrade done) |
| altar 0x3F | ogre-mage upgrade 32, bloodlust 44, runes 50 (`4E3920`) |
| lumber mill 0x4C / 0x4D | arrows 4 / 5 (axes 6 / 7), ranger 24 (berserker 28, `4E3290`: keep or castle), scouting 26 / 30, longbow 25 / lighter axes 29, marksmanship 27 / regeneration 31 (these three need the ranger upgrade done) |
| blacksmith 0x52 / 0x53 | swords 0 / 1 (axes 2 / 3), shields 8 / 9 (10 / 11), ballista 22 / 23 (catapult 20 / 21) |
| foundry 0x4E / 0x4F | ship cannons 12 / 13 (14 / 15), ship armor 16 / 17 (18 / 19) |
| scout tower 0x40 / 0x41 | guard tower 0x60 / 0x61 (lumber mill), cannon tower 0x62 / 0x63 (blacksmith) |
| stables, farms, refinery, platform | none |

Holy vision (34), fireball (38), eye of kilrogg (43), death coil (46) have no research button (cost 0, known from the
start through the map's ALOW "spells known").

---

## 3. Counting

Per-type counter pointer table `0x8C0B80[type]` -> `u16[16]` by player, updated by CountAdd / CountRemove
(`food_supply.md`), complete units / buildings only:

| Counter | Types counted |
|---|---|
| `0x91B6EC` | footman, grunt (+ heroes Grom 0x19, Danath 0x2E, Korgath 0x2F) |
| `0x91B66C` | peasant, peon, attack peasant / peon 0x10 / 0x11 |
| `0x91B76C` | ballista, catapult |
| `0x91B74C` | knight, ogre, paladin, ogre-mage (+ Dentarg 0x17, Turalyon 0x2C) |
| `0x91B70C` | archer, axethrower (+ Alleria 0x14) |
| `0x91B72C` | ranger, berserker |
| `0x91B6CC` | mage, death knight (+ Teron 0x15, Khadgar 0x18) |
| `0x91B84C` | dwarves, sappers |
| `0x91B82C` | gryphon rider, dragon (+ Kurdran 0x16, Deathwing 0x23) |
| `0x91B80C` | flying machine, zeppelin |
| `0x91B68C` / `0x91B78C` / `0x91B7AC` / `0x91B7CC` / `0x91B7EC` | tankers / transports / destroyers / battleships / submarines |
| `0x91B6AC` | skeleton, daemon, critter (the food-free units) |
| `0x91B48C` farms, `0x91B4CC` barracks, `0x91B4AC` church / altar, `0x91B40C` all towers, `0x91B46C` stables / ogre mound, `0x91B5CC` inventor / alchemist, `0x91B5EC` aviary / roost, `0x91B60C` shipyards, `0x91B42C` lumber mills, `0x91B5AC` foundries, `0x91B44C` blacksmiths, `0x91B58C` refineries, `0x91B3EC` oil platforms | buildings |
| `0x91B52C` / `0x91B54C` / `0x91B56C` | halls / keeps / castles (each building counts once, in its current tier) |

Mage towers / temples have no counter (null entry). A hall being upgraded keeps its old type (and counter) until
the upgrade finishes (`FUN_004aca20`: CountRemove, type change, CountAdd).

Units in training: only the per-player total `0x9193F0`. Per type: scan the player's busy buildings with
`+0x6C == 0` and read `+0x6D`.

---

## 4. Research reserve

Available right now for player `p` = the button conditions (all read `0x918CCD` / `0x9342EC` in the game; the mod
substitutes `p` and the candidate building):

| Kind | Rule | Evidence |
|---|---|---|
| upgrade (kind 2), index `i` | building of type `0x838284[i]`, complete, idle; `0x919310[p] & flag[i]`; `!(0x919350[p] & flag[i])`; level counter of `group[i]` equals the level the button asks for (0 for the first of a pair, 1 for the second) | `FUN_004e3180` (arrows), `4E31F0` (swords), `4E3400` (shields), `4E3240` (siege), `4E3470` / `4E34E0` (ships) |
| ranger / berserker upgrade 24 / 28 | also keep or castle (`0x91B54C` or `0x91B56C`), counter `0x918C6C == 0` | `FUN_004e3290` |
| longbow 25, scouting 26, marksmanship 27 (and orc) | counter `0x918C6C != 0` (ranger done) and the line counter `0x918C7C` / `0x918C8C` / `0x918C9C == 0` | `FUN_004e32f0`, `4E3340`, `4E3390` |
| spell (kind 1), upgrade index `i`, bit `b = flag[i]` | `0x919290[p] & b` (allowed), `!(0x919250[p] & b)` (not known), `!(0x9192D0[p] & b)` (not in research) | `FUN_004e38e0` |
| healing, exorcism, bloodlust, runes | also paladin / ogre-mage upgrade known (`0x919250[p] & 0x100000`) | `FUN_004e3920` |
| keep / castle (kind 3 on the hall / keep) | ALOW `0x8000000` + barracks / ALOW `0x10000000` + stables + lumber mill + blacksmith | `FUN_004e36f0`, `FUN_004ac8f0` |
| guard / cannon tower (kind 3 on a scout tower) | lumber mill / blacksmith | `FUN_004e37b0`, `FUN_004ac8f0` |

Costs: kinds 1 / 2 use `0x9188E0[i]` gold, `0x918948[i]` lumber, `0x9189B0[i]` oil (u16, plain numbers) and time
`0x9188A8[i] * 2`; kind 3 uses the unit tables of the TARGET type (`0x917980 / 0x9179F0 / 0x917A60`, x10). Upgrade
index, group and flag list: `data_tables.md` section 2.

Upgrade levels per line (u8[16] each, `0x8C03F8[group]`): 0 `0x918BEC` missile weapons, 1 `0x918BFC` melee weapons,
2 `0x918C1C` shields, 3 `0x918C2C` ship cannons, 4 `0x918C3C` ship armor, 6 `0x918C5C` catapult / ballista, 7
`0x918C6C` ranger / berserker, 8 `0x918C7C` longbow / lighter axes, 9 `0x918C8C` scouting, 10 `0x918C9C` marksmanship
/ regeneration. Which unit benefits (damage `FUN_004bdad0` / `FUN_004bda20`, armor `FUN_004bd9b0`):

- basic damage + melee counter x 2 for every weapons-upgradable (`0x9181C0`) unit that is not a ship (flag 8), not
  siege (flag 4) and not archer / axethrower / ranger / berserker; ships + ship cannons x 5; siege + catapult counter
  x 15;
- piercing damage + missile counter x 2 for types 8, 9, 0x12, 0x13; rangers with marksmanship add a bonus;
- armor + shields x 2 (land) or ship armor x 5 (ships) for armor-upgradable (`0x918230`) types.

Multipliers per level are the bytes at `0x8C11DC` (`data_tables.md`, RE_NOTES "Attack range").

**Traps:** `StartProduction` does not check "already done" or "in research" (only the level counter for indices
< 0x18). The finish handlers apply a result only `if (inResearch[owner] & flag)`, then clear the bit:

```c
// FUN_004acca0 (kind 2 finish), FUN_004acbc0 (kind 1) likewise with 0x9192D0 / 0x919250
if (flag & upgradesInResearch[owner]) { upgradesInResearch[owner] ^= flag;
  if (!done) refund; else levelCounter[group][owner]++; ... }
// else: nothing at all, no refund
```

Two parallel starts of the same research cost twice and give one result; a spell that is already known can be paid
for again. The mod must check both masks and the done state itself.

---

## 5. Unit costs, times and food

`unitdata.dat` values (file; the game overrides gryphon / dragon gold to 2250 and archer / ranger piercing to 7 at
load, `data_tables.md`; the mod's `[costs]` may change the live tables, so always read the tables):

| Type | HP | Time byte | Gold | Lumber | Oil | Armor | Basic | Piercing |
|---|---|---|---|---|---|---|---|---|
| footman / grunt | 60 | 60 | 600 | 0 | 0 | 2 | 6 | 3 |
| peasant / peon | 30 | 45 | 400 | 0 | 0 | 0 | 3 | 2 |
| ballista / catapult | 110 | 250 | 900 | 300 | 0 | 0 | 80 | 0 |
| knight / ogre, paladin / ogre-mage | 90 | 90 | 800 | 100 | 0 | 4 | 8 | 4 |
| archer / axethrower | 40 | 70 | 500 | 50 | 0 | 0 | 3 | 6 |
| ranger / berserker | 50 | 70 | 500 | 50 | 0 | 0 | 3 | 6 |
| mage / death knight | 60 | 120 | 1200 | 0 | 0 | 0 | 0 | 9 |
| dwarves / sappers | 40 | 200 | 700 | 250 | 0 | 0 | 4 | 2 |
| tanker | 90 | 50 | 400 | 200 | 0 | 10 | 0 | 0 |
| transport | 150 | 70 | 600 | 200 | 500 | 0 | 0 | 0 |
| destroyer | 100 | 90 | 700 | 350 | 700 | 10 | 35 | 0 |
| battleship / juggernaught | 150 | 140 | 1000 | 500 | 1000 | 15 | 130 | 0 |
| submarine / turtle | 60 | 100 | 800 | 150 | 900 | 0 | 50 | 0 |
| flying machine / zeppelin | 150 | 65 | 500 | 100 | 0 | 2 | 0 | 0 |
| gryphon rider / dragon | 100 | 250 | 2500 (2250 at runtime) | 0 | 0 | 5 | 0 | 16 |
| keep / stronghold (upgrade) | 1400 | 200 | 2000 | 1000 | 200 | 20 | | |
| castle / fortress (upgrade) | 1600 | 200 | 2500 | 1200 | 500 | 20 | | |
| guard tower (upgrade) | 130 | 140 | 500 | 150 | 0 | 20 | 4 | 12 |
| cannon tower (upgrade) | 160 | 190 | 1000 | 300 | 0 | 20 | 50 | 0 |

Food: the train check (`FUN_004ac610`, units only):

```
supply = food[p] (0x91B50C), clamped to 200;  used = units[p] (0x91B38C) - foodFree[p] (0x91B6AC)
if (supply <= used) return "stat_txt_438"      // "Not enough food...build more farms."
```

`0x91B38C` counts every created non-building unit (the Eye of Kilrogg included, `food_supply.md`), units in training
are not in it, so a unit whose training started with the last free food still appears when finished even if other
buildings started units too (`FUN_004acb00` does not check food).

---

## 6. Side questions

### (a) Game speed

```c
void FUN_004c4910(uint speed, char netSave) {            // setter
  DAT_008c1e64 = (netSave && netGame) ? DAT_00964ca8 : -1;
  if (DAT_0091c178 == 0) speed = 0x8C1E50[speed];       // classic 7-level map, never in Remastered
  DAT_00964ca8 = speed;
}
// FUN_004f2650(speed): network ? FUN_0049c970(speed) : queue {0x0A, ..., [6] = 0x27, [8] = speed}
// FUN_004f16f0 case 0x27: if (speed < (remastered ? 9 : 7)) FUN_004c4910(speed, 0);
// FUN_0049c3f0 (the other executor): if (speed < 9 && !netGame) FUN_004c4910(speed, 0);
// options dialog OK (FUN_004d7f30): copies the option block, then (classic mode or no network game)
// FUN_004f2650(newSpeed), and FUN_004c4910(oldSpeed, 0) until the queued command applies it
```

The loop reads `0x83A478[0x964CA8]` every step (`data_tables.md` section 6), so a change applies at the next step.
Other writers: game start (`FUN_004c57f0`, `FUN_004c4910(0x916556, 1)`), a network setup path (`FUN_004b1de0`: speed
6), `FUN_004b2280` [unverified: probably settings or save loading]. Recommended: `FUN_004f2650(speed)` from the tick,
single player only: identical to the player using the menu.

### (b) Rally points and new units

No rally point: no string containing "rally", "gather" or "waypoint" in the exe (the two "rally" hits in `enUS.json`
are campaign briefings), and the unit finish handler has no destination logic. `FUN_004edfa0`:

```c
DAT_009349cc = layer[type];                                              // 0x918310
if (!FUN_004e2e40(&pos, *sizeEntryOfBuilding, origin, 0xC, FUN_004ee080)) { message_exit_blocked; return 0; }
unit = FUN_004edb10(pos.x * 32, pos.y * 32, type, owner);                // order 2, next order 0x3C
```

A mod-side rally point would issue a move (on-map tile, `game::IssueOrder` guard) to units whose creation serial
(`+0x14`) is newer than the last one seen and whose type the building produces.

---

## Recommended way for the mod to train a unit

Single player only, from the tick hook after the multiplayer gate (same as every other mod feature):

1. Candidate building `b`: owner = local player, `(state & 0x0F) == 0`, `state & 0x80`, `(b[0x1C] & 0x10) == 0`.
   Skip a building the player has selected (the selection array at `0x9342F0`, primary `0x9342EC`) so the mod never
   pre-empts a click [unverified: array length and semantics].
2. Unit type `t` for `b`: `0x838248[t]` equals `b`'s type (keep / castle -> hall), ALOW `0x919210[p]` bit of `t`
   (section 2 table), the requirement: call `0x8C0428[t](b)` (pure reads, returns nonzero when met) or mirror it.
3. Resources: `gold[p] >= price*10` and lumber, oil likewise (plus the design's reserve). Food:
   `min(food[p], 200) - (units[p] - foodFree[p] + inTraining[p]) - 1 >= 4`.
4. `StartProduction(b, t, 0)` (`0x4ACE10`, cdecl, 3 args, caller cleans 0xC). Returns 1 = started and paid.
   With steps 2 and 3 true it cannot fail at the cost step, so it never shows a "Not enough ..." message.
5. Research / building upgrades: the same call with kind 1 / 2 / 3 after the section 4 checks, which the function does
   NOT do itself.

Do not use `FUN_004f1350` (it acts on the current selection through the command queue) and do not call the AI
production functions `0x8C0540[...]` for human buildings (they read computer-only AI state).

## Risks

- **Multiplayer:** a direct `StartProduction` changes simulation state on one peer. Tick-hook gate, like IssueOrder.
- **Double payment for research:** no "done" / "in research" check in `StartProduction`; a second start is paid and
  lost (section 4). Always check `0x919350` / `0x9192D0` and the done state first.
- **ALOW ignored by the engine path:** `StartProduction` never reads `0x919210` / `0x919310` / `0x919290`; a campaign
  mission that forbids a unit would still get it from the mod unless the mod checks the masks.
- **Requirements off:** with `0x91B03D == 0` the kind-0 and kind-3 validators accept missing prerequisite buildings;
  the mod's own checks must not rely on the validator for that.
- **Food overshoot:** the engine ignores units in training; the mod must add `0x9193F0[p]`.
- **Messages:** a failed cost step shows a message to the local player every time; pre-check to avoid spam. A unit
  that cannot be placed ("exit blocked", or no free unit slot) is refunded at the end with a message: back off from a
  building after such a result (the building went back to idle without a new unit).
- **Player clicks:** a building the mod just started is busy; the player's queued command for it is then dropped
  silently by `StartProduction` (busy check first, no message).
- **Debug print:** `StartProduction` on a non-building prints `bldg: %d %d %d` before failing: only pass buildings.
- **Refund drift:** cancel / failure refunds use the prices in the tables at that moment (vanilla).
- **Speed:** only through the command (`FUN_004f2650`) or a direct write in single player; never in multiplayer.
- **Savegames:** production state, the in-training counter and the ALOW masks are saved; the mod needs no state of its
  own to resume after a load.

## [unverified]

- Real-time pace of the production step: whether the order-0x25 step runs every simulation step or on an animation
  cadence (the totals are `2 x time byte` progress points).
- When `0x91B03D` is 0 (set to 0 at `0x4B1194` inside `FUN_004b1030` depending on the player slot table `0x91AD94`).
- The ALOW unit / building bit names not exercised by a condition function (13, 16, 20, 22, 24..26, 30, 31); farm = bit
  16 comes from the AI farm callback (`food_supply.md`).
- `FUN_004dfac0` / `FUN_004dfad0` as "first unit of the commanding player's selection", and the selection array
  `0x9342F0` length.
- Which of `FUN_004b2280`'s callers set the speed from a savegame or from settings, and whether a speed changed by the
  mod persists into the player's preferences (`bInitialGameSpeed`).
- Whether a PUD's ALOW section can preset bits in `0x9192D0` / `0x919350` (the game treats them as "in research").
