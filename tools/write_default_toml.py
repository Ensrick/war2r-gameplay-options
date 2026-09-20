"""Generates config/gameplay_options.default.toml with a banner above every part of the file."""
import pathlib

CRLF, LF = chr(13) + chr(10), chr(10)
BAR = '# ' + '=' * 110


def banner(title, *lines):
    out = [BAR, '#  ' + title]
    out += ['#  ' + l for l in lines]
    out.append(BAR)
    return LF.join(out) + LF


UNIT_GROUPS = [('workers', 'peasant / peon'), ('melee', 'footman, knight, paladin / grunt, ogre, ogre-mage'),
               ('ranged', 'archer, ranger / axethrower, berserker'), ('siege', 'ballista / catapult'),
               ('casters', 'mage / death knight'), ('air', 'gryphon rider, flying machine / dragon, zeppelin'),
               ('naval', 'every ship'), ('demolition', 'dwarves / goblin sappers')]


def race_table(section, race, health):
    human = race == 'human'
    who = 'HUMAN' if human else 'ORC'
    L = [f'[{section}.{race}]']
    everything = f'# everything {race}: units, ships and structures' if health else f'# everything {race}: units, structures and research'
    L += [everything, 'all = 1.0', '', f'# --- {who} UNITS ---', f'# every {race} unit and ship', 'units = 1.0']
    for key, what in UNIT_GROUPS:
        L.append(f'{key} = 1.0'.ljust(24) + '# ' + (what.split(' / ')[0 if human else 1] if ' / ' in what else what))
    if health:
        L.append('heroes = 1.0'.ljust(24) + f'# {race} heroes (the list is under HEROES above)')
    L += ['', f'# --- {who} STRUCTURES ---', f'# every {race} structure', 'structures = 1.0',
          'buildings = 1.0'.ljust(24) + '# everything a worker places',
          'building_upgrades = 1.0'.ljust(24) + ('# keep, castle, guard tower, cannon tower' if human else '# stronghold, fortress, guard tower, cannon tower')]
    if not health:
        L += ['', f'# --- {who} RESEARCH ---', f'# all {race} research', 'research = 1.0',
              'melee_upgrades = 1.0'.ljust(28) + ('# swords, shields' if human else '# battle axes, shields'),
              'ranged_upgrades = 1.0'.ljust(28) + ('# arrows, ranger upgrade, longbow, scouting, marksmanship' if human else '# throwing axes, berserker upgrade, lighter axes, scouting, regeneration'),
              'siege_upgrades = 1.0'.ljust(28) + ('# ballista upgrades' if human else '# catapult upgrades'),
              'naval_upgrades = 1.0'.ljust(28) + '# ship cannons and ship armor']
        if human:
            L += ['paladin_upgrades = 1.0'.ljust(28) + '# paladin upgrade, holy vision, healing, exorcism',
                  'mage_spells = 1.0'.ljust(28) + '# flame shield, fireball, slow, invisibility, polymorph, blizzard']
        else:
            L += ['ogre_mage_upgrades = 1.0'.ljust(28) + '# ogre-mage upgrade, eye of kilrogg, bloodlust, runes',
                  'death_knight_spells = 1.0'.ljust(28) + '# raise dead, death coil, whirlwind, haste, unholy armor, death and decay']
    return LF.join(L) + LF


text = ''
text += banner('WARCRAFT II: REMASTERED - GAMEPLAY OPTIONS',
               'Out of the box only four autocasts (Heal, Slow, Bloodlust, Raise Dead) and worker auto-repair are on.',
               'Everything else is off or neutral until you turn it on; ready-made examples are given as comments.',
               '',
               'Save the file while the game runs: changes are picked up within a few seconds. A typo keeps your previous',
               'settings and is reported in gameplay_options.log. Single-player only: the mod switches itself off in multiplayer.',
               '',
               'CONTENTS    1. GENERAL          2. AUTOCAST (spells, heal, polymorph, haste, eye of kilrogg)',
               '            3. WORKERS          4. RESOURCES           5. HEROES AND REGENERATION',
               '            6. HEALTH           7. PRICES              8. BUILD AND RESEARCH TIME',
               '            9. RANGE UPGRADE   10. SPELL POWER AND MANA',
               '           11. AUTO-PRODUCTION  12. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING')
