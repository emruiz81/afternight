# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_fences: the `controls` and `figure` blocks, read, checked and drawn.

Run with the documentation toolchain's Python:
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import logging
import os
import pathlib
import shutil
import sys
import tempfile
import unittest

import markdown

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import guide_fences as fences  # noqa: E402

FIXTURE_IMAGES = pathlib.Path(__file__).resolve().parent / 'fixtures' / 'components' / 'docs' / 'assets'

SPIN = """\
window: Levels
controls:
  - name: Black point
    kind: spinbox
    default: "0.25"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    range: Never above the white point.
    effect: Higher maps more samples to **black**; `0` keeps them.
    tradeoff: It can erase faint signal.
"""


def control(**changes):
    """A one-control block: a valid combo, with keys changed or (None) removed."""
    values = {'name': 'Channel', 'kind': 'combo', 'default': 'RGB', 'options': '[RGB, Red]', 'effect': 'Chooses.'}
    values.update(changes)
    return '- ' + '\n  '.join('%s: %s' % (key, value) for key, value in values.items() if value is not None) + '\n'


class ControlsParseTests(unittest.TestCase):
    def assertRefused(self, source, *fragments, line=None):
        with self.assertRaises(fences.FenceError) as raised:
            fences.parse_controls(source)
        for fragment in fragments:
            self.assertIn(fragment, str(raised.exception))
        if line is not None:
            self.assertEqual(raised.exception.line, line)

    def test_a_block_names_its_window_and_lists_its_controls(self):
        block = fences.parse_controls(SPIN)
        self.assertEqual(block['window'], 'Levels')
        self.assertEqual(block['controls'][0]['name'], 'Black point')
        self.assertEqual(block['controls'][0]['widget'], {'min': '0', 'max': '1', 'step': '0.01', 'decimals': '2'})

    def test_a_plain_list_is_a_block_without_a_window(self):
        block = fences.parse_controls(control())
        self.assertIsNone(block['window'])
        self.assertEqual(block['controls'][0]['options'], ['RGB', 'Red'])

    def test_words_that_yaml_reads_as_booleans_stay_words(self):
        block = fences.parse_controls(control(kind='check', default='Off', options=None))
        self.assertEqual(block['controls'][0]['default'], 'Off')

    def test_invalid_yaml_is_refused_with_its_line(self):
        self.assertRefused('window: Levels\ncontrols:\n  - name: [unclosed\n', 'not valid YAML', line=4)

    def test_an_unknown_kind_names_the_control(self):
        self.assertRefused(control(kind='dial'), 'control "Channel"', 'unknown kind `dial`', line=1)

    def test_a_missing_required_key_names_the_control(self):
        self.assertRefused(control(effect=None), 'control "Channel"', '`effect` is missing')
        self.assertRefused(control(name=None), 'control 1', '`name` is missing')

    def test_a_combo_needs_options_that_hold_its_default(self):
        self.assertRefused(control(options=None), 'control "Channel"', 'needs `options`')
        self.assertRefused(control(default='Luminance'), 'the default "Luminance" is not among its options')

    def test_a_spin_box_needs_its_range_and_a_default_inside_it(self):
        self.assertRefused(control(kind='spinbox', default='1', options=None), 'needs `widget.min` and `widget.max`')
        self.assertRefused(control(kind='spinbox', default='2', options=None, widget='{min: 0, max: 1}'),
                           'the default 2 is outside 0 to 1')
        self.assertRefused(control(kind='slider', default='0', options=None, widget='{min: 1, max: 0}'),
                           '`widget.min` 1 is above `widget.max` 0')
        self.assertRefused(control(kind='spinbox', default='0', options=None, widget='{min: 0, max: lots}'),
                           "`widget.max` is 'lots', not a number")

    def test_a_range_the_image_bounds_is_said_in_words(self):
        block = fences.parse_controls(control(kind='spinbox', default='0', options=None, widget='{min: 0, step: 1}',
                                              range='Up to the width of the image.'))
        self.assertEqual(block['controls'][0]['widget'], {'min': '0', 'step': '1'})
        fences.parse_controls(control(kind='spinbox', default='640', options=None,
                                      range='From one pixel to the width of the image.'))
        self.assertRefused(control(kind='spinbox', default='0', options=None, widget='{min: 0}'),
                           'needs `widget.min` and `widget.max`, or a `range`')
        self.assertRefused(control(kind='spinbox', default='-1', options=None, widget='{min: 0}',
                                   range='Up to the width of the image.'), 'the default -1 is outside from 0')
        self.assertRefused(control(kind='slider', default='9', options=None, widget='{max: 8}',
                                   range='From the smallest scale the image has.'), 'the default 9 is outside up to 8')

    def test_a_control_may_name_its_object_in_the_inventory(self):
        block = fences.parse_controls(control(object='channelSelector'))
        self.assertEqual(block['controls'][0]['object'], 'channelSelector')
        self.assertRefused(control(object='""'), '`object` is text')

    def test_a_label_the_window_has_twice_is_listed_once_for_each_object(self):
        block = fences.parse_controls(control(object='inputChannel') + control(object='outputChannel'))
        self.assertEqual([entry['object'] for entry in block['controls']], ['inputChannel', 'outputChannel'])
        self.assertRefused(control(object='inputChannel') + control(object='inputChannel'),
                           'control "Channel" is listed twice in the block', line=7)
        self.assertRefused(control(object='inputChannel') + control(),
                           'control "Channel" is listed twice in the block; when the window has several controls of '
                           'one label, each states its `object`')
        self.assertRefused(control() + control(object='outputChannel'), 'each states its `object`')

    def test_a_default_that_is_no_number_is_left_to_the_writer(self):
        block = fences.parse_controls(control(kind='spinbox', default='Auto', options=None,
                                              widget='{min: 0, max: 1}'))
        self.assertEqual(block['controls'][0]['default'], 'Auto')

    def test_unknown_keys_and_repeated_names_are_refused(self):
        self.assertRefused(control(colour='red'), 'unknown key `colour`')
        self.assertRefused(control() + control(), 'control "Channel" is listed twice', line=6)
        self.assertRefused('window: W\nrows: []\n', 'unknown key `rows`')
        self.assertRefused('controls:\n' + control(), '`window` names the window')
        self.assertRefused('window: W\ncontrols: []\n', 'lists no control')

    def test_a_key_stated_twice_is_refused(self):
        self.assertRefused('- name: A\n  name: B\n  kind: button\n  default: x\n  effect: y\n', '`name` is stated twice',
                           line=2)


