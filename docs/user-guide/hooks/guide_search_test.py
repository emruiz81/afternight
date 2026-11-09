# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_search.

Run with the documentation toolchain's Python:
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import guide_search  # noqa: E402

CONTENT = (
    '<p>The <b>opening</b> of the page.</p>\n'
    '<h2 id="steps">Steps<a class="headerlink" href="#steps" title="Permanent link">&para;</a></h2>\n'
    '<p>Raise Shadows. <span class="arithmatex">\\(x^2\\)</span></p>\n'
    '<div class="highlight"><pre tabindex="0"><code>code is not searched</code></pre></div>\n'
    '<h3 id="by-hand">By hand</h3>\n<p>Move the handle.</p>\n'
    '<h3>No id</h3>\n<p>Still part of By hand? No: a heading without an id ends it.</p>\n'
    '<h2 id="empty">Empty</h2>\n'
)


class SectionTests(unittest.TestCase):
    def setUp(self):
        self.entries = guide_search.sections(CONTENT, 'processes/ht.html', 'Histogram Transformation', 'Processes',
                                             'Process > Intensity Transformations > Histogram Transformation')

    def test_the_lead_is_an_entry_titled_with_the_page(self):
        lead = self.entries[0]
        self.assertEqual(lead['l'], 'processes/ht.html')
        self.assertEqual(lead['t'], 'Histogram Transformation')
        self.assertEqual(lead['x'], 'The opening of the page.')
        self.assertEqual(lead['s'], 'Processes')
        self.assertEqual(lead['m'], 'Process > Intensity Transformations > Histogram Transformation')

    def test_each_h2_and_h3_with_an_id_is_an_entry_without_code_or_equations(self):
        self.assertEqual([entry['l'] for entry in self.entries],
                         ['processes/ht.html', 'processes/ht.html#steps', 'processes/ht.html#by-hand'])
        steps = self.entries[1]
        self.assertEqual(steps['t'], 'Steps')
        self.assertEqual(steps['x'], 'Raise Shadows.')
        self.assertEqual(steps['p'], 'Histogram Transformation')

    def test_an_empty_section_is_left_out(self):
        self.assertNotIn('processes/ht.html#empty', [entry['l'] for entry in self.entries])

    def test_a_page_with_no_menu_states_none(self):
        entries = guide_search.sections('<p>Text.</p>', 'reference/glossary.html', 'Glossary', 'Reference')
        self.assertEqual(entries, [{'l': 'reference/glossary.html', 't': 'Glossary', 'p': 'Glossary',
                                    's': 'Reference', 'x': 'Text.'}])


class ScriptTests(unittest.TestCase):
    def test_the_index_is_a_script_of_ascii_json(self):
        script = guide_search.index_script([{'l': 'a.html', 't': 'Gráfico', 'p': 'P', 's': 'S', 'x': '—'}])
        self.assertTrue(script.startswith('window.__guideSearchIndex='))
        self.assertTrue(script.rstrip().endswith(';'))
        script.encode('ascii')
        payload = json.loads(script[len('window.__guideSearchIndex='):script.rindex(';')])
        self.assertEqual(payload['version'], 1)
        self.assertEqual(payload['entries'][0]['t'], 'Gráfico')


if __name__ == '__main__':
    unittest.main()
