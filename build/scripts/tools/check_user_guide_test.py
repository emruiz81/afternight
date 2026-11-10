# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_user_guide.

Run with:
    python build/scripts/tools/check_user_guide_test.py

Every rule has a site, or a page, that passes and one that fails; the cases plant them in a temporary directory.
The rules of a rewritten page have a guide with a page on each template, and a copy of it broken for each rule.
"""

from __future__ import annotations

import io
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_user_guide as guide  # noqa: E402


class SiteCase(unittest.TestCase):
    """A small built site: a home page, a page in a folder, a stylesheet, a script, an image and a font."""

    def setUp(self):
        self.site = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.site, True)
        self.write('index.html', '<html><body><a href="guide/page.html">Page</a></body></html>\n')
        self.write('guide/page.html', '<html><body><a href="../index.html">Home</a></body></html>\n')
        self.write('assets/site.css', 'body { color: black; }\n')
        self.write('assets/site.js', '"use strict";\n')
        self.write('assets/images/logo.png', 'png')
        self.write('assets/images/logo@2x.png', 'png')
        self.write('assets/fonts/face.woff2', 'woff2')
        self.write('assets/images/a b.png', 'png')

    def write(self, relative, text):
        path = self.site / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='')

    def findings(self):
        return guide.check_site(str(self.site), 'site')[0]

    def assertClean(self):
        self.assertEqual([str(finding) for finding in self.findings()], [])

    def assertFinding(self, path, line, *fragments):
        findings = self.findings()
        self.assertEqual(len(findings), 1, [str(finding) for finding in findings])
        finding = findings[0]
        self.assertEqual((finding.path, finding.line, finding.rule, finding.severity), ('site/' + path, line, 'R1',
                                                                                         'error'))
        for fragment in fragments:
            self.assertIn(fragment, finding.message)


class PassingSiteTests(SiteCase):
    def test_the_base_site_is_clean(self):
        self.assertClean()

    def test_every_kind_of_local_reference_resolves(self):
        self.write('guide/page.html', '\n'.join([
            '<html><head>',
            '<link rel="stylesheet" href="../assets/site.css">',
            '<link rel="icon" href="../assets/images/logo.png">',
            '<link rel="canonical" href="https://docs.example.org/guide/page.html">',
            '<link rel="alternate" href="https://example.org/feed.xml">',
            '<script src="../assets/site.js?v=3"></script>',
            '<style>@font-face { src: url("../assets/fonts/face.woff2") format("woff2"); }</style>',
            '</head><body>',
            '<a href="#top">Top</a> <a href="../index.html#start">Home</a> <a href="">Here</a>',
            '<a href="https://github.com/example/example">Source</a> <a href="mailto:a@example.org">Mail</a>',
            '<img src="../assets/images/logo.png"',
            ' srcset="../assets/images/logo.png 1x, ../assets/images/logo@2x.png 2x">',
            '<img src="../assets/images/a%20b.png" alt="">',
            '<img src="data:image/png;base64,AAAA" alt="">',
            '<div style="background: url(\'../assets/images/logo.png\')"></div>',
            '<svg><use href="#icon"></use></svg>',
            '</body></html>',
        ]) + '\n')
        self.assertClean()

    def test_stylesheet_references_resolve_from_the_stylesheet(self):
        self.write('assets/site.css', '@import "more.css";\n@font-face { src: url(fonts/face.woff2); }\n'
                   '/* url(missing.woff2) in a comment is not a reference */\n'
                   '.icon { background: url("data:image/svg+xml;utf8,<svg/>"); mask: url(#shape); }\n')
        self.write('assets/more.css', 'a { color: blue; }\n')
        self.assertClean()

    def test_the_404_page_names_files_from_the_site_root(self):
        self.write('404.html', '<html><head><link rel="stylesheet" href="/assets/site.css"></head>\n'
                   '<body><a href="/guide/page.html">Page</a> <a href="/">Home</a></body></html>\n')
        self.assertClean()


class FailingSiteTests(SiteCase):
    def test_a_missing_page_is_reported_with_its_line(self):
        self.write('guide/page.html', '<html><body>\n<p>Intro</p>\n<a href="gone.html">Gone</a>\n</body></html>\n')
        self.assertFinding('guide/page.html', 3, '<a href>', 'gone.html', 'not in the site')

    def test_a_remote_script_is_refused(self):
        self.write('index.html', '<script src="https://cdn.example.org/lib.js"></script>\n')
        self.assertFinding('index.html', 1, '<script src>', 'from the network')

    def test_a_remote_stylesheet_is_refused(self):
        self.write('index.html', '<link rel="stylesheet" href="https://fonts.example.org/css">\n')
        self.assertFinding('index.html', 1, '<link href>', 'from the network')

    def test_a_preconnect_counts_as_loading(self):
        self.write('index.html', '<link rel="preconnect" href="https://fonts.example.org">\n')
        self.assertFinding('index.html', 1, 'from the network')

    def test_a_protocol_relative_image_is_refused(self):
        self.write('index.html', '<img src="//img.example.org/badge.svg" alt="">\n')
        self.assertFinding('index.html', 1, '<img src>', 'from the network')

    def test_a_remote_srcset_candidate_is_refused(self):
        self.write('index.html', '<img src="assets/images/logo.png" '
                   'srcset="assets/images/logo.png 1x, https://img.example.org/logo.png 2x">\n')
        self.assertFinding('index.html', 1, '<img srcset>', 'https://img.example.org/logo.png')

    def test_a_remote_url_in_a_style_attribute_is_refused(self):
        self.write('index.html', '<div style="background: url(https://img.example.org/sky.png)"></div>\n')
        self.assertFinding('index.html', 1, '<div style>', 'from the network')

    def test_a_remote_font_in_a_style_element_is_refused_on_its_own_line(self):
        self.write('index.html', '<html><head>\n<style>\nbody { margin: 0; }\n'
                   '@font-face { src: url("https://fonts.example.org/f.woff2"); }\n</style>\n</head></html>\n')
        self.assertFinding('index.html', 4, '<style>', 'from the network')

    def test_a_remote_url_in_a_stylesheet_is_refused(self):
        self.write('assets/site.css',
                   'body { margin: 0; }\n@font-face { src: url(https://fonts.example.org/f.woff2); }\n')
        self.assertFinding('assets/site.css', 2, 'url()', 'from the network')

    def test_a_remote_import_in_a_stylesheet_is_refused(self):
        self.write('assets/site.css', '@import url("https://fonts.example.org/css");\n')
        self.assertFinding('assets/site.css', 1, 'from the network')

    def test_a_missing_stylesheet_import_is_reported(self):
        self.write('assets/site.css', '\n@import "missing.css";\n')
        self.assertFinding('assets/site.css', 2, '@import', 'missing.css', 'not in the site')

    def test_a_root_relative_reference_is_refused(self):
        self.write('guide/page.html', '<img src="/assets/images/logo.png" alt="">\n')
        self.assertFinding('guide/page.html', 1, 'root-relative')

    def test_a_folder_is_refused(self):
        self.write('index.html', '<a href="guide/">Guide</a>\n')
        self.assertFinding('index.html', 1, 'a folder')

    def test_a_reference_outside_the_site_is_refused(self):
        self.write('index.html', '<a href="../outside.html">Out</a>\n')
        self.assertFinding('index.html', 1, 'outside the site')

    def test_a_file_url_is_refused(self):
        self.write('index.html', '<img src="file:///C:/guide/logo.png" alt="">\n')
        self.assertFinding('index.html', 1, 'file: URL')

    def test_a_drive_path_is_refused(self):
        self.write('index.html', '<img src="C:/guide/logo.png" alt="">\n')
        self.assertFinding('index.html', 1, 'absolute path')

    def test_a_reference_in_another_case_is_refused(self):
        self.write('index.html', '<img src="assets/images/Logo.png" alt="">\n')
        self.assertFinding('index.html', 1, 'another spelling of its case')

    def test_a_missing_local_link_relation_is_reported(self):
        self.write('index.html', '<link rel="next" href="guide/next.html">\n')
        self.assertFinding('index.html', 1, '<link href>', 'not in the site')

    def test_a_404_page_still_refuses_what_is_missing_and_what_is_remote(self):
        self.write('404.html', '<link rel="stylesheet" href="/assets/gone.css">\n'
                   '<script src="https://cdn.example.org/lib.js"></script>\n')
        findings = self.findings()
        self.assertEqual([(finding.path, finding.line) for finding in findings],
                         [('site/404.html', 1), ('site/404.html', 2)], [str(finding) for finding in findings])

    def test_only_the_root_404_page_may_be_root_relative(self):
        self.write('guide/404.html', '<a href="/index.html">Home</a>\n')
        self.assertFinding('guide/404.html', 1, 'root-relative')


REAL_DATA = {name: (pathlib.Path(guide.REPO_ROOT) / 'docs' / 'user-guide' / 'data' / name).read_text(
    encoding='utf-8') for name in ('stages.yml', 'limits.yml', 'vocabulary.yml')}

UI_BUILDER = '''\
        menuProcess->setTitle(QCoreApplication::translate("AfterNightAppClass", "&Process", nullptr));
        menuStretch->setTitle(QCoreApplication::translate("AfterNightAppClass", "Intensity Transformations", nullptr));
        menuAdjustments->setTitle(QCoreApplication::translate("AfterNightAppClass", "Adjustments", nullptr));
        menuImage->setTitle(QCoreApplication::translate("AfterNightAppClass", "Image", nullptr));
        actionHT->setText(QCoreApplication::translate("AfterNightAppClass", "Histogram Transformation", nullptr));
        actionCurves->setText(QCoreApplication::translate("AfterNightAppClass", "Curves Transformation", nullptr));
        actionNegative->setText(QCoreApplication::translate("AfterNightAppClass", "Negative Transformation", nullptr));
        actionZoom->setText(QCoreApplication::translate("AfterNightAppClass", "Zoom &In", nullptr));
        menuBar->addAction(menuProcess->menuAction());
        menuBar->addAction(menuImage->menuAction());
        menuProcess->addAction(menuStretch->menuAction());
        menuProcess->addAction(menuAdjustments->menuAction());
        menuStretch->addAction(actionHT);
        menuStretch->addAction(actionCurves);
        menuAdjustments->addAction(actionNegative);
        menuImage->addAction(actionZoom);
'''

REGISTRY = '''\
void registerBuiltins()
{
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionHT, histogramTransformation,
                               facts);
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionCurves, curvesTransformation, facts);
}
'''

MKDOCS = '''\
site_name: Test guide
docs_dir: docs/en
theme:
  name: null
  logo: assets/images/logo.png
markdown_extensions:
  - pymdownx.superfences:
      format: !!python/name:pymdownx.superfences.fence_code_format
nav:
  - Home: index.md
  - Processes:
      - Histogram Transformation: processes/ht.md
      - Curves Transformation: processes/curves.md
      - Adjustments: processes/adjustments.md
  - Glossary: reference/glossary.md
'''

PAGES = {
    'index.md': '---\nkind: home\n---\n# The guide\n\nWelcome.\n',
    'processes/ht.md': '---\nkind: process\nstage: stretch\nmenu: Process > Intensity Transformations > Histogram '
                       'Transformation\nsummary: Stretches the image.\n---\n# Histogram Transformation\n\n'
                       '![The window](../assets/images/ht.png)\n',
    'processes/curves.md': '---\nkind: process\nstage: finish\nmenu: Process > Intensity Transformations > Curves '
                           'Transformation\nsummary: Reshapes tone.\n---\n# Curves Transformation\n',
    'processes/adjustments.md': '---\nkind: process\nstage: any\nmenu: Process > Adjustments\nsummary: Immediate '
                                'actions.\n---\n# Adjustments\n\n```bash\n# not a heading\n![x](missing.png)\n```\n',
    'reference/glossary.md': '---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n',
}


class GuideCase(unittest.TestCase):
    """A repository with the application sources R4 reads and a guide of five pages, none of them rewritten, that
    passes every rule a page not yet rewritten is held to."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write('src/AfterNightControls/app/AfterNightAppUiBuilder.h', UI_BUILDER)
        self.write('src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp', REGISTRY)
        self.write('README.md', '# Repository\n')
        self.write('docs/user-guide/mkdocs.yml', MKDOCS)
        for name, text in REAL_DATA.items():
            self.write('docs/user-guide/data/' + name, text)
        for relative, text in PAGES.items():
            self.page(relative, text)
        self.write('docs/user-guide/docs/en/assets/images/logo.png', 'png')
        self.write('docs/user-guide/docs/en/assets/images/ht.png', 'png')

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='')
        return path

    def page(self, relative, text):
        return self.write('docs/user-guide/docs/en/' + relative, text)

    def run_main(self, *arguments):
        stream = io.StringIO()
        code = guide.main(['--repo', str(self.root)] + list(arguments), stream)
        return code, stream.getvalue()

    def findings(self, rule, *arguments):
        code, output = self.run_main(*arguments)
        return code, [line for line in output.splitlines() if ': %s ' % rule in line]

    def assertOneFinding(self, rule, path, line, *fragments):
        code, found = self.findings(rule)
        self.assertEqual(len(found), 1, found)
        self.assertIn('%s:%d: %s ' % (path, line, rule), found[0].replace('\\', '/'))
        for fragment in fragments:
            self.assertIn(fragment, found[0])
        return code


