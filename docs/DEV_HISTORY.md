# Development history (pre-release builds)

Builds made before the first public release. The public changelog (CHANGELOG.md) starts at 1.0.0.
Every change gets its own build number; newest first.

## Community set-up (2026-09-19, no version: nothing in the mod changed)

- Author: "could you draft a BBCode instruction on how to report bugs and a personal welcome, they'll need links to
  github and gitlab and we'll need to set things up so they can attach the files you need", plus "people might request
  features too".
- `nexus/NEXUS_COMMENT_WELCOME.txt` for the comments tab, GitLab mirror `gitlab.com/ensrick7/war2r-gameplay-options`
  (public, issues on, both tags pushed), issue templates on both sites for bugs and features. Nexus comments cannot
  carry files, which is why the post sends people to a tracker and says exactly which three files to attach.
- GitLab quirk: the anonymous issues page 404s while the API reports issues public and both templates registered; the
  "new issue" link redirects to sign-in, so an account is needed there as on GitHub. The post covers people with
  neither.

## 1.14.1 (2026-09-20, NOT released)

- The agent amended its plenty_units commit after I had merged it (our messages crossed); the delta was one tutorial
  paragraph. The tutorial ships in the zip, so it gets its own patch version like any other change.

## 1.14.0 (2026-09-20, UNTESTED in game, NOT released)

- Author: "If I can simultaneously afford to produce 10 of any unit then resource ratios don't matter, does that make
  sense? And we can make that number configurable." That is the saturation point of the money factor in Targets,
  hard-coded 5 until now. The agent proved his sentence as a test: two banks with nothing in common, both above the
  line for every class, give identical targets equal to the plain weights.
- Agent's observation worth keeping: each group is renormalised over its own classes, so a plenty_units that EVERY
  class of a group is under cancels out; it only moves the mix where some classes are over the line and others not.
- filler_min is a different job (what to build when nothing of the mix is affordable); the tutorial says so.
- Default 10 for new configs, his own installed config gets 10 (he asked for it); the migrator writes 5 into other
  existing auto-production configs, because a migration never changes behaviour.

## 1.13.0 (2026-09-20, UNTESTED in game, NOT released)

- Author, in game: "I did see a death and decay use on 3 enemy battleships, but I've never seen it on land, and I've
  never seen it on a building." Log: five casts in two hours, and the one land cast (18:27:20) was stopped 0.9 s later
  by the watchdog, "a friendly unit or building is in the area". The no-friendly-fire rule is his own; the fault was
  the AIM: only the target's own tile was ever tried, with 4 tiles of clearance around it.
- The agent read the scatter from the code (death and decay +/- 2 tiles, blizzard -3..+2 because its pattern sits
  half a tile past the aim) and walks the aim over that span when the straight aim fails on the friendly test only.
  Budget 64 aim tiles per caster per pass. Aim tiles out of the spell's range from where the caster stands are
  refused (a clearance check made before a walk is stale on arrival); what the engine does with an out-of-range spell
  order stays [unverified]. 16 mutations over both autocast commits, 16 caught.
- Trap recorded by the agent: after a header-only change MSBuild kept stale objects in a worktree under Temp (175
  phantom failures from a struct layout mismatch). Touch every .cpp before building there.

## 1.12.0 (2026-09-20, UNTESTED in game, NOT released)

- Author, in game: "I'm getting a lot of Troll Destroyers, likely because I have an abundance of gold or something."
  The log said otherwise: bank 590000 gold / 41000 lumber but only 2200 to 2900 oil (the mod builds one tanker), 21
  destroyers against 2 battleships started, at tier 3 where the mix wants 25 / 60. Gold cannot be it: the money factor
  in Targets saturates at a few units' worth. The cause is PickArmyClass skipping every class that fails CanAfford
  and taking the best AFFORDABLE deficit: a destroyer at 2920 oil (4 x 700 + the 120 reserve), so the 4120 a
  battleship needs is never reached. Issue #26.
- Fresh agent in its own worktree (the spell agent was busy): one pure production::Decide wrapping PickArmyClass,
  PickFiller and the new hold, per-building SaveUp state in the serial-keyed slots, release after save_up_seconds
  without growth of the blocked resource. It corrected two of my numbers (2920 not 2800; 21 is a cumulative count,
  the over-share rule already refuses with that many alive). 10 mutations, 10 caught.
- His "if I can afford 10 of any unit then resource ratios don't matter" becomes plenty_units in the next version.

## 1.11.0 (2026-09-20, UNTESTED in game, NOT released)

- Author, in game on 1.10.1: "We need priority orders for autocast ... One thing that is not working at all is death
  and decay". His log agreed: 82 death_coil, 145 raise_dead, 18 haste and 2 death_and_decay in two hours, both at
  buildings where Death Coil has no target. CasterThink had a fixed order with Death and Decay after Death Coil and
  Haste, and a channel needs three waves of mana (90 at his price) while Death Coil fires at 50.
- A list alone would not have fixed it (put D&D first and the knight still spends at 50 on the next spell down), so
  the agent built `save_mana` with a dry-run mode: every Try path can be asked "would you cast if mana were no
  object" without leaving a trace. The audit closed the side effects: orders, claims, the cast counter and log line,
  RememberChannel, the raise_dead diagnostic, holy vision's per-pass cache. 11 mutations, 11 caught.
