"""Brings an older gameplay_options.toml up to the current layout IN PLACE, without changing what the game does.

    py -3 tools/migrate_config.py <gameplay_options.toml>

Steps (each one only when the file still needs it):
  1.0.6  adds the [oil_platforms] block after [gold_mines]
  1.1.0  adds "amount = 1.0" to [gold_mines] and [oil_platforms]
  1.2.0  adds the [food] block after [oil_platforms]
  1.3.0  adds the [trees] block after [food]
  1.11.0 adds the [priority] block (the game's own spell order, save_mana = true) after [autocast]
  1.10.0 adds [general] log_ai = false after log_casts
  1.9.0  adds [autocast] area_building_value after area_min_enemies and updates the comment above it
  1.8.0  adds [auto_production.no_enemy_navy_cap]
  1.7.2  [auto_production] workers_per_hall_tier becomes workers_tier1 / 2 / 3 (12 / 16 / 24), and the two
         *_ignore_reserve switches are added
  1.7.0  adds part "11. AUTO-PRODUCTION" ([auto_production] and its sub-tables, switched off) in front of
         "YOUR OWN NUMBERS", which becomes part 12
  1.6.1  the Raise Dead comment says that the spell must be researched
  1.6.0  adds part "10. SPELL POWER AND MANA" ([spell_damage], [spell_cost], [mana], all neutral) in front of
         "YOUR OWN NUMBERS", which becomes part 11
  1.5.0  adds the eight new [spells] switches (false) and three [autocast] keys; the Raise Dead comment is updated
  1.4.0  [heroes] gains the "regen" switch (true when the old regen_hp_per_second was above 0, so nothing changes) and
         the [unit_regen] block is added after [heroes]
  1.0.8  [health] "all" used to mean "every unit". It now means everything (units, ships, structures), and the new
         "units" key is the units-only master. An old value moves from "all" to "units", so units keep their health and
         buildings stay untouched. Same for [health.human] / [health.orc].

Keeps a .bak copy next to the file, keeps the file's line endings, and proves with a TOML parse that every other value
is unchanged.
"""
import pathlib
import re
import shutil
import sys
import time
import tomllib

CRLF, LF = chr(13) + chr(10), chr(10)
OIL_BLOCK = ['[oil_platforms]',
             "# true = oil platforms never run dry (every platform on the map, the computer's too). A platform keeps the amount it",
             '# had when you switched this on, never less than 5000.',
             'unlimited = false']