class ControlsRenderTests(unittest.TestCase):
    def test_a_row_shows_the_widget_the_effect_and_the_facets(self):
        html = fences.render_controls(fences.parse_controls(SPIN))
        self.assertIn('<section class="ctls" aria-label="Controls of Levels">', html)
        self.assertIn('<p class="ctls-window">Levels</p>', html)
        self.assertIn('<div class="ctl" id="ctl-black-point"><div class="lab"><b>Black point</b>'
                      '<small>spin box</small></div>', html)
        self.assertIn('<span class="spin"><span class="v">0.25</span>', html)
        self.assertIn('<i style="left:25%"></i>', html)
        self.assertIn('Default <b>0.25</b> &middot; 0 to 1 &middot; step 0.01 &middot; 2 decimals', html)
        self.assertIn('<p class="eff">Higher maps more samples to <strong>black</strong>; <code>0</code> keeps them.'
                      '</p>', html)
        self.assertIn('<dt>Range</dt><dd>Never above the white point.</dd>', html)
        self.assertIn('<dt>Trade-off</dt><dd class="warn">It can erase faint signal.</dd>', html)

    def test_a_range_the_image_bounds_draws_no_handle_and_says_the_end_it_has(self):
        html = fences.render_controls(fences.parse_controls(control(
            kind='spinbox', default='0', options=None, widget='{min: 0, step: 1}', range='Up to the image width.')))
        self.assertIn('<span class="spin"><span class="v">0</span>', html)
        self.assertNotIn('class="slider"', html)
        self.assertIn('Default <b>0</b> &middot; from 0 &middot; step 1', html)
        self.assertIn('<dt>Range</dt><dd>Up to the image width.</dd>', html)
        html = fences.render_controls(fences.parse_controls(control(
            kind='spinbox', default='640', options=None, range='From one pixel to the image width.')))
        self.assertIn('<span class="state">Default <b>640</b></span>', html)

    def test_the_object_of_a_control_is_not_drawn(self):
        html = fences.render_controls(fences.parse_controls(control(object='channelSelector')))
        self.assertNotIn('channelSelector', html)

    def test_a_combo_lists_its_options_with_the_default_marked(self):
        html = fences.render_controls(fences.parse_controls(control()))
        self.assertIn('<section class="ctls" aria-label="Controls">', html)
        self.assertIn('<ul class="opts" aria-label="Choices"><li class="on">RGB</li><li>Red</li></ul>', html)

    def test_text_is_escaped(self):
        html = fences.render_controls(fences.parse_controls(control(name='"A <b> & c"', effect='"x < y"')))
        self.assertIn('<b>A &lt;b&gt; &amp; c</b>', html)
        self.assertIn('x &lt; y', html)

    def test_a_label_listed_for_two_objects_gets_two_anchors(self):
        html = fences.render_controls(fences.parse_controls(control(object='inputChannel') +
                                                            control(object='outputChannel')))
        self.assertIn('id="ctl-channel"', html)
        self.assertIn('id="ctl-channel-2"', html)

    def test_a_name_repeated_on_a_page_gets_an_anchor_of_its_own(self):
        used = set()
        first = fences.render_controls(fences.parse_controls(control()), used)
        second = fences.render_controls(fences.parse_controls('window: Options\ncontrols:\n' + control()), used)
        third = fences.render_controls(fences.parse_controls('window: Options\ncontrols:\n' + control()), used)
        self.assertIn('id="ctl-channel"', first)
        self.assertIn('id="ctl-options-channel"', second)
        self.assertIn('id="ctl-options-channel-2"', third)

    def test_a_facet_takes_code_bold_and_key_caps_and_nothing_else(self):
        self.assertEqual(fences.inline('Press ++ctrl+shift+k++ over **Preview** for `0.5`.'),
                         'Press <span class="keys"><kbd>Ctrl</kbd><span>+</span><kbd>Shift</kbd><span>+</span>'
                         '<kbd>K</kbd></span> over <strong>Preview</strong> for <code>0.5</code>.')
        # A label that holds an asterisk is bold whole, and a message that holds asterisks is quoted as code.
        self.assertEqual(fences.inline('**CIE L* (Lightness)** and **CIE a***'),
                         '<strong>CIE L* (Lightness)</strong> and <strong>CIE a*</strong>')
        self.assertEqual(fences.inline('`** PICK VALUE **` and 1 + 1 and [a link](b.md) and <b>'),
                         '<code>** PICK VALUE **</code> and 1 + 1 and [a link](b.md) and &lt;b&gt;')

    def test_every_kind_draws(self):
        for kind in fences.CONTROL_KINDS:
            extra = {'options': None}
            if kind in fences.RANGED_KINDS:
                extra.update(default='0', widget='{min: 0, max: 1}')
            elif kind in fences.CHOICE_KINDS:
                extra = {}
            with self.subTest(kind=kind):
                html = fences.render_controls(fences.parse_controls(control(kind=kind, **extra)))
                self.assertIn('<small>%s</small>' % fences.CONTROL_KINDS[kind], html)
                self.assertIn('class="w"', html)


class FigureTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        (self.root / 'docs' / 'pages').mkdir(parents=True)
        (self.root / 'docs' / 'assets').mkdir()
        for name in ('window.png', 'before.png', 'after.png'):
            shutil.copyfile(FIXTURE_IMAGES / name, self.root / 'docs' / 'assets' / name)
        (self.root / 'outside.png').write_bytes((FIXTURE_IMAGES / 'before.png').read_bytes())
        (self.root / 'docs' / 'assets' / 'notes.png').write_text('not a picture', encoding='utf-8')

    def parse(self, source):
        return fences.parse_figure(source, str(self.root / 'docs' / 'pages'), str(self.root / 'docs'))

    def assertRefused(self, source, *fragments):
        with self.assertRaises(fences.FenceError) as raised:
            self.parse(source)
        for fragment in fragments:
            self.assertIn(fragment, str(raised.exception))
        return raised.exception

    def test_a_figure_carries_its_size_its_lightbox_link_and_its_pins(self):
        figure = self.parse('image: ../assets/window.png\nalt: The "window".\ncaption: What it shows.\n'
                            'pins:\n  - {n: 1, x: 15, y: 20.5}\n  - {n: 3, x: 100, y: 0}\n')
        html = fences.render_figure(figure)
        self.assertTrue(html.startswith('<figure class="an-figure"><div class="shotwrap">'))
        self.assertIn('<a class="glightbox" href="../assets/window.png" data-type="image"><img '
                      'src="../assets/window.png" alt="The &quot;window&quot;." width="800" height="500" '
                      'loading="lazy"></a>', html)
        self.assertIn('<span class="pin" style="left:15%;top:20.5%"><span class="visually-hidden">Step </span>1</span>',
                      html)
        self.assertIn('<figcaption>What it shows.</figcaption></figure>', html)

    def test_a_pair_draws_two_labelled_pictures(self):
        html = fences.render_figure(self.parse(
            'images:\n  - {image: ../assets/before.png, alt: Dark., label: Before}\n'
            '  - {image: ../assets/after.png, alt: Bright., label: After}\ncaption: The same field.\n'))
        self.assertIn('<figure class="an-figure an-pair"><div class="pair">', html)
        self.assertEqual(html.count('<div class="shotwrap">'), 2)
        self.assertIn('<span class="pair-label">Before</span>', html)
        self.assertIn('width="320" height="240"', html)

    def test_a_missing_image_is_refused(self):
        error = self.assertRefused('image: ../assets/absent.png\nalt: A.\ncaption: C.\n',
                                   'image ../assets/absent.png does not exist')
        self.assertEqual(error.line, 1)

    def test_an_image_must_be_a_picture_inside_the_docs_folder_named_from_the_page(self):
        self.assertRefused('image: ../../outside.png\nalt: A.\ncaption: C.\n', 'lies outside the docs folder')
        self.assertRefused('image: /assets/window.png\nalt: A.\ncaption: C.\n', 'not a path relative to the page')
        self.assertRefused('image: https://example.org/a.png\nalt: A.\ncaption: C.\n', 'not a path relative')
        self.assertRefused('image: ../assets/notes.png\nalt: A.\ncaption: C.\n', 'is not a PNG, WebP or JPEG file')

    def test_alt_and_caption_are_required(self):
        self.assertRefused('image: ../assets/window.png\ncaption: C.\n', '`alt` is missing')
        self.assertRefused('image: ../assets/window.png\nalt: A.\n', '`caption` is missing')

    def test_pins_are_numbered_from_one_and_placed_in_percent(self):
        figure = 'image: ../assets/window.png\nalt: A.\ncaption: C.\npins:\n'
        self.assertRefused(figure + '  - {n: 0, x: 1, y: 1}\n', 'pins are numbered from 1')
        self.assertRefused(figure + '  - {n: 1, x: 101, y: 1}\n', 'pin 1 is placed at x 101')
        self.assertRefused(figure + '  - {n: 1, x: 1, y: 1}\n  - {n: 1, x: 2, y: 2}\n', 'pin 1 is placed twice')
        self.assertRefused(figure + '  - {n: 1, x: 1}\n', 'a pin states `n`, `x` and `y`')

    def test_a_pair_is_two_labelled_images_without_pins(self):
        entry = '  - {image: ../assets/before.png, alt: A., label: Before}\n'
        self.assertRefused('images:\n' + entry + 'caption: C.\n', '`images` is a pair')
        self.assertRefused('images:\n' + entry + '  - {image: ../assets/after.png, alt: B.}\ncaption: C.\n',
                           'has no `label`')
        self.assertRefused('images:\n' + entry + entry + 'caption: C.\npins: []\n', 'unknown key `pins`')


