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

The module also reads a page's body as what it says - its headings, lists, tables and prose, outside code and math -
for the rules that hold a page to its template, its budgets and its vocabulary - and the inventory of the controls
every process window opens with (`data/ui/`), which a test of the application writes and a page's `controls` blocks
are held to.

Tests: docs/user-guide/hooks/guide_model_test.py
"""

from __future__ import annotations

import json
import os
import re
import struct
import textwrap
from dataclasses import dataclass, field

import yaml

KINDS = ('home', 'catalog', 'process', 'interface', 'tutorial', 'workflow', 'reference')
DATA_STATES = ('linear', 'nonlinear', 'either', 'cfa')
PREVIEW_MODES = ('real-time', 'static', 'none')
MASK_MODES = ('post-blend', 'modulates', 'none')
GLANCE_KEYS = ('enabled', 'inputs', 'outputs', 'state')
# Where the image stands at a numbered stage: still linear, turned nonlinear by this stage, or nonlinear.
STAGE_DATA = ('linear', 'turn', 'nonlinear')
HERO_KEYS = ('lede', 'accent', 'actions', 'image', 'alt', 'caption')
HERO_REQUIRED = ('lede', 'actions')
MAX_HERO_ACTIONS = 3
MAX_PATHS = 4
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
_FENCE_LANGUAGE = re.compile(r'\s*\{?\s*\.?([A-Za-z0-9_+-]*)')
_ATX_H1 = re.compile(r'^ {0,3}#(?:[ \t]+|$)')
_HTML_H1 = re.compile(r'<h1[\s>]', re.IGNORECASE)
_ATX_SECTION = re.compile(r'^ {0,3}(#{1,2})[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$')
_HTML_COMMENT = re.compile(r'<!--.*?-->', re.DOTALL)
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


def _is_page_path(value):
    """A page named by its source path under the docs folder, as the nav names it: `processes/index.md`."""
    return _is_text(value) and value.endswith('.md') and not value.startswith(('/', '.')) and '\\' not in value


def _is_record(keys):
    """A test for a mapping that states exactly `keys`, each as text, its `page` a page path."""
    def test(value):
        return (isinstance(value, dict) and set(value) == set(keys) and all(_is_text(item) for item in value.values())
                and ('page' not in value or _is_page_path(value['page'])))
    return test


def _is_hero(value):
    if not isinstance(value, dict) or any(key not in HERO_KEYS for key in value):
        return False
    if any(key not in value for key in HERO_REQUIRED):
        return False
    if not all(_is_text(value[key]) for key in ('lede', 'accent', 'image', 'alt', 'caption') if key in value):
        return False
    # A picture is stated whole: the file, what it shows and its caption.
    if len({key in value for key in ('image', 'alt', 'caption')}) != 1:
        return False
    actions = value['actions']
    return (isinstance(actions, list) and 1 <= len(actions) <= MAX_HERO_ACTIONS
            and all(_is_record(('label', 'page'))(action) for action in actions))


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
    check('controls_ignore', is_list_of(lambda item: (isinstance(item, dict)
                                                      and set(item) in ({'label', 'reason'},
                                                                        {'label', 'object', 'reason'})
                                                      and all(_is_text(value) for value in item.values()))),
          'a list of {label, reason}, each with the `object` of the one control meant when the window has several of '
          'that label')
    check('needs', is_list_of(_is_text), 'a list of short phrases, empty when nothing outside AfterNight is needed')
    check('hero', _is_hero,
          'a mapping of `lede`, `actions` (one to %d of {label, page}, the first the main one), and optionally `accent` '
          '(words of the H1 to draw in the accent color) and a picture as `image`, `alt` and `caption` together' %
          MAX_HERO_ACTIONS)
    check('notice', _is_text, 'text')
    check('paths', lambda value: (is_list_of(_is_record(('tag', 'title', 'text', 'page')), allow_empty=False)(value)
                                  and len(value) <= MAX_PATHS),
          'a list of one to %d of {tag, title, text, page}' % MAX_PATHS)
    check('popular', lambda value: (is_list_of(_is_page_path, allow_empty=False)(value)
                                    and len(set(value)) == len(value)),
          'a list of distinct pages, each named by its path under the docs folder')
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
    data: str | None = None


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
        state = entry.get('data')
        if (state is None) != (number is None) or (state is not None and state not in STAGE_DATA):
            raise GuideDataError('%s: stage %s states `data` (%s) when it is numbered, and not otherwise' %
                                 (path, values[0], ', '.join(STAGE_DATA)))
        stages.append(Stage(id=values[0], name=values[1], description=values[2], number=number,
                            hue_dark=values[3], hue_light=values[4], data=state))
    ids = [stage.id for stage in stages]
    if len(set(ids)) != len(ids):
        raise GuideDataError('%s: stage ids repeat' % path)
    if stages[-1].number is not None or any(stage.number is None for stage in stages[:-1]):
        raise GuideDataError('%s: every stage but the last, "Any time", is numbered' % path)
    return stages


# The limits a page kind may state in data/limits.yml, and the ones its `large` pages may raise.
KIND_LIMITS = {
    'process': ('words', 'opening_words', 'before_you_start', 'steps', 'how_it_works', 'boundaries',
                'starting_points_rows', 'starting_points_columns', 'warnings', 'troubleshooting', 'related',
                'credits'),
    'interface': ('words', 'troubleshooting', 'related'),
    'tutorial': ('words', 'steps', 'wrong_per_step'),
    'workflow': ('words', 'steps', 'wrong_per_step'),
    'reference': (),
}
IMAGE_LIMITS = ('longest_side', 'max_bytes')


def _load_data(guide_dir, name):
    path = os.path.join(guide_dir, 'data', name)
    try:
        data = yaml.safe_load(read_text(path))
    except (OSError, yaml.YAMLError) as error:
        raise GuideDataError('%s cannot be read: %s' % (path, error)) from error
    if not isinstance(data, dict):
        raise GuideDataError('%s is not a mapping' % path)
    return path, data


def _is_count(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


@dataclass(frozen=True)
class Limits:
    """The budgets of data/limits.yml. A limit is an upper bound, and one a kind does not state does not apply."""

    summary_words: int
    table_columns: int
    kinds: dict
    large: dict
    large_pages: frozenset
    glossary_page: str
    glossary_entry_words: int
    image_longest_side: int
    image_max_bytes: int
    provenance_page: str
    before_after_groups: tuple

    def of(self, kind, name, page=None):
        """The limit `name` of a page of `kind`, the `large` one when `page` is a large page; None when the kind has
        none."""
        if page in self.large_pages and name in self.large.get(kind, {}):
            return self.large[kind][name]
        return self.kinds.get(kind, {}).get(name)


def load_limits(guide_dir):
    """The budgets of data/limits.yml."""
    path, data = _load_data(guide_dir, 'limits.yml')

    def fail(message):
        raise GuideDataError('%s: %s' % (path, message))

    known = ('summary_words', 'table_columns', 'kinds', 'large_pages', 'glossary', 'images', 'before_after_groups')
    for key in data:
        if key not in known:
            fail('unknown key `%s`' % key)
    for key in known:
        if key not in data:
            fail('`%s` is missing' % key)
    for key in ('summary_words', 'table_columns'):
        if not _is_count(data[key]):
            fail('`%s` is %r, not a count' % (key, data[key]))
    if not isinstance(data['kinds'], dict):
        fail('`kinds` is a mapping of page kinds to their limits')
    kinds, large = {}, {}
    for kind, limits in data['kinds'].items():
        if kind not in KIND_LIMITS:
            fail('`kinds` names %r; a page kind with limits is one of %s' % (kind, ', '.join(KIND_LIMITS)))
        if not isinstance(limits, dict):
            fail('the limits of `%s` are a mapping' % kind)
        kinds[kind] = {}
        for name, value in limits.items():
            if name == 'large':
                if not isinstance(value, dict):
                    fail('`%s.large` is a mapping of the limits a large page raises' % kind)
                for large_name, large_value in value.items():
                    if large_name not in KIND_LIMITS[kind] or not _is_count(large_value):
                        fail('`%s.large.%s` is %r; it is a count for one of %s' %
                             (kind, large_name, large_value, ', '.join(KIND_LIMITS[kind])))
                large[kind] = dict(value)
            elif name not in KIND_LIMITS[kind]:
                fail('`%s.%s` is no limit of a %s page; its limits are %s' %
                     (kind, name, kind, ', '.join(KIND_LIMITS[kind]) or 'none'))
            elif not _is_count(value):
                fail('`%s.%s` is %r, not a count' % (kind, name, value))
            else:
                kinds[kind][name] = value
    pages = data['large_pages']
    if not isinstance(pages, list) or not all(_is_page_path(page) for page in pages) \
            or len(set(pages)) != len(pages):
        fail('`large_pages` is a list of distinct pages, each named by its path under the docs folder')
    glossary = data['glossary']
    if not isinstance(glossary, dict) or set(glossary) != {'page', 'entry_words'} \
            or not _is_page_path(glossary['page']) or not _is_count(glossary['entry_words']):
        fail('`glossary` states `page`, a path under the docs folder, and `entry_words`, a count')
    images = data['images']
    if not isinstance(images, dict) or set(images) != set(IMAGE_LIMITS) | {'provenance'} \
            or not all(_is_count(images[key]) for key in IMAGE_LIMITS) or not _is_page_path(images['provenance']):
        fail('`images` states %s, each a count, and `provenance`, a page named by its path under the docs folder' %
             ', '.join(IMAGE_LIMITS))
    groups = data['before_after_groups']
    if not isinstance(groups, list) or not all(_is_text(group) for group in groups):
        fail('`before_after_groups` is a list of nav group labels')
    return Limits(summary_words=data['summary_words'], table_columns=data['table_columns'], kinds=kinds,
                  large=large, large_pages=frozenset(pages), glossary_page=glossary['page'],
                  glossary_entry_words=glossary['entry_words'], image_longest_side=images['longest_side'],
                  image_max_bytes=images['max_bytes'], provenance_page=images['provenance'],
                  before_after_groups=tuple(groups))


@dataclass(frozen=True)
class ForbiddenPattern:
    group: str
    why: str
    source: str
    regex: re.Pattern
    code: bool = False


@dataclass(frozen=True)
class Allowance:
    page: str
    text: str
    reason: str
    line: int


@dataclass(frozen=True)
class Vocabulary:
    """The forbidden patterns of data/vocabulary.yml and what a page may say all the same."""

    patterns: tuple
    allow: tuple


def load_vocabulary(guide_dir):
    """The forbidden vocabulary of data/vocabulary.yml: its patterns, compiled without regard to case, and its
    allow-list."""
    path, data = _load_data(guide_dir, 'vocabulary.yml')

    def fail(message):
        raise GuideDataError('%s: %s' % (path, message))

    if set(data) != {'forbidden', 'allow'}:
        fail('the file states `forbidden` and `allow`')
    if not isinstance(data['forbidden'], dict) or not data['forbidden']:
        fail('`forbidden` is a mapping of groups, each with `why` and `patterns`')
    patterns = []
    for group, entry in data['forbidden'].items():
        if not isinstance(entry, dict) or set(entry) != {'why', 'patterns'} or not _is_text(entry['why']) \
                or not isinstance(entry['patterns'], list) or not entry['patterns']:
            fail('`forbidden.%s` states `why`, a sentence, and `patterns`, a list' % group)
        for item in entry['patterns']:
            if isinstance(item, dict) and set(item) == {'pattern', 'code'} and isinstance(item['code'], bool):
                source, code = item['pattern'], item['code']
            else:
                source, code = item, False
            if not _is_text(source):
                fail('a pattern of `forbidden.%s` is %r; it is a regular expression, or {pattern, code}' %
                     (group, item))
            try:
                regex = re.compile(source, re.IGNORECASE)
            except re.error as error:
                fail('the pattern %r of `forbidden.%s` is not a regular expression: %s' % (source, group, error))
            patterns.append(ForbiddenPattern(group=group, why=entry['why'].strip(), source=source, regex=regex,
                                             code=code))
    if not isinstance(data['allow'], list):
        fail('`allow` is a list of {page, text, reason}')
    lines = read_text(path).split('\n')
    allow = []
    searched_from = 0
    for entry in data['allow']:
        if not _is_record(('page', 'text', 'reason'))(entry):
            fail('an entry of `allow` is %r; it states `page`, a path under the docs folder, `text` and `reason`' %
                 (entry,))
        # The line of the entry: the next line, after the previous entry's, that names its page.
        line = next((index for index in range(searched_from, len(lines))
                     if not lines[index].lstrip().startswith('#') and entry['page'] in lines[index]), None)
        if line is not None:
            searched_from = line + 1
        allow.append(Allowance(page=entry['page'], text=entry['text'], reason=entry['reason'],
                               line=line + 1 if line is not None else 1))
    return Vocabulary(patterns=tuple(patterns), allow=tuple(allow))


# --- The inventory of the windows' controls ---------------------------------------------------------------------------
# One file per process window under data/ui/, named by the id the process is registered under, `<category>.<action>`.
# The application's test suite writes them (ProcessUiInventoryTest in Test_ProcessesShared) and fails when a window no
# longer matches its file, so they are generated, and never edited by hand.

UI_INVENTORY = os.path.join('data', 'ui')
UI_KINDS = ('spinbox', 'slider', 'combo', 'check', 'radio', 'toolbutton', 'button', 'field', 'action')
# The numbers a spin box or a slider is recorded with, as a `controls` block states them under `widget`.
UI_NUMBERS = ('min', 'max', 'step', 'decimals')
UI_GROUP_SEPARATOR = ' > '
# What stands in a recorded window title for the name of the image the window was opened on.
UI_IMAGE_PLACEHOLDER = '{image}'
_UI_IMAGE_IN_TITLE = re.compile(r'\s*<\{image\}>|\s*\{image\}')
_UI_KEYS = {
    'label': str, 'kind': str, 'object': str, 'group': str, 'visible': bool, 'default': str, 'min': (int, float),
    'max': (int, float), 'step': (int, float), 'decimals': int, 'suffix': str, 'options': list, 'slider': bool,
    'shared': str, 'varies': list,
}
_UI_REQUIRED = ('label', 'kind', 'object', 'group')
# What a record may leave to the image the window is opened on, or to the machine it runs on.
_UI_VARYING = ('visible', 'default', 'min', 'max', 'step', 'decimals', 'options')


@dataclass(frozen=True)
class UiControl:
    """One control of a window as the inventory records it. `default`, `options` and a number are None when the
    window does not fix them: `varies` names what follows the image the window is opened on, or the machine."""

    index: int
    label: str
    kind: str
    object: str
    group: str
    visible: bool | None
    default: str | None
    numbers: dict
    suffix: str
    options: tuple | None
    slider: bool
    shared: str
    varies: tuple

    @property
    def groups(self):
        """The titles the control sits under, outermost first: a tab, a card, a group box."""
        return tuple(self.group.split(UI_GROUP_SEPARATOR)) if self.group else ()


@dataclass(frozen=True)
class UiWindow:
    """One process window's inventory. `id` is the registered id, `action` the action's object name in it, `title`
    the window's title as recorded and `name` that title without the image's name."""

    id: str
    action: str
    title: str
    name: str
    controls: tuple
    path: str


def _ui_window(path, data):
    def fail(message):
        raise GuideDataError('%s: %s' % (path, message))

    if not isinstance(data, dict) or set(data) != {'id', 'window', 'controls'} or not _is_text(data['id']) \
            or not isinstance(data['window'], str) or not isinstance(data['controls'], list):
        fail('an inventory states `id`, `window` and `controls`')
    if '.' not in data['id'] or os.path.basename(path) != data['id'] + '.json':
        fail('the file is named by its `id`, `<category>.<action>`')
    controls = []
    for index, record in enumerate(data['controls']):
        who = 'control %d' % (index + 1)
        if not isinstance(record, dict):
            fail('%s is not a mapping' % who)
        for key, value in record.items():
            if key not in _UI_KEYS:
                fail('%s states the unknown key `%s`' % (who, key))
            if not isinstance(value, _UI_KEYS[key]) or (isinstance(value, bool) and _UI_KEYS[key] is not bool):
                fail('%s: `%s` is %r' % (who, key, value))
        varies = tuple(record.get('varies', ()))
        for key in _UI_REQUIRED:
            if key not in record:
                fail('%s does not state `%s`' % (who, key))
        if record['kind'] not in UI_KINDS:
            fail('%s is of the unknown kind %r' % (who, record['kind']))
        if any(key not in _UI_VARYING or key in record for key in varies):
            fail('%s: `varies` names what the record does not state, among %s' % (who, ', '.join(_UI_VARYING)))
        for key in ('visible', 'default'):
            if key not in record and key not in varies:
                fail('%s does not state `%s`' % (who, key))
        options = record.get('options')
        if options is not None and not all(isinstance(option, str) for option in options):
            fail('%s: `options` is a list of texts' % who)
        controls.append(UiControl(
            index=index, label=record['label'], kind=record['kind'], object=record['object'], group=record['group'],
            visible=record.get('visible'), default=record.get('default'),
            numbers={key: record[key] for key in UI_NUMBERS if key in record}, suffix=record.get('suffix', ''),
            options=tuple(options) if options is not None else None, slider=record.get('slider', False),
            shared=record.get('shared', ''), varies=varies))
    return UiWindow(id=data['id'], action=data['id'].split('.', 1)[1], title=data['window'],
                    name=_UI_IMAGE_IN_TITLE.sub('', data['window']).strip(), controls=tuple(controls), path=path)


def load_ui_inventory(guide_dir):
    """The inventory of the process windows' controls, by the object name of the action that opens each window: the
    files of data/ui/. Empty when the guide has no inventory."""
    folder = os.path.join(guide_dir, UI_INVENTORY)
    windows = {}
    if not os.path.isdir(folder):
        return windows
    for name in sorted(os.listdir(folder)):
        if not name.endswith('.json'):
            continue
        path = os.path.join(folder, name)
        try:
            data = json.loads(read_text(path))
        except (OSError, ValueError) as error:
            raise GuideDataError('%s cannot be read: %s' % (path, error)) from error
        window = _ui_window(path, data)
        if window.action in windows:
            raise GuideDataError('%s and %s are inventories of one action, %s' %
                                 (windows[window.action].path, path, window.action))
        windows[window.action] = window
    return windows


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


def fence_blocks(body, first_line=1):
    """(language, line of the opening fence, source) for every fenced block of a page body that names a language.
    The source is the block's lines as written, indentation included."""
    blocks = []
    fence = None
    language = ''
    opened_at = 0
    lines = []
    for number, line in enumerate(body.split('\n'), start=1):
        opened = _FENCE.match(line)
        if fence is None:
            if opened:
                fence = opened.group(1)
                language = _FENCE_LANGUAGE.match(line[opened.end():]).group(1)
                opened_at = number
                lines = []
            continue
        if opened and opened.group(1)[0] == fence[0] and len(opened.group(1)) >= len(fence) \
                and line.strip() == opened.group(1):
            if language:
                blocks.append((language, opened_at + first_line - 1, '\n'.join(lines)))
            fence = None
            continue
        lines.append(line)
    return blocks


