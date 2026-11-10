#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Draft the `controls` blocks of a process page from its window's inventory and its old parameter table.

A process page of the user guide describes its window's controls in `controls` blocks, and the guide's check holds
those blocks to the inventory of the controls the window really opens with (docs/user-guide/data/ui/, rule R14 of
check_user_guide.py). A page that is rewritten therefore starts from two things it must not lose: the window's own
controls - their labels, kinds, defaults, numbers and choices, in window order - and every fact the page's old
parameter table states about them. This drafts both into one block per window, for a writer to edit into the page:

  - Every control of the inventory, in window order, with what the inventory fixes. A control the window shares with
    every other - the histogram toolbar of a real-time preview, the Apply, Reset and Close buttons - is left out, as
    a page leaves it out; --shared keeps them.
  - `object`, the control's object name, where the window's label for it is not unique or is no caption at all, so
    the check can tell which control an entry means. A control keeps the window's label as its name; one the window
    gives no caption is listed under its object name, for the writer to name.
  - A placeholder wherever the inventory records that a value follows the image the window is opened on, or the
    machine: the window does not fix it, so the writer says what it opens with.
  - Each row of the old table on the control its "UI label" names, its cells mapped to the block's facets by the
    header of their column, whatever the columns' order: "Runtime default" is compared with the inventory, "Valid
    values" is `range`, "Effect" is `effect`, "Dependencies" and "Enabled when" are `enabled`, the trade-off column is
    `tradeoff`, "Scope" is `scope`, the columns about persistence are `persistence`, and "Type and unit" and any
    other column are `note`. Several columns that land in one facet are each written under their header.
  - Every row that names no drafted control, listed after the blocks with all its cells: the rows of the shared
    controls, which belong to the Process windows page, and the rows of plots, gestures and behaviors, whose facts
    go into the page's prose.

Nothing of the old table is dropped but a cell that is empty or a dash, and a default the inventory already states.
`TODO` and `(follows ...)` mark what the draft cannot know. The draft is a start: its names, kinds, defaults, numbers
and choices are the window's and stay; its sentences are the old table's and are rewritten.

The tool serves the rewrite of the guide's process pages. Once every page is rewritten there is no old table left to
draft from, and it is removed.

Usage, with the documentation toolchain's Python, which has PyYAML (docs/user-guide/.venv, made by the build helper):
    python build/scripts/tools/draft_guide_controls.py <page> [--guide docs/user-guide] [--repo <root>] [--shared]

The page is named relative to the docs folder or to the working directory. The draft is printed as Markdown. Exits 0,
and 2 when the page is unknown, is not a process page or has no window in the inventory: its `menu` reaches none, or
only windows another page names more exactly, as the check holds each window to one page.

Tests: build/scripts/tools/draft_guide_controls_test.py
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_user_guide as check  # noqa: E402 - beside this script

REPO_ROOT = check.REPO_ROOT

# The facets of a control a block states after what the inventory fixes, in the order a block lists them.
FACETS = ('range', 'effect', 'enabled', 'tradeoff', 'scope', 'persistence', 'note')
# What a control without a row of the old table says it does.
EFFECT_TODO = 'TODO: say what it does.'
# What stands for a choice list that is empty when the window opens.
NO_CHOICE = '(none listed when the window opens)'
# A window with more own controls than this, under several titles, gets a comment where each group starts.
GROUP_COMMENTS_FROM = 25
# A cell that states nothing.
NOTHING = ('', '-', '‒', '–', '—', '―')
# The words an old table says a check box opens with, as the inventory's `On` and `Off`.
CHECKED = frozenset({'on', 'checked', 'selected', 'ticked', 'enabled', 'true', 'yes'})
UNCHECKED = frozenset({'off', 'unchecked', 'cleared', 'clear', 'unselected', 'not selected', 'disabled', 'false', 'no'})
RANGED_KINDS = ('spinbox', 'slider')
CHOICE_KINDS = ('combo', 'radio')

