# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for draft_guide_controls.

Run with the documentation toolchain's Python, which has PyYAML:
    python build/scripts/tools/draft_guide_controls_test.py

The cases plant a repository in a temporary directory: the application sources that name the menus, an inventory of
three windows and a page for each shape an old parameter table has. The last cases draft the Stretching pages of the
guide itself.
"""

from __future__ import annotations

import io
import json
import os
import pathlib
import re
import shutil
import sys
import tempfile
import unittest

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import draft_guide_controls as draft  # noqa: E402

UI_BUILDER = '''\
        menuProcess->setTitle(QCoreApplication::translate("AfterNightAppClass", "&Process", nullptr));
        menuStretch->setTitle(QCoreApplication::translate("AfterNightAppClass", "Stretch", nullptr));
        menuTools->setTitle(QCoreApplication::translate("AfterNightAppClass", "Tools", nullptr));
        actionHT->setText(QCoreApplication::translate("AfterNightAppClass", "Histogram Transformation", nullptr));
        actionDenoise->setText(QCoreApplication::translate("AfterNightAppClass", "Deep Noise", nullptr));
        actionCompose->setText(QCoreApplication::translate("AfterNightAppClass", "Compose", nullptr));
        actionExtract->setText(QCoreApplication::translate("AfterNightAppClass", "Extract Red", nullptr));
        menuBar->addAction(menuProcess->menuAction());
        menuProcess->addAction(menuStretch->menuAction());
        menuProcess->addAction(menuTools->menuAction());
        menuStretch->addAction(actionHT);
        menuStretch->addAction(actionDenoise);
        menuTools->addAction(actionCompose);
        menuTools->addAction(actionExtract);
'''

REGISTRY = '''\
void registerBuiltins()
{
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionHT, histogramTransformation,
                               facts);
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionDenoise, deepNoise, facts);
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryColor, actionCompose, compose, facts);
    AN_REGISTER_CHANNEL(actionExtract, extractRed);
}
'''

MKDOCS = '''\
site_name: Test guide
docs_dir: docs/en
nav:
  - Histogram Transformation: processes/ht.md
'''


def control(label, kind, **stated):
    """One record of an inventory: `visible` and `default` are stated unless `varies` names them."""
    record = {'label': label, 'kind': kind, 'object': stated.pop('object', ''), 'group': stated.pop('group', '')}
    varies = stated.get('varies', [])
    if 'visible' not in varies:
        record['visible'] = stated.pop('visible', True)
    if 'default' not in varies:
        record['default'] = stated.pop('default', '')
    record.update(stated)
    return record


HT_WINDOW = {
    'id': 'tone_brightness.actionHT',
    'window': 'Histogram Transformation <{image}>',
    'controls': [
        control('Zoom in', 'toolbutton', object='zoomIn', shared='rt-preview'),
        control('Plot resolution', 'combo', object='plotResolution', default='8-bit (256)',
                options=['8-bit (256)', '16-bit (64K)'], shared='rt-preview'),
        control('Channel', 'combo', object='channelSelector', default='RGB', options=['RGB', 'Red', 'Green', 'Blue']),
        control('Shadows', 'spinbox', object='shadowsValue', default='0.000000', min=0, max=1, step=0.01, decimals=6,
                slider=True),
        control('Clip', 'check', object='clip', default='Off', visible=False),
        control('Target Image', 'combo', object='targetImageSelector', varies=['default', 'options']),
        control('Width', 'spinbox', object='targetWidth', min=1, step=1, suffix='px', varies=['default', 'max']),
        control('Preview', 'check', object='preview', default='On'),
        control('Apply', 'button', shared='process-window'),
    ],
}

AWKWARD_LABELS = ['Score \u2265', '\u03c3 low: clip', '- offset', '#tag', 'Say "hi"', 'Yes', 'Limit # of frames',
                  'Ends with colon:', "It's", '50%', '[bracket]', 'back\\slash']
AWKWARD_OPTIONS = ['a, b', '[x]', 'c: d', 'plain', '{y}', '50% #1', '<--NO IMAGE-->', 'CIE L* (Lightness)', '',
                   '\u03c3-clip', "it's", 'tab\there']

DENOISE_WINDOW = {
    'id': 'denoising.actionDenoise',
    'window': 'Deep Noise <{image}>',
    'controls': [
        control('Stride', 'combo', object='stridePreset', default='Default (480)',
                options=['Default (480)', 'Custom']),
        control('Stride', 'spinbox', object='customStride', default='480', min=2, max=512, step=2),
        control('outputMode', 'combo', object='outputMode', default='Replace', options=['Replace', 'New image']),
        control('Browse', 'button'),
        control('Browse', 'button'),
        control('Weights', 'field', object='weightsPath'),
        control('Fast / Exact', 'radio', object='fastMode', default='Fast', options=['Fast', 'Exact']),
        control('Awkward', 'combo', object='awkward', default='plain', options=AWKWARD_OPTIONS),
        control('Sensor', 'combo', object='sensor', options=[]),
        control('Mode', 'combo', object='mode', options=['First', 'Second'], varies=['default']),
    ] + [control(label, 'check', object='awkward%d' % index, default='Off')
         for index, label in enumerate(AWKWARD_LABELS)],
}

COMPOSE_WINDOW = {
    'id': 'color.actionCompose',
    'window': 'Compose',
    'controls': [
        control('Ha', 'combo', object='haSelector', group='Sources', default='<--NO IMAGE-->', varies=['options']),
        control('CPU Threads', 'spinbox', object='threads', group='Sources', min=1, step=1,
                varies=['default', 'max']),
    ] + [control('Weight %d' % index, 'spinbox', object='weight%d' % index,
                 group='Options > Advanced' if index > 15 else 'Options', default='1', min=0, max=9, step=1)
         for index in range(1, 31)] + [control('Close', 'button', shared='process-window')],
}

def table(headers, *rows):
    """A Markdown table of the headers and the rows given, each row its cells; a row may stop early."""
    lines = ['| %s |' % ' | '.join(headers), '|' + ' --- |' * len(headers)]
    lines += ['| %s |' % ' | '.join(row) for row in rows]
    return '\n'.join(lines) + '\n'


def process_page(keys, title, *parts):
    """A process page: the front matter keys given, its H1 and its parts."""
    return '---\nkind: process\n%ssummary: A process.\n---\n# %s\n\n%s' % (keys, title, '\n'.join(parts))


TEN_COLUMNS = ('UI label/action', 'Type and unit', 'Runtime default', 'Persisted-default exception',
               'Valid values, step, or constraints', 'Effect of increasing, decreasing, or selecting',
               'Dependencies and enable/visibility rules', 'Quality, artifact, runtime, memory, or disk trade-off',
               'Scope: preview/apply, channel, mask, still/sequence', 'Persistence')

HT_PAGE = process_page(
    'stage: stretch\nmenu: Process > Stretch > Histogram Transformation\n', 'Histogram Transformation',
    'The window.\n',
    '```text\n| UI label/action | Not a table |\n| --- | --- |\n| Shadows | inside a code block |\n```\n',
    '## Parameters\n',
    table(TEN_COLUMNS,
          ('Histogram plot', 'Graph', 'Source', 'None', 'Binning', 'Shows the distribution', 'Always', 'Work',
           'Display only', 'Recomputed'),
          ('**Zoom in**', 'Toolbar action', 'Fitted', 'None', 'Each `1.30x`', 'Enlarges the plot', 'Idle',
           'Rendering', 'Display', 'Window'),
          ('**Channel**', 'Choice', '**RGB** for RGB; **Grayscale** for mono', 'None', '`x|y` or a \\| b',
           'Chooses the channel: the one the sliders move', 'RGB target', 'Shifts color', 'Preview/Apply',
           'Reset returns RGB'),
          ('**Shadows**', 'Normalized spin box', '`0.000000`', '\u2014', '`0..1`, step `0.01`',
           'Higher clips more shadows', 'Idle', 'Erases faint signal', 'Preview/Apply', 'Reset after Apply'),
          ('Clip', 'Check box', 'Unchecked', '-', 'Checked/unchecked', 'Clips the result', 'Idle', 'None', 'Apply',
           'Window'),
          ('**Target Image**', 'Selector', 'Launching view', 'None', 'Open images', 'Changes the source',
           'An image is open', 'Wrong target is replaced', 'Source', 'Target lifecycle'),
          ('**Preview**', 'Boolean', 'Checked', 'None', 'Checked/unchecked', 'Shows the result', 'Idle',
           'Costs memory', 'Preview only', 'Window'),
          ('**Apply**', 'Action', 'Disabled at identity', 'None', 'One action', 'Replaces the target',
           'Target locks', 'One pass', 'Still/sequence', 'History step'),
          ('Short row', 'Only two cells')),
    '## Troubleshooting\n',
    table(('Symptom', 'Cause', 'Fix'), ('Shadows', 'A table that is not the parameter table', 'Left alone')))

# The columns in another order, under other headers, as one page of the guide has them.
DENOISE_PAGE = process_page(
    'stage: noise\nmenu: Process > Stretch > Deep Noise\n', 'Deep Noise',
    table(('UI label', 'Type and unit', 'Valid values', 'Runtime default', 'Persisted-default behavior',
           'Effect of increasing or enabling', 'Dependencies and scope: preview/apply, channel, mask, still/sequence',
           'Trade-off, failure boundary, and cost', 'Output and persistence'),
          ('Stride', 'Preset and pixels', 'Presets, or `2..512`', '**Default (480)**', 'Kept in settings',
           'Larger tiles run faster', 'Custom enables the spin box', 'Seams at small strides', 'Captured per run'),
          ('Weights', 'Path', 'A model file', 'Empty', 'Kept in settings', 'Chooses the model',
           'A file must exist', 'Wrong model fails', 'Not written'),
          ('Score \u2265', 'Check', 'On or off', 'Selected', 'None', 'Filters by score', 'Idle', 'None',
           'Window')))

# The seven-column table, and the table of actions whose columns no facet is named after.
COMPOSE_PAGE = process_page(
    'stage: color\nmenu:\n  - Process > Tools\n', 'Compose',
    table(('UI label/action', 'Type and unit', 'Runtime default', 'Valid values or constraints',
           'Effect and dependencies', 'Trade-off and scope', 'Persistence'),
          ('Ha', 'Image selector', '**<--NO IMAGE-->**', 'Open images of one size',
           'Chooses the hydrogen frame; needs an open image', 'A wrong frame tints the result; stills only',
           'Window'),
          ('CPU Threads', 'Whole number', 'All threads', 'One to the processors of the machine',
           'More threads run faster', 'Memory rises with threads', 'Kept in settings')),
    table(('UI label/action', 'Menu path and wiring', 'Enabled when', 'Output and source effect',
           'Precision and metadata consequence'),
          ('Weight 1', '**Process > Tools > Compose**', 'A frame is chosen', 'Scales the first frame',
           'Keeps the sample type'),
          ('Extract Red', '**Process > Tools > Extract Red**', 'An RGB image is active', 'A new mono image',
           'Keeps the header')))

REWRITTEN_PAGE = '''\
---
kind: process
stage: stretch
menu: Process > Stretch > Histogram Transformation
summary: Stretches the image.
rewritten: true
---
# Histogram Transformation

The window.
'''

EXTRACT_PAGE = '''\
---
kind: process
stage: any
menu: Process > Tools > Extract Red
summary: Extracts a channel.
---
# Extract Red
'''

ORPHAN_PAGE = EXTRACT_PAGE.replace('Process > Tools > Extract Red', 'Process > Nowhere')
GLOSSARY_PAGE = '---\nkind: reference\nsummary: The terms.\n---\n# Glossary\n'

KINDS = ('spinbox', 'slider', 'combo', 'check', 'radio', 'toolbutton', 'button', 'field', 'action')
BLOCK = re.compile(r'(?ms)^```controls\n(.*?)^```$')


def blocks(output):
    """The `controls` blocks of a draft, each as the text between its fences."""
    return [match.group(1) for match in BLOCK.finditer(output)]


def load(block):
    return yaml.load(block, yaml.BaseLoader)


class DraftCase(unittest.TestCase):
    """A repository with three windows in its inventory and a page for each shape of the old table."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.write('src/AfterNightControls/app/AfterNightAppUiBuilder.h', UI_BUILDER)
        self.write('src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp', REGISTRY)
        self.write('docs/user-guide/mkdocs.yml', MKDOCS)
        for window in (HT_WINDOW, DENOISE_WINDOW, COMPOSE_WINDOW):
            self.write('docs/user-guide/data/ui/%s.json' % window['id'], json.dumps(window, ensure_ascii=False))
        self.page('processes/ht.md', HT_PAGE)
        self.page('processes/denoise.md', DENOISE_PAGE)
        self.page('processes/compose.md', COMPOSE_PAGE)
        self.page('processes/rewritten.md', REWRITTEN_PAGE)
        self.page('processes/extract.md', EXTRACT_PAGE)
        self.page('processes/orphan.md', ORPHAN_PAGE)
        self.page('reference/glossary.md', GLOSSARY_PAGE)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='')
        return path

    def page(self, relative, text):
        return self.write('docs/user-guide/docs/en/' + relative, text)

    def run_main(self, *arguments):
        stream = io.StringIO()
        code = draft.main(['--repo', str(self.root)] + list(arguments), stream)
        return code, stream.getvalue()

    def drafted(self, page, *arguments):
        """The draft of a page: its output, and the controls of its first block by name."""
        code, output = self.run_main(page, *arguments)
        self.assertEqual(code, 0, output)
        found = blocks(output)
        self.assertTrue(found, output)
        return output, load(found[0])

    def by_name(self, block):
        controls = {}
        for entry in block['controls']:
            controls.setdefault(entry['name'], entry)
        return controls

    def assertShape(self, block, unnamed_duplicates=()):
        """The block is one the fence takes: the keys every control needs, a choice among its options, bounds or a
        range, and no name twice without the object that tells the two apart."""
        self.assertTrue(block['window'])
        names = [entry['name'] for entry in block['controls']]
        for entry in block['controls']:
            for key in ('name', 'kind', 'default', 'effect'):
                self.assertIsInstance(entry.get(key), str, (entry.get('name'), key))
            self.assertTrue(entry['name'] and entry['effect'], entry)
            self.assertIn(entry['kind'], KINDS)
            for key in entry:
                self.assertIn(key, ('name', 'kind', 'default', 'effect', 'widget', 'options', 'object') + draft.FACETS)
            if entry['kind'] in ('combo', 'radio'):
                self.assertTrue(entry.get('options'), entry['name'])
                self.assertIn(entry['default'], entry['options'], entry['name'])
            else:
                self.assertNotIn('options', entry)
            if entry['kind'] in ('spinbox', 'slider'):
                widget = entry.get('widget', {})
                self.assertTrue(('min' in widget and 'max' in widget) or entry.get('range'), entry['name'])
                for number in widget.values():
                    float(number)
            if names.count(entry['name']) > 1 and entry['name'] not in unnamed_duplicates:
                self.assertTrue(entry.get('object'), entry['name'])