def read_figure(source):
    """A `figure` block as written, without judging it: {'pair', 'images': [{'image', 'alt', 'label'}], 'caption',
    'pins': [numbers], 'owed'}, each text stripped and empty when it is not stated, `owed` true when the block says its
    pictures are not yet taken. None when the block is not a YAML mapping, which the build reports."""
    try:
        data = yaml.load(textwrap.dedent(source), yaml.BaseLoader)
    except yaml.YAMLError:
        return None
    if not isinstance(data, dict):
        return None

    def text(entry, key):
        return entry[key].strip() if isinstance(entry.get(key), str) else ''

    pair = 'images' in data
    if not pair:
        entries = [data]
    elif isinstance(data['images'], list):
        entries = [entry for entry in data['images'] if isinstance(entry, dict)]
    else:
        entries = []
    pins = []
    for pin in data.get('pins') if isinstance(data.get('pins'), list) else []:
        number = pin.get('n') if isinstance(pin, dict) else None
        if isinstance(number, str) and number.strip().isdigit():
            pins.append(int(number))
    return {'pair': pair, 'caption': text(data, 'caption'), 'pins': pins, 'owed': text(data, 'owed') == 'true',
            'images': [{'image': text(entry, 'image'), 'alt': text(entry, 'alt'), 'label': text(entry, 'label')}
                       for entry in entries]}