class CleanGuideTests(GuideCase):
    def test_a_complete_guide_passes_and_reports_what_it_read(self):
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        for rule in ('R2 front matter', 'R3 nav and H1', 'R4 menu parity', 'R5 images',
                     'R6 credits of derived pages'):
            self.assertIn('%s: 0 error(s), 0 warning(s)' % rule, output)
        self.assertIn('5 pages: 1 home, 3 process, 1 reference', output)
        self.assertIn('process pages by stage: stretch 1, finish 1, any 1; 0 derived', output)
        self.assertIn("menus: 3 of 3 registered actions and other Process menu actions on a page's `menu`", output)
        self.assertIn('R1 not run', output)


class FrontMatterRuleTests(GuideCase):
    def test_a_page_without_front_matter_fails(self):
        self.page('reference/glossary.md', '# Glossary\n')
        self.assertEqual(self.assertOneFinding('R2', 'reference/glossary.md', 1, 'no front matter'), 1)

    def test_invalid_yaml_fails(self):
        self.page('reference/glossary.md', '---\nkind: reference\nsummary: [open\n---\n# Glossary\n')
        self.assertOneFinding('R2', 'reference/glossary.md', 1, 'not valid YAML')

    def test_an_unknown_stage_is_reported_on_its_line(self):
        self.page('processes/curves.md', PAGES['processes/curves.md'].replace('stage: finish', 'stage: polish'))
        self.assertOneFinding('R2', 'processes/curves.md', 3, "`stage` is 'polish'")

    def test_a_key_of_another_kind_fails(self):
        self.page('reference/glossary.md', '---\nkind: reference\nsummary: The terms.\nstage: any\n---\n# Glossary\n')
        self.assertOneFinding('R2', 'reference/glossary.md', 4, '`stage` does not apply to a reference page')

    def test_a_missing_required_key_fails(self):
        self.page('reference/glossary.md', '---\nkind: reference\n---\n# Glossary\n')
        self.assertOneFinding('R2', 'reference/glossary.md', 1, 'a reference page needs `summary`')


class NavRuleTests(GuideCase):
    def test_a_page_outside_the_nav_fails(self):
        self.page('reference/extra.md', '---\nkind: reference\nsummary: More.\n---\n# Extra\n')
        self.assertOneFinding('R3', 'reference/extra.md', 1, 'not in the nav')

    def test_a_nav_entry_without_a_page_fails_on_its_line(self):
        (self.root / 'docs/user-guide/docs/en/reference/glossary.md').unlink()
        self.assertOneFinding('R3', 'mkdocs.yml', 15, 'reference/glossary.md, which is not a page')

    def test_two_h1_headings_fail_on_the_second(self):
        self.page('reference/glossary.md', '---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n\n# Again\n')
        self.assertOneFinding('R3', 'reference/glossary.md', 7, '2 H1 headings, at lines 5, 7')

    def test_a_page_without_an_h1_fails(self):
        self.page('reference/glossary.md', '---\nkind: reference\nsummary: The terms.\n---\n## Terms\n')
        self.assertOneFinding('R3', 'reference/glossary.md', 5, 'no H1')


class MenuRuleTests(GuideCase):
    def test_a_menu_path_the_application_lacks_fails_on_its_line(self):
        self.page('processes/curves.md', PAGES['processes/curves.md'].replace('Curves Transformation\nsummary',
                                                                              'Curves\nsummary'))
        code, found = self.findings('R4')
        messages = '\n'.join(found)
        self.assertIn('processes/curves.md:4: R4 `menu` names Process > Intensity Transformations > Curves, which',
                      messages.replace('\\', '/'))
        self.assertIn('registered action actionCurves (Process > Intensity Transformations > Curves Transformation) '
                      "is on no page's `menu`", messages)
        self.assertEqual(code, 1)

    def test_an_uncovered_registered_action_is_reported_at_its_registration(self):
        self.page('processes/ht.md', PAGES['processes/ht.md'].replace(
            'menu: Process > Intensity Transformations > Histogram Transformation', 'menu: Process > Adjustments'))
        self.assertOneFinding('R4', 'ProcessRegistryBuiltins.cpp', 3, 'actionHT')

    def test_an_uncovered_process_menu_action_is_reported_at_its_menu_line(self):
        self.page('processes/adjustments.md', PAGES['processes/adjustments.md'].replace(
            'menu: Process > Adjustments', 'menu: Process > Intensity Transformations > Curves Transformation'))
        self.assertOneFinding('R4', 'AfterNightAppUiBuilder.h', 15, 'Process menu action actionNegative')

    def test_a_submenu_covers_every_action_under_it(self):
        self.page('processes/ht.md', PAGES['processes/ht.md'].replace(
            'menu: Process > Intensity Transformations > Histogram Transformation',
            'menu: Process > Intensity Transformations'))
        self.page('processes/curves.md', PAGES['processes/curves.md'].replace(
            'menu: Process > Intensity Transformations > Curves Transformation', 'menu: Process > Adjustments'))
        code, found = self.findings('R4')
        self.assertEqual((code, found), (0, []))

    def test_a_registered_action_without_a_menu_fails(self):
        self.write('src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp',
                   REGISTRY.replace('}\n', '    AN_REGISTER_CHANNEL(actionExtract_Red, extractRed);\n}\n'))
        code, found = self.findings('R4')
        self.assertEqual(len(found), 2, found)
        self.assertIn('actionExtract_Red has no label', found[0])
        self.assertIn('actionExtract_Red is in no menu', found[1])


class ImageRuleTests(GuideCase):
    def test_a_missing_image_fails_on_its_line(self):
        self.page('reference/glossary.md', '---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n\n'
                  '![Gone](../assets/images/gone.png)\n')
        self.assertOneFinding('R5', 'reference/glossary.md', 7, 'gone.png does not exist')

    def test_an_image_in_another_case_fails(self):
        self.page('processes/ht.md', PAGES['processes/ht.md'].replace('ht.png', 'HT.png'))
        code, found = self.findings('R5')
        self.assertEqual(len(found), 2, found)
        self.assertIn('another spelling of its case', found[0])
        self.assertIn('no page, README.md, logo or favicon shows this image', found[1])

    def test_a_root_relative_image_fails(self):
        self.page('reference/glossary.md', '---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n\n'
                  '<img src="/assets/images/ht.png" alt="">\n')
        self.assertOneFinding('R5', 'reference/glossary.md', 7, 'root-relative')

    def test_a_figure_blocks_image_is_held_and_counts_as_shown(self):
        self.write('docs/user-guide/docs/en/assets/images/spare.webp', 'webp')
        page = ('---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n\n```figure\n'
                'image: ../assets/images/%s\nalt: A.\ncaption: C.\n```\n')
        self.page('reference/glossary.md', page % 'spare.webp')
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R5 images: 0 error(s), 0 warning(s)', output)
        (self.root / 'docs/user-guide/docs/en/assets/images/spare.webp').unlink()
        self.page('reference/glossary.md', page % 'gone.webp')
        self.assertOneFinding('R5', 'reference/glossary.md', 8, 'gone.webp does not exist')

    def test_the_home_pages_picture_is_held_and_counts_as_shown(self):
        self.write('docs/user-guide/docs/en/assets/images/spare.webp', 'webp')
        page = ('---\nkind: home\nhero:\n  lede: The guide.\n  actions:\n    - {label: Terms, page: '
                'reference/glossary.md}\n  image: assets/images/%s\n  alt: A.\n  caption: C.\n---\n# Guide\n')
        self.page('index.md', page % 'spare.webp')
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R5 images: 0 error(s), 0 warning(s)', output)
        (self.root / 'docs/user-guide/docs/en/assets/images/spare.webp').unlink()
        self.page('index.md', page % 'gone.webp')
        self.assertOneFinding('R5', 'index.md', 3, 'gone.webp does not exist')

    def test_an_unused_image_is_a_warning(self):
        self.write('docs/user-guide/docs/en/assets/images/spare.webp', 'webp')
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R5 images: 0 error(s), 1 warning(s)', output)
        self.assertIn('spare.webp:1: R5 warning: no page, README.md, logo or favicon shows this image',
                      output.replace('\\', '/'))

    def test_an_image_the_readme_shows_is_used(self):
        self.write('docs/user-guide/docs/en/assets/images/spare.webp', 'webp')
        self.write('README.md', '# Repository\n\n![Spare](docs/user-guide/docs/en/assets/images/spare.webp)\n')
        code, output = self.run_main()
        self.assertIn('R5 images: 0 error(s), 0 warning(s)', output)


