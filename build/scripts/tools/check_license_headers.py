# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Fail a source file that does not state its licence and its copyright.

Every C, C++ and Python file AfterNight owns states both in two SPDX lines before its first line of code:

    // SPDX-License-Identifier: GPL-3.0-or-later
    // SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

with `#` in Python. The licence is GPL-3.0-or-later, the program's own, with two kinds of exception:

* **The extension protocol's boundary is Apache-2.0** (`APACHE_BOUNDARY`): the protocol library, the Lite host and the
  protocol half of the Python SDK, so that an extension written against the protocol alone is not bound by the GPL.
  `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md` describes the boundary. Every file there states Apache-2.0, and no
  file outside it does.
* **A derived file states its upstream's terms where they are not GPL-3.0-or-later** (`EXPRESSIONS`), each with its
  reason. A derived file whose upstream grants GPL-3.0-or-later needs no entry; its credit is its module comment, as
  AGENTS.md's "Credit for derived code" requires.

A line counts only as a `//` (or `#`) comment before the first line of code: a directive such as `#pragma once` is
not code, and a module docstring is. A file states one licence. A copyright line names whoever holds a copyright in
the file; the check asks that there is one and leaves its text to its author.

Read: every file of the tree with a suffix in `SUFFIXES`, outside `3rdparty/` and the submodules' checkouts.

`--apply` gives a file with no header both lines, and a file that states its licence but no copyright the copyright
line beneath the licence. The lines go after a byte-order mark, a shebang and a coding declaration, and after an
opening comment that carries a third-party notice - a derived file keeps its upstream's notice first - and otherwise
first. Each change is proved before it is written: a C or C++ file whose new text is anything other than the old one
with header and blank lines inserted, or whose code once its comments are removed differs, is refused, and so is a
Python file whose syntax tree differs. A file stating the wrong licence is reported, never rewritten.

Usage:
    python build/scripts/tools/check_license_headers.py [--root <dir>] [--apply]

Exits 0 when every file states its licence and copyright and every entry of `EXPRESSIONS` names a file that exists,
1 otherwise.