FOOD_BLOCK = ['[food]', '# true = every Town Hall / Great Hall, Keep / Stronghold and Castle / Fortress gives hall_food_amount food instead', "# of the game's 1. In a custom game that starts with one peasant you can then train from the hall right away,", '# without waiting for a farm. A farm still gives 4, the 200 food limit stays. The computer gets the same.', 'hall_food = false', 'hall_food_amount = 5']
TREES_BLOCK = ['[trees]', "# EXPERIMENTAL. true = felled forest grows back, so lumber never runs out for good (the computer's too). Every stump", '# waits its own time between regrow_min_minutes and regrow_max_minutes of play, so a felled patch fills back in bit', '# by bit. A tree never grows back close to a building, a wall or a ground unit, never where it would close a', '# passage, and only where a forest stood before. Flyers do not hold it up.', 'regrow = false', 'regrow_min_minutes = 10', 'regrow_max_minutes = 20', '# No regrowth within this many tiles of a building or wall / of a ground unit of any player.', 'building_distance = 3', 'unit_distance = 3']
UNIT_REGEN_BLOCK = ['[unit_regen]', '# true = every unit regenerates hit points while you play: land units, flyers and ships, never a structure. A hurt', '# unit is then worth keeping instead of being a waste of food. Heroes follow [heroes] regen while that is on.', 'enabled = false', 'hp_per_second = 1', '# "all" = every unit on the map (the enemy\'s too), "mine" = only your own.', 'regen_for = "all"']
SPELLS_NEW = ['# Area spells hurt YOUR units and buildings too. They are only cast with no friendly unit, building or wall in the', '# blast area, and a Blizzard / Death and Decay the mod started is stopped when a friendly walks in (see [autocast]).', 'fireball = false', 'blizzard = false', 'death_and_decay = false', 'whirlwind = false', 'flame_shield = false', 'runes = false', 'invisibility = false', '# Holy Vision: an idle paladin at full mana looks at the least explored part of the map. It may move the camera to', '# its target for your own paladins (not tested in game yet).', 'holy_vision = false']
PRIORITY_1110 = ['[priority]', '# The order each caster tries its spells in, by [spells] name. These lists are the defaults. A misspelled name, or one', '# that belongs to another caster, is written to the log and ignored; spells you leave out are added at the end, so', '# nothing is switched off by being forgotten. Heroes follow their caster. Eye of Kilrogg has its own rule in', '# [eye_of_kilrogg]. Example: put "death_and_decay" first and a death knight goes for buildings before Death Coil.', 'paladin      = ["heal", "exorcism", "holy_vision"]', 'mage         = ["polymorph", "slow", "fireball", "invisibility", "blizzard", "flame_shield"]', 'ogre_mage    = ["bloodlust", "runes"]', 'death_knight = ["raise_dead", "unholy_armor", "death_coil", "haste", "death_and_decay", "whirlwind"]', '# true = a caster that could cast a spell higher in its list, if only it had the mana, casts NOTHING lower and saves', '# up for it. Without this a death knight spends its mana on Death Coil for ever and never reaches what a Death and', '# Decay channel needs (three waves). A spell with no target never holds anything back.', 'save_mana = true']
LOG_AI_1100 = ['# true = once a minute, write what every computer player is doing to gameplay_options.log: where its script is, what', '# it is waiting for, its resources, food and army numbers, plus a line when it has not moved for 5 minutes. For', '# finding out why a computer stopped attacking. It only reads the game, it changes nothing.', 'log_ai = false']
OLD_AREA_COMMENT = '# Blizzard, Death and Decay and Whirlwind need at least this many enemies close together.'
AREA_190 = ['# Blizzard and Death and Decay are cast on the spot worth the most: one enemy building is enough, without a building', '# it takes at least this many enemy units close together. Whirlwind needs this many enemies of any kind.', 'area_min_enemies = 3', '# An enemy building in the blast counts as this many units when the spot is picked (a building cannot walk away).', '# A channel aimed at buildings stops once the waves already cast cover their hit points: no mana spent on rubble.', 'area_building_value = 3']
AUTOCAST_NEW = ['# Blizzard and Death and Decay keep casting wave after wave. The mod stops the ones it started when a friendly walks', "# in, when no enemy is left, or when the caster's mana drops below this (0 = let them run until the game stops them).", 'channel_mana_reserve = 0', '# Blizzard, Death and Decay and Whirlwind need at least this many enemies close together.', 'area_min_enemies = 3', "# Fireball needs at least this many enemies along its path (1 = the computer's own rule).", 'fireball_min_enemies = 2']
RAISE_DEAD_COMMENT = '# Raise Dead: any fresh corpse within 15 tiles, enemies around or not. It must be researched unless the map grants it.'
RAISE_DEAD_COMMENT_150 = "# Raise Dead works like the computer's: any fresh corpse within 15 tiles, enemies around or not."
OLD_RAISE_DEAD_COMMENT = '# Raise Dead is only cast while an enemy is within search_radius, so skeletons are not wasted.'
NO_ENEMY_NAVY_CAP = ['# While no hostile player owns a shipyard or a warship anywhere on the map (fog does not matter), the mod holds at', '# most this many of each class and builds no further ships; the share that would have gone to ships goes to your', '# land army. Ships you already have count. A class left out of this table has no ceiling.', '[auto_production.no_enemy_navy_cap]', 'tankers = 1', 'destroyers = 5', 'battleships = 2', 'submarines = 2']
AUTO_PRODUCTION = ['[auto_production]', '# true = every IDLE building of yours trains by itself, all of them at once: halls, keeps and castles make workers,', "# barracks, aviaries / roosts, mage towers / temples and shipyards make the army. Single player only. The computer's", '# buildings are never touched, a building you have selected is left alone, and the mod never starts a research or a', '# building upgrade. It never builds transports, flying machines / zeppelins, dwarves / sappers or heroes, and never a', '# second oil tanker: scouting, ferrying and oil runs stay yours.', 'enabled = false', 'toggle_key = "F10"          # Ctrl + this key turns it on and off in game ("" = no hotkey)', 'workers_per_hall_tier = 6   # 6 workers with a town hall, 12 with a keep, 18 with a castle', '# Food that always stays free AFTER the unit being trained: this much, or this share of your supply, whichever is', '# more. That room is yours, for peasants, transports, zeppelins or a tanker of your own.', 'food_free_min = 4', 'food_free_percent = 10', '# Money kept back for upgrades: the dearest one you could buy right now plus this share of all the others. The more', '# you could be researching (towers, keep, castle, weapons, spells), the more it saves.', 'reserve_extra = 0.25', "upgrade_bias = 0.25         # each upgrade level a unit line already has raises that class's share by this much", 'filler_min = 10             # nothing of the mix affordable at a building: build what the spare bank buys this often', '# How much of your army is ships comes from the MAP: the water fraction plus 5% per oil source (at most 30% from', '# oil), a little higher while you only have a town hall. No oil and less than 10% water = a land map, no ships.', 'navy_weight = 1.0           # x that share (0 = never build ships)', 'navy_max = 80               # ships are never more than this much of the army, in percent', '', '[auto_production.units]', '# Switch a whole class off. Names are race-neutral: infantry = footman / grunt, archers = archer, ranger / axethrower,', '# berserker, knights = knight, paladin / ogre, ogre-mage, casters = mage / death knight, flyers = gryphon rider /', '# dragon, siege = ballista / catapult, battleships = battleship / juggernaught, submarines = submarine / turtle.', 'workers = true', 'infantry = true', 'archers = true', 'knights = true', 'casters = true', 'flyers = true', 'siege = true', 'tankers = true', 'destroyers = true', 'battleships = true', 'submarines = true', '', '[auto_production.bank_multiple]', '# Spare money (what is left after the upgrade reserve) that must be there before a unit is trained, as a multiple of', "# that unit's CURRENT price: four grunts' worth of gold before a grunt. Raise a price anywhere and this rises with it.", 'all = 4.0', '# knights = 8.0             # or per class, e.g. be twice as rich before each knight. Submarines always want twice.', '', '# Army mix per hall tier, in percent. Land and ships are split by the map first (see navy_weight above), then these', '# shares are spread over what you can actually train; a class you cannot build yet gives its share to the others.', '# Plenty of gold and little lumber makes the cheap classes grow by themselves.', '[auto_production.land_tier1]', 'infantry = 75', 'archers = 20', 'siege = 5', '', '[auto_production.land_tier2]', 'knights = 60', 'archers = 25', 'infantry = 10', 'siege = 5', '', '[auto_production.land_tier3]', 'knights = 40', 'casters = 25', 'archers = 15', 'flyers = 15', 'siege = 5', 'infantry = 0', '', '[auto_production.navy_tier1]', 'destroyers = 80', 'battleships = 20', '', '[auto_production.navy_tier2]', 'battleships = 60', 'destroyers = 25', 'submarines = 10', '', '[auto_production.navy_tier3]', 'battleships = 60', 'destroyers = 25', 'submarines = 10', '']
SPELL_POWER = ['[spell_damage]', '# Damage of every damage spell, and the healing of Heal, x this: 2.0 = double. The per-spell numbers below replace', "# the game's own first (-1 = the game's), then this multiplier applies. Everyone gets these numbers, the computer too.", 'all = 1.0', 'fireball = -1          # per explosion, 5 explosions per cast (game: 40, at most 254)', 'flame_shield = -1      # per flame hit (game: 4, at most 254)', 'blizzard = -1          # per hit (game: 10, at most 254)', 'death_and_decay = -1   # per hit (game: 10, at most 254)', 'whirlwind = -1         # per hit (game: 4, at most 254)', 'death_coil = -1        # per cast, shared by the enemies hit; the caster heals as much (game: 50, at most 127)', 'runes = -1             # per rune (game: 50, at most 128)', 'heal = -1              # the most hit points one Heal restores (game: 40, at most 255)', '', '[spell_cost]', "# Mana cost of every spell x this, rounded up. The per-spell numbers below replace the game's own first (-1 = the", "# game's), then this multiplier applies. 1 to 255. Heal and Exorcism cost mana PER HIT POINT, and", '# [spell_damage] all lowers that price further (x2 damage = half the price). Everyone gets these costs.', 'all = 1.0', 'holy_vision = -1       # game: 70', 'heal = -1              # game: 5 per hit point healed', 'exorcism = -1          # game: 4 per hit point of damage', 'flame_shield = -1      # game: 80', 'fireball = -1          # game: 100', 'slow = -1              # game: 50', 'invisibility = -1      # game: 200', 'polymorph = -1         # game: 200', 'blizzard = -1          # game: 25 per wave', 'eye_of_kilrogg = -1    # game: 70', 'bloodlust = -1         # game: 60', 'raise_dead = -1        # game: 50', 'death_coil = -1        # game: 100', 'whirlwind = -1         # game: 100', 'haste = -1             # game: 50', 'unholy_armor = -1      # game: 100', 'runes = -1             # game: 200', 'death_and_decay = -1   # game: 30 per wave', '', '[mana]', '# How fast casters regain mana: 2.0 = twice as fast (the game: 1 point every 40 steps). Mana stops at 255, that', '# limit is part of the game. Everyone gets it, the computer too.', 'regen = 1.0']
SPELL_POWER_TITLE = '10. SPELL POWER AND MANA'
AMOUNT = {
    'gold_mines': ['# Multiplies the gold in every mine when a NEW map starts: 3.0 = three times as much, for both sides. "Gold left"', '# shows the real number. A mine holds up to 6,553,500, ten times what the map editor allows, so 10.0 always fits.'],
    'oil_platforms': ['# The same for oil: every oil patch and platform, when a NEW map starts.'],
}