class CreditsRuleTests(GuideCase):
    DERIVED = PAGES['processes/curves.md'].replace('summary: Reshapes tone.\n', 'summary: Reshapes tone.\n'
                                                                              'derived: true\n')

    def test_a_derived_page_with_credits_passes(self):
        self.page('processes/curves.md', self.DERIVED + '\n## Credits\n\n- [The upstream work](https://example.org/), '
                  'by its authors, under GPL-3.0-or-later.\n')
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R6 credits of derived pages: 0 error(s), 0 warning(s)', output)
        self.assertIn('finish 1, any 1; 1 derived', output)

    def test_a_derived_page_without_a_credits_section_fails_where_it_says_it_is_derived(self):
        self.page('processes/curves.md', self.DERIVED + '\n## Related\n\n- Another page.\n')
        self.assertEqual(self.assertOneFinding('R6', 'processes/curves.md', 6, '`derived: true`',
                                               'no `## Credits` section'), 1)

    def test_an_empty_credits_section_fails_on_its_heading(self):
        self.page('processes/curves.md', self.DERIVED + '\n## Credits\n\n<!-- to be written -->\n\n## Related\n\n'
                  '- Another page.\n')
        self.assertOneFinding('R6', 'processes/curves.md', 10, '`## Credits` section is empty')

    def test_a_credits_heading_inside_a_code_block_is_no_section(self):
        self.page('processes/curves.md', self.DERIVED + '\n```markdown\n## Credits\n\n- An example.\n```\n')
        self.assertOneFinding('R6', 'processes/curves.md', 6, 'no `## Credits` section')

    def test_a_page_that_is_not_derived_needs_no_credits(self):
        code, found = self.findings('R6')
        self.assertEqual((code, found), (0, []))

    def test_page_holds_the_rule_on_the_page_named(self):
        self.page('processes/curves.md', self.DERIVED)
        code, found = self.findings('R6', '--page', 'processes/curves.md')
        self.assertEqual((code, len(found)), (1, 1), found)


class SelectionTests(GuideCase):
    def test_page_holds_only_the_pages_named_and_skips_the_guide_wide_parts(self):
        self.page('reference/glossary.md', '# Glossary\n')
        self.page('processes/ht.md', PAGES['processes/ht.md'].replace('menu: Process > Intensity Transformations > '
                                                                      'Histogram Transformation\n', ''))
        code, output = self.run_main('--page', 'processes/curves.md')
        self.assertEqual(code, 0, output)
        self.assertIn('1 pages: 1 process', output)
        self.assertNotIn('menus:', output)

    def test_page_names_a_page_relative_to_the_working_directory_too(self):
        page = self.root / 'docs/user-guide/docs/en/reference/glossary.md'
        self.page('reference/glossary.md', '# Glossary\n')
        code, found = self.findings('R2', '--page', str(page))
        self.assertEqual(code, 1)
        self.assertEqual(len(found), 1, found)

    def test_an_unknown_page_is_a_usage_error(self):
        code, output = self.run_main('--page', 'processes/none.md')
        self.assertEqual(code, 2)
        self.assertIn('is not a page', output)

    def test_broken_guide_data_is_reported_and_stops_the_check(self):
        self.write('docs/user-guide/data/stages.yml', 'stages: []\n')
        code, output = self.run_main()
        self.assertEqual(code, 2)
        self.assertIn("the guide's data cannot be read", output)


class MainTests(GuideCase):
    def setUp(self):
        super().setUp()
        self.site = self.root / 'site'
        self.write('site/index.html', '<a href="page.html">Page</a>\n<link rel="stylesheet" href="site.css">\n')
        self.write('site/page.html', '<a href="index.html">Home</a>\n')
        self.write('site/site.css', 'body { margin: 0; }\n')

    def test_a_clean_site_exits_zero_and_counts_what_it_read(self):
        code, output = self.run_main('--site', str(self.site))
        self.assertEqual(code, 0, output)
        self.assertIn('R1 built-site references: 0 error(s)', output)
        self.assertIn('2 HTML documents and 1 stylesheets', output)

    def test_a_finding_prints_as_path_line_rule_message_and_exits_one(self):
        self.write('site/index.html', '<a href="gone.html">Gone</a>\n')
        code, output = self.run_main('--site', str(self.site))
        self.assertEqual(code, 1, output)
        self.assertRegex(output, r'(?m)^.*index\.html:1: R1 <a href> names gone\.html, which is not in the site$')
        self.assertIn('R1 built-site references: 1 error(s), 0 warning(s)', output)

    def test_a_folder_without_a_built_site_is_a_usage_error(self):
        empty = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, empty, True)
        code, output = self.run_main('--site', str(empty))
        self.assertEqual(code, 2)
        self.assertIn('build the guide first', output)

    def test_without_a_site_the_site_rule_does_not_run(self):
        code, output = self.run_main()
        self.assertEqual(code, 0)
        self.assertIn('R1 not run', output)
        self.assertNotIn('R1 built-site references', output)

    def test_it_runs_as_a_script(self):
        self.write('site/index.html', '<script src="https://cdn.example.org/lib.js"></script>\n')
        script = pathlib.Path(guide.__file__).resolve()
        ran = subprocess.run([sys.executable, '-I', str(script), '--repo', str(self.root), '--site', str(self.site)],
                             capture_output=True, text=True)
        self.assertEqual(ran.returncode, 1, ran.stderr + ran.stdout)
        self.assertIn(': R1 <script src> loads https://cdn.example.org/lib.js from the network', ran.stdout)


class TokenRuleTests(GuideCase):
    def setUp(self):
        super().setUp()
        self.write('docs/user-guide/theme/assets/guide/css/tokens.css', ':root {\r\n    --bg: #0B0F1A;\r\n}\r\n')

    def test_a_copy_equal_but_for_line_endings_passes(self):
        website = self.write('website/tokens.css', ':root {\n    --bg: #0B0F1A;\n}\n')
        code, output = self.run_main('--tokens', str(website))
        self.assertEqual(code, 0, output)
        self.assertIn('R16 token parity: 0 error(s), 0 warning(s)', output)

    def test_a_copy_that_drifted_fails_on_the_first_line_that_differs(self):
        website = self.write('website/tokens.css', ':root {\n    --bg: #0A0E19;\n}\n')
        code, output = self.run_main('--tokens', str(website))
        self.assertEqual(code, 1, output)
        self.assertRegex(output.replace('\\', '/'), r'theme/assets/guide/css/tokens\.css:2: R16 differs from ')

    def test_a_missing_website_file_is_an_error(self):
        code, output = self.run_main('--tokens', str(self.root / 'website' / 'none.css'))
        self.assertEqual(code, 1, output)
        self.assertIn("the website's tokens cannot be read", output)

    def test_without_the_website_file_the_rule_does_not_run(self):
        code, output = self.run_main()
        self.assertIn('R16 not run', output)
        self.assertNotIn('R16 token parity', output)


def png(width, height, weight=0):
    """The bytes of a file that opens as a PNG image of this size, padded to `weight` bytes."""
    data = b'\x89PNG\r\n\x1a\n' + b'\x00\x00\x00\rIHDR' + struct.pack('>II', width, height)
    return data + b'\0' * max(0, weight - len(data))


WRITING_NAV = '''\
nav:
  - Home: index.md
  - Getting Started:
      - First Stack: start/first.md
  - Interface:
      - Image Viewer: interface/viewer.md
  - Processes:
      - Stretching:
          - Histogram Transformation: processes/ht.md
  - Workflows:
      - Deep Sky: workflows/deep.md
  - Reference:
      - Glossary: reference/glossary.md
      - Media Provenance: assets/MEDIA_PROVENANCE.md
'''

HOME = '---\nkind: home\nrewritten: true\n---\n# The guide\n'

PROCESS = '''\
---
kind: process
stage: stretch
menu:
  - Process > Intensity Transformations
  - Process > Adjustments
summary: Stretches the image through a midtones transfer.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: An image view is active.
  inputs: One image view.
  outputs: The same view, replaced.
  state: A master that was never stretched.
derived: false
rewritten: true
---
# Histogram Transformation

The process maps the image through a transfer. Apply replaces the target.

```figure
image: ../assets/images/ht.png
alt: The window with its histogram.
caption: The window as it opens.
pins:
  - {n: 1, x: 10, y: 10}
  - {n: 3, x: 50, y: 50}
```

## Before you start

- Work on a [linear](../reference/glossary.md#linear) master.

## Steps

1. **Open the window.** Choose it from the menu.
2. **Move the midtones.** Watch the preview.
3. **Apply.** The view is replaced.

## Controls

```controls
- name: Midtones
  kind: spinbox
  default: "0.50"
  widget: {min: 0, max: 1}
  effect: Lower brightens the image.
```

## How it works

1. The samples are rescaled.

### Model

The transfer bends the range.

### Boundaries

- A sample outside the range is clipped.

### Worked example

A master is stretched.

```figure
images:
  - image: ../assets/images/before.png
    alt: The master before.
    label: Before
  - image: ../assets/images/after.png
    alt: The master after.
    label: After
caption: The same field before and after.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A dark master | Midtones 0.12 | The shadows |

## When it goes wrong

!!! warning "A stretch is not undone by another"
    Use the history.

The preview is black
:   **Cause.** The shadows are too high.
:   **Fix.** Lower them.

## Related

- [Image Viewer](../interface/viewer.md), where the result shows.

## Credits

- [A paper](https://example.org/), by its authors.
'''

INTERFACE = '''\
---
kind: interface
summary: The window an image opens in.
menu: Image > Zoom In
rewritten: true
---
# Image Viewer

The viewer shows one image.

```figure
image: ../assets/images/viewer.png
alt: The viewer with its toolbar.
caption: The parts of the viewer.
pins:
  - {n: 1, x: 10, y: 10}
  - {n: 2, x: 60, y: 60}
```

## What you see

1. **The toolbar.** What acts on the view.
2. **The image.** What is shown.

## What you can do

- Zoom to fit.

## When it goes wrong

The image is black
:   **Cause.** No display stretch is on.
:   **Fix.** Turn it on.

## Related

- [Histogram Transformation](../processes/ht.md)
'''