def read_controls(source):
    """A `controls` block as written, without judging it: {'window': its name or None, 'controls': [...]}, each
    control {'line': its line inside the block, from 1; 'name', 'kind', 'default', 'object': texts, None when not
    stated; 'widget': {key: text}; 'options': [texts] or None}. None when the block is not YAML of that shape, which
    the build reports."""
    try:
        root = yaml.compose(textwrap.dedent(source), Loader=yaml.BaseLoader)
    except yaml.YAMLError:
        return None
    window = None
    entries = root
    if isinstance(root, yaml.MappingNode):
        entries = None
        for key, value in root.value:
            if isinstance(key, yaml.ScalarNode) and key.value == 'window' and isinstance(value, yaml.ScalarNode):
                window = value.value.strip() or None
            elif isinstance(key, yaml.ScalarNode) and key.value == 'controls':
                entries = value
    if not isinstance(entries, yaml.SequenceNode):
        return None
    controls = []
    for entry in entries.value:
        if not isinstance(entry, yaml.MappingNode):
            return None
        control = {'line': entry.start_mark.line + 1, 'name': None, 'kind': None, 'default': None, 'object': None,
                   'widget': {}, 'options': None}
        for key, value in entry.value:
            if not isinstance(key, yaml.ScalarNode):
                continue
            if key.value in ('name', 'kind', 'default', 'object') and isinstance(value, yaml.ScalarNode):
                control[key.value] = value.value if key.value == 'default' else value.value.strip()
            elif key.value == 'widget' and isinstance(value, yaml.MappingNode):
                control['widget'] = {name.value: number.value for name, number in value.value
                                     if isinstance(name, yaml.ScalarNode) and isinstance(number, yaml.ScalarNode)}
            elif key.value == 'options' and isinstance(value, yaml.SequenceNode):
                control['options'] = [option.value for option in value.value if isinstance(option, yaml.ScalarNode)]
        controls.append(control)
    return {'window': window, 'controls': controls}


