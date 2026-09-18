# Eye of Kilrogg: spell action, computer scouting, human feasibility

Target: `Warcraft II.exe` 1.0.2.2818 (same build as `docs/RE_NOTES.md`). All VAs at preferred base 0x400000.
Static analysis only (Ghidra project `war2r_a`, capstone helpers). Raw decompiles: `C:\Tools\ghidra_projects\out\eye_*.c`.
Anything not read directly from the exe is marked [unverified].

## Short answers

1. **Spell action.** Order 0x30 executes `FUN_004e2040` (entry 0x30 of the spell-action table at `0x8C1590`,
   dispatched by `FUN_004a8970` from the cast animation's hit frame). It deducts the mana cost, sets the caster to
   order 2 (stop), and creates unit type 0x2D at the CASTER's tile with the CASTER's owner via `FUN_004edfa0`. There is
   no owner-specific code in the action. Lifetime is the generic UDTA "decay rate" mechanism: decay rate for type 0x2D
   is 3 in `unitdata.dat`, the create code sets mana = 255 and the per-cycle timer pass burns 1 mana every 3 cycles,
   then 5 hp every 3 cycles: about 828 game cycles total. The only computer-specific thing at spawn is the generic
   AI new-unit hook `FUN_004cd080`, which gives every computer-owned FLYER AI order 4 (patrol).
2. **Computer scouting.** There is no eye-specific AI. No code compares a unit type against 0x2D except one command
   filter (Remastered lets humans give Patrol to type 0x2D). A computer eye moves because it is a flyer with AI order
   4: the think loop `FUN_004ccce0` (every 50 cycles) calls `FUN_004cc400`, which, when the unit is idle (order 2),
   picks a random tile within +/- mapSize/4 of the unit's position (game RNG), clamps it to the map and calls
   `IssueOrder(unit, x, y, 0, 0x4D88F0)` (order 4, move-patrol). It is a random walk, not real scouting.
3. **Human feasibility.** `FUN_004cc400` reads no per-player AI structure at all (unit fields, map size, game RNG),
   so calling it on a human eye is memory-safe. It is still not recommended: it is a blind random walk, it advances
   the game RNG, and it uses order 4. The think loop itself can never serve a human eye (it requires
   `controller[owner] == 1` and `unit+0x5E != 0`). Recommended: the mod's own rule using `IssueOrder` + move handler
   `0x4D8690`, steering by the local player's explored map `*(uint8_t**)0x91AD60` (0x10 = never explored).
4. **Human cast path.** The UI button action `FUN_004e2b20` special-cases order 0x30: it sends the order command
   immediately with x = 0, y = 0, target = 0 (`FUN_004f3680(0,0,0,0x30)`), every other spell enters targeting mode.
   The command handler sets the pending-spell global to 0x30 and calls `IssueOrder(unit, 0, 0, NULL, 0x4E2970)`
   (through the thin wrapper `FUN_004ed8c0`). So the mod's existing IssueOrder path is the human path. The
   coordinates do not matter: the range table entry for order 0x30 is 0xFF ("always in range") and the action spawns
   at the caster's own tile.

## New addresses

| VA | What | Convention | Evidence |
|---|---|---|---|
| `0x8C1590` | Spell/attack-frame action table, `void(*)(Unit*)` indexed by order id (+0x2E). 0x30 -> `0x4E2040`, 0x31 -> `0x4E1A10`, 0x26 -> `0x4E2720` | data | `FUN_004a8970`: `mov eax,[eax*4+0x8c1590]; call eax` at 0x4A8993 |
| `0x4A8970` | Dispatches the action above; skips when `unit+0x1E & 0x4000`; zeroes invis timer +0x44 first | cdecl(unit) | decompile, caller `FUN_00484990` (animation script step) |
| `0x4E2040` | Eye of Kilrogg action | cdecl(unit) | decompile below |
| `0x4EDFA0` | CreateUnitNear(x, y, type, owner, sizeEntry*): finds a free spot within 12 tiles on the type's movement layer, then `FUN_004edb10` | cdecl, 5 args | decompile; callers: eye action, building "unit trained" `FUN_004acb00` |
| `0x4EDB10` | CreateUnit(xPixel, yPixel, type, owner): allocates the slot, fills defaults, calls `FUN_004e87b0` | cdecl, 4 args | decompile below |
| `0x918380` | Decay rate by type (u8[110], UDTA "decay rate"). Type 0x2D = 3, skeleton 0x37 = 100 | data | create + timer pass; value read from `Data\Rez\unitdata.dat` file offset 4646 + type |
| unit `+0x74` | Decay countdown (u8) for non-casters; mana regen countdown (reload 0x28) for casters | field | `FUN_004ef480` |
| unit `+0x14` | Creation serial (u32, from counter `0x8C1F6C`), unique per created unit | field | `FUN_004edb10`: `*(int*)(u+0x14) = DAT_008c1f6c++` |
| `0x4EF480` | Per-cycle unit timer pass (spell timers, mana regen, decay). Called once per simulation step at the end of `FUN_004eea80` | cdecl() | decompile below; RE_NOTES already cites its body at 0x4EF4A0 |
| `0x4E87B0` -> `0x4CD080` | AI new-unit hook: zeroes +0x58..+0x67, then for `controller[owner]==1` assigns the AI order byte | cdecl(unit) | decompile below |
| `0x8C3E10` | AI-order function table indexed by +0x5E: 0 nop `0x4857C0`, 1 defend `0x4CC650`, 2 attack `0x4CC740`, 3 transport `0x4CC810`, 4 patrol `0x4CC400`, 5 guard `0x4CC550`, 6 ship patrol `0x4CC4B0`, 7 nop | data | table dump + think loop `call [eax*4+0x8c3e10]` at 0x4CCE96 |
| `0x4CC400` | AI patrol (what moves a computer eye) | cdecl(unit), plain `ret` | decompile + disasm below |
| `0x4CCEC0` | SetAiOrder(unit, aiOrder, u32* xy): writes +0x58 dest, +0x5C region, +0x5E | cdecl, 3 args | decompile |
| `0x4A0200` | Game RNG: `seed = seed*0x15A4E35 + 1; return (seed>>16)&0x7FFF`, seed at `0x916B58` | cdecl() | decompile |
| `0x4D88F0` | Order-4 (move-patrol) handler: SetOrder(unit,4) (non-aggressive types run the move handler first) | cdecl(unit) | disasm |
| `0x4D8690` | Order-3 move handler (already in RE_NOTES). For a non-peon/non-tanker/non-transport with no target it is just `SetOrder(unit,3)`. NOTE it indexes the unit grid with the order x,y, so x,y MUST be inside the map | cdecl(unit) | decompile `FUN_004d8690` + `FUN_004d8090` |
| `0x8C13A0` | Per-order step-action table (`int(*)(Unit*)`). 3 and 4 -> `0x4A9F10` (on arrival: `+0x54 = 0; SetOrder(unit,2)`). 0x30 -> `0x49BB60` (`return 0`) | data | table dump, `FUN_004eea80` `call [order*4+0x8c13a0]` |
| `0x8C1744` | Order range table (u8[order]); 0xFF = always in range, 0xFE = use weapon range. Order 0x30 = 0xFF, 0x26 = 0xFF | data | `FUN_004d9420` first lines; dump |
| `0x8C16C8` | Order flag words (u16[order]); bit 4 = needs range/approach check. All spells = 0x000C | data | `FUN_004eea80`, `FUN_004d9bb0` |
| `0x8C1804` | Per-order "clear target" bytes used by the spell order handler. 0x30 = 1 | data | `FUN_004e2970`; dump |
| `0x4E2B20` | UI spell button action (param = order id) | cdecl(u8) | decompile below; button cards at `0x8C77E8`, `0x8C7A28` (24-byte records, action fn at +12, order id at +17) |
| `0x4F3680` | UI SendOrderCommand(x, y, target, order): ack sound + `FUN_004f2410` (queue command) | cdecl, 4 args | decompile |
| `0x49BE20` / `0x4F2190` | Order command executors (two packet layouts). Set pending spell `0x9348BC`, walk the commanding player's selection, call `FUN_004ed8c0(unit,x,y,target,handlerTable[order])`, clear pending spell | cdecl(packet*) | decompile below |
| `0x4ED8C0` | Command-side IssueOrder wrapper: same busy-order guard, clears `+0x1C & 0x40` for transports, tail-calls `0x4EF210` | cdecl, 5 args | decompile |
| `0x4F1270` | OrderTypeMask(order, type): the ONLY code that tests type 0x2D. When `0x91C178 != 0` and order == 5 (patrol), types 0x0E, 0x0F, 0x28, 0x29, 0x2D are accepted (mask 0x1F) | cdecl(u8,u8) | disasm 0x4F1270..0x4F12AD; inlined copy in `FUN_004f2190` |
| `0x91AD60` | `uint8_t*` explored map of the LOCAL player, index `y*mapSize + x`, 0x10 = never explored, 0 = fully explored, 1..0x0F = partly revealed edge shapes. Allocated 0x4000 bytes, memset 0x10 at map start | data (pointer) | `FUN_004c6110` (GAMEMAP.cpp line 399), `FUN_004d3a10`, `FUN_004f0d60` |
| `0x91AD5C` | `uint8_t*` currently-visible (fog) map, same layout; re-fogged to 0x10 every 100 cycles when fog is on (`0x918CCF != 0`) | data (pointer) | `FUN_004d39d0`, `FUN_004d3a10` |
| `0x91AD64` | `uint8_t*` per-tile bitmask of players that do NOT currently see the tile (reset 0xFF, bit cleared on reveal) | data (pointer) | `FUN_004d3a10`, `FUN_004d3960` |
| `0x4D38F0` / `0x4D3A10` | Reveal core / per-tile reveal callback. The explored and fog maps are only written when the revealing player is the local player (or shares vision with it) | cdecl | decompile |
| `0x91C178` | Ruleset flag that enables Remastered-only behaviour (order resume byte +0x8D, eye/sapper/flyer patrol, 100 vs 101 cycle vision period). Exact meaning [unverified] | data (u32) | `FUN_004ef080`, `FUN_004f1270`, `FUN_004c5190` |
| unit `+0x8D` / `+0x8E` | Resume-order byte (0x3C none, 5 patrol, 10 attack-move) and its cooldown; only used when `0x91C178 != 0`. The command executors write `+0x8D = 0x3C` before issuing a new order | field | `FUN_004ef080`, `FUN_0049be20` |

## Evidence

### Q1. The action (order 0x30)

```c
// FUN_004e2040
if (unit->mana < manaCostByOrder[unit->order]) { SetOrder(unit, 2); return; }
unit->mana -= manaCostByOrder[unit->order];          // 0x8C5EB8 + 0x30*2 = 0x8C5F18 = 70
SetOrder(unit, 2);
unit->invisTimer = 0;
eye = FUN_004edfa0(unit->x, unit->y, 0x2d, unit->owner, &DAT_00917b84); // 0x917AD0 + 0x2D*4 = size entry of type 0x2D
if (eye != 0) FUN_004c7f20(0x15a, unit, unit, 1, 1); // sound
```

Mana is deducted BEFORE the create call. `FUN_004edfa0` returns 0 with "message_exit_blocked" when no free air tile
exists within 12 tiles, and `FUN_004edb10` returns 0 with "message_cannot_create_units" when the owner has no free
unit slot. In both cases the 70 mana is lost.

Lifetime, from `FUN_004edb10` and `FUN_004ef480`:

```c
// FUN_004edb10 (create)
if (decayRate[type] == 0 || loadingMap) { ... }
else { unit[0x74] = decayRate[type]; unit->mana = 0xff; }

// FUN_004ef480 (once per simulation step, non-caster branch)
if (decayRate[type] != 0 && (unit[0x1e] & 0x20) == 0) {      // 0x20 = preplaced on the map: never decays
    if (--unit[0x74] == 0) {
        if (unit->mana == 0) { if (unit->hp < 5) Kill(unit); else unit->hp -= 5; }
        else unit->mana--;
        unit[0x74] = decayRate[type];
    }
}
```

`unitdata.dat` row for type 0x2D: hp 100, sight 3, decay 3, movement layer 1 (air), flags 0x82 (flyer + sees
submarines), can-target 0. So 255 * 3 = 765 cycles of "mana", then 21 * 3 = 63 cycles of hp drain: 828 cycles.
The eye's remaining life is readable as its mana byte (+0x26).

Spawn-time AI hook (the only owner-dependent code on the path):

```c
// FUN_004cd080, called for every created unit via FUN_004e87b0
unit[0x58..0x67] = 0;
if (controller[owner] == 1 && (flags & 0x1f) != 0 && (flags & 0x300) == 0) {
    if (flags & 0x400)      { aiDest = pos; unit[0x5e] = 3; }   // transport
    else if (flags & 8)     { aiDest = pos; unit[0x5e] = 6; }   // ship
    else if (flags & 2)     { aiDest = pos; unit[0x5e] = 4; }   // FLYER -> patrol (the eye lands here)
    else { FUN_004cd120(unit,&p); FUN_004ccec0(unit, 1, &p); }  // land -> defend
}
```

A human-owned eye keeps `+0x5E == 0`.

### Q2. What moves a computer eye

Think loop (`FUN_004ccce0`, from `FUN_004e89a0` every 0x32 steps), disasm 0x4CCE67..0x4CCE9F:

```
test byte [esi+0x1e],0xf ; jne skip
cmp  byte [esi+0x5e],0   ; je  skip          <- human units stop here
test [type*4+0x9185f0],0x20000 ; je +        <- eye is not a caster
movzx eax, byte [esi+0x5e]
push esi ; mov eax,[eax*4+0x8c3e10] ; call eax ; add esp,4
```

(the enclosing condition also requires `controller[owner] == 1`, see `r_think.c`).

```c
// FUN_004cc400 (AI order 4)
if (unit->order == 2) {
    x = unit->x - (mapSize >> 2) + rand() % (mapSize >> 1);
    y = unit->y - (mapSize >> 2) + rand() % (mapSize >> 1);
    clamp x,y to [0, mapSize-1];
    FUN_004ef210(unit, x, y, 0, FUN_004d88f0);      // push 0x4d88f0; push 0; push y; push x; push unit; add esp,0x14
}
```

Type-0x2D scan: regex over `.text` for `cmp byte [reg+0x27],0x2d` and for any `cmp reg,0x2d` within 160 bytes after a
`+0x27` load. Hits: only 0x4F1297 (`FUN_004f1270`) and its inlined twin 0x4F22E7 (`FUN_004f2190`). Both are the
Remastered patrol-permission filter, not AI.

### Q3. What the AI functions read

| Function | Globals read | Safe for a human unit? |
|---|---|---|
| `FUN_004cc400` patrol | `0x918D10` map size, RNG seed `0x916B58` (written) | Yes. No per-player state |
| `FUN_004cd080` spawn hook | `0x918CAC` controller, `0x9185F0` flags | Already runs for humans (no-op) |
| `FUN_004cc550` guard | per-player AI block stride 0x30: `0x91D67B..0x91D67F + owner*0x30` (base bounding box, only written for `controller==1` in the think loop prologue) | No: never initialised for humans |
| `FUN_004cc650` defend | sector threat table `0x91D960` (stride 0x42), `0x921B61` | Not needed; [unverified] whether maintained for humans |
| `FUN_004cc810` transport | `0x91D65C + owner*0x30`, `0x91D65F`, region map `0x91AD7C` | No |

Explored map evidence:

```c
// FUN_004c6110 (GAMEMAP.cpp)   DAT_0091ad60 = alloc(0x4000) /*line 399*/ ... memset(DAT_0091ad60, 0x10, 0x4000);
// FUN_004d3a10 (per revealed tile, param_1 = tile index)
if (revealingPlayer == localPlayer /* or shared-vision mask when 0x91C178 != 0 */) {
    if (explored[i] != 0) explored[i] = combine[mask + explored[i]*0x11];   // 0x8C45E8; row 0x10 maps to the mask itself
    ...same for visible[i] (0x91AD5C)
}
// FUN_004f0d60: tile index = (xPixel>>5) + (yPixel>>5)*mapSize
```

### Q4. Human cast path

```c
// FUN_004e2b20 (button action, param_1 = order id)
if (!(cheats & 8) && selectedUnit->mana < manaCostByOrder[param_1]) { ShowMessage("stat_txt_435"); return; }
if (param_1 == 0x30) { FUN_004f3680(0, 0, 0, 0x30); return; }   // instant, no targeting
FUN_004e9420();                                                  // every other spell: targeting cursor

// FUN_0049be20 (command executor), per selected unit of the commanding player
_DAT_009348bc = packet->order;
if (DAT_0091c178 != 0) unit[0x8d] = 0x3c;
FUN_004ed8c0(unit, x, y, target, handlerTable[packet->order]);   // -> FUN_004ef210
unit[0x54] = 0;
...
_DAT_009348bc = 0;

// FUN_004e2970 (spell order handler)
SetOrder(unit, pendingSpell); if (clearTarget[pendingSpell]) unit->orderTarget = 0;

// FUN_004d9420 (range check)    if (rangeByOrder[order] == 0xff) return 1;      // order 0x30
```

The computer's `SetOrder(unit, 0x30)` shortcut works for the same reason (range 0xFF, action ignores order x/y/target).

## Recommended implementation

1. **Cast**: reuse the mod's existing spell path: pending spell = 0x30, `IssueOrder(caster, caster.x, caster.y, NULL,
   0x4E2970)`, pending = 0. Gate: type 0x0D / 0x17 (add 0x31 Cho'gall only if wanted; the game's own caster AI
   dispatcher `FUN_004ca4a0` does not handle 0x31), research bit 0x400, mana >= 70. Suggested policy = the game AI's:
   only at mana 255, and only when the caster is idle, because the action ends with `SetOrder(caster, 2)` and drops
   whatever the ogre-mage was doing.
2. **Parity detail for all mod orders**: the command executors write `unit[0x8D] = 0x3C` when `*(u32*)0x91C178 != 0`
   and `*(u32*)(unit+0x54) = 0` after IssueOrder. Do the same for the eye move (and consider it for the existing
   casts), otherwise a stale resume-order can re-trigger in `SetOrder(unit,2)`.
3. **Scout rule** (each pass, per unit with type 0x2D, owner == local human, `state & 0xF == 0`, EffectiveOrder == 2,
   `unit[0x8D] == 0x3C`): choose a destination and call `IssueOrder(eye, x, y, NULL, 0x4D8690)`. On arrival the game
   returns the eye to order 2 and the rule fires again. Destination choice: sample K random tiles with the mod's own
   RNG, score = number of `explored[y*size+x] == 0x10` tiles in the 7x7 window (sight 3) minus a distance penalty;
   if nothing is unexplored fall back to tiles where `visible[...] == 0x10` (0x91AD5C) or to the AI's random-walk
   formula. Budget: mana byte = remaining life (3 cycles per point).
4. Track eyes by pointer + creation serial (+0x14) if the mod should only steer eyes it cast itself, or stop steering
   an eye whose order destination (+0x84/+0x86) differs from the last one the mod issued (the player took over).

## Risks

- x,y MUST be clamped to `[0, mapSize-1]`: the move handler indexes the unit grid with them before any validation.
- `0x91AD60` / `0x91AD5C` are pointers; null outside a match. They describe the LOCAL player only, which is fine for
  the single-player-only mod. With the "no fog / map revealed" setup (`0x91BCD4 != 0` path `FUN_004afe10`) the whole
  explored map is 0, so the fallback branch is required.
- Failed spawn still costs 70 mana (unit cap or no free air tile). Per-player unit-cap check not located [unverified].
- Calling `FUN_004cc400` directly is memory-safe but advances the game RNG and issues order 4; harmless in
  single-player, pointless given the better rule above. Never call guard/transport AI functions on human units.
- Read timing: the tick hook runs before `FUN_004d39d0` / `FUN_004eea80` in the same step, so the fog map is never seen
  half-rebuilt; the explored map is never reset mid-game.
- Remastered semantics of `0x91C178` are [unverified]; the mod only needs to mirror the two writes in item 2.
- Same single-player rule as every other direct order (`kRvaNetGame`).
