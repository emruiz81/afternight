#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Draw the pictures the Windows installer shows, at every size Inno Setup asks for.

The setup wizard shows two pictures: a tall panel on the left of its first and last pages, and a small square in the
corner of every page between. Inno Setup scales its windows by the font a display setting uses, so the area each
picture fills has seven sizes between 100% and 250%, and a picture of one size is stretched into the others. Both are
therefore drawn once per size, and installer/win_installer.iss names them by wildcard: setup takes the file that
matches the area it has.

What is drawn comes from the product's own mark, icons/afternight_icon_924x924_blue.png, and from nothing else, so
the installer shows no photograph that can age:

  - The panel is the logo's night blue with a glow behind the mark, a field of faint stars, a few of the logo's
    four-pointed stars, the mark and the name under it. The stars are placed by a generator seeded with a constant,
    in coordinates relative to the panel, so every size shows the same sky.
  - The square is the mark alone, its corners transparent, for the wizard's dark header.

Each picture is drawn several times larger than it is written and reduced with a Lanczos filter. The mark and the name
are cut out of the logo with a mask taken from the logo itself: the mark where the picture is lighter than the
background around it, the name by how light each pixel is, so the name is laid down in one colour over whatever is
behind it.

The pictures are checked in under installer/artwork/ and the installer build reads them from there; this runs only
when the artwork changes. It needs Pillow, which no build step installs:
    python -m pip install pillow

Usage:
    python build/scripts/tools/render_installer_artwork.py [--output-dir installer/artwork]
    python build/scripts/tools/render_installer_artwork.py --check

`--check` needs no Pillow: it reads the header of every checked-in picture and holds the set to the sizes below.
Exits 0 when the pictures were written or the check passed, 1 when the check found a problem, 2 for a wrong command
line, a missing logo or a missing Pillow.

