# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The passes the guide's theme makes over a page's rendered HTML.

They rely on the shape of Python-Markdown's output at the versions the guide pins (an `<h1 id>` with a permalink
anchor, plain `<table>` and `<pre>` elements), and guide_html_test.py pins that shape.

Tests: docs/user-guide/hooks/guide_html_test.py
"""

from __future__ import annotations

import html
import re

_H1 = re.compile(r'<h1(?P<attrs>[^>]*)>(?P<inner>.*?)</h1>\s*', re.DOTALL)
_ID = re.compile(r'\bid="([^"]*)"')
_HEADERLINK = re.compile(r'<a class="headerlink"[^>]*>.*?</a>', re.DOTALL)
_TAG = re.compile(r'<[^>]+>')
_SPACE = re.compile(r'\s+')
_H2 = re.compile(r'<h2[\s>]')
_H2_FULL = re.compile(r'<h2(?P<attrs>[^>]*)>(?P<inner>.*?)</h2>', re.DOTALL)
_H3 = re.compile(r'<h3[\s>]')
_TABLE = re.compile(r'<table(?=[\s>]).*?</table>', re.DOTALL)
_ROW = re.compile(r'<tr[\s>].*?</tr>', re.DOTALL)
_CELL = re.compile(r'<t[hd][\s>]')
_LIST_OPEN = {'ol': re.compile(r'<ol(?P<attrs>[^>]*)>'), 'ul': re.compile(r'<ul(?P<attrs>[^>]*)>')}
_CLASS = re.compile(r'\bclass="([^"]*)"')
_DL = re.compile(r'<dl>(?P<inner>.*?)</dl>', re.DOTALL)
_TERM_GROUP = re.compile(r'(?:<dt[\s>].*?</dt>\s*)+(?:<dd[\s>].*?</dd>\s*)*', re.DOTALL)
_FIGURE = re.compile(r'<figure class="an-figure(?P<classes>[^"]*)">.*?</figure>\s*', re.DOTALL)
_PIN = re.compile(r'<span class="pin"[^>]*>(?:<span[^>]*>[^<]*</span>)?(\d+)</span>')
_CONTROL = re.compile(r'<div class="ctl" id="(?P<id>[^"]+)"><div class="lab"><b>(?P<name>.*?)</b>', re.DOTALL)

# A table this wide cannot be read in the page's column at the body's size.
WIDE_TABLE_COLUMNS = 6

# The sections the passes dress, by the words of their `<h2>`, lower-cased.
STEPS = 'steps'
TROUBLE = ('when it goes wrong', 'troubleshooting')
NEED = 'what you need'
BRANCHES = ('your camera', 'your data')
RESULT = 'the result'
NEXT = 'next'


def text_of(fragment):
    """The visible text of an HTML fragment: tags dropped, entities decoded, white space collapsed."""
    return _SPACE.sub(' ', html.unescape(_TAG.sub(' ', fragment))).strip()


def lift_h1(content):
    """(the H1's inner HTML, its text, its id, the content without it). The theme's page head renders the H1, so the
    content keeps the rest; a page without an H1 comes back unchanged with empty title parts."""
    match = _H1.search(content)
    if not match:
        return '', '', '', content
    inner = _HEADERLINK.sub('', match.group('inner')).strip()
    identifier = _ID.search(match.group('attrs'))
    rest = content[:match.start()] + content[match.end():]
    return inner, text_of(inner), identifier.group(1) if identifier else '', rest


def wrap_tables(content):
    """Every table inside a `.table-wrap` that scrolls sideways when the table is wider than the column, and that the
    keyboard can reach, so a wide table never widens the page. A table of WIDE_TABLE_COLUMNS columns or more is
    marked `wide`: the theme sets it smaller and keeps its first column in view while the rest scrolls."""
    def wrap(match):
        first_row = _ROW.search(match.group(0))
        columns = len(_CELL.findall(first_row.group(0))) if first_row else 0
        classes = 'table-wrap wide' if columns >= WIDE_TABLE_COLUMNS else 'table-wrap'
        return '<div class="%s" tabindex="0">%s</div>' % (classes, match.group(0))
    return _TABLE.sub(wrap, content)


def focusable_scrollers(content):
    """Code blocks and display equations, which scroll sideways when they are wider than the column, reachable by the
    keyboard. The highlighter's empty anchor on each code line (for a link to the line) is kept out of the reading
    order and the tab order: it has no text a screen reader could announce."""
    content = re.sub(r'<pre(?![^>]*\btabindex=)(?=[\s>])', '<pre tabindex="0"', content)
    content = re.sub(r'<a id="(__codelineno-[^"]*)"(?![^>]*\baria-hidden=)',
                     r'<a id="\1" aria-hidden="true" tabindex="-1"', content)
    return re.sub(r'<div class="arithmatex"(?![^>]*\btabindex=)', '<div class="arithmatex" tabindex="0"', content)


def _map_sections(content, dress):
    """The content with each `<h2>` section's body replaced by `dress(title, body)`, where `title` is the heading's
    words, lower-cased, and `body` everything from the heading's end to the next `<h2>`. `dress` returns the new body,
    or None to leave it."""
    headings = list(_H2_FULL.finditer(content))
    if not headings:
        return content
    parts = [content[:headings[0].start()]]
    for index, heading in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(content)
        body = content[heading.end():end]
        title = text_of(_HEADERLINK.sub('', heading.group('inner'))).lower()
        dressed = dress(title, body)
        parts.append(heading.group(0))
        parts.append(body if dressed is None else dressed)
    return ''.join(parts)


def _class_first(body, tag, name):
    """The body with `name` added to the classes of its first `<tag>` list; unchanged when it has none."""
    match = _LIST_OPEN[tag].search(body)
    if not match:
        return body
    attrs = match.group('attrs')
    existing = _CLASS.search(attrs)
    if existing:
        attrs = attrs[:existing.start(1)] + existing.group(1) + ' ' + name + attrs[existing.end(1):]
    else:
        attrs = ' class="%s"' % name + attrs
    return '%s<%s%s>%s' % (body[:match.start()], tag, attrs, body[match.end():])


def _trouble_cards(body):
    """Every definition list of the body as cards: `<dl class="tsc">`, each symptom and its cause and fix grouped
    in a `<div class="ts">`."""
    def cards(match):
        inner = _TERM_GROUP.sub(lambda group: '<div class="ts">%s</div>\n' % group.group(0).rstrip(),
                                match.group('inner'))
        return '<dl class="tsc">%s</dl>' % inner
    return _DL.sub(cards, body)


def _tutorial_steps(body):
    """A tutorial's Steps section: each `<h3>` step an item of `<ol class="tsteps">`, beside the section's figure when
    the text before the first step has one (`<div class="tut">`, the figure first so that a narrow page shows it
    before the steps). A step whose number the figure does not pin is marked `nopin`."""
    starts = [match.start() for match in _H3.finditer(body)]
    if not starts:
        return None
    intro = body[:starts[0]]
    figure = _FIGURE.search(intro)
    pinned = set()
    side = ''
    if figure:
        intro = intro[:figure.start()] + intro[figure.end():]
        side = figure.group(0).rstrip().replace('<figure class="an-figure', '<figure class="fig an-figure', 1)
        pinned = {int(number) for number in _PIN.findall(side)}
    items = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(body)
        marked = ' class="nopin"' if side and index + 1 not in pinned else ''
        items.append('<li%s>%s</li>' % (marked, body[start:end].strip()))
    return '%s<div class="tut%s">%s<ol class="tsteps">\n%s\n</ol></div>\n' % (
        intro, '' if side else ' nofig', side, '\n'.join(items))


def dress_sections(content, kind):
    """The sections of a page that the theme draws as components, found by their `<h2>`:

    - on a process page, the first ordered list under "Steps" becomes the step cards (`ol.steps`);
    - on a tutorial or workflow, the first list under "What you need" becomes the checklist (`ul.need`), the first
      under "Your camera" or "Your data" the branch cards (`ul.branches`), the `<h3>` steps under "Steps" the step
      list beside its figure, the body of "The result" the result box (`div.result`), and the first list under
      "Next" the next cards (`ul.nextg`);
    - on every page, the definition lists under "When it goes wrong" or "Troubleshooting" become the troubleshooting
      cards (`dl.tsc`)."""
    spanning = kind in ('tutorial', 'workflow')

    def dress(title, body):
        if title in TROUBLE:
            return _trouble_cards(body)
        if kind == 'process' and title == STEPS:
            return _class_first(body, 'ol', 'steps')
        if not spanning:
            return None
        if title == NEED:
            return _class_first(body, 'ul', 'need')
        if title in BRANCHES:
            return _class_first(body, 'ul', 'branches')
        if title == STEPS:
            return _tutorial_steps(body)
        if title == RESULT:
            return '\n<div class="result">%s</div>\n' % body.strip() if body.strip() else None
        if title == NEXT:
            return _class_first(body, 'ul', 'nextg')
        return None

    return _map_sections(content, dress)


def controls_of(content):
    """(id, name) of every control row the page's `controls` blocks drew, in order."""
    return [(match.group('id'), text_of(match.group('name'))) for match in _CONTROL.finditer(content)]


def accent(title_html, words):
    """The title with the first occurrence of `words` drawn in the accent color; unchanged when it has none."""
    if not words:
        return title_html
    escaped = html.escape(words, quote=False)
    index = title_html.find(escaped)
    if index < 0:
        return title_html
    return '%s<em>%s</em>%s' % (title_html[:index], escaped, title_html[index + len(escaped):])


def split_lead(content):
    """(the lead before the first `<h2>`, the rest from it)."""
    match = _H2.search(content)
    if not match:
        return content, ''
    return content[:match.start()], content[match.start():]
