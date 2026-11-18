# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Write the extension author's reference of a panel's kinds from the record that holds them.

`src/AfterNightTests/Test_ControlsExtensionSeam/data/PanelKinds.txt` is the record of every kind a description of an
extension's panel may hold, each field of it and the bounds a description is held to, compared with the product's own
table (`panelKinds()` in `controls/ExtensionPanelDescription.h`) by `PanelDescription.KindsMatchTheRecord`. The
reference an author reads, `docs/dev/EXTENSION_PANEL_KINDS.md`, is generated from that record and never written by
hand, so the page cannot say a kind or a field the product does not have, nor leave one out.

Usage:
    python build/scripts/tools/panel_kinds_reference.py [--write | --check] [--repo-root <dir>]

`--check`, the default, exits 1 when the page is not what the record generates; `--write` writes it.

Tests: build/scripts/tools/panel_kinds_reference_test.py
"""

import argparse
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
RECORD = 'src/AfterNightTests/Test_ControlsExtensionSeam/data/PanelKinds.txt'
REFERENCE = 'docs/dev/EXTENSION_PANEL_KINDS.md'

INTRODUCTION = """\
An extension describes its controls as data, and AfterNight draws them. A process window's controls are the list its
`get_params()` answers; a panel sent through the protocol, and the settings section an extension's
`get_settings_params()` list is drawn as, are a tree of widgets. Both are read into one description: a tree of nodes,
each a kind with the fields it states and, for a group, what it holds. This page lists every kind, each field, what the
field holds, what its absence means and how long it may be. A description is refused whole at the first thing it
breaks, and the reason is said: a kind or a field this page does not list, a value of the wrong type, a text or a list
past its bound, a range whose least is above its most, a choice with no entries. Nothing of a refused one is drawn."""

LIST_INTRODUCTION = """\
The entries of the list, in order. A `meta` entry's `panel_variant` (`native`, or `custom` for the custom look) is the
panel's; the rest of `meta` is the window's own and is not listed here. `section` opens a section in the page or at the
top, and `tabs` a set of tabs whose `tab` entries are its pages, until `end_tabs`. A parameter, an action, a readout or
a hosted control with no section open before it opens one, titled Parameters (Creative Controls in the custom look). An
entry with no id, or of a type not listed here, is passed over, as the window has always passed over it."""

TREE_INTRODUCTION = """\
A tree's root is a node with its kind under `widget` and what it holds under `children`. A group holds nodes of the tree
alone."""

TABLE_HEAD = ('| Field | Holds | When absent | Bound | What it does |\n'
              '|---|---|---|---|---|')


class RecordError(Exception):
    """The record cannot be read as a record of kinds."""


def unescape_fields(entry):
    """The fields of an entry as the record writes them: ` | ` between them, `-` for an empty one, escapes undone."""
    fields = []
    field = []
    escaped = False
    text = entry.strip()
    index = 0
    while index < len(text):
        character = text[index]
        if escaped:
            escaped = False
            field.append({'t': '\t', 'n': '\n', 'r': '\r'}.get(character, character))
        elif character == '\\':
            escaped = True
        elif text.startswith(' | ', index):
            value = ''.join(field)
            fields.append('' if value == '-' else value)
            field = []
            index += 3
            continue
        else:
            field.append(character)
        index += 1
    value = ''.join(field)
    fields.append('' if value == '-' else value)
    return fields


def read_record(text):
    """The record's sections, each a list of (depth, fields)."""
    sections = {}
    current = None
    for raw in text.replace('\r\n', '\n').split('\n'):
        line = raw.rstrip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1]
            sections[current] = []
            continue
        if current is None:
            raise RecordError('an entry stands before the first section: ' + line)
        depth = (len(line) - len(line.lstrip(' '))) // 4
        sections[current].append((depth, unescape_fields(line)))
    return sections


