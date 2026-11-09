# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The user guide's model: the pages' front matter, the guide's data files, its navigation and image headers.

The guide's MkDocs hooks and `build/scripts/tools/check_user_guide.py` both read the guide through this module, so the
build and the check hold one schema. It needs PyYAML, which the documentation toolchain installs, and nothing else.

A page states what the theme and the checks need in YAML front matter, read as MkDocs reads it: a block between a
leading `---` line and the next `---` or `...` line, loaded with PyYAML's safe loader. `kind` picks the page's layout
and the rules it is held to. A fact that has a source elsewhere is not repeated here: the title is the page's H1 and
its nav label, the category is its nav group, related pages are links in its text, and its budgets live in
`data/limits.yml`.

Tests: docs/user-guide/hooks/guide_model_test.py
"""

from __future__ import annotations

import os
import re
import struct
from dataclasses import dataclass, field

import yaml

KINDS = ('home', 'catalog', 'process', 'interface', 'tutorial', 'workflow', 'reference')
DATA_STATES = ('linear', 'nonlinear', 'either', 'cfa')
PREVIEW_MODES = ('real-time', 'static', 'none')
MASK_MODES = ('post-blend', 'modulates', 'none')
GLANCE_KEYS = ('enabled', 'inputs', 'outputs', 'state')
MENU_SEPARATOR = ' > '

_ALL = frozenset(KINDS)
_PROCESS = frozenset({'process'})
_SPANNING = frozenset({'tutorial', 'workflow'})

# Every key a page may state, with the kinds that may state it.
KEY_KINDS = {
    'kind': _ALL,
    'summary': _ALL - {'home'},
    'stage': _PROCESS,
    'stages': _SPANNING,
    'menu': _ALL,
    'data_in': _PROCESS,
    'data_out': _PROCESS,
    'destructive': _PROCESS,
    'preview': _PROCESS,
    'sequences': _PROCESS,
    'masks': _PROCESS,
    'external': _PROCESS,
    'glance': _PROCESS,
    'derived': _PROCESS,
    'controls_ignore': _PROCESS,
    'needs': _SPANNING,
    'hero': frozenset({'home'}),
    'notice': frozenset({'home'}),
    'paths': frozenset({'home'}),
    'popular': frozenset({'home'}),
    'reach': frozenset({'catalog'}),
    'rewritten': _ALL,
}

# The keys a page of each kind must state.
REQUIRED_KEYS = {
    'home': ('kind',),
    'catalog': ('kind', 'summary'),
    'process': ('kind', 'summary', 'stage', 'menu'),
    'interface': ('kind', 'summary'),
    # A tutorial states `stages` when it walks through part of the pipeline; installing and a first launch come before
    # it.
    'tutorial': ('kind', 'summary'),
    'workflow': ('kind', 'summary', 'stages'),
    'reference': ('kind', 'summary'),
}

_FRONT_MATTER = re.compile(r'^-{3}[ \t]*\n(.*?\n)(?:\.{3}|-{3})[ \t]*\n', re.UNICODE | re.DOTALL)
_KEY_LINE = re.compile(r'^([A-Za-z0-9_-]+)\s*:')
_FENCE = re.compile(r'^\s*(`{3,}|~{3,})')
_ATX_H1 = re.compile(r'^ {0,3}#(?:[ \t]+|$)')
_HTML_H1 = re.compile(r'<h1[\s>]', re.IGNORECASE)
_MARKDOWN_IMAGE = re.compile(
    r'!\[(?:[^\]\\]|\\.)*\]\(\s*(?:<([^>]*)>|([^)\s]+))(?:\s+(?:"[^"]*"|\'[^\']*\'))?\s*\)')
_HTML_IMAGE = re.compile(r'<img\b[^>]*?\bsrc\s*=\s*(?:"([^"]*)"|\'([^\']*)\')', re.IGNORECASE)
_CODE_SPAN = re.compile(r'(`+)(.+?)\1')
_HEX_COLOR = re.compile(r'^#[0-9A-Fa-f]{6}$')


class GuideDataError(Exception):
    """A data file of the guide cannot be read or does not hold what the guide needs."""


@dataclass
class FrontMatter:
    """A page's front matter: the mapping it states, the line of each key, and where the body starts."""

    meta: dict | None
    error: str | None = None
    key_lines: dict = field(default_factory=dict)
    body: str = ''
    body_line: int = 1

    @property
    def present(self):
        return self.meta is not None or self.error is not None


