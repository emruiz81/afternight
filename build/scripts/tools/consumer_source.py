# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Read a product source file the way the tools that hold the product read it.

Several tools read the product's C++ sources - the process-state check finds every object with process
lifetime, the target registry's check reads what a target's sources include, and the layering check
reads every include of a product library. Each has to stop reading prose as code, and each has to agree
with the others about what the text of a file is, so the reading is written once here.

The product's sources differ from the engine's in three ways that matter to a lexical reader: many of
them begin with a byte-order mark, which hides a first-line `namespace` from a rule anchored at the
start of a line; several hold whole style sheets or scripts in raw string literals, whose braces and
semicolons read as declarations; and all of them are CRLF. `code_only` answers all three by blanking
comments and every kind of literal - raw, prefixed, character and ordinary - through the shared lexical
walk of `source_scan.py`, keeping every newline so a line number stays true.
`without_comments` keeps the literals, for a reader that needs a string's contents: an include names
its header in one.

Tests: build/scripts/tools/consumer_source_test.py
"""

from __future__ import annotations

import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import source_scan  # noqa: E402

SOURCE_SUFFIXES = ('.h', '.hpp', '.inl', '.cpp', '.c')


def read_text(path: pathlib.Path) -> str:
    """The file's text with any byte-order mark removed and line endings normalized to LF."""
    text = path.read_bytes().decode('utf-8', errors='replace').replace('\r\n', '\n')
    return text[1:] if text.startswith('﻿') else text


def _blank(text: str, kinds: tuple[str, ...]) -> str:
    pieces = []
    cursor = 0
    for start, end, kind in source_scan.tokens(text):
        if kind not in kinds:
            continue
        pieces.append(text[cursor:start])
        pieces.append(''.join('\n' if character == '\n' else ' ' for character in text[start:end]))
        cursor = end
    pieces.append(text[cursor:])
    return ''.join(pieces)


def code_only(text: str) -> str:
    """The text with comments and literals blanked, newlines kept."""
    return _blank(text, ('line comment', 'block comment', 'literal'))


def without_comments(text: str) -> str:
    """The text with comments blanked and literals kept, newlines kept."""
    return _blank(text, ('line comment', 'block comment'))
