"""Brings an older gameplay_options.toml up to the current layout IN PLACE, without changing what the game does.

    py -3 tools/migrate_config.py <gameplay_options.toml>

Steps (each one only when the file still needs it):
  1.0.6  adds the [oil_platforms] block after [gold_mines]
  1.1.0  adds "amount = 1.0" to [gold_mines] and [oil_platforms]
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
    health_units(lines, before, notes)
    text = LF.join(lines)
    text = text.replace('#  4. GOLD MINES' + LF, '#  4. GOLD MINES AND OIL' + LF, 1)
    text = text.replace('4. GOLD MINES          5. HEROES', '4. GOLD MINES AND OIL  5. HEROES', 1)
    if text == raw.replace(CRLF, LF):
        print('already up to date, nothing to do')
        return 0

    after = tomllib.loads(text)
    old, new = dict(leaves(before)), dict(leaves(after))
    allowed = {('oil_platforms', 'unlimited'), ('oil_platforms', 'amount'), ('gold_mines', 'amount')} | {t + (k,) for t in (('health',), ('health', 'human'), ('health', 'orc')) for k in ('all', 'units')}
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