def leaves(tree, prefix=()):
    for k, v in tree.items():
        if isinstance(v, dict):
            yield from leaves(v, prefix + (k,))
        else:
            yield prefix + (k,), v


def span(lines, header):
    """(index of the header line, index of the next table header or banner) or None."""
    for i, line in enumerate(lines):
        if line.strip() == header:
            for j in range(i + 1, len(lines)):
                if re.match(r'\s*\[[^\[\]]+\]\s*(#.*)?$', lines[j]) or lines[j].startswith('# ===='):
                    return i, j
            return i, len(lines)
    return None


def value_line(lines, start, end, key):
    for i in range(start + 1, end):
        if re.match(rf'\s*{re.escape(key)}\s*=', lines[i]):
            return i
    return None


def add_oil(lines, tree, notes):
    if 'oil_platforms' in tree:
        return
    s = span(lines, '[gold_mines]')
    if s is None:
        notes.append('no [gold_mines] section: add [oil_platforms] by hand')
        return
    end = s[1]
    while end > s[0] + 1 and not lines[end - 1].strip():  # keep the blank line(s) in front of the next part
        end -= 1
    lines[end:end] = [''] + OIL_BLOCK
    notes.append('added [oil_platforms] unlimited = false')


def add_amounts(lines, notes):
    """Runs after add_oil, on the text: both sections exist by now."""
    for section, comment in AMOUNT.items():
        s = span(lines, f'[{section}]')
        if s is None or value_line(lines, s[0], s[1], 'amount') is not None:
            continue
        at = value_line(lines, s[0], s[1], 'unlimited')
        at = s[0] + 1 if at is None else at + 1
        lines[at:at] = comment + ['amount = 1.0']
        notes.append(f'added [{section}] amount = 1.0')