- His installed config gets death_and_decay and blizzard first (my call, he asked for the priority and can reorder).
- Second blocker found in the same log, NOT in this version: the 4-tile friendly clearance kills nearly every land
  cast because his army stands next to what it attacks (#25). Aim search + area_friendly_clearance are in progress.

## 1.10.1 (2026-09-20, NOT released)

- Found while preparing the stall hunt: the log was truncated at every game start, so the evidence of a stalled game
  is gone the moment he starts the game again (the 2026-09-20 research only had a log because the game had not been
  restarted). Two generations are kept now. Only "Warcraft II.exe" opens the log (dllmain), so the helper exes that
  also load the proxy never rotate it.

## 1.10.0 (2026-09-20, UNTESTED in game, NOT released)

- Author: the computer "was attacking relentlessly, and then stopped at some point despite having all the resources
  needed", on two Beyond the Dark Portal orc missions in a row. He asked for research, and wants AI TWEAKS to be a
  separate mod; this build only observes.
- Research agent (static RE plus decoding his savegames): the computer is a 4-opcode script interpreter and WAITFOR
  never times out. Its first verdict, "every savegame load doubles the army counters", did not survive the check I
  asked for: in a mid-mission save that had been through a load, every land counter matched the live unit count
  exactly. The load path stays [unverified] in docs/research/ai_stall.md. No save of a stalled state exists.
- So the deliverable is the instrument: `[general] log_ai`. Read-only by construction (no write to game memory in
  src/aiwatch.cpp, the program counter is range-checked against the script blob before any read, the build-list
  pointer is never followed). My review addition: a SLEEP counting down is not a stall (the wait word is above the 1
  a failed WAITFOR leaves), so sleeping never feeds the stall clock and the line says "sleeping N steps, then ...".
- Issue #24 tracks the stall itself.

## 1.9.0 (2026-09-20, UNTESTED in game)

- Author: "Autocast for it should target enemy buildings AND try to target the greatest number ... only are cast when
  there is either at least 1 building or more than 2 units", no friendly units in the blast, no overflow on
  structures. He also put Blizzard and Death and Decay back to their old prices in his own config (25 / 30): at half
  price with double damage they were too cheap.
- Implemented by the agent in the auto-production worktree: ScanArea counts enemy units and buildings once each and sums
  the buildings' hit points; value = buildings x area_building_value + units; gate = one building or area_min_enemies
  units. The engine keeps no wave count, so the watchdog counts waves by the mana the caster has spent since the
  channel began, against about 5 x the LIVE damage byte per wave (docs/research/autocast_all_spells.md 2.6a).
- My review additions: aim at the centre tile of a building's footprint (the splash measures from the unit's centre,
  so the top-left tile wastes most of a wave on a 3x3 or 4x4 building), and the no-overkill stop must never touch a
  channel that was started for units.
- QA pass before the release found stale readme lines (area spells "stay manual", 6 workers per hall tier) and one
  log oddity filed as #23: a rescuable shipyard seems to count as a hostile navy until the player reaches it.

## 1.8.0 (2026-09-20, UNTESTED in game)

- Author: "If the number of enemy shipyards is 0 (regardless of whether we can see them or not), then no auto ship
  production at all aside from 1 oil tanker, 5 troll destroyers and 2 juggernauts and 2 submarines."
- Implemented by the agent: HostileNavy scans the unit array inside the existing pass (no fog / visibility check, he
  said so explicitly), counting hostile shipyards 0x48 / 0x49 including under construction, plus warships
  (destroyer 0x1E/0x1F, battleship 0x20/0x21, submarine 0x26/0x27). Enemy transports and tankers deliberately do not
  count. The warship half was my addition (missions hand out fleets with no shipyard); the agent argued the same way.
- Caps live in Plan::cap and are enforced in the mix, the filler, the tanker and the worker path; a capped class
  leaves the mix so its share goes to the land army. Nothing is ever cancelled. Note the implied edge: with
  destroyers and battleships capped out, submarines get 0 too, since they may never exceed a fifth of a fleet.

## 1.7.3 (2026-09-19)

- Author: "make the tier auto-production weight based, so it doesn't have to add up to 100%, is that how it works?"
  It already is (`Targets` divides each class weight by its group total), but the config called the numbers "percent".
  Wording fixed in the generator and the tutorial; no behaviour change.
- Fixed while looking: his log had the map-profile line twice per map. The fingerprint hashed every square-flag bit,
  and tree regrowth sets the unpassable bit, so the profile recounted and logged again. It hashes the water bit only.

## 1.7.2 (2026-09-19, UNTESTED in game)

- Author in game: "why isn't it auto-producing peasants?" then "Maybe it is working" (his log did show four peons a
  minute later once gold came in), and "I think we need 12 peasants at tier 1, 16 at tier 2 and 24 at tier 3".
- Real deadlock underneath: workers went through CanAfford, so the first extra peasant needed the reserve (keep 2000
  gold anchored it) + 4 x price = ~3800 gold, and peasants are what earn it. Workers and the tanker now need their own
  price only. Diagnostic line added because the report was pure guesswork otherwise.
- Reserve rule (agent's proposal, accepted): only a RESEARCH can anchor the reserve; keeps, castles and towers count
  at reserve_extra. Hall + barracks + blacksmith: 2200 -> 1300 gold, a 400-gold grunt gated at 2900 instead of 3800,
  close to the ~2500 he asked for. Rejected alternatives: a share-of-bank cap (worse early), "dearest only below N
  gold" (an absolute gold threshold does not scale with the cost multipliers).
- 27 mutation checks.

## 1.7.1 (2026-09-19, UNTESTED in game)

- Author: "some missions have restrictions on which units you can get, have you considered that?" Verified by the
  agent: the units-allowed mask `0x919210[player]` is read fresh every pass for every candidate (StartProduction
  checks it not at all), keep / castle upgrades use bits `0x8000000` / `0x10000000` of that same mask (FUN_004E36F0),
  the tower button tests no ALOW bit, and no instruction writes any of the six masks (they come from the PUD handler
  FUN_004D19D0), so a per-pass read covers mid-mission changes either way.
- Gap it found while checking: the reserve counted a research as purchasable when a building of the right type merely
  existed. It now needs one that is not already paying for a research or a building upgrade (job kind 1, 2, 3); a
  building that is training a unit still counts, deliberately.
- Tests: a campaign mask forbidding knights and battleships (their share renormalises over what is left), a mask with
  every barracks unit cleared (StartProduction never called), and the reserve dropping to 0 with the upgrade / spell
  masks cleared.

## 1.7.0 (2026-09-19, UNTESTED in game)

- Author: "I spend most of my time cranking out units ... I'd like to make it so that above a certain resource
  thresholds, troops start auto-producing", then three rounds of tuning (dynamic ratios instead of fixed numbers,
  ships from the map's water and oil, more grunts at tier 1, no casters at tier 2, one tanker, no zeppelins, small
  resource-gated submarines, food headroom for his own production, mission unit restrictions).
- Research docs/research/production.md (agent): every production start goes through StartProduction FUN_004ace10
  (kind 0 = unit), which the computer AI calls directly; it checks trained-at 0x838248, the requirement table
  0x8C0428 and the cost / food function, but NOT the map's units-allowed mask, not "already researched", and it
  ignores units in training in the food test. No rally points exist in this build.