_TABLE_RULE = re.compile(r'^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$')
_CODE_SPAN = re.compile(r'(`+)(.+?)\1')
_MARKUP = re.compile(r'\*\*|__|`')
_NOT_A_WORD = re.compile(r'[\W_]+')
_PLAIN = re.compile(r'^[^\W_](?:[^\x00-\x1f\x7f-\x9f  ﻿]*[^\s\x00-\x1f\x7f-\x9f  ﻿:])?$')
_PLAIN_IN_FLOW = re.compile(r'^[A-Za-z0-9](?:[A-Za-z0-9 ._+/()%*<>=-]*[A-Za-z0-9._+/()%*<>=-])?$')
_UNPRINTABLE = re.compile('[\x7f-\x9f  ﻿]')


def facet_of(header):
    """The facet a column of the old table lands in, by its header: `name`, `default` or one of FACETS."""
    said = _MARKUP.sub('', header).strip().lower()
    if said.startswith('ui label'):
        return 'name'
    if 'runtime default' in said:
        return 'default'
    if 'persist' in said:
        return 'persistence'
    if said.startswith('valid values'):
        return 'range'
    if 'effect' in said:
        return 'effect'
    if said.startswith('dependencies') or said == 'enabled when':
        return 'enabled'
    if 'trade-off' in said:
        return 'tradeoff'
    if said.startswith('scope'):
        return 'scope'
    return 'note'


def split_row(row):
    """The cells of a table row as written, a pipe inside a code span or behind a backslash left in its cell."""
    row = row.strip()
    masked = _CODE_SPAN.sub(lambda match: 'x' * len(match.group(0)), row).replace('\\|', 'xx')
    start = 1 if masked.startswith('|') else 0
    end = len(masked) - 1 if masked.endswith('|') and len(masked) > start else len(masked)
    cells = []
    last = start
    for cut in [index for index in range(start, end) if masked[index] == '|'] + [end]:
        cells.append(row[last:cut].strip().replace('\\|', '|'))
        last = cut + 1
    return cells


@dataclass
class OldRow:
    """One row of an old parameter table: the line it is on, the name its "UI label" cell gives and, for every other
    cell that states something, (the column's header, the facet the column lands in, the cell)."""

    line: int
    name: str
    cells: list


def old_rows(model, front):
    """Every row of the page's old parameter tables: the tables whose first column is headed "UI label"."""
    written = front.body.split('\n')
    # A line inside a fenced block is no table row; the prose reader leaves those lines empty.
    outside = [bool(text.strip()) for _, text in model.prose_lines(front.body, front.body_line)]
    lines = [(front.body_line + index, text if index < len(outside) and outside[index] else '')
             for index, text in enumerate(written)]
    rows = []
    index = 0
    while index + 1 < len(lines):
        header, rule = lines[index][1], lines[index + 1][1]
        if '|' not in header or '-' not in rule or not _TABLE_RULE.match(rule) \
                or len(split_row(header)) != len(split_row(rule)):
            index += 1
            continue
        headers = [_MARKUP.sub('', cell).strip() for cell in split_row(header)]
        index += 2
        described = facet_of(headers[0]) == 'name'
        while index < len(lines) and lines[index][1].strip() and '|' in lines[index][1]:
            if described:
                cells = split_row(lines[index][1])
                # A row with a pipe too many keeps its last cells together; one that stops early states less.
                if len(cells) > len(headers):
                    cells = cells[:len(headers) - 1] + [' | '.join(cells[len(headers) - 1:])]
                cells += [''] * (len(headers) - len(cells))
                rows.append(OldRow(line=lines[index][0], name=cells[0], cells=[
                    (title, facet_of(title), cell) for title, cell in zip(headers[1:], cells[1:])
                    if cell.strip() not in NOTHING]))
            index += 1
    return rows


def _key(text):
    """A name as rows and labels are matched: its words, without markup, punctuation or case."""
    return ' '.join(_NOT_A_WORD.sub(' ', _MARKUP.sub('', text).lower()).split())


def _sentence(text):
    text = text.strip()
    return text if text[-1:] in '.!?' else text + '.'


def _plain_value(text):
    """A default as an old table or the inventory states it, for comparing the two."""
    return _MARKUP.sub('', text).strip().lower()


