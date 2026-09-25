# Auto-scouting flyers, and a command-card button for it (static RE, Warcraft II: Remastered 1.0.2.2818)

The author (2026-09-24): "Is there a way you could make the player's zeppelins and flying machines auto scout? I
would like a button that I could click to toggle auto-scouting on." Part 1 is built (`src/scouts.cpp`, `[scouts]`,
Ctrl+F11). Part 2 (a button) is research only.

## 1. What the scouts use

| Address | What | Evidence |
|---|---|---|
| `0x91AD60` explored map | `uint8[size*size]`, 0x10 = never explored by the local player | `game.h`, eye and holy vision already use it |
| `0x91AD5C` visible map | same layout, 0x10 = fogged right now | `game.h`, `src/eye.cpp` |
| unit +0x28 fog mask | bit p = every footprint tile fogged for player p, rebuilt every step (`FUN_004f0600`) | `autocast_all_spells.md` 2.10 |
| `0x918580` can-target byte | `FUN_004a9810(attacker, target)`: `& 4` when the target is in the air (target `+0x1C & 4`, `0x4A9825`), `& 3` otherwise (`0x4A9876`) | disassembly; selftest checks the instruction bytes |
| `0x917E40` attack range | tiles, per type | `game.h` |
| move handler `0x4D8690` | order 3, x,y on the map | `game.h` |

The game keeps no "last seen" time per tile: the visible map only says fogged or not. The mod keeps its own
`uint32 lastSeen[128*128]` (play time at which each tile was last not fogged), updated on every scout pass (every
250 ms of play) and cleared at every new map. Once nothing is unexplored, a scout flies to the fogged tile unseen
longest (capped at 10 minutes, so all old fog is equal), minus a tile per tile of flight.

Rules as built:
- Candidates: 64 random tiles per decision, half within 24 tiles, half anywhere on the map. Unexplored first: the
  most never-explored tiles in the 9x9 window around the tile, minus the flight distance; only when no sampled tile has
  any unexplored ground, the fog rule above.
- Idle = order stop or stand AND nothing in the next-order slot. A scout must have been idle for `idle_seconds`
  (default 5) before the mod sends it. Arriving where the mod sent it counts as idle long enough: it goes straight on.
- Manual detection: the mod remembers the destination it gave. A unit whose order is not "move to that destination"
  while busy, or that comes to rest more than 2 tiles from it, was ordered by the player: the destination is dropped
  and the idle clock restarts. So a flyer the player parks stays parked for `idle_seconds`, then scouts again.
- Anti-air: every enemy unit or building with the can-target-air bit that the player knows about (not under his fog,
  or a building standing on explored ground) keeps destinations and the midpoint of the flight line `attack range + 2`
  tiles away (Chebyshev). The whole path is not checked.
- Two scouts are never sent within 10 tiles of each other's destination.
- Orders through `game::IssueOrder` (move handler, destination clamped to the map). Single player only (the tick
  returns before any module in a network game).
- Eye of Kilrogg keeps its own code in `src/eye.cpp`; the scout picker could replace it later.

Ctrl+F11: the static search found no compare with `VK_F11` (0x7A) in the classic game code (`0x401000..0x560000`: only
`'z'` character tests at `0x5514F7` / `0x5516B8`) and no F-key switch there either, so how the game reads F-keys at all
is `[unverified]`; the Remastered layer has a key-name table with "F11" at `0x8CC534` (key rebinding UI,
`[unverified]`). Ctrl+F9 and Ctrl+F10 are in use by the mod already; F11 is the next one. The key is configurable.

## 2. A button on the flying machine / zeppelin command card (research only, not built)

Button sets: `0x8C5F48[type] = {u32 count, record*}`, 24-byte records `{u16 pos, u16 icon, cond* +4, greyCond* +8,
action* +0xC, u8 a +0x10, u8 b +0x11, u16 string +0x12, u16 flags +0x14, ...}` (`production.md` section 2).

| Type | Records | Buttons |
|---|---|---|
| flying machine 0x28 | 5 at `0x8C8160` | pos 0 move (icon 83, action `0x4D8AB0`), pos 1 stop (3 records, icons 164..166 by armor level, `0x4D8A60`), pos 3 patrol (icon 184, cond `0x4E3100` = Remastered ruleset only, action `0x4D8AC0`) |
| zeppelin 0x29 | 5 at `0x8C81D8` | the same with the orc icons 84, 167..169, 185; the same array is the Eye of Kilrogg's card too (`0x8C5F48[0x2D]` at `0x8C60B4` points at it) |

Positions 2 and 4..8 of the 3 x 3 card are free on both.

- Condition: `int __cdecl cond(u8 a)` (called at `0x4E8388` with record +0x10); `0x4A1F70` returns 1.
- Action: `void __cdecl action(u8 b)` (called at `0x4E7337` with record +0x11). The selected unit is `0x9342EC`.
- Both the classic card builder (`0x4E7C25..0x4E7CDA`, `0x4E8341..0x4E83BA`) and the Remastered one (`0x52CB2F..
  0x52CDE5`) read the same table, so one edit shows in both.

Feasible: point `0x8C5F48[0x28]` and `[0x29]` at a copy of the 5 records plus a sixth `{pos 4, icon <existing>, cond
0x4A1F70, grey 0x49BB60, action <mod function>, a 0, b 0, string <existing>}` kept in the DLL. The action flips
`[scouts] enabled`. The table is plain `.data` (not saved in savegames), so it is set per map and put back on
unload / in a network game, like the spell numbers.

Risks and open points:
- Icon: the icon id indexes the game's own atlas (classic and HD). A new picture needs new art in both atlases;
  reusing an existing icon (e.g. eye of Kilrogg, or patrol) is safe. Showing on / off would mean swapping the icon id
  in the record on toggle.
- Tooltip: +0x12 is a string id into the game's string table; custom text needs a hook on the string lookup (not
  found yet) or reusing an existing string. `[unverified]`: how the Remastered tooltip resolves the id.
- The Remastered click path was not traced; only the classic `0x4E7337` call is confirmed. If the HD UI calls the
  action with other arguments the mod's function must tolerate that.
- The zeppelin's record array is also the Eye of Kilrogg's: edit the per-type pointer of 0x28 / 0x29, never the
  shared records.
- Hotkey letter: +0x14 flags / the hotkey text come from the string; `[unverified]`.
- Multiplayer: the card is local UI, but the action changes nothing unless the mod runs, and the mod is inert in
  network games; the table must still be restored there so nothing points into the DLL.
