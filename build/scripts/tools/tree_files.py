# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The files of a tree, as the checks that read a whole tree list them.

A repository lists its tracked files and the untracked ones it does not ignore, so a file is read before it is
committed; a directory that is not a repository - an exported tree, a test's temporary one - is walked, without `.git`
and without the checkouts of the submodules its `.gitmodules` names. Either way a path is repository-relative, with
forward slashes, and names a regular file.

Tests: build/scripts/tools/tree_files_test.py
"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess


def submodule_paths(root):
    """The directories the tree's `.gitmodules` names, each with a trailing slash."""
    path = pathlib.Path(root) / '.gitmodules'
    if not path.is_file():
        return []
    return [match.strip().rstrip('/') + '/' for match in
            re.findall(r'^\s*path\s*=\s*(.+)$', path.read_text(encoding='utf-8', errors='replace'), re.MULTILINE)]


def list_files(root):
    """Every file of the tree as a repository-relative path, sorted."""
    root = pathlib.Path(root)
    if (root / '.git').exists():
        listed = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others',
                                 '--exclude-standard'], capture_output=True)
        if listed.returncode == 0:
            names = {name for name in listed.stdout.decode('utf-8').split('\0') if name}
            return sorted(name for name in names if (root / name).is_file())
    skipped = ['.git/'] + submodule_paths(root)
    found = []
    for directory, subdirectories, files in os.walk(root):
        relative = pathlib.Path(directory).relative_to(root).as_posix()
        prefix = '' if relative == '.' else relative + '/'
        subdirectories[:] = [name for name in subdirectories if prefix + name + '/' not in skipped]
        found.extend(prefix + name for name in files)
    return sorted(found)