TUTORIAL = '''\
---
kind: tutorial
summary: Stack a first set of frames.
stages: [calibrate, register, stack]
needs: []
rewritten: true
---
# First Stack

You start with frames and end with one master.

## What you need

- [ ] The light frames of one target

## Your camera

- **A color camera.** Import the raw files.
- **A mono camera.** Import one filter at a time.

## Steps

```figure
image: ../assets/images/first.png
alt: The window with its frame list.
caption: The window once the frames are in.
pins:
  - {n: 1, x: 10, y: 10}
  - {n: 2, x: 60, y: 60}
```

### Add the frames

Drop the folder on the list.

!!! see "You should see"
    Every frame listed.

??? wrong "If it goes wrong: a frame is missing"
    Add it by hand.

### Run

Press Run.

## The result

One master in the output folder.

## Next

- [Histogram Transformation](../processes/ht.md) makes it visible.
'''

WORKFLOW = '''\
---
kind: workflow
summary: From frames to a finished picture.
stages: [calibrate, register, stack, stretch]
needs: []
rewritten: true
---
# Deep Sky

You take one target through the pipeline.

## What you need

- [ ] A set of calibrated frames

## Your data

- **Broadband.** Calibrate the color.
- **Narrowband.** Map the filters.

## Steps

### Stack

Follow [First Stack](../start/first.md).

### Stretch

Use [Histogram Transformation](../processes/ht.md).

## The result

A finished picture.

## Next

- [Image Viewer](../interface/viewer.md) shows it.
'''

GLOSSARY = '''\
---
kind: reference
summary: The terms of the guide.
rewritten: true
---
# Glossary

The terms, by letter.

## L

### Linear

Data whose values are proportional to the light that made them.

## M

### MAD

The median absolute deviation.
'''

PROVENANCE = '''\
---
kind: reference
summary: Where every picture of the guide comes from.
rewritten: true
---
# Media Provenance

| Picture | Source | Date |
| --- | --- | --- |
| `assets/images/ht.png` | Captured by the maintainer | 2026-11-09 |
| `assets/images/before.png` | Captured by the maintainer | 2026-11-09 |
| `assets/images/after.png` | Captured by the maintainer | 2026-11-09 |
| `assets/images/viewer.png` | Captured by the maintainer | 2026-11-09 |
| `assets/images/first.png` | Captured by the maintainer | 2026-11-09 |
'''

WRITING_PAGES = {
    'index.md': HOME,
    'processes/ht.md': PROCESS,
    'interface/viewer.md': INTERFACE,
    'start/first.md': TUTORIAL,
    'workflows/deep.md': WORKFLOW,
    'reference/glossary.md': GLOSSARY,
    'assets/MEDIA_PROVENANCE.md': PROVENANCE,
}
WRITING_RULES = ('R7 headings', 'R8 limits', 'R9 vocabulary', 'R10 figures', 'R11 image files', 'R12 process keys',
                 'R13 glossary links', 'R14 controls of the windows')


def ui_control(label, kind, obj, group='', **stated):
    """One control of a window's inventory, as the application's test records it."""
    record = {'label': label, 'kind': kind, 'object': obj, 'group': group, 'visible': True, 'default': ''}
    record.update(stated)
    for key in record.get('varies', ()):
        record.pop(key, None)
    return record


# The window the process page of the writing cases describes: its one control, a button of the toolbar every
# real-time preview has and the buttons every process window has.
HT_WINDOW = [
    ui_control('Zoom in', 'toolbutton', 'zoomIn', shared='rt-preview'),
    ui_control('Midtones', 'spinbox', 'midtonesValue', default='0.50', min=0, max=1),
    ui_control('Apply', 'button', '', shared='process-window'),
]
NOT_REWRITTEN = ('rewritten: true\n', '')


def words(count):
    return ' '.join(['word'] * count)


def items(count, pattern):
    return ''.join(pattern % number for number in range(1, count + 1))


class WritingCase(GuideCase):
    """A guide of seven pages, one on each template and each marked `rewritten: true`, that passes every rule."""

    def setUp(self):
        super().setUp()
        for relative in PAGES:
            (self.root / 'docs/user-guide/docs/en' / relative).unlink()
        self.write('docs/user-guide/mkdocs.yml', MKDOCS[:MKDOCS.index('nav:')] + WRITING_NAV)
        for relative, text in WRITING_PAGES.items():
            self.page(relative, text)
        for name in ('ht', 'before', 'after', 'viewer', 'first'):
            self.image('assets/images/%s.png' % name, png(800, 600))
        self.inventory('tone_brightness.actionHT', 'Histogram Transformation <{image}>', HT_WINDOW)

    def inventory(self, identifier, title, controls):
        """Plants the inventory of a window: the file the application's test writes for it."""
        return self.write('docs/user-guide/data/ui/%s.json' % identifier,
                          json.dumps({'id': identifier, 'window': title, 'controls': controls}, indent=2) + '\n')

    def image(self, relative, data):
        path = self.root / 'docs/user-guide/docs/en' / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def change(self, relative, old, new):
        """The page with `old` replaced by `new`; `old` must be in it once."""
        text = WRITING_PAGES[relative]
        self.assertEqual(text.count(old), 1, old)
        self.page(relative, text.replace(old, new))

    def restore(self):
        for relative, text in WRITING_PAGES.items():
            self.page(relative, text)

    def assertFails(self, rule, *fragments, count=1):
        """The check fails, with `count` findings of the rule and none of another, all errors."""
        code, output = self.run_main()
        found = [line for line in output.splitlines() if re.search(r':\d+: R\d+ ', line)]
        self.assertEqual(len(found), count, output)
        for line in found:
            self.assertIn(': %s ' % rule, line)
            self.assertNotIn('warning: ', line)
        for fragment in fragments:
            self.assertIn(fragment, '\n'.join(found))
        self.assertEqual(code, 1, output)

    def assertWarns(self, rule, *fragments):
        """The check passes, with one finding, a warning of the rule."""
        code, output = self.run_main()
        found = [line for line in output.splitlines() if re.search(r':\d+: R\d+ ', line)]
        self.assertEqual(len(found), 1, output)
        self.assertIn(': %s warning: ' % rule, found[0])
        for fragment in fragments:
            self.assertIn(fragment, found[0])
        self.assertEqual(code, 0, output)

    def assertPasses(self):
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        for rule in WRITING_RULES:
            self.assertIn('%s: 0 error(s), 0 warning(s)' % rule, output)

    def cases(self, rule, cases):
        """Each case - (what it shows, page, old, new, a fragment of the message) - fails with one finding of the
        rule."""
        for name, relative, old, new, fragment in cases:
            with self.subTest(name):
                self.restore()
                self.change(relative, old, new)
                self.assertFails(rule, fragment)


class TemplatePageTests(WritingCase):
    def test_a_rewritten_page_on_each_template_passes_every_rule(self):
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        for rule in ('R2 front matter', 'R3 nav and H1', 'R4 menu parity', 'R5 images',
                     'R6 credits of derived pages') + WRITING_RULES:
            self.assertIn('%s: 0 error(s), 0 warning(s)' % rule, output)
        self.assertIn('7 of 7 pages rewritten', output)
        self.assertNotIn('not yet rewritten', output)

    def test_a_page_not_yet_rewritten_is_counted_and_never_fails(self):
        self.page('processes/ht.md', PROCESS.replace(*NOT_REWRITTEN).replace('## Related', '## See also')
                  .replace('The process maps', 'The verified process maps'))
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R7 headings: 0 error(s), 2 warning(s)', output)
        self.assertIn('R9 vocabulary: 0 error(s), 1 warning(s)', output)
        self.assertIn('6 of 7 pages rewritten', output)
        self.assertIn('3 warning(s) about 1 file(s) of pages not yet rewritten are counted and not listed', output)
        self.assertNotIn(': R7 ', output)

    def test_pending_and_page_list_the_warnings_of_a_page_not_yet_rewritten(self):
        self.page('processes/ht.md', PROCESS.replace(*NOT_REWRITTEN).replace('## Related', '## See also'))
        for arguments in (['--pending'], ['--page', 'processes/ht.md']):
            code, found = self.findings('R7', *arguments)
            self.assertEqual(code, 0)
            self.assertEqual(len(found), 2, found)
            self.assertIn('ht.md:97: R7 warning: `## See also` is no section of a process page', found[0])
            self.assertIn('R7 warning: the page has no `## Related`', found[1])

    def test_a_page_not_yet_rewritten_is_not_held_to_figures_keys_and_glossary_links(self):
        self.page('processes/ht.md', PROCESS.replace(*NOT_REWRITTEN).replace('derived: false\n', '')
                  .replace('[linear](../reference/glossary.md#linear)', 'linear')
                  .replace('  - {n: 3, x: 50, y: 50}', '  - {n: 9, x: 50, y: 50}'))
        code, output = self.run_main('--pending')
        self.assertEqual(code, 0, output)
        for rule in ('R10 figures', 'R12 process keys', 'R13 glossary links'):
            self.assertIn('%s: 0 error(s), 0 warning(s)' % rule, output)