class TenColumnTests(DraftCase):
    def test_the_draft_lists_the_windows_own_controls_in_window_order(self):
        output, block = self.drafted('processes/ht.md')
        self.assertEqual(block['window'], 'Histogram Transformation')
        self.assertEqual([entry['name'] for entry in block['controls']],
                         ['Channel', 'Shadows', 'Clip', 'Target Image', 'Width', 'Preview'])
        self.assertShape(block)
        self.assertIn('# Controls draft of processes/ht.md', output)
        self.assertIn('`tone_brightness.actionHT`: 6 controls drafted, 3 shared with every window left out.', output)

    def test_what_the_inventory_fixes_is_stated_as_the_window_has_it(self):
        _, block = self.drafted('processes/ht.md')
        controls = self.by_name(block)
        self.assertEqual(controls['Shadows']['kind'], 'spinbox')
        self.assertEqual(controls['Shadows']['default'], '0.000000')
        self.assertEqual(controls['Shadows']['widget'], {'min': '0', 'max': '1', 'step': '0.01', 'decimals': '6'})
        self.assertEqual(controls['Channel']['options'], ['RGB', 'Red', 'Green', 'Blue'])
        self.assertEqual(controls['Preview']['default'], 'On')
        self.assertNotIn('object', controls['Shadows'])

    def test_every_default_is_double_quoted(self):
        output, _ = self.drafted('processes/ht.md')
        defaults = [line.strip() for line in output.splitlines() if line.strip().startswith('default:')]
        self.assertEqual(len(defaults), 6)
        for line in defaults:
            self.assertRegex(line, r'^default: ".*"$')

    def test_each_column_lands_in_its_facet(self):
        _, block = self.drafted('processes/ht.md')
        shadows = self.by_name(block)['Shadows']
        self.assertEqual(shadows['range'], '`0..1`, step `0.01`')
        self.assertEqual(shadows['effect'], 'Higher clips more shadows')
        self.assertEqual(shadows['enabled'], 'Idle')
        self.assertEqual(shadows['tradeoff'], 'Erases faint signal')
        self.assertEqual(shadows['scope'], 'Preview/Apply')
        self.assertEqual(shadows['note'], 'Type and unit: Normalized spin box.')

    def test_a_dash_states_nothing_and_none_is_kept(self):
        _, block = self.drafted('processes/ht.md')
        controls = self.by_name(block)
        # The dash under "Persisted-default exception" is dropped, so one column is left and needs no header.
        self.assertEqual(controls['Shadows']['persistence'], 'Reset after Apply')
        self.assertEqual(controls['Clip']['persistence'], 'Window')
        self.assertEqual(controls['Preview']['persistence'],
                         'Persisted-default exception: None. Persistence: Window.')

    def test_a_default_the_inventory_states_is_not_repeated_and_another_is_kept_as_a_note(self):
        _, block = self.drafted('processes/ht.md')
        controls = self.by_name(block)
        self.assertNotIn('Old default', controls['Shadows']['note'])   # `0.000000`
        self.assertNotIn('Old default', controls['Clip']['note'])      # Unchecked is Off
        self.assertNotIn('Old default', controls['Preview']['note'])   # Checked is On
        self.assertEqual(controls['Channel']['note'],
                         'Type and unit: Choice. Old default: **RGB** for RGB; **Grayscale** for mono.')
        self.assertIn('Old default: Launching view.', controls['Target Image']['note'])

    def test_a_pipe_in_a_code_span_or_behind_a_backslash_stays_in_its_cell(self):
        _, block = self.drafted('processes/ht.md')
        channel = self.by_name(block)['Channel']
        self.assertEqual(channel['range'], '`x|y` or a | b')
        self.assertEqual(channel['effect'], 'Chooses the channel: the one the sliders move')

    def test_a_control_without_a_row_says_what_is_missing(self):
        _, block = self.drafted('processes/ht.md')
        width = self.by_name(block)['Width']
        self.assertEqual(width['effect'], draft.EFFECT_TODO)

    def test_what_follows_the_image_is_a_placeholder_with_a_comment(self):
        output, block = self.drafted('processes/ht.md')
        controls = self.by_name(block)
        self.assertEqual(controls['Target Image']['default'], '(follows the image)')
        self.assertEqual(controls['Target Image']['options'], ['(follows the image)'])
        self.assertEqual(controls['Width']['default'], '(follows the image)')
        self.assertEqual(controls['Width']['widget'], {'min': '1', 'step': '1'})
        self.assertEqual(controls['Width']['range'],
                         'TODO: the largest value follows the image; say what bounds it.')
        self.assertIn('  # default and options follow the image: replace the placeholder', output)
        self.assertIn('  # default and max follow the image: replace the placeholder', output)

    def test_a_hidden_control_and_a_unit_are_commented(self):
        output, _ = self.drafted('processes/ht.md')
        lines = output.splitlines()
        self.assertEqual(lines[lines.index('  - name: Clip') - 1], '  # hidden when the window opens')
        self.assertIn('  # unit: px', lines[lines.index('  - name: Width') - 2:lines.index('  - name: Width')])

    def test_shared_controls_are_left_out_and_their_rows_listed(self):
        output, block = self.drafted('processes/ht.md')
        self.assertNotIn('Zoom in', self.by_name(block))
        shared = output[output.index('## Old rows of controls every window shares'):
                        output.index('## Old rows that name no control of the window')]
        self.assertIn('they belong to the Process windows page', shared)
        self.assertIn('- **Zoom in**\n    - Type and unit: Toolbar action\n    - Runtime default: Fitted\n', shared)
        self.assertIn('- **Apply**\n', shared)
        self.assertNotIn('Histogram plot', shared)

    def test_shared_controls_are_kept_when_asked_for(self):
        output, block = self.drafted('processes/ht.md', '--shared')
        names = [entry['name'] for entry in block['controls']]
        self.assertEqual(names[:3], ['Zoom in', 'Plot resolution', 'Channel'])
        self.assertEqual(names[-1], 'Apply')
        self.assertEqual(self.by_name(block)['Zoom in']['effect'], 'Enlarges the plot')
        self.assertEqual(self.by_name(block)['Apply']['default'], '')
        self.assertNotIn('## Old rows of controls every window shares', output)
        self.assertIn('`tone_brightness.actionHT`: 9 controls drafted.', output)
        self.assertShape(block)

    def test_rows_that_name_no_control_are_listed_with_every_cell(self):
        output, _ = self.drafted('processes/ht.md')
        unmatched = output[output.index('## Old rows that name no control of the window'):]
        self.assertIn('- Histogram plot\n    - Type and unit: Graph\n    - Runtime default: Source\n'
                      '    - Persisted-default exception: None\n', unmatched)
        self.assertIn('    - Persistence: Recomputed\n', unmatched)
        # A row that stops early states what it has.
        self.assertIn('- Short row\n    - Type and unit: Only two cells\n', unmatched)

    def test_every_row_is_placed_once(self):
        result = draft.draft_page(str(self.root), str(self.root / 'docs/user-guide'), 'processes/ht.md')
        self.assertEqual(result.rows, 9)
        self.assertEqual(result.matched, 5)
        self.assertEqual([row.name for row in result.shared_rows], ['**Zoom in**', '**Apply**'])
        self.assertEqual([row.name for row in result.unmatched_rows], ['Histogram plot', 'Short row'])
        self.assertIn('Of the 9 rows of the old parameter table, 5 are on a control, 2 on a control every window '
                      'shares and 2 on no control of the window.', result.text)

    def test_a_table_in_a_code_block_and_a_table_of_another_kind_are_no_rows(self):
        result = draft.draft_page(str(self.root), str(self.root / 'docs/user-guide'), 'processes/ht.md')
        said = ' '.join(cell for row in result.unmatched_rows + result.shared_rows for _, _, cell in row.cells)
        self.assertNotIn('inside a code block', said)
        self.assertNotIn('Left alone', result.text)