def read_text(path):
    """A text file as MkDocs reads a page: UTF-8 with an optional byte-order mark, line endings made `\\n`."""
    with open(path, encoding='utf-8-sig') as handle:
        return handle.read()


def split_front_matter(text):
    """The front matter of a page's text and the body after it, as MkDocs splits them."""
    match = _FRONT_MATTER.match(text)
    if not match:
        return FrontMatter(meta=None, body=text, body_line=1)
    block = match.group(1)
    key_lines = {}
    for number, line in enumerate(block.split('\n'), start=2):
        key = _KEY_LINE.match(line)
        if key and key.group(1) not in key_lines:
            key_lines[key.group(1)] = number
    body_line = text.count('\n', 0, match.end()) + 1
    try:
        meta = yaml.load(block, yaml.SafeLoader)
    except yaml.YAMLError as error:
        mark = getattr(error, 'problem_mark', None)
        where = ' at front matter line %d' % (mark.line + 2) if mark is not None else ''
        return FrontMatter(meta=None, error='the front matter is not valid YAML%s: %s' %
                           (where, getattr(error, 'problem', None) or error), key_lines=key_lines, body=text,
                           body_line=1)
    if not isinstance(meta, dict):
        return FrontMatter(meta=None, error='the front matter is not a mapping of keys to values',
                           key_lines=key_lines, body=text, body_line=1)
    return FrontMatter(meta=meta, key_lines=key_lines, body=text[match.end():], body_line=body_line)


def _is_text(value):
    return isinstance(value, str) and value.strip() != ''


def _is_menu_path(value):
    if not _is_text(value):
        return False
    parts = value.split(MENU_SEPARATOR)
    return all(part.strip() == part and part != '' and '>' not in part for part in parts)


def validate_meta(meta, stage_ids):
    """Every problem with a page's front matter, as (key or None, message) pairs; an empty list when it is valid."""
    problems = []
    kind = meta.get('kind')
    if kind not in KINDS:
        problems.append(('kind', '`kind` is %r; it must be one of %s' % (kind, ', '.join(KINDS))))
        return problems
    for key in meta:
        if key not in KEY_KINDS:
            problems.append((key, 'unknown key `%s`' % key))
        elif kind not in KEY_KINDS[key]:
            problems.append((key, '`%s` does not apply to a %s page' % (key, kind)))
    for key in REQUIRED_KEYS[kind]:
        if key not in meta:
            problems.append((None, 'a %s page needs `%s`' % (kind, key)))

    def check(key, test, expected):
        if key in meta and kind in KEY_KINDS.get(key, ()) and not test(meta[key]):
            problems.append((key, '`%s` is %r; it must be %s' % (key, meta[key], expected)))

    def is_list_of(test, allow_empty=True):
        return lambda value: isinstance(value, list) and (allow_empty or value) and all(test(item) for item in value)

    check('summary', _is_text, 'one sentence of text')
    check('stage', lambda value: value in stage_ids, 'a stage id of data/stages.yml (%s)' % ', '.join(stage_ids))
    check('stages', lambda value: (is_list_of(lambda item: item in stage_ids, allow_empty=False)(value)
                                   and len(set(value)) == len(value)
                                   and value == sorted(value, key=stage_ids.index)),
          'a list of distinct stage ids of data/stages.yml, in pipeline order (%s)' % ', '.join(stage_ids))
    check('menu', lambda value: _is_menu_path(value) or (is_list_of(_is_menu_path, allow_empty=False)(value)
                                                         and len(set(value)) == len(value)),
          'a menu path, or a list of distinct menu paths, with " > " between the levels')
    check('data_in', lambda value: value in DATA_STATES, 'one of ' + ', '.join(DATA_STATES))
    check('data_out', lambda value: value in DATA_STATES, 'one of ' + ', '.join(DATA_STATES))
    for key in ('destructive', 'sequences', 'derived'):
        check(key, lambda value: isinstance(value, bool), 'true or false')
    check('preview', lambda value: value in PREVIEW_MODES, 'one of ' + ', '.join(PREVIEW_MODES))
    check('masks', lambda value: value in MASK_MODES, 'one of ' + ', '.join(MASK_MODES))
    check('external', _is_text, '`none` or a short phrase naming what the process needs')
    check('glance', lambda value: (isinstance(value, dict) and value
                                   and all(key in GLANCE_KEYS and _is_text(text) for key, text in value.items())),
          'a mapping of %s to text' % ', '.join(GLANCE_KEYS))
    check('controls_ignore', is_list_of(lambda item: (isinstance(item, dict) and set(item) == {'label', 'reason'}
                                                      and _is_text(item['label']) and _is_text(item['reason']))),
          'a list of {label, reason}')
    check('needs', is_list_of(_is_text), 'a list of short phrases, empty when nothing outside AfterNight is needed')
    check('hero', lambda value: isinstance(value, dict), 'a mapping')
    check('notice', _is_text, 'text')
    check('paths', lambda value: isinstance(value, list), 'a list')
    check('popular', lambda value: isinstance(value, list), 'a list')
    check('reach', is_list_of(_is_text, allow_empty=False), 'a list of sentences')
    check('rewritten', lambda value: value is True, 'true; leave it out until the page is rewritten')
    return problems