def add_food(lines, tree, notes):
    if 'food' in tree:
        return
    s = span(lines, '[oil_platforms]') or span(lines, '[gold_mines]')
    if s is None:
        notes.append('no [gold_mines] / [oil_platforms] section: add [food] by hand')
        return
    end = s[1]
    while end > s[0] + 1 and not lines[end - 1].strip():
        end -= 1
    lines[end:end] = [''] + FOOD_BLOCK
    notes.append('added [food] hall_food = false, hall_food_amount = 5')


def add_trees(lines, tree, notes):
    if 'trees' in tree:
        return
    s = span(lines, '[food]') or span(lines, '[oil_platforms]') or span(lines, '[gold_mines]')
    if s is None:
        notes.append('no [food] / [gold_mines] section: add [trees] by hand')
        return
    end = s[1]
    while end > s[0] + 1 and not lines[end - 1].strip():
        end -= 1
    lines[end:end] = [''] + TREES_BLOCK
    notes.append('added [trees] regrow = false')


def add_regen(lines, tree, notes):
    heroes = tree.get('heroes', {})
    s = span(lines, '[heroes]')
    if s is not None and 'regen' not in heroes:
        was_on = heroes.get('regen_hp_per_second', 0) > 0
        at = value_line(lines, s[0], s[1], 'regen_hp_per_second')
        new = ['# true = heroes regenerate regen_hp_per_second hit points per second of play.', 'regen = ' + ('true' if was_on else 'false')]
        if at is None:
            lines[s[0] + 1:s[0] + 1] = new
        else:
            first = at
            while first > s[0] + 1 and lines[first - 1].startswith('# Hit points every hero regenerates'):
                first -= 1
            lines[first:at] = new
            if not was_on:  # 0 used to be the off switch; the amount now shows what "on" would give
                at = value_line(lines, s[0], span(lines, '[heroes]')[1], 'regen_hp_per_second')
                lines[at] = 'regen_hp_per_second = 2'
        notes.append('[heroes] regen = ' + ('true (it was on)' if was_on else 'false'))
    if 'unit_regen' not in tree and s is not None:
        s = span(lines, '[heroes]')
        end = s[1]
        while end > s[0] + 1 and not lines[end - 1].strip():
            end -= 1
        lines[end:end] = [''] + UNIT_REGEN_BLOCK
        notes.append('added [unit_regen] enabled = false')


