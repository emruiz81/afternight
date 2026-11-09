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


class MainTests(SiteCase):
    def run_main(self, *arguments):
        stream = io.StringIO()
        code = guide.main(list(arguments), stream)
        return code, stream.getvalue()

    def test_a_clean_site_exits_zero_and_counts_what_it_read(self):
        code, output = self.run_main('--site', str(self.site))
        self.assertEqual(code, 0, output)
        self.assertIn('R1 built-site references: 0 error(s)', output)
        self.assertIn('2 HTML documents and 1 stylesheets', output)

    def test_a_finding_prints_as_path_line_rule_message_and_exits_one(self):
        self.write('index.html', '<a href="gone.html">Gone</a>\n')
        code, output = self.run_main('--site', str(self.site))
        self.assertEqual(code, 1, output)
        self.assertRegex(output, r'(?m)^.*index\.html:1: R1 <a href> names gone\.html, which is not in the site$')
        self.assertIn('R1 built-site references: 1 error(s), 0 warning(s)', output)

    def test_a_folder_without_a_built_site_is_an_error(self):
        empty = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, empty, True)
        code, output = self.run_main('--site', str(empty))
        self.assertEqual(code, 1)
        self.assertIn('build the guide first', output)

    def test_without_a_site_the_site_rule_does_not_run(self):
        code, output = self.run_main()
        self.assertEqual(code, 0)
        self.assertIn('R1 not run', output)

    def test_it_runs_as_a_script_with_only_the_standard_library(self):
        self.write('index.html', '<script src="https://cdn.example.org/lib.js"></script>\n')
        script = pathlib.Path(guide.__file__).resolve()
        ran = subprocess.run([sys.executable, '-I', str(script), '--site', str(self.site)], capture_output=True,
                             text=True)
        self.assertEqual(ran.returncode, 1, ran.stderr + ran.stdout)
        self.assertIn(': R1 <script src> loads https://cdn.example.org/lib.js from the network', ran.stdout)


if __name__ == '__main__':
    unittest.main()
