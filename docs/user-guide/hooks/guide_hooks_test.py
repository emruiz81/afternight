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
COMPONENTS = HOOKS / 'fixtures' / 'components'

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
  - pymdownx.superfences
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


class DataDrawnTests(FixtureCase):
    def test_the_catalog_lists_each_process_in_the_lane_of_its_stage(self):
        self.page('processes/crop.md', '---\nkind: process\nstage: any\nmenu: [Process > Geometry > Crop, Image > Crop]'
                  '\nsummary: Cuts the borders off.\ndata_in: either\ndata_out: either\n---\n# Crop\n')
        config = (self.root / 'mkdocs.yml').read_text(encoding='utf-8')
        (self.root / 'mkdocs.yml').write_text(config.replace(
            '          - Histogram Transformation: processes/ht.md\n',
            '          - Histogram Transformation: processes/ht.md\n      - Geometry:\n'
            '          - Crop: processes/crop.md\n'), encoding='utf-8')
        self.page('processes/index.md', PAGES['processes/index.md'].replace(
            'summary: Every process.\n', 'summary: Every process.\nreach:\n  - Open the Process menu.\n'))
        self.build()
        html = self.site('processes/index.html')
        self.assertEqual(html.count('<section class="lane"'), 8)
        self.assertNotIn('All of them.', html)
        self.assertIn('<li>Open the Process menu.</li>', html)
        stretch = html[html.index('id="stage-stretch"'):html.index('id="stage-finish"')]
        self.assertIn('<span class="cnt">1 process</span>', stretch)
        self.assertIn('<h3 class="cath">Stretching</h3>', stretch)
        self.assertIn('<a href="ht.html">Histogram Transformation</a>', stretch)
        self.assertIn('<span class="sum">Stretches the image.</span>', stretch)
        self.assertIn('Process &gt; Intensity Transformations &gt; Histogram Transformation', stretch)
        any_time = html[html.index('id="stage-any"'):]
        self.assertIn('<a href="crop.html">Crop</a>', any_time)
        self.assertIn('Image &gt; Crop', any_time)
        self.assertIn('<span class="dm dm-either">', any_time)
        self.assertIn('No process of this stage has a page yet.', html[html.index('id="stage-calibrate"'):
                                                                      html.index('id="stage-register"')])

    def test_a_link_to_a_lane_of_the_catalog_is_valid(self):
        self.page('reference/glossary.md', PAGES['reference/glossary.md'] +
                  '\nSee [the stage](../processes/index.md#stage-stretch).\n')
        self.build()
        self.assertIn('href="../processes/index.html#stage-stretch"', self.site('reference/glossary.html'))

    def test_the_home_page_draws_what_its_front_matter_names(self):
        self.page('index.md', '---\nkind: home\nhero:\n  accent: guide\n  lede: Where to start.\n  actions:\n'
                  '    - {label: The terms, page: reference/glossary.md}\n    - {label: Processes, page: '
                  'processes/index.md}\n  image: logo.png\n  alt: The logo.\n  caption: A picture.\n'
                  'notice: Unfinished.\npaths:\n  - {tag: Look up, title: Read the terms, text: Every word., '
                  'page: reference/glossary.md}\npopular:\n  - processes/ht.md\n---\n# The guide\n\nWelcome.\n')
        self.build()
        html = self.site('index.html')
        self.assertNotIn('Welcome.', html)
        self.assertIn('<h1 id="the-guide">The <em>guide</em></h1>', html)
        self.assertIn('<p class="lede">Where to start.</p>', html)
        self.assertIn('<a class="btn btn-primary" href="reference/glossary.html">The terms</a>', html)
        self.assertIn('<a class="btn" href="processes/index.html">Processes</a>', html)
        self.assertIn('<img src="logo.png" alt="The logo."', html)
        self.assertIn('<span>Unfinished.</span>', html)
        self.assertIn('<span class="tag">Look up</span><b>Read the terms</b>', html)
        self.assertIn('<li><a href="processes/ht.html">Histogram Transformation</a></li>', html)
        self.assertIn('<span class="count">2 pages</span><b>Processes</b>', html)
        self.assertIn('<span class="count">1 page</span><b>Reference</b>', html)
        self.assertNotIn('class="track"', html)
        script = self.site('search/search_index.js')
        self.assertIn('Where to start.', script)
        self.assertNotIn('Welcome.', script)

    def test_a_process_page_shows_its_position_and_no_glance_until_it_states_one(self):
        self.build()
        html = self.site('processes/ht.html')
        self.assertIn('<h2 id="where-it-belongs">Where it belongs</h2>', html)
        self.assertIn('<p class="k">Comes after</p>', html)
        self.assertIn('<a href="index.html#stage-noise">Noise &amp; stars</a>', html)
        self.assertIn('The image turns nonlinear at this stage.', html)
        self.assertNotIn('id="at-a-glance"', html)
        self.assertIn('<li><a href="#where-it-belongs">Where it belongs</a>', html)


