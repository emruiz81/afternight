#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Fail the developer documents for a link that leads nowhere.

A link in a developer document is a promise that a reader can follow it, and a moved or removed file breaks the
promise silently - the most common way documentation goes stale. This reads every Markdown document under the roots
it is given - by default `docs/dev/`, the contributor guides, the repository's root documents and the `.github`
templates - and every link in them: inline links, images and reference definitions, outside fenced blocks and code
spans. A link to a web address or to an anchor of the same page is not a file and is not read; any other target, its
anchor and query dropped and its percent-encoding decoded, must name a file or directory that exists, relative to the
document or, with a leading `/`, to the repository root.

`check_engine_documentation.py` holds the engine's guides to the identifiers and paths their code spans name; this
holds every developer document to its links. The user guide's links are held by its strict MkDocs build.

Usage:
    python build/scripts/tools/check_markdown_links.py [--root <dir>] [<document or directory>...]

Exits 0 when every link resolves, 1 otherwise, printing one line per dangling link.

Tests: build/scripts/tools/check_markdown_links_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import posixpath
import re
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import tree_files  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_ROOTS = ('docs/dev', 'AGENTS.md', 'CLAUDE.md', 'README.md', 'BUILD.md', 'CONTRIBUTING.md', 'SECURITY.md',
                 'HISTORY.md', 'CODE_OF_CONDUCT.md', '.github')

FENCE = re.compile(r'^\s*(```|~~~)')
CODE_SPAN = re.compile(r'(`+)(.+?)\1')
# Link text may hold one level of brackets, as `[the [x] rule](target)` does.
INLINE = re.compile(r'!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*(<[^>]*>|[^)\s]+)(?:\s+"[^"]*")?\s*\)')
DEFINITION = re.compile(r'^ {0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)')
EXTERNAL = re.compile(r'^[A-Za-z][A-Za-z0-9+.-]*:')


def documents(root, roots):
    """The Markdown documents of the tree under the given roots, each a file or a directory, sorted."""
    roots = [r.rstrip('/') for r in roots]
    return [path for path in tree_files.list_files(root)
            if path.endswith('.md') and any(path == r or path.startswith(r + '/') for r in roots)]


def _targets(text):
    """(line number, target) for every link outside fenced blocks and code spans."""
    fenced = False
    for number, line in enumerate(text.replace('\r\n', '\n').split('\n'), start=1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced:
            continue
        line = CODE_SPAN.sub(lambda match: ' ' * len(match.group(0)), line)
        for match in INLINE.finditer(line):
            yield number, match.group(1)
        definition = DEFINITION.match(line)
        if definition:
            yield number, definition.group(1)


def _resolves(root, relative, target):
    target = target.strip('<>')
    if not target or EXTERNAL.match(target) or target.startswith('#'):
        return True
    path = urllib.parse.unquote(target.split('#', 1)[0].split('?', 1)[0])
    if not path:
        return True
    if path.startswith('/'):
        resolved = posixpath.normpath(path.lstrip('/'))
    else:
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative), path))
    return not resolved.startswith('..') and (pathlib.Path(root) / resolved).exists()


def _read(root, relative):
    """The dangling links of one document, and how many links it holds."""
    text = (pathlib.Path(root) / relative).read_text(encoding='utf-8', errors='replace')
    problems = []
    count = 0
    for number, target in _targets(text):
        count += 1
        if not _resolves(root, relative, target):
            problems.append('%s:%d: %r does not resolve' % (relative, number, target))
    return problems, count


def check_file(root, relative):
    """Every dangling link of one document."""
    return _read(root, relative)[0]


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--root', default=str(REPO_ROOT), help='the repository to read')
    parser.add_argument('roots', nargs='*', default=list(DEFAULT_ROOTS),
                        help='documents or directories to read, relative to the root')
    arguments = parser.parse_args(argv)
    stream = stream or sys.stdout
    problems = []
    count = 0
    found = documents(arguments.root, arguments.roots)
    for relative in found:
        dangling, links = _read(arguments.root, relative)
        problems.extend(dangling)
        count += links
    for problem in problems:
        print(problem, file=stream)
    if problems:
        print('%d dangling link(s) in %d document(s)' % (len(problems), len(found)), file=stream)
        return 1
    print('%d link(s) in %d document(s) resolve' % (count, len(found)), file=stream)
    return 0


if __name__ == '__main__':
    sys.exit(main())