class HeadingRuleTests(WritingCase):
    def test_each_break_of_the_process_template_fails(self):
        related = '## Related\n\n- [Image Viewer](../interface/viewer.md), where the result shows.\n\n'
        self.cases('R7', (
            ('a section the template lacks', 'processes/ht.md', '## Starting points', '## Performance',
             '`## Performance` is no section of a process page (docs/user-guide/templates/PROCESS_PAGE_TEMPLATE.md)'),
            ('a missing section', 'processes/ht.md', related, '', 'the page has no `## Related`'),
            ('a subsection the section lacks', 'processes/ht.md', '### Model', '### Theory',
             '`### Theory` is no subsection of `## How it works`; they are Model, Boundaries, Worked example'),
            ('a subsection where there is none', 'processes/ht.md', '## Controls\n', '### More\n\nText.\n\n'
             '## Controls\n', '`### More` is under `## Steps`, which has no subsections'),
            ('a heading of level four', 'processes/ht.md', '### Model', '### Model\n\n#### Detail',
             '"Detail" is a heading of level 4'),
            ('a heading in the opening', 'processes/ht.md', 'The process maps', '### Overview\n\nThe process maps',
             '`### Overview` comes before the first section'),
            ('a step without a bold lead', 'processes/ht.md', '2. **Move the midtones.**', '2. Move the midtones.',
             'step 2 opens without a bold lead'),
            ('an entry without a fix', 'processes/ht.md', ':   **Fix.** Lower them.\n', '',
             'the entry "The preview is black" has no `**Fix.**`'),
            ('controls outside a block', 'processes/ht.md', '```controls\n- name: Midtones\n  kind: spinbox\n  '
             'default: "0.50"\n  widget: {min: 0, max: 1}\n  effect: Lower brightens the image.\n```\n',
             'Midtones brightens.\n', '`## Controls` has no `controls` block'),
        ))

    def test_sections_out_of_order_and_repeated_fail(self):
        related = '## Related\n\n- [Image Viewer](../interface/viewer.md), where the result shows.\n\n'
        self.page('processes/ht.md', PROCESS.replace(related, '').replace('## Starting points', related +
                                                                           '## Starting points'))
        self.assertFails('R7', '`## Starting points` comes after `## Related`; the sections of a process page are, '
                         'in order: Before you start, Steps, Controls, How it works, Starting points (optional), '
                         'When it goes wrong, Related, Credits (optional)', count=2)
        self.page('processes/ht.md', PROCESS.replace('### Model', '### Boundaries').replace(
            '### Boundaries\n\n- A sample', '### Model\n\n- A sample'))
        self.assertFails('R7', '`### Model` comes after `### Boundaries`')
        self.restore()
        self.change('processes/ht.md', '## Credits', '## Related\n\n- [Glossary](../reference/glossary.md)\n\n'
                    '## Credits')
        self.assertFails('R7', 'ht.md:102: R7 `## Related` repeats `## Related` of line 98')

    def test_a_process_page_without_steps_as_a_list_fails(self):
        self.change('processes/ht.md', '1. **Open the window.** Choose it from the menu.\n2. **Move the midtones.** '
                    'Watch the preview.\n3. **Apply.** The view is replaced.\n', 'Open the window and apply.\n')
        # The pins of the opening figure then match no step, which is R10's to say.
        code, found = self.findings('R7')
        self.assertEqual((code, len(found)), (1, 1), found)
        self.assertIn('`## Steps` has no ordered list', found[0])

    def test_each_break_of_the_other_templates_fails(self):
        self.cases('R7', (
            ('an interface page without its numbered list', 'interface/viewer.md',
             '1. **The toolbar.** What acts on the view.\n2. **The image.** What is shown.\n', 'A toolbar.\n',
             '`## What you see` has no numbered list'),
            ('a tutorial whose steps are not headings', 'workflows/deep.md', '### Stack\n\nFollow [First Stack]'
             '(../start/first.md).\n\n### Stretch\n', '1. Stack.\n2. Stretch.\n', '`## Steps` has no step; each step '
             'of a workflow is a `###` heading'),
            ('a tutorial without a task list', 'start/first.md', '- [ ] The light frames', '- The light frames',
             '`## What you need` has no task list'),
            ('both branch sections', 'start/first.md', '## Steps', '## Your data\n\n- **One.** Text.\n\n## Steps',
             '`## Your data` repeats `## Your camera`'),
            ('a subsection outside the steps', 'start/first.md', 'One master in the output folder.',
             '### Where\n\nOne master in the output folder.', '`### Where` is under `## The result`'),
            ('a glossary section that is no letter', 'reference/glossary.md', '## M\n', '## Measures\n',
             '`## Measures` is no section of the glossary'),
            ('glossary sections out of order', 'reference/glossary.md', '## M\n\n### MAD', '## A\n\n### Apex',
             '`## A` comes after `## L`'),
            ('a term under another letter', 'reference/glossary.md', '### MAD', '### Sigma',
             '`### Sigma` is under `## M`'),
            ('a home page with a body', 'index.md', '# The guide\n', '# The guide\n\nWelcome.\n',
             'the home page is drawn from its front matter; it has nothing below its H1'),
        ))

    def test_an_interface_page_may_head_its_tasks_and_a_tutorial_its_steps(self):
        self.change('interface/viewer.md', '- Zoom to fit.', '### Zoom\n\n- Zoom to fit.')
        self.assertPasses()

    def test_a_reference_page_is_free_in_its_sections(self):
        self.page('assets/MEDIA_PROVENANCE.md', PROVENANCE + '\n## Anything\n\n### At all\n\n#### Deeper\n\nText.\n')
        self.assertPasses()

    def test_a_heading_in_a_code_block_is_no_heading(self):
        self.change('processes/ht.md', 'A master is stretched.', 'A master is stretched.\n\n```markdown\n## Not a '
                    'section\n```')
        self.assertPasses()


class LimitRuleTests(WritingCase):
    def test_each_budget_of_a_process_page_is_held(self):
        row = '| A dark master | Midtones 0.12 | The shadows |\n'
        callout = '!!! warning "A stretch is not undone by another"\n    Use the history.\n\n'
        entry = 'The preview is black\n:   **Cause.** The shadows are too high.\n:   **Fix.** Lower them.\n'
        self.cases('R8', (
            ('the summary', 'processes/ht.md', 'summary: Stretches the image through a midtones transfer.',
             'summary: ' + words(41), '`summary` has 41 words, over the limit of 40'),
            ('the page', 'processes/ht.md', 'The transfer bends the range.', words(1800),
             'words, over the limit of 1800 for a process page'),
            ('the opening', 'processes/ht.md', 'The process maps the image through a transfer. Apply replaces the '
             'target.', words(121), 'the opening has 121 words, over the limit of 120 for a process page'),
            ('the steps', 'processes/ht.md', '3. **Apply.** The view is replaced.\n',
             items(8, '%d. **Again.** Once more.\n'), '`## Steps` has 10 steps, over the limit of 9'),
            ('the bullets before you start', 'processes/ht.md', '## Steps', items(6, '- Bullet %d.\n') + '\n## Steps',
             '`## Before you start` has 7 bullets, over the limit of 6'),
            ('the numbered statements', 'processes/ht.md', '1. The samples are rescaled.\n',
             items(6, '%d. A statement.\n'), '`## How it works` has 6 numbered statements, over the limit of 5'),
            ('the boundaries', 'processes/ht.md', '- A sample outside the range is clipped.\n',
             items(6, '- Boundary %d.\n'), '`### Boundaries` has 6 bullets, over the limit of 5'),
            ('the rows of the starting points', 'processes/ht.md', row, row * 5,
             'the table of `## Starting points` has 5 rows, over the limit of 4'),
            ('the columns of the starting points', 'processes/ht.md', '| Situation | Start | Then adjust |\n| --- | '
             '--- | --- |\n' + row, '| Situation | Start | Then adjust | Why |\n| --- | --- | --- | --- |\n'
             '| A dark master | Midtones 0.12 | The shadows | Faint signal |\n',
             'the table of `## Starting points` has 4 columns, over the limit of 3'),
            ('the warning callouts', 'processes/ht.md', callout, callout * 5,
             'the page has 5 warning and danger callouts, over the limit of 4'),
            ('the troubleshooting entries', 'processes/ht.md', entry, (entry + '\n') * 9,
             '`## When it goes wrong` has 9 entries, over the limit of 8'),
            ('the related links', 'processes/ht.md', '## Credits', items(6, '- [Glossary %d](../reference/'
             'glossary.md)\n') + '\n## Credits', '`## Related` has 7 links, over the limit of 6'),
            ('the credits', 'processes/ht.md', '- [A paper](https://example.org/), by its authors.\n',
             items(9, '- Credit %d.\n'), '`## Credits` has 9 entries, over the limit of 8'),
        ))

    def test_the_credits_the_blocks_and_the_math_are_not_counted_as_words(self):
        self.change('processes/ht.md', '- [A paper](https://example.org/), by its authors.',
                    '- [A paper](https://example.org/), by its authors. ' + words(1800))
        self.assertPasses()
        self.restore()
        self.change('processes/ht.md', 'The transfer bends the range.', 'The transfer bends the range.\n\n```text\n' +
                    words(1800) + '\n```\n\n\\[\n' + words(1800) + '\n\\]\n\nInline \\(' + words(1800) + '\\) math.')
        self.assertPasses()

    def test_a_table_of_six_columns_fails_on_any_page(self):
        self.change('assets/MEDIA_PROVENANCE.md', '| Picture | Source | Date |\n| --- | --- | --- |',
                    '| Picture | Source | Date | A | B | C |\n| --- | --- | --- | --- | --- | --- |')
        self.assertFails('R8', 'MEDIA_PROVENANCE.md:8: R8 the table has 6 columns, over the limit of 5')

    def test_a_bar_in_a_code_span_or_escaped_is_no_column(self):
        self.change('assets/MEDIA_PROVENANCE.md', '| Picture | Source | Date |\n| --- | --- | --- |',
                    '| Picture `a|b|c|d` | Source \\| more | Date |\n| --- | --- | --- |')
        self.assertPasses()

    def test_a_large_page_has_the_budgets_of_large_pages(self):
        limits = REAL_DATA['limits.yml'].replace('large_pages:\n', 'large_pages:\n  - processes/ht.md\n')
        self.write('docs/user-guide/data/limits.yml', limits)
        self.change('processes/ht.md', '3. **Apply.** The view is replaced.\n',
                    items(10, '%d. **Again.** Once more.\n'))
        self.assertPasses()
        self.restore()
        self.change('processes/ht.md', '3. **Apply.** The view is replaced.\n',
                    items(11, '%d. **Again.** Once more.\n'))
        self.assertFails('R8', '`## Steps` has 13 steps, over the limit of 12 for a large process page')

    def test_the_budgets_of_the_other_kinds_are_held(self):
        entry = 'The image is black\n:   **Cause.** No display stretch is on.\n:   **Fix.** Turn it on.\n'
        wrong = '??? wrong "If it goes wrong: a frame is missing"\n    Add it by hand.\n'
        self.cases('R8', (
            ('an interface page\'s entries', 'interface/viewer.md', entry, (entry + '\n') * 7,
             '`## When it goes wrong` has 7 entries, over the limit of 6 for an interface page'),
            ('an interface page\'s words', 'interface/viewer.md', 'The viewer shows one image.', words(1200),
             'words, over the limit of 1200 for an interface page'),
            ('a tutorial\'s steps', 'start/first.md', '### Run\n\nPress Run.\n', items(12, '### Step %d\n\nDo it.\n\n'),
             '`## Steps` has 13 steps, over the limit of 12 for a tutorial page'),
            ('two wrong boxes in a step', 'start/first.md', wrong, wrong + '\n' + wrong,
             'the step "Add the frames" has 2 `wrong` boxes, over the limit of 1'),
            ('a workflow\'s words', 'workflows/deep.md', 'A finished picture.', words(2500),
             'over the limit of 2500 for a workflow page'),
            ('a glossary entry', 'reference/glossary.md', 'The median absolute deviation.', words(81),
             'the entry "MAD" has 81 words, over the limit of 80 for a glossary entry'),
        ))

    def test_there_is_no_lower_limit(self):
        self.page('workflows/deep.md', WORKFLOW.replace('You take one target through the pipeline.\n', ''))
        self.assertPasses()