- Implemented by the same agent in a worktree (src/production.cpp, pure decision core + engine pass): map profile
  (water %, oil sources) -> navy share; per-tier land / navy mix renormalised over what is trainable; upgrade reserve
  from every purchasable research and building upgrade; bank_multiple x the live price; filler rule for a lopsided
  bank (never across the land / ship line); food checked AFTER the unit; the player's selected building skipped; 10 s
  back-off when the game refuses. 15 mutation checks.

## 1.6.2 (2026-09-19)

- Author: "I also don't think eye of krillog is bieng cast at max mana." Log of his session: 12 eye casts, one at
  14:00:42 and the next at 14:27:00, i.e. no new eye while one was out. Cause: `max_active = 1` counts all his
  ogre-magi together, and Bloodlust autocast keeps their mana below 255. Not a bug; he chose "Eye max active lets say 3
  by default". Default 3; his own config set to 3 as well (migration keeps existing values, so it is set by hand).

## 1.6.1 (2026-09-19) - Raise Dead fix, UNTESTED in game

- Author: "Raise dead auto doesn't work. It has to target a grid square with a decaying body". Log of his 1.6.0
  session (log_casts on, orcs): Death Coil 338, Bloodlust 573, Eye 12, Raise Dead 0.
- Evidence (agent, the two key instructions re-checked here): a dying unit's slot is retyped to 0x69 at `0x4BE0A5`
  (`mov byte [ecx], 0x69`, the only such write) when its death animation ends; death unfiles it from the grid
  (FUN_004b5000) and nothing files a corpse again, so corpses are in NO grid. The computer's search FUN_004cb3e0 reads
  only the ground grid: its Raise Dead cannot fire either. The spell FUN_004e2420 itself walks the unit array. Every
  new map grants only fireball + death coil (`0x4D2B9E mov [eax+0x919290], 0x4020`), so Raise Dead needs research.
- Fix: TryRaiseDead walks the unit array (type 0x69, state 2, 15-tile box, not on water, not inside a raise under
  way), nearest wins, tile claim kept. Diagnostic line per death knight per 30 s of play with log_casts. The 1.5.0
  tests had filed corpses in the grid, which is how the bug passed them; the tests now place corpses the engine's way.
  Five mutation checks caught, grid-only search = 13 failures.
- Class note: the tree research had already said "corpses are in neither grid"; the Raise Dead design followed the
  computer AI's code instead of checking. Any "where does the engine keep X" assumption gets a code check now.

## 1.6.0 (2026-09-19, UNTESTED in game)

- Author: heroes "NEED to be fairly strong"; asked whether max mana can go above 255, and for "a multiplier for all
  spell damage, and then individual tweaks ... individual spell cost and a spell cost mult", rounding costs up, and
  "make sure we're not opening the doors to more bugs like the outside the map stuff".
- Research docs/research/spells.md (agent; costs re-checked here against the exe): mana is a byte, capped by the regen
  code at 255 and treated as full by every UI bar, so the maximum cannot rise; regen is +1 per 40 steps (reload byte
  0x4EF589). The cost table is u16 per order, never saved or reloaded, valid 1..255 (0 divides by zero in heal /
  exorcism). Every damage number is an instruction immediate; heal and exorcism are priced per HP.
- Implemented by the same agent in a worktree (src/spells.cpp), merged after 1.5.0: cost table from verified base
  values; byte-verified group patches (fireball incl. a 16-byte marker rewrite, death coil 5 sites, runes 2 sites, heal
  cap, regen 3 sites), each group all-or-nothing and refused on unknown bytes; synced at the new-map hook and every tick
  before the multiplayer return, game values in multiplayer and at defaults. No patch touches a coordinate or an index.

## 1.5.0 (2026-09-19, UNTESTED in game)

- Author: "include options for autocast for all spells" and, on Raise Dead not firing, "Yes" to matching the computer
  AI. Research docs/research/autocast_all_spells.md (agent, re-checked: the eight mana costs match the exe): every
  area spell splashes through FUN_004af9e0 -> FUN_004afb50 with NO owner test (flyers and buildings included), runes
  have no owner at all, blizzard / death and decay channel until mana runs out, holy vision may move the local
  player's camera (0x91AD80 / 84, inferred).
