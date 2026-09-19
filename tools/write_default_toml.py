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
               '            3. WORKERS          4. GOLD MINES AND OIL  5. HEROES',
               '            6. HEALTH           7. PRICES              8. BUILD AND RESEARCH TIME',
               '            9. RANGE UPGRADE   10. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING')
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
# Raise Dead is only cast while an enemy is within search_radius, so skeletons are not wasted.
raise_dead = true

[autocast]
# How far (tiles) a caster looks for targets. It walks into range if needed.
search_radius = 8
# Bloodlust / Haste / Unholy Armor only go on units that are fighting: attacking with an enemy this close.
combat_radius = 6
# true = Heal / Bloodlust / Haste only on your own units, false = allies too.
own_units_only = true
# true = casters may interrupt their own attack to cast.
cast_while_attacking = true

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
# How many of your eyes may be out at the same time.
max_active = 1
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

''' + banner('4. GOLD MINES AND OIL') + '''
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

''' + banner('5. HEROES') + '''
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
# Hit points every hero regenerates per second of play. 0 = off. Example: 1
regen_hp_per_second = 0
# "all" = every hero on the map (enemy heroes too), "mine" = only your own.
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

''' + banner('10. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING',
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