class VocabularyRuleTests(WritingCase):
    def test_a_forbidden_word_fails_wherever_the_page_says_it(self):
        self.cases('R9', (
            ('in the prose', 'processes/ht.md', 'Watch the preview.', 'Verify the preview.', 'ht.md:43: R9 says '
             '"Verify": what proves a sentence belongs in the repository'),
            ('in a heading', 'assets/MEDIA_PROVENANCE.md', '# Media Provenance', '# Media Evidence',
             'says "Evidence"'),
            ('in the summary', 'processes/ht.md', 'summary: Stretches the image',
             'summary: As implemented in the current build, stretches the image', 'ht.md:7: R9 says "current build"'),
            ('in the glance', 'processes/ht.md', 'inputs: One image view.', 'inputs: One validated image view.',
             'says "validated"'),
            ('in a control\'s facet', 'processes/ht.md', '  effect: Lower brightens the image.',
             '  effect: Lower brightens the image.\n  note: Source-derived.', 'ht.md:54: R9 says "Source-derived"'),
            ('in a caption', 'processes/ht.md', 'caption: The window as it opens.',
             'caption: The window, checked 2026-11-09.', 'says "checked 2026-11-09"'),
            ('a function in the prose', 'processes/ht.md', 'Watch the preview.', 'Watch what applyStretch() does.',
             'says "applyStretch()": a reader cannot use the name of a function'),
            ('a source file in a code span', 'processes/ht.md', 'Watch the preview.',
             'Read `HistogramProcess.cpp`.', 'says "HistogramProcess.cpp"'),
            ('a scoped name in a code span', 'processes/ht.md', 'Watch the preview.', 'It is `an::ui::Stretch`.',
             'says "an::ui::Stretch"'),
            ('a test in a code span', 'processes/ht.md', 'Watch the preview.', 'See `Test_ProcessesTone`.',
             'says "Test_ProcessesTone"'),
            ('a comment for the authors', 'processes/ht.md', '## Related', '<!-- to be completed -->\n\n## Related',
             'the page has an HTML comment'),
        ))

    def test_one_word_is_one_finding(self):
        self.change('processes/ht.md', 'Watch the preview.', 'See `test_stretch.cpp` of `Test_Stretch`.')
        self.assertFails('R9', 'says "test_stretch.cpp"', 'says "Test_Stretch"', count=2)

    def test_code_math_and_addresses_are_not_what_the_page_says(self):
        self.change('processes/ht.md', 'Watch the preview.', 'Type `mtf()` or read [the paper]'
                    '(https://example.org/verified/evidence.html) and <https://example.org/validation>. '
                    '\\( \\mathrm{evidence} \\)\n\n    ```text\n    verify() and validate\n    ```\n\n    \\[\n    '
                    '\\mathrm{validation}\n    \\]')
        self.assertPasses()

    def test_a_label_of_the_window_is_not_the_pages_word(self):
        self.inventory('tone_brightness.actionHT', 'Histogram Transformation <{image}>',
                       [ui_control('Validate', 'spinbox', 'midtonesValue', default='0.50', min=0, max=1)])
        self.change('processes/ht.md', '- name: Midtones', '- name: Validate')
        self.assertPasses()

    def test_the_allow_list_allows_a_text_on_its_page_only(self):
        allow = ('allow:\n  - page: processes/ht.md\n    text: "**Validate masters**"\n    reason: The button\'s '
                 'label.\n')
        self.write('docs/user-guide/data/vocabulary.yml', REAL_DATA['vocabulary.yml'].replace('allow: []\n', allow))
        self.change('processes/ht.md', 'Watch the preview.', 'Press **Validate masters**.')
        self.assertPasses()
        self.change('processes/ht.md', 'Watch the preview.', 'Press **Validate masters** to validate them.')
        self.assertFails('R9', 'says "validate"')
        self.restore()
        self.change('interface/viewer.md', 'Zoom to fit.', 'Press **Validate masters**.')
        code, output = self.run_main()
        self.assertEqual(code, 1, output)
        self.assertIn('viewer.md:27: R9 says "Validate"', output)
        self.assertRegex(output.replace('\\', '/'), r'data/vocabulary\.yml:\d+: R9 warning: the allow-list allows '
                         r'"\*\*Validate masters\*\*" on processes/ht\.md, which does not say it')

    def test_an_allowance_for_a_page_the_guide_lacks_is_a_warning(self):
        allow = 'allow:\n  - {page: processes/gone.md, text: Validate, reason: A label.}\n'
        self.write('docs/user-guide/data/vocabulary.yml', REAL_DATA['vocabulary.yml'].replace('allow: []\n', allow))
        self.assertWarns('R9', 'the allow-list names processes/gone.md, which is not a page')


class FigureRuleTests(WritingCase):
    def test_each_break_of_the_figures_fails(self):
        opening = ('```figure\nimage: ../assets/images/ht.png\nalt: The window with its histogram.\ncaption: The '
                   'window as it opens.\npins:\n  - {n: 1, x: 10, y: 10}\n  - {n: 3, x: 50, y: 50}\n```\n')
        self.cases('R10', (
            ('a picture outside a figure block', 'workflows/deep.md', 'A finished picture.',
             'A finished picture.\n\n![The picture](../assets/images/ht.png)', 'deep.md:35: R10 the picture is not '
             'in a `figure` block'),
            ('a figure without a caption', 'processes/ht.md', 'caption: The window as it opens.\n', '',
             'the figure has no `caption`'),
            ('a picture without an alternative text', 'processes/ht.md', '    alt: The master after.\n', '',
             'the figure has a picture without `alt`'),
            ('a process page whose figure comes after its first section', 'processes/ht.md', opening +
             '\n## Before you start\n', '## Before you start\n\n' + opening,
             'A process page opens with one figure of its window, before its first section; this one has 0 figures'),
            ('a process page with two', 'processes/ht.md', opening, opening + '\n' + opening.replace('ht.png',
                                                                                                    'before.png'),
             'this one has 2 figures there'),
            ('a pin without a step', 'processes/ht.md', '{n: 3, x: 50, y: 50}', '{n: 4, x: 50, y: 50}',
             'ht.md:27: R10 pin 4 matches no step; the page has 3 steps'),
            ('an item without a pin', 'interface/viewer.md', '  - {n: 2, x: 60, y: 60}\n', '',
             'item 2 of `## What you see` has no pin on the opening figure'),
            ('a tutorial\'s pin without a step', 'start/first.md', '{n: 2, x: 60, y: 60}', '{n: 5, x: 60, y: 60}',
             'pin 5 matches no step; the page has 2 steps'),
        ))

    def test_an_opening_pair_is_not_the_window(self):
        self.page('interface/viewer.md', INTERFACE.replace(
            'image: ../assets/images/viewer.png\nalt: The viewer with its toolbar.\ncaption: The parts of the '
            'viewer.\npins:\n  - {n: 1, x: 10, y: 10}\n  - {n: 2, x: 60, y: 60}\n',
            'images:\n  - image: ../assets/images/viewer.png\n    alt: Before.\n    label: Before\n  - image: '
            '../assets/images/first.png\n    alt: After.\n    label: After\ncaption: A pair.\n'))
        self.assertFails('R10', 'An interface page opens with one figure of its window', 'a before-and-after pair')

    def test_a_result_centric_process_without_a_pair_is_a_warning(self):
        pair = PROCESS[PROCESS.index('```figure\nimages:'):PROCESS.index('## Starting points')]
        self.page('processes/ht.md', PROCESS.replace(pair, ''))
        self.page('assets/MEDIA_PROVENANCE.md', PROVENANCE)
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('ht.md:68: R10 warning: a Stretching process is judged by its result, and the page shows no '
                      'before-and-after pair under `### Worked example`', output)

    def test_a_process_of_another_group_needs_no_pair(self):
        pair = PROCESS[PROCESS.index('```figure\nimages:'):PROCESS.index('## Starting points')]
        self.page('processes/ht.md', PROCESS.replace(pair, ''))
        self.write('docs/user-guide/mkdocs.yml', MKDOCS[:MKDOCS.index('nav:')] +
                   WRITING_NAV.replace('Stretching:', 'Geometry:'))
        code, found = self.findings('R10')
        self.assertEqual((code, found), (0, []))


class ImageFileRuleTests(WritingCase):
    def test_each_break_of_an_image_file_fails_when_a_rewritten_page_shows_it(self):
        for name, data, fragment in (
                ('no image', b'GIF89a' + b'\0' * 64, 'the picture is not a PNG, WebP or JPEG file'),
                ('too large', png(1601, 900), 'the picture is 1601 px on its longest side, over the limit of 1600 px'),
                ('too heavy', png(800, 600, 409601), 'the picture weighs 401 KB, over the limit of 400 KB'),
        ):
            with self.subTest(name):
                self.image('assets/images/viewer.png', data)
                self.assertFails('R11', 'assets/images/viewer.png:1: R11 ' + fragment,
                                 '(shown by interface/viewer.md)')

    def test_the_limits_themselves_pass(self):
        self.image('assets/images/viewer.png', png(1600, 1600, 409600))
        self.assertPasses()

    def test_a_picture_without_a_provenance_row_fails(self):
        self.change('assets/MEDIA_PROVENANCE.md', '| `assets/images/viewer.png` | Captured by the maintainer | '
                    '2026-11-09 |\n', '')
        self.assertFails('R11', 'the picture has no row in assets/MEDIA_PROVENANCE.md')

    def test_a_picture_only_pages_not_yet_rewritten_show_is_counted(self):
        self.image('assets/images/viewer.png', png(3000, 900))
        self.page('interface/viewer.md', INTERFACE.replace(*NOT_REWRITTEN))
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R11 image files: 0 error(s), 1 warning(s)', output)
        self.assertNotIn(': R11 ', output)
        # A rewritten page that shows it too makes it that page's picture.
        self.change('workflows/deep.md', 'A finished picture.', 'A finished picture.\n\n```figure\nimage: '
                    '../assets/images/viewer.png\nalt: The viewer.\ncaption: The picture in the viewer.\n```')
        self.assertFails('R11', 'over the limit of 1600 px (shown by workflows/deep.md)')


