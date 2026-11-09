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
    keyboard can reach, so a wide table never widens the page."""
    content = re.sub(r'<table(?=[\s>])', '<div class="table-wrap" tabindex="0"><table', content)
    return content.replace('</table>', '</table></div>')


def focusable_scrollers(content):
    """Code blocks and display equations, which scroll sideways when they are wider than the column, reachable by the
    keyboard. The highlighter's empty anchor on each code line (for a link to the line) is kept out of the reading
    order and the tab order: it has no text a screen reader could announce."""
    content = re.sub(r'<pre(?![^>]*\btabindex=)(?=[\s>])', '<pre tabindex="0"', content)
    content = re.sub(r'<a id="(__codelineno-[^"]*)"(?![^>]*\baria-hidden=)',
                     r'<a id="\1" aria-hidden="true" tabindex="-1"', content)
    return re.sub(r'<div class="arithmatex"(?![^>]*\btabindex=)', '<div class="arithmatex" tabindex="0"', content)


def split_lead(content):
    """(the lead before the first `<h2>`, the rest from it)."""
    match = _H2.search(content)
    if not match:
        return content, ''
    return content[:match.start()], content[match.start():]