Tests: build/scripts/tools/check_license_headers_test.py
"""

from __future__ import annotations

import argparse
import ast
import difflib
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import source_scan  # noqa: E402
import tree_files  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = 'build/scripts/tools/check_license_headers.py'

SUFFIXES = ('.h', '.hpp', '.c', '.cpp', '.inl', '.py')
EXCLUDED = ('3rdparty/',)

GPL = 'GPL-3.0-or-later'
APACHE = 'Apache-2.0'
COPYRIGHT = 'SPDX-FileCopyrightText: 2026 Ezequiel Ruiz'

# The extension protocol's boundary: a directory ends in a slash, a file is named whole.
APACHE_BOUNDARY = (
    'src/AfterNightExtensionProtocol/',
    'src/AfterNightExtensionHostLite/',
    'python/modules/afternight/__init__.py',
    'python/modules/afternight/lite.py',
    'python/modules/afternight/protocol.py',
    'python/modules/afternight/sequences.py',
    'python/modules/afternight/ui_protocol.py',
)

_NBCOLORMAPPER = ('the layer mapping is a derived work of the NBColorMapper script, Copyright (C) 2023, 2024 Mike '
                  'Cranfield, which states the GNU General Public License version 3 and no later version')
_PALETTES = ('the palettes are a derived work of the narrowband palette process of Seti Astro Suite Pro by Franklin '
             'Marek, whose licence is stated as GPL-3.0 with no later version')
_SPINNER = ('a derived work of QtWaitingSpinner, Copyright (c) 2012-2014 Alexander Turkin, modified by William '
            'Hallatt and Jacob Dawid, whose MIT licence the file keeps with its notice')
_WEIGHTED_MEAN = ('the auto-adaptive weighted mean is adapted from DeepSkyStacker under the BSD 3-Clause licence, '
                  'whose notice THIRD_PARTY_NOTICES.md keeps; the rest of the file is AfterNight\'s')
_STAR_MASK = ('the star mask\'s falloff shapes are adapted from DeepSkyStacker under the BSD 3-Clause licence, whose '
              'notice THIRD_PARTY_NOTICES.md keeps; the rest of the file is AfterNight\'s')
_DRIZZLE = ('the corner interpolation is derived from Siril\'s routine (GPL-3.0-or-later), itself derived from STScI '
            'drizzle under the BSD 3-Clause licence of AURA, whose notice THIRD_PARTY_NOTICES.md keeps')

# A derived file whose upstream's terms are not GPL-3.0-or-later: path -> (SPDX expression, reason).
EXPRESSIONS = {
    'src/AfterNightEngine/include/AfterNightEngine/Algorithms/Color/NarrowbandColorMap.h': ('GPL-3.0-only',
                                                                                             _NBCOLORMAPPER),
    'src/AfterNightEngine/src/algorithms/color/NarrowbandColorMap.cpp': ('GPL-3.0-only', _NBCOLORMAPPER),
    'src/AfterNightEngine/include/AfterNightEngine/Algorithms/Color/NarrowbandPalette.h': ('GPL-3.0-only', _PALETTES),
    'src/AfterNightEngine/src/algorithms/color/NarrowbandPalette.cpp': ('GPL-3.0-only', _PALETTES),
    'src/AfterNightControls/widgets/waitingspinnerwidget.h': ('MIT', _SPINNER),
    'src/AfterNightControls/widgets/waitingspinnerwidget.cpp': ('MIT', _SPINNER),
    'src/AfterNightEngine/src/kernels/reference/StackMethodRow.inl': ('GPL-3.0-or-later AND BSD-3-Clause',
                                                                      _WEIGHTED_MEAN),
    'src/AfterNightEngine/src/backends/cpu/StackMethodCpu.cpp': ('GPL-3.0-or-later AND BSD-3-Clause', _WEIGHTED_MEAN),
    'src/AfterNightBench/common/StackingOracle.cpp': ('GPL-3.0-or-later AND BSD-3-Clause', _WEIGHTED_MEAN),
    'src/AfterNightEngine/include/AfterNightEngine/Algorithms/Stars/StarMask.h': ('GPL-3.0-or-later AND BSD-3-Clause',
                                                                                 _STAR_MASK),
    'src/AfterNightEngine/src/algorithms/stars/StarMask.cpp': ('GPL-3.0-or-later AND BSD-3-Clause', _STAR_MASK),
    'src/AfterNightEngine/src/algorithms/drizzle/DrizzlePixmap.cpp': ('GPL-3.0-or-later AND BSD-3-Clause', _DRIZZLE),
}

IDENTIFIER = re.compile(r'^[ \t]*(//|#)[ \t]*SPDX-License-Identifier:[ \t]*(.*?)[ \t]*$')
COPYRIGHT_LINE = re.compile(r'^[ \t]*(//|#)[ \t]*SPDX-FileCopyrightText:[ \t]*\S')
# What an opening comment says when it carries a third-party notice rather than only describing the file: a copyright
# line, a licence by name, or a sentence that opens by saying where the file came from. "derived from" in the middle
# of a sentence is ordinary prose - a value derived from another - and is not a notice.
NOTICE = re.compile(r'[Cc]opyright\s*(?:\([Cc]\)|©)|[Cc]opyright[^\n]{0,40}\b(?:19|20)\d{2}\b|\bGPL-\d|\bGPLv\d'
                    r'|GNU General Public License|\bLGPL|BSD[ -]\d-Clause|\bCeCILL|MIT [Ll]icen[cs]e'
                    r'|[Pp]ermission is hereby granted|\bDerived from\b')
PYTHON_CODING = re.compile(r'^[ \t\f]*#.*?coding[:=][ \t]*[-\w.]+')
BOM = '﻿'


class Refused(Exception):
    """A header this tool will not write: the file states the wrong licence, or the change is not comment-only."""


def held(relative):
    """Whether the file at a repository-relative path is one the check reads."""
    return relative.endswith(SUFFIXES) and not relative.startswith(EXCLUDED)


def in_apache_boundary(relative):
    return any(relative == entry or (entry.endswith('/') and relative.startswith(entry)) for entry in APACHE_BOUNDARY)


def expected_expression(relative, expressions=None):
    """The SPDX expression the file must state."""
    if in_apache_boundary(relative):
        return APACHE
    entry = (EXPRESSIONS if expressions is None else expressions).get(relative)
    return entry[0] if entry else GPL


def _marker(relative):
    return '#' if relative.endswith('.py') else '//'


def _split(text):
    """The byte-order mark, if any, and the lines with their own endings."""
    bom = BOM if text.startswith(BOM) else ''
    return bom, text[len(bom):].splitlines(keepends=True)


def _bare(line):
    return line.rstrip('\r\n')


def _code_start_c(body):
    """The index of the first line of C or C++ code, directives and their continuations not being code."""
    continued = False
    lines = source_scan.strip_comments(body.replace('\r\n', '\n')).split('\n')
    for index, line in enumerate(lines):
        stripped = line.strip()
        if continued:
            continued = stripped.endswith('\\')
            continue
        if not stripped:
            continue
        if stripped.startswith('#'):
            continued = stripped.endswith('\\')
            continue
        return index
    return len(lines)


def _header_lines(relative, text):
    """(index, line) of every comment line that stands where a header is read, before the first line of code."""
    bom, lines = _split(text)
    if relative.endswith('.py'):
        found = []
        for index, line in enumerate(lines):
            stripped = line.strip()
            if not stripped:
                continue
            if not stripped.startswith('#'):
                break
            found.append((index, _bare(line)))
        return found
    body = text[len(bom):]
    start = _code_start_c(body)
    normalized = body.replace('\r\n', '\n')
    found = []
    line = 0
    last = 0
    for begin, _, kind in source_scan.tokens(normalized):
        line += normalized.count('\n', last, begin)
        last = begin
        if line >= start:
            break
        if kind == 'line comment' and not normalized[normalized.rfind('\n', 0, begin) + 1:begin].strip():
            found.append((line, _bare(lines[line])))
    return found


def _read_header(relative, text):
    identifiers = []
    copyrights = []
    for index, line in _header_lines(relative, text):
        match = IDENTIFIER.match(line)
        if match and match.group(1) == _marker(relative):
            identifiers.append((index, match.group(2)))
        elif COPYRIGHT_LINE.match(line) and line.lstrip().startswith(_marker(relative)):
            copyrights.append(index)
    return identifiers, copyrights


def check_text(relative, text, expressions=None):
    """Every problem with one file's header, as messages naming the file."""
    identifiers, copyrights = _read_header(relative, text)
    expected = expected_expression(relative, expressions)
    if not identifiers and not copyrights:
        return ['%s: states no licence and no copyright before its first line of code' % relative]
    problems = []
    if not identifiers:
        problems.append('%s: states no licence: no SPDX-License-Identifier line stands before its first line of code'
                        % relative)
    elif len(identifiers) > 1:
        problems.append('%s: has two licence lines or more; a file states one licence' % relative)
    elif identifiers[0][1] != expected:
        problems.append('%s: states %s and must state %s' % (relative, identifiers[0][1], expected))
    if not copyrights:
        problems.append('%s: states no copyright: no SPDX-FileCopyrightText line stands before its first line of code'
                        % relative)
    return problems