def figure_images(source, owed=False):
    """The image paths a `figure` block names, in order, when the block's pictures are taken - or, with `owed`, when
    they are owed; none when the block is not valid YAML, which the build reports."""
    figure = read_figure(source)
    if not figure or figure['owed'] != owed:
        return []
    return [entry['image'] for entry in figure['images'] if entry['image']]


def block_texts(source, keys):
    """(line inside the block, from 1; key; text) for every scalar a `controls` or `figure` block states under one of
    `keys`, at any depth: the sentences such a block puts on the page. None of them when the block is not valid YAML,
    which the build reports."""
    try:
        root = yaml.compose(textwrap.dedent(source), Loader=yaml.BaseLoader)
    except yaml.YAMLError:
        return []
    found = []

    def walk(node):
        if isinstance(node, yaml.SequenceNode):
            for item in node.value:
                walk(item)
        elif isinstance(node, yaml.MappingNode):
            for key, value in node.value:
                if isinstance(key, yaml.ScalarNode) and isinstance(value, yaml.ScalarNode):
                    if key.value in keys:
                        found.append((value.start_mark.line + 1, key.value, value.value))
                else:
                    walk(value)

    if root is not None:
        walk(root)
    return found


def meta_images(meta):
    """The images a page's front matter names, relative to the page: the home page's picture."""
    hero = meta.get('hero') if isinstance(meta, dict) else None
    if isinstance(hero, dict) and isinstance(hero.get('image'), str):
        return [hero['image']]
    return []