def append_missing(lines, tree, section, block, notes):
    """Appends the key lines of `block` (with the comments right above each) that [section] does not have yet."""
    have = tree.get(section, {})
    s = span(lines, f'[{section}]')
    if s is None:
        notes.append(f'no [{section}] section: new keys not added')
        return
    new, pending = [], []
    for line in block:
        if line.startswith('#'):
            pending.append(line)
            continue
        key = line.split('=')[0].strip()
        if key not in have:
            new += pending + [line]
        pending = []
    if not new:
        return
    end = s[1]
    while end > s[0] + 1 and not lines[end - 1].strip():
        end -= 1
    lines[end:end] = new
    notes.append(f'[{section}] gained ' + ', '.join(l.split('=')[0].strip() for l in new if not l.startswith('#')))


def spells_150(lines, tree, notes):
    for i, line in enumerate(lines):
        if line in (OLD_RAISE_DEAD_COMMENT, RAISE_DEAD_COMMENT_150):
            lines[i] = RAISE_DEAD_COMMENT
    append_missing(lines, tree, 'spells', SPELLS_NEW, notes)
    append_missing(lines, tree, 'autocast', AUTOCAST_NEW, notes)


def priority_1110(lines, tree, notes):
    """A config written before 1.11.0: the [priority] block goes after [autocast]."""
    if 'priority' in tree:
        return
    s = span(lines, '[autocast]')
    if s is None:
        lines += [''] + PRIORITY_1110
        notes.append('added [priority] at the end (default spell order, save_mana = true)')
        return
    end = s[1]
    while end > s[0] + 1 and not lines[end - 1].strip():
        end -= 1
    lines[end:end] = [''] + PRIORITY_1110
    notes.append('added [priority] (default spell order, save_mana = true)')


def log_ai_1100(lines, tree, notes):
    """A config written before 1.10.0: log_ai goes right under log_casts."""
    general = tree.get('general', {})
    if not isinstance(general, dict) or 'log_ai' in general:
        return
    s = span(lines, '[general]')
    if s is None:
        return
    for i in range(s[0], s[1]):
        if '=' in lines[i] and lines[i].split('=')[0].strip() == 'log_casts':
            lines[i + 1:i + 1] = LOG_AI_1100
            notes.append('[general] gained log_ai = false')
            return
    append_missing(lines, tree, 'general', LOG_AI_1100, notes)


