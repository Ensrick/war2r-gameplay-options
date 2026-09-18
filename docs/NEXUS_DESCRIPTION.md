# Nexus page draft

Status: DRAFT for 1.0.0. Mod name below is a placeholder until the author names it.
Paste the BBCode block into the Nexus description editor (BBCode mode).

Suggested summary line (max 350 chars):

> Your casters cast on their own like the computer's do: Heal, Exorcism, Slow, Polymorph, Bloodlust, Death Coil, Haste,
> Raise Dead, Eye of Kilrogg. Plus idle workers that go back to work, optional endless gold mines, tougher units and
> heroes, cheaper units and upgrades. One drag-and-drop DLL, everything switchable in one config file. Single-player.

```bbcode
[size=5][b]PLACEHOLDER NAME: Autocast and Tweaks for Warcraft II: Remastered[/b][/size]

The computer in Warcraft II has near perfect spell micro. Now your casters do too. Pick which spells run on their own, decide who is a valid Polymorph target, and leave the clicking to the mod. Single-player only.

[size=4][b]Autocast[/b][/size]
[list]
[*][b]Paladin[/b]: Heal (only units missing 10+ HP, most hurt first), Exorcism on enemy undead
[*][b]Mage[/b]: Polymorph (you choose the valid targets and their priority: dragons, gryphons and daemons first by default), Slow
[*][b]Ogre-Mage[/b]: Bloodlust on units that are actually fighting, Eye of Kilrogg when idle at full mana
[*][b]Death Knight[/b]: Raise Dead when enemies are near, Death Coil, Haste on your flyers, Unholy Armor (off by default)
[/list]
[list]
[*]Your move orders are never interrupted. Casters are only taken over while idle, guarding, patrolling or attacking.
[*]Two casters never waste mana on the same target.
[*]The Eye of Kilrogg scouts by itself: it flies to ground you have not explored yet. Move an eye yourself and the mod leaves that eye to you.
[*]Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind and Runes stay manual on purpose.
[*]Ctrl+F9 toggles autocast in game.
[/list]

[size=4][b]Workers[/b][/size]
[list]
[*]An idle peasant or peon repairs damaged buildings nearby (after 1 second, within 10 tiles).
[*]Still idle after 10 seconds? It walks to the nearest gold mine or tree within 5 tiles. A worker carrying gold or lumber delivers it first.
[*]Stand Ground is respected, so you can still park a worker.
[/list]

[size=4][b]Optional tweaks[/b][/size]
[list]
[*]Unlimited gold mines (OFF by default). A computer opponent that runs out of gold can stall a long game; starving a mine is also a real campaign tactic, so this one is your call.
[*]Heroes regenerate 1 HP per second.
[*]Health multipliers for units and for heroes (default 1.0 = unchanged). Set units to 2.0 and heroes to 4.0 for slower, weightier combat.
[*]Dragons and gryphon riders see 2 tiles further.
[*]Price multipliers for units, for troll and elf research and for catapult / ballista upgrades (default 1.0 = unchanged, 0.5 = half price). Multipliers only touch units, never structures.
[/list]
Every item above has its own switch and numbers in the config file.

[size=4][b]Install[/b][/size]
[list=1]
[*]Close the game.
[*]Extract the zip into your [b]Warcraft II Remastered[/b] folder (the one that contains the [b]x86[/b] folder). The files land in x86.
[*]Start the game from Battle.net as usual. In a single-player game press Ctrl+F9: a banner confirms the mod is loaded.
[/list]
Uninstall: delete version.dll and the autocast files from the x86 folder.

[size=4][b]Settings[/b][/size]
Everything lives in [b]x86\autocast.toml[/b], a plain text file with a comment above every setting. Save it while the game is running and the change applies within seconds. A typo cannot break anything: the mod keeps your previous settings and tells you the line number in x86\autocast.log. A full tutorial with copy-paste recipes is included in the download and on the GitHub page.

[size=4][b]Good to know[/b][/size]
[list]
[*][b]Single-player only.[/b] The mod switches itself off in multiplayer games: what it does is local and would desync a network game.
[*]Made for game build 1.0.2.2818. After a Blizzard patch the mod goes inert (the game runs normally) until it is updated.
[*]Battle.net "Scan and Repair" may remove version.dll. Just extract the zip again.
[*]No game files are modified. It is one DLL that the game loads next to its exe, plus its config and log.
[/list]

[size=4][b]Source[/b][/size]
Open source, with the reverse-engineering notes for anyone who wants to build on it: GITHUB LINK HERE

[size=4][b]Credits[/b][/size]
The war2.ru community (Mistral and others) for two decades of Warcraft II reverse engineering, and toml++ by Mark Gillard for the config parser.
```

## Notes for the author before publishing

- Replace the placeholder name and the GitHub link (the repo is private today).
- The tweak list describes the 1.0.0 target. Remove any item that does not make the release.
- Add the Buy Me a Coffee block (memory `reference_bmc_button.md`) if wanted on this page.
- Nexus category: there is no Warcraft II Remastered game page check done yet; confirm the game exists on Nexus
  (nexusmods.com/warcraft2 hosts DAIFE, which targets War2Combat) and whether Remastered mods belong there.