def h1_lines(body, first_line=1):
    """The line of every level-one heading of a page body: `# ` headings and `<h1>` elements, outside code."""
    lines = []
    for number, line in _outside_code(body):
        if _ATX_H1.match(line) or _HTML_H1.search(line):
            lines.append(number + first_line - 1)
    return lines


def section(body, title, first_line=1):
    """(line of the heading, text) of the first `## <title>` section of a page body, outside code, or None when the
    page has none. The text runs to the next heading of the first or second level, without HTML comments and the blank
    lines around it, so a section that says nothing has an empty text."""
    lines = body.split('\n')
    start = None
    end = len(lines)
    for number, line in _outside_code(body):
        heading = _ATX_SECTION.match(line)
        if not heading:
            continue
        if start is not None:
            end = number - 1
            break
        # The title is compared as written: `_outside_code` blanks code spans, and a title may hold one.
        written = _ATX_SECTION.match(lines[number - 1])
        if len(heading.group(1)) == 2 and written and written.group(2) == title:
            start = number
    if start is None:
        return None
    return start + first_line - 1, _HTML_COMMENT.sub('', '\n'.join(lines[start:end])).strip()


def image_references(body, first_line=1, owed=False):
    """(line, url) for every image a page body shows: Markdown images and `<img src>`, outside code, and the images
    of its `figure` blocks. With `owed`, the images its `figure` blocks name as not yet taken instead."""
    references = []
    for language, line, source in fence_blocks(body, first_line):
        if language == 'figure':
            source_lines = source.split('\n')
            for url in figure_images(source, owed):
                offset = next((index for index, text in enumerate(source_lines, start=1) if url in text), 0)
                references.append((line + offset, url))
    return references if owed else references + inline_images(body, first_line)


