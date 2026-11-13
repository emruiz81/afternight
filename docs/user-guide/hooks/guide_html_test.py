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
    return markdown.markdown(text, extensions=['tables', 'toc', 'def_list', 'admonition', 'pymdownx.details',
                                               'pymdownx.tasklist', 'pymdownx.superfences', 'pymdownx.arithmatex'],
                             extension_configs={'toc': {'permalink': True},
                                                'pymdownx.tasklist': {'custom_checkbox': True},
                                                'pymdownx.arithmatex': {'generic': True}})


FIGURE = ('<figure class="an-figure"><div class="shotwrap"><a class="glightbox" href="w.png"><img src="w.png" alt="W">'
          '</a><span class="pin" style="left:1%;top:2%"><span class="visually-hidden">Step </span>1</span>'
          '<span class="pin" style="left:3%;top:4%"><span class="visually-hidden">Step </span>3</span></div>'
          '<figcaption>The window.</figcaption></figure>')


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

    def test_every_table_of_a_page_gets_its_own_scroller(self):
        table = '| A | B | C | D | E |\n| - | - | - | - | - |\n| 1 | 2 | 3 | 4 | 5 |\n'
        html = guide_html.wrap_tables(render(table + '\nText.\n\n' + table))
        self.assertEqual(html.count('<div class="table-wrap" tabindex="0"><table>'), 2)
        self.assertEqual(html.count('</table></div>'), 2)
        self.assertEqual(html.count('class='), 2)

    def test_code_blocks_and_display_equations_can_take_the_focus(self):
        html = guide_html.focusable_scrollers(render('```bash\nls\n```\n\n$$\nx^2\n$$\n'))
        self.assertIn('<pre tabindex="0"', html)
        self.assertIn('<div class="arithmatex" tabindex="0">', html)
        self.assertEqual(guide_html.focusable_scrollers(html), html)

    def test_an_element_of_a_class_is_told_from_the_class_written_about(self):
        html = render('Inline $x^2$.\n')
        self.assertTrue(guide_html.has_class(html, 'arithmatex'))
        self.assertFalse(guide_html.has_class(html, 'arith'))
        self.assertTrue(guide_html.has_class('<pre id="a" class="wide an-mermaid dark">x</pre>', 'an-mermaid'))
        self.assertFalse(guide_html.has_class('<pre class="an-mermaid-source">x</pre>', 'an-mermaid'))
        self.assertFalse(guide_html.has_class('<pre data-class="an-mermaid">x</pre>', 'an-mermaid'))
        written = render('The class `an-mermaid`, or class="an-mermaid".\n\n'
                         '```html\n<pre class="an-mermaid">x</pre>\n```\n')
        self.assertFalse(guide_html.has_class(written, 'an-mermaid'))

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