def cell(text):
    """A table cell: a bar escaped, an empty one a dash."""
    return text.replace('|', '\\|') if text else '-'


def kinds_of(entries, section):
    """The kinds of a section: each (fields, [field fields])."""
    kinds = []
    for depth, fields in entries:
        if depth == 0:
            if len(fields) != 5:
                raise RecordError('a kind of [%s] is not `kind | role | holds | aliases | meaning`: %r' % (section, fields))
            kinds.append((fields, []))
        elif depth == 1:
            if not kinds:
                raise RecordError('a field of [%s] stands before its kind' % section)
            if len(fields) != 5:
                raise RecordError('a field of [%s] is not `field | holds | absent | bound | meaning`: %r' %
                                  (section, fields))
            kinds[-1][1].append(fields)
        else:
            raise RecordError('an entry of [%s] is nested deeper than a field' % section)
    return kinds


def render_kinds(kinds):
    lines = []
    for (name, role, holds, aliases, meaning), fields in kinds:
        heading = '### `%s`' % name
        if aliases:
            heading += ' (also %s)' % ', '.join('`%s`' % alias.strip() for alias in aliases.split(','))
        lines += [heading, '']
        about = meaning + ' Role: ' + role + ('; it holds other nodes.' if holds else '.')
        lines += [about, '', TABLE_HEAD]
        for field_name, holds_type, absent, bound, field_meaning in fields:
            lines.append('| `%s` | %s | %s | %s | %s |' % (field_name, cell(holds_type), cell(absent), cell(bound),
                                                           cell(field_meaning)))
        lines.append('')
    return lines


def render(sections):
    """The reference's text, from the record's sections."""
    for name in ('limits', 'parameters', 'widgets'):
        if name not in sections:
            raise RecordError('the record has no [%s] section' % name)
    lines = ['# The kinds of an extension\'s panel', '',
             '<!-- Generated from %s by build/scripts/tools/panel_kinds_reference.py: edit the record (and the kinds it '
             'records), then write this page again with --write. -->' % RECORD, '',
             INTRODUCTION, '', '## Bounds', '', '| Bound | Value | What it bounds |', '|---|---|---|']
    for depth, fields in sections['limits']:
        if depth != 0 or len(fields) != 3:
            raise RecordError('a bound is not `name | value | meaning`: %r' % (fields,))
        lines.append('| %s | %s | %s |' % tuple(cell(field) for field in fields))
    lines += ['', '## A process window\'s list of parameters', '', LIST_INTRODUCTION, '']
    lines += render_kinds(kinds_of(sections['parameters'], 'parameters'))
    lines += ['## A tree of widgets', '', TREE_INTRODUCTION, '']
    lines += render_kinds(kinds_of(sections['widgets'], 'widgets'))
    while lines and lines[-1] == '':
        lines.pop()
    # Every line ends in CRLF, the lines of the texts above included, as every document of the tree does.
    return '\n'.join(lines).replace('\r\n', '\n').replace('\n', '\r\n') + '\r\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true', help='write the reference from the record')
    mode.add_argument('--check', action='store_true', help='fail when the reference is not what the record generates')
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    arguments = parser.parse_args(argv)
    root = pathlib.Path(arguments.repo_root)
    try:
        text = render(read_record((root / RECORD).read_text(encoding='utf-8')))
    except (OSError, RecordError) as error:
        print('panel kinds: %s' % error, file=sys.stderr)
        return 2
    reference = root / REFERENCE
    if arguments.write:
        reference.write_bytes(text.encode('utf-8'))
        print('wrote %s' % REFERENCE)
        return 0
    current = reference.read_bytes().decode('utf-8') if reference.is_file() else None
    if current != text:
        print('%s is not what %s generates: run panel_kinds_reference.py --write' % (REFERENCE, RECORD),
              file=sys.stderr)
        return 1
    print('%s is what the record generates' % REFERENCE)
    return 0


if __name__ == '__main__':
    sys.exit(main())
