# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Reading C and C++ source as code: comments and literals found by one token walk.

Several checks read the tree for what its code says - a name it spells, an include it makes, a declaration it
carries - and each must not mistake prose or a literal for code. They share this walk rather than a regular
expression each, because the shapes that defeat one are all in this tree: an escaped quote, a character literal
holding a quote, a raw string holding both, and a `//` inside a URL.

Tests: build/scripts/tools/source_scan_test.py
"""

import re

# Everything a C++ scan has to stop at: the two comment openers and the three literal openers. One
# search jumps from one to the next, so the ordinary text between them is copied in whole spans
# rather than a character at a time - the difference between reading this tree in seconds and in
# minutes.
INTERESTING = re.compile(r'//|/\*|(?:u8|u|U|L)?R"[^()\\ ]*\(|"|\'')


def strip_comments(text):
    """The source with comments blanked and its line count preserved.

    Line numbers stay true because every removed newline is put back, and prose stops being read as
    code, which is the difference between this check and a grep.

    Literals are walked rather than ignored, because a regular expression that removes from `//` to
    the end of the line also removes everything after a `"http://..."` - and what it removes is
    code, so the failure is a dependency this check would not report.
    """
    result = []
    index = 0
    for start, end, kind in tokens(text):
        result.append(text[index:start])
        if kind == 'line comment':
            pass
        elif kind == 'block comment':
            result.append('\n' * text.count('\n', start, end))
        else:
            result.append(text[start:end])
        index = end
    result.append(text[index:])
    return ''.join(result)


def tokens(text):
    """Every comment and literal in source order, as (start, end, kind).

    One regular expression finds the next opener and the closer is found with `str.find`, so the
    cost is proportional to the number of tokens rather than to the number of characters.
    """
    index = 0
    length = len(text)
    while index < length:
        match = INTERESTING.search(text, index)
        if match is None:
            return
        start = match.start()
        opener = match.group(0)
        if opener == '//':
            end = text.find('\n', start)
            end = length if end < 0 else end
            yield start, end, 'line comment'
        elif opener == '/*':
            end = text.find('*/', start + 2)
            end = length if end < 0 else end + 2
            yield start, end, 'block comment'
        elif opener.endswith('('):
            closing = ')%s"' % opener[opener.index('"') + 1:-1]
            end = text.find(closing, match.end())
            end = length if end < 0 else end + len(closing)
            yield start, end, 'literal'
        else:
            end = quoted_end(text, start, opener)
            if end is None:
                # An unterminated quote on one line is punctuation this file writes, not a literal.
                index = start + 1
                continue
            yield start, end, 'literal'
        index = end


def quoted_end(text, start, quote):
    """One past a quoted literal's closing quote, or None when the line ends before it."""
    index = start + 1
    length = len(text)
    while index < length and text[index] != quote:
        if text[index] == '\n':
            return None
        index += 2 if text[index] == '\\' else 1
    return min(index + 1, length)


def blank_literals(text):
    """The source with the contents of every literal blanked, and those literals with their lines.

    The same token walk the comment stripper uses, because the shapes that defeat a plain regular
    expression are all in this tree: an escaped quote, a character literal holding a quote, and a
    raw string holding both.
    """
    code = []
    literals = []
    index = 0
    line = 1
    for start, end, kind in tokens(text):
        code.append(text[index:start])
        line += text.count('\n', index, start)
        body = text[start:end]
        if kind == 'literal':
            literals.append((line, body))
        code.append(' ' * (len(body) - body.count('\n')) + '\n' * body.count('\n'))
        line += body.count('\n')
        index = end
    code.append(text[index:])
    return ''.join(code), literals
