# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The guide's search index: one entry per section of every page, written as a script.

A page opened from disk cannot `fetch()` a file or start a worker, so the index is a script that sets
`window.__guideSearchIndex`, which the theme's search.js loads with a `<script>` element on the first use of the search
box, and indexes with lunr. An entry is a page's lead (the text before its first `<h2>`) or one of its `<h2>` and
`<h3>` sections, with no page-level copy of the text.

Tests: docs/user-guide/hooks/guide_search_test.py
"""

from __future__ import annotations

import json
import re

import guide_html

_HEADING = re.compile(r'<h(?P<level>[23])(?P<attrs>[^>]*)>(?P<inner>.*?)</h(?P=level)>', re.DOTALL)
_ID = re.compile(r'\bid="([^"]*)"')
_HEADERLINK = re.compile(r'<a class="headerlink"[^>]*>.*?</a>', re.DOTALL)
# What a reader does not search for: code, diagrams' source, scripts and styles, and equations' TeX.
_UNSEARCHED = re.compile(
    r'<(pre|script|style)\b[^>]*>.*?</\1>'
    r'|<span class="arithmatex"[^>]*>.*?</span>'
    r'|<div class="arithmatex"[^>]*>.*?</div>', re.DOTALL)


def _searchable_text(fragment):
    return guide_html.text_of(_UNSEARCHED.sub(' ', _HEADERLINK.sub('', fragment)))


def sections(content, location, page_title, section, menu=None):
    """The search entries of one page's content, its H1 already lifted: its lead, then one entry per `<h2>` and
    `<h3>` with an id. `location` is the page's URL relative to the site's root."""
    entries = []

    def add(where, title, fragment):
        text = _searchable_text(fragment)
        if not text and where != location:
            return
        entry = {'l': where, 't': title, 'p': page_title, 's': section, 'x': text}
        if menu:
            entry['m'] = menu
        entries.append(entry)

    matches = list(_HEADING.finditer(content))
    add(location, page_title, content[:matches[0].start()] if matches else content)
    for index, match in enumerate(matches):
        identifier = _ID.search(match.group('attrs'))
        end = matches[index + 1].start() if index + 1 < len(matches) else len(content)
        title = guide_html.text_of(_HEADERLINK.sub('', match.group('inner')))
        if identifier:
            add('%s#%s' % (location, identifier.group(1)), title, content[match.end():end])
    return entries


def index_script(entries):
    """The index as a script that sets `window.__guideSearchIndex`, ASCII only, so the page's encoding never matters."""
    payload = json.dumps({'version': 1, 'entries': entries}, ensure_ascii=True, separators=(',', ':'))
    return 'window.__guideSearchIndex=%s;\n' % payload