class OtherTableTests(DraftCase):
    def test_columns_are_mapped_by_header_in_any_order(self):
        _, block = self.drafted('processes/denoise.md')
        stride = block['controls'][0]
        self.assertEqual((stride['name'], stride['kind']), ('Stride', 'combo'))
        self.assertEqual(stride['range'], 'Presets, or `2..512`')
        self.assertEqual(stride['effect'], 'Larger tiles run faster')
        self.assertEqual(stride['enabled'], 'Custom enables the spin box')
        self.assertEqual(stride['tradeoff'], 'Seams at small strides')
        self.assertEqual(stride['persistence'],
                         'Persisted-default behavior: Kept in settings. Output and persistence: Captured per run.')
        # **Default (480)** is what the inventory records.
        self.assertEqual(stride['note'], 'Type and unit: Preset and pixels.')
        self.assertNotIn('scope', stride)

    def test_a_row_goes_to_the_first_control_of_its_label(self):
        _, block = self.drafted('processes/denoise.md')
        second = block['controls'][1]
        self.assertEqual((second['name'], second['kind']), ('Stride', 'spinbox'))
        self.assertEqual(second['effect'], draft.EFFECT_TODO)

    def test_a_repeated_label_and_an_object_name_for_a_label_get_object(self):
        output, block = self.drafted('processes/denoise.md')
        controls = block['controls']
        self.assertEqual(controls[0]['object'], 'stridePreset')
        self.assertEqual(controls[1]['object'], 'customStride')
        self.assertEqual(controls[2]['name'], 'outputMode')
        self.assertEqual(controls[2]['object'], 'outputMode')
        self.assertNotIn('object', self.by_name(block)['Weights'])
        self.assertNotIn('object', self.by_name(block)['Fast / Exact'])
        # Two buttons of one label and no object name cannot be told apart, which the draft says.
        browse = [entry for entry in controls if entry['name'] == 'Browse']
        self.assertEqual(len(browse), 2)
        self.assertTrue(all('object' not in entry for entry in browse))
        self.assertEqual(output.count('  # the window has several controls of this name, and this one has no object '
                                      'name to tell it by'), 2)
        self.assertShape(block, unnamed_duplicates=('Browse',))

    def test_a_list_with_no_choice_and_a_default_the_image_decides_are_stood_in_for(self):
        output, block = self.drafted('processes/denoise.md')
        controls = self.by_name(block)
        self.assertEqual(controls['Sensor']['default'], draft.NO_CHOICE)
        self.assertEqual(controls['Sensor']['options'], [draft.NO_CHOICE])
        self.assertIn('  # the window opens with no choice in this list: write what the list holds', output)
        self.assertEqual(controls['Mode']['default'], 'First')
        self.assertEqual(controls['Mode']['options'], ['First', 'Second'])
        self.assertIn('  # the default follows the image: the first choice stands in for it', output)

    def test_awkward_names_and_choices_read_back_as_they_are(self):
        _, block = self.drafted('processes/denoise.md')
        names = [entry['name'] for entry in block['controls']]
        self.assertEqual(names[-len(AWKWARD_LABELS):], AWKWARD_LABELS)
        self.assertEqual(self.by_name(block)['Awkward']['options'], AWKWARD_OPTIONS)
        # A row is matched whatever the punctuation of its name, and its default of a check box is read as the
        # inventory's.
        score = self.by_name(block)['Score \u2265']
        self.assertEqual(score['effect'], 'Filters by score')
        self.assertEqual(score['note'], 'Type and unit: Check. Old default: Selected.')

    def test_the_seven_column_table_keeps_its_merged_columns(self):
        output, block = self.drafted('processes/compose.md')
        ha = block['controls'][0]
        self.assertEqual(ha['range'], 'Open images of one size')
        self.assertEqual(ha['effect'], 'Chooses the hydrogen frame; needs an open image')
        self.assertEqual(ha['tradeoff'], 'A wrong frame tints the result; stills only')
        self.assertEqual(ha['persistence'], 'Window')
        self.assertEqual(ha['note'], 'Type and unit: Image selector.')
        self.assertNotIn('enabled', ha)
        self.assertNotIn('scope', ha)

    def test_a_column_no_facet_is_named_after_is_a_note_under_its_header(self):
        _, block = self.drafted('processes/compose.md')
        weight = self.by_name(block)['Weight 1']
        self.assertEqual(weight['enabled'], 'A frame is chosen')
        self.assertEqual(weight['effect'], 'Scales the first frame')
        self.assertEqual(weight['note'], 'Menu path and wiring: **Process > Tools > Compose**. Precision and metadata '
                                         'consequence: Keeps the sample type.')

    def test_a_window_opened_on_no_image_follows_the_machine_and_the_open_images(self):
        output, block = self.drafted('processes/compose.md')
        controls = self.by_name(block)
        self.assertEqual(controls['Ha']['default'], '<--NO IMAGE-->')
        self.assertEqual(controls['Ha']['options'], ['<--NO IMAGE-->', '(follows the open images)'])
        self.assertEqual(controls['CPU Threads']['default'], '(follows the machine)')
        self.assertEqual(controls['CPU Threads']['widget'], {'min': '1', 'step': '1'})
        self.assertTrue(controls['CPU Threads']['range'].startswith(
            'TODO: the largest value follows the machine; say what bounds it. One to the processors'))
        self.assertIn('  # options follows the open images: replace the placeholder', output)
        self.assertShape(block)

    def test_a_window_of_many_controls_names_each_group_where_it_starts(self):
        output, block = self.drafted('processes/compose.md')
        self.assertEqual(block['window'], 'Compose')
        self.assertEqual(len(block['controls']), 32)
        lines = output.splitlines()
        # A control's own comments sit between its group's and its name.
        self.assertEqual(lines[lines.index('  - name: Ha') - 2], '  # --- Sources ---')
        self.assertEqual(lines[lines.index('  - name: Weight 1') - 1], '  # --- Options ---')
        self.assertEqual(lines[lines.index('  - name: Weight 16') - 1], '  # --- Options > Advanced ---')
        self.assertEqual(sum(1 for line in lines if line.startswith('  # --- ')), 3)

    def test_a_small_window_names_no_group(self):
        output, _ = self.drafted('processes/ht.md')
        self.assertNotIn('  # --- ', output)

    def test_a_page_of_several_windows_gets_a_block_for_each(self):
        self.page('processes/stretch.md', HT_PAGE.replace('menu: Process > Stretch > Histogram Transformation',
                                                          'menu: Process > Stretch'))
        for named in ('ht.md', 'denoise.md', 'rewritten.md'):
            (self.root / 'docs/user-guide/docs/en/processes' / named).unlink()
        code, output = self.run_main('processes/stretch.md')
        self.assertEqual(code, 0, output)
        found = [load(block) for block in blocks(output)]
        self.assertEqual([block['window'] for block in found], ['Histogram Transformation', 'Deep Noise'])
        self.assertIn('drafted from the inventory of its 2 windows', output)

    def test_a_rewritten_page_is_drafted_from_the_inventory_alone(self):
        output, block = self.drafted('processes/rewritten.md')
        self.assertEqual(len(block['controls']), 6)
        self.assertTrue(all(entry['effect'] == draft.EFFECT_TODO for entry in block['controls']))
        self.assertIn('Of the 0 rows of the old parameter table', output)
        self.assertNotIn('## Old rows', output)
        self.assertShape(block)