def menu_paths(meta):
    """The menu paths a page's front matter states, as a list."""
    menu = meta.get('menu')
    if isinstance(menu, str):
        return [menu]
    if isinstance(menu, list):
        return [item for item in menu if isinstance(item, str)]
    return []


@dataclass(frozen=True)
class Stage:
    id: str
    name: str
    description: str
    number: int | None
    hue_dark: str
    hue_light: str


def load_stages(guide_dir):
    """The stages of data/stages.yml, in pipeline order, with "Any time" last."""
    path = os.path.join(guide_dir, 'data', 'stages.yml')
    try:
        data = yaml.safe_load(read_text(path))
    except (OSError, yaml.YAMLError) as error:
        raise GuideDataError('%s cannot be read: %s' % (path, error)) from error
    entries = data.get('stages') if isinstance(data, dict) else None
    if not isinstance(entries, list) or not entries:
        raise GuideDataError('%s holds no `stages` list' % path)
    stages = []
    for index, entry in enumerate(entries, start=1):
        if not isinstance(entry, dict):
            raise GuideDataError('%s: stage %d is not a mapping' % (path, index))
        hue = entry.get('hue') if isinstance(entry.get('hue'), dict) else {}
        values = (entry.get('id'), entry.get('name'), entry.get('description'), hue.get('dark'), hue.get('light'))
        if not all(_is_text(value) for value in values):
            raise GuideDataError('%s: stage %d needs an id, a name, a description and a dark and light hue' %
                                 (path, index))
        if not all(_HEX_COLOR.match(color) for color in values[3:]):
            raise GuideDataError('%s: stage %s has a hue that is not #RRGGBB' % (path, values[0]))
        number = entry.get('number')
        if number is not None and number != index:
            raise GuideDataError('%s: stage %s is numbered %r but is stage %d of the pipeline' %
                                 (path, values[0], number, index))
        stages.append(Stage(id=values[0], name=values[1], description=values[2], number=number,
                            hue_dark=values[3], hue_light=values[4]))
    ids = [stage.id for stage in stages]
    if len(set(ids)) != len(ids):
        raise GuideDataError('%s: stage ids repeat' % path)
    if stages[-1].number is not None or any(stage.number is None for stage in stages[:-1]):
        raise GuideDataError('%s: every stage but the last, "Any time", is numbered' % path)
    return stages


class _NavLoader(yaml.SafeLoader):
    """A safe loader that reads mkdocs.yml's `!!python/name:` and `!ENV` tags as nothing: only the nav matters here."""


_NavLoader.add_multi_constructor('tag:yaml.org,2002:python/', lambda loader, suffix, node: None)
_NavLoader.add_constructor('!ENV', lambda loader, node: None)
_NavLoader.add_constructor('!relative', lambda loader, node: None)


@dataclass(frozen=True)
class NavEntry:
    label: str
    path: str
    groups: tuple


def load_config(guide_dir, config_name='mkdocs.yml'):
    """The guide's MkDocs configuration as a mapping, its Python tags read as nothing."""
    path = os.path.join(guide_dir, config_name)
    try:
        config = yaml.load(read_text(path), _NavLoader)
    except (OSError, yaml.YAMLError) as error:
        raise GuideDataError('%s cannot be read: %s' % (path, error)) from error
    if not isinstance(config, dict):
        raise GuideDataError('%s is not a mapping' % path)
    return config


