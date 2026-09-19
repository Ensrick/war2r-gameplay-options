WARCRAFT II: REMASTERED - GAMEPLAY OPTIONS
==========================================

Your casters cast their spells on their own, the way the computer's do. Idle workers go back to work. Optional
settings let you rebalance health, prices, build times and any single unit or building. One text file controls it all.
Single-player only.


INSTALL
-------
1. Close the game.
2. Extract this zip into your "Warcraft II Remastered" folder (the one that contains the "x86" folder).
   Default: C:\Program Files (x86)\Warcraft II Remastered
   You should end up with:
       Warcraft II Remastered\x86\version.dll
       Warcraft II Remastered\x86\gameplay_options.toml
       Warcraft II Remastered\x86\gameplay_options_readme.txt
       Warcraft II Remastered\x86\gameplay_options_tutorial.txt
3. Start the game from Battle.net as usual.

In a single-player game press Ctrl+F9. A banner "Autocast OFF" / "Autocast ON" proves the mod is loaded.


UNINSTALL
---------
Delete version.dll (and the gameplay_options.* files) from the x86 folder. No game file is modified by this mod.


WHAT IT DOES
------------
OUT OF THE BOX only this is switched on:
  - Autocast for Heal, Slow, Bloodlust and Raise Dead.
  - Idle workers repair damaged buildings nearby.
Everything else below is there for you to turn on in gameplay_options.toml. Nothing changes the game's balance until
you say so.

AUTOCAST (Ctrl+F9 toggles it; [on] = on by default)
  Paladin        Heal [on] (units missing 10+ HP, most hurt first), Exorcism (enemy undead)
  Mage           Slow [on], Polymorph (targets and priority come from your list: dragons, gryphons, daemons first)
  Ogre-Mage      Bloodlust [on] (only on units that are fighting), Eye of Kilrogg (idle ogre-mage at full mana)
  Death Knight   Raise Dead [on] (when enemies are near), Death Coil, Haste (your flyers only), Unholy Armor

  - A caster is only taken over while it is idle, guarding, patrolling or attacking. Your move orders are never
    interrupted, and an invisible caster is left alone.
  - Two casters never pick the same target for the same spell.
  - Spells must be researched, and mana costs are whatever the game says they are.
  - Eye of Kilrogg auto-scout: the eye flies to ground you have not explored. Move an eye yourself and the mod leaves
    that eye to you.
  - Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind and Runes stay manual on purpose.

WORKERS
  - Auto-repair [on]: an idle peasant / peon repairs a damaged building of yours within 10 tiles after 1 second.
  - Auto-harvest: still idle after 10 seconds, it walks to the nearest gold mine or tree within 5 tiles. A worker
    carrying gold or lumber delivers it first.
  - "Idle" means stopped with nothing queued. Stand Ground is respected, so that is how you park a worker.

OPTIONS (all off or neutral until you change them)
  - Hero regeneration (for example 1 HP per second).
  - Unlimited gold mines, and separately unlimited oil platforms.
  - Multipliers for HEALTH, PRICES and BUILD / RESEARCH TIME, all 1.0 (unchanged) by default. Each has master
    values (all, units, structures, research), the same per race (human / orc) and values per group (workers, melee,
    ranged, siege, casters, air, naval, demolition, heroes; buildings, building upgrades; melee / ranged / siege /
    naval research, paladin or ogre-mage research, mage or death knight spells). They multiply into each other.
    "all" is everything (units, ships, structures, research), "units" and "structures" are the masters per kind.
    So [health] all, [costs] all and [time] all are the three one-line "whole game" settings.
  - The config file is split into numbered, labeled parts (GENERAL, AUTOCAST, WORKERS, GOLD MINES AND OIL, HEROES, HEALTH,
    PRICES, BUILD AND RESEARCH TIME, RANGE UPGRADE, YOUR OWN NUMBERS) with UNITS / STRUCTURES / RESEARCH sub-headers.
  - Your own base stats for any unit in a [unit.<name>] table, and for any structure in a [building.<name>] table:
    hit points, armor, basic and piercing damage, range, sight, gold, lumber, oil, build time. The file contains
    ready-made examples as comments (sight 8 for dragons and gryphon riders, a tweaked destroyer, a stronger guard
    tower): remove the "# " in front of the lines to switch one on.
  - How much range Longbow / Lighter Axes add (the game's own bonus is 1).
  Engine limits: 65535 hit points (32767 for structures), 2550 for a unit or structure price, 65535 for research,
  255 for times, sight 9.

  Health, prices, times and unit stats are applied when a NEW map starts (new mission, custom game, restart). A
  savegame keeps the numbers it was made with, so saves from before you installed the mod stay as they were. The
  computer plays by the same numbers you do.


SETTINGS
--------
Open x86\gameplay_options.toml in Notepad. Every setting has a comment above it, and gameplay_options_tutorial.txt has
copy-paste recipes. Save the file while the game is running
and the change applies within a few seconds (a banner confirms it). If you make a typo the mod keeps your previous
settings and tells you the line number in x86\gameplay_options.log.

Lost your settings file? Delete it; a fresh one with the defaults is written the next time you start a game.


GOOD TO KNOW
------------
- Multiplayer: the mod switches itself off. What it does is local and would desync a network game.
- Game updates: the mod only activates on the exact game build it was made for (1.0.2.2818). After a Blizzard
  patch it does nothing (the game runs normally) until the mod is updated. gameplay_options.log says "staying inert".
- Battle.net "Scan and Repair" may remove version.dll. Just extract the zip again.
- Nothing happens at all, no Ctrl+F9 banner, and there is NO x86\gameplay_options.log after you started the game?
  Then the game you started is not the copy the mod is in. Check your shortcut: it must start "Warcraft II.exe" from
  the same x86 folder that holds version.dll (a second or older install elsewhere on the disk is the usual reason).
  Starting from the Battle.net app always runs the right one.
- Trouble? Look at x86\gameplay_options.log first, and include it (and your gameplay_options.toml) when you report
  a problem: https://github.com/Ensrick/war2r-gameplay-options/issues


LICENSE
-------
Copyright (c) 2026 Ensrick. Open source under the MIT license: you may use, change and share it as long as this
copyright notice and the license text stay with it. Full text and source code:
https://github.com/Ensrick/war2r-gameplay-options
The TOML reader toml++ by Mark Gillard is used under the MIT license as well.