def inline_images(body, first_line=1):
    """(line, url) for every image a page body shows without a `figure` block: Markdown images and `<img src>`,
    outside code."""
    references = []
    for number, line in _outside_code(body):
        for match in _MARKDOWN_IMAGE.finditer(line):
            references.append((number + first_line - 1, match.group(1) if match.group(1) is not None
                               else match.group(2)))
        for match in _HTML_IMAGE.finditer(line):
            references.append((number + first_line - 1, match.group(1) if match.group(1) is not None
                               else match.group(2)))
    return references


# --- A page body as what it says --------------------------------------------------------------------------------------

_MATH = re.compile(r'\\\[.*?\\\]|\\\(.*?\\\)|\$\$.*?\$\$', re.DOTALL)
_HEADING = re.compile(r'^ {0,3}(#{1,6})[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$')
_HEADING_ATTRIBUTES = re.compile(r'[ \t]*\{[^{}]*\}$')
_ORDERED_ITEM = re.compile(r'^ {0,3}\d+[.)][ \t]+(.*)$')
_BULLET = re.compile(r'^ {0,3}[-*+][ \t]+(.*)$')
_DEFINITION = re.compile(r'^ {0,3}:[ \t]+(.*)$')
_TABLE_SEPARATOR = re.compile(r'^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$')
_UNESCAPED_PIPE = re.compile(r'(?<!\\)\|')
_CALLOUT = re.compile(r'^\s*(?:!!!|\?\?\?\+?)[ \t]+([A-Za-z0-9_-]+)')
_LINK = re.compile(r'\[((?:[^\]\\]|\\.)*)\]\(\s*(<[^>]*>|[^)\s]*)(?:\s+(?:"[^"]*"|\'[^\']*\'))?\s*\)')
_LINK_TARGET = re.compile(r'(?<=\])\(\s*(?:<[^>]*>|[^)\s]*)(?:\s+(?:"[^"]*"|\'[^\']*\'))?\s*\)')
_BARE_URL = re.compile(r'<[^>\s]*>|\b[a-z][a-z0-9+.-]*://\S+', re.IGNORECASE)
_HTML_TAG = re.compile(r'</?[A-Za-z][^>]*>')
_WORD = re.compile(r'\w')
_MARKUP_PREFIX = re.compile(r'^\s*(?:(?:[-*+]|\d+[.)])[ \t]+(?:\[[ xX]\][ \t]+)?|(?:!!!|\?\?\?\+?)[ \t]+[A-Za-z0-9_-]+|'
                            r'#{1,6}[ \t]+|:[ \t]+|>[ \t]*)')