def docs_dir(guide_dir, config):
    return os.path.normpath(os.path.join(guide_dir, config.get('docs_dir', 'docs')))


def nav_entries(config):
    """Every page of the nav, in order, with the labels of the groups that hold it."""
    entries = []

    def walk(items, groups):
        for item in items or []:
            if isinstance(item, str):
                entries.append(NavEntry(label='', path=item, groups=tuple(groups)))
            elif isinstance(item, dict):
                for label, value in item.items():
                    if isinstance(value, list):
                        walk(value, groups + [label])
                    elif isinstance(value, str) and not re.match(r'^[a-z][a-z0-9+.-]*:', value, re.I):
                        entries.append(NavEntry(label=label, path=value, groups=tuple(groups)))

    walk(config.get('nav'), [])
    return entries


def _outside_code(text):
    """(line number, line) for every line outside fenced blocks, with code spans blanked."""
    fence = None
    for number, line in enumerate(text.split('\n'), start=1):
        opened = _FENCE.match(line)
        if fence is None:
            if opened:
                fence = opened.group(1)
                continue
        else:
            if opened and opened.group(1)[0] == fence[0] and len(opened.group(1)) >= len(fence) \
                    and line.strip() == opened.group(1):
                fence = None
            continue
        yield number, _CODE_SPAN.sub(lambda match: ' ' * len(match.group(0)), line)


def h1_lines(body, first_line=1):
    """The line of every level-one heading of a page body: `# ` headings and `<h1>` elements, outside code."""
    lines = []
    for number, line in _outside_code(body):
        if _ATX_H1.match(line) or _HTML_H1.search(line):
            lines.append(number + first_line - 1)
    return lines


def image_references(body, first_line=1):
    """(line, url) for every image a page body shows: Markdown images and `<img src>`, outside code."""
    references = []
    for number, line in _outside_code(body):
        for match in _MARKDOWN_IMAGE.finditer(line):
            references.append((number + first_line - 1, match.group(1) if match.group(1) is not None
                               else match.group(2)))
        for match in _HTML_IMAGE.finditer(line):
            references.append((number + first_line - 1, match.group(1) if match.group(1) is not None
                               else match.group(2)))
    return references


def image_size(path):
    """(width, height) of a PNG, WebP or JPEG file from its header, or None when it is none of them."""
    with open(path, 'rb') as handle:
        head = handle.read(64)
        if head[:8] == b'\x89PNG\r\n\x1a\n' and head[12:16] == b'IHDR':
            return struct.unpack('>II', head[16:24])
        if head[:4] == b'RIFF' and head[8:12] == b'WEBP':
            chunk = head[12:16]
            if chunk == b'VP8 ' and head[23:26] == b'\x9d\x01\x2a':
                width, height = struct.unpack('<HH', head[26:30])
                return width & 0x3FFF, height & 0x3FFF
            if chunk == b'VP8L' and head[20] == 0x2F:
                bits = struct.unpack('<I', head[21:25])[0]
                return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
            if chunk == b'VP8X':
                width = int.from_bytes(head[24:27], 'little') + 1
                height = int.from_bytes(head[27:30], 'little') + 1
                return width, height
            return None
        if head[:2] == b'\xff\xd8':
            handle.seek(2)
            while True:
                marker = handle.read(2)
                if len(marker) < 2 or marker[0] != 0xFF:
                    return None
                code = marker[1]
                if code == 0xFF:
                    handle.seek(-1, os.SEEK_CUR)
                    continue
                if code in (0xD8, 0x01) or 0xD0 <= code <= 0xD7:
                    continue
                length_bytes = handle.read(2)
                if len(length_bytes) < 2:
                    return None
                length = struct.unpack('>H', length_bytes)[0]
                if 0xC0 <= code <= 0xCF and code not in (0xC4, 0xC8, 0xCC):
                    data = handle.read(5)
                    if len(data) < 5:
                        return None
                    height, width = struct.unpack('>HH', data[1:5])
                    return width, height
                handle.seek(length - 2, os.SEEK_CUR)
    return None
