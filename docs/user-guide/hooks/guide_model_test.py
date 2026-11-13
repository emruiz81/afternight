# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for guide_model.

Run with the documentation toolchain's Python, which has PyYAML:
    docs/user-guide/.venv/Scripts/python docs/user-guide/hooks/guide_model_test.py
    python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"
"""

from __future__ import annotations

import json
import os
import pathlib
import re
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
        self.assertEqual(front.body_line, 19)

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
        self.assertEqual(self.problems(controls_ignore=[{'label': 'Width', 'object': 'percentWidth',
                                                         'reason': 'Follows the other.'}], **valid), [])
        for entry in ({'label': 'Width', 'object': '', 'reason': 'R.'}, {'label': 'Width', 'group': 'Size',
                                                                         'reason': 'R.'}):
            self.assertEqual([key for key, _ in self.problems(controls_ignore=[entry], **valid)], ['controls_ignore'])

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

    def test_the_guide_navigates_each_of_its_pages_once(self):
        # The number of pages is not held: a page that outgrows its budget splits off a reference page.
        config = model.load_config(str(GUIDE_DIR))
        entries = model.nav_entries(config)
        paths = [entry.path for entry in entries]
        self.assertGreaterEqual(len(paths), 113)
        self.assertEqual(len(set(paths)), len(paths))
        docs = pathlib.Path(model.docs_dir(str(GUIDE_DIR), config))
        self.assertEqual([path for path in paths if not (docs / path).is_file()], [])


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


PROSE = """# Title

Opening with `code` and \\( x^2 \\) and a [link](../page.md "Title").

<!-- a note
over two lines -->

## Steps {#the-steps}

1. **One.** First.
2. **Two.** Second,
   continued.
    1. Nested.

```text
## Not a heading
1. Not an item
```

\\[
E = mc^2 \\tag{1}
\\]

### Sub ##

- A bullet
- [ ] A task

Term
:   **Cause.** One.
    More of it.
:   **Fix.** Two.

Another term

:   Its definition.

!!! warning "Careful"
    Text.

??? wrong "If it goes wrong: nothing"
    Text.

