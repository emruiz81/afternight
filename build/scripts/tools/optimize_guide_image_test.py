# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for optimize_guide_image.

Run with the Python that has PyYAML and Pillow (docs/user-guide/requirements-capture.txt installed into the
documentation environment):
    python build/scripts/tools/optimize_guide_image_test.py

Every case plants a guide with the real data/limits.yml in a temporary directory and draws the capture it needs: a
window of flat panels under a plot's grid, a field of noise that stands for a sky image, a picture larger than the
limit. Without Pillow or PyYAML the whole module is skipped, because continuous integration installs neither for it.
"""

from __future__ import annotations

import contextlib
import datetime
import hashlib
import io
import os
import pathlib
import random
import re
import shutil
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import optimize_guide_image as tool  # noqa: E402

try:
    import yaml  # noqa: F401,E402 - the guide's model needs it
    from PIL import Image, ImageDraw, ImageFilter, PngImagePlugin  # noqa: E402
    MISSING = ''
except ImportError as error:
    MISSING = str(error)

REAL_GUIDE = pathlib.Path(tool.REPO_ROOT) / 'docs' / 'user-guide'
MKDOCS = 'site_name: Test guide\ndocs_dir: docs/en\n'
ROW = re.compile(r'^\| `([^`|]+)` \| (.+) \| (.+) \| (\d{4}-\d{2}-\d{2})(?:, AfterNight (\S+))? \| `([0-9a-f]{64})` \|$')
PANEL = (0x16, 0x1D, 0x2E)
GRID = (0x60, 0xA5, 0xFA)


def setUpModule():
    if MISSING:
        raise unittest.SkipTest('optimize_guide_image needs PyYAML and Pillow (%s); install %s into the '
                                'documentation environment' % (MISSING, tool.CAPTURE_REQUIREMENTS))


def panels(width, height):
    """A window of flat panels with a plot's grid over them: sharp lines of one pixel in a few colors, which a PNG
    holds in little and a lossy encoder pays for line by line."""
    image = Image.new('RGB', (width, height), PANEL)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, width, height // 20), fill=(0x0B, 0x0F, 0x1A))
    for x in range(3, width - 8, 7):
        draw.line((x, height // 20, x, height - 8), fill=GRID)
    for y in range(height // 20, height - 8, 5):
        draw.line((0, y, width - 8, y), fill=(0xFB, 0x71, 0x85))
    return image


def sky(width, height, seed=7):
    """A field of soft noise over a gradient: what a sky image is to an encoder."""
    noise = Image.frombytes('RGB', (width, height), random.Random(seed).randbytes(width * height * 3))
    ground = Image.linear_gradient('L').resize((width, height)).convert('RGB')
    return Image.blend(ground, noise.filter(ImageFilter.GaussianBlur(1)), 0.5)


def webp_bytes(image, quality):
    buffer = io.BytesIO()
    image.save(buffer, 'WEBP', quality=quality, method=tool.WEBP_METHOD)
    return buffer.getvalue()


def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, 'PNG', optimize=True)
    return buffer.getvalue()


class OptimizeCase(unittest.TestCase):
    """A guide with the real limits, an empty docs folder and a folder of captures beside it."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.guide = self.root / 'docs' / 'user-guide'
        self.docs = self.guide / 'docs' / 'en'
        self.docs.mkdir(parents=True)
        (self.guide / 'data').mkdir()
        (self.guide / 'mkdocs.yml').write_text(MKDOCS, encoding='utf-8', newline='')
        self.limits_text = (REAL_GUIDE / 'data' / 'limits.yml').read_text(encoding='utf-8')
        self.limit()
        self.model = tool._load_model()
        self.real = self.model.load_limits(str(REAL_GUIDE))

    def limit(self, longest_side=None, max_bytes=None):
        """The guide's limits: the real ones, with the images' size or weight replaced."""
        text = self.limits_text
        for key, value in (('longest_side', longest_side), ('max_bytes', max_bytes)):
            if value is not None:
                text, count = re.subn(r'(?m)^(\s+%s:\s*)\d+' % key, r'\g<1>%d' % value, text)
                self.assertEqual(count, 1, key)
        (self.guide / 'data' / 'limits.yml').write_text(text, encoding='utf-8', newline='')

    def capture(self, name, image, **options):
        path = self.root / 'captures' / name
        path.parent.mkdir(exist_ok=True)
        image.save(path, **options)
        return str(path)

    def run_tool(self, source, to, *more, shows='The window as it opens.', data='A linear master.'):
        stream = io.StringIO()
        arguments = [source, '--to', str(self.docs / to), '--shows', shows, '--data', data, '--guide',
                     str(self.guide)] + list(more)
        code = tool.main(arguments, stream)
        return code, stream.getvalue().splitlines()

    def written(self):
        """Every file under the docs folder, as paths relative to it."""
        return sorted(path.relative_to(self.docs).as_posix() for path in self.docs.rglob('*') if path.is_file())

    def assertWritten(self, relative, code, lines):
        """The run wrote this one file, in the format its extension names."""
        self.assertEqual(code, 0, lines)
        self.assertEqual(self.written(), [relative], lines)
        return self.docs / relative

    def picture(self, stem, code, lines):
        """The picture a run wrote under a name, whichever format was kept."""
        self.assertEqual(code, 0, lines)
        found = [path for path in (self.docs / (stem + extension) for extension in ('.png', '.webp'))
                 if path.is_file()]
        self.assertEqual(len(found), 1, self.written())
        return found[0]

    def assertNear(self, pixel, color, tolerance=4):
        """A pixel has a color, within what a lossy encoding moves it."""
        for got, wanted in zip(pixel, color):
            self.assertLessEqual(abs(got - wanted), tolerance, (pixel, color))