def _blank(match):
    return re.sub(r'[^\n]', ' ', match.group(0))


def prose_lines(body, first_line=1):
    """(line number, text) for every line of a page body, as what the page says: the lines of fenced blocks are
    empty, and HTML comments and math are blanked, each character but a line end replaced by a space, so a column
    in the text is the column in the file."""
    lines = body.split('\n')
    kept = [''] * len(lines)
    fence = None
    for index, line in enumerate(lines):
        opened = _FENCE.match(line)
        if fence is None:
            if opened:
                fence = opened.group(1)
            else:
                kept[index] = line
        elif opened and opened.group(1)[0] == fence[0] and len(opened.group(1)) >= len(fence) \
                and line.strip() == opened.group(1):
            fence = None
    text = _MATH.sub(_blank, _HTML_COMMENT.sub(_blank, '\n'.join(kept)))
    return [(number + first_line - 1, line) for number, line in enumerate(text.split('\n'), start=1)]


def without_code_spans(text):
    """The text with its code spans blanked."""
    return _CODE_SPAN.sub(_blank, text)


def without_addresses(text):
    """The text with what it does not say blanked: the addresses of its links, bare URLs and HTML tags."""
    return _HTML_TAG.sub(_blank, _BARE_URL.sub(_blank, _LINK_TARGET.sub(_blank, text)))