class RefusalTests(DraftCase):
    def refused(self, page, *fragments):
        code, output = self.run_main(page)
        self.assertEqual(code, 2, output)
        for fragment in fragments:
            self.assertIn(fragment, output)
        self.assertNotIn('```controls', output)

    def test_an_unknown_page_is_refused(self):
        self.refused('processes/missing.md', 'processes/missing.md is not a page of')

    def test_a_page_of_another_kind_is_refused(self):
        self.refused('reference/glossary.md', 'reference/glossary.md is not a process page')

    def test_a_page_whose_action_opens_no_window_is_refused(self):
        self.refused('processes/extract.md', 'opens no window the inventory holds for it',
                     'covers actionExtract, which open no process window')

    def test_a_page_of_a_submenu_is_not_given_the_windows_other_pages_name(self):
        self.page('processes/stretch.md', HT_PAGE.replace('menu: Process > Stretch > Histogram Transformation',
                                                          'menu: Process > Stretch'))
        self.refused('processes/stretch.md', 'its `menu` (Process > Stretch) reaches actionHT, actionDenoise, and '
                     'another page names each of those windows more exactly')
        (self.root / 'docs/user-guide/docs/en/processes/denoise.md').unlink()
        code, output = self.run_main('processes/stretch.md')
        self.assertEqual(code, 0, output)
        self.assertEqual([load(block)['window'] for block in blocks(output)], ['Deep Noise'])

    def test_a_page_whose_menu_covers_no_action_is_refused(self):
        self.refused('processes/orphan.md', 'its `menu` (Process > Nowhere) covers no registered action')

    def test_a_broken_inventory_is_reported(self):
        self.write('docs/user-guide/data/ui/tone_brightness.actionHT.json', '{"id": ')
        self.refused('processes/ht.md', 'the guide cannot be read', 'tone_brightness.actionHT.json')

    def test_a_page_is_named_relative_to_the_docs_folder_or_by_its_path(self):
        by_path = str(self.root / 'docs/user-guide/docs/en/processes/ht.md')
        self.assertEqual(self.run_main('processes/ht.md'), self.run_main(by_path))