class FormatTests(OptimizeCase):
    def test_a_window_of_panels_and_fine_lines_is_kept_as_png(self):
        image = panels(1200, 800)
        code, lines = self.run_tool(self.capture('flat.png', image), 'assets/images/processes/a/flat')
        path = self.assertWritten('assets/images/processes/a/flat.png', code, lines)
        self.assertIn('flat.png: PNG, 1200 x 800 px, %d bytes' % path.stat().st_size, lines[0])
        self.assertNotIn('quality', lines[0])
        self.assertEqual(path.read_bytes(), png_bytes(image))
        self.assertLess(path.stat().st_size, len(webp_bytes(image, tool.WEBP_QUALITY)))

    def test_a_window_with_a_sky_image_is_kept_as_webp_at_the_usual_quality(self):
        image = sky(900, 600)
        code, lines = self.run_tool(self.capture('sky.png', image), 'assets/images/processes/a/sky')
        path = self.assertWritten('assets/images/processes/a/sky.webp', code, lines)
        self.assertIn('sky.webp: WebP, 900 x 600 px, %d bytes' % path.stat().st_size, lines[0])
        self.assertNotIn('quality', lines[0])
        self.assertEqual(path.read_bytes(), webp_bytes(image, tool.WEBP_QUALITY))
        self.assertLess(path.stat().st_size, len(png_bytes(image)))
        self.assertLessEqual(path.stat().st_size, self.real.image_max_bytes)

    def test_the_format_kept_sets_the_extension_whatever_the_destination_carries(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat.webp')
        self.assertWritten('assets/images/flat.png', code, lines)
        code, lines = self.run_tool(self.capture('sky.png', sky(300, 200)), 'assets/images/sky.PNG')
        self.assertEqual(code, 0, lines)
        self.assertEqual(self.written(), ['assets/images/flat.png', 'assets/images/sky.webp'])

    def test_a_name_with_a_dot_keeps_it(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat.v2')
        self.assertWritten('assets/images/flat.v2.png', code, lines)

    def test_a_picture_left_by_another_run_in_the_other_format_is_noted(self):
        other = self.docs / 'assets/images/flat.webp'
        other.parent.mkdir(parents=True)
        other.write_bytes(webp_bytes(panels(60, 40), 90))
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat')
        self.assertEqual(code, 0, lines)
        self.assertEqual(self.written(), ['assets/images/flat.png', 'assets/images/flat.webp'])
        self.assertEqual(len(lines), 3, lines)
        self.assertIn('note: ', lines[1])
        self.assertIn('flat.webp is there as well', lines[1])
        self.assertRegex(lines[2], ROW)


class SizeTests(OptimizeCase):
    def test_a_large_picture_is_scaled_to_the_longest_side_and_keeps_its_shape(self):
        longest = self.real.image_longest_side
        height = longest + longest // 4
        for name, size, expected in (('wide', (2 * longest, height), (longest, height // 2)),
                                     ('tall', (longest + 100, 2 * longest + 200), (longest // 2, longest))):
            with self.subTest(name):
                code, lines = self.run_tool(self.capture(name + '.png', panels(*size)), 'assets/images/' + name)
                path = self.picture('assets/images/' + name, code, lines)
                self.assertEqual(self.model.image_size(str(path)), expected)
                self.assertIn('%d x %d px (from %d x %d)' % (expected + size), lines[0])
                self.assertLessEqual(path.stat().st_size, self.real.image_max_bytes)

    def test_a_picture_within_the_limit_is_never_scaled(self):
        for name, size in (('small', (400, 300)), ('full', (self.real.image_longest_side, 900))):
            with self.subTest(name):
                code, lines = self.run_tool(self.capture(name + '.png', panels(*size)), 'assets/images/' + name)
                self.assertEqual(self.model.image_size(str(self.picture('assets/images/' + name, code, lines))), size)
                self.assertNotIn('(from ', lines[0])

    def test_the_limits_are_the_guides_own(self):
        self.limit(longest_side=500)
        code, lines = self.run_tool(self.capture('flat.png', panels(1000, 600)), 'assets/images/flat')
        self.assertEqual(self.model.image_size(str(self.picture('assets/images/flat', code, lines))), (500, 300))

    def test_crop_keeps_the_part_named_in_the_sources_pixels(self):
        image = Image.new('RGB', (400, 300), (10, 20, 30))
        ImageDraw.Draw(image).rectangle((100, 50, 299, 199), fill=(200, 100, 50))
        code, lines = self.run_tool(self.capture('quarters.png', image), 'assets/images/part', '--crop',
                                    '100,50,200,150')
        with Image.open(self.picture('assets/images/part', code, lines)) as result:
            self.assertEqual(result.size, (200, 150))
            for place in ((0, 0), (199, 0), (0, 149), (199, 149), (100, 75)):
                self.assertNear(result.convert('RGB').getpixel(place), (200, 100, 50))
        self.assertIn('200 x 150 px (from 400 x 300)', lines[0])

    def test_a_crop_is_taken_before_the_picture_is_scaled(self):
        longest = self.real.image_longest_side
        code, lines = self.run_tool(self.capture('wide.png', panels(2 * longest, 1000)), 'assets/images/part',
                                    '--crop', '0,0,%d,1000' % longest)
        self.assertEqual(self.model.image_size(str(self.picture('assets/images/part', code, lines))),
                         (longest, 1000))

    def test_a_crop_that_reaches_outside_the_picture_is_refused(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(400, 300)), 'assets/images/part', '--crop',
                                    '300,0,200,100')
        self.assertEqual(code, 2, lines)
        self.assertIn('reaches outside the picture, which is 400 x 300 px', lines[0])
        self.assertEqual(self.written(), [])


class WeightTests(OptimizeCase):
    def test_a_lower_quality_is_tried_until_the_picture_fits(self):
        image = sky(900, 600)
        sizes = {quality: len(webp_bytes(image, quality)) for quality in (90, 85, 80, 75)}
        self.assertGreater(sizes[80], sizes[75])
        self.limit(max_bytes=sizes[75])
        code, lines = self.run_tool(self.capture('sky.png', image), 'assets/images/sky')
        path = self.assertWritten('assets/images/sky.webp', code, lines)
        self.assertEqual(path.stat().st_size, sizes[75])
        self.assertTrue(lines[0].endswith('%d bytes, quality 75' % sizes[75]), lines[0])
        self.assertRegex(lines[1], ROW)
        self.assertEqual(ROW.match(lines[1]).group(6), hashlib.sha256(path.read_bytes()).hexdigest())

    def test_the_lowest_quality_tried_is_the_floor(self):
        image = sky(900, 600)
        self.limit(max_bytes=len(webp_bytes(image, tool.WEBP_QUALITY_FLOOR)))
        code, lines = self.run_tool(self.capture('sky.png', image), 'assets/images/sky')
        self.assertWritten('assets/images/sky.webp', code, lines)
        self.assertIn('quality %d' % tool.WEBP_QUALITY_FLOOR, lines[0])

    def test_a_picture_that_fits_at_no_quality_is_refused_and_nothing_is_written(self):
        image = sky(900, 600)
        self.limit(max_bytes=len(webp_bytes(image, tool.WEBP_QUALITY_FLOOR)) - 1)
        code, lines = self.run_tool(self.capture('sky.png', image), 'assets/images/processes/a/sky')
        self.assertEqual(code, 1, lines)
        self.assertEqual(len(lines), 1, lines)
        self.assertIn('900 x 600 px', lines[0])
        self.assertIn('WebP at quality %d' % tool.WEBP_QUALITY_FLOOR, lines[0])
        self.assertIn('crop it', lines[0])
        self.assertEqual(self.written(), [])
        self.assertFalse((self.docs / 'assets').exists())


class ChoiceTests(unittest.TestCase):
    """Which file is kept, over encoders that give the weights a case names."""

    class Picture:
        width, height = 1600, 1000

    def lightest(self, max_bytes, png, webp):
        """What is kept of a picture that weighs `png` bytes as PNG and `webp[quality]` bytes as WebP, and the
        encodings that were asked for."""
        asked = []

        def encoded(image, file_format, quality=None):
            asked.append((file_format, quality))
            return b'\0' * (png if file_format == 'PNG' else webp[quality])

        with mock.patch.object(tool, 'encoded', side_effect=encoded):
            name, extension, data, quality = tool.lightest(self.Picture(), max_bytes)
        return (name, extension, len(data), quality), asked

    def test_the_png_is_kept_when_it_is_the_smaller(self):
        kept, asked = self.lightest(5000, png=1000, webp={90: 1200})
        self.assertEqual(kept, ('PNG', '.png', 1000, None))
        self.assertEqual(sorted(asked, key=str), [('PNG', None), ('WEBP', 90)])

    def test_the_webp_is_kept_when_it_is_the_smaller_or_weighs_the_same(self):
        self.assertEqual(self.lightest(5000, png=1300, webp={90: 1200})[0], ('WebP', '.webp', 1200, 90))
        self.assertEqual(self.lightest(5000, png=1200, webp={90: 1200})[0], ('WebP', '.webp', 1200, 90))

    def test_what_is_kept_may_weigh_the_limit_itself(self):
        self.assertEqual(self.lightest(1200, png=1300, webp={90: 1200})[0], ('WebP', '.webp', 1200, 90))

    def test_a_png_over_the_limit_gives_way_to_the_first_webp_that_fits(self):
        kept, asked = self.lightest(950, png=1000, webp={90: 1200, 85: 1100, 80: 900, 75: 800})
        self.assertEqual(kept, ('WebP', '.webp', 900, 80))
        self.assertEqual(asked[2:], [('WEBP', 85), ('WEBP', 80)])

    def test_every_quality_down_to_the_floor_is_tried_and_none_below_it(self):
        weights = {quality: 1000 + quality for quality in range(60, 95, 5)}
        with self.assertRaises(tool.Refused) as refused:
            self.lightest(1024, png=5000, webp=weights)
        self.assertEqual(refused.exception.code, 1)
        self.assertIn('1600 x 1000 px, weighs 2 KB at its lightest, WebP at quality 60, over the limit of 1 KB',
                      str(refused.exception))
        kept, asked = self.lightest(1060, png=5000, webp=weights)
        self.assertEqual(kept, ('WebP', '.webp', 1060, 60))
        self.assertEqual([quality for file_format, quality in asked if file_format == 'WEBP'],
                         [90, 85, 80, 75, 70, 65, 60])


class AlphaTests(OptimizeCase):
    def test_an_alpha_channel_that_is_opaque_throughout_is_dropped(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(300, 200).convert('RGBA')), 'assets/images/flat')
        with Image.open(self.picture('assets/images/flat', code, lines)) as result:
            self.assertEqual(result.mode, 'RGB')

    def test_an_alpha_channel_the_capture_never_wrote_is_dropped_and_the_picture_kept(self):
        image = panels(300, 200).convert('RGBA')
        image.putalpha(0)
        code, lines = self.run_tool(self.capture('flat.png', image), 'assets/images/flat')
        with Image.open(self.picture('assets/images/flat', code, lines)) as result:
            self.assertEqual(result.mode, 'RGB')
            self.assertNear(result.getpixel((299, 199)), PANEL)
            self.assertNear(result.getpixel((3, 101)), GRID)

    def test_transparent_corners_are_kept(self):
        image = panels(300, 200).convert('RGBA')
        for corner in ((0, 0), (299, 0), (0, 199), (299, 199)):
            image.putpixel(corner, (0, 0, 0, 0))
        code, lines = self.run_tool(self.capture('flat.png', image), 'assets/images/flat')
        with Image.open(self.picture('assets/images/flat', code, lines)) as result:
            self.assertEqual(result.mode, 'RGBA')
            self.assertEqual(result.getpixel((0, 0))[3], 0)
            self.assertEqual(result.getpixel((150, 100))[3], 255)

    def test_a_sky_image_with_transparent_corners_is_a_webp_the_guide_reads(self):
        image = sky(600, 400).convert('RGBA')
        image.putpixel((0, 0), (0, 0, 0, 0))
        code, lines = self.run_tool(self.capture('sky.png', image), 'assets/images/sky')
        path = self.assertWritten('assets/images/sky.webp', code, lines)
        self.assertEqual(self.model.image_size(str(path)), (600, 400))
        with Image.open(path) as result:
            self.assertEqual(result.getpixel((0, 0))[3], 0)
            self.assertEqual(result.getpixel((300, 200))[3], 255)


class ProvenanceTests(OptimizeCase):
    def test_the_row_names_the_picture_what_it_shows_its_data_its_day_its_commit_and_its_hash(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)),
                                    'assets/images/processes/stretching/gamma-correction', '--date', '2026-11-09',
                                    '--commit', '50da371', shows='The Gamma Correction window.',
                                    data='The linear master of a nebula.')
        path = self.assertWritten('assets/images/processes/stretching/gamma-correction.png', code, lines)
        self.assertEqual(len(lines), 2, lines)
        self.assertEqual(lines[1], '| `assets/images/processes/stretching/gamma-correction.png` | The Gamma '
                         'Correction window. | The linear master of a nebula. | 2026-11-09, AfterNight 50da371 | `%s` |'
                         % hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(len(re.split(r'(?<!\\)\|', lines[1])), 7)

    def test_the_day_is_today_and_the_commit_this_repositorys_unless_given(self):
        with mock.patch.object(tool, 'head_commit', return_value='abc1234') as head:
            code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat')
        self.assertEqual(code, 0, lines)
        head.assert_called_once_with(tool.REPO_ROOT)
        row = ROW.match(lines[1])
        self.assertIsNotNone(row, lines[1])
        self.assertEqual(row.group(4), datetime.date.today().isoformat())
        self.assertEqual(row.group(5), 'abc1234')

    def test_a_row_without_a_commit_states_the_day_alone(self):
        with mock.patch.object(tool, 'head_commit', return_value=''):
            code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat', '--date',
                                        '2026-11-09')
        self.assertEqual(code, 0, lines)
        self.assertIn(' | 2026-11-09 | `', lines[1])
        self.assertNotIn('AfterNight', lines[1])
        self.assertRegex(lines[1], ROW)

    def test_this_repositorys_head_is_a_short_hash(self):
        self.assertEqual(tool.head_commit(str(self.root)), '')
        if not os.path.exists(os.path.join(tool.REPO_ROOT, '.git')):
            self.skipTest('the tool does not sit in a git checkout')
        self.assertRegex(tool.head_commit(tool.REPO_ROOT), r'^[0-9a-f]{7,40}$')

    def test_a_bar_and_a_line_break_in_the_words_stay_in_their_cell(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat', '--date',
                                    '2026-11-09', '--commit', 'abc1234', shows='Before | after,\nside by side.',
                                    data='A | B')
        self.assertEqual(code, 0, lines)
        self.assertEqual(len(lines), 2, lines)
        self.assertIn('| Before \\| after, side by side. | A \\| B |', lines[1])
        self.assertEqual(len(re.split(r'(?<!\\)\|', lines[1])), 7)

    def test_the_row_is_found_by_the_check_of_the_pictures_provenance(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat')
        self.assertEqual(code, 0, lines)
        # Rule R11 of check_user_guide.py reads the provenance page for the picture's path set as code.
        self.assertIn('assets/images/flat.png', re.findall(r'`([^`\s]+)`', lines[1]))


class RefusalTests(OptimizeCase):
    def test_a_destination_outside_the_docs_folder_is_refused(self):
        source = self.capture('flat.png', panels(600, 400))
        for destination in (self.guide / 'theme' / 'flat', self.root / 'flat.png', self.docs / '..' / 'flat'):
            with self.subTest(destination):
                stream = io.StringIO()
                code = tool.main([source, '--to', str(destination), '--shows', 'x', '--data', 'y', '--guide',
                                  str(self.guide)], stream)
                self.assertEqual(code, 2, stream.getvalue())
                self.assertIn('outside the guide\'s docs folder', stream.getvalue())
                self.assertEqual(self.written(), [])
                self.assertFalse((self.guide / 'theme').exists())
                self.assertEqual(sorted(path.name for path in self.root.iterdir()), ['captures', 'docs'])

    def test_a_destination_that_is_a_folder_is_refused(self):
        (self.docs / 'assets').mkdir()
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets')
        self.assertEqual(code, 2, lines)
        self.assertIn('a folder', lines[0])
        self.assertEqual(self.written(), [])

    def test_a_file_that_is_no_picture_is_refused(self):
        path = self.root / 'notes.png'
        path.write_text('not a picture', encoding='utf-8')
        code, lines = self.run_tool(str(path), 'assets/images/notes')
        self.assertEqual(code, 1, lines)
        self.assertIn('is not a picture this can read', lines[0])
        self.assertEqual(self.written(), [])

    def test_a_source_that_is_not_there_is_refused(self):
        code, lines = self.run_tool(str(self.root / 'gone.png'), 'assets/images/gone')
        self.assertEqual(code, 1, lines)
        self.assertIn('is not a picture this can read', lines[0])

    def test_a_picture_of_sixteen_bits_is_refused(self):
        code, lines = self.run_tool(self.capture('deep.png', Image.new('I;16', (200, 100), 40000)),
                                    'assets/images/deep')
        self.assertEqual(code, 1, lines)
        self.assertIn('not of eight bits a channel', lines[0])
        self.assertEqual(self.written(), [])

    def test_words_that_say_nothing_are_refused(self):
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat', shows='  ')
        self.assertEqual(code, 2, lines)
        self.assertEqual(self.written(), [])

    def test_a_wrong_command_line_is_refused(self):
        source = self.capture('flat.png', panels(600, 400))
        common = ['--shows', 'x', '--data', 'y', '--guide', str(self.guide)]
        for arguments in ([source] + common,
                          [source, '--to', str(self.docs / 'flat'), '--date', '9 November'] + common,
                          [source, '--to', str(self.docs / 'flat'), '--date', '2026-13-40'] + common,
                          [source, '--to', str(self.docs / 'flat'), '--crop', '1,2,3'] + common,
                          [source, '--to', str(self.docs / 'flat'), '--crop', '0,0,0,10'] + common):
            with self.subTest(arguments[1:4]):
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(tool.main(arguments, io.StringIO()), 2)
                self.assertEqual(self.written(), [])

    def test_guide_data_that_cannot_be_read_is_refused(self):
        (self.guide / 'data' / 'limits.yml').unlink()
        code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat')
        self.assertEqual(code, 2, lines)
        self.assertIn('the guide\'s data cannot be read', lines[0])

    def test_a_missing_pillow_says_what_to_install(self):
        with mock.patch.object(tool, '_load_pillow', side_effect=ImportError('No module named \'PIL\'')):
            code, lines = self.run_tool(self.capture('flat.png', panels(600, 400)), 'assets/images/flat')
        self.assertEqual(code, 2, lines)
        self.assertIn('needs Pillow', lines[0])
        self.assertIn(tool.CAPTURE_REQUIREMENTS, lines[0])
        self.assertEqual(self.written(), [])


class MetadataTests(OptimizeCase):
    def test_the_picture_carries_none_of_the_captures_metadata(self):
        notes = PngImagePlugin.PngInfo()
        notes.add_text('Comment', 'captured on the maintainer\'s desk')
        for name, image in (('flat', panels(600, 400)), ('sky', sky(300, 200))):
            with self.subTest(name):
                source = self.capture(name + '.png', image, pnginfo=notes, dpi=(144, 144))
                self.assertIn(b'maintainer', pathlib.Path(source).read_bytes())
                code, lines = self.run_tool(source, 'assets/images/' + name)
                path = self.picture('assets/images/' + name, code, lines)
                with Image.open(path) as result:
                    self.assertNotIn('Comment', result.info)
                    self.assertNotIn('dpi', result.info)
                self.assertNotIn(b'maintainer', path.read_bytes())

    def test_a_palette_picture_and_a_jpeg_are_read(self):
        for name, image in (('palette.png', panels(600, 400).quantize(16)), ('photo.jpg', sky(600, 400))):
            with self.subTest(name):
                code, lines = self.run_tool(self.capture(name, image), 'assets/images/' + name)
                self.picture('assets/images/' + name.split('.')[0], code, lines)
                self.assertRegex(lines[-1], ROW)


class RepositoryTests(unittest.TestCase):
    def test_the_lock_pins_the_pillow_the_requirement_names_with_hashes(self):
        requirement = (REAL_GUIDE / 'requirements-capture.in').read_text(encoding='utf-8')
        lock = (REAL_GUIDE / 'requirements-capture.txt').read_text(encoding='utf-8')
        pins = re.findall(r'(?m)^(pillow==[0-9.]+)\s*$', requirement)
        self.assertEqual(len(pins), 1, requirement)
        self.assertIn(pins[0] + ' \\\n', lock)
        self.assertEqual(re.findall(r'(?m)^([A-Za-z0-9_.-]+)==', lock), ['pillow'])
        self.assertRegex(lock, r'--hash=sha256:[0-9a-f]{64}')
        self.assertIn('pip-compile --generate-hashes --output-file=docs/user-guide/requirements-capture.txt '
                      'docs/user-guide/requirements-capture.in', lock)

    def test_the_tool_names_the_lock_that_is_there(self):
        self.assertTrue((pathlib.Path(tool.REPO_ROOT) / tool.CAPTURE_REQUIREMENTS).is_file())


if __name__ == '__main__':
    unittest.main()
