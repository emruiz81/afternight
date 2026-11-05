# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Files kept at the archive tag rather than in the tree, and how the tree names one.

Some files a tool can read are kept at `ARCHIVE_TAG`, a tag of the private repository, and not in the tree. The tree
names such a file `<ARCHIVE_TAG>:<path>`, the spelling `git show` reads. A clone that holds the tag can verify such a
name and read the file; a clone without it - the public repository - cannot, and takes the name as stated. Every
answer here is git's, so a directory that is not a repository, or a host with no git, holds no tag.

Tests: build/scripts/tools/archive_record_test.py
"""

from __future__ import annotations

import pathlib
import subprocess

ARCHIVE_TAG = 'archive/private-record-2026-10'
PREFIX = ARCHIVE_TAG + ':'


def cite(path):
    """The name the tree gives a path kept at the tag."""
    return PREFIX + path


def cited_path(value):
    """The path a `<ARCHIVE_TAG>:<path>` name names, or None when the value is not such a name."""
    if value.startswith(PREFIX) and len(value) > len(PREFIX):
        return value[len(PREFIX):]
    return None


def _git(repo_root, *arguments):
    try:
        return subprocess.run(['git', '-C', str(pathlib.Path(repo_root)), *arguments], capture_output=True)
    except OSError:
        return None


def tag_present(repo_root):
    """Whether this clone holds the archive tag."""
    result = _git(repo_root, 'rev-parse', '--quiet', '--verify', ARCHIVE_TAG + '^{commit}')
    return result is not None and result.returncode == 0


def holds(repo_root, path):
    """Whether the tag holds a path: True or False in a clone with the tag, None in one without it."""
    if not tag_present(repo_root):
        return None
    result = _git(repo_root, 'cat-file', '-e', cite(path))
    return result is not None and result.returncode == 0


def read_text(repo_root, path):
    """A file kept at the tag as text, or None when this clone has no tag or the tag does not hold it."""
    if not tag_present(repo_root):
        return None
    result = _git(repo_root, 'show', cite(path))
    if result is None or result.returncode != 0:
        return None
    return result.stdout.decode('utf-8', errors='replace')