class ScalarTests(unittest.TestCase):
    TEXTS = AWKWARD_LABELS + AWKWARD_OPTIONS + [
        'plain words', 'Histogram Transformation', '0.000000', '1e-06', 'On', 'null', '~', 'true', 'a: b', 'a:b',
        'trailing space ', ' leading space', 'ends:', 'has # hash', 'has#hash', "'single'", '"double"', '`code`',
        '**bold** text', '* star', '& and', '! bang', '| pipe', '> more', '% percent', '@ at', '? what', ': colon',
        ', comma', 'a, b', '{brace}', 'line\nbreak', 'tab\tinside', '', ' ', '\u2014', 'caf\u00e9', '\u03c3',
        'next\u0085line', 'separator\u2028line', '(follows the image)', '<--NO IMAGE-->', '8-bit (256)',
        'Stretch factor(In(D+1))', 'C:\\path\\file.fit', '---', '...', '- ', '#',
    ]

    def test_a_scalar_reads_back_as_its_text_in_a_block(self):
        for text in self.TEXTS:
            with self.subTest(text):
                written = 'key: %s\nnext: end\n' % draft.scalar(text)
                self.assertEqual(yaml.load(written, yaml.BaseLoader), {'key': text, 'next': 'end'})

    def test_a_scalar_reads_back_as_its_text_in_a_list(self):
        for text in self.TEXTS:
            with self.subTest(text):
                written = 'key: [%s, %s, end]\n' % (draft.scalar(text, flow=True), draft.scalar(text, flow=True))
                self.assertEqual(yaml.load(written, yaml.BaseLoader), {'key': [text, text, 'end']})

    def test_a_safe_text_is_written_plain_and_a_default_always_quoted(self):
        self.assertEqual(draft.scalar('Histogram Transformation'), 'Histogram Transformation')
        self.assertEqual(draft.scalar('8-bit (256)', flow=True), '8-bit (256)')
        self.assertEqual(draft.scalar('RGB', quoted=True), '"RGB"')
        self.assertEqual(draft.scalar('a: b'), '"a: b"')
        self.assertEqual(draft.scalar('a, b', flow=True), '"a, b"')
        self.assertEqual(draft.scalar('a, b'), 'a, b')


