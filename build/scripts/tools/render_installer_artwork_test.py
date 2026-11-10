# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for render_installer_artwork.

Run with:
    python build/scripts/tools/render_installer_artwork_test.py

The cases that hold the checked-in pictures and the installer script to each other need nothing beyond Python. The
cases that draw need Pillow and are skipped without it, because no build step installs it. Every case writes only
into its own temporary directory.
"""

from __future__ import annotations

import contextlib
import fnmatch
import io
import os
import re
import shutil
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import render_installer_artwork as artwork  # noqa: E402

try:
    from PIL import Image  # noqa: E402
    MISSING = ''
except ImportError as error:
    MISSING = str(error)


def write_png(path, width, height):
    """The smallest PNG file of a size: one grey row repeated."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    rows = (b'\x00' + b'\x80' * width) * height
    with open(path, 'wb') as handle:
        handle.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


class CheckTests(unittest.TestCase):
    """The checked-in set is held to the sizes the wizard shows."""

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='installer_artwork_')
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        for name, (width, height) in artwork.expected_sizes().items():
            write_png(os.path.join(self.root, name), width, height)

    def test_every_size_present_passes(self):
        self.assertEqual(artwork.check(self.root), [])

    def test_a_missing_size_fails(self):
        os.remove(os.path.join(self.root, 'wizard-panel-336x643.png'))
        self.assertEqual(artwork.check(self.root), ['installer/artwork/wizard-panel-336x643.png: missing'])

    def test_a_picture_of_another_size_than_its_name_fails(self):
        write_png(os.path.join(self.root, 'wizard-mark-97.png'), 96, 96)
        self.assertEqual(artwork.check(self.root),
                         ['installer/artwork/wizard-mark-97.png: is 96x96 where the wizard shows 97x97'])

    def test_a_picture_the_wizard_has_no_area_for_fails(self):
        write_png(os.path.join(self.root, 'wizard-panel-164x314.png'), 164, 314)
        problems = artwork.check(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn('wizard-panel-164x314.png: matches the installer\'s wildcard', problems[0])

    def test_a_file_that_is_no_picture_fails(self):
        with open(os.path.join(self.root, 'wizard-mark-58.png'), 'wb') as handle:
            handle.write(b'not a picture')
        self.assertEqual(artwork.check(self.root), ['installer/artwork/wizard-mark-58.png: not a PNG picture'])

    def test_a_missing_folder_fails(self):
        self.assertEqual(artwork.check(os.path.join(self.root, 'absent')), ['installer/artwork: not found'])

    def test_the_command_reports_and_fails(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(artwork.main(['--check', '--output-dir', self.root]), 0)
            os.remove(os.path.join(self.root, 'wizard-mark-159.png'))
            self.assertEqual(artwork.main(['--check', '--output-dir', self.root]), 1)
        self.assertIn('installer/artwork/wizard-mark-159.png: missing', output.getvalue())


class RepositoryTests(unittest.TestCase):
    """The pictures this repository holds, and the installer script that names them."""

    def test_the_checked_in_pictures_pass(self):
        self.assertEqual(artwork.check(os.path.join(artwork.REPO_ROOT, artwork.ARTWORK_DIR)), [])

    def test_the_installer_names_each_set_by_a_wildcard_that_takes_it_alone(self):
        with open(os.path.join(artwork.REPO_ROOT, 'installer', 'win_installer.iss'), encoding='utf-8-sig') as handle:
            script = handle.read()
        named = {}
        for directive in ('WizardImageFile', 'WizardSmallImageFile'):
            found = re.search(r'^%s=(\S+)$' % directive, script, re.MULTILINE)
            self.assertIsNotNone(found, directive)
            folder, pattern = found.group(1).replace('\\', '/').rsplit('/', 1)
            self.assertEqual(folder, 'artwork')
            named[directive] = sorted(name for name in artwork.expected_sizes() if fnmatch.fnmatch(name, pattern))
        self.assertEqual(named['WizardImageFile'], sorted(artwork.panel_names()))
        self.assertEqual(named['WizardSmallImageFile'], sorted(artwork.square_names()))

    def test_the_panel_keeps_the_shape_of_its_area(self):
        # Inno Setup keeps the panel's area at 164:314 within a pixel of rounding.
        for width, height in artwork.PANEL_SIZES:
            self.assertAlmostEqual(width / height, 164 / 314, delta=0.002, msg='%dx%d' % (width, height))


@unittest.skipIf(MISSING, 'Pillow is not installed: %s' % MISSING)
class RenderTests(unittest.TestCase):
    """What is drawn."""

    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.mkdtemp(prefix='installer_artwork_')
        cls.first = os.path.join(cls.root, 'first')
        cls.written = artwork.render(artwork.REPO_ROOT, cls.first)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def test_every_picture_is_written_at_its_size(self):
        self.assertEqual(sorted(os.path.basename(path) for path in self.written), sorted(artwork.expected_sizes()))
        self.assertEqual(artwork.check(self.first), [])

    def test_drawing_again_gives_the_same_files(self):
        second = os.path.join(self.root, 'second')
        artwork.render(artwork.REPO_ROOT, second)
        for name in artwork.expected_sizes():
            with open(os.path.join(self.first, name), 'rb') as one, open(os.path.join(second, name), 'rb') as two:
                self.assertEqual(one.read(), two.read(), name)

    def test_the_panel_is_opaque_and_the_night_blue_at_its_edges(self):
        for name in artwork.panel_names():
            with Image.open(os.path.join(self.first, name)) as panel:
                self.assertEqual(panel.mode, 'RGB', name)
                red, green, blue = panel.getpixel((0, 0))
                self.assertLess(red, 40, name)
                self.assertLess(green, 50, name)
                self.assertGreater(blue, red, name)

    def test_the_panel_shows_the_name_in_the_logo_cream(self):
        with Image.open(os.path.join(self.first, artwork.PANEL_NAME % artwork.PANEL_SIZES[-1])) as panel:
            width, height = panel.size
            band = panel.crop((0, int(height * 0.45), width, int(height * 0.62)))
            samples = band.tobytes()
        cream = sum(1 for at in range(0, len(samples), 3)
                    if samples[at] > 235 and samples[at + 1] > 230 and samples[at + 2] > 215)
        self.assertGreater(cream, 2000)

    def test_the_mark_has_transparent_corners_and_an_opaque_middle(self):
        for name in artwork.square_names():
            with Image.open(os.path.join(self.first, name)) as mark:
                self.assertEqual(mark.mode, 'RGBA', name)
                side = mark.width
                for corner in ((0, 0), (side - 1, 0), (0, side - 1), (side - 1, side - 1)):
                    self.assertEqual(mark.getpixel(corner)[3], 0, name)
                self.assertEqual(mark.getpixel((side // 2, side // 2))[3], 255, name)
                # The mark fills its square: a twelfth in from the middle of a side is the mark, not a margin.
                self.assertEqual(mark.getpixel((side // 2, side // 12))[3], 255, name)
                self.assertEqual(mark.getpixel((side // 12, side // 2))[3], 255, name)


if __name__ == '__main__':
    unittest.main()
