# war2r-autocast (working name)

Autocast for **Warcraft II: Remastered** single-player. Your own casters pick targets and cast the spells you enable,
using the same order path the computer AI uses.

| Caster | Spells (default) |
|---|---|
| Paladin | Heal (on), Exorcism (on) |
| Mage | Polymorph (on), Slow (on) |
| Ogre-Mage | Bloodlust (on) |
| Death Knight | Death Coil (on), Haste (on), Unholy Armor (off) |

Hero casters (Turalyon/Uther, Khadgar, Cho'gall, Teron/Gul'dan class units) follow their class.

## Install / remove

```powershell
.\build.ps1          # needs VS 2022 with the x86 C++ toolchain
.\deploy.ps1         # copies version.dll into <game>\x86\
.\deploy.ps1 -Disable
```

The mod is a `version.dll` proxy next to `Warcraft II.exe`. It only activates in the exact supported build
(1.0.2.2818, PE timestamp 1771967463); after a game patch it logs "staying inert" and the game runs unmodified.

## Use

- `Ctrl+F9` toggles autocast in game (banner text confirms).
- `<game>\x86\autocast.ini` is created on first start. Edits apply within a few seconds, no restart.
- `<game>\x86\autocast.log` records load, hook status, config, and (with `log_casts = 1`) every cast.

Behaviour rules:

- Single-player only. In a network game the mod does nothing: its orders bypass the network command queue and
  would desync the match.
- A caster is only taken over while idle, guarding, patrolling or attacking. Move / follow / board orders are
  never interrupted. An invisible caster is left alone.
- Two casters never pick the same target for the same spell.
- Heal goes to the most hurt unit at or below `heal_below_pct`. Bloodlust / Haste / Unholy Armor go only to units that
  are fighting. Polymorph prefers enemy casters, then the biggest unit type at or above `polymorph_min_hp`.
- Mana costs and researched spells are read live from the game, so campaign restrictions and Remastered's
  rebalanced costs (Heal 5, Bloodlust 60) are respected.

Known limits: area spells (Blizzard, Death and Decay, Whirlwind, Runes) and Fireball / Flame Shield / Invisibility /
Raise Dead are not automated yet. Enemies inside `search_radius` are targeted even if fog hides them from you.

## Verify without the game

```powershell
.\build\Release\selftest.exe "C:\Program Files (x86)\Warcraft II Remastered\x86\Warcraft II.exe"
.\test\proxy_load_test.ps1
```

`selftest` maps the real exe as an image, checks the hook site bytes, and runs the targeting logic over a fake world.