def _same_default(control, old):
    """Whether an old table's default says what the inventory records."""
    if control.default is None:
        return False
    recorded, said = _plain_value(control.default), _plain_value(old)
    if recorded == said:
        return True
    if recorded in ('on', 'off'):
        return said in (CHECKED if recorded == 'on' else UNCHECKED)
    try:
        return float(recorded) == float(said)
    except ValueError:
        return False


def scalar(text, flow=False, quoted=False):
    """A text as a YAML scalar: plain when that reads back as the same text, double-quoted otherwise. `flow` says the
    scalar sits in a `[...]` or `{...}`, where a comma, a bracket or a brace would end it."""
    plain = _PLAIN_IN_FLOW.match(text) if flow else _PLAIN.match(text)
    if plain and not quoted and ': ' not in text and ' #' not in text:
        return text
    return _UNPRINTABLE.sub(lambda match: '\\u%04x' % ord(match.group(0)), json.dumps(text, ensure_ascii=False))


def _number(value):
    if isinstance(value, float) and value == int(value) and abs(value) < 1e15:
        return str(int(value))
    return repr(value)


@dataclass
class Entry:
    """One control a block lists, with the rows of the old table that name it."""

    window: object
    control: object
    rows: list = field(default_factory=list)


@dataclass
class Draft:
    """A page's draft: its text, the controls it lists by window, and where every row of the old table went."""

    text: str
    windows: list       # (UiWindow, [Entry, ...])
    rows: int
    matched: int
    shared_rows: list   # OldRow
    unmatched_rows: list


class DraftError(Exception):
    """A page no draft can be made of."""


_MODEL = None


def check_model():
    """The guide's model, loaded as the check loads it."""
    global _MODEL
    if _MODEL is None:
        _MODEL = check._load_model()
    return _MODEL


def _follows(window, control):
    """What a value the window does not fix follows: the image the window is opened on, or in a window opened on
    none the machine, for a number, and the images that are open, for a choice."""
    if check_model().UI_IMAGE_PLACEHOLDER in window.title:
        return 'the image'
    return 'the machine' if control.kind in RANGED_KINDS else 'the open images'


def _facets(entry):
    """The facets the old table gives a control: {facet: text}, and the old defaults the inventory does not state."""
    parts = collections.OrderedDict((facet, []) for facet in FACETS)
    for row in entry.rows:
        for header, facet, cell in row.cells:
            if facet == 'default':
                if not _same_default(entry.control, cell):
                    parts['note'].append(('Old default', cell))
            else:
                parts[facet].append((header, cell))
    facets = {}
    for facet, found in parts.items():
        if not found:
            continue
        if len(found) == 1 and facet != 'note':
            facets[facet] = found[0][1].strip()
        else:
            facets[facet] = ' '.join(_sentence('%s: %s' % (header, cell.strip())) for header, cell in found)
    return facets