def _opening_comment_end(lines):
    """The index after the last line of the comment a C or C++ file opens with, 0 when it opens with code."""
    end = 0
    in_block = False
    for index, line in enumerate(lines):
        stripped = line.strip()
        if in_block:
            if '*/' in stripped:
                in_block = False
                if stripped.split('*/', 1)[1].strip():
                    return end
            end = index + 1
            continue
        if not stripped:
            continue
        if stripped.startswith('//'):
            end = index + 1
        elif stripped.startswith('/*'):
            rest = stripped[2:]
            if '*/' in rest:
                if rest.split('*/', 1)[1].strip():
                    return end
            else:
                in_block = True
            end = index + 1
        else:
            return end
    return end


def insert_header(relative, text, expressions=None):
    """The file's text with whatever header it lacks inserted; the text itself when it lacks nothing."""
    identifiers, copyrights = _read_header(relative, text)
    expected = expected_expression(relative, expressions)
    if len(identifiers) > 1:
        raise Refused('%s: has two licence lines or more; correct it by hand' % relative)
    if identifiers and identifiers[0][1] != expected:
        raise Refused('%s: states %s and must state %s; correct it by hand' % (relative, identifiers[0][1], expected))
    if identifiers and copyrights:
        return text
    if copyrights:
        raise Refused('%s: states a copyright and no licence; correct it by hand' % relative)
    bom, lines = _split(text)
    newline = '\r\n' if '\r\n' in text else '\n'
    marker = _marker(relative)
    copyright_line = '%s %s%s' % (marker, COPYRIGHT, newline)
    if identifiers:
        index = identifiers[0][0]
        if not lines[index].endswith(('\n', '\r')):
            lines[index] += newline
        lines.insert(index + 1, copyright_line)
        return bom + ''.join(lines)
    header = ['%s SPDX-License-Identifier: %s%s' % (marker, expected, newline), copyright_line]
    if relative.endswith('.py'):
        position = 0
        if lines and lines[0].startswith('#!'):
            position = 1
        for index in range(min(2, len(lines))):
            if PYTHON_CODING.match(lines[index]):
                position = index + 1
        block = header
    else:
        end = _opening_comment_end(lines)
        if end and NOTICE.search(''.join(lines[:end])):
            position = end
            block = [newline] + header
        else:
            position = 0
            block = header
        following = lines[position] if position < len(lines) else ''
        if following.strip():
            block = block + [newline]
    if position and not lines[position - 1].endswith(('\n', '\r')):
        lines[position - 1] += newline
    lines[position:position] = block
    return bom + ''.join(lines)


