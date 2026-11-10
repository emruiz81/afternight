# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The guide's two fenced blocks: `controls`, a window's controls drawn as the widgets they are, and `figure`, a
screenshot with its caption and numbered pins, or a before-and-after pair.

Both hold YAML, read with PyYAML's base loader, so every scalar arrives as text: `Off`, `Yes` and `No` stay the words
a window shows. Numbers are converted where a number is meant (`widget`, a pin's place).

A control is called by the label its window gives it. `object` names the control in the inventory of the window's
controls (`data/ui/`) when the label cannot: the window has several controls of that label - a row of three boxes
under one caption - or gives the control no caption at all. It is for the check that holds the block to the window,
and draws nothing; a name may be listed more than once in a block only when `object` tells each of them apart. A spin
box or a slider states the numbers of its range under `widget`; one whose range follows the image it works on - a
crop cannot be wider than the picture - says so in `range` instead. A `readout` is what the window shows and takes
no input - a count beside a control, a plot, a status line: it sits in the block where the window shows it, and it
is no control of the inventory.

````markdown
```controls
window: Histogram Transformation
controls:
  - name: Shadows
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    effect: Higher maps more low samples to black and rescales the rest.
```

```figure
image: ../../assets/images/processes/stretching/histogram-transformation.webp
alt: The Histogram Transformation window.
caption: The source histogram below, the predicted result above.
pins:
  - {n: 2, x: 91.0, y: 27.5}
```
````

The hooks register the two formatters with pymdownx.superfences in `on_config` and call `begin_page` before each
page renders, so a formatter knows the page it is on and the line of its block. SuperFences swallows whatever a
formatter raises and leaves the block as stray text, so a formatter never raises: a block that cannot render becomes
a visible error box and a warning through the `guide` plugin logger, which names the page and the line and which
`mkdocs build --strict` counts.

Tests: docs/user-guide/hooks/guide_fences_test.py
"""

from __future__ import annotations

import html
import os
import re
import textwrap
from dataclasses import dataclass, field

import yaml
from mkdocs.plugins import get_plugin_logger

import guide_model

try:  # the names the keys extension gives the keys, so a facet's key caps read as the prose's do
    from pymdownx import keymap_db as _keymap_db
    _KEY_NAMES, _KEY_ALIASES = _keymap_db.keymap, _keymap_db.aliases
except ImportError:  # pragma: no cover - the extension is part of the documentation toolchain
    _KEY_NAMES, _KEY_ALIASES = {}, {}

log = get_plugin_logger('guide')

CONTROL_KINDS = {
    'spinbox': 'spin box',
    'slider': 'slider',
    'combo': 'combo box',
    'check': 'check box',
    'radio': 'radio buttons',
    'toolbutton': 'toolbar button',
    'button': 'button',
    'field': 'text field',
    'action': 'menu action',
    # What the window shows and takes no input: a count, a plot, a status line. It is no control, so the inventory
    # of the window's controls does not have it and the check does not look for it there.
    'readout': 'readout',
}
CONTROL_REQUIRED = ('name', 'kind', 'default', 'effect')
# The facets a control may state beside the required ones, in the order a row shows them, with their labels.
CONTROL_FACETS = (
    ('range', 'Range'),
    ('enabled', 'Enabled'),
    ('tradeoff', 'Trade-off'),
    ('scope', 'Scope'),
    ('persistence', 'Persistence'),
    ('note', 'Note'),
)
CONTROL_KEYS = CONTROL_REQUIRED + ('widget', 'options', 'object') + tuple(key for key, _ in CONTROL_FACETS)
WIDGET_KEYS = ('min', 'max', 'step', 'decimals')
RANGED_KINDS = ('spinbox', 'slider')
CHOICE_KINDS = ('combo', 'radio')
CHECKED_WORDS = ('on', 'checked', 'true', 'yes', 'selected', 'enabled')
FIGURE_KEYS = ('image', 'alt', 'caption', 'pins')
PAIR_KEYS = ('images', 'caption')
PAIR_ENTRY_KEYS = ('image', 'alt', 'label')
PIN_KEYS = ('n', 'x', 'y')

_SLUG_DROP = re.compile(r'[^a-z0-9]+')
_SCHEME = re.compile(r'^[A-Za-z][A-Za-z0-9+.-]*:')
# What a facet takes of Markdown: a code span, bold text - which may hold an asterisk, as the label CIE L* does -
# and key caps written as the pages write them, `++ctrl+shift+k++`.
_INLINE = re.compile(r'`([^`]+)`|\*\*(.+?)\*\*(?!\*)|\+\+([A-Za-z0-9][A-Za-z0-9+-]*?)\+\+')


class FenceError(Exception):
    """A block that cannot render. `line` is the line of the problem inside the block, from 1, when it is known."""

    def __init__(self, message, line=None):
        super().__init__(message)
        self.line = line


@dataclass
class _Page:
    """The page being rendered: its name in messages, its folder and the docs folder, and its blocks' lines."""

    name: str = ''
    folder: str = ''
    docs: str = ''
    blocks: list = field(default_factory=list)
    used_ids: set = field(default_factory=set)


_page = _Page()


def begin_page(name, folder, docs, markdown, first_line=1):
    """Called before a page renders: `markdown` is its body, which starts at `first_line` of its file."""
    global _page
    _page = _Page(name=name, folder=folder, docs=docs,
                  blocks=[[language, line, textwrap.dedent(source).strip(), False]
                          for language, line, source in guide_model.fence_blocks(markdown, first_line)
                          if language in ('controls', 'figure')])


def _block_line(language, source):
    """The line of the opening fence of the block being rendered: the next block of the page with this source, or
    failing that the next one of its language. None when the page's blocks are unknown."""
    wanted = source.strip()
    candidates = [block for block in _page.blocks if block[0] == language and not block[3]]
    for block in candidates:
        if block[2] == wanted:
            block[3] = True
            return block[1]
    if candidates:
        candidates[0][3] = True
        return candidates[0][1]
    return None


# --- Reading ---------------------------------------------------------------------------------------------------------

def _construct(node):
    """A YAML node as plain data whose mappings and sequences remember the line they start on (`.line`, from 1)."""
    if isinstance(node, yaml.ScalarNode):
        return node.value
    if isinstance(node, yaml.SequenceNode):
        items = _List(_construct(item) for item in node.value)
        items.line = node.start_mark.line + 1
        return items
    mapping = _Dict()
    mapping.line = node.start_mark.line + 1
    for key_node, value_node in node.value:
        if not isinstance(key_node, yaml.ScalarNode):
            raise FenceError('a key is not plain text', key_node.start_mark.line + 1)
        if key_node.value in mapping:
            raise FenceError('`%s` is stated twice' % key_node.value, key_node.start_mark.line + 1)
        mapping[key_node.value] = _construct(value_node)
    return mapping


class _List(list):
    line = None


class _Dict(dict):
    line = None


def _load(source):
    try:
        node = yaml.compose(source, Loader=yaml.BaseLoader)
    except yaml.YAMLError as error:
        mark = getattr(error, 'problem_mark', None)
        raise FenceError('the block is not valid YAML: %s' % (getattr(error, 'problem', None) or error),
                         mark.line + 1 if mark is not None else None) from error
    if node is None:
        raise FenceError('the block is empty')
    return _construct(node)


def _text(value):
    return isinstance(value, str) and value.strip() != ''


def _number(value):
    """A number written in a block, or None."""
    if not isinstance(value, str):
        return None
    try:
        number = float(value.strip())
    except ValueError:
        return None
    return number if number == number and number not in (float('inf'), float('-inf')) else None


def parse_controls(source):
    """A `controls` block as {'window': name or None, 'controls': [control, ...]}; FenceError when it is not one."""
    data = _load(source)
    if isinstance(data, list):
        window, controls = None, data
    elif isinstance(data, dict):
        unknown = [key for key in data if key not in ('window', 'controls')]
        if unknown:
            raise FenceError('unknown key `%s`; a block states `window` and `controls`' % unknown[0], data.line)
        window, controls = data.get('window'), data.get('controls')
        if not _text(window):
            raise FenceError('`window` names the window or tab the controls are on', data.line)
        if not isinstance(controls, list):
            raise FenceError('`controls` is a list of controls', data.line)
    else:
        raise FenceError('the block is a list of controls, or a mapping of `window` and `controls`')
    if not controls:
        raise FenceError('the block lists no control', getattr(controls, 'line', None))
    listed = {}  # a name -> the objects it was listed with, None for none
    for index, control in enumerate(controls, start=1):
        if not isinstance(control, dict):
            raise FenceError('control %d is not a mapping of keys to values' % index, getattr(controls, 'line', None))
        _check_control(control, index)
        objects = listed.setdefault(control['name'], [])
        stated = control.get('object')
        if objects and (stated is None or None in objects or stated in objects):
            raise FenceError('control "%s" is listed twice in the block%s' % (
                control['name'], '' if stated in objects and stated is not None else
                '; when the window has several controls of one label, each states its `object`'), control.line)
        objects.append(stated)
    return {'window': window.strip() if window else None, 'controls': controls}


def _check_control(control, index):
    name = control.get('name')
    who = 'control "%s"' % name if _text(name) else 'control %d' % index

    def fail(message):
        raise FenceError('%s: %s' % (who, message), control.line)

    for key in CONTROL_REQUIRED:
        if key not in control:
            fail('`%s` is missing' % key)
    for key in control:
        if key not in CONTROL_KEYS:
            fail('unknown key `%s`' % key)
    for key in control:
        if key not in ('widget', 'options', 'default') and not _text(control[key]):
            fail('`%s` is text' % key)
    if not isinstance(control['default'], str):
        fail('`default` is the text the control shows when the window opens')
    kind = control['kind']
    if kind not in CONTROL_KINDS:
        fail('unknown kind `%s`; it is one of %s' % (kind, ', '.join(CONTROL_KINDS)))
    widget = control.get('widget')
    if widget is not None:
        if kind not in RANGED_KINDS:
            fail('`widget` belongs to a spinbox or a slider')
        if not isinstance(widget, dict):
            fail('`widget` is a mapping of %s' % ', '.join(WIDGET_KEYS))
        for key, value in widget.items():
            if key not in WIDGET_KEYS:
                fail('unknown `widget` key `%s`' % key)
            if _number(value) is None:
                fail('`widget.%s` is %r, not a number' % (key, value))
    if kind in RANGED_KINDS:
        widget = widget or {}
        if ('min' not in widget or 'max' not in widget) and 'range' not in control:
            fail('a %s needs `widget.min` and `widget.max`, or a `range` that says what bounds it when the image '
                 'does' % kind)
        low = _number(widget['min']) if 'min' in widget else None
        high = _number(widget['max']) if 'max' in widget else None
        if low is not None and high is not None and low > high:
            fail('`widget.min` %s is above `widget.max` %s' % (widget['min'], widget['max']))
        default = _number(control['default'])
        if default is not None and ((low is not None and default < low) or (high is not None and default > high)):
            fail('the default %s is outside %s' % (control['default'], _bounds(widget)))
    options = control.get('options')
    if options is not None:
        if kind not in CHOICE_KINDS:
            fail('`options` belongs to a combo or a radio')
        if not isinstance(options, list) or not options or not all(_text(option) for option in options):
            fail('`options` is a list of the choices, as the window words them')
        if len(set(options)) != len(options):
            fail('`options` repeats a choice')
    if kind in CHOICE_KINDS:
        if options is None:
            fail('a %s needs `options`' % kind)
        if control['default'] not in options:
            fail('the default "%s" is not among its options (%s)' % (control['default'], ', '.join(options)))


def _bounds(widget):
    """The range a `widget` states, as words: both ends, or the one it has."""
    if 'min' in widget and 'max' in widget:
        return '%s to %s' % (widget['min'].strip(), widget['max'].strip())
    if 'min' in widget:
        return 'from %s' % widget['min'].strip()
    if 'max' in widget:
        return 'up to %s' % widget['max'].strip()
    return ''


def parse_figure(source, folder, docs):
    """A `figure` block as {'images': [{image, alt, label, width, height}], 'caption', 'pins': [(n, x, y)]}.
    `folder` is the page's folder and `docs` the docs folder, both on disk; FenceError when it is not a figure."""
    data = _load(source)
    if not isinstance(data, dict):
        raise FenceError('the block is a mapping: `image`, `alt`, `caption` and optionally `pins`, or `images` and '
                         '`caption` for a before-and-after pair')
    pair = 'images' in data
    allowed = PAIR_KEYS if pair else FIGURE_KEYS
    for key in data:
        if key not in allowed:
            raise FenceError('unknown key `%s`%s' % (key, '; a pair states `images` and `caption`' if pair else ''),
                             data.line)
    if not _text(data.get('caption')):
        raise FenceError('`caption` is missing: say what the picture shows', data.line)
    if pair:
        entries = data['images']
        if not isinstance(entries, list) or len(entries) != 2 or not all(isinstance(entry, dict) for entry in entries):
            raise FenceError('`images` is a pair: two entries of `image`, `alt` and `label`', data.line)
        for entry in entries:
            for key in entry:
                if key not in PAIR_ENTRY_KEYS:
                    raise FenceError('unknown key `%s` in an entry of `images`' % key, entry.line)
            if not _text(entry.get('label')):
                raise FenceError('an entry of `images` has no `label`, such as Before or After', entry.line)
    else:
        entries = [data]
    images = []
    for entry in entries:
        if not _text(entry.get('image')):
            raise FenceError('`image` is missing', entry.line)
        if not _text(entry.get('alt')):
            raise FenceError('`alt` is missing: describe the picture for a reader who cannot see it', entry.line)
        width, height = _image_size(entry['image'].strip(), folder, docs, entry.line)
        images.append({'image': entry['image'].strip(), 'alt': entry['alt'].strip(),
                       'label': entry.get('label', '').strip() if pair else '', 'width': width, 'height': height})
    pins = []
    if data.get('pins') is not None:
        if not isinstance(data['pins'], list):
            raise FenceError('`pins` is a list of {n, x, y}', data.line)
        for pin in data['pins']:
            if not isinstance(pin, dict) or set(pin) != set(PIN_KEYS):
                raise FenceError('a pin states `n`, `x` and `y`', getattr(pin, 'line', None) or data['pins'].line)
            number, x, y = _number(pin['n']), _number(pin['x']), _number(pin['y'])
            if number is None or number != int(number) or number < 1:
                raise FenceError('a pin\'s `n` is %r; pins are numbered from 1' % pin['n'], pin.line)
            if x is None or y is None or not (0 <= x <= 100 and 0 <= y <= 100):
                raise FenceError('pin %d is placed at x %s, y %s; both are percentages from 0 to 100 of the picture\'s '
                                 'width and height' % (number, pin['x'], pin['y']), pin.line)
            if any(int(number) == other[0] for other in pins):
                raise FenceError('pin %d is placed twice' % number, pin.line)
            pins.append((int(number), x, y))
    return {'images': images, 'caption': data['caption'].strip(), 'pins': pins, 'pair': pair}


def _image_size(image, folder, docs, line):
    if _SCHEME.match(image) or image.startswith('/'):
        raise FenceError('image %s is not a path relative to the page' % image, line)
    path = os.path.normpath(os.path.join(folder, image.replace('/', os.sep)))
    if os.path.relpath(path, docs).startswith('..'):
        raise FenceError('image %s lies outside the docs folder' % image, line)
    if not os.path.isfile(path):
        raise FenceError('image %s does not exist' % image, line)
    try:
        size = guide_model.image_size(path)
    except OSError as error:
        raise FenceError('image %s cannot be read: %s' % (image, error), line) from error
    if size is None:
        raise FenceError('image %s is not a PNG, WebP or JPEG file' % image, line)
    return size


# --- Drawing ---------------------------------------------------------------------------------------------------------

def _e(text):
    return html.escape(str(text), quote=True)


def _key_caps(keys):
    """`ctrl+shift+k` as the key caps the keys extension draws in the pages' prose, with its names for the keys."""
    caps = []
    for name in keys.split('+'):
        name = name.lower()
        label = _KEY_NAMES.get(_KEY_ALIASES.get(name, name)) or (name.upper() if len(name) == 1 else name.capitalize())
        caps.append('<kbd>%s</kbd>' % _e(label))
    return '<span class="keys">%s</span>' % '<span>+</span>'.join(caps)


def inline(text):
    """A facet's text as HTML: escaped, with `code`, **bold** and ++key+caps++ and nothing else of Markdown."""
    def mark(match):
        if match.group(1) is not None:
            return '<code>%s</code>' % match.group(1)
        if match.group(2) is not None:
            return '<strong>%s</strong>' % match.group(2)
        return _key_caps(match.group(3))
    return _INLINE.sub(mark, _e(text.strip()))


def slug(text):
    return _SLUG_DROP.sub('-', text.lower()).strip('-') or 'control'


def _unique_id(base, used):
    identifier, count = base, 1
    while identifier in used:
        count += 1
        identifier = '%s-%d' % (base, count)
    used.add(identifier)
    return identifier


def _plain(number_text):
    return _e(number_text.strip())


def _widget_html(control):
    """The control drawn as the widget it is, at its default, and the words that say the same."""
    kind, default = control['kind'], control['default'].strip()
    widget = control.get('widget') or {}
    drawn, said = '', ''
    if kind in RANGED_KINDS:
        # The handle sits where the default lies in the range; a range the image bounds has no place to draw it at.
        low = _number(widget['min']) if 'min' in widget else None
        high = _number(widget['max']) if 'max' in widget else None
        value = _number(default)
        track = ''
        if value is not None and low is not None and high is not None:
            position = 0.0 if high == low else (value - low) / (high - low) * 100
            track = ('<span class="slider"><b style="width:%.4g%%"></b><i style="left:%.4g%%"></i></span>'
                     % (position, position))
        box = '<span class="spin"><span class="v">%s</span><span class="arr"><i></i><i></i></span></span>' % _e(default)
        drawn = box + track if kind == 'spinbox' else track + '<span class="v">%s</span>' % _e(default)
        said = 'Default <b>%s</b>' % _e(default)
        if _bounds(widget):
            said += ' &middot; %s' % _e(_bounds(widget))
        if 'step' in widget:
            said += ' &middot; step %s' % _plain(widget['step'])
        if 'decimals' in widget:
            said += ' &middot; %s decimals' % _plain(widget['decimals'])
    elif kind == 'combo':
        drawn = '<span class="combo"><span>%s</span><i></i></span>' % _e(default)
        said = 'Default <b>%s</b>' % _e(default)
    elif kind == 'radio':
        said = 'Default <b>%s</b>' % _e(default)
    elif kind == 'check':
        on = default.lower() in CHECKED_WORDS
        drawn = '<span class="box%s"></span><span class="v">%s</span>' % (' on' if on else '', _e(control['name']))
        said = 'Default <b>%s</b>' % _e(default)
    elif kind == 'toolbutton':
        drawn = '<span class="tbtn"><i></i></span>'
        said = 'Default <b>%s</b>' % _e(default) if default else ''
    elif kind == 'field':
        drawn = '<span class="fld">%s</span>' % (_e(default) if default else '&nbsp;')
        said = 'Default <b>%s</b>' % _e(default) if default else 'Empty by default'
    elif kind == 'readout':
        drawn = '<span class="fld ro">%s</span>' % _e(default) if default else ''
        said = 'Opens with <b>%s</b>' % _e(default) if default else ''
    else:  # button, action
        drawn = '<span class="abtn%s">%s</span>' % (' menu' if kind == 'action' else '', _e(control['name']))
        said = 'Default <b>%s</b>' % _e(default) if default else ''
    parts = []
    if drawn:
        parts.append('<span class="draw" aria-hidden="true">%s</span>' % drawn)
    if said:
        parts.append('<span class="state">%s</span>' % said)
    if kind in CHOICE_KINDS:
        parts.append('<ul class="opts%s" aria-label="Choices">%s</ul>' % (
            ' radio' if kind == 'radio' else '',
            ''.join('<li%s>%s</li>' % (' class="on"' if option == control['default'] else '', _e(option))
                    for option in control['options'])))
    return '<div class="w">%s</div>' % ''.join(parts)


def render_controls(block, used_ids=None):
    """A parsed `controls` block as HTML: one row per control, in a `<section class="ctls">`."""
    used_ids = used_ids if used_ids is not None else set()
    rows = []
    for control in block['controls']:
        name = control['name'].strip()
        base = 'ctl-' + slug(name)
        if base in used_ids and block['window']:
            base = 'ctl-%s-%s' % (slug(block['window']), slug(name))
        identifier = _unique_id(base, used_ids)
        facets = ''.join('<dt>%s</dt><dd%s>%s</dd>' % (label, ' class="warn"' if key == 'tradeoff' else '',
                                                    inline(control[key]))
                         for key, label in CONTROL_FACETS if key in control)
        rows.append(
            '<div class="ctl" id="%s"><div class="lab"><b>%s</b><small>%s</small></div>'
            '<div class="body">%s<p class="eff">%s</p>%s</div></div>'
            % (identifier, _e(name), CONTROL_KINDS[control['kind']], _widget_html(control), inline(control['effect']),
               '<dl>%s</dl>' % facets if facets else ''))
    if block['window']:
        label = 'Controls of %s' % block['window']
        head = '<p class="ctls-window">%s</p>' % _e(block['window'])
    else:
        label, head = 'Controls', ''
    return '<section class="ctls" aria-label="%s">%s%s</section>' % (_e(label), head, ''.join(rows))


def _shot(image, pins=()):
    marks = ''.join('<span class="pin" style="left:%.4g%%;top:%.4g%%"><span class="visually-hidden">Step </span>%d'
                    '</span>' % (x, y, number) for number, x, y in pins)
    label = '<span class="pair-label">%s</span>' % _e(image['label']) if image['label'] else ''
    # The link is the lightbox's: with it in place the lightbox plugin leaves the image alone, and the pins stay over
    # the picture they belong to.
    return ('<div class="shotwrap"><a class="glightbox" href="%s" data-type="image"><img src="%s" alt="%s" width="%d" '
            'height="%d" loading="lazy"></a>%s%s</div>'
            % (_e(image['image']), _e(image['image']), _e(image['alt']), image['width'], image['height'], marks, label))


def render_figure(figure):
    """A parsed `figure` block as HTML: `<figure class="an-figure">` with the picture, its pins and its caption."""
    if figure['pair']:
        inner = '<div class="pair">%s</div>' % ''.join(_shot(image) for image in figure['images'])
        classes = 'an-figure an-pair'
    else:
        inner = _shot(figure['images'][0], figure['pins'])
        classes = 'an-figure'
    return '<figure class="%s">%s<figcaption>%s</figcaption></figure>' % (classes, inner, inline(figure['caption']))


def error_box(language, message):
    """What a block that cannot render leaves on the page, so a preview shows it."""
    return ('<div class="fence-error" role="alert"><b>This %s block cannot be drawn.</b> %s</div>'
            % (_e(language), _e(message)))


def _fail(language, source, error):
    line = _block_line(language, source)
    if line is not None and getattr(error, 'line', None):
        line += error.line
    where = '%s:%d' % (_page.name, line) if line is not None else (_page.name or 'a page')
    log.warning('%s: %s block: %s', where, language, error)
    return error_box(language, str(error))


def controls_format(source, language, class_name, options, md, **kwargs):
    """The SuperFences formatter of `controls`."""
    try:
        block = parse_controls(source)
        _block_line(language, source)
        return render_controls(block, _page.used_ids)
    except Exception as error:  # SuperFences would swallow it and leave the block as stray text.
        return _fail('controls', source, error)


def figure_format(source, language, class_name, options, md, **kwargs):
    """The SuperFences formatter of `figure`."""
    try:
        figure = parse_figure(source, _page.folder, _page.docs)
        _block_line(language, source)
        return render_figure(figure)
    except Exception as error:  # SuperFences would swallow it and leave the block as stray text.
        return _fail('figure', source, error)


def custom_fences():
    """The entries the hooks add to pymdownx.superfences' `custom_fences`."""
    return [{'name': 'controls', 'class': 'ctls', 'format': controls_format},
            {'name': 'figure', 'class': 'an-figure', 'format': figure_format}]