def area_building_value_190(lines, tree, notes):
    """A config written before 1.9.0: area_building_value goes right under area_min_enemies, whose comment changes."""
    auto = tree.get('autocast', {})
    if not isinstance(auto, dict) or 'area_building_value' in auto:
        return
    s = span(lines, '[autocast]')
    if s is None:
        return
    for i in range(s[0], s[1]):
        if lines[i].split('=')[0].strip() == 'area_min_enemies' and '=' in lines[i]:
            if i > 0 and lines[i - 1] == OLD_AREA_COMMENT:
                lines[i - 1:i] = AREA_190[:2]
                i += 1
            lines[i + 1:i + 1] = AREA_190[3:]
            notes.append('[autocast] gained area_building_value = 3')
            return
    append_missing(lines, tree, 'autocast', AREA_190[3:], notes)


def spell_power_160(lines, tree, notes):
    if any(k in tree for k in ('spell_damage', 'spell_cost', 'mana')):
        return
    bar = '# ' + '=' * 110
    block = [bar, '#  ' + SPELL_POWER_TITLE, bar, ''] + SPELL_POWER + ['']
    for i, line in enumerate(lines):
        if line.startswith('#  10. YOUR OWN NUMBERS'):
            lines[i] = line.replace('#  10.', '#  11.', 1)
            at = i - 1 if i > 0 and lines[i - 1].startswith('# ====') else i
            lines[at:at] = block
            notes.append('added [spell_damage], [spell_cost] and [mana] (all neutral)')
            return
    lines += [''] + block  # a file without the banner: at the end
    notes.append('added [spell_damage], [spell_cost] and [mana] at the end (all neutral)')


def _auto_production_keys():
    """Every (section..., key) tuple the [auto_production] block adds, sub-tables included."""
    keys, section = set(), ()
    for line in AUTO_PRODUCTION:
        t = line.strip()
        if t.startswith('[') and t.endswith(']'):
            section = tuple(t[1:-1].split('.'))
        elif t and not t.startswith('#') and '=' in t:
            keys.add(section + (t.split('=')[0].strip(),))
    return keys


AUTO_PRODUCTION_KEYS = _auto_production_keys()


def auto_production_180(lines, tree, notes):
    """A config written before 1.8.0: add the cap table after the auto-production block."""
    prod = tree.get('auto_production', {})
    if not isinstance(prod, dict) or 'no_enemy_navy_cap' in prod or not prod:
        return
    anchor = span(lines, '[auto_production.navy_tier1]') or span(lines, '[auto_production.land_tier1]')
    at = anchor[0] if anchor else None
    if at is None:
        s = span(lines, '[auto_production]')
        if s is None:
            return
        at = s[1]
    while at > 0 and not lines[at - 1].strip():
        at -= 1
    lines[at:at] = [''] + NO_ENEMY_NAVY_CAP
    notes.append('added [auto_production.no_enemy_navy_cap] (1 tanker, 5 destroyers, 2 battleships, 2 submarines)')


def auto_production_172(lines, tree, notes):
    """A config written by 1.7.0 / 1.7.1: replace the old worker key, add the two switches."""
    prod = tree.get('auto_production', {})
    if not isinstance(prod, dict) or 'workers_tier1' in prod:
        return
    s = span(lines, '[auto_production]')
    if s is None:
        return
    old = value_line(lines, s[0], s[1], 'workers_per_hall_tier')
    per = prod.get('workers_per_hall_tier', 6)
    new = ['workers_tier1 = ' + str(per * 2), 'workers_tier2 = ' + str(per * 2 + 4), 'workers_tier3 = ' + str(per * 4)] \
        if per != 6 else ['workers_tier1 = 12', 'workers_tier2 = 16', 'workers_tier3 = 24']
    if old is None:
        lines[s[0] + 1:s[0] + 1] = new
    else:
        first = old
        while first > s[0] + 1 and lines[first - 1].startswith('#'):
            first -= 1
        lines[first:old + 1] = ['# Workers wanted, by your best hall tier (0 to 200, 0 = never train workers).'] + new
    append_missing(lines, tree, 'auto_production',
                   ['# Workers and the single tanker wait for their own price only, never for the upgrade reserve.',
                    'workers_ignore_reserve = true', 'tankers_ignore_reserve = true'], notes)
    notes.append('[auto_production] workers_tier1/2/3 replace workers_per_hall_tier')


