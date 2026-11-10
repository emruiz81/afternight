# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_model.

Run with the documentation toolchain's Python, which has PyYAML:
    docs/user-guide/.venv/Scripts/python docs/user-guide/hooks/guide_model_test.py
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import os
import pathlib
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import guide_model as model  # noqa: E402

GUIDE_DIR = pathlib.Path(__file__).resolve().parents[1]
STAGE_IDS = ['calibrate', 'register', 'stack', 'color', 'noise', 'stretch', 'finish', 'any']

PROCESS_PAGE = """---
kind: process
stage: stretch
menu: Process > Intensity Transformations > Histogram Transformation
summary: >-
  Maps the image through a shadows, midtones and highlights transfer.
data_in: linear
data_out: nonlinear
destructive: true
preview: real-time
sequences: true
masks: post-blend
external: none
glance:
  enabled: An image view is active.
  inputs: One image or sequence view.
derived: false
rewritten: true
---
# Histogram Transformation

Opening.
"""


class TempDir(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)

    def write(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, bytes):
            path.write_bytes(data)
        else:
            path.write_text(data, encoding='utf-8', newline='')
        return path


class FrontMatterTests(TempDir):
    def test_a_page_without_front_matter_has_none(self):
        front = model.split_front_matter('# Title\n\nText.\n')
        self.assertFalse(front.present)
        self.assertEqual(front.body, '# Title\n\nText.\n')
        self.assertEqual(front.body_line, 1)

    def test_the_front_matter_is_read_with_its_key_lines_and_the_body_after_it(self):
        front = model.split_front_matter(PROCESS_PAGE)
        self.assertTrue(front.present)
        self.assertIsNone(front.error)
        self.assertEqual(front.meta['stage'], 'stretch')
        self.assertIs(front.meta['destructive'], True)
        self.assertEqual(front.meta['summary'], 'Maps the image through a shadows, midtones and highlights transfer.')
        self.assertEqual(front.key_lines['kind'], 2)
        self.assertEqual(front.key_lines['menu'], 4)
        self.assertNotIn('enabled', front.key_lines)
        self.assertTrue(front.body.startswith('# Histogram Transformation'))
        self.assertEqual(front.body_line, 20)

    def test_a_dotted_end_line_closes_the_front_matter_as_mkdocs_allows(self):
        front = model.split_front_matter('---\nkind: reference\n...\n# Title\n')
        self.assertEqual(front.meta, {'kind': 'reference'})
        self.assertEqual(front.body, '# Title\n')

    def test_invalid_yaml_is_an_error_with_its_line(self):
        front = model.split_front_matter('---\nkind: process\nsummary: [unclosed\n---\n# Title\n')
        self.assertTrue(front.present)
        self.assertIsNone(front.meta)
        self.assertIn('not valid YAML', front.error)
        self.assertIn('line', front.error)

    def test_front_matter_that_is_not_a_mapping_is_an_error(self):
        front = model.split_front_matter('---\n- a\n- b\n---\n# Title\n')
        self.assertIn('not a mapping', front.error)

    def test_a_crlf_page_reads_as_mkdocs_reads_it(self):
        path = self.write('page.md', '\ufeff---\r\nkind: reference\r\nsummary: A page.\r\n---\r\n# Title\r\n')
        front = model.split_front_matter(model.read_text(path))
        self.assertEqual(front.meta, {'kind': 'reference', 'summary': 'A page.'})
        self.assertEqual(front.body, '# Title\n')