def links(text):
    """(start and end of the link's words, its address) for every inline link of a line of text."""
    return [(match.start(1), match.end(1), match.group(2).strip('<>')) for match in _LINK.finditer(text)]


def count_words(text):
    """The words of a piece of Markdown: what white space separates and holds a letter or a digit, once the list
    marker, the heading marker or the callout type that opens a line and the addresses of links are set aside. A
    table's rules and a row's bars are no words."""
    words = 0
    for line in text.split('\n'):
        line = _MARKUP_PREFIX.sub('', without_addresses(line), count=1)
        words += sum(1 for token in line.split() if _WORD.search(token))
    return words


@dataclass(frozen=True)
class Heading:
    level: int
    title: str
    line: int


def headings(lines):
    """The headings among `prose_lines`, in order. A title is compared as written, without the attribute list that
    may close it."""
    found = []
    for number, text in lines:
        match = _HEADING.match(text)
        if match:
            found.append(Heading(level=len(match.group(1)), title=_HEADING_ATTRIBUTES.sub('', match.group(2)).strip(),
                                 line=number))
    return found


def section_lines(lines, found, heading):
    """The lines of `prose_lines` under a heading, to the next heading of its level or above."""
    end = next((other.line for other in found if other.line > heading.line and other.level <= heading.level), None)
    return [(number, text) for number, text in lines if number > heading.line and (end is None or number < end)]


def ordered_items(lines):
    """(line, text) of every top-level item of the ordered lists among some lines."""
    return [(number, match.group(1)) for number, text in lines for match in [_ORDERED_ITEM.match(text)] if match]


def bullets(lines):
    """(line, text) of every top-level bullet among some lines."""
    return [(number, match.group(1)) for number, text in lines for match in [_BULLET.match(text)] if match]


def definitions(lines):
    """(line, term, definitions) of every entry of the definition lists among some lines: a term is the line a
    `:   ` line follows, and its definitions are the texts of those lines, each with the lines that continue it."""
    entries = []
    previous = None
    current = None
    for number, text in lines:
        match = _DEFINITION.match(text)
        if match:
            if current is None and previous is not None:
                current = (previous[0], previous[1].strip(), [])
                entries.append(current)
            if current is not None:
                current[2].append(match.group(1))
        elif text.strip():
            if current is not None and text.startswith((' ', '\t')):
                current[2][-1] += ' ' + text.strip()
            else:
                current = None
                previous = (number, text)
    return entries


def _cells(row):
    row = _CODE_SPAN.sub(lambda match: 'x' * len(match.group(0)), row.strip())
    if row.startswith('|'):
        row = row[1:]
    if row.endswith('|') and not row.endswith('\\|'):
        row = row[:-1]
    return len(_UNESCAPED_PIPE.split(row))


def tables(lines):
    """(line of the header, columns, body rows) of every table among some lines."""
    found = []
    index = 0
    while index + 1 < len(lines):
        number, text = lines[index]
        rule = lines[index + 1][1]
        if '|' in text and '-' in rule and _TABLE_SEPARATOR.match(rule) and _cells(text) == _cells(rule):
            rows = 0
            index += 2
            while index < len(lines) and lines[index][1].strip() and '|' in lines[index][1]:
                rows += 1
                index += 1
            found.append((number, _cells(text), rows))
        else:
            index += 1
    return found


def callouts(lines):
    """(line, type) of every callout among some lines: `!!! type` and the folded `??? type`."""
    return [(number, match.group(1).lower()) for number, text in lines for match in [_CALLOUT.match(text)] if match]


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
