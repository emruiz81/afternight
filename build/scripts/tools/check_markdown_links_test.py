# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_markdown_links.

Run with:
    python build/scripts/tools/check_markdown_links_test.py

The cases plant documents in a temporary directory; the last case reads the repository's own.
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_markdown_links as links  # noqa: E402


class LinkCase(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write('docs/dev/GUIDE.md', '# Guide\n')
        self.write('docs/dev/sub/NOTE.md', '# Note\n')
        self.write('docs/dev/images/a b.png', 'png')

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='')

    def problems(self, relative, text):
        self.write(relative, text)
        return links.check_file(self.root, relative)

    def assertClean(self, relative, text):
        self.assertEqual(self.problems(relative, text), [])

    def assertDangling(self, relative, text, target, line=1):
        problems = self.problems(relative, text)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('%s:%d' % (relative, line), problems[0])
        self.assertIn(target, problems[0])


class ResolutionTests(LinkCase):
    def test_a_link_to_a_file_beside_it_resolves(self):
        self.assertClean('docs/dev/README.md', 'See [the guide](GUIDE.md).\n')

    def test_a_link_to_a_missing_file_is_dangling(self):
        self.assertDangling('docs/dev/README.md', 'Intro.\nSee [the notes](NOTES.md).\n', 'NOTES.md', line=2)

    def test_a_relative_link_up_and_down_resolves(self):
        self.assertClean('docs/dev/sub/OTHER.md', '[up](../GUIDE.md) and [here](NOTE.md)\n')
        self.assertDangling('docs/dev/sub/OTHER.md', '[up](../MISSING.md)\n', '../MISSING.md')

    def test_a_link_to_a_directory_resolves_when_the_directory_exists(self):
        self.assertClean('docs/dev/README.md', '[notes](sub/) and [notes](sub)\n')
        self.assertDangling('docs/dev/README.md', '[gone](wip/)\n', 'wip/')

    def test_an_anchor_and_a_query_are_not_part_of_the_path(self):
        self.assertClean('docs/dev/README.md', '[a](GUIDE.md#section-2) and [b](GUIDE.md?plain=1)\n')

    def test_a_root_relative_link_resolves_from_the_repository_root(self):
        self.assertClean('docs/dev/sub/OTHER.md', '[g](/docs/dev/GUIDE.md)\n')

    def test_a_percent_encoded_or_bracketed_path_is_decoded(self):
        self.assertClean('docs/dev/README.md', '![a](images/a%20b.png) and ![b](<images/a b.png>)\n')

    def test_a_reference_definition_is_a_link(self):
        self.assertDangling('docs/dev/README.md', 'See [the notes][n].\n\n[n]: NOTES.md\n', 'NOTES.md', line=3)

    def test_an_image_is_a_link(self):
        self.assertDangling('docs/dev/README.md', '![shot](images/missing.png)\n', 'images/missing.png')


class SkippedTests(LinkCase):
    def test_external_and_in_page_links_are_not_files(self):
        self.assertClean('docs/dev/README.md', '[w](https://example.test/x.md) [m](mailto:a@example.test) [s](#top)\n')

    def test_a_link_in_a_fenced_block_or_a_code_span_is_not_read(self):
        self.assertClean('docs/dev/README.md', '```text\n[x](MISSING.md)\n```\nInline `[y](MISSING.md)` too.\n')

    def test_a_link_text_holding_brackets_is_read(self):
        self.assertDangling('docs/dev/README.md', 'See [the `[x]` rule](MISSING.md).\n', 'MISSING.md')


class TreeTests(LinkCase):
    def test_the_documents_are_those_under_the_roots(self):
        self.write('docs/user-guide/page.md', '[x](MISSING.md)\n')
        self.write('AGENTS.md', '[x](docs/dev/GUIDE.md)\n')
        found = links.documents(self.root, ('docs/dev', 'AGENTS.md'))
        self.assertIn('AGENTS.md', found)
        self.assertIn('docs/dev/GUIDE.md', found)
        self.assertNotIn('docs/user-guide/page.md', found)

    def test_main_reports_every_dangling_link_and_exits_one(self):
        self.write('docs/dev/README.md', '[a](GUIDE.md) [b](GONE.md)\n')
        stream = io.StringIO()
        self.assertEqual(links.main(['--root', str(self.root), 'docs/dev'], stream), 1)
        self.assertIn('docs/dev/README.md:1', stream.getvalue())
        self.assertIn('GONE.md', stream.getvalue())

    def test_main_counts_what_it_read_when_clean(self):
        self.write('docs/dev/README.md', '[a](GUIDE.md)\n')
        stream = io.StringIO()
        self.assertEqual(links.main(['--root', str(self.root), 'docs/dev'], stream), 0)
        self.assertIn('1 link', stream.getvalue())


class StandaloneTests(LinkCase):
    def test_it_runs_beside_only_the_modules_the_public_tree_carries(self):
        tools = self.root / 'tools'
        tools.mkdir()
        here = pathlib.Path(links.__file__).resolve().parent
        for name in ('check_markdown_links.py', 'tree_files.py'):
            shutil.copyfile(here / name, tools / name)
        self.write('docs/dev/README.md', '[a](GUIDE.md)\n')
        ran = subprocess.run([sys.executable, '-I', str(tools / 'check_markdown_links.py'), '--root', str(self.root),
                              'docs/dev'], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stderr + ran.stdout)

    def test_the_root_files_and_templates_are_read_by_default(self):
        for root in ('README.md', 'BUILD.md', 'CONTRIBUTING.md', 'SECURITY.md', 'HISTORY.md', 'CODE_OF_CONDUCT.md',
                     'AGENTS.md', 'CLAUDE.md', '.github'):
            self.assertIn(root, links.DEFAULT_ROOTS)


class RepositoryTests(unittest.TestCase):
    def test_the_developer_documents_link_only_to_what_exists(self):
        problems = []
        for relative in links.documents(links.REPO_ROOT, links.DEFAULT_ROOTS):
            problems.extend(links.check_file(links.REPO_ROOT, relative))
        self.assertEqual(problems, [])


if __name__ == '__main__':
    unittest.main()
