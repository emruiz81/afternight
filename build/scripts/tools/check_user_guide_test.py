# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_user_guide.

Run with:
    python build/scripts/tools/check_user_guide_test.py

Every rule has a site, or a page, that passes and one that fails; the cases plant them in a temporary directory.
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


REAL_STAGES = pathlib.Path(guide.REPO_ROOT) / 'docs' / 'user-guide' / 'data' / 'stages.yml'

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
    """A repository with the application sources R4 reads and a guide of five pages that passes every rule."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write('src/AfterNightControls/app/AfterNightAppUiBuilder.h', UI_BUILDER)
        self.write('src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp', REGISTRY)
        self.write('README.md', '# Repository\n')
        self.write('docs/user-guide/mkdocs.yml', MKDOCS)
        self.write('docs/user-guide/data/stages.yml', REAL_STAGES.read_text(encoding='utf-8'))
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
        self.assertIn('spare.webp:1: R5 no page, README.md, logo or favicon shows this image',
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


class RepositoryTests(unittest.TestCase):
    def test_the_guide_passes_its_page_rules(self):
        stream = io.StringIO()
        code = guide.main([], stream)
        self.assertEqual(code, 0, stream.getvalue())


if __name__ == '__main__':
    unittest.main()
