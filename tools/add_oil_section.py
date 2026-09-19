"""One-off for configs written before 1.0.6: adds the [oil_platforms] block after [gold_mines], touching nothing else.

    py -3 tools/add_oil_section.py <gameplay_options.toml>

Keeps a .bak copy next to the file, keeps the file's line endings, and proves with a TOML parse that every value the
file had is still there unchanged.
"""
import pathlib
import shutil
import sys
import time
import tomllib

CRLF, LF = chr(13) + chr(10), chr(10)
BLOCK = ['[oil_platforms]',
         "# true = oil platforms never run dry (every platform on the map, the computer's too). A platform keeps the amount it",
         '# had when you switched this on, never less than 5000.',
         'unlimited = false',
         '']


def leaves(tree, prefix=()):
    for k, v in tree.items():
        if isinstance(v, dict):
            yield from leaves(v, prefix + (k,))
        else:
            yield prefix + (k,), v


def main():
    path = pathlib.Path(sys.argv[1])
    raw = path.read_bytes().decode('utf-8')
    before = tomllib.loads(raw)
    if 'oil_platforms' in before:
        print('already has [oil_platforms], nothing to do')
        return 0
    eol = CRLF if CRLF in raw else LF
    lines = raw.replace(CRLF, LF).split(LF)
    try:
        start = next(i for i, line in enumerate(lines) if line.strip() == '[gold_mines]')
    except StopIteration:
        print('no [gold_mines] section found: add [oil_platforms] by hand')
        return 1
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if lines[i].startswith('[') or lines[i].startswith('# ===='):
            end = i
            break
    while end > start + 1 and not lines[end - 1].strip():  # keep the blank line(s) in front of the next part
        end -= 1
    lines[end:end] = [''] + BLOCK[:-1]
    text = LF.join(lines)
    text = text.replace('#  4. GOLD MINES' + LF, '#  4. GOLD MINES AND OIL' + LF, 1)
    text = text.replace('4. GOLD MINES          5. HEROES', '4. GOLD MINES AND OIL  5. HEROES', 1)

    after = tomllib.loads(text)
    old, new = dict(leaves(before)), dict(leaves(after))
    lost = [k for k, v in old.items() if new.get(k, KeyError) != v]
    extra = [k for k in new if k not in old]
    if lost or extra != [('oil_platforms', 'unlimited')]:
        print('refusing to write: lost', lost, 'extra', extra)
        return 1
    backup = path.with_name(path.name + '.bak.pre-oil-' + time.strftime('%Y%m%d-%H%M%S'))
    shutil.copyfile(path, backup)
    path.write_bytes(text.replace(LF, eol).encode('utf-8'))
    print(f'added [oil_platforms] unlimited = false; {len(old)} existing values unchanged; backup {backup.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