class ComponentSiteTests(unittest.TestCase):
    """The fixture site under fixtures/components: every page kind and component, built strictly as it stands."""

    @classmethod
    def setUpClass(cls):
        cls.out = pathlib.Path(tempfile.mkdtemp())
        cls.addClassCleanup(shutil.rmtree, cls.out, True)
        previous = os.environ.pop('AFTERNIGHT_GUIDE_VERSION', None)
        try:
            build(load_config(config_file=str(COMPONENTS / 'mkdocs.yml'), strict=True, site_dir=str(cls.out)))
        finally:
            if previous is not None:
                os.environ['AFTERNIGHT_GUIDE_VERSION'] = previous

    def site(self, relative):
        return (self.out / relative).read_text(encoding='utf-8')

    def test_a_process_page_on_the_new_template(self):
        html = self.site('processes/stretching/levels.html')
        self.assertIn('<li class="chip warn"><i aria-hidden="true"></i>Replaces the target</li>', html)
        self.assertIn('<li class="chip"><b>Preview</b> real-time</li>', html)
        self.assertIn('<figure class="an-figure"><div class="shotwrap"><a class="glightbox" '
                      'href="../../assets/window.png"', html)
        self.assertLess(html.index('<figure class="an-figure">'), html.index('id="where-it-belongs"'))
        self.assertLess(html.index('id="where-it-belongs"'), html.index('id="at-a-glance"'))
        self.assertLess(html.index('id="at-a-glance"'), html.index('id="before-you-start"'))
        self.assertIn('<dt>Enabled when</dt>', html)
        self.assertIn('<dd>Linear data in, nonlinear out</dd>', html)
        self.assertIn('<ol class="steps">', html)
        self.assertEqual(html.count('<section class="ctls"'), 2)
        self.assertIn('id="ctl-channel"', html)
        self.assertIn('id="ctl-levels-options-channel"', html)
        self.assertIn('<li><a href="#ctl-black-point">Black point</a></li>', html)
        self.assertIn('<figure class="an-figure an-pair">', html)
        self.assertIn('<dl class="tsc">', html)
        self.assertNotIn('fence-error', html)

    def test_position_blocks_at_the_first_stage_the_stretch_and_any_time(self):
        first = self.site('processes/calibration/flat-division.html')
        self.assertIn('The pipeline starts with this stage.', first)
        self.assertIn('#stage-register">Register</a>', first)
        self.assertIn('The image is still linear at this stage.', first)
        self.assertIn('<i class="linear on" style="--c:var(--s1)"></i>', first)
        self.assertIn('<li class="chip"><b>Needs</b> A flat master on disk</li>', first)
        self.assertNotIn('Replaces the target', first)
        stretch = self.site('processes/stretching/soft-stretch.html')
        self.assertIn('#stage-noise">Noise &amp; stars</a>', stretch)
        self.assertIn('#stage-finish">Finish</a>', stretch)
        self.assertIn('<i class="turn on" style="--c:var(--s6)"></i>', stretch)
        self.assertNotIn('id="at-a-glance"', stretch)
        self.assertIn('<dl class="tsc">', stretch)
        any_time = self.site('processes/geometry/trim.html')
        self.assertIn('<div class="pos pos-any"', any_time)
        self.assertIn('It belongs to no one stage', any_time)
        self.assertNotIn('Comes after', any_time)
        self.assertNotIn(' on" style="--c', any_time)

    def test_the_catalog_and_the_home_page(self):
        catalog = self.site('processes/index.html')
        counts = [catalog[catalog.index('id="stage-%s"' % stage):].split('<span class="cnt">')[1].split('<')[0]
                  for stage in ('calibrate', 'register', 'stretch', 'any')]
        self.assertEqual(counts, ['1 process', '0 processes', '2 processes', '1 process'])
        self.assertEqual(catalog.count('<li class="prow">'), 4)
        self.assertIn('<span class="dm dm-nonlinear"><span class="visually-hidden">Image: </span>linear \u2192 nonlinear',
                      catalog)
        home = self.site('index.html')
        self.assertIn('Finish <em>your pictures</em>', home)
        self.assertIn('stepper-big', home)
        self.assertEqual(home.count('<a class="card" href='), 4 + 5)
        self.assertIn('<a href="start/first-stack.html">First Stack</a>: 4 steps', home)
        self.assertIn('<li><a href="start/first-stack.html#add-the-frames"><i aria-hidden="true">1</i>Add the frames'
                      '</a></li>', home)
        self.assertNotIn('this body is not shown', home)

    def test_a_tutorial_and_a_workflow(self):
        tutorial = self.site('start/first-stack.html')
        self.assertIn('class="wrap layout layout-two"', tutorial)
        self.assertNotIn('side-toc', tutorial)
        self.assertIn('<li><a href="#add-the-frames">Add the frames</a></li>', tutorial)
        self.assertIn('<ul class="task-list need">', tutorial)
        self.assertIn('<ul class="branches">', tutorial)
        self.assertIn('<div class="tut"><figure class="fig an-figure">', tutorial)
        self.assertIn('<li class="nopin"><h3 id="open-the-master">', tutorial)
        self.assertIn('<div class="admonition see">', tutorial)
        self.assertIn('<details class="wrong">', tutorial)
        self.assertIn('<div class="result">', tutorial)
        self.assertIn('<ul class="nextg">', tutorial)
        workflow = self.site('workflows/deep-sky.html')
        self.assertIn('<div class="tut nofig">', workflow)
        self.assertIn('stepper-span', workflow)
        self.assertIn('a plate solver, a star catalog', workflow)

    def test_an_interface_page_and_plain_markdown(self):
        viewer = self.site('interface/viewer.html')
        self.assertIn('<figure class="an-figure">', viewer)
        self.assertIn('<dl class="tsc">', viewer)
        self.assertNotIn('class="steps"', viewer)
        markdown = self.site('reference/markdown.html')
        self.assertEqual(markdown.count('<div class="table-wrap" tabindex="0"><table>'), 1)
        self.assertIn('<div class="admonition see">', markdown)
        self.assertIn('<details class="wrong">', markdown)
        self.assertIn('<dl>', markdown)


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

    CONTROLS = ('---\nkind: process\nstage: stretch\nmenu: Process > Intensity Transformations > Histogram '
                'Transformation\nsummary: Stretches the image.\n---\n# Histogram Transformation\n\n## Controls\n\n'
                '```controls\n%s```\n')
    COMBO = '- name: Channel\n  kind: combo\n  default: RGB\n  options: [RGB, Red]\n  effect: Chooses.\n'

    def test_a_valid_controls_block_builds(self):
        self.page('processes/ht.md', self.CONTROLS % self.COMBO)
        self.build()
        html = self.site('processes/ht.html')
        self.assertIn('<div class="ctl" id="ctl-channel">', html)
        self.assertIn('<li><a href="#ctl-channel">Channel</a></li>', html)

    def test_a_controls_block_that_is_not_yaml_fails_naming_the_page_and_the_line(self):
        self.page('processes/ht.md', self.CONTROLS % 'window: Levels\ncontrols:\n  - name: [unclosed\n')
        self.assertBuildWarns('processes/ht.md:14: controls block: the block is not valid YAML')

    def test_an_unknown_kind_fails_naming_the_control(self):
        self.page('processes/ht.md', self.CONTROLS % self.COMBO.replace('kind: combo', 'kind: dial'))
        self.assertBuildWarns('processes/ht.md:12: controls block: control "Channel": unknown kind `dial`')

    def test_a_combo_without_options_fails_naming_the_control(self):
        self.page('processes/ht.md', self.CONTROLS % self.COMBO.replace('  options: [RGB, Red]\n', ''))
        self.assertBuildWarns('processes/ht.md:12: controls block: control "Channel": a combo needs `options`')

    def test_a_combo_default_outside_its_options_fails(self):
        self.page('processes/ht.md', self.CONTROLS % self.COMBO.replace('default: RGB', 'default: Luminance'))
        self.assertBuildWarns('processes/ht.md', 'control "Channel": the default "Luminance" is not among its options')

    def test_a_figure_with_a_missing_image_fails(self):
        self.page('processes/ht.md', PAGES['processes/ht.md'] +
                  '\n```figure\nimage: ../absent.png\nalt: A.\ncaption: C.\n```\n')
        self.assertBuildWarns('processes/ht.md:18: figure block: image ../absent.png does not exist')

    def test_a_broken_block_shows_an_error_box_in_a_preview(self):
        self.page('processes/ht.md', self.CONTROLS % self.COMBO.replace('kind: combo', 'kind: dial'))
        self.capture()
        self.build(strict=False)
        self.assertIn('<div class="fence-error" role="alert">', self.site('processes/ht.html'))

    def test_a_home_page_that_names_no_page_or_no_picture_fails(self):
        home = ('---\nkind: home\nhero:\n  lede: Start.\n  actions:\n    - {label: Gone, page: gone.md}\n'
                '  image: absent.png\n  alt: A.\n  caption: C.\npopular: [reference/missing.md]\n---\n# The guide\n')
        self.page('index.md', home)
        self.assertBuildWarns('index.md: `hero.actions` names gone.md, which is no page of the nav',
                              'index.md: `hero.image` names absent.png',
                              'index.md: `popular` names reference/missing.md')

    def test_broken_stage_data_stops_the_build(self):
        (self.root / 'data' / 'stages.yml').write_text('stages: []\n', encoding='utf-8')
        with self.assertRaises((PluginError, Abort)):
            self.build()


if __name__ == '__main__':
    unittest.main()