def _allowed_insertion(relative, line):
    if not line.strip():
        return True
    marker = _marker(relative)
    stripped = line.strip()
    return stripped.startswith(marker) and bool(IDENTIFIER.match(stripped) or COPYRIGHT_LINE.match(stripped))


def _code_lines(text):
    return [line.rstrip() for line in source_scan.strip_comments(text.replace('\r\n', '\n')).split('\n')
            if line.strip()]


def prove_comment_only(relative, old, new):
    """Raise `Refused` unless `new` is `old` with header and blank lines inserted, and its code unchanged."""
    old_bom, old_lines = _split(old)
    new_bom, new_lines = _split(new)
    if old_bom != new_bom:
        raise Refused('%s: the byte-order mark moved' % relative)
    old_bare = [_bare(line) for line in old_lines]
    new_bare = [_bare(line) for line in new_lines]
    matcher = difflib.SequenceMatcher(None, old_bare, new_bare, autojunk=False)
    for tag, _, _, start, end in matcher.get_opcodes():
        if tag == 'equal':
            continue
        if tag != 'insert':
            raise Refused('%s: the change is not only inserted lines' % relative)
        for line in new_bare[start:end]:
            if not _allowed_insertion(relative, line):
                raise Refused('%s: an inserted line is not a header line: %r' % (relative, line))
    if relative.endswith('.py'):
        try:
            before = ast.dump(ast.parse(old.encode('utf-8', 'surrogateescape')))
            after = ast.dump(ast.parse(new.encode('utf-8', 'surrogateescape')))
        except (SyntaxError, ValueError) as error:
            raise Refused('%s: does not parse, so the change cannot be proved: %s' % (relative, error))
        if before != after:
            raise Refused('%s: the syntax tree changed' % relative)
    elif _code_lines(old[len(old_bom):]) != _code_lines(new[len(new_bom):]):
        raise Refused('%s: the code changed, not only its comments' % relative)


def _read(path):
    return path.read_bytes().decode('utf-8', 'surrogateescape')


def _register_problems(root, expressions):
    problems = []
    for relative, (expression, _) in sorted(expressions.items()):
        where = '%s: EXPRESSIONS' % SCRIPT
        if not (root / relative).is_file():
            problems.append('%s names a file that does not exist, %s; remove the entry with the file' % (where, relative))
        elif in_apache_boundary(relative):
            problems.append('%s names %s, which is on the Apache-2.0 boundary and states nothing else' % (where, relative))
        elif expression == GPL:
            problems.append('%s names %s with the program\'s own licence, which needs no entry' % (where, relative))
    return problems


def run(root=REPO_ROOT, apply=False, expressions=None, stream=None):
    """Check, or with `apply` repair, every file of the tree under `root`; returns the exit code."""
    root = pathlib.Path(root)
    expressions = EXPRESSIONS if expressions is None else expressions
    stream = stream or sys.stdout
    files = [relative for relative in tree_files.list_files(root) if held(relative)]
    refused = []
    written = 0
    if apply:
        for relative in files:
            path = root / relative
            old = _read(path)
            try:
                new = insert_header(relative, old, expressions)
                if new == old:
                    continue
                prove_comment_only(relative, old, new)
            except Refused as error:
                refused.append(str(error))
                continue
            path.write_bytes(new.encode('utf-8', 'surrogateescape'))
            written += 1
    problems = _register_problems(root, expressions)
    for relative in files:
        problems.extend(check_text(relative, _read(root / relative), expressions))
    for message in refused:
        print('refused: %s' % message, file=stream)
    for problem in problems:
        print(problem, file=stream)
    if apply:
        print('%d file(s) given a header, %d refused' % (written, len(refused)), file=stream)
    if problems or refused:
        print('%d problem(s) in %d file(s) read' % (len(problems), len(files)), file=stream)
        return 1
    print('%d file(s) state their licence and copyright' % len(files), file=stream)
    return 0


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--root', default=str(REPO_ROOT), help='the tree to read')
    parser.add_argument('--apply', action='store_true', help='insert every missing header, proving each change')
    arguments = parser.parse_args(argv)
    return run(arguments.root, apply=arguments.apply, stream=stream)


if __name__ == '__main__':
    sys.exit(main())
