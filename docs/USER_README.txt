WARCRAFT II: REMASTERED - AUTOCAST AND TWEAKS
=============================================

Your casters cast their spells on their own, the way the computer's do. Idle workers go back to work. Optional
tweaks slow combat down and make the economy kinder. Everything can be switched off or tuned in one text file.
Single-player only.


INSTALL
-------
1. Close the game.
2. Extract this zip into your "Warcraft II Remastered" folder (the one that contains the "x86" folder).
   Default: C:\Program Files (x86)\Warcraft II Remastered
   You should end up with:
       Warcraft II Remastered\x86\version.dll
       Warcraft II Remastered\x86\autocast.toml
       Warcraft II Remastered\x86\autocast_readme.txt
       Warcraft II Remastered\x86\autocast_config_tutorial.txt
3. Start the game from Battle.net as usual.

In a single-player game press Ctrl+F9. A banner "Autocast OFF" / "Autocast ON" proves the mod is loaded.


UNINSTALL
---------
Delete version.dll (and the autocast.* files) from the x86 folder. No game file is modified by this mod.


WHAT IT DOES
------------
AUTOCAST (Ctrl+F9 toggles it)
  Paladin        Heal (units missing 10+ HP, most hurt first), Exorcism (enemy undead)
  Mage           Polymorph (targets and priority come from your list: dragons, gryphons, daemons first), Slow
  Ogre-Mage      Bloodlust (only on units that are fighting), Eye of Kilrogg (idle ogre-mage at full mana)
  Death Knight   Raise Dead (when enemies are near), Death Coil, Haste (your flyers only), Unholy Armor (off)

  - A caster is only taken over while it is idle, guarding, patrolling or attacking. Your move orders are never
    interrupted, and an invisible caster is left alone.
  - Two casters never pick the same target for the same spell.
  - Spells must be researched, and mana costs are whatever the game says they are.
  - The Eye of Kilrogg scouts on its own: it flies to ground you have not explored. Move an eye yourself and the
    mod leaves that eye to you.
  - Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind and Runes stay manual on purpose.

WORKERS
  - An idle peasant / peon repairs a damaged building of yours within 10 tiles after 1 second.
  - Still idle after 10 seconds, it walks to the nearest gold mine or tree within 5 tiles. A worker carrying gold or
    lumber delivers it first.
  - "Idle" means stopped with nothing queued. Stand Ground is respected, so that is how you park a worker.

TWEAKS (each has its own switch or number)
  - Heroes regenerate 1 HP per second.
  - Dragons and gryphon riders see 2 tiles further.
  - Unlimited gold mines: OFF by default.
  - Health multipliers for units and for heroes: 1.0 (unchanged) by default. Try units = 2.0, heroes = 4.0 for slower,
    weightier fights.
  - Price multipliers, all 1.0 by default (0.5 = half price): units, buildings, building upgrades (keep, castle,
    guard / cannon tower), and research by group: melee (swords, axes, shields), elf / troll, siege, paladin /
    ogre-mage, naval, mage / death knight spells.
  Health multipliers only ever touch units, never structures. The engine's limits apply: 65535 hit points, 2550 for a
  unit or structure price, 65535 for research.

  Health, prices and sight are applied when a NEW map starts (new mission, custom game, restart). A savegame keeps the
  numbers it was made with, so saves from before you installed the mod stay as they were. The computer plays by the
  same numbers you do.


SETTINGS
--------
Open x86\autocast.toml in Notepad. Every setting has a comment above it, and autocast_config_tutorial.txt has
copy-paste recipes. Save the file while the game is running
and the change applies within a few seconds (a banner confirms it). If you make a typo the mod keeps your previous
settings and tells you the line number in x86\autocast.log.

Want the mod to do nothing but autocast? Delete the lines under [vision], set regen_hp_per_second = 0, and switch
the [workers] options to false. The health and price multipliers are already 1.0 out of the box.

Lost your settings file? Delete it; a fresh one with the defaults is written the next time you start a game.


GOOD TO KNOW
------------
- Multiplayer: the mod switches itself off. What it does is local and would desync a network game.
- Game updates: the mod only activates on the exact game build it was made for (1.0.2.2818). After a Blizzard
  patch it does nothing (the game runs normally) until the mod is updated. autocast.log says "staying inert".
- Battle.net "Scan and Repair" may remove version.dll. Just extract the zip again.
- Trouble? Look at x86\autocast.log first, and include it when you report a problem.