class ColumnTests(unittest.TestCase):
    def test_every_header_of_the_guides_tables_has_its_facet(self):
        for header, facet in (
                ('UI label/action', 'name'), ('UI label', 'name'), ('Type and unit', 'note'),
                ('Runtime default', 'default'), ('Persisted-default exception', 'persistence'),
                ('Persisted-default behavior', 'persistence'), ('Persistence', 'persistence'),
                ('Output and persistence', 'persistence'), ('Valid values, step, or constraints', 'range'),
                ('Valid values or constraints', 'range'), ('Valid values', 'range'),
                ('Effect of increasing, decreasing, or selecting', 'effect'), ('Effect and dependencies', 'effect'),
                ('Effect of increasing or enabling', 'effect'), ('Output and source effect', 'effect'),
                ('Dependencies and enable/visibility rules', 'enabled'),
                ('Dependencies and scope: preview/apply, channel, mask, still/sequence', 'enabled'),
                ('Enabled when', 'enabled'), ('Quality, artifact, runtime, memory, or disk trade-off', 'tradeoff'),
                ('Trade-off and scope', 'tradeoff'), ('Trade-off, failure boundary, and cost', 'tradeoff'),
                ('Scope: preview/apply, channel, mask, still/sequence', 'scope'),
                ('Scope: preview/execution, channel, mask, still/sequence', 'scope'),
                ('Menu path and wiring', 'note'), ('Precision and metadata consequence', 'note')):
            with self.subTest(header):
                self.assertEqual(draft.facet_of(header), facet)

    def test_a_row_splits_at_the_pipes_that_part_its_cells(self):
        self.assertEqual(draft.split_row('| a | `b|c` | d \\| e |  |'), ['a', '`b|c`', 'd | e', ''])
        self.assertEqual(draft.split_row('a | b'), ['a', 'b'])


