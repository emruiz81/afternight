# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_search.

Run with the documentation toolchain's Python:
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
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


SEARCH_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'theme', 'assets', 'guide', 'js',
                             'search.js')

# Runs the theme's search script against a stand-in page and prints what it did: the scripts it added to the page,
# the note it showed, and the links of the results it listed. The page states its addresses as attributes of its
# root element; `linking` gives the script an index and an engine of its own, so it loads nothing and lists a result.
SEARCH_HARNESS = r'''
"use strict";
const fs = require("fs");
const vm = require("vm");
const source = fs.readFileSync(process.argv[2], "utf8");
const scenario = JSON.parse(process.argv[3]);

function element(tag) {
  const made = {
    tag: tag, children: [], attributes: {}, handlers: {}, hidden: true, value: "",
    setAttribute(name, value) { this.attributes[name] = String(value); },
    removeAttribute(name) { delete this.attributes[name]; },
    getAttribute(name) { return name in this.attributes ? this.attributes[name] : null; },
    appendChild(child) { this.children.push(child); return child; },
    addEventListener(name, handler) { this.handlers[name] = handler; },
    focus() {}, blur() {}, closest() { return null; },
  };
  Object.defineProperty(made, "innerHTML", { set() { made.children = []; }, get() { return ""; } });
  return made;
}

const root = element("html");
Object.keys(scenario.page).forEach(function (name) { root.setAttribute(name, scenario.page[name]); });
const input = element("input");
const list = element("div");
const head = element("head");
const document = {
  documentElement: root,
  head: head,
  getElementById(id) { return id === "guide-search" ? input : id === "guide-search-results" ? list : null; },
  querySelector() { return null; },
  createElement: element,
  addEventListener() {},
};
const sandbox = { document: document, Promise: Promise, location: { href: "" },
                  setTimeout(run) { run(); return 0; }, clearTimeout() {} };
sandbox.window = sandbox;
if (scenario.linking) {
  const lunr = function (build) {
    build.call({ ref() {}, field() {}, add() {} });
    return { query(ask) { ask({ term() {} }); return [{ ref: "0" }]; } };
  };
  lunr.tokenizer = {};
  lunr.stopWordFilter = function (word) { return word; };
  lunr.Query = { presence: { REQUIRED: 1 }, wildcard: { TRAILING: 1 } };
  sandbox.lunr = lunr;
  sandbox.__guideSearchIndex = { entries: [{ t: "Curves", p: "Curves", x: "A curve.", l: "processes/curves.html",
                                             s: "Processes", m: "" }] };
}
vm.createContext(sandbox);
vm.runInContext(source, sandbox);

input.value = "curves";
if (scenario.linking) { input.handlers.input(); } else { input.handlers.focus(); }

setTimeout(function () {
  const links = [];
  let note = "";
  list.children.forEach(function (child) {
    if (child.className === "note") { note = child.textContent; }
    child.children.forEach(function (option) { if (option.tag === "a") { links.push(option.href); } });
  });
  process.stdout.write(JSON.stringify({
    scripts: head.children.filter(function (child) { return child.tag === "script"; })
                          .map(function (child) { return child.src; }),
    note: note,
    links: links,
  }));
}, 50);
'''


@unittest.skipUnless(shutil.which('node'), 'the search script is run under Node, which is not installed')
class SearchScriptTests(unittest.TestCase):
    """The page states where the guide's own files are, and the search script holds each address to that.

    The theme writes the addresses itself, as paths within the site. The script still takes none on trust: an
    address with a scheme or a host of its own names another place, so no script is loaded from it and no result
    links to it. An address taken as written would run whatever a page that was altered named.
    """

    ENGINE = '../assets/guide/vendor/lunr-2.3.9.min.js'
    INDEX = '../search/search_index.js'

    def run_script(self, page, linking=False):
        with tempfile.TemporaryDirectory() as folder:
            harness = os.path.join(folder, 'harness.cjs')
            with open(harness, 'w', encoding='utf-8', newline='\n') as handle:
                handle.write(SEARCH_HARNESS)
            done = subprocess.run(['node', harness, SEARCH_SCRIPT, json.dumps({'page': page, 'linking': linking})],
                                  capture_output=True, text=True, encoding='utf-8', timeout=60, check=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        return json.loads(done.stdout)

    def test_the_files_of_the_guide_are_loaded_from_the_paths_the_page_states(self):
        for base, engine, index in (('..', self.ENGINE, self.INDEX),
                                    ('.', 'assets/guide/vendor/lunr-2.3.9.min.js', 'search/search_index.js'),
                                    ('/', '/assets/guide/vendor/lunr-2.3.9.min.js', '/search/search_index.js')):
            with self.subTest(base=base):
                did = self.run_script({'data-base': base, 'data-search-engine': engine, 'data-search-index': index})
                self.assertEqual(did['scripts'], [engine, index])
                self.assertEqual(did['note'], 'Preparing search\u2026')

    def test_no_script_is_loaded_from_an_address_of_another_place(self):
        for elsewhere in ('javascript:alert(1)', 'https://elsewhere.example/lunr.js', '//elsewhere.example/lunr.js',
                          'data:text/javascript,alert(1)', '..\\..\\elsewhere.js', ''):
            with self.subTest(engine=elsewhere):
                did = self.run_script({'data-base': '..', 'data-search-engine': elsewhere,
                                       'data-search-index': self.INDEX})
                self.assertEqual(did['scripts'], [self.INDEX], 'only the address within the guide is loaded')
                self.assertIn('could not load', did['note'])

    def test_a_result_links_into_the_guide_whatever_the_page_states_as_its_base(self):
        for base, link in (('..', '../processes/curves.html'), ('.', './processes/curves.html'),
                           ('/', '/processes/curves.html'), ('../..', '../../processes/curves.html'),
                           ('javascript:alert(1)//', './processes/curves.html'),
                           ('https://elsewhere.example', './processes/curves.html'),
                           ('//elsewhere.example', './processes/curves.html')):
            with self.subTest(base=base):
                did = self.run_script({'data-base': base, 'data-search-engine': self.ENGINE,
                                       'data-search-index': self.INDEX}, linking=True)
                self.assertEqual(did['scripts'], [], 'an index and an engine that are there are not loaded again')
                self.assertEqual(did['links'], [link])


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
