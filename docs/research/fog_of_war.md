# Fog of war: where the game keeps it and why it is lost (static RE, Warcraft II: Remastered 1.0.2.2818)

The author (2026-09-27): "My preferences aren't saved. I keep turning fog of war off, over and over and over."
Built as `[general] fog_of_war` (`src/fog.cpp`), default "game" (the mod does nothing).

## Where the game keeps its settings

- Folder: `FOLDERID_SavedGames` (GUID `4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4` at `0x889C60`, `SHGetKnownFolderPath` in
  `FUN_004f7b30`) + `Warcraft2Remastered\`, so `%USERPROFILE%\Saved Games\Warcraft2Remastered\`. The savegames live
  there too.
- `Warcraft2.ini` (`FUN_00589fb0`, header "; Global (and legacy) game settings"): one `[game]` section. Its key names
  are the strings at `0x84FB44..0x84FD8C` (speed, mscroll, kscroll, music, ... use_grid_keys). There is **no fog key**,
  and the fog flag `0x918CCF` has no data reference from that table.
- `GlobalSave.json` (`FUN_00611f30`, `FUN_004f7b30`): only `missionsCompleted` (todOrc, todHuman, bdpOrc, bdpHuman).
- There are no cloud strings in the exe. The ClientSdk `SettingsService` / `ISettingsV1` classes are the Battle.net client
  interface, and `Battle.net\Launch Options\W2R` holds the launch arguments. On the author's machine the ini is
  written locally, is writable, and has the keys he changes, so it is not a sync problem.

## The flag

`0x918CCF` (RVA `0x518CCF`, `kRvaFogOfWar`): uint8, 1 = fog on. It sits next to the local player byte `0x918CCD`.

| Where | What |
|---|---|
| `0x4CDC28` in `FUN_004cd950` (INIT.cpp, startup) | `mov byte [0x918CCF], 1` on **every start** (nothing reads it back from a file afterwards) |
| `FUN_004d39d0` | the one game-logic reader: if set, `memset(visible 0x91AD5C, 0x10, 0x4000)` and `memset(mask 0x91AD64, 0xFF, 0x4000)` (re-fog everything; units re-reveal what they see). Called from the simulation step (`FUN_004c4e80` / `FUN_004c5190`) when the step countdown `0x91C590` (reloaded with 0x65, 0x64 with the Remastered ruleset) hits 0, so about every 100 steps |
| Options screen `FUN_005400e0` (`"fog_of_war"`, `option_preference_fog_of_war`, data refs `0x540AEE` / `0x540B0F`) and the in-game dialog `FUN_004df040` | write it (`on` = 1). `FUN_004df1b0` remembers the value when the dialog opens (`0x933E0C`) and, when it closes with fog gone from on to off, calls `FUN_004d3960` (`0x4DF23E`) |
| `FUN_004d3960` (`kRvaRevealExplored`) | `memcpy(visible, explored, 0x4000)`, then for each of size x size tiles: mask = player bit (`1 << 0x918CCD`, or the vision-sharing byte `0x919678[local]` with the Remastered ruleset) where visible is 0x10, else 0 |
| `FUN_004d7ec0` / `FUN_004d7f30` | Options dialog snapshot and cancel: saved into / restored from the dialog copy (+0x44) |
| savegame writer `FUN_004ab090` (`0x4AB3B8`) / loader `FUN_004aa6e0` (`0x4AA9D4`) | stored in the save header at +0x324: **a savegame brings back the fog it was saved with** |
| `FUN_004d6800` | custom games: `flag = !(0x91AF58 & 0x100)` (the game-options word). Only reached from `FUN_004d6d30` when `0x918CCE != 0` (custom game), and from the network branch of `FUN_004d6a60` |
| cheat "showpath" (`FUN_004b4390`) | clears it |

So the flag is lost on every start, and it is set again by any savegame that was made with fog on and by any custom game.
It stays as it is through campaign missions within one session (`0x918CCE == 0`: `FUN_004d6800` is not called).

## Order at map start

New map (`FUN_004c4280`, param 0): `FUN_004d2b40` (tables; our hook at `0x4D2C46` calls FinalizeTables at the end) at
`0x4C4559`, **then** `FUN_004d6a60` at `0x4C45A0`, which reaches `FUN_004d6800` for a custom game. A value written by the
new-map hook would be overwritten there in a custom game, so the mod only arms a flag in the hook and writes on the
first simulation step.

Savegame (`FUN_004c4280`, param != 0): `FUN_004e0ab0` -> `FUN_004aa6e0` restores the flag. The new-map hook does not
fire.

## How the mod sees a game start

The step counter `0x919824` (`kRvaGameStep`):
- the game loop `FUN_004c5220` sets it to 0 when it starts (`0x4C527F`);
- `FUN_004c4e80` increments it after each step (`inc dword [0x919824]` at `0x4C5059`, after the step call `0x4C5001`);
- `FUN_004c5190` (from `FUN_004c57f0`) runs a step without counting;
- the savegame loader restores its own value (`0x4AAE84`), and the save writer stores it (`0x4AB8B7`).

The mod treats a tick as the start of a game when the counter did anything but stay the same or go up by one since the
previous tick, when the new-map hook armed it, or on the first tick of the session. Only then is `fog_of_war` "off" or
"on" written (and once more when the setting itself changes on a config reload). The Options screen still changes it
for the rest of that game. A savegame whose counter equals the running one (+1) would go unnoticed; that takes loading
a save of the very step being played.

When the mod turns fog off it calls `FUN_004d3960`, as the Options screen does, so ground that is already explored
shows at once. When it turns fog on it writes only the flag; the game's own re-fog pass takes over within 100 steps.
Nothing is written in multiplayer: the tick returns before `fog::OnTick`, and the new-map hook returns before
`fog::OnNewMap`.

## Not the same thing

The per-unit byte +0x28 (`kOffFogMask`, `FUN_004f0600`) is the "under fog for player p" bit set that the picking code
reads. It follows from the visible map and is not the setting.