def auto_production_170(lines, tree, notes):
    if 'auto_production' in tree:
        return
    bar = '# ' + '=' * 110
    block = [bar, '#  11. AUTO-PRODUCTION', bar, ''] + [l for l in AUTO_PRODUCTION if l is not None]
    for i, line in enumerate(lines):
        if line.startswith('#  11. YOUR OWN NUMBERS'):
            lines[i] = line.replace('#  11.', '#  12.', 1)
            at = i - 1 if i > 0 and lines[i - 1].startswith('# ====') else i
            lines[at:at] = block
            notes.append('added [auto_production] (switched off)')
            return
    lines += [''] + block
    notes.append('added [auto_production] at the end (switched off)')


def health_units(lines, tree, notes):
    health = tree.get('health', {})
    for table, who in (('health', None), ('health.human', 'human'), ('health.orc', 'orc')):
        node = health if who is None else health.get(who, {})
        if not isinstance(node, dict) or 'units' in node:
            continue
        s = span(lines, f'[{table}]')
        if s is None:
            continue
        i = value_line(lines, s[0], s[1], 'all')
        old = node.get('all', 1.0)
        text = repr(float(old))
        if who is None:
            new = ['# Master for EVERYTHING: every unit, ship and structure of every race.', 'all = 1.0',
                   '# Masters per kind (both races).', f'units = {text}'.ljust(25) + '# every unit and ship, never a structure']
            if i is None:
                lines[s[0] + 1:s[0] + 1] = new
            else:
                first = i - 1 if i > s[0] + 1 and lines[i - 1].startswith('# UNITS master') else i
                lines[first:i + 1] = new
                nxt = first + len(new)
                if nxt < len(lines) and lines[nxt].startswith('# STRUCTURES master'):
                    del lines[nxt]
        else:
            new = [f'# everything {who}: units, ships and structures', 'all = 1.0', '', f'# --- {who.upper()} UNITS ---',
                   f'# every {who} unit and ship', f'units = {text}']
            if i is None:
                lines[s[0] + 1:s[0] + 1] = new
            else:
                first = i
                while first > s[0] + 1 and lines[first - 1].startswith('#'):  # the old "--- UNITS ---" / "every unit" comments
                    first -= 1
                lines[first:i + 1] = new
        if old != 1.0:
            notes.append(f'[{table}] all = {text} now lives in "units" (same effect as before: units only)')
        else:
            notes.append(f'[{table}] gained "units"')
    for k, line in enumerate(lines):
        if line.startswith('#  Units and structures have SEPARATE masters here'):
            lines[k] = '#  Same layout as PRICES and TIME: "all" is everything, then one master per kind.'


