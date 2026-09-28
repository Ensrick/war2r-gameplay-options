# When the game declares a victory (static RE, Warcraft II: Remastered 1.0.2.2818)

Evidence for the "all enemies defeated" rule in `src/production.cpp` and `src/farms.cpp` (`game::EnemiesDefeated`,
`src/world.cpp`). Static analysis only (capstone over a copy of the exe), plus one savegame decode. VA = RVA + 0x400000.

## Summary

- The objective check is **periodic, nothing else**. Nothing triggers it when a unit dies, it does not depend on what
  your units can see, and there is no extra delay after the last enemy dies.
- It runs every **51 game steps** and tests for a win **only while the local player has nothing in training and no
  building under construction**. Anything that keeps one of your buildings busy the whole time postpones the victory
  until a check happens to find both counters at 0.
- The enemy condition reads counters only: for every player 0..7 except the local one, no building (complete or not)
  and no unit other than flying machines / zeppelins, transports and tankers. Alliances and the controller table are
  not read.
- Observed 2026-09-25: with auto-production training every 2..10 s at three buildings and the farm feature starting a
  farm every 15..40 s, a "destroy all enemy forces" mission ended about 10 minutes after the last enemy died. The
  autosave taken 4 minutes after that death holds no unit of any enemy player, 3 units in training and 1 building
  under construction for the player.

## The timer and the dispatch

```
0x4C504A  call 0x4F4F60                  ; in the game step (it also increments 0x919824)

0x4F4F60: if (!net 0x922F5B && !0x91BCD4 && (cheats 0x91B270 & 0x200)) return;   ; the "no victory" cheat bit
          ax = word [0x937668]; word [0x937668] = ax - 1;
          if (ax != 0) return;
          word [0x937668] = 0x32;                                  ; 50 -> fires every 51 calls
          jmp dword [0x93766C]                                     ; the objective routine
```

`FUN_004F4520` (called from the load / new-map path at `0x4C4792`) picks the routine from the objective word
`0x9191C4` (saved in the game globals: packer `0x4AB32C`, unpacker `0x4AA976`) and sets the countdown to 1. The
new-map path first stores the objective word (`0x4C4473..0x4C44A9`: 0x100 when `0x918BE8 > 1` or `0x918CCE != 0`, else from the mission
table `0x8C1BB8`). Values 0..0x1E select per-mission routines. Case 7 sets the word to 0x100, case 4 to 0x200, and
every value above 0x1E keeps the word and uses `0x4F43E0` (`jmp 0x4F42A0`). The per-mission routines
`0x4F43F0`, `0x4F4420`, `0x4F4470`, `0x4F4490`, `0x4F44C0`, `0x4F4900` call `0x4F42A0` first and add their own goals.
Case 5 (`0x4F43F0`) switches to 0x100 / `0x4F43E0` when `0x9191F0[local]` becomes nonzero. `0x4F44C0` (case 3)
sets the word to 0x100 only for the call and also needs a castle.

The winning / losing routines are stored into the same pointer: `0x4F4100` = victory, `0x4F40C0` = defeat.

## The check, FUN_004F42A0

```
0x4F42A0: cheats = [0x91B270]
          if (cheats & 0x100) -> victory                       ; the victory cheat
          if (cheats & 0x80)  -> defeat                        ; the defeat cheat
          L = byte [0x918CCD]; ax = word [0x9191C4]
0x4F42C4: if (word [0x9193F0 + 2L] != 0) goto 0x4F4368         ; units in training
0x4F42D3: if (word [0x919410 + 2L] != 0) goto 0x4F4368         ; buildings under construction
0x4F42E2: if (word [0x91B38C + 2L] == 0) {                     ; no unit of the player's own
              if (word [0x91B4EC + 2L] == 0 || gold [0x919128 + 4L] < 400) -> defeat
              goto 0x4F4368
          }
0x4F4316: if (!(ax & 0x100)) goto 0x4F4368                     ; bt ax, 8: only "destroy all enemy forces"
          for (p = 0; p < 8; ++p) { if (p == L) continue;
0x4F4324:     if (word [0x91B3AC + 2p] != 0) return 0;          ; buildings, complete or not
0x4F432F:     if ((u16)(word [0x91B38C + 2p] - [0x91B80C + 2p] - [0x91B78C + 2p] - [0x91B68C + 2p]) != 0) return 0;
          }                                                    ; units - flying machines - transports - tankers
          -> victory (0x93766C = 0x4F4100, return 2)
0x4F4368: if (ax & 0x600) { ... the 0x200 / 0x400 objectives ... }
          return 0
```

