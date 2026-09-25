"""Writes "+N % at max upgrades" comments beside the auto-production mix weights of a gameplay_options.toml, computed
from that file's own upgrade_bias and [auto_production.class_upgrade_bias]. Run it again after changing either.

    py -3 tools/annotate_upgrades.py <gameplay_options.toml>

Only comments change (proved with a TOML parse); a .bak copy is kept next to the file. The level counts are the ones
src/production.cpp (Levels) adds up; a class with no upgrades of its own (flyers) gets no comment.
"""
import pathlib
import re
import shutil
import sys
import time
import tomllib

# Most levels Levels() can count per class (src/production.cpp): weapons 2 + armor 2 for infantry, + paladin / ogre-mage
# 1 for knights, arrows 2 + ranger 1 + longbow 1 + scouting 1 + marksmanship 1 for archers, catapult 2 for siege,
# cannons 2 + ship armor 2 for every ship, 5 spells for casters.
MAX_LEVELS = {'infantry': 4, 'knights': 5, 'archers': 6, 'siege': 2, 'casters': 5,
              'destroyers': 4, 'battleships': 4, 'submarines': 4}
TABLES = ('land_tier1', 'land_tier2', 'land_tier3', 'navy_tier1', 'navy_tier2', 'navy_tier3')
MARK = '# +'


def main():
    path = pathlib.Path(sys.argv[1])
    raw = path.read_bytes().decode('utf-8')
    before = tomllib.loads(raw)
    prod = before.get('auto_production', {})
    bias = float(prod.get('upgrade_bias', 0.25))
    per_class = prod.get('class_upgrade_bias', {}) or {}
    crlf = '\r\n' in raw
    lines = raw.replace('\r\n', '\n').split('\n')
    section = None
    changed = 0
    for i, line in enumerate(lines):
        m = re.match(r'\s*\[auto_production\.([a-z0-9_]+)\]', line)
        if m:
            section = m.group(1)
            continue
        if line.startswith('['):
            section = None
            continue
        if section not in TABLES:
            continue
        m = re.match(r'^(\s*([a-z_]+)\s*=\s*[0-9.]+)\s*(#.*)?$', line)
        if not m:
            continue
        cls = m.group(2)
        if cls not in MAX_LEVELS:
            continue
        weight = tomllib.loads(m.group(1).strip())[cls]
        b = float(per_class.get(cls, bias))
        levels = MAX_LEVELS[cls]
        factor = 1.0 + b * levels
        note = f'{MARK}{round((factor - 1) * 100)} % at max upgrades ({levels} levels x {b:g}: weight {weight:g} -> {weight * factor:g})'
        old_comment = m.group(3) or ''
        # keep a comment of the player's own, replace ours (and the "(+ at max upgrades)" placeholder)
        keep = '' if (not old_comment or old_comment.startswith(MARK) or 'at max upgrades' in old_comment) else '  ' + old_comment
        new = f'{m.group(1).ljust(15)} {note}{keep}'
        if new != line:
            lines[i] = new
            changed += 1
    text = '\n'.join(lines)
    if tomllib.loads(text) != before:
        raise SystemExit('refusing: a value would change')
    if not changed:
        print('already annotated, nothing to do')
        return
    shutil.copyfile(path, path.with_name(path.name + '.bak.pre-annotate-' + time.strftime('%Y%m%d-%H%M%S')))
    path.write_bytes((text.replace('\n', '\r\n') if crlf else text).encode('utf-8'))
    print(f'{changed} weight line(s) annotated (upgrade_bias {bias:g}, per class {dict(per_class)})')


if __name__ == '__main__':
    main()