| A | B `x|y` | C \\| c |
| --- | :-: | ---: |
| 1 | 2 | 3 |
| 4 | 5 | 6 |
"""


class ProseTests(unittest.TestCase):
    def setUp(self):
        self.lines = model.prose_lines(PROSE, first_line=10)
        self.by_number = dict(self.lines)

    def test_prose_lines_keep_their_numbers_and_columns(self):
        self.assertEqual(len(self.lines), len(PROSE.split('\n')))
        self.assertEqual(self.lines[0], (10, '# Title'))
        opening = self.by_number[12]
        self.assertEqual(len(opening), len(PROSE.split('\n')[2]))
        self.assertIn('Opening with `code` and ', opening)
        self.assertNotIn('x^2', opening)

    def test_code_blocks_comments_and_math_say_nothing(self):
        said = '\n'.join(text for _, text in self.lines)
        for unsaid in ('Not a heading', 'Not an item', 'a note', 'two lines', 'mc^2'):
            self.assertNotIn(unsaid, said)

    def test_headings_are_read_without_their_attributes_and_closing_marks(self):
        self.assertEqual([(heading.level, heading.title, heading.line) for heading in model.headings(self.lines)],
                         [(1, 'Title', 10), (2, 'Steps', 17), (3, 'Sub', 33)])

    def test_section_lines_run_to_the_next_heading_of_the_level_or_above(self):
        found = model.headings(self.lines)
        steps = model.section_lines(self.lines, found, found[1])
        self.assertEqual((steps[0][0], steps[-1][0]), (18, self.lines[-1][0]))
        title = model.section_lines(self.lines, found, found[0])
        self.assertEqual(title[0][0], 11)

    def test_ordered_items_and_bullets_are_the_top_level_ones(self):
        self.assertEqual(model.ordered_items(self.lines), [(19, '**One.** First.'), (20, '**Two.** Second,')])
        self.assertEqual(model.bullets(self.lines), [(35, 'A bullet'), (36, '[ ] A task')])

    def test_definitions_are_terms_with_their_texts(self):
        self.assertEqual(model.definitions(self.lines), [
            (38, 'Term', ['**Cause.** One. More of it.', '**Fix.** Two.']),
            (43, 'Another term', ['Its definition.']),
        ])

    def test_callouts_are_read_with_their_type(self):
        self.assertEqual(model.callouts(self.lines), [(47, 'warning'), (50, 'wrong')])

    def test_tables_count_columns_past_code_spans_and_escaped_bars(self):
        self.assertEqual(model.tables(self.lines), [(53, 3, 2)])
        self.assertEqual(model.tables([(1, 'A | B'), (2, '--- | ---'), (3, '1 | 2'), (4, ''), (5, 'x | y')]),
                         [(1, 2, 1)])
        self.assertEqual(model.tables([(1, 'not | a table'), (2, 'just text')]), [])

    def test_words_are_counted_without_markers_and_addresses(self):
        self.assertEqual(model.count_words('## A heading of five words'), 5)
        self.assertEqual(model.count_words('- [ ] One task\n1. Two words\n:   A definition'), 6)
        self.assertEqual(model.count_words('See [the page](../a/long-address.md "A title") and '
                                           '<https://example.org/x>.'), 4)
        self.assertEqual(model.count_words('| A | B |\n| --- | --- |\n| one two | 3 |'), 5)
        self.assertEqual(model.count_words('!!! warning "Two words"\n    Three more words.'), 5)
        self.assertEqual(model.count_words(''), 0)

    def test_links_give_the_place_of_their_words_and_their_address(self):
        text = 'A [linear](../reference/glossary.md#linear) master and [another](<a b.md> "T").'
        found = model.links(text)
        self.assertEqual([(text[start:end], address) for start, end, address in found],
                         [('linear', '../reference/glossary.md#linear'), ('another', 'a b.md')])

    def test_addresses_and_code_spans_blank_in_place(self):
        text = 'See [the page](verify.md), `validate()` and <b class="evidence">this</b> at https://example.org/verify.'
        said = model.without_code_spans(model.without_addresses(text))
        self.assertEqual(len(said), len(text))
        for unsaid in ('verify', 'validate', 'evidence'):
            self.assertNotIn(unsaid, said)
        self.assertIn('the page', said)
        self.assertIn('this', said)

    def test_inline_images_are_the_ones_outside_figure_blocks(self):
        body = '![A](a.png)\n\n```figure\nimage: b.png\nalt: B.\ncaption: C.\n```\n\n<img src="c.png" alt="">\n'
        self.assertEqual(model.inline_images(body, 5), [(5, 'a.png'), (13, 'c.png')])


class BlockTests(unittest.TestCase):
    def test_a_figure_is_read_as_written(self):
        figure = model.read_figure('image: a.png\nalt: An image.\ncaption: A caption.\npins:\n  - {n: 2, x: 1, y: 1}\n'
                                   '  - {n: two, x: 1, y: 1}\n')
        self.assertEqual(figure, {'pair': False, 'caption': 'A caption.', 'pins': [2], 'owed': False,
                                  'images': [{'image': 'a.png', 'alt': 'An image.', 'label': ''}]})

    def test_a_pair_is_read_with_its_labels_and_what_it_lacks_is_empty(self):
        figure = model.read_figure('images:\n  - image: a.png\n    label: Before\n  - image: b.png\n    alt: After.\n')
        self.assertTrue(figure['pair'])
        self.assertEqual(figure['caption'], '')
        self.assertEqual([image['alt'] for image in figure['images']], ['', 'After.'])

    def test_a_block_that_is_no_mapping_is_not_read(self):
        self.assertIsNone(model.read_figure('- a\n- b\n'))
        self.assertIsNone(model.read_figure('image: [open\n'))

    def test_block_texts_are_the_sentences_under_the_keys_asked_for(self):
        source = ('window: Levels\ncontrols:\n  - name: Black point\n    kind: spinbox\n    effect: Higher maps more '
                  'to black.\n    widget: {min: 0, max: 1}\n  - name: Channel\n    note: >-\n      A mono image '
                  'shows Grayscale.\n')
        self.assertEqual(model.block_texts(source, ('effect', 'note')),
                         [(5, 'effect', 'Higher maps more to black.'), (8, 'note', 'A mono image shows Grayscale.')])
        self.assertEqual(model.block_texts('effect: [open\n', ('effect',)), [])

    def test_a_controls_block_is_read_as_written_with_the_line_of_each_control(self):
        block = model.read_controls('window: Levels\ncontrols:\n  - name: Black point\n    kind: spinbox\n    default: '
                                    '"0.25"\n    widget: {min: 0, max: 1}\n    effect: Higher.\n  - name: Channel\n    '
                                    'object: channelSelector\n    kind: combo\n    default: RGB\n    options: [RGB, '
                                    'Off]\n')
        self.assertEqual(block['window'], 'Levels')
        self.assertEqual(block['controls'], [
            {'line': 3, 'name': 'Black point', 'kind': 'spinbox', 'default': '0.25', 'object': None,
             'widget': {'min': '0', 'max': '1'}, 'options': None},
            {'line': 8, 'name': 'Channel', 'kind': 'combo', 'default': 'RGB', 'object': 'channelSelector',
             'widget': {}, 'options': ['RGB', 'Off']}])

    def test_a_plain_list_of_controls_has_no_window_and_what_a_control_lacks_is_none(self):
        block = model.read_controls('- name: Apply\n  kind: button\n')
        self.assertIsNone(block['window'])
        self.assertEqual(block['controls'][0]['default'], None)
        self.assertEqual(block['controls'][0]['line'], 1)

    def test_a_block_that_is_no_list_of_controls_is_not_read(self):
        for source in ('window: Levels\n', 'controls: none\n', '- Apply\n', 'name: [open\n'):
            with self.subTest(source):
                self.assertIsNone(model.read_controls(source))


def ui_file(identifier='tone.actionLevels', title='Levels <{image}>', controls=None):
    return json.dumps({'id': identifier, 'window': title, 'controls': controls if controls is not None else [
        {'label': 'Black point', 'kind': 'spinbox', 'object': 'blackPoint', 'group': 'Levels > Input',
         'visible': True, 'default': '0.25', 'min': 0, 'max': 1, 'step': 0.01, 'decimals': 2, 'slider': True},
        {'label': 'Width', 'kind': 'spinbox', 'object': 'width', 'group': '', 'visible': True, 'min': 1, 'step': 1,
         'suffix': 'px', 'varies': ['default', 'max']},
        {'label': 'Channel', 'kind': 'combo', 'object': '', 'group': '', 'visible': False, 'default': 'RGB',
         'options': ['RGB', 'Red']},
        {'label': 'Apply', 'kind': 'button', 'object': '', 'group': '', 'visible': True, 'default': '',
         'shared': 'process-window'},
    ]})


class UiInventoryTests(TempDir):
    def test_a_guide_without_an_inventory_has_no_windows(self):
        self.assertEqual(model.load_ui_inventory(str(self.root)), {})

    def test_a_window_is_found_by_its_action_and_named_without_the_image(self):
        self.write('data/ui/tone.actionLevels.json', ui_file())
        self.write('data/ui/README.txt', 'not an inventory')
        windows = model.load_ui_inventory(str(self.root))
        self.assertEqual(list(windows), ['actionLevels'])
        window = windows['actionLevels']
        self.assertEqual((window.id, window.action, window.title, window.name),
                         ('tone.actionLevels', 'actionLevels', 'Levels <{image}>', 'Levels'))
        black, width, channel, apply = window.controls
        self.assertEqual((black.index, black.label, black.kind, black.object, black.default, black.slider),
                         (0, 'Black point', 'spinbox', 'blackPoint', '0.25', True))
        self.assertEqual(black.numbers, {'min': 0, 'max': 1, 'step': 0.01, 'decimals': 2})
        self.assertEqual(black.groups, ('Levels', 'Input'))
        self.assertEqual((width.default, width.numbers, width.suffix, width.varies, width.groups),
                         (None, {'min': 1, 'step': 1}, 'px', ('default', 'max'), ()))
        self.assertEqual((channel.options, channel.visible, channel.shared), (('RGB', 'Red'), False, ''))
        self.assertEqual((apply.shared, apply.options), ('process-window', None))

    def test_a_title_without_an_image_is_the_name(self):
        self.write('data/ui/stack.actionStack.json', ui_file('stack.actionStack', 'Image Stacking', []))
        self.assertEqual(model.load_ui_inventory(str(self.root))['actionStack'].name, 'Image Stacking')

    def test_the_inventory_of_the_guide_reads(self):
        windows = model.load_ui_inventory(str(GUIDE_DIR))
        self.assertTrue(windows)
        for window in windows.values():
            self.assertTrue(window.name, window.id)
            for control in window.controls:
                self.assertTrue(control.label, '%s: control %d' % (window.id, control.index + 1))

    def test_what_is_not_an_inventory_is_refused(self):
        control = {'label': 'A', 'kind': 'check', 'object': 'a', 'group': '', 'visible': True, 'default': 'On'}
        for name, text, message in (
                ('tone.actionLevels.json', '{"id": "tone.actionLevels"', 'cannot be read'),
                ('tone.actionLevels.json', '[]', 'an inventory states `id`, `window` and `controls`'),
                ('other.json', ui_file(), 'the file is named by its `id`'),
                ('tone.actionLevels.json', ui_file(controls=[dict(control, kind='dial')]), 'the unknown kind \'dial\''),
                ('tone.actionLevels.json', ui_file(controls=[dict(control, colour='red')]), 'the unknown key `colour`'),
                ('tone.actionLevels.json', ui_file(controls=[dict(control, visible='yes')]), '`visible` is \'yes\''),
                ('tone.actionLevels.json', ui_file(controls=[dict(control, min=True)]), '`min` is True'),
                ('tone.actionLevels.json', ui_file(controls=[{'label': 'A', 'kind': 'check', 'object': 'a',
                                                              'group': '', 'visible': True}]),
                 'control 1 does not state `default`'),
                ('tone.actionLevels.json', ui_file(controls=[dict(control, varies=['default'])]),
                 '`varies` names what the record does not state'),
                ('tone.actionLevels.json', ui_file(controls=[dict(control, varies=['label'])]),
                 '`varies` names what the record does not state'),
        ):
            with self.subTest(message):
                shutil.rmtree(self.root / 'data', True)
                self.write('data/ui/' + name, text)
                with self.assertRaises(model.GuideDataError) as raised:
                    model.load_ui_inventory(str(self.root))
                self.assertIn(message, str(raised.exception))

    def test_two_files_of_one_action_are_refused(self):
        self.write('data/ui/tone.actionLevels.json', ui_file())
        self.write('data/ui/other.actionLevels.json', ui_file('other.actionLevels'))
        with self.assertRaises(model.GuideDataError) as raised:
            model.load_ui_inventory(str(self.root))
        self.assertIn('are inventories of one action, actionLevels', str(raised.exception))


LIMITS = (GUIDE_DIR / 'data' / 'limits.yml').read_text(encoding='utf-8')
# The guide's own allowances name its pages, which a test's folder does not have: a test starts from none.
VOCABULARY = re.sub(r'(?ms)^allow:.*\Z', 'allow: []\n',
                    (GUIDE_DIR / 'data' / 'vocabulary.yml').read_text(encoding='utf-8'))


class LimitsTests(TempDir):
    def load(self, text):
        self.write('data/limits.yml', text)
        return model.load_limits(str(self.root))

    def test_the_guides_limits_are_the_budgets_of_its_kinds(self):
        limits = model.load_limits(str(GUIDE_DIR))
        self.assertEqual((limits.summary_words, limits.table_columns), (40, 5))
        self.assertEqual((limits.of('process', 'words'), limits.of('process', 'steps'),
                          limits.of('process', 'troubleshooting')), (1800, 9, 8))
        self.assertEqual((limits.of('interface', 'words'), limits.of('tutorial', 'words'),
                          limits.of('workflow', 'steps')), (1200, 2500, 12))
        self.assertEqual((limits.image_longest_side, limits.image_max_bytes), (1600, 400 * 1024))
        self.assertEqual(limits.glossary_page, 'reference/glossary.md')
        self.assertEqual(limits.provenance_page, 'assets/MEDIA_PROVENANCE.md')
        self.assertIn('Stretching', limits.before_after_groups)
        self.assertTrue(all(reason.strip() for reason in limits.before_after_exempt.values()))

    def test_a_large_page_has_the_budgets_its_kind_raises_and_the_others_unchanged(self):
        limits = model.load_limits(str(GUIDE_DIR))
        page = 'processes/pixel-math/pixel-math.md'
        self.assertEqual((limits.of('process', 'words', page), limits.of('process', 'steps', page),
                          limits.of('process', 'troubleshooting', page)), (4000, 12, 12))
        self.assertEqual(limits.of('process', 'related', page), limits.of('process', 'related'))
        self.assertEqual(limits.of('interface', 'words', 'interface/settings-dialog.md'), 3000)
        self.assertEqual(limits.of('process', 'words', 'processes/stretching/gh-transformation.md'), 1800)

    def test_a_limit_a_kind_does_not_state_does_not_apply(self):
        limits = model.load_limits(str(GUIDE_DIR))
        self.assertIsNone(limits.of('reference', 'words'))
        self.assertIsNone(limits.of('interface', 'steps'))
        self.assertIsNone(limits.of('home', 'words'))

    def test_every_large_page_and_named_page_is_a_page_of_the_guide(self):
        limits = model.load_limits(str(GUIDE_DIR))
        docs = pathlib.Path(model.docs_dir(str(GUIDE_DIR), model.load_config(str(GUIDE_DIR))))
        for page in sorted(limits.large_pages) + sorted(limits.before_after_exempt) + [limits.glossary_page,
                                                                                     limits.provenance_page]:
            self.assertTrue((docs / page).is_file(), page)

    def test_every_before_and_after_group_is_a_nav_group(self):
        limits = model.load_limits(str(GUIDE_DIR))
        groups = {group for entry in model.nav_entries(model.load_config(str(GUIDE_DIR))) for group in entry.groups}
        for group in limits.before_after_groups:
            self.assertIn(group, groups)

    def test_what_is_not_a_limit_is_refused(self):
        for old, new, message in (
                ('    steps: 9 ', '    stepz: 9 ', '`process.stepz` is no limit of a process page'),
                ('    words: 1800', '    words: many', '`process.words` is \'many\', not a count'),
                ('    words: 1800', '    words: 0', 'not a count'),
                ('summary_words: 40', 'summary_words: forty', '`summary_words` is \'forty\', not a count'),
                ('table_columns: 5\n', '', '`table_columns` is missing'),
                ('table_columns: 5\n', 'table_columns: 5\nfloor_words: 600\n', 'unknown key `floor_words`'),
                ('  reference: {}', '  catalog: {}', '`kinds` names \'catalog\''),
                ('large: {words: 3000}', 'large: {steps: 3}', '`interface.large.steps` is 3'),
                ('  - interface/settings-dialog.md', '  - Settings', '`large_pages` is a list of distinct pages'),
                ('  entry_words: 80', '  entry_words: eighty', '`glossary` states'),
                ('  max_bytes: 409600', '  max_bytes: 400 KB', '`images` states'),
                ('before_after_groups:', 'before_after_groups: Stretching\nother:', '`other`'),
                ('before_after_exempt:\n  - page: processes/', 'before_after_exempt:\n  - page: /processes/',
                 '`before_after_exempt` is a list of {page, reason}'),
                ('before_after_exempt:\n', 'before_after_exempt:\n  - page: reference/glossary.md\n',
                 '`before_after_exempt` is a list of {page, reason}'),
        ):
            with self.subTest(new):
                self.assertEqual(LIMITS.count(old), 1, old)
                with self.assertRaises(model.GuideDataError) as raised:
                    self.load(LIMITS.replace(old, new))
                self.assertIn(message, str(raised.exception))

    def test_a_page_is_excused_from_its_pair_once_and_with_a_reason(self):
        start = LIMITS.index('before_after_exempt:')
        entry = '  - {page: processes/a.md, reason: Its result is a file.}\n'
        limits = self.load(LIMITS[:start] + 'before_after_exempt:\n' + entry)
        self.assertEqual(limits.before_after_exempt, {'processes/a.md': 'Its result is a file.'})
        self.assertEqual(self.load(LIMITS[:start] + 'before_after_exempt: []\n').before_after_exempt, {})
        with self.assertRaises(model.GuideDataError) as raised:
            self.load(LIMITS[:start] + 'before_after_exempt:\n' + entry + entry)
        self.assertIn('distinct pages', str(raised.exception))

    def test_a_missing_file_is_a_data_error(self):
        with self.assertRaises(model.GuideDataError):
            model.load_limits(str(self.root))


class VocabularyTests(TempDir):
    def load(self, text):
        self.write('data/vocabulary.yml', text)
        return model.load_vocabulary(str(self.root))

    def found(self, vocabulary, text, code=False):
        return sorted({pattern.group for pattern in vocabulary.patterns
                       if (pattern.code or not code) and pattern.regex.search(text)})

    def test_the_guides_vocabulary_finds_proof_tooling_and_dating(self):
        vocabulary = model.load_vocabulary(str(GUIDE_DIR))
        for text, group in (
                ('The evidence shows it.', 'proof'), ('Verified on a real set.', 'proof'),
                ('Validation passed.', 'proof'), ('Mode S only.', 'proof'), ('A source-derived default.', 'proof'),
                ('Focused tests establish it.', 'proof'), ('The authoritative page.', 'proof'),
                ('Checked 2026-07-27.', 'proof'), ('It calls applyStretch().', 'tooling'),
                ('See an::ui::Stretch.', 'tooling'), ('In HistogramProcess.cpp.', 'tooling'),
                ('Test_ProcessesTone covers it.', 'tooling'), ('MEDIA_SLOT', 'tooling'),
                ('The current behavior is this.', 'dating'), ('Currently implementation-defined: current code.',
                                                               'dating'),
                ('It does not promise a result.', 'dating'),
        ):
            with self.subTest(text):
                self.assertEqual(self.found(vocabulary, text), [group])
        for text in ('Check the result at 100 %.', 'The valid range is 0 to 1.', 'Use the current image.',
                     'The header (a FITS card).', 'A ratio of 1:2.', 'An evident gradient.'):
            with self.subTest(text):
                self.assertEqual(self.found(vocabulary, text), [])

    def test_a_code_span_is_read_only_for_names_of_source(self):
        vocabulary = model.load_vocabulary(str(GUIDE_DIR))
        self.assertEqual(self.found(vocabulary, 'mtf()', code=True), [])
        self.assertEqual(self.found(vocabulary, 'verify', code=True), [])
        for text in ('Stretch.cpp', 'Stretch.h', 'an::Stretch', 'Test_EngineImage'):
            self.assertEqual(self.found(vocabulary, text, code=True), ['tooling'])

    def test_every_allowance_of_the_guide_names_one_of_its_pages(self):
        for allowance in model.load_vocabulary(str(GUIDE_DIR)).allow:
            self.assertTrue((GUIDE_DIR / 'docs' / 'en' / allowance.page).is_file(), allowance.page)

    def test_allowances_are_read_with_their_lines(self):
        allow = ('allow:\n  - page: processes/a.md\n    text: "**Validate**"\n    reason: A label.\n'
                 '  - {page: processes/b.md, text: Verify, reason: Another label.}\n')
        vocabulary = self.load(VOCABULARY.replace('allow: []\n', allow))
        first = len(VOCABULARY.split('allow: []')[0].split('\n')) + 1
        self.assertEqual([(entry.page, entry.text, entry.reason, entry.line) for entry in vocabulary.allow],
                         [('processes/a.md', '**Validate**', 'A label.', first),
                          ('processes/b.md', 'Verify', 'Another label.', first + 3)])

    def test_what_is_not_a_vocabulary_is_refused(self):
        for old, new, message in (
                ("'source-derived'", "'source-derived('", 'is not a regular expression'),
                ("'source-derived'", '12', 'it is a regular expression, or {pattern, code}'),
                ('allow: []', 'allow:\n  - {page: a.md, text: Validate}', 'an entry of `allow`'),
                ('allow: []', 'allow: none', '`allow` is a list'),
                ('allow: []', 'allow: []\nextra: 1', 'the file states `forbidden` and `allow`'),
                ('    why: >-\n      what proves', '    because: >-\n      what proves', '`forbidden.proof` states'),
        ):
            with self.subTest(new):
                self.assertEqual(VOCABULARY.count(old), 1, old)
                with self.assertRaises(model.GuideDataError) as raised:
                    self.load(VOCABULARY.replace(old, new))
                self.assertIn(message, str(raised.exception))


if __name__ == '__main__':
    unittest.main()
