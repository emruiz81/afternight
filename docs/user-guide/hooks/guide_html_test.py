# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_html, on HTML in the shape Python-Markdown writes at the guide's pinned versions.

Run with the documentation toolchain's Python:
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import os
import sys
import unittest

import markdown

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import guide_html  # noqa: E402


def render(text):
    """Markdown as the guide renders it, for the parts these passes touch."""
    return markdown.markdown(text, extensions=['tables', 'toc', 'pymdownx.superfences', 'pymdownx.arithmatex'],
                             extension_configs={'toc': {'permalink': True},
                                                'pymdownx.arithmatex': {'generic': True}})


class LiftTests(unittest.TestCase):
    def test_the_h1_is_lifted_without_its_permalink(self):
        title_html, title, identifier, rest = guide_html.lift_h1(render('# Histogram *Transformation*\n\nText.\n'))
        self.assertEqual(title_html, 'Histogram <em>Transformation</em>')
        self.assertEqual(title, 'Histogram Transformation')
        self.assertEqual(identifier, 'histogram-transformation')
        self.assertNotIn('<h1', rest)
        self.assertIn('<p>Text.</p>', rest)

    def test_only_the_first_h1_is_lifted(self):
        _, title, _, rest = guide_html.lift_h1('<h1 id="a">A</h1>\n<p>x</p>\n<h1 id="b">B</h1>')
        self.assertEqual(title, 'A')
        self.assertIn('<h1 id="b">B</h1>', rest)

    def test_a_page_without_an_h1_is_unchanged(self):
        self.assertEqual(guide_html.lift_h1('<p>x</p>'), ('', '', '', '<p>x</p>'))

    def test_text_of_drops_tags_and_decodes_entities(self):
        self.assertEqual(guide_html.text_of('<b>Shadows</b> &amp; <i>Highlights</i>\n  set'),
                         'Shadows & Highlights set')


class ScrollerTests(unittest.TestCase):
    def test_tables_are_wrapped_in_a_focusable_scroller(self):
        html = guide_html.wrap_tables(render('| A | B |\n| --- | --- |\n| 1 | 2 |\n'))
        self.assertIn('<div class="table-wrap" tabindex="0"><table>', html)
        self.assertIn('</table></div>', html)

    def test_code_blocks_and_display_equations_can_take_the_focus(self):
        html = guide_html.focusable_scrollers(render('```bash\nls\n```\n\n$$\nx^2\n$$\n'))
        self.assertIn('<pre tabindex="0"', html)
        self.assertIn('<div class="arithmatex" tabindex="0">', html)
        self.assertEqual(guide_html.focusable_scrollers(html), html)

    def test_the_highlighters_line_anchors_leave_the_reading_and_tab_order(self):
        line = '<a id="__codelineno-0-1" name="__codelineno-0-1" href="#__codelineno-0-1"></a>ls'
        html = guide_html.focusable_scrollers('<pre><code>%s</code></pre>' % line)
        self.assertIn('<a id="__codelineno-0-1" aria-hidden="true" tabindex="-1" name=', html)
        self.assertEqual(guide_html.focusable_scrollers(html), html)

    def test_the_lead_ends_at_the_first_h2(self):
        lead, body = guide_html.split_lead(render('Opening.\n\n## Steps\n\nOne.\n'))
        self.assertEqual(lead.strip(), '<p>Opening.</p>')
        self.assertTrue(body.startswith('<h2 id="steps">'))
        self.assertEqual(guide_html.split_lead('<p>Only</p>'), ('<p>Only</p>', ''))


if __name__ == '__main__':
    unittest.main()