- Implemented by the same agent (src/autocast.cpp): per-spell safe rules, friendly = own / allied units on both grids,
  building footprints and walls; positional tiles always computed on the map; a watchdog stops only the channels the
  mod started (stop handler 0x4D8580 at the caster's tile); module-owned claims only. Seven mutation checks caught.
- Raise Dead: FUN_004cac80 -> FUN_004cb3e0 (31 x 31 box, ground grid) with filter FUN_004ca8d0 (type 0x69, state 2,
  not hidden). No enemy requirement any more; nearest corpse wins (the computer takes the first in scan order).
- The selftest now uses a temp folder per process: two checkouts testing at once rewrote each other's config file
  (the 64-tick reload switched auto-harvest off mid-test) and failed at random.

## 1.4.1 (2026-09-19) - crash fix

- Author, first session with 1.4.0: "The game crashed". Blizzard crash report `x86\Errors\2026-09-19 03.03.05 ...\`
  (`Crash.txt` + `War2_Remastered.dmp`), 33 s after a Restart Scenario (log: new map 03:02:32, crash 03:03:05).
- Evidence: ACCESS_VIOLATION reading 0x0150FEE4 at `0x4D80BF` (image base 0x00ED0000 that session). Stack: `0x4D80BF` in
  FUN_004d8090 <- `0x4D8607` harvest handler FUN_004d85e0 <- `0x4EF2D7` IssueOrder <- VERSION.dll (the mod). Exception
  context from the minidump: `eax` = unit grid, `ecx` = -75, `edx` = unit 0x0167A868: type 3 (peon), owner 5 (the
  local player), at 54,0, order tile 53,-1, target NULL. Stack arguments of the IssueOrder call: 53, -1, NULL, harvest.
  FUN_004d8090 reads `unitGrid[orderY * mapSize + orderX]` with no bounds check (`0x4D80A0..0x4D80BF`).
- Cause: `workers.cpp` `RegionAt` returned the FOREST value for every tile outside the map (meant as "blocked"), and
  `TreeReachable` asked it whether the tile itself is forest. A worker idling on any map edge with auto_harvest on and
  nothing closer found an off-map "tree" in its first search ring (the ring starts at y - 1). Present since the
  auto-harvest feature (dev builds); on Nexus since 1.0.0, but auto_harvest ships off. Tree regrowth was not involved.
- Fix: off-map tiles are nothing. Hardening for the class: `game::IssueOrder` refuses every positional order (no
  target) outside the map and logs it (first five), whatever feature asks; the eye module already clamped its own.
  Tests: a worker on each edge and corner gets no order and nothing reaches the guard; a tree on the edge row is still
  found; the guard refuses four off-map orders and passes an on-map and a targeted one. Mutation check: the old
  off-map rule restored fails three checks.
- Side fix: the tree regrowth count logged after a restart (03:00:26 "8 tiles grew back" 36 s into a new map) was
  carried over from the previous game; it is now logged as "(previous map)" when a new map attaches.
- Not claimed fixed until the author has played an edge-of-map idle worker session.
- Released on the author's "update the nexus, github, and everything else": Nexus file id 15 (MAIN, 1.4.0 archived,
  changelog posted) and the first GitHub release, `v1.4.1` with the zip.

## 1.4.0 (2026-09-19, UNTESTED in game)

- Uploaded to Nexus Mods 2026-09-19 02:09 on the author's instruction ("Make sure you update the nexus files and
  changelog when done. You should have access to the API key"): file id 14 "War2R Gameplay Options 1.4.0" is MAIN, his
  own 1.0.0 upload (file id 13) archived, changelog entries posted for 1.0.1 to 1.4.0 (1.0.0 was his). The description
  text cannot be set through the API.

- Author: "units that are injured are often just a waste of food, what if we add 1 second regen to all units not just
  heroes? And then keep the section we have for heroes separate? I'm thinking 1hp a second by default, and change the
  default for heroes to 2 per second." and "Ships included, not structures."
- `[unit_regen] enabled / hp_per_second / regen_for`; `[heroes] regen` switch added, amount default 2. Both switches
  ship OFF (the author's standing rule since 1.0.2 that nothing but four autocasts and auto-repair is on out of the
  box); 1 and 2 are the amounts a player gets when switching them on. Legacy rule: a file without the `regen` key and
  with `regen_hp_per_second` above 0 still means on, so 1.0.0 configs keep their hero regeneration.
- One pass in `tweaks.cpp` (`Regenerate`): hero rule first (list, switch, regen_for), everything else that has no
  building flag follows the unit rule; never both. Built on a separate branch in a worktree while the tree agent
  owned the main working tree, merged after 1.3.0.

## 1.3.0 (2026-09-19, UNTESTED in game)

- Author: "the multiplier takes care of gold and oil, but not lumber. What if trees could grow back? ... they have to
  not be able to grow back within 3 squares of a building", then "units also block regrowth, nothing should regrow
  within 3 of a unit either", "Ground unit specifically", and "maybe we make the tree regrowth a bit more random, like it
  can take between a certain number of minutes".
- Research and implementation by a research agent, reviewed and re-run here (docs/research/tree_regrowth.md). Felling
  (`FUN_004eb400`) writes three maps: tile id, square flag 0x80, region word. The neighbour fix-up is a per-tileset
  removal table, a 4-corner automaton that only ever removes; the engine has no code that places a tree. A felled tile
  is always tile id 0x7E and nothing else ever is, which is how the mod knows where a forest stood. All three maps are
  stored verbatim in savegames.
- `src/trees.cpp`: "corner painting", the inverse of felling, with raw writes from the tick (no game function called):
  tile id (only ever a forest state 1..24), then `sq |= 0x80`, then region `0xFFFE`. Preconditions: plain land, no unit
  or building in the first grid, land region, the ring test (open neighbours form one unbroken run, so nothing is cut
  off and the merge-only region ids stay true), building / wall box, ground-unit box, no corpse. The loaded table must
  byte-match the known rows 0..25, tree base 0x66, stride 10, map at most 128: otherwise the feature is off for that
  map with one log line.
- Per stump: first-seen play time + one random byte; wait = min + (max - min) x byte / 255, evaluated at check time.
  One full map pass per 4 s of play, at most 8 due stumps per pass, a blocked stump is retried every 8th pass.
- Tests: exact timing, the three writes, distances, flyers ignored, passage rule, pockets, resets (new map, load into
  the same buffers), six sanity failures, a seeded stress run over 20 random forests (3349 tiles), relocation-aware
  instruction pins, and 15 deliberate breakages of the module that each fail a test.
- Labelled EXPERIMENTAL for players: edge art, minimap, harvesting a regrown tree and re-pathing are in-game questions.

## 1.2.0 (2026-09-19, NOT uploaded, UNTESTED in game)

- Author: halls providing food "is a way to speed up custom matches. You typically start with a peasant and have to
  build a town hall, but can't do any unit production until after that ... by default maybe have it toggled off".
- Research (docs/research/food_supply.md, re-checked by hand against the exe): supply is `u16[16]` at `0x91B50C`, a
  running counter with exactly two writers, the farm callback (`0x4B50EA lea eax,[edx*4]`, +4) and the hall callback
  shared by all six hall types (`0x4B5182`, +1), dispatched through the per-type table `0x8C0EF0`. No data table, not
  in unitdata.dat. Readers clamp to 200 at read time (train check `0x4AC66F..0x4AC697`); "used" is never stored; the
  game re-counts everything after a save load. A fresh hall gives 1 and the peasant uses 1, which is why nothing can be
  trained before the first farm.
- Built as the recommended stateless recompute in the tick: `supply = 4 x farms + N x (halls + keeps + castles)` from the
  game's own counters `0x91B48C / 52C / 54C / 56C`. No patch, no new hook. N = 1 is the game's own value, so switching
  off restores it, and nothing at all is written until the option has been on once. A wrapped (underflowed) counter
  makes the mod skip that player. Rejected: in-place patch of the hall callback (the save-load re-count runs before any
  tick, and +N / -N with a hot-reloadable N corrupts the word), callback table swap (needs the same reconciliation).
- One key for all three tiers on purpose: fewer knobs. Per-tier amounts fit the same formula if ever wanted.

## 1.1.0 (2026-09-19, NOT uploaded, UNTESTED in game)

- Author: unlimited mines "isn't always desirable, but I do need more gold so that starving the AI out isn't so easy",
  and asked whether a trip could remove less than it pays. Advice given and accepted: multiply the mine's starting
  amount instead. The "left" word is u16 in hundreds (max 6,553,500) while the PUD loader can only produce 6375
  (`b * 0x19` from one byte), so there is a x10 headroom, no rounding (whole trips), the panel shows the truth and the
  AI reads the same field.
- When: the new-map hook runs before the map's units exist, so it only arms a flag (after the multiplayer gate); the
  first tick of that map scales mines (0x5C), oil patches (0x5D) and platforms (flag 0x800), before the "unlimited"
  refill so the peak it remembers is the scaled amount. Never for a loaded game: the hook does not fire for a save, and
  the game's own "came from a savegame" word (`0x91BFB0`, set at `0x4C4295`) is checked too for the case "new map, then
  a save loaded before the first tick". The flag is spent either way.
- Decided against for now (author): bounty, raiding, passive hall income ("that awards building tons of halls").
  In research: halls providing food (custom games start with one peasant), tree regrowth with a 3-tile building
  exclusion. See docs/research/food_supply.md and tree_regrowth.md when they land.
- First minor version under the semantic versioning rule (new setting).

## 1.0.10 (2026-09-18, NOT uploaded)

- Author, after seeing the restrictive license: "Ah well, just make it open source, use the MIT license." LICENSE is
  the standard MIT text, Copyright (c) 2026 Ensrick (credit is kept by the notice requirement). Readme, shipped readme
  and Nexus description say open source again. The permissions set on the Nexus page itself are website-only and are
  the author's to change (they still said "no upload / no modification" at this point).

## 1.0.9 (2026-09-18, NOT uploaded)

- Author: "Make it public so people can report issues." Pre-publication scan of the tracked files and the whole
  history: no keys or tokens, no binaries or game files, no e-mail or user name. Local-PC details in CLAUDE.md, this
  file and one issue comment were reworded first.
- License: he first asked for GPL, then pasted the Nexus permissions ("make it match the nexus license conditions",
  then "whichever license most makes sense for that, maybe not to include one is best"). GPL grants exactly what those
  permissions forbid (re-upload, modification, reuse), so it was dropped. No LICENSE file at all would also mean "all
  rights reserved", but says nothing about what IS allowed (building it, issues, pull requests) or about credit, so
  the repository carries a short plain-language LICENSE mirroring the Nexus conditions. toml++ stays MIT.
- Author: "From now on we increment the changelog properly since it's now public." CHANGELOG.md: semantic versioning
  rule, clean dated headings, one line saying which version is on Nexus.

## 1.0.8 (release build, staged 2026-09-18, NOT uploaded, UNTESTED in game)

- Author: "We could [use] a multiplier that covers the health of all buildings, units, and ships. Basically everything.
  Same for build time and cost." `[costs] all` and `[time] all` already were that. `[health] all` was units-only (his
  own rule from the first multiplier request), which is what made the three sections read differently.
- Decision: make `[health]` identical to the other two. `all` = everything, new `units` = units-only master (top level
  and per race). `Multipliers::StructureHealth` is gone; health uses `Unit()` / `Structure()` like prices and times.
  Neutral structures (gold mine, dark portal, runestone) are still never scaled.
- This CHANGES the meaning of an existing key. `tools/migrate_config.py` (was `add_oil_section.py`) moves an old
  `[health] all` value into `units` so the game behaves as before, verified by a TOML parse (the product all x units
  per table must be unchanged, every other value identical). The author's installed config is migrated with it.
- Also asked: "is there a more effective way to organize this rather than having so many overlapping multipliers?"
  Proposed, NOT built: one flat table per section (all, human / orc / neutral, units / structures / research, then the
  groups for both races at once); final = product of the dials that describe a thing. Tracked in a GitHub issue.

## 1.0.7 (release build, staged 2026-09-18, NOT uploaded, UNTESTED in game)

- Author, first in-game autocast observation: "Why aren't air targets auto-cast for Bloodlust, and Death coil? Are
  they just out of range for me?" Not range. `ScanGridRaw` read only the unit grid at `0x91AD6C`; the game files a
  unit with `unit+0x1C & 4` (air layer) into a SECOND grid at `0x91AD70` and nowhere else (`FUN_004b4a00`:
  `0x4B4A39 mov eax,[0x91AD6C]; 0x4B4A3E cmovne eax,[0x91AD70]; 0x4B4A46 mov [eax+ecx*4],edx`). So since the first
  build every flyer was invisible to every scan: all nine spells as targets, and `EnemyNear`.
- Type flags from `unitdata.dat` rule the flag filter out: gryphon rider / dragon `0x08080082`, daemon `0x08080092`
  (fleshy + flyer); flying machine, zeppelin, eye `0x82` (not fleshy, so never heal / lust / coil targets, by design).
- The offline test had hidden it: its fake world put flyers into the ground grid. `AddUnit` now files flyers into an
  air grid only, so the existing dragon / daemon / eye / haste tests run through the second layer, plus new cases:
  coil an enemy dragon, lust my fighting dragon, enemy flyer counts as "enemy near", dragon hovering over a farm.
- Class hardening: the research note had this grid marked `[unverified]` "air layer" since the worker research and it
  was never followed up. RE_NOTES now states both grids with the rule "every scan reads both".

## 1.0.6 (release build, staged 2026-09-18, NOT uploaded, UNTESTED in game)

- Author: "Are oil platforms affected by unlimited gold mine? If not, we need an option for oil platforms too." They
  were not: the refill only looked at type 0x5C. New `[oil_platforms] unlimited`, default false.
- Evidence (docs/research/workers_and_gold.md + disassembly this session): platforms are the types with flag 0x800
  (0x56 / 0x57); "oil left" is the same u16 at +0x82, decremented by the same ENTER action (`0x4C99C8`) when a tanker
  enters. `0x4EDCB4`: a platform copies the amount from the oil patch under it when it is CREATED (patch hidden);
  `0x4EE4E3`: a dying platform with oil left creates a new patch 0x5D with that amount. So topping up platforms alone
  is enough, and patches are never drained on their own.
- `tweaks.cpp`: one shared peak table for mines and platforms, each kind forgotten when its switch goes off.

## 1.0.5 (release build, staged 2026-09-18, NOT uploaded)

- Author, on his tower tables: "Ok, it's not watch tower?" The game's strings say "Build Watch Tower" (orc) and "Build
  Scout Tower" (human); the mod only knew `orc_scout_tower`, so `[building.watch_tower]` would have been refused with
  a log note. Added building aliases `watch_tower`, `orc_watch_tower` -> 0x41 and `scout_tower` -> 0x40. `guard_tower`
  / `cannon_tower` stay unknown on purpose: the same in-game name exists for both races.
- Vanilla tower health read from `Data\Rez\unitdata.dat` (UDTA hit point words at offset 1676): scout 100, guard 130,
  cannon 160, both races. The author's doubles (200 / 260 / 320) are exact.
- In-game log, author's session 20:38 on the RIGHT install (1.0.4): first tick 20:38:32 with no "map load" line before
  it (= a save was loaded, tables untouched, as designed), then 20:38:44 "map load: 34 unit stats set, health [all
  x2.00 ...] costs [all x0.50 ...]". So Restart Scenario after loading a save DOES go through the new-map hook. Visible
  effect still to be confirmed by the author.

## 1.0.4 (release build, staged 2026-09-18, NOT uploaded)

- Author report 20:21: "I loaded up the campaign, I didn't see any changes in health to units when I restarted a loaded
  mission." Evidence: no `x86\gameplay_options.log` existed although the game had just run (`Saved Games\
  Warcraft2Remastered\Autosave.sav` 20:20:52). File access times show the session ran from a SECOND install elsewhere
  on the disk (a different game build, no mod files; its exe read at 20:20:20, its ogre voice files at 20:20:55), which
  a desktop shortcut pointed at, and the Battle.net agent log has no w2r launch request in that window. The mod lives
  in the Battle.net install (`...\Warcraft II Remastered\x86`, build 1.0.2.2818). So the mod was never in that session;
  the restart-after-load path is still UNTESTED, not disproven.
- The mod supports build 1.0.2.2818 only: in any other build it stays inert (PE timestamp gate).
- Hardening: `test/activation_host.cpp` + `test/activation_test.ps1`, a stand-in exe NAMED "Warcraft II.exe" that
  imports version.dll. Proves DllMain takes the game path and writes the log (the proxy load test only covers the
  inert path). The installed 1.0.3 DLL passed it, which ruled the DLL out.
- Readme + Nexus description gain the "no log = wrong install" troubleshooting entry.
- The author's desktop shortcut to the old copy was renamed so the two identical icons can be told apart.

## 1.0.3 (release build, staged 2026-09-18, NOT uploaded, UNTESTED in game)

- Author: "where is the health and cost configs for structures? I'm looking for the all option for structures but I
  can't find it. We might need to label sections better." There was no such key: structures only had the two group
  keys per race, buried after the unit groups.
- `Multipliers` gains top-level `units`, `structures`, `research`; `RaceMultipliers` gains `structures`.
  `Structure()` = all x structures x race.all x race.structures x group. New `StructureHealth()` leaves both `all`
  values out, so the unit-only rule of `[health]` holds. `[health]` reads `all` + `structures` at the top; `units` /
  `research` there are reported as unknown keys. Neutral structures are skipped outright.
- The default TOML is now generated by `tools/write_default_toml.py` (banners, contents list, sub-headers). Edit the
  generator, not the TOML.
- The author's installed config was ported onto the new layout with `tools/port_config.py` (values verified one by
  one, backup kept next to it).

## 1.0.2 (release build, staged 2026-09-18, NOT uploaded)

- Author's decision: "users probably won't appreciate having to disable features they might not want". Defaults in
  code and in the shipped config: spells heal / slow / bloodlust / raise_dead on, the other five off; eye cast and
  auto-scout off; worker auto-repair on, auto-harvest off; hero regen 0; all unit tables in the shipped file are
  commented examples. The author's own installed config was deliberately left untouched.
- 1.0.1 was built but never uploaded, so this gets its own number (one version per change) and both changelog
  entries are posted at upload time.

## 1.0.1 (release build, staged 2026-09-18, NOT uploaded)

- The author created the Nexus page and uploaded build dev.23 as "1.0.0" (file id 13, 2026-09-18 19:24). Builds dev.24
  to dev.26 are therefore the content of 1.0.1. Same code as dev.26; only the version string changed.
- From here on pre-release builds are numbered against the NEXT public version (`1.0.2-dev.N`).

## 1.0.0-dev.26 - 2026-09-18 (UNTESTED in game)

- `[building.<name>]` tables: the same ten base stats as `[unit.<name>]` for every structure (43 names, ids checked
  against `unitdata.dat` health and prices). Towers moved here from the unit names. A name in the wrong section is
  refused with a log note. Structure `hit_points` is limited to 32767 (signed 16-bit construction maths).
- `[health.human]` / `[health.orc]` gain `buildings` and `building_upgrades`. Structure health follows ONLY these keys:
  the `all` masters and unit groups stay unit-only, as the player asked earlier. Neutral structures are never scaled.
- `tools/port_config.py`: carries a player's changed values and added tables over to a new default file, verifies the
  result value by value.

## 1.0.0-dev.25 - 2026-09-18 (UNTESTED in game)

- Unit price multipliers now scale oil too (player: "I wanted oil costs too"). The first request named gold and lumber
  only, which is why oil had been left out.

## 1.0.0-dev.24 - 2026-09-18 (UNTESTED in game)

- Player report: "uther seems to be missing" from `[heroes] units`. He was there as `uther_lightbringer`, the last
  entry of a two-line list. The default list is now grouped by side with all 15 heroes, and unit names accept short
  and in-game spellings everywhere (`uther`, `grom`, `grommash_hellscream`, `teron`, `korgath`, `kurdran_and_skyree`,
  `gul_dan`, `cho_gall`, `zul_jin`, `gryphon`, `ogremage`, `deathknight`, `sappers`). Game display names checked in
  `Data\Strings\enUS.json`.

## 1.0.0-dev.23 - 2026-09-18 (UNTESTED in game)

- The author named the mod: everything mod-level is now `gameplay_options` (config `gameplay_options.toml`, log
  `gameplay_options.log`, readme / tutorial file names, banners "Gameplay Options: ...", zip
  `War2R-Gameplay-Options-<version>.zip`, repo `war2r-gameplay-options`). "Autocast" now only names the spell-casting
  feature (`[autocast]` section, `src/autocast.*`, the Ctrl+F9 "Autocast ON/OFF" banner).
- `deploy.ps1` renames an existing `autocast.toml` to `gameplay_options.toml`, keeping the player's settings.
- Nexus texts as plain BBCode files in `nexus/`: summary, description (installation + how to use the config file),
  changelog.

## 1.0.0-dev.22 - 2026-09-18 (UNTESTED in game)

- The `[vision]` bonus section is gone. Dragons and gryphon riders get their sight from ordinary
  `[unit.dragon]` / `[unit.gryphon_rider]` tables (`sight = 8`) in the shipped config.

## 1.0.0-dev.21 - 2026-09-18 (UNTESTED in game)

- `[unit.<name>]` tables: base stats per unit type (hit_points, armor, basic_damage, piercing_damage, range, sight,
  gold, lumber, oil, build_time). -1 or a missing key keeps the game's value, 0 is a real value. Applied before the
  multipliers. The shipped config carries the user's destroyer example (105 HP, armor 11, 37 + 2 damage, range 5,
  sight 9, 600 / 300 / 500, build time 80; game values 100, 10, 35 + 0, 4, 8, 700 / 350 / 700, 90).
- `[range.units]` (added in dev.20, never shipped) is folded into these tables. Tower names added for them.

## 1.0.0-dev.20 - 2026-09-18 (UNTESTED in game)

- `[range] upgrade_bonus`: the Longbow / Lighter Axes bonus is the `inc al` at `0x4EE689` in GetAttackRange; the mod
  rewrites it as `add al, n` (same 2 bytes) and keeps the status panels' per-level byte `0x8C11E4` in step. Synced every
  tick (a savegame can load without a new map) and forced back to the game's +1 in multiplayer. Refuses to patch if
  the two bytes are not the expected ones.

## 1.0.0-dev.19 - 2026-09-18 (UNTESTED in game)

- `[time]`: training, construction, structure upgrade and research time multipliers, same tree as `[costs]`.
  Byte tables (`0x917910`, `0x9188A8`), cap 255.

## 1.0.0-dev.18 - 2026-09-18 (UNTESTED in game)

- `[costs]` restructured: master `all`, then `[costs.human]` / `[costs.orc]` with `all`, the umbrellas `units` and
  `research`, the 8 unit groups, `buildings`, `building_upgrades` and the research groups (`naval_upgrades` is the ship
  research group; `paladin_upgrades` / `ogre_mage_upgrades`, `mage_spells` / `death_knight_spells` are per race).
  Every research row and structure type is assigned a race (units.cpp).

## 1.0.0-dev.17 - 2026-09-18 (UNTESTED in game)

- `[health]` restructured: master `all`, `[health.human]` / `[health.orc]` with `all` and the unit groups including
  `heroes`, `[health.neutral] all`. Values multiply top down. Still units only.

## 1.0.0-dev.16 - 2026-09-18 (UNTESTED in game)

- `[costs]` is now a full set of price groups, all 1.0 by default: `units`, `buildings`, `building_upgrades`
  (types 0x58-0x5B keep / stronghold / castle / fortress and 0x60-0x63 guard / cannon towers: a structure upgrade is
  priced as the type it turns into, same pay path `FUN_004ac610`), and research by group: `melee_upgrades` (UGRD rows
  0-3, 8-11), `ranged_upgrades` (4-7, 24-31), `siege_upgrades` (20-23), `paladin_ogre_mage_upgrades` (32-36, 43, 44,
  50), `naval_upgrades` (12-19), `mage_death_knight_spells` (37-42, 45-49, 51).
- Buildings and building upgrades scale gold, lumber and oil; units stay gold and lumber only. Free rows stay free.
- Structure ids and prices checked against `Data\Rez\unitdata.dat` (tower flag 0x100000 on 0x60-0x63, hall flag on
  0x4A, 0x4B, 0x58-0x5B).

## 1.0.0-dev.15 - 2026-09-18 (UNTESTED in game)

- Health and price multipliers now default to 1.0 (the game's own numbers) and only ever touch units: the
  `[health] buildings` key is gone and structure rows are never read or written. Accepted range 0.01 to 1000.
- Caps are the engine's storage limits instead of a cautious 9999: health 65535 (16-bit word; damage code verified
  unsigned, see the addendum in docs/research/data_tables.md), unit price 2550 (one byte of tens), upgrade price 65535.

## 1.0.0-dev.14 - 2026-09-18 (UNTESTED in game)

- Vision (#7): `[vision]` maps unit names to extra sight range, default `dragon = 2`, `gryphon_rider = 2` (6 -> 8).
  Clamped at 9: the engine has reveal functions for sight 0..9 only, and a foreign pointer in the sight table would
  crash the next savegame load.

## 1.0.0-dev.13 - 2026-09-18 (UNTESTED in game)

- Upgrade prices (#8): `[costs] ranged_upgrades = 0.5` (UGRD rows 4-7, 24-31: arrows, throwing axes, ranger / berserker
  upgrade, longbow, lighter axes, scouting, marksmanship, regeneration) and `siege_upgrades = 0.5` (rows 20-23).

## 1.0.0-dev.12 - 2026-09-18 (UNTESTED in game)

- Unit prices (#9): `[costs] units = 0.5` scales the gold and lumber byte tables (price / 10) for unit types below
  0x3A. Rounds to the nearest 10, a priced unit never becomes free, buildings and oil are untouched.

## 1.0.0-dev.11 - 2026-09-18 (UNTESTED in game)

- Second hook: the new-map-only `call FinalizeTables` at `0x4D2C46`. The mod edits the unit / upgrade tables there:
  after the map's or the default data (and Blizzard's hard-coded overrides) are loaded, before any unit exists, and
  never on a savegame load, so nothing can double-apply (saves store the tables). Gated on the load-time network
  flag `0x922F5B`. Research: docs/research/data_tables.md. If this hook does not match, only these tweaks are lost.
- Health (#5): `[health] units = 2.0`, `heroes = 4.0`, `buildings = 1.0`. Capped at 9999; gold mine, dark portal and
  runestone are never scaled. Heroes are the `[heroes] units` list.
- Config is now loaded by whichever hook fires first (the map-load hook runs before the first tick).

## 1.0.0-dev.10 - 2026-09-18 (UNTESTED in game)

- Idle workers (#3, #4): a peasant / peon of yours that sits in STOP with nothing queued repairs the nearest damaged,
  finished building of yours within `repair_radius` (10) after `repair_idle_seconds` (1), otherwise after
  `harvest_idle_seconds` (10) walks to the nearest gold mine or reachable tree within `harvest_radius` (5). A loaded
  worker returns its cargo instead (a harvest order would relabel carried gold as lumber). Stand Ground is respected.
  Repair needs 1+ gold and 1+ lumber (the game stops the worker with a message otherwise); construction sites are
  skipped (power-build exploit). Orders use the game's own handler table entries 23 / 24 / 27 and clear the Remastered
  resume-order byte the way the player command path does. Research: docs/research/workers_and_gold.md.
- selftest now verifies the handler table entries and the gold decrement instruction bytes in the real exe.

## 1.0.0-dev.9 - 2026-09-18 (UNTESTED in game)

- Unlimited gold mines (#2), `[gold_mines] unlimited = false` by default. No code patch: each pass the mine's
  "gold left" word (+0x82, hundreds) is restored to the most it held while watched, never below 50 (5000 gold, which
  also keeps the computer's expansion test satisfied). Applies to every mine on the map, the computer's included.

## 1.0.0-dev.8 - 2026-09-18 (UNTESTED in game)

- Eye of Kilrogg (#1): idle ogre-magi (also Dentarg and Cho'gall) cast it at `cast_at_mana` (default 255, the
  computer's own rule), at most `max_active` eyes at a time. Cast through the same IssueOrder + pending-spell path the
  player's own button uses (research: docs/research/eye_of_kilrogg.md).
- Auto-scout: the computer's eye only random-walks (AI order 4). The mod steers the player's eyes with the move
  handler toward tiles the local explored map (`0x91AD60`) marks as never seen, then toward fogged tiles, then wanders.
  An eye that comes to rest more than 2 tiles from where the mod sent it is treated as player-controlled and released.
- IssueOrder / IssueSpell moved into `world.*` for every feature to share.

## 1.0.0-dev.7 - 2026-09-18 (UNTESTED in game)

- Hero regeneration (#6): `[heroes] regen_hp_per_second = 1`, `regen_for = "all" | "mine"`, and a configurable
  `units` list that defines what a hero is (only 5 of the 15 hero types carry the game's own hero flag). Paced by real
  time while the simulation is stepping; pauses and loads are not credited.
- Internal split: `world.*` (game snapshot + unit helpers), `mod.*` (tick orchestration, multiplayer gate),
  `tweaks.*` (non-spell features). The multiplayer gate now covers every feature, not only casting.

## 1.0.0-dev.6 - 2026-09-18 (UNTESTED in game)

- Config moved from `autocast.ini` to `autocast.toml` (toml++ 3.4.0 vendored). New `[polymorph] targets` list is the
  whole Polymorph rule: listed unit types only, earlier in the list wins, nearest first. The default list reproduces
  the previous behaviour (flyers, then casters, then 90+ HP ground units); `polymorph_min_hp` is gone.
- Unknown sections/keys, wrong value types and out-of-range numbers are reported in `autocast.log`; a syntax error
  keeps the previous settings and shows an in-game banner. A successful hot reload shows a banner too.
- Version scheme: public changelog starts at 1.0.0, pre-release builds are 1.0.0-dev.N.

## 0.1.5 - 2026-09-18 (UNTESTED in game)

- Fix (found by decompile review before any in-game run): the game's SetOrder (`FUN_004ef080`) writes a new order to
  the unit's NEXT-order slot (+0x2F); it only becomes the current order (+0x2E) when the running action step ends
  (`0x4ED9C3`). The mod checked +0x2E, so every cast looked like a failure: a paladin's heal fell through to exorcism
  in the same pass, no target was ever claimed, and casts were re-issued every pass. All order reads now use the
  effective order (next if set, else current), the same rule the game's UI uses at `0x4E85B2`.
- A move the player has just queued is protected exactly like a move under way.

## 0.1.4 - 2026-09-18 (UNTESTED in game)

- Heal only goes on units missing at least 10 HP (`heal_min_missing_hp = 10`), so paladins stop casting on scratches.
  The percent gate stays as an option but now defaults to off (`heal_below_pct = 100`).

## 0.1.3 - 2026-09-18 (UNTESTED in game)

- Raise Dead added for death knights (`raise_dead = 1`). Cast at the nearest corpse tile, the way the game AI does
  it (order 0x32 at x/y, no target unit), and only while an enemy is within `search_radius`. It runs first in the
  death knight's priority list, matching the AI: raise dead, unholy armor, death coil, haste.
- Fireball, Flame Shield, Invisibility, Blizzard, Death and Decay, Whirlwind and Runes are deliberately left manual.

## 0.1.2 - 2026-09-18 (UNTESTED in game)

- Haste only goes on your flying units (`haste_flyers_only = 1`, new default), when they have an attack order or
  are fighting. Set it to 0 for the old rule (any fighting unit).

## 0.1.1 - 2026-09-18 (UNTESTED in game)

- Polymorph: flying attackers (dragon, gryphon rider, daemon, Deathwing) always qualify and are picked first, then
  enemy casters, then big ground units. Before this a daemon was skipped: its max HP in `Data\Rez\unitdata.dat` is 60,
  under the default `polymorph_min_hp = 90`. Dragons (100 HP) already qualified.

## 0.1.0 - 2026-09-18 (UNTESTED in game)

- First build. `version.dll` proxy for Warcraft II Remastered 1.0.2.2818, hooks the per-step AI tick.
- Autocast for Heal, Exorcism, Slow, Polymorph, Bloodlust, Death Coil, Haste; Unholy Armor available, off by default.
- `autocast.ini` with hot reload, `Ctrl+F9` in-game toggle, `autocast.log`.
- Inert in multiplayer, in any other exe build, and in the launcher helper exes that share the folder.
- Offline `selftest.exe` (hook bytes + targeting logic against the real exe image) and 32-bit proxy load test pass.