Tests: build/scripts/tools/render_installer_artwork_test.py
"""

from __future__ import annotations

import argparse
import math
import os
import random
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

LOGO = os.path.join('icons', 'afternight_icon_924x924_blue.png')
ARTWORK_DIR = os.path.join('installer', 'artwork')

# The area of the panel and of the square at 100%, 125%, 150%, 175%, 200%, 225% and 250%, as Inno Setup 6.6 and later
# lay the wizard out with its default font and size ("WizardImageFile" and "WizardSmallImageFile" in its help).
PANEL_SIZES = ((202, 386), (269, 515), (336, 643), (403, 772), (430, 824), (498, 953), (534, 1022))
SQUARE_SIZES = (58, 77, 97, 116, 124, 143, 159)

PANEL_NAME = 'wizard-panel-%dx%d.png'
SQUARE_NAME = 'wizard-mark-%d.png'

# The logo's colours: the night blue around the mark, and the cream of its name and stars.
NIGHT = (10, 20, 41)
CREAM = (251, 247, 238)
GLOW = (37, 99, 235)
HORIZON = (96, 165, 250)

# Where the mark and the name are in the logo, in its own pixels, each with room around it for the mask's edge.
MARK_BOX = (140, 58, 784, 668)
NAME_BOX = (60, 690, 864, 900)

# The panel's layout, as fractions of its width (x, sizes) and of its height (y).
MARK_WIDTH = 0.66
MARK_CENTRE_Y = 0.285
NAME_WIDTH = 0.70
NAME_GAP = 0.075
STAR_SEED = 5906
STAR_COUNT = 90
# The logo's four-pointed stars, repeated in the sky below the name: x, y and the length of the long arm.
SPARKLES = ((0.20, 0.655, 0.050), (0.79, 0.735, 0.036), (0.33, 0.865, 0.028), (0.70, 0.925, 0.042),
            (0.86, 0.070, 0.026), (0.12, 0.085, 0.020))


def panel_names():
    return [PANEL_NAME % size for size in PANEL_SIZES]


def square_names():
    return [SQUARE_NAME % size for size in SQUARE_SIZES]


def expected_sizes():
    """{file name: (width, height)} for every picture the installer names."""
    sizes = {PANEL_NAME % size: size for size in PANEL_SIZES}
    sizes.update({SQUARE_NAME % size: (size, size) for size in SQUARE_SIZES})
    return sizes


def png_size(path):
    """The width and height a PNG file states in its header, or None when it is not one."""
    with open(path, 'rb') as handle:
        head = handle.read(24)
    if len(head) < 24 or head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        return None
    return struct.unpack('>II', head[16:24])


def check(artwork_dir):
    """What is wrong with the checked-in pictures: one missing, one of another size, one the installer never asked for."""
    shown = ARTWORK_DIR.replace(os.sep, '/')
    if not os.path.isdir(artwork_dir):
        return ['%s: not found' % shown]
    problems = []
    expected = expected_sizes()
    for name, size in sorted(expected.items()):
        path = os.path.join(artwork_dir, name)
        if not os.path.isfile(path):
            problems.append('%s/%s: missing' % (shown, name))
            continue
        found = png_size(path)
        if found is None:
            problems.append('%s/%s: not a PNG picture' % (shown, name))
        elif tuple(found) != tuple(size):
            problems.append('%s/%s: is %dx%d where the wizard shows %dx%d'
                            % (shown, name, found[0], found[1], size[0], size[1]))
    for name in sorted(os.listdir(artwork_dir)):
        if name.lower().endswith('.png') and name not in expected:
            problems.append('%s/%s: matches the installer\'s wildcard and is no size the wizard shows' % (shown, name))
    return problems


def _mark_cutout(logo):
    """The mark with its rounded corners transparent: opaque where the logo is lighter than the night around it."""
    from PIL import Image, ImageDraw, ImageFilter

    mark = logo.crop(MARK_BOX).convert('RGB')
    blue = mark.getchannel('B').filter(ImageFilter.GaussianBlur(2.5))
    # The night's blue is 41 and the mark's darkest blue 57: halfway is the edge.
    inside = blue.point(lambda value: 255 if value > 49 else 0)
    # Whatever the threshold left dark inside the mark is no corner: only what a fill reaches from outside is.
    outside = inside.copy()
    for corner in ((0, 0), (outside.width - 1, 0), (0, outside.height - 1), (outside.width - 1, outside.height - 1)):
        ImageDraw.floodfill(outside, corner, 128)
    mask = outside.point(lambda value: 0 if value == 128 else 255)
    mask = mask.filter(ImageFilter.GaussianBlur(1.6)).point(lambda value: max(0, min(255, (value - 96) * 4)))
    box = mask.getbbox()
    mark = mark.crop(box)
    mask = mask.crop(box)
    # The cut-out is square in the logo within a few pixels; pad it so every size keeps the mark's shape.
    side = max(mark.size)
    square = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    mark.putalpha(mask)
    square.paste(mark, ((side - mark.width) // 2, (side - mark.height) // 2))
    return square


def _name_cutout(logo):
    """The name as one colour under a mask of how light the logo is there."""
    from PIL import Image

    name = logo.crop(NAME_BOX).convert('L')
    # The night's lightness is about 20 and the letters' about 245.
    mask = name.point(lambda value: max(0, min(255, int((value - 44) * 255 / 180))))
    box = mask.getbbox()
    mask = mask.crop(box)
    cutout = Image.new('RGBA', mask.size, CREAM + (0,))
    cutout.putalpha(mask)
    return cutout


def _radial(size, centre, radius, colour, strength):
    """A glow of one colour, strongest at its centre and gone at its radius."""
    from PIL import Image, ImageFilter

    # Drawn small and enlarged: a glow has no detail to lose, and a per-pixel loop over the full panel is slow.
    width, height = size
    step = 8
    small = Image.new('L', (max(1, width // step), max(1, height // step)), 0)
    pixels = small.load()
    for y in range(small.height):
        for x in range(small.width):
            distance = math.hypot((x + 0.5) * step - centre[0], (y + 0.5) * step - centre[1]) / radius
            if distance < 1.0:
                pixels[x, y] = int(255 * strength * (1.0 - distance) ** 2)
    glow = Image.new('RGBA', size, colour + (0,))
    glow.putalpha(small.resize(size, Image.BILINEAR).filter(ImageFilter.GaussianBlur(step)))
    return glow


def _sparkle(draw, x, y, arm, colour):
    """One of the logo's four-pointed stars: four concave points, the vertical pair the longer."""
    points = []
    waist = arm * 0.21
    for index in range(8):
        angle = index * math.pi / 4
        if index % 2:
            length = waist
        else:
            length = arm if index % 4 == 0 else arm * 0.78
        points.append((x + length * math.sin(angle), y - length * math.cos(angle)))
    draw.polygon(points, fill=colour)