class SchemaTests(unittest.TestCase):
    def problems(self, **meta):
        return model.validate_meta(meta, STAGE_IDS)

    def messages(self, **meta):
        return [message for _, message in self.problems(**meta)]

    def test_a_complete_process_page_is_valid(self):
        self.assertEqual(model.validate_meta(model.split_front_matter(PROCESS_PAGE).meta, STAGE_IDS), [])

    def test_the_least_each_kind_needs_is_valid(self):
        self.assertEqual(self.problems(kind='home'), [])
        self.assertEqual(self.problems(kind='catalog', summary='Every process.'), [])
        self.assertEqual(self.problems(kind='process', summary='Crops.', stage='any', menu='Process > Geometry > Crop'),
                         [])
        self.assertEqual(self.problems(kind='interface', summary='The viewer.'), [])
        self.assertEqual(self.problems(kind='tutorial', summary='A first stack.', stages=['calibrate', 'stack'],
                                       needs=[]), [])
        self.assertEqual(self.problems(kind='workflow', summary='Broadband.', stages=STAGE_IDS[:7]), [])
        self.assertEqual(self.problems(kind='reference', summary='Formats.'), [])

    def test_an_unknown_kind_is_refused_and_nothing_else_is_judged(self):
        self.assertEqual(self.problems(kind='component', bogus=1), [('kind', "`kind` is 'component'; it must be one "
                                                                             "of home, catalog, process, interface, "
                                                                             "tutorial, workflow, reference")])

    def test_an_unknown_key_is_refused(self):
        self.assertIn((('title'), 'unknown key `title`'), self.problems(kind='reference', summary='S.', title='T'))

    def test_a_key_of_another_kind_is_refused(self):
        self.assertIn(('stage', '`stage` does not apply to a reference page'),
                      self.problems(kind='reference', summary='S.', stage='any'))

    def test_a_missing_required_key_is_named(self):
        self.assertIn('a process page needs `menu`', self.messages(kind='process', summary='S.', stage='any'))
        self.assertIn('a workflow page needs `stages`', self.messages(kind='workflow', summary='S.'))
        self.assertEqual(self.messages(kind='tutorial', summary='Installing.'), [])
        self.assertIn('a reference page needs `summary`', self.messages(kind='reference'))

    def test_a_stage_must_exist(self):
        messages = self.messages(kind='process', summary='S.', stage='polish', menu='Process > X')
        self.assertTrue(any(message.startswith("`stage` is 'polish'") for message in messages), messages)

    def test_stages_must_be_distinct_known_and_in_pipeline_order(self):
        for stages in (['stack', 'calibrate'], ['stack', 'stack'], [], ['polish'], 'stack'):
            with self.subTest(stages=stages):
                self.assertEqual([key for key, _ in self.problems(kind='workflow', summary='S.', stages=stages)],
                                 ['stages'])

    def test_a_menu_is_a_path_or_a_list_of_paths(self):
        valid = dict(kind='process', summary='S.', stage='any')
        self.assertEqual(self.problems(menu=['Process > A > B', 'Image > C'], **valid), [])
        for menu in ('Process>A', 'Process >  A', ' > A', [], ['Process > A', 'Process > A'], 3):
            with self.subTest(menu=menu):
                self.assertEqual([key for key, _ in self.problems(menu=menu, **valid)], ['menu'])

    def test_enumerations_and_booleans_take_only_their_values(self):
        valid = dict(kind='process', summary='S.', stage='any', menu='Process > A')
        for key, value in (('data_in', 'raw'), ('data_out', True), ('preview', 'live'), ('masks', 'yes'),
                           ('destructive', 'yes'), ('sequences', 1), ('derived', 'true'), ('external', '')):
            with self.subTest(key=key):
                self.assertEqual([found for found, _ in self.problems(**{key: value}, **valid)], [key])

    def test_glance_holds_only_its_four_texts(self):
        valid = dict(kind='process', summary='S.', stage='any', menu='Process > A')
        self.assertEqual(self.problems(glance={'state': 'Linear.'}, **valid), [])
        self.assertEqual([key for key, _ in self.problems(glance={'when': 'Now.'}, **valid)], ['glance'])

    def test_controls_ignore_names_a_label_and_a_reason(self):
        valid = dict(kind='process', summary='S.', stage='any', menu='Process > A')
        self.assertEqual(self.problems(controls_ignore=[{'label': 'Zoom', 'reason': 'Shared.'}], **valid), [])
        self.assertEqual([key for key, _ in self.problems(controls_ignore=[{'label': 'Zoom'}], **valid)],
                         ['controls_ignore'])

    def test_rewritten_is_true_or_absent(self):
        self.assertEqual([key for key, _ in self.problems(kind='home', rewritten=False)], ['rewritten'])

    def test_menu_paths_lists_what_a_page_states(self):
        self.assertEqual(model.menu_paths({'menu': 'A > B'}), ['A > B'])
        self.assertEqual(model.menu_paths({'menu': ['A > B', 'C']}), ['A > B', 'C'])
        self.assertEqual(model.menu_paths({}), [])

    HERO = {'lede': 'The guide.', 'actions': [{'label': 'Start', 'page': 'start/first.md'}]}

    def test_the_home_page_states_its_hero_paths_and_popular_pages(self):
        picture = dict(self.HERO, accent='guide', image='assets/a.png', alt='A window.', caption='The window.')
        self.assertEqual(self.problems(kind='home', hero=self.HERO), [])
        self.assertEqual(self.problems(
            kind='home', hero=picture, notice='Unfinished.',
            paths=[{'tag': 'First', 'title': 'Install', 'text': 'Set it up.', 'page': 'start/install.md'}],
            popular=['start/first.md', 'reference/glossary.md']), [])

    def test_a_hero_is_stated_whole(self):
        def hero(**changes):
            value = dict(self.HERO, **changes)
            return [key for key, _ in self.problems(kind='home', hero={k: v for k, v in value.items()
                                                                     if v is not None})]
        self.assertEqual(hero(lede=None), ['hero'])
        self.assertEqual(hero(actions=[]), ['hero'])
        self.assertEqual(hero(actions=[{'label': 'Start', 'page': 'start/first.md'}] * 4), ['hero'])
        self.assertEqual(hero(actions=[{'label': 'Start', 'page': 'start/first.html'}]), ['hero'])
        self.assertEqual(hero(actions=[{'label': 'Start'}]), ['hero'])
        self.assertEqual(hero(image='assets/a.png'), ['hero'])
        self.assertEqual(hero(image='assets/a.png', alt='A window.'), ['hero'])
        self.assertEqual(hero(title='Another title'), ['hero'])

    def test_paths_and_popular_name_pages_by_their_source_path(self):
        path = {'tag': 'First', 'title': 'Install', 'text': 'Set it up.', 'page': 'start/install.md'}
        self.assertEqual([key for key, _ in self.problems(kind='home', paths=[])], ['paths'])
        self.assertEqual([key for key, _ in self.problems(kind='home', paths=[path] * 5)], ['paths'])
        self.assertEqual([key for key, _ in self.problems(kind='home', paths=[dict(path, page='/start/x.md')])],
                         ['paths'])
        self.assertEqual([key for key, _ in self.problems(kind='home', paths=[dict(path, icon='x')])], ['paths'])
        self.assertEqual([key for key, _ in self.problems(kind='home', popular=['a.md', 'a.md'])], ['popular'])
        self.assertEqual([key for key, _ in self.problems(kind='home', popular=['Quick start'])], ['popular'])

    def test_meta_images_names_the_home_pages_picture(self):
        self.assertEqual(model.meta_images({'hero': {'image': 'assets/a.png'}}), ['assets/a.png'])
        self.assertEqual(model.meta_images({'hero': 'x'}), [])
        self.assertEqual(model.meta_images({}), [])