class _Capture(logging.Handler):
    def __init__(self):
        super().__init__(logging.WARNING)
        self.messages = []

    def emit(self, record):
        self.messages.append(record.getMessage())


class FormatterTests(unittest.TestCase):
    """The blocks as pymdownx.superfences runs them, on a page the hooks announced."""

    def setUp(self):
        self.handler = _Capture()
        logger = logging.getLogger('mkdocs.plugins.guide')
        logger.addHandler(self.handler)
        self.addCleanup(logger.removeHandler, self.handler)

    def render(self, body, first_line=1):
        folder = str(FIXTURE_IMAGES.parent / 'interface')
        fences.begin_page('interface/page.md', folder, str(FIXTURE_IMAGES.parent), body, first_line)
        return markdown.markdown(body, extensions=['pymdownx.superfences'], extension_configs={
            'pymdownx.superfences': {'custom_fences': fences.custom_fences()}})

    def test_valid_blocks_draw_without_a_warning(self):
        html = self.render('Text.\n\n```controls\n' + control() + '```\n\n```figure\nimage: ../assets/window.png\n'
                           'alt: A.\ncaption: C.\n```\n')
        self.assertIn('<section class="ctls"', html)
        self.assertIn('<figure class="an-figure">', html)
        self.assertEqual(self.handler.messages, [])

    def test_a_yaml_error_warns_with_the_page_and_the_line_and_leaves_an_error_box(self):
        body = 'Text.\n\n```controls\nwindow: Levels\ncontrols:\n  - name: [unclosed\n```\n'
        html = self.render(body, first_line=9)
        self.assertIn('<div class="fence-error" role="alert"><b>This controls block cannot be drawn.</b>', html)
        self.assertEqual(len(self.handler.messages), 1)
        # The fence opens on line 3 of a body that starts on line 9 of its file, line 11; the problem is on the block's
        # third line.
        self.assertIn('interface/page.md:14: controls block: the block is not valid YAML', self.handler.messages[0])

    def test_each_block_reports_its_own_line(self):
        good = '```controls\n' + control() + '```\n\n'
        bad = '```controls\n' + control(kind='dial') + '```\n'
        self.render(good + bad)
        self.assertEqual(len(self.handler.messages), 1)
        self.assertIn('interface/page.md:10: controls block: control "Channel": unknown kind `dial`',
                      self.handler.messages[0])

    def test_a_missing_image_warns_and_leaves_an_error_box(self):
        html = self.render('```figure\nimage: ../assets/absent.png\nalt: A.\ncaption: C.\n```\n')
        self.assertIn('This figure block cannot be drawn.', html)
        self.assertIn('interface/page.md:2: figure block: image ../assets/absent.png does not exist',
                      self.handler.messages[0])

    def test_a_block_inside_another_fence_is_not_a_block(self):
        html = self.render('````markdown\n```controls\nnot yaml: [\n```\n````\n')
        self.assertNotIn('fence-error', html)
        self.assertEqual(self.handler.messages, [])


if __name__ == '__main__':
    unittest.main()