def _control_lines(entry, needs_object):
    """The lines of one control of a block."""
    window, control = entry.window, entry.control
    follows = '(follows %s)' % _follows(window, control)
    comments = []
    if control.visible is False:
        comments.append('hidden when the window opens')
    if control.suffix:
        comments.append('unit: %s' % control.suffix)
    facets = _facets(entry)
    default = control.default
    options = list(control.options) if control.options is not None else None
    varying = [key for key in control.varies if key != 'visible']
    if control.kind in CHOICE_KINDS:
        if options is not None and not options:
            comments.append('the window opens with no choice in this list: write what the list holds')
            default, options = NO_CHOICE, [NO_CHOICE]
        elif options is None and default:
            options = [default, follows]
        elif options is None:
            default, options = follows, [follows]
        elif default is None:
            # A block's default is one of its choices: the first stands in for the one the image decides.
            default = options[0]
            comments.append('the default follows %s: the first choice stands in for it' % _follows(window, control))
            varying = [key for key in varying if key != 'default']
    elif default is None:
        default = follows
    if varying:
        comments.append('%s %s %s: replace the placeholder with what the window opens with' % (
            ' and '.join(varying), 'follows' if len(varying) == 1 else 'follow', _follows(window, control)))
    if needs_object and not control.object:
        comments.append('the window has several controls of this name, and this one has no object name to tell it by')
    lines = ['  # %s' % comment for comment in comments]
    lines.append('  - name: %s' % scalar(control.label))
    lines.append('    kind: %s' % control.kind)
    lines.append('    default: %s' % scalar(default, quoted=True))
    if control.numbers:
        lines.append('    widget: {%s}' % ', '.join('%s: %s' % (key, _number(control.numbers[key]))
                                                    for key in ('min', 'max', 'step', 'decimals')
                                                    if key in control.numbers))
    if options is not None:
        lines.append('    options: [%s]' % ', '.join(scalar(option, flow=True) for option in options))
    if needs_object and control.object:
        lines.append('    object: %s' % scalar(control.object))
    if control.kind in RANGED_KINDS:
        unbounded = [word for key, word in (('min', 'smallest'), ('max', 'largest')) if key not in control.numbers]
        if unbounded:
            todo = 'TODO: the %s %s %s; say what bounds %s.' % (
                ' and the '.join(unbounded) + (' value' if len(unbounded) == 1 else ' values'),
                'follows' if len(unbounded) == 1 else 'follow', _follows(window, control),
                'it' if len(unbounded) == 1 else 'them')
            facets['range'] = todo + (' ' + facets['range'] if 'range' in facets else '')
    facets.setdefault('effect', EFFECT_TODO)
    for facet in FACETS:
        if facet in facets:
            lines.append('    %s: %s' % (facet, scalar(facets[facet])))
    return lines


def _block_lines(window, entries):
    """The `controls` block of one window."""
    labels = collections.Counter(entry.control.label for entry in entries)
    titled = len({entry.control.groups[:1] for entry in entries}) > 1 and len(entries) > GROUP_COMMENTS_FROM
    lines = ['```controls', 'window: %s' % scalar(window.name), 'controls:']
    group = None
    for entry in entries:
        control = entry.control
        if titled and control.group != group:
            group = control.group
            lines.append('  # --- %s ---' % (group or 'under no title'))
        lines += _control_lines(entry, labels[control.label] > 1 or control.label == control.object)
    lines.append('```')
    return lines


def _row_lines(row):
    lines = ['- %s' % (row.name.strip() or '(no name)')]
    lines += ['    - %s: %s' % (header, cell.strip()) for header, _, cell in row.cells]
    return lines


def resolve_page(docs, given):
    """A page named on the command line, as a path relative to the docs folder, or None."""
    for candidate in (os.path.join(docs, given), os.path.abspath(given)):
        try:
            relative = os.path.relpath(os.path.abspath(candidate), docs).replace('\\', '/')
        except ValueError:
            continue  # another drive than the docs folder's
        if not relative.startswith('../') and os.path.isfile(candidate):
            return relative
    return None