def _luminance(color):
    def channel(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    red, green, blue = (int(color[i:i + 2], 16) for i in (1, 3, 5))
    return 0.2126 * channel(red) + 0.7152 * channel(green) + 0.0722 * channel(blue)


def _contrast(first, second):
    lighter, darker = sorted((_luminance(first), _luminance(second)), reverse=True)
    return (lighter + 0.05) / (darker + 0.05)


class StageTests(TempDir):
    def test_the_guide_has_seven_stages_and_any_time_in_pipeline_order(self):
        stages = model.load_stages(str(GUIDE_DIR))
        self.assertEqual([stage.id for stage in stages], STAGE_IDS)
        self.assertEqual([stage.number for stage in stages], [1, 2, 3, 4, 5, 6, 7, None])
        self.assertEqual(stages[4].name, 'Noise & stars')

    def test_the_image_is_linear_until_the_stretch_turns_it(self):
        stages = model.load_stages(str(GUIDE_DIR))
        self.assertEqual([stage.data for stage in stages],
                         ['linear'] * 5 + ['turn', 'nonlinear', None])

    def test_a_numbered_stage_states_where_the_image_stands_and_any_time_does_not(self):
        hue = 'hue: {dark: "#FFFFFF", light: "#000000"}'
        for first, last in (('', ''), ('data: sideways, ', ''), ('data: linear, ', 'data: linear, ')):
            with self.subTest(first=first, last=last):
                guide = self.write_stages('stages:\n  - {id: a, number: 1, %sname: A, description: D, %s}\n'
                                          '  - {id: any, %sname: Any, description: D, %s}\n' % (first, hue, last, hue))
                with self.assertRaisesRegex(model.GuideDataError, '`data`'):
                    model.load_stages(guide)

    def test_every_hue_is_readable_on_its_backgrounds(self):
        for stage in model.load_stages(str(GUIDE_DIR)):
            for color, backgrounds in ((stage.hue_dark, ('#0B0F1A', '#161D2E')), (stage.hue_light, ('#FAFAFA',
                                                                                                   '#FFFFFF'))):
                for background in backgrounds:
                    with self.subTest(stage=stage.id, color=color, background=background):
                        self.assertGreaterEqual(_contrast(color, background), 4.5)

    def write_stages(self, text):
        self.write('guide/data/stages.yml', text)
        return str(self.root / 'guide')

    def test_a_misnumbered_stage_is_refused(self):
        hue = 'hue: {dark: "#FFFFFF", light: "#000000"}'
        guide = self.write_stages('stages:\n  - {id: a, number: 2, name: A, description: D, %s}\n'
                                  '  - {id: any, name: Any, description: D, %s}\n' % (hue, hue))
        with self.assertRaisesRegex(model.GuideDataError, 'numbered 2'):
            model.load_stages(guide)

    def test_a_stage_without_a_hue_is_refused(self):
        guide = self.write_stages('stages:\n  - {id: any, name: Any, description: D}\n')
        with self.assertRaisesRegex(model.GuideDataError, 'hue'):
            model.load_stages(guide)

    def test_repeated_ids_are_refused(self):
        stage = ('{id: a, number: %d, data: linear, name: A, description: D, '
                 'hue: {dark: "#FFFFFF", light: "#000000"}}')
        guide = self.write_stages('stages:\n  - %s\n  - %s\n  - {id: any, name: Any, description: D, '
                                  'hue: {dark: "#FFFFFF", light: "#000000"}}\n' % (stage % 1, stage % 2))
        with self.assertRaisesRegex(model.GuideDataError, 'repeat'):
            model.load_stages(guide)

    def test_a_missing_file_is_a_data_error(self):
        with self.assertRaises(model.GuideDataError):
            model.load_stages(str(self.root))


class NavTests(TempDir):
    def test_the_nav_is_read_past_python_tags_with_its_groups(self):
        self.write('guide/mkdocs.yml', 'docs_dir: docs/en\nmarkdown_extensions:\n  - pymdownx.superfences:\n'
                   '      format: !!python/name:pymdownx.superfences.fence_code_format\n'
                   'nav:\n  - Home: index.md\n  - Processes:\n      - Overview: processes/index.md\n'
                   '      - Stretching:\n          - Curves: processes/curves.md\n'
                   '  - Repository: https://example.org/repo\n')
        guide = str(self.root / 'guide')
        config = model.load_config(guide)
        self.assertEqual(model.docs_dir(guide, config), os.path.normpath(str(self.root / 'guide/docs/en')))
        self.assertEqual(model.nav_entries(config), [
            model.NavEntry('Home', 'index.md', ()),
            model.NavEntry('Overview', 'processes/index.md', ('Processes',)),
            model.NavEntry('Curves', 'processes/curves.md', ('Processes', 'Stretching')),
        ])

    def test_the_guide_navigates_113_pages(self):
        config = model.load_config(str(GUIDE_DIR))
        entries = model.nav_entries(config)
        self.assertEqual(len(entries), 113)
        self.assertEqual(len({entry.path for entry in entries}), 113)


class BodyTests(unittest.TestCase):
    def test_h1_lines_ignore_code_and_lower_headings(self):
        body = ('# Title\n\n## Section\n\n```bash\n# a comment\n```\n\n    ```python\n    # also code\n    ```\n'
                'Text with `# not a heading`.\n<h1 class="x">HTML title</h1>\n#\n')
        self.assertEqual(model.h1_lines(body, first_line=10), [10, 22, 23])

    def test_a_section_is_found_by_its_heading_with_its_line_and_text(self):
        body = ('# Title\n\nOpening.\n\n## Steps\n\n1. One.\n\n```markdown\n## Credits\n- Not this one.\n```\n\n'
                '## Credits ##\n\n- The upstream work.\n\n### Papers\n\n- A paper.\n\n## Related\n\n- A page.\n')
        self.assertEqual(model.section(body, 'Credits', first_line=10),
                         (23, '- The upstream work.\n\n### Papers\n\n- A paper.'))
        self.assertEqual(model.section(body, 'Related'), (22, '- A page.'))
        self.assertIsNone(model.section(body, 'Controls'))

    def test_a_section_of_comments_and_blank_lines_has_no_text(self):
        body = '# Title\n\n## Credits\n\n<!-- to be\nwritten -->\n\n# Another title\n\nText.\n'
        self.assertEqual(model.section(body, 'Credits'), (3, ''))

    def test_image_references_read_markdown_and_html_outside_code(self):
        body = ('![A window](../../assets/images/a.png)\n'
                'Inline ![b](b.webp "Title") and `![not](code.png)`.\n'
                '<img alt="c" src="c.jpg">\n```\n![fenced](d.png)\n```\n'
                '![spaced](<e f.png>)\n')
        self.assertEqual(model.image_references(body, first_line=5), [
            (5, '../../assets/images/a.png'), (6, 'b.webp'), (7, 'c.jpg'), (11, 'e f.png')])


class FenceTests(unittest.TestCase):
    BODY = ('Text.\n\n```figure\nimage: ../assets/a.png\nalt: A.\ncaption: C.\n```\n\n'
            '    ```controls\n    - name: A\n    ```\n\n'
            '````markdown\n```figure\nimage: ../assets/shown.png\n```\n````\n\n'
            '~~~ { .python title="x" }\nprint()\n~~~\n\n```\nplain\n```\n')

    def test_fence_blocks_are_found_with_their_line_and_language(self):
        blocks = model.fence_blocks(self.BODY, first_line=10)
        self.assertEqual([(language, line) for language, line, _ in blocks],
                         [('figure', 12), ('controls', 18), ('markdown', 22), ('python', 28)])
        self.assertEqual(blocks[0][2], 'image: ../assets/a.png\nalt: A.\ncaption: C.')
        self.assertEqual(blocks[1][2], '    - name: A')

    def test_a_figure_blocks_images_are_images_the_page_shows(self):
        self.assertEqual(model.image_references(self.BODY, first_line=10), [(13, '../assets/a.png')])
        pair = '```figure\nimages:\n  - {image: a.png, alt: A, label: Before}\n  - image: b.webp\ncaption: C\n```\n'
        self.assertEqual(model.image_references(pair), [(3, 'a.png'), (4, 'b.webp')])
        self.assertEqual(model.figure_images('image: [unclosed'), [])
        self.assertEqual(model.figure_images('- a list'), [])


class ImageSizeTests(TempDir):
    def test_png(self):
        path = self.write('a.png', b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' +
                          struct.pack('>II', 1600, 900) + b'\x08\x06\x00\x00\x00')
        self.assertEqual(model.image_size(path), (1600, 900))

    def test_lossy_webp(self):
        frame = b'\x00\x00\x00' + b'\x9d\x01\x2a' + struct.pack('<HH', 1200, 675)
        path = self.write('a.webp', b'RIFF' + struct.pack('<I', 4 + 8 + len(frame)) + b'WEBP' + b'VP8 ' +
                          struct.pack('<I', len(frame)) + frame + b'\x00' * 20)
        self.assertEqual(model.image_size(path), (1200, 675))

    def test_lossless_webp(self):
        bits = (1023 - 1) | ((511 - 1) << 14)
        payload = b'\x2f' + struct.pack('<I', bits)
        path = self.write('a.webp', b'RIFF' + struct.pack('<I', 4 + 8 + len(payload)) + b'WEBP' + b'VP8L' +
                          struct.pack('<I', len(payload)) + payload + b'\x00' * 20)
        self.assertEqual(model.image_size(path), (1023, 511))

    def test_extended_webp(self):
        payload = b'\x10\x00\x00\x00' + (1599).to_bytes(3, 'little') + (899).to_bytes(3, 'little')
        path = self.write('a.webp', b'RIFF' + struct.pack('<I', 4 + 8 + len(payload)) + b'WEBP' + b'VP8X' +
                          struct.pack('<I', len(payload)) + payload + b'\x00' * 20)
        self.assertEqual(model.image_size(path), (1600, 900))

    def test_jpeg_past_its_other_segments(self):
        app0 = b'\xff\xe0' + struct.pack('>H', 16) + b'JFIF\x00' + b'\x00' * 9
        dqt = b'\xff\xdb' + struct.pack('>H', 4) + b'\x00\x00'
        sof = b'\xff\xc0' + struct.pack('>H', 17) + b'\x08' + struct.pack('>HH', 768, 1024) + b'\x03' + b'\x00' * 9
        path = self.write('a.jpg', b'\xff\xd8' + app0 + dqt + sof + b'\xff\xd9')
        self.assertEqual(model.image_size(path), (1024, 768))

    def test_anything_else_has_no_size(self):
        self.assertIsNone(model.image_size(self.write('a.gif', b'GIF89a' + b'\x00' * 20)))
        self.assertIsNone(model.image_size(self.write('b.jpg', b'\xff\xd8\xff')))


if __name__ == '__main__':
    unittest.main()