text += '''
''' + banner('1. GENERAL') + '''
[general]
# Master switch for autocasting. Ctrl + toggle_key flips it in game.
enabled = true
# "F1".."F12", or a Windows virtual-key number. "" = no hotkey.
toggle_key = "F9"
# Game steps between autocast passes. Lower = snappier; the computer AI itself uses 50.
interval_ticks = 10
# true = write every cast and worker order to gameplay_options.log
log_casts = false

''' + banner('2. AUTOCAST', 'Your casters cast on their own. Move orders are never interrupted; two casters never pick the same target.') + '''
[spells]
# true = your casters cast this spell on their own.
heal = true
exorcism = false
slow = true
polymorph = false
bloodlust = true
death_coil = false
haste = false
unholy_armor = false
# Raise Dead: any fresh corpse within 15 tiles, enemies around or not. It must be researched unless the map grants it.
raise_dead = true
# Area spells hurt YOUR units and buildings too. They are only cast with no friendly unit, building or wall in the
# blast area, and a Blizzard / Death and Decay the mod started is stopped when a friendly walks in (see [autocast]).
fireball = false
blizzard = false
death_and_decay = false
whirlwind = false
flame_shield = false
runes = false
invisibility = false
# Holy Vision: an idle paladin at full mana looks at the least explored part of the map. It may move the camera to
# its target for your own paladins (not tested in game yet).
holy_vision = false

[autocast]
# How far (tiles) a caster looks for targets. It walks into range if needed.
search_radius = 8
# Bloodlust / Haste / Unholy Armor only go on units that are fighting: attacking with an enemy this close.
combat_radius = 6
# true = Heal / Bloodlust / Haste only on your own units, false = allies too.
own_units_only = true
# true = casters may interrupt their own attack to cast.
cast_while_attacking = true
# Blizzard and Death and Decay keep casting wave after wave. The mod stops the ones it started when a friendly walks
# in, when no enemy is left, or when the caster's mana drops below this (0 = let them run until the game stops them).
channel_mana_reserve = 0
# Blizzard, Death and Decay and Whirlwind need at least this many enemies close together.
area_min_enemies = 3
# Fireball needs at least this many enemies along its path (1 = the computer's own rule).
fireball_min_enemies = 2

[heal]
# Heal only units missing at least this many hit points.
min_missing_hp = 10
# Extra gate: also require the unit to be at or below this percent of max HP. 100 = off.
below_percent = 100

[polymorph]
# Valid Polymorph targets in priority order: the first type in this list that is in range gets sheeped,
# nearest one first. Anything not listed is never polymorphed. Only living (organic) units can be affected.
# Names: footman grunt peasant peon knight ogre archer axethrower mage death_knight paladin ogre_mage dwarves
#   goblin_sappers ranger berserker gryphon_rider dragon daemon skeleton critter
#   heroes: alleria teron_gorefiend kurdran dentarg khadgar grom_hellscream deathwing turalyon danath
#   korgath_bladefist chogall lothar guldan uther_lightbringer zuljin
targets = [
    "deathwing", "kurdran", "dragon", "gryphon_rider", "daemon",
    "death_knight", "mage", "ogre_mage", "paladin",
    "teron_gorefiend", "guldan", "khadgar", "dentarg", "chogall", "turalyon", "uther_lightbringer",
    "grom_hellscream", "korgath_bladefist", "danath", "alleria", "knight", "ogre", "lothar",
]

[haste]
# true = Haste only on your flying units, when they are sent to attack or are fighting. false = any fighting unit.
flyers_only = true

[eye_of_kilrogg]
# true = ogre-magi cast Eye of Kilrogg on their own. Only an idle ogre-mage does it (the cast makes it stop).
cast = false
# Mana the ogre-mage must have before it casts. 255 = only when full, which is the computer's own rule,
# so Bloodlust always gets first call on the mana.
cast_at_mana = 255
# How many of your eyes may be out at the same time (all your ogre-magi together).
max_active = 3
# true = your eyes fly to unexplored ground by themselves (then to fogged ground once everything is explored).
# Give an eye an order of your own and the mod leaves that eye to you. Works for eyes you cast by hand too.
auto_scout = false

''' + banner('3. WORKERS', '"Idle" means stopped with nothing queued. Stand Ground is respected: that is how you park a worker.') + '''
[workers]
# Idle this long -> repair the nearest damaged building of yours within repair_radius tiles. Needs at least
# 1 gold and 1 lumber in the bank; buildings still under construction are left alone.
auto_repair = true
repair_idle_seconds = 1
repair_radius = 10
# Idle this long -> walk to the nearest gold mine or tree within harvest_radius tiles (a loaded worker returns
# its cargo first).
auto_harvest = false
harvest_idle_seconds = 10
harvest_radius = 5

''' + banner('4. RESOURCES', 'Gold mines, oil, food and trees.') + '''
[gold_mines]
# true = gold mines never run dry (every mine on the map, the computer's too). Starving a mine is a legitimate way to
# win some campaign missions, but a computer that runs out of gold can stall a long game: your call.
unlimited = false
# Multiplies the gold in every mine when a NEW map starts: 3.0 = three times as much, for both sides. "Gold left"
# shows the real number. A mine holds up to 6,553,500, ten times what the map editor allows, so 10.0 always fits.
amount = 1.0

[oil_platforms]
# true = oil platforms never run dry (every platform on the map, the computer's too). A platform keeps the amount it
# had when you switched this on, never less than 5000.
unlimited = false
# The same for oil: every oil patch and platform, when a NEW map starts.
amount = 1.0

[food]
# true = every Town Hall / Great Hall, Keep / Stronghold and Castle / Fortress gives hall_food_amount food instead
# of the game's 1. In a custom game that starts with one peasant you can then train from the hall right away,
# without waiting for a farm. A farm still gives 4, the 200 food limit stays. The computer gets the same.
hall_food = false
hall_food_amount = 5

[trees]
# EXPERIMENTAL. true = felled forest grows back, so lumber never runs out for good (the computer's too). Every stump
# waits its own time between regrow_min_minutes and regrow_max_minutes of play, so a felled patch fills back in bit
# by bit. A tree never grows back close to a building, a wall or a ground unit, never where it would close a
# passage, and only where a forest stood before. Flyers do not hold it up.
regrow = false
regrow_min_minutes = 10
regrow_max_minutes = 20
# No regrowth within this many tiles of a building or wall / of a ground unit of any player.
building_distance = 3
unit_distance = 3

''' + banner('5. HEROES AND REGENERATION') + '''
[heroes]
# Which unit types count as heroes (used by hero regeneration and by the "heroes" health multiplier).
# All 15 heroes of both campaigns are listed. Short names work too: "uther", "grom", "teron", "korgath".
units = [
    # Alliance
    "alleria", "danath", "khadgar", "kurdran", "lothar", "turalyon", "uther_lightbringer",
    # Horde
    "chogall", "dentarg", "grom_hellscream", "guldan", "korgath_bladefist", "teron_gorefiend", "zuljin",
    "deathwing",
]
# true = heroes regenerate regen_hp_per_second hit points per second of play.
regen = false
regen_hp_per_second = 2
# "all" = every hero on the map (enemy heroes too), "mine" = only your own.
regen_for = "all"

[unit_regen]
# true = every unit regenerates hit points while you play: land units, flyers and ships, never a structure. A hurt
# unit is then worth keeping instead of being a waste of food. Heroes follow [heroes] regen while that is on.
enabled = false
hp_per_second = 1
# "all" = every unit on the map (the enemy's too), "mine" = only your own.
regen_for = "all"

''' + banner('MULTIPLIERS: HEALTH (6), PRICES (7), BUILD AND RESEARCH TIME (8)',
             '1.0 = the game\'s own number, 0.5 = half, 2.0 = double. Values multiply into each other from the top down:',
             '',
             '      master          x      race               x      group',
             '      [costs]                [costs.human]             e.g. melee, buildings, naval_upgrades',
             '',
             'Every section has its masters first, then one table for humans and one for orcs. Inside a race table the',
             'lines are grouped under UNITS, STRUCTURES and RESEARCH.',
             '',
             'These are applied once, when a NEW map starts (new mission, custom game, restart). A savegame keeps the numbers it',
             'was made with. The computer plays by the same numbers. Engine limits: unit health 65535 (the status panel stops',
             'printing numbers from 10000 up), structure health 32767, unit and structure prices 2550 in steps of 10, research',
             'prices 65535, times 255 (many are already 200-255: they can be cut freely but barely lengthened). Free stays free.') + '''
''' + banner('6. HEALTH', 'Same layout as PRICES and TIME: "all" is everything, then one master per kind.') + '''
[health]
# Master for EVERYTHING: every unit, ship and structure of every race.
all = 1.0
# Masters per kind (both races).
units = 1.0              # every unit and ship, never a structure
structures = 1.0         # every building, never a unit

''' + race_table('health', 'human', True) + LF + race_table('health', 'orc', True) + '''
[health.neutral]
# Skeletons, daemons, critters, the Eye of Kilrogg.
all = 1.0

''' + banner('7. PRICES', 'Gold, lumber and oil all scale.') + '''
[costs]
# Master for EVERYTHING: units, ships, structures and research.
all = 1.0
# Masters per kind (both races).
units = 1.0
structures = 1.0
research = 1.0

''' + race_table('costs', 'human', False) + LF + race_table('costs', 'orc', False) + '''
''' + banner('8. BUILD AND RESEARCH TIME', 'Training, construction, structure upgrades and research. Same layout as PRICES.') + '''
[time]
# Master for everything.
all = 1.0
# Masters per kind (both races).
units = 1.0
structures = 1.0
research = 1.0

''' + race_table('time', 'human', False) + LF + race_table('time', 'orc', False) + '''
''' + banner('9. RANGE UPGRADE') + '''
[range]
# What Longbow (rangers) and Lighter Axes (berserkers) add to attack range. The game's own bonus is 1.
# One number for both: the game uses a single rule for the two upgrades. Applies right away, no new map needed.
upgrade_bonus = 1

''' + banner('10. SPELL POWER AND MANA') + '''
[spell_damage]
# Damage of every damage spell, and the healing of Heal, x this: 2.0 = double. The per-spell numbers below replace
# the game's own first (-1 = the game's), then this multiplier applies. Everyone gets these numbers, the computer too.
all = 1.0
fireball = -1          # per explosion, 5 explosions per cast (game: 40, at most 254)
flame_shield = -1      # per flame hit (game: 4, at most 254)
blizzard = -1          # per hit (game: 10, at most 254)
death_and_decay = -1   # per hit (game: 10, at most 254)
whirlwind = -1         # per hit (game: 4, at most 254)
death_coil = -1        # per cast, shared by the enemies hit; the caster heals as much (game: 50, at most 127)
runes = -1             # per rune (game: 50, at most 128)
heal = -1              # the most hit points one Heal restores (game: 40, at most 255)

[spell_cost]
# Mana cost of every spell x this, rounded up. The per-spell numbers below replace the game's own first (-1 = the
# game's), then this multiplier applies. 1 to 255. Heal and Exorcism cost mana PER HIT POINT, and
# [spell_damage] all lowers that price further (x2 damage = half the price). Everyone gets these costs.
all = 1.0
holy_vision = -1       # game: 70
heal = -1              # game: 5 per hit point healed
exorcism = -1          # game: 4 per hit point of damage
flame_shield = -1      # game: 80
fireball = -1          # game: 100
slow = -1              # game: 50
invisibility = -1      # game: 200
polymorph = -1         # game: 200
blizzard = -1          # game: 25 per wave
eye_of_kilrogg = -1    # game: 70
bloodlust = -1         # game: 60
raise_dead = -1        # game: 50
death_coil = -1        # game: 100
whirlwind = -1         # game: 100
haste = -1             # game: 50
unholy_armor = -1      # game: 100
runes = -1             # game: 200
death_and_decay = -1   # game: 30 per wave

[mana]
# How fast casters regain mana: 2.0 = twice as fast (the game: 1 point every 40 steps). Mana stops at 255, that
# limit is part of the game. Everyone gets it, the computer too.
regen = 1.0

''' + banner('11. AUTO-PRODUCTION') + '''
[auto_production]
# true = every IDLE building of yours trains by itself, all of them at once: halls, keeps and castles make workers,
# barracks, aviaries / roosts, mage towers / temples and shipyards make the army. Single player only. The computer's
# buildings are never touched, a building you have selected is left alone, and the mod never starts a research or a
# building upgrade. It never builds transports, flying machines / zeppelins, dwarves / sappers or heroes, and never a
# second oil tanker: scouting, ferrying and oil runs stay yours.
enabled = false
toggle_key = "F10"          # Ctrl + this key turns it on and off in game ("" = no hotkey)
workers_tier1 = 12          # workers wanted with a town hall / great hall
workers_tier2 = 16          # with a keep / stronghold
workers_tier3 = 24          # with a castle / fortress (0 to 200, 0 = never train workers)
# Food that always stays free AFTER the unit being trained: this much, or this share of your supply, whichever is
# more. That room is yours, for peasants, transports, zeppelins or a tanker of your own.
food_free_min = 4
food_free_percent = 10
# Money kept back for upgrades: the dearest RESEARCH you could buy right now plus this share of everything else you
# could buy. Keeps, castles and towers never count as the dearest, only at this weight.
reserve_extra = 0.25
upgrade_bias = 0.25         # each upgrade level a unit line already has raises that class's share by this much
filler_min = 10             # nothing of the mix affordable at a building: build what the spare bank buys this often
# How much of your army is ships comes from the MAP: the water fraction plus 5% per oil source (at most 30% from
# oil), a little higher while you only have a town hall. No oil and less than 10% water = a land map, no ships.
navy_weight = 1.0           # x that share (0 = never build ships)
navy_max = 80               # ships are never more than this much of the army, in percent
workers_ignore_reserve = true  # workers wait for their own price only, never for the upgrade reserve
tankers_ignore_reserve = true  # the one oil tanker with them

[auto_production.units]
# Switch a whole class off. Names are race-neutral: infantry = footman / grunt, archers = archer, ranger / axethrower,
# berserker, knights = knight, paladin / ogre, ogre-mage, casters = mage / death knight, flyers = gryphon rider /
# dragon, siege = ballista / catapult, battleships = battleship / juggernaught, submarines = submarine / turtle.
workers = true
infantry = true
archers = true
knights = true
casters = true
flyers = true
siege = true
tankers = true
destroyers = true
battleships = true
submarines = true

[auto_production.bank_multiple]
# Spare money (what is left after the upgrade reserve) that must be there before a unit is trained, as a multiple of
# that unit's CURRENT price: four grunts' worth of gold before a grunt. Raise a price anywhere and this rises with it.
all = 4.0
# knights = 8.0             # or per class, e.g. be twice as rich before each knight. Submarines always want twice.

# Army mix per hall tier. These are WEIGHTS, not percentages: only their ratios matter, so 75 / 20 / 5 builds exactly
# the same army as 15 / 4 / 1, and they need not add up to anything. Land and ships are split by the map first (see
# navy_weight above), then each group's weights are shared out over what you can actually train; a class you cannot
# build yet gives its weight to the others, a line with upgrades gets more, and plenty of gold with little lumber
# makes the cheap classes grow by themselves.
[auto_production.land_tier1]
infantry = 75
archers = 20
siege = 5

[auto_production.land_tier2]
knights = 60
archers = 25
infantry = 10
siege = 5

[auto_production.land_tier3]
knights = 40
casters = 25
archers = 15
flyers = 15
siege = 5
infantry = 0

# While no hostile player owns a shipyard or a warship anywhere on the map (fog does not matter), the mod holds at
# most this many of each class and builds no further ships; the share that would have gone to ships goes to your
# land army. Ships you already have count. A class left out of this table has no ceiling.
[auto_production.no_enemy_navy_cap]
tankers = 1
destroyers = 5
battleships = 2
submarines = 2

[auto_production.navy_tier1]
destroyers = 80
battleships = 20

[auto_production.navy_tier2]
battleships = 60
destroyers = 25
submarines = 10

[auto_production.navy_tier3]
battleships = 60
destroyers = 25
submarines = 10

''' + banner('12. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING',
             'One [unit.<name>] table per unit, one [building.<name>] table per structure. -1 (or leaving a line out) keeps',
             'the game\'s own number; 0 is a real value where it makes sense (armor, damage, lumber, oil ...). These replace the',
             'game\'s base numbers first; the multipliers above then apply on top of them. Applied when a new map starts.',
             '',
             '   hit_points 1-65535 (structures 1-32767)   armor 0-255   basic_damage 0-255   piercing_damage 0-255',
             '   range 0-20 tiles   sight 0-9   gold / lumber / oil 0-2550 in steps of 10   build_time 0-255',
             '',
             'UNIT NAMES      every name listed under [polymorph], plus ballista catapult flying_machine zeppelin human_tanker',
             '                orc_tanker human_transport orc_transport elven_destroyer troll_destroyer battleship juggernaught',
             '                gnomish_submarine giant_turtle eye_of_kilrogg. Short names work: uther, grom, gryphon ...',
             'BUILDING NAMES  farm pig_farm human_barracks orc_barracks church altar_of_storms human_scout_tower orc_scout_tower',
             '                stables ogre_mound gnomish_inventor goblin_alchemist gryphon_aviary dragon_roost human_shipyard',
             '                orc_shipyard town_hall great_hall elven_lumber_mill troll_lumber_mill human_foundry orc_foundry',
             '                mage_tower temple_of_the_damned human_blacksmith orc_blacksmith human_refinery orc_refinery',
             '                human_oil_platform orc_oil_platform keep stronghold castle fortress human_guard_tower',
             '                orc_guard_tower human_cannon_tower orc_cannon_tower human_wall orc_wall gold_mine dark_portal runestone',
             '                The orc Watch Tower is orc_scout_tower (watch_tower and orc_watch_tower work too).') + '''
# Template (remove the "# " in front of the lines you want):
# [unit.footman]
# hit_points = -1
# armor = -1
# basic_damage = -1
# piercing_damage = -1
# range = -1
# sight = -1
# gold = -1
# lumber = -1
# oil = -1
# build_time = -1

# Example: scouts of the sky. Dragons and gryphon riders see 8 tiles instead of the game's 6.
# [unit.dragon]
# sight = 8
#
# [unit.gryphon_rider]
# sight = 8

# Example: a destroyer with every stat nudged a little, so each one is easy to check in game.
# The game's own destroyer: 100 hit points, armor 10, damage 35 basic + 0 piercing, range 4, sight 8,
# 700 gold / 350 lumber / 700 oil, build time 90. Copy it for troll_destroyer to change the orc ship too.
# [unit.elven_destroyer]
# hit_points = 105
# armor = 11
# basic_damage = 37
# piercing_damage = 2
# range = 5
# sight = 9
# gold = 600
# lumber = 300
# oil = 500
# build_time = 80

# Example: a tougher guard tower. The game's own: 130 hit points, armor 20, damage 4 basic + 12 piercing, range 6,
# sight 9, 500 gold / 150 lumber, build time 140.
# [building.human_guard_tower]
# hit_points = 200
# piercing_damage = 14
# range = 7
'''
out = pathlib.Path(__file__).resolve().parent.parent / 'config' / 'gameplay_options.default.toml'
out.write_bytes(text.replace(LF, CRLF).encode('utf-8'))
print(len(text.splitlines()), 'lines written')
