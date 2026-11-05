# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for tree_files.

Run with:
    python build/scripts/tools/tree_files_test.py
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

import tree_files  # noqa: E402


class TreeFilesCase(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, relative, text='x\n'):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='')

    def test_a_walked_tree_skips_git_and_submodules(self):
        self.write('.gitmodules', '[submodule "vcpkg"]\n\tpath = vcpkg\n\turl = https://example.test/vcpkg.git\n')
        self.write('vcpkg/scripts/a.cmake')
        self.write('.git-not-a-repository/x')
        self.write('src/a.cpp', 'int a;\n')
        files = tree_files.list_files(self.root)
        self.assertIn('src/a.cpp', files)
        self.assertIn('.gitmodules', files)
        self.assertFalse(any(f.startswith('vcpkg/') for f in files), files)
        self.assertEqual(files, sorted(files))

    def test_a_repository_lists_tracked_and_unignored_files(self):
        if shutil.which('git') is None:
            self.skipTest('git is not on the path')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.write('.gitignore', 'ignored.txt\n')
        self.write('tracked.py')
        self.write('ignored.txt')
        self.write('new/untracked.h')
        subprocess.run(['git', '-C', str(self.root), 'add', 'tracked.py', '.gitignore'], check=True)
        files = tree_files.list_files(self.root)
        self.assertEqual(files, ['.gitignore', 'new/untracked.h', 'tracked.py'])

    def test_a_tracked_file_deleted_from_the_working_tree_is_not_listed(self):
        if shutil.which('git') is None:
            self.skipTest('git is not on the path')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        self.write('gone.py')
        subprocess.run(['git', '-C', str(self.root), 'add', 'gone.py'], check=True)
        (self.root / 'gone.py').unlink()
        self.assertEqual(tree_files.list_files(self.root), [])


if __name__ == '__main__':
    unittest.main()