The jump at `0x4F42C4` / `0x4F42D3` skips the defeat test AND the victory test: while anything of the player's is in
training or under construction, nothing is decided.

## The counters

| VA | What | Writers |
|---|---|---|
| `0x9193F0` | u16[16] units in training | +1 StartProduction kind 0 (`0x4AD076`), -1 when training ends (`0x4ACB89`, `0x4ACBAE`) |
| `0x919410` | u16[16] buildings under construction | +1 when a building is placed (`0x4EDE22`, not while a map loads), -1 finished (`0x4ED69F`, in the construction-complete path) or destroyed unfinished (`0x4EE53B`); an unfinished building that changes owner moves its count (`0x4ED1F8` -1 old, `0x4ED203` +1 new) |
| `0x91B38C` | u16[16] units (every non-building) | CountAdd `0x4B531F` / CountRemove `0x4B5784` |
| `0x91B3AC` | u16[16] buildings, complete or not | CountAdd `0x4B5315` / CountRemove `0x4B577A` |
| `0x91B80C` | u16[16] flying machines, zeppelins | per-type counter `0x8C0B80[0x28 / 0x29]` |
| `0x91B78C` | u16[16] transports | `0x8C0B80[0x1C / 0x1D]` |
| `0x91B68C` | u16[16] tankers | `0x8C0B80[0x1A / 0x1B]` |

`0x91B38C..0x91B86B` are not in the savegame: `FUN_004B5390` zeroes them on every load and the unit loader
`FUN_004EE210` counts every unit with `(state & 7) == 0` again. `0x9193F0` and `0x919410` ARE saved
(packer `0x4AB43F`, `0x4AB477`).

## The cheat word 0x91B270

Set by `FUN_004B3D30`. Bits read here: 0x100 instant victory, 0x80 instant defeat, 0x200 skip the check. A load
masks the word to `& 0x24002000` (`0x4C457F..0x4C4590`), so none of the three survives a load.

## What the mod does with it

`game::EnemiesDefeated` implements `0x4F4316..0x4F434F` exactly (16-bit subtraction, players 0..7, no alliance), and
only when the objective word has bit 0x100 and the game is single player. While it holds, auto-production and the
farm feature start nothing new for the local player. Nothing already queued is cancelled: the queue drains, both
counters reach 0, and the game's own check ends the mission at its next run. No game code or counter is written.

Every other reference to the two counters (xref scan of the exe) is the savegame packer / unpacker (`0x4AAA42`,
`0x4AAA74`, `0x4AB442`, `0x4AB47A`), the reset at `0x4B55E1..0x4B55EF` and the check itself. Of the mod's own orders
only these two raise them: auto-production (StartProduction kind 0) and the farm feature (the Build handler).
Worker auto-repair picks finished buildings only (`src/workers.cpp`), and repairing a finished building touches
neither counter. No other feature issues a build order or calls StartProduction.

## Caveats

- The player's own training or building also holds the mission open (vanilla behaviour, not changed).
- A construction site that nobody finishes keeps `0x919410` above 0 for good, and the victory never comes until it
  is finished or destroyed. Not verified whether any game path leaves such a site.
- The per-mission routines with extra goals (cases 0..0x1E other than 7) are not covered by the rule unless their
  objective word carries 0x100.
