# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_hooks: fixture sites built by MkDocs with the guide's theme and hooks.

Run with the documentation toolchain's Python, which has MkDocs:
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import json
import logging
import os
import pathlib
import shutil
import tempfile
import unittest

from mkdocs.commands.build import build
from mkdocs.config import load_config
from mkdocs.exceptions import Abort, PluginError

HOOKS = pathlib.Path(__file__).resolve().parent
GUIDE = HOOKS.parent

CONFIG = """\
site_name: Fixture guide
docs_dir: docs
site_dir: site
use_directory_urls: false
copyright: Copyright fixture
repo_url: https://github.com/example/example
edit_uri: edit/main/docs/
theme:
  name: null
  custom_dir: {theme}
  static_templates: [404.html]
  locale: en
  logo: logo.png
  favicon: logo.png
hooks:
  - {hooks}
plugins: []
markdown_extensions:
  - tables
  - toc:
      permalink: true
nav:
  - Home: index.md
  - Processes:
      - Overview: processes/index.md
      - Stretching:
          - Histogram Transformation: processes/ht.md
  - Reference:
      - Glossary: reference/glossary.md
"""

PAGES = {
    'index.md': '---\nkind: home\n---\n# The guide\n\nWelcome.\n',
    'processes/index.md': '---\nkind: catalog\nsummary: Every process.\n---\n# Processes\n\nAll of them.\n',
    'processes/ht.md': ('---\nkind: process\nstage: stretch\nmenu: Process > Intensity Transformations > Histogram '
                        'Transformation\nsummary: Stretches the image.\n---\n# Histogram Transformation\n\n'
                        'Opening.\n\n## Steps\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n'),
    'reference/glossary.md': '---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n\n## Linear\n\nData.\n',
}


class FixtureCase(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        (self.root / 'data').mkdir()
        shutil.copyfile(GUIDE / 'data' / 'stages.yml', self.root / 'data' / 'stages.yml')
        for relative, text in PAGES.items():
            self.page(relative, text)
        shutil.copyfile(GUIDE / 'docs' / 'en' / 'assets' / 'images' / 'interface' / 'logo.png',
                        self.root / 'docs' / 'logo.png')
        (self.root / 'mkdocs.yml').write_text(CONFIG.format(theme=(GUIDE / 'theme').as_posix(),
                                                            hooks=(HOOKS / 'guide_hooks.py').as_posix()),
                                              encoding='utf-8')
        self.previous_version = os.environ.pop('AFTERNIGHT_GUIDE_VERSION', None)
        self.addCleanup(self.restore_version)

    def restore_version(self):
        os.environ.pop('AFTERNIGHT_GUIDE_VERSION', None)
        if self.previous_version is not None:
            os.environ['AFTERNIGHT_GUIDE_VERSION'] = self.previous_version

    def page(self, relative, text):
        path = self.root / 'docs' / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def build(self, strict=True):
        config = load_config(config_file=str(self.root / 'mkdocs.yml'), strict=strict)
        build(config)

    def site(self, relative):
        return (self.root / 'site' / relative).read_text(encoding='utf-8')


class CleanBuildTests(FixtureCase):
    def test_a_valid_guide_builds_strictly_in_the_theme(self):
        self.build()
        html = self.site('processes/ht.html')
        self.assertEqual(html.count('<h1'), 1)
        self.assertIn('<h1 id="histogram-transformation">Histogram Transformation</h1>', html)
        self.assertIn('class="wrap layout"', html)
        self.assertNotIn('search/main.js', html)
        self.assertIn('<p class="summary">Stretches the image.</p>', html)
        self.assertIn('Process &gt; Intensity Transformations &gt; Histogram Transformation', html)
        self.assertIn('aria-current="step"', html)
        self.assertIn('<div class="table-wrap" tabindex="0"><table>', html)
        self.assertIn('--stage-count:8;--s1:#60A5FA', html)
        self.assertIn('development build', html)
        self.assertIn('../assets/guide/css/tokens.css', html)

    def test_each_kind_gets_its_layout_and_stepper(self):
        self.build()
        self.assertIn('stepper-big', self.site('index.html'))
        self.assertIn('stepper-filter', self.site('processes/index.html'))
        self.assertIn('stepper-plain', self.site('reference/glossary.html'))
        self.assertIn('kind-reference', self.site('reference/glossary.html'))

    def test_the_search_index_has_an_entry_per_section(self):
        self.build()
        script = self.site('search/search_index.js')
        payload = json.loads(script[len('window.__guideSearchIndex='):script.rindex(';')])
        locations = [entry['l'] for entry in payload['entries']]
        self.assertIn('processes/ht.html', locations)
        self.assertIn('processes/ht.html#steps', locations)
        self.assertIn('reference/glossary.html#linear', locations)
        ht = next(entry for entry in payload['entries'] if entry['l'] == 'processes/ht.html')
        self.assertEqual(ht['s'], 'Processes')
        self.assertEqual(ht['m'], 'Process > Intensity Transformations > Histogram Transformation')

    def test_the_404_page_is_built(self):
        self.build()
        html = self.site('404.html')
        self.assertIn('Page not found', html)
        self.assertIn('/assets/guide/css/tokens.css', html)

    def test_the_version_comes_from_the_environment(self):
        os.environ['AFTERNIGHT_GUIDE_VERSION'] = '1.1.0'
        self.build()
        self.assertIn('AfterNight 1.1.0 user guide', self.site('index.html'))


class _Capture(logging.Handler):
    """Collects the guide's warnings while they still propagate to MkDocs, whose strict mode counts them; assertLogs
    would stop the propagation."""

    def __init__(self):
        super().__init__(logging.WARNING)
        self.messages = []

    def emit(self, record):
        self.messages.append(record.getMessage())


class WarningTests(FixtureCase):
    def capture(self):
        handler = _Capture()
        logger = logging.getLogger('mkdocs.plugins.guide')
        logger.addHandler(handler)
        self.addCleanup(logger.removeHandler, handler)
        return handler

    def assertBuildWarns(self, *fragments):
        handler = self.capture()
        with self.assertRaises(Abort):
            self.build()
        text = '\n'.join(handler.messages)
        for fragment in fragments:
            self.assertIn(fragment, text)

    def test_an_unknown_stage_fails_the_strict_build_naming_the_page(self):
        self.page('processes/ht.md', PAGES['processes/ht.md'].replace('stage: stretch', 'stage: polish'))
        self.assertBuildWarns('processes/ht.md', "`stage` is 'polish'")

    def test_a_page_without_front_matter_fails_naming_the_page(self):
        self.page('reference/glossary.md', '# Glossary\n')
        self.assertBuildWarns('reference/glossary.md', 'no front matter')

    def test_an_unknown_kind_still_renders_as_a_reference_page(self):
        self.page('reference/glossary.md', '---\nkind: component\nsummary: The terms.\n---\n# Glossary\n')
        handler = self.capture()
        self.build(strict=False)
        self.assertTrue(any("`kind` is 'component'" in message for message in handler.messages), handler.messages)
        self.assertIn('kind-reference', self.site('reference/glossary.html'))

    def test_broken_stage_data_stops_the_build(self):
        (self.root / 'data' / 'stages.yml').write_text('stages: []\n', encoding='utf-8')
        with self.assertRaises((PluginError, Abort)):
            self.build()


if __name__ == '__main__':
    unittest.main()
