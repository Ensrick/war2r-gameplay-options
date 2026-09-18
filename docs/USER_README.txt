WARCRAFT II: REMASTERED - AUTOCAST
==================================

Your casters cast their spells on their own, the way the computer's do. Single-player only.


INSTALL
-------
1. Close the game.
2. Extract this zip into your "Warcraft II Remastered" folder (the one that contains the "x86" folder).
   Default: C:\Program Files (x86)\Warcraft II Remastered
   You should end up with:
       Warcraft II Remastered\x86\version.dll
       Warcraft II Remastered\x86\autocast.toml
       Warcraft II Remastered\x86\autocast_readme.txt
3. Start the game from Battle.net as usual.

In a single-player game press Ctrl+F9. A banner "Autocast OFF" / "Autocast ON" proves the mod is loaded.


UNINSTALL
---------
Delete version.dll (and the autocast.* files) from the x86 folder. Nothing else is touched.


WHAT IT DOES
------------
Paladin        Heal (units missing 10+ HP, most hurt first), Exorcism (enemy undead)
Mage           Polymorph (targets you list in the config), Slow
Ogre-Mage      Bloodlust (only on units that are fighting)
Death Knight   Raise Dead (when enemies are near), Death Coil, Haste (your flyers only), Unholy Armor (off by default)

- A caster is only taken over while it is idle, guarding, patrolling or attacking. Your move orders are never
  interrupted, and an invisible caster is left alone.
- Two casters never pick the same target for the same spell.
- Spells must be researched, and mana costs are whatever the game says they are.
- Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind and Runes stay manual on purpose.


SETTINGS
--------
Open x86\autocast.toml in Notepad. Every setting has a comment above it. Save the file while the game is running
and the change applies within a few seconds (a banner confirms it). If you make a typo the mod keeps your previous
settings and tells you the line number in x86\autocast.log.

Lost your settings file? Delete it; a fresh one with the defaults is written the next time you start a game.


GOOD TO KNOW
------------
- Multiplayer: the mod switches itself off. Its orders are local and would desync a network game.
- Game updates: the mod only activates on the exact game build it was made for (1.0.2.2818). After a Blizzard
  patch it does nothing (the game runs normally) until the mod is updated. autocast.log says "staying inert".
- Battle.net "Scan and Repair" may remove version.dll. Just extract the zip again.
- Trouble? Look at x86\autocast.log first, and include it when you report a problem.