def render_panel(logo, size):
    """The tall panel at one size."""
    from PIL import Image, ImageDraw, ImageFilter

    scale = max(2, round(1100 / size[0]))
    width, height = size[0] * scale, size[1] * scale
    panel = Image.new('RGBA', (width, height), NIGHT + (255,))

    mark_side = int(width * MARK_WIDTH)
    mark_centre = (width // 2, int(height * MARK_CENTRE_Y))
    panel.alpha_composite(_radial((width, height), mark_centre, width * 1.15, GLOW, 0.30))
    panel.alpha_composite(_radial((width, height), (width // 2, int(height * 1.12)), width * 1.10, HORIZON, 0.20))

    mark = _mark_cutout(logo).resize((mark_side, mark_side), Image.LANCZOS)
    name = _name_cutout(logo)
    name_width = int(width * NAME_WIDTH)
    name = name.resize((name_width, max(1, round(name.height * name_width / name.width))), Image.LANCZOS)
    mark_top = mark_centre[1] - mark_side // 2
    name_top = mark_top + mark_side + int(width * NAME_GAP)
    # The stars keep clear of the mark and of the name, with a margin.
    margin = int(width * 0.04)
    kept_clear = ((mark_centre[0] - mark_side // 2 - margin, mark_top - margin,
                   mark_centre[0] + mark_side // 2 + margin, mark_top + mark_side + margin),
                  ((width - name_width) // 2 - margin, name_top - margin,
                   (width + name_width) // 2 + margin, name_top + name.height + margin))

    def is_clear(x, y):
        return not any(left <= x <= right and top <= y <= bottom for left, top, right, bottom in kept_clear)

    sky = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(sky)
    generator = random.Random(STAR_SEED)
    for _ in range(STAR_COUNT):
        x = generator.random() * width
        y = generator.random() * height
        radius = width * (0.0016 + 0.0034 * generator.random() ** 2.2)
        alpha = int(255 * (0.22 + 0.60 * generator.random() ** 1.6))
        tint = generator.choice((CREAM, CREAM, (191, 219, 254), HORIZON))
        if is_clear(x, y):
            draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=tint + (alpha,))
    for x, y, arm in SPARKLES:
        _sparkle(draw, x * width, y * height, arm * width, CREAM + (235,))
    panel.alpha_composite(sky)

    # A soft shadow lifts the mark off the glow behind it.
    shadow = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    shadow_mask = mark.getchannel('A').point(lambda value: value * 110 // 255)
    shadow.paste((4, 9, 20, 255), (mark_centre[0] - mark_side // 2, mark_top + int(width * 0.018)), shadow_mask)
    panel.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(width * 0.025)))
    panel.alpha_composite(mark, (mark_centre[0] - mark_side // 2, mark_top))
    panel.alpha_composite(name, ((width - name_width) // 2, name_top))

    return panel.convert('RGB').resize(size, Image.LANCZOS)


def render_square(logo, side):
    """The mark alone at one size, its corners transparent."""
    from PIL import Image

    return _mark_cutout(logo).resize((side, side), Image.LANCZOS)


def render(repo_root, output_dir):
    """Write every picture into output_dir and return the paths written."""
    from PIL import Image

    logo = Image.open(os.path.join(repo_root, LOGO)).convert('RGB')
    os.makedirs(output_dir, exist_ok=True)
    written = []
    for size in PANEL_SIZES:
        path = os.path.join(output_dir, PANEL_NAME % size)
        render_panel(logo, size).save(path, 'PNG', optimize=True)
        written.append(path)
    for side in SQUARE_SIZES:
        path = os.path.join(output_dir, SQUARE_NAME % side)
        render_square(logo, side).save(path, 'PNG', optimize=True)
        written.append(path)
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description='Draw the Windows installer\'s pictures at every size the wizard '
                                                 'shows them.')
    parser.add_argument('--output-dir', default=None,
                        help='where the pictures are written (default: installer/artwork of this repository)')
    parser.add_argument('--check', action='store_true',
                        help='write nothing: hold the checked-in pictures to the sizes the wizard shows')
    arguments = parser.parse_args(argv)

    if arguments.check:
        problems = check(arguments.output_dir or os.path.join(REPO_ROOT, ARTWORK_DIR))
        for problem in problems:
            print(problem)
        if problems:
            return 1
        print('The installer\'s pictures: %d, each at a size the wizard shows.' % len(expected_sizes()))
        return 0

    if not os.path.isfile(os.path.join(REPO_ROOT, LOGO)):
        print('%s: not found' % LOGO.replace(os.sep, '/'), file=sys.stderr)
        return 2
    try:
        import PIL  # noqa: F401
    except ImportError:
        print('Pillow is needed to draw the pictures: python -m pip install pillow', file=sys.stderr)
        return 2
    for path in render(REPO_ROOT, arguments.output_dir or os.path.join(REPO_ROOT, ARTWORK_DIR)):
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