class RepositoryTests(unittest.TestCase):
    """The Stretching pages of the guide itself, drafted from the inventory the application's tests keep."""

    GUIDE = os.path.join(draft.REPO_ROOT, 'docs', 'user-guide')
    PAGES = ('arcsinh-stretch', 'curves-transformation', 'exposure-correction', 'gamma-correction',
             'gh-transformation', 'histogram-transformation', 'statistical-stretch')

    def setUp(self):
        if not os.path.isdir(os.path.join(self.GUIDE, 'data', 'ui')):
            self.skipTest('the guide has no inventory of the windows\' controls')

    def test_each_stretching_page_drafts_every_control_of_its_window_and_places_every_row(self):
        for name in self.PAGES:
            with self.subTest(name):
                page = 'processes/stretching/%s.md' % name
                result = draft.draft_page(draft.REPO_ROOT, self.GUIDE, page)
                self.assertEqual(len(result.windows), 1)
                window, entries = result.windows[0]
                own = [control for control in window.controls if not control.shared]
                self.assertTrue(own)
                self.assertEqual([entry.control for entry in entries], own)
                found = blocks(result.text)
                self.assertEqual(len(found), 1)
                block = load(found[0])
                self.assertEqual(block['window'], window.name)
                self.assertEqual([entry['name'] for entry in block['controls']], [control.label for control in own])
                for entry, control in zip(block['controls'], own):
                    self.assertEqual(entry['kind'], control.kind)
                    if control.default is not None:
                        self.assertEqual(entry['default'], control.default)
                    if control.options is not None and control.options:
                        self.assertEqual(entry['options'], list(control.options))
                    for key, number in control.numbers.items():
                        self.assertEqual(float(entry['widget'][key]), float(number))
                    labelled_twice = sum(1 for other in own if other.label == control.label) > 1
                    self.assertEqual('object' in entry,
                                     bool(control.object) and (labelled_twice or control.label == control.object))
                    if 'object' in entry:
                        self.assertEqual(entry['object'], control.object)
                # Every row of the old table is on a control or listed; a page already rewritten has none.
                self.assertEqual(result.matched, sum(len(entry.rows) for entry in entries))
                self.assertEqual(result.rows, result.matched + len(result.shared_rows) + len(result.unmatched_rows))
                for row in result.shared_rows + result.unmatched_rows:
                    self.assertIn('- %s\n' % row.name.strip(), result.text)

    def test_the_blocks_of_the_stretching_pages_are_blocks_the_guide_builds(self):
        sys.path.insert(0, os.path.join(self.GUIDE, 'hooks'))
        try:
            import guide_fences
        except ImportError:
            self.skipTest('the documentation toolchain is not installed')
        for name in self.PAGES:
            with self.subTest(name):
                result = draft.draft_page(draft.REPO_ROOT, self.GUIDE, 'processes/stretching/%s.md' % name)
                for block in blocks(result.text):
                    parsed = guide_fences.parse_controls(block)
                    self.assertTrue(guide_fences.render_controls(parsed))

    def test_the_tool_prints_a_draft_of_a_page_of_the_guide(self):
        stream = io.StringIO()
        code = draft.main(['processes/stretching/gamma-correction.md'], stream)
        self.assertEqual(code, 0, stream.getvalue())
        block = load(blocks(stream.getvalue())[0])
        self.assertEqual(block['window'], 'Gamma Correction')
        self.assertIn('Gamma', [entry['name'] for entry in block['controls']])


if __name__ == '__main__':
    unittest.main()