class DressTests(unittest.TestCase):
    PROCESS = ('Opening.\n\n## Before you start\n\n1. Not a step.\n\n## Steps\n\n1. **Open it.** Choose the menu.\n'
               '2. **Set it.** Move it.\n\n## When it goes wrong\n\nThe preview is black\n'
               ':   **Cause.** Too high.\n:   **Fix.** Lower it.\n\nNothing happens\n:   **Cause.** X. **Fix.** Y.\n')

    def test_a_process_pages_steps_become_step_cards(self):
        html = guide_html.dress_sections(render(self.PROCESS), 'process')
        self.assertEqual(html.count('<ol class="steps">'), 1)
        self.assertLess(html.index('id="steps"'), html.index('<ol class="steps">'))
        self.assertIn('<ol>\n<li>Not a step.</li>', html)

    def test_troubleshooting_definitions_become_cards_on_any_kind(self):
        for kind in ('process', 'interface', 'reference'):
            with self.subTest(kind=kind):
                html = guide_html.dress_sections(render(self.PROCESS), kind)
                self.assertIn('<dl class="tsc">', html)
                self.assertEqual(html.count('<div class="ts">'), 2)
                self.assertIn('<div class="ts"><dt>The preview is black</dt>\n<dd><strong>Cause.</strong> Too high.'
                              '</dd>\n<dd><strong>Fix.</strong> Lower it.</dd></div>', html)

    def test_a_definition_list_elsewhere_is_left_alone(self):
        html = guide_html.dress_sections(render('## Terms\n\nLinear\n:   Not stretched.\n'), 'reference')
        self.assertIn('<dl>', html)
        self.assertNotIn('class="ts"', html)

    def test_only_a_process_page_gets_step_cards(self):
        self.assertNotIn('class="steps"', guide_html.dress_sections(render(self.PROCESS), 'interface'))

    TUTORIAL = ('Opening.\n\n## What you need\n\n- [ ] Lights\n- [ ] Darks\n\n## Your camera\n\n- **Color.** One.\n'
                '- **Mono.** Two.\n\n## Steps\n\nThe numbers match.\n\nFIGURE\n\n### Add the frames\n\nDrop them.\n\n'
                '!!! see "You should see"\n    A list.\n\n### Check\n\nLook.\n\n### Run\n\nPress Run.\n\n'
                '## The result\n\nOne master.\n\n## Next\n\n- [Levels](levels.md)\n')

    def dressed(self, kind='tutorial', figure=FIGURE):
        return guide_html.dress_sections(render(self.TUTORIAL).replace('<p>FIGURE</p>', figure), kind)

    def test_a_tutorials_lists_become_the_checklist_the_branches_and_the_next_cards(self):
        html = self.dressed()
        self.assertIn('<ul class="task-list need">', html)
        self.assertIn('<ul class="branches">', html)
        self.assertIn('<ul class="nextg">', html)

    def test_a_tutorials_steps_sit_beside_their_figure_and_know_their_pins(self):
        html = self.dressed()
        tut = html[html.index('<div class="tut">'):html.index('<h2 id="the-result">')]
        self.assertLess(html.index('<p>The numbers match.</p>'), html.index('<div class="tut">'))
        self.assertTrue(tut.startswith('<div class="tut"><figure class="fig an-figure">'))
        self.assertEqual(tut.count('<figure'), 1)
        self.assertEqual(tut.count('<li'), 3)
        self.assertIn('<li><h3 id="add-the-frames">', tut)
        self.assertIn('<li class="nopin"><h3 id="check">', tut)
        self.assertIn('<li><h3 id="run">', tut)
        self.assertIn('<div class="admonition see">', tut[:tut.index('<li class="nopin">')])
        self.assertTrue(tut.rstrip().endswith('</ol></div>'))

    def test_steps_without_a_figure_run_the_full_width(self):
        html = self.dressed(figure='')
        self.assertIn('<div class="tut nofig"><ol class="tsteps">', html)
        self.assertNotIn('nopin', html)

    def test_the_result_is_boxed(self):
        self.assertIn('<div class="result"><p>One master.</p></div>', self.dressed('workflow'))

    def test_another_kind_keeps_a_tutorials_headings_plain(self):
        html = self.dressed('reference')
        for name in ('need', 'branches', 'nextg', 'tsteps', 'result'):
            self.assertNotIn('class="%s"' % name, html)
        self.assertNotIn(' need"', html)

    def test_dressing_twice_changes_nothing_more(self):
        once = guide_html.dress_sections(render(self.PROCESS), 'process')
        self.assertEqual(once.count('class="ts"'), guide_html.dress_sections(once, 'process').count('class="ts"'))

    def test_controls_are_listed_with_their_anchors(self):
        html = ('<section class="ctls"><div class="ctl" id="ctl-black-point"><div class="lab"><b>Black point</b>'
                '<small>spin box</small></div></div><div class="ctl" id="ctl-a-b"><div class="lab"><b>A &amp; B</b>')
        self.assertEqual(guide_html.controls_of(html), [('ctl-black-point', 'Black point'), ('ctl-a-b', 'A & B')])

    def test_the_accent_marks_its_words_in_the_title(self):
        self.assertEqual(guide_html.accent('Finish your pictures', 'your pictures'), 'Finish <em>your pictures</em>')
        self.assertEqual(guide_html.accent('Finish your pictures', 'nothing'), 'Finish your pictures')
        self.assertEqual(guide_html.accent('Finish', ''), 'Finish')


if __name__ == '__main__':
    unittest.main()