class ProcessKeyRuleTests(WritingCase):
    def test_a_rewritten_process_page_states_every_key(self):
        self.cases('R12', (
            ('no preview', 'processes/ht.md', 'preview: real-time\n', '', 'a rewritten process page states `preview`'),
            ('no derived', 'processes/ht.md', 'derived: false\n', '', 'a rewritten process page states `derived`'),
            ('no glance', 'processes/ht.md', 'glance:\n  enabled: An image view is active.\n  inputs: One image '
             'view.\n  outputs: The same view, replaced.\n  state: A master that was never stretched.\n', '',
             'a rewritten process page states `glance`'),
            ('a glance without its state', 'processes/ht.md', '  state: A master that was never stretched.\n', '',
             'ht.md:15: R12 `glance` lacks `state`; it states enabled, inputs, outputs, state'),
        ))


class GlossaryRuleTests(WritingCase):
    def test_the_first_use_of_a_term_without_its_link_is_a_warning(self):
        self.change('processes/ht.md', '[linear](../reference/glossary.md#linear)', 'linear')
        self.assertWarns('R13', 'ht.md:38: R13 warning: "Linear" is a term of the glossary; link its first use')

    def test_a_later_use_needs_no_link(self):
        self.change('processes/ht.md', 'Watch the preview.', 'The image is still linear here.')
        self.assertPasses()

    def test_a_first_use_before_the_linked_one_is_a_warning(self):
        self.change('processes/ht.md', 'The process maps the image', 'The process maps the linear image')
        self.assertWarns('R13', 'ht.md:25: R13 warning: "Linear"')

    def test_a_plural_is_the_term_and_a_longer_word_is_not(self):
        self.change('workflows/deep.md', 'A finished picture.', 'Nonlinear and linearized data, then linears.')
        self.assertWarns('R13', 'deep.md:33: R13 warning: "Linear"')

    def test_a_term_in_capitals_matches_only_in_capitals(self):
        self.change('workflows/deep.md', 'A finished picture.', 'A mad rush.')
        self.assertPasses()
        self.change('workflows/deep.md', 'A finished picture.', 'The MAD of the background.')
        self.assertWarns('R13', '"MAD" is a term of the glossary')

    def test_code_headings_and_a_link_elsewhere_do_not_count(self):
        self.change('workflows/deep.md', '### Stretch\n', '### Stretch the linear master\n\nSet `linear` off.\n')
        self.assertPasses()
        self.change('workflows/deep.md', 'A finished picture.', 'A [linear](../processes/ht.md) picture.')
        self.assertPasses()

    def test_a_name_the_page_repeats_is_no_use_and_the_next_use_is_the_first(self):
        for name in ('the [Linear Fit](../processes/ht.md) page', '**Linear Fit**', 'the message "not linear"'):
            with self.subTest(name=name):
                self.change('workflows/deep.md', 'A finished picture.', 'Open %s.' % name)
                self.assertPasses()
                self.change('workflows/deep.md', 'A finished picture.', 'Open %s, then a linear picture.' % name)
                self.assertWarns('R13', 'deep.md:33: R13 warning: "Linear"')

    def test_a_name_linked_to_its_entry_is_the_linked_first_use(self):
        self.change('workflows/deep.md', 'A finished picture.',
                    'Choose **[Linear](../reference/glossary.md#linear) Fit**, then a linear picture.')
        self.assertPasses()

    def test_the_glossary_itself_links_nothing(self):
        self.change('reference/glossary.md', 'The median absolute deviation.', 'A linear estimate of spread.')
        self.assertPasses()


CONTROLS_BLOCK = ('```controls\n- name: Midtones\n  kind: spinbox\n  default: "0.50"\n  widget: {min: 0, max: 1}\n  '
                  'effect: Lower brightens the image.\n```\n')
CHANNEL_ENTRY = ('- name: Channel\n  kind: combo\n  default: RGB\n  options: [RGB, Red]\n  effect: Chooses what the '
                 'transfer acts on.\n')
CURVES_PAGE = ('---\nkind: process\nstage: finish\nmenu: Process > Intensity Transformations > Curves Transformation\n'
               'summary: Reshapes tone.\n---\n# Curves Transformation\n')


