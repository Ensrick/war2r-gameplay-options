"""Carry a player's settings over to a newer default gameplay_options.toml.

    py -3 tools/port_config.py <players file> <new default> <output> [--old-default <file>]

The new default's text (comments, layout, new settings) is kept; every value the player had changed is written into
it. [unit.*] / [building.*] tables the player added are appended. With --old-default, unit / building tables that the
player deleted from the older default are commented out in the output instead of silently coming back.
Settings that no longer exist are reported, never guessed at. The result is parsed again and compared value by value.
"""
import argparse
import re
import sys
import tomllib

CRLF, LF = chr(13) + chr(10), chr(10)
OPEN_SECTIONS = ('unit', 'building')  # tables the player may add freely


def leaves(tree, prefix=()):
    for key, value in tree.items():
        if isinstance(value, dict):
            yield from leaves(value, prefix + (key,))
        else:
            yield prefix + (key,), value


def lookup(tree, path):
    for key in path:
        if not isinstance(tree, dict) or key not in tree:
            return KeyError
        tree = tree[key]
    return tree


def fmt(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, float):
        text = repr(value)
        return text if ('.' in text or 'e' in text) else text + '.0'
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
    if isinstance(value, list):
        if not value:
            return '[]'
        rows, row = [], '   '
        for item in value:
            piece = ' ' + fmt(item) + ','
            if len(row) + len(piece) > 110:
                rows.append(row)
                row = '   '
            row += piece
        rows.append(row)
        return '[' + LF + LF.join(rows) + LF + ']'
    raise TypeError(f'cannot write {value!r}')


def section_span(lines, section):
    """(first line after the header, line index of the next header) of [section], or None."""
    header = '[' + '.'.join(section) + ']'
    for i, line in enumerate(lines):
        if line.strip() == header:
            end = len(lines)
            for j in range(i + 1, len(lines)):
                if re.match(r'\s*\[[^\[\]]+\]\s*(#.*)?$', lines[j]):
                    end = j
                    break
            return i + 1, end
    return None


def set_value(lines, path, value):
    span = section_span(lines, path[:-1])
    if span is None:
        return False
    key = re.escape(path[-1])
    for i in range(*span):
        m = re.match(rf'(\s*){key}\s*=\s*(.*)$', lines[i])
        if not m:
            continue
        end = i
        if m.group(2).lstrip().startswith('[') and ']' not in m.group(2):  # multi-line array
            while ']' not in lines[end].split('#', 1)[0] or end == i:
                end += 1
        comment = ''
        if end == i:
            cm = re.search(r'\s+#.*$', m.group(2))
            comment = cm.group(0) if cm else ''
        lines[i:end + 1] = (m.group(1) + path[-1] + ' = ' + fmt(value) + comment).split(LF)
        return True
    return False


def comment_out_table(lines, section):
    span = section_span(lines, section)
    if span is None:
        return False
    for i in range(span[0] - 1, span[1]):
        if lines[i].strip() and not lines[i].lstrip().startswith('#'):
            lines[i] = '# ' + lines[i]
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('player')
    ap.add_argument('new_default')
    ap.add_argument('output')
    ap.add_argument('--old-default')
    args = ap.parse_args()

    player = tomllib.load(open(args.player, 'rb'))
    default_text = open(args.new_default, 'rb').read().decode('utf-8').replace(CRLF, LF)
    default = tomllib.loads(default_text)
    lines = default_text.split(LF)

    carried, appended, dropped = [], {}, []
    for path, value in leaves(player):
        current = lookup(default, path)
        if current is KeyError:
            if path[0] in OPEN_SECTIONS and len(path) == 3:
                if lookup(default, path[:2]) is KeyError:
                    appended.setdefault(path[:2], []).append((path[2], value))
                    continue
                span = section_span(lines, path[:2])  # a new key inside a table the default already has
                lines.insert(span[1] if not lines[span[1] - 1].strip() == '' else span[1] - 1, path[2] + ' = ' + fmt(value))
                carried.append((path, value))
                continue
            dropped.append((path, value))
        elif current != value:
            if set_value(lines, path, value):
                carried.append((path, value))
            else:
                dropped.append((path, value))

    removed = []
    if args.old_default:
        old = tomllib.load(open(args.old_default, 'rb'))
        for section in OPEN_SECTIONS:
            for name in old.get(section, {}):
                if name not in player.get(section, {}) and name in default.get(section, {}):
                    if comment_out_table(lines, (section, name)):
                        removed.append((section, name))

    if appended:
        while lines and not lines[-1].strip():
            lines.pop()
        lines += ['', '# --- YOUR TABLES (carried over from your previous file) ---']
        for table, items in appended.items():
            lines += ['[' + '.'.join(table) + ']'] + [f'{k} = {fmt(v)}' for k, v in items] + ['']

    text = LF.join(lines).rstrip(LF) + LF
    merged = tomllib.loads(text)  # must still be valid TOML
    problems = [p for p, v in leaves(player) if (p, v) not in dropped and lookup(merged, p) != v]
    open(args.output, 'wb').write(text.replace(LF, CRLF).encode('utf-8'))

    for path, value in carried:
        print(f'kept     {".".join(path)} = {fmt(value).replace(LF, " ")[:90]}')
    for table, items in appended.items():
        print(f'added    [{".".join(table)}] ' + ', '.join(f'{k} = {fmt(v)}' for k, v in items))
    for section, name in removed:
        print(f'removed  [{section}.{name}] (you had deleted it): commented out')
    for path, value in dropped:
        print(f'DROPPED  {".".join(path)} = {fmt(value)[:60]}  (this setting does not exist any more)')
    if problems:
        print('MISMATCH after merge:', ['.'.join(p) for p in problems])
        return 1
    print(f'ok: {len(carried)} values kept, {len(appended)} tables added, {len(dropped)} dropped')
    return 0


if __name__ == '__main__':
    sys.exit(main())