def main():
    path = pathlib.Path(sys.argv[1])
    raw = path.read_bytes().decode('utf-8')
    before = tomllib.loads(raw)
    eol = CRLF if CRLF in raw else LF
    lines = raw.replace(CRLF, LF).split(LF)
    notes = []
    add_oil(lines, before, notes)
    add_amounts(lines, notes)
    add_food(lines, before, notes)
    add_trees(lines, before, notes)
    add_regen(lines, before, notes)
    spells_150(lines, before, notes)
    area_building_value_190(lines, before, notes)
    log_ai_1100(lines, before, notes)
    priority_1110(lines, before, notes)
    spell_power_160(lines, before, notes)
    auto_production_170(lines, before, notes)
    auto_production_172(lines, before, notes)
    auto_production_180(lines, before, notes)
    health_units(lines, before, notes)
    text = LF.join(lines)
    text = text.replace('#  5. HEROES' + LF, '#  5. HEROES AND REGENERATION' + LF, 1)
    text = text.replace('11. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING' + LF,
                        '11. AUTO-PRODUCTION  12. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING' + LF, 1)
    text = text.replace('9. RANGE UPGRADE   10. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING' + LF,
                        '9. RANGE UPGRADE   10. SPELL POWER AND MANA' + LF + '#             11. YOUR OWN NUMBERS FOR ONE UNIT OR ONE BUILDING' + LF, 1)
    for old_title in ('#  4. GOLD MINES' + LF, '#  4. GOLD MINES AND OIL' + LF, '#  4. GOLD, OIL AND FOOD' + LF):
        text = text.replace(old_title, '#  4. RESOURCES' + LF + '#  Gold mines, oil, food and trees.' + LF, 1)
    for old_contents in ('4. GOLD MINES          5. HEROES', '4. GOLD MINES AND OIL  5. HEROES', '4. GOLD, OIL AND FOOD  5. HEROES',
                         '4. RESOURCES           5. HEROES'):
        text = text.replace(old_contents + LF, '4. RESOURCES           5. HEROES AND REGENERATION' + LF, 1)
    if text == raw.replace(CRLF, LF):
        print('already up to date, nothing to do')
        return 0

    after = tomllib.loads(text)
    old, new = dict(leaves(before)), dict(leaves(after))
    allowed = {('oil_platforms', 'unlimited'), ('oil_platforms', 'amount'), ('gold_mines', 'amount'), ('food', 'hall_food'),
               ('food', 'hall_food_amount'), ('heroes', 'regen'), ('unit_regen', 'enabled'), ('unit_regen', 'hp_per_second'),
               ('unit_regen', 'regen_for')} | {('spells', l.split('=')[0].strip()) for l in SPELLS_NEW if not l.startswith('#')} | {
               ('autocast', l.split('=')[0].strip()) for l in AUTOCAST_NEW if not l.startswith('#')} | {('autocast', 'area_building_value'), ('general', 'log_ai')} | {('priority', k) for k in ('paladin', 'mage', 'ogre_mage', 'death_knight', 'save_mana')} | {
               (sec, l.split('=')[0].strip()) for sec in ('spell_damage', 'spell_cost', 'mana') for l in SPELL_POWER
               if l and not l.startswith(('#', '['))} | {
               k for k in AUTO_PRODUCTION_KEYS | {('auto_production', 'no_enemy_navy_cap', c) for c in
               ('tankers', 'destroyers', 'battleships', 'submarines')} | {('auto_production', k) for k in ('workers_tier1', 'workers_tier2',
               'workers_tier3', 'workers_per_hall_tier', 'workers_ignore_reserve', 'tankers_ignore_reserve')}} | {('trees', k) for k in ('regrow', 'regrow_min_minutes', 'regrow_max_minutes',
               'building_distance', 'unit_distance')} | {t + (k,) for t in (('health',), ('health', 'human'), ('health', 'orc')) for k in ('all', 'units')}
    hero_amount = ('heroes', 'regen_hp_per_second')
    if old.get(hero_amount) == 0 and new.get(('heroes', 'regen')) is False:
        allowed = allowed | {hero_amount}
    lost = [k for k, v in old.items() if k not in allowed and new.get(k, KeyError) != v]
    extra = [k for k in new if k not in old and k not in allowed]
    # the health move must keep the product of the two keys for units
    for t in (('health',), ('health', 'human'), ('health', 'orc')):
        was = old.get(t + ('all',), 1.0) * old.get(t + ('units',), 1.0)
        now = new.get(t + ('all',), 1.0) * new.get(t + ('units',), 1.0)
        if abs(was - now) > 1e-9:
            lost.append(t + ('all x units',))
    if lost or extra:
        print('refusing to write: changed', lost, 'unexpected new keys', extra)
        return 1
    backup = path.with_name(path.name + '.bak.pre-migrate-' + time.strftime('%Y%m%d-%H%M%S'))
    shutil.copyfile(path, backup)
    path.write_bytes(text.replace(LF, eol).encode('utf-8'))
    for n in notes:
        print(n)
    print(f'{len(old)} existing values checked; backup {backup.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