class ControlsRuleTests(WritingCase):
    """The window of the process page: Midtones, a toolbar button every preview shares and the window's Apply."""

    def window(self, *controls):
        """The window with these controls beside the ones it has."""
        self.inventory('tone_brightness.actionHT', 'Histogram Transformation <{image}>', HT_WINDOW + list(controls))

    def block(self, new):
        """The page with its `controls` block replaced."""
        self.change('processes/ht.md', CONTROLS_BLOCK, new)

    def test_the_page_is_held_to_its_window_and_shared_controls_are_not_asked_for(self):
        self.assertPasses()
        code, output = self.run_main()
        self.assertIn('controls: 1 of the 1 windows of the inventory described by rewritten pages', output)

    def test_each_break_of_a_control_fails_on_its_line(self):
        self.cases('R14', (
            ('a label in another case', 'processes/ht.md', '- name: Midtones', '- name: midtones',
             'ht.md:49: R14 "midtones" is no control of the window "Histogram Transformation"; the window labels it '
             '"Midtones"'),
            ('another kind', 'processes/ht.md', 'kind: spinbox', 'kind: slider',
             '"Midtones" is a spin box in the window, and the block says `kind: slider`'),
            ('another default', 'processes/ht.md', 'default: "0.50"', 'default: "0.25"',
             '"Midtones" opens with "0.50" in the window, and the block says `default: "0.25"`'),
            ('another number', 'processes/ht.md', 'widget: {min: 0, max: 1}', 'widget: {min: 0, max: 2}',
             '"Midtones": `widget.max` is 2, and the window has 1'),
        ))

    def test_a_page_checked_by_name_says_how_many_words_it_has(self):
        code, output = self.run_main('--page', 'processes/ht.md')
        self.assertEqual(code, 0, output)
        self.assertRegex(output, r'(?m)^processes/ht\.md: \d+ words, of a limit of 1800$')
        code, output = self.run_main()
        self.assertNotIn(' words, of a limit of ', output)

    def test_a_readout_is_no_control_of_the_inventory(self):
        readout = ('- name: Clipped\n  kind: readout\n  default: "0 - 0.000000%"\n  effect: Counts the samples the '
                   'transfer sends to black.\n')
        self.block(CONTROLS_BLOCK[:-len('```\n')] + readout + '```\n')
        self.assertPasses()
        # The same entry as a control is one the window lacks.
        self.block(CONTROLS_BLOCK[:-len('```\n')] + readout.replace('kind: readout', 'kind: field') + '```\n')
        self.assertFails('R14', '"Clipped" is no control of the window "Histogram Transformation"')

    def test_a_control_the_window_lacks_fails_and_leaves_the_windows_own_unlisted(self):
        self.change('processes/ht.md', '- name: Midtones', '- name: Middle')
        self.assertFails('R14', 'ht.md:49: R14 "Middle" is no control of the window "Histogram Transformation"',
                         'has the spin box "Midtones", which no `controls` block lists', count=2)
        self.change('processes/ht.md', '- name: Midtones', '- name: Midtones\n  object: midValue')
        self.assertFails('R14', '"Midtones" states `object: midValue`, and the window "Histogram Transformation" has '
                         'no control of that object name', 'has the spin box "Midtones"', count=2)

    def test_a_number_the_window_fixes_is_stated(self):
        self.inventory('tone_brightness.actionHT', 'Histogram Transformation <{image}>',
                       [ui_control('Midtones', 'spinbox', 'midtonesValue', default='0.50', min=0, max=1, step=0.01,
                                   decimals=2)])
        self.assertFails('R14', '"Midtones": the window has `step` 0.01, and the block\'s `widget` does not state it',
                         '"Midtones": the window has `decimals` 2', count=2)
        self.block(CONTROLS_BLOCK.replace('{min: 0, max: 1}', '{min: 0.0, max: 1, step: 0.010, decimals: 2}'))
        self.assertPasses()

    def test_what_follows_the_image_is_the_writers_to_say(self):
        self.inventory('tone_brightness.actionHT', 'Histogram Transformation <{image}>',
                       [ui_control('Midtones', 'spinbox', 'midtonesValue', min=0, max=1, varies=['default', 'max']),
                        ui_control('Channel', 'combo', 'channelSelector', varies=['default', 'options'])])
        self.block(CONTROLS_BLOCK.replace('default: "0.50"', 'default: "640"')
                   .replace('{min: 0, max: 1}', '{min: 0}\n  range: Up to the width of the image.')
                   .replace('```\n', CHANNEL_ENTRY.replace('[RGB, Red]', '[RGB, Luminance]') + '```\n'))
        self.assertPasses()

    def test_a_control_no_block_lists_fails_unless_the_page_says_why(self):
        self.window(ui_control('Shadows', 'spinbox', 'shadowsValue', 'Clipping', default='0.00', min=0, max=1))
        self.assertFails('R14', 'ht.md:46: R14 the window "Histogram Transformation" has the spin box "Shadows" (under '
                         '"Clipping"), which no `controls` block lists')
        self.change('processes/ht.md', 'derived: false\n', 'derived: false\ncontrols_ignore:\n  - {label: Shadows, '
                    'reason: Moved by the same handle as Midtones.}\n')
        self.assertPasses()

    def test_an_excuse_for_a_control_that_needs_none_is_a_warning(self):
        self.change('processes/ht.md', 'derived: false\n', 'derived: false\ncontrols_ignore:\n  - {label: Midtones, '
                    'reason: None.}\n  - {label: Gone, reason: None.}\n')
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('ht.md:21: R14 warning: `controls_ignore` names "Midtones", which excuses no control', output)
        self.assertIn('R14 warning: `controls_ignore` names "Gone"', output)
        self.assertIn('R14 controls of the windows: 0 error(s), 2 warning(s)', output)

    def test_a_label_the_window_has_twice_is_told_apart_by_its_object(self):
        self.window(ui_control('Midtones', 'slider', 'midtonesSlider', default='50', min=0, max=100))
        self.assertFails('R14', 'the window "Histogram Transformation" has 2 controls labelled "Midtones"; say which '
                         'one this is with `object`: midtonesValue, midtonesSlider')
        named = PROCESS.replace('- name: Midtones', '- name: Midtones\n  object: midtonesValue')
        self.page('processes/ht.md', named)
        self.assertFails('R14', 'has the slider "Midtones" (object `midtonesSlider`), which no `controls` block lists')
        self.page('processes/ht.md', named.replace('derived: false\n', 'derived: false\ncontrols_ignore:\n  - {label: '
                                                   'Midtones, object: midtonesSlider, reason: The handle of the spin '
                                                   'box.}\n'))
        self.assertPasses()

    def test_a_control_listed_twice_fails(self):
        self.block(CONTROLS_BLOCK.replace('```\n', '- name: Midtones\n  object: midtonesValue\n  kind: spinbox\n  '
                                          'default: "0.50"\n  widget: {min: 0, max: 1}\n  effect: The same.\n```\n'))
        self.assertFails('R14', 'ht.md:54: R14 "Midtones" is the control "Midtones" of line 49 again')

    def test_a_control_is_called_by_its_label_unless_the_window_gives_it_none(self):
        self.change('processes/ht.md', '- name: Midtones', '- name: Middle tones\n  object: midtonesValue')
        self.assertFails('R14', '"Middle tones" states `object: midtonesValue`, which the window "Histogram '
                         'Transformation" labels "Midtones"; a control is called by its label')
        self.inventory('tone_brightness.actionHT', 'Histogram Transformation <{image}>',
                       [ui_control('midtonesValue', 'spinbox', 'midtonesValue', default='0.50', min=0, max=1)])
        self.assertPasses()

    def test_a_list_the_window_opens_empty_is_the_writers_to_fill(self):
        self.window(ui_control('Catalog', 'combo', 'catalogSelector', default='', options=[]))
        self.block(CONTROLS_BLOCK.replace('```\n', CHANNEL_ENTRY.replace('Channel', 'Catalog') + '```\n'))
        self.assertPasses()

    def test_the_choices_of_a_combo_are_the_windows(self):
        self.window(ui_control('Channel', 'combo', 'channelSelector', default='RGB', options=['RGB', 'Red']))
        for options, message in (('[RGB]', 'the window also has "Red"'),
                                 ('[RGB, Red, Blue]', 'the window has no "Blue"'),
                                 ('[Red, RGB]', 'the window lists them as RGB, Red')):
            with self.subTest(options):
                self.restore()
                self.block(CONTROLS_BLOCK.replace('```\n', CHANNEL_ENTRY.replace('[RGB, Red]', options) + '```\n'))
                self.assertFails('R14', '"Channel": its `options` are not the window\'s: ' + message)
        self.restore()
        self.block(CONTROLS_BLOCK.replace('```\n', CHANNEL_ENTRY + '```\n'))
        self.assertPasses()

    def test_a_block_named_for_a_tab_holds_the_controls_of_the_tab(self):
        self.window(ui_control('Channel', 'combo', 'channelSelector', 'Options > Advanced', default='RGB',
                               options=['RGB', 'Red']),
                    ui_control('Channel', 'combo', 'outputChannel', 'Output', default='RGB', options=['RGB', 'Red']))
        tabs = ('\n### Advanced\n\n```controls\nwindow: Advanced\ncontrols:\n' +
                CHANNEL_ENTRY.replace('- ', '  - ').replace('\n  ', '\n    ') + '```\n\n### Output\n\n```controls\n'
                'window: Output\ncontrols:\n' + CHANNEL_ENTRY.replace('- ', '  - ').replace('\n  ', '\n    ') + '```\n')
        self.block(CONTROLS_BLOCK + tabs)
        self.assertPasses()
        self.block(CONTROLS_BLOCK + tabs.replace('window: Output', 'window: Advanced', 1)
                   .replace('### Output', '### Advanced again'))
        self.assertFails('R14', '"Channel" is the control "Channel" of line', 'has the combo box "Channel" (under '
                         '"Output", object `outputChannel`)', count=2)
        self.restore()
        self.block(CONTROLS_BLOCK + tabs.replace('- name: Channel', '- name: Mode', 1))
        self.assertFails('R14', '"Mode" is no control of "Advanced" in the window "Histogram Transformation"',
                         'has the combo box "Channel" (under "Options > Advanced"', count=2)

    def test_a_block_of_a_dialog_the_window_opens_is_not_held(self):
        self.block(CONTROLS_BLOCK + '\n### Settings\n\n```controls\nwindow: Stretch settings\ncontrols:\n  - name: '
                   'Anything\n    kind: check\n    default: "On"\n    effect: It is the dialog\'s.\n```\n')
        self.assertPasses()

    def test_a_page_without_a_block_of_its_window_fails_once(self):
        self.block(CONTROLS_BLOCK.replace('- name: Midtones', 'window: Histogram\ncontrols:\n  - name: Midtones')
                   .replace('\n  kind', '\n    kind').replace('\n  default', '\n    default')
                   .replace('\n  widget', '\n    widget').replace('\n  effect', '\n    effect'))
        self.assertFails('R14', 'ht.md:46: R14 no `controls` block is of the window "Histogram Transformation"')

    def test_controls_in_another_order_than_the_windows_are_a_warning(self):
        self.window(ui_control('Channel', 'combo', 'channelSelector', default='RGB', options=['RGB', 'Red']))
        self.block(CONTROLS_BLOCK.replace('- name: Midtones', CHANNEL_ENTRY + '- name: Midtones'))
        self.assertWarns('R14', '"Midtones" comes before "Channel" in the window "Histogram Transformation", and '
                         'after it on the page')

    def test_a_page_of_two_windows_says_which_window_a_block_is_of(self):
        self.inventory('tone_brightness.actionCurves', 'Curves Transformation <{image}>',
                       [ui_control('Interpolation', 'combo', 'interpolation', default='Cubic',
                                   options=['Linear', 'Cubic'])])
        self.assertFails('R14', 'the page describes 2 windows ("Histogram Transformation", "Curves Transformation"); '
                         'the block says which one it is of with `window`',
                         'no `controls` block is of the window "Histogram Transformation"',
                         'no `controls` block is of the window "Curves Transformation"', count=3)

    def test_a_window_belongs_to_the_page_that_names_it_most_exactly(self):
        self.inventory('tone_brightness.actionCurves', 'Curves Transformation <{image}>',
                       [ui_control('Interpolation', 'combo', 'interpolation', default='Cubic',
                                   options=['Linear', 'Cubic'])])
        self.write('docs/user-guide/mkdocs.yml', MKDOCS[:MKDOCS.index('nav:')] + WRITING_NAV.replace(
            '          - Histogram Transformation: processes/ht.md\n',
            '          - Histogram Transformation: processes/ht.md\n          - Curves: processes/curves.md\n'))
        self.page('processes/curves.md', CURVES_PAGE)
        code, output = self.run_main()
        self.assertEqual(code, 0, output)
        self.assertIn('R14 controls of the windows: 0 error(s), 0 warning(s)', output)
        self.assertIn('controls: 1 of the 2 windows of the inventory described by rewritten pages', output)

    def test_a_page_not_yet_rewritten_is_not_held_to_its_window(self):
        self.page('processes/ht.md', PROCESS.replace(*NOT_REWRITTEN).replace('- name: Midtones', '- name: Middle'))
        code, output = self.run_main('--pending')
        self.assertEqual(code, 0, output)
        self.assertIn('R14 controls of the windows: 0 error(s), 0 warning(s)', output)

    def test_an_inventory_that_cannot_be_read_stops_the_check(self):
        self.write('docs/user-guide/data/ui/tone_brightness.actionHT.json', '{"id": "tone_brightness.actionHT"}\n')
        code, output = self.run_main()
        self.assertEqual(code, 2)
        self.assertIn("the guide's data cannot be read", output)
        self.assertIn('an inventory states `id`, `window` and `controls`', output)


class WritingDataTests(WritingCase):
    def test_limits_that_cannot_be_read_stop_the_check(self):
        self.write('docs/user-guide/data/limits.yml', REAL_DATA['limits.yml'].replace('    steps: 9', '    stepz: 9'))
        code, output = self.run_main()
        self.assertEqual(code, 2)
        self.assertIn("the guide's data cannot be read", output)
        self.assertIn('`process.stepz` is no limit of a process page', output)

    def test_a_pattern_that_is_no_expression_stops_the_check(self):
        self.write('docs/user-guide/data/vocabulary.yml',
                   REAL_DATA['vocabulary.yml'].replace("'source-derived'", "'source-derived('"))
        code, output = self.run_main()
        self.assertEqual(code, 2)
        self.assertIn('is not a regular expression', output)

    def test_a_changed_limit_is_the_limit(self):
        self.write('docs/user-guide/data/limits.yml',
                   REAL_DATA['limits.yml'].replace('    steps: 9', '    steps: 2', 1))
        self.assertFails('R8', '`## Steps` has 3 steps, over the limit of 2 for a process page')


class TemplateTests(unittest.TestCase):
    """The templates a writer copies are held to the sections and the budgets the check holds a page to."""

    TEMPLATES = (('PROCESS_PAGE_TEMPLATE.md', 'process'), ('INTERFACE_PAGE_TEMPLATE.md', 'interface'),
                 ('TUTORIAL_PAGE_TEMPLATE.md', 'tutorial'), ('REFERENCE_PAGE_TEMPLATE.md', 'reference'))

    def setUp(self):
        self.model = guide._load_model()
        self.guide = os.path.join(guide.REPO_ROOT, 'docs', 'user-guide')
        self.check = guide._GuideCheck(self.model, guide.REPO_ROOT, self.guide, str)

    def template(self, name):
        path = os.path.join(self.guide, 'templates', name)
        return path, self.model.split_front_matter(self.model.read_text(path))

    def test_every_template_has_the_sections_and_shapes_of_its_kind(self):
        for name, kind in self.TEMPLATES:
            with self.subTest(name):
                path, front = self.template(name)
                self.assertIsNone(front.error)
                self.assertEqual((front.meta['kind'], front.meta.get('rewritten')), (kind, True))
                writing = guide._Writing(self.check, 'templates/' + name, path, front)
                writing.check_headings()
                self.assertEqual([str(finding) for finding in self.check.findings], [])
                stated = [heading.title for heading in writing.sections]
                if kind in guide.SECTIONS:
                    # A template shows every section, the optional ones too, one title where a section has two.
                    self.assertEqual(stated, [titles[0] for titles, _ in guide.SECTIONS[kind]])
                self.assertEqual(guide.TEMPLATE_FILES[kind], name)

    def test_the_budgets_a_template_states_are_those_of_the_limits(self):
        limits = self.check.limits
        shared = {'summary_words': limits.summary_words, 'table_columns': limits.table_columns,
                  'entry_words': limits.glossary_entry_words}
        for name, kind in self.TEMPLATES:
            with self.subTest(name):
                path, _ = self.template(name)
                stated = re.findall(r'at most ([\d,]+)\s[^()]*?\((\w+)\)', self.model.read_text(path))
                self.assertTrue(stated)
                for number, limit in stated:
                    expected = shared[limit] if limit in shared else limits.of(kind, limit)
                    self.assertEqual(int(number.replace(',', '')), expected, '%s: %s' % (name, limit))


class RepositoryTests(unittest.TestCase):
    def test_the_guide_passes_its_page_rules(self):
        stream = io.StringIO()
        code = guide.main([], stream)
        self.assertEqual(code, 0, stream.getvalue())


if __name__ == '__main__':
    unittest.main()
