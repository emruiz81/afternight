# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for archive_record.

Run with:
    python build/scripts/tools/archive_record_test.py

Each case builds a throwaway repository with git, because what the module answers is what git answers about a tag.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import archive_record  # noqa: E402


def git(root, *arguments):
    return subprocess.run(['git', '-C', str(root), '-c', 'user.name=t', '-c', 'user.email=t@example.test',
                           *arguments], capture_output=True, text=True, check=True).stdout


@unittest.skipIf(shutil.which('git') is None, 'git is not on PATH')
class ArchiveRecordTest(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(self._remove, self.root)
        git(self.root, 'init', '-q')
        self.write('docs/run/README.md', '# a run\n')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'a file')

    @staticmethod
    def _remove(root):
        for path in sorted(root.rglob('*'), reverse=True):
            if path.is_dir():
                path.rmdir()
            else:
                # Git writes its objects read-only, and Windows does not unlink a read-only file.
                path.chmod(0o600)
                path.unlink()
        root.rmdir()

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def tag_and_remove_the_file(self):
        git(self.root, 'tag', archive_record.ARCHIVE_TAG)
        git(self.root, 'rm', '-q', '-r', 'docs/run')
        git(self.root, 'commit', '-q', '-m', 'the file leaves the tree')

    def test_a_name_at_the_tag_names_a_path(self):
        self.assertEqual(archive_record.cited_path(archive_record.ARCHIVE_TAG + ':docs/run/README.md'),
                         'docs/run/README.md')
        self.assertIsNone(archive_record.cited_path('docs/run/README.md'))
        self.assertIsNone(archive_record.cited_path(archive_record.ARCHIVE_TAG + ':'))

    def test_a_name_at_the_tag_is_spelled_as_git_reads_it(self):
        self.assertEqual(archive_record.cite('docs/run/README.md'), archive_record.ARCHIVE_TAG + ':docs/run/README.md')

    def test_a_clone_without_the_tag_cannot_tell(self):
        self.assertFalse(archive_record.tag_present(self.root))
        self.assertIsNone(archive_record.holds(self.root, 'docs/run/README.md'))
        self.assertIsNone(archive_record.read_text(self.root, 'docs/run/README.md'))

    def test_a_clone_with_the_tag_reads_a_file_the_tree_no_longer_holds(self):
        self.tag_and_remove_the_file()
        self.assertFalse((self.root / 'docs/run/README.md').exists())
        self.assertTrue(archive_record.tag_present(self.root))
        self.assertTrue(archive_record.holds(self.root, 'docs/run/README.md'))
        self.assertEqual(archive_record.read_text(self.root, 'docs/run/README.md').replace('\r\n', '\n'), '# a run\n')

    def test_a_path_the_tag_does_not_hold_is_not_there(self):
        self.tag_and_remove_the_file()
        self.assertFalse(archive_record.holds(self.root, 'docs/run/MISSING.md'))
        self.assertIsNone(archive_record.read_text(self.root, 'docs/run/MISSING.md'))

    def test_a_directory_that_is_not_a_repository_has_no_tag(self):
        plain = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, plain, True)
        self.assertFalse(archive_record.tag_present(plain))
        self.assertIsNone(archive_record.holds(plain, 'docs/run/README.md'))


if __name__ == '__main__':
    unittest.main()
