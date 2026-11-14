#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the product's sources to the include discipline of their libraries.

A lower library that included a header of the library built on top of it would need the higher one to compile,
so a source of a product library includes no header of a product target above it - `AfterNightControls` none of
`AfterNightProcesses`, `AfterNightShellCore`, a shell, the application or the full host; `AfterNightProcesses` none
of the last four; `AfterNightShellCore`, the application without a window that every shell is built on, no shell,
the application or the full host; and a shell - `AfterNightShellClassic` - neither the application nor the full
host. An include is resolved the way the compiler searches - beside the including file,
then the library's own root, then the roots below it - so a path that two libraries hold is the including
library's own.

`check_build_graph.py` holds the generated projects to the same order.

Usage:
    python build/scripts/tools/check_consumer_layering.py [--repo-root <dir>]

Exits 0 when every rule holds and 1 otherwise, printing one line per problem and what each rule read.

Tests: build/scripts/tools/check_consumer_layering_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import consumer_source  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
PLATFORM_INCLUDE = 'src/AfterNightPlatform/include'

# Each product library held to the layer order: its source root, the roots below it that its includes may
# resolve into besides its own and the platform library's, and the targets above it.
LAYERS = (
    ('AfterNightControls', 'src/AfterNightControls', (),
     ('AfterNightProcesses', 'AfterNightShellCore', 'AfterNightShellClassic', 'AfterNightApp', 'AfterNightExtensionHost')),
    ('AfterNightProcesses', 'src/AfterNightProcesses', ('src/AfterNightControls',),
     ('AfterNightShellCore', 'AfterNightShellClassic', 'AfterNightApp', 'AfterNightExtensionHost')),
    ('AfterNightShellCore', 'src/AfterNightShellCore', ('src/AfterNightProcesses', 'src/AfterNightControls'),
     ('AfterNightShellClassic', 'AfterNightApp', 'AfterNightExtensionHost')),
    ('AfterNightShellClassic', 'src/AfterNightShellClassic',
     ('src/AfterNightShellCore', 'src/AfterNightProcesses', 'src/AfterNightControls'),
     ('AfterNightApp', 'AfterNightExtensionHost')),
)

_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.MULTILINE)


def _relative(repo_root: pathlib.Path, path: pathlib.Path) -> str:
    return pathlib.Path(os.path.relpath(path, repo_root)).as_posix()


def _includes(path: pathlib.Path) -> list[tuple[int, str]]:
    """(line, included name) for every include directive outside a comment."""
    text = consumer_source.without_comments(consumer_source.read_text(path))
    return [(text.count('\n', 0, match.start()) + 1, match.group(2).strip()) for match in _INCLUDE.finditer(text)]


def _resolve(repo_root: pathlib.Path, including: pathlib.Path, name: str, roots) -> pathlib.Path | None:
    """The first file an include names, searched beside the including file and then in each root."""
    for base in (including.parent,) + tuple(repo_root / root for root in roots):
        candidate = pathlib.Path(os.path.normpath(base / name))
        if candidate.is_file():
            return candidate
    return None


def _inside(repo_root: pathlib.Path, path: pathlib.Path, root: str) -> bool:
    return _relative(repo_root, path).startswith(root + '/')


def _sources(repo_root: pathlib.Path, root: str) -> list[pathlib.Path]:
    base = repo_root / root
    if not base.is_dir():
        return []
    # Ordered by the forward-slashed components, case and all: a Windows path compares without case, and the
    # registers built from this order have to come out the same on every host.
    return sorted((path for path in base.rglob('*') if path.is_file() and path.suffix in consumer_source.SOURCE_SUFFIXES),
                  key=lambda path: path.as_posix().split('/'))


# ---- The layer order.

def check_layers(repo_root: pathlib.Path) -> tuple[list[str], int]:
    """Problems, and how many sources of the layered libraries were read."""
    problems = []
    read = 0
    for project, root, below, above in LAYERS:
        allowed = (root,) + below + (PLATFORM_INCLUDE,)
        forbidden = tuple(('src/' + name, name) for name in above)
        for path in _sources(repo_root, root):
            read += 1
            relative = _relative(repo_root, path)
            for line, name in _includes(path):
                resolved = _resolve(repo_root, path, name, allowed)
                if resolved is None:
                    resolved = _resolve(repo_root, path, name, tuple(upper for upper, _ in forbidden))
                if resolved is None:
                    continue
                for upper, owner in forbidden:
                    if _inside(repo_root, resolved, upper):
                        problems.append(f'{relative}:{line} includes {name} from {owner}; {project} includes only '
                                        'its own headers and those of the libraries below it')
    return problems, read


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT))
    args = parser.parse_args(argv)
    repo_root = pathlib.Path(args.repo_root)
    problems = []
    counts = []
    for label, check in (('layered library sources', check_layers),):
        found, read = check(repo_root)
        problems.extend(found)
        counts.append(f'{read} {label}')
    for problem in problems:
        print(problem)
    print('read ' + ', '.join(counts))
    if problems:
        print(f'{len(problems)} layering problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