def draft_page(repo, guide, given, shared=False):
    """The draft of a page's `controls` blocks; DraftError when the page is none a draft can be made of."""
    model = check_model()
    docs = model.docs_dir(guide, model.load_config(guide))
    relative = resolve_page(docs, given)
    if relative is None:
        raise DraftError('%s is not a page of %s' % (given, docs.replace('\\', '/')))
    front = model.split_front_matter(model.read_text(os.path.join(docs, relative)))
    if not front.meta or front.meta.get('kind') != 'process':
        raise DraftError('%s is not a process page: its front matter does not state `kind: process`' % relative)
    menus = model.menu_paths(front.meta)
    product = check.ProductMenus(repo)
    inventory = model.load_ui_inventory(guide)
    # The windows the check holds the page to: the ones its `menu` reaches that no page names more exactly.
    pages = [os.path.relpath(os.path.join(folder, name), docs).replace('\\', '/')
             for folder, _, names in os.walk(docs) for name in sorted(names) if name.endswith('.md')]
    windows = check.windows_of(product, inventory, menus, check.guide_menus(model, docs, pages).values())
    if not windows:
        actions = [action for action, _ in product.registered
                   if any(check._covered(place, menus) for place, _, _ in product.placements_of(action))]
        stated = '; '.join(menus) or 'not stated'
        elsewhere = [action for action in actions if action in inventory]
        if elsewhere:
            reason = 'its `menu` (%s) reaches %s, and another page names %s more exactly' % (
                stated, ', '.join(elsewhere), 'that window' if len(elsewhere) == 1 else 'each of those windows')
        elif actions:
            reason = 'its `menu` (%s) covers %s, which open no process window' % (stated, ', '.join(actions))
        else:
            reason = 'its `menu` (%s) covers no registered action' % stated
        raise DraftError('%s opens no window the inventory holds for it: %s' % (relative, reason))

    listed = [(window, [Entry(window, control) for control in window.controls if shared or not control.shared])
              for window in windows]
    left_out = [control for window in windows for control in window.controls if control.shared and not shared]
    rows = old_rows(model, front)
    shared_rows, unmatched_rows = [], []
    matched = 0
    for row in rows:
        key = _key(row.name)
        entry = next((entry for _, entries in listed for entry in entries
                      if key and _key(entry.control.label) == key), None)
        if entry is not None:
            entry.rows.append(row)
            matched += 1
        elif key and any(_key(control.label) == key for control in left_out):
            shared_rows.append(row)
        else:
            unmatched_rows.append(row)

    drafted = sum(len(entries) for _, entries in listed)
    lines = ['# Controls draft of %s' % relative, '']
    lines.append('%d control%s drafted from the inventory of %s. Of the %d row%s of the old parameter table, %d %s on '
                 'a control, %d on a control every window shares and %d on no control of the window. `TODO` and '
                 '`(follows ...)` mark what is still to be written.' % (
                     drafted, '' if drafted == 1 else 's',
                     'its window' if len(windows) == 1 else 'its %d windows' % len(windows),
                     len(rows), '' if len(rows) == 1 else 's', matched, 'is' if matched == 1 else 'are',
                     len(shared_rows), len(unmatched_rows)))
    for window, entries in listed:
        omitted = len(window.controls) - len(entries)
        lines += ['', '## %s' % window.name, '']
        lines.append('`%s`: %d control%s drafted%s.' % (
            window.id, len(entries), '' if len(entries) == 1 else 's',
            ', %d shared with every window left out' % omitted if omitted else ''))
        lines.append('')
        if entries:
            lines += _block_lines(window, entries)
        else:
            lines.append('The window has no control of its own to draft.')
    if shared_rows:
        lines += ['', '## Old rows of controls every window shares', '',
                  'A process page leaves these controls out: they belong to the Process windows page.', '']
        for row in shared_rows:
            lines += _row_lines(row)
    if unmatched_rows:
        lines += ['', '## Old rows that name no control of the window', '',
                  'Plots, gestures, behaviors, and controls under another name: their facts go into the page\'s prose, '
                  'or onto the control they belong to.', '']
        for row in unmatched_rows:
            lines += _row_lines(row)
    return Draft(text='\n'.join(lines) + '\n', windows=listed, rows=len(rows), matched=matched,
                 shared_rows=shared_rows, unmatched_rows=unmatched_rows)


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('page', help='the process page, relative to the docs folder or to the working directory')
    parser.add_argument('--repo', default=REPO_ROOT, help='the repository whose application sources name the menus')
    parser.add_argument('--guide', help='the guide folder that holds mkdocs.yml; default <repo>/docs/user-guide')
    parser.add_argument('--shared', action='store_true', help='keep the controls every window shares, which a page '
                        'leaves out')
    arguments = parser.parse_args(argv)
    if stream is None:
        stream = sys.stdout
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    guide = arguments.guide or os.path.join(arguments.repo, 'docs', 'user-guide')
    try:
        model = check_model()
    except ImportError as error:
        print('draft_guide_controls.py needs PyYAML (%s): run it with the documentation toolchain\'s Python, '
              'docs/user-guide/.venv, which the guide\'s build helper makes' % error, file=stream)
        return 2
    try:
        draft = draft_page(arguments.repo, guide, arguments.page, arguments.shared)
    except DraftError as error:
        print(error, file=stream)
        return 2
    except (model.GuideDataError, OSError) as error:
        print('the guide cannot be read: %s' % error, file=stream)
        return 2
    stream.write(draft.text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
