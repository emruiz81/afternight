#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Make a captured picture fit for the user guide, and print its row for the media provenance page.

The guide ships inside the installers, and a page that opens with a megabyte of screenshot is slow to open from disk
and heavy to carry: every picture a page shows is bounded in size and in weight by data/limits.yml (`images`), which
rule R11 of check_user_guide.py holds. A window captured with a sky image in it weighs 400 KB to more than a megabyte
as a lossless PNG and a third of that as a WebP no eye tells from it, while a window of sharp lines over flat panels
- a plot's grid, a table - is smaller as a PNG. This takes a capture as it comes off the screen and writes the file a
page can show:

  1. The part `--crop` names, when it names one, in the capture's own pixels.
  2. Its alpha channel dropped, unless the picture has transparent pixels beside opaque ones: a capture is opaque,
     and a channel no pixel uses, or one the capture never wrote, only adds weight. A picture with real transparency
     - the rounded corners of a window - keeps it.
  3. Scaled down, never up, with a Lanczos filter until its longest side is within `images.longest_side`.
  4. Encoded as WebP at quality 90 and as PNG, and the PNG kept when it is the smaller of the two, else the WebP.
  5. When what was kept is over `images.max_bytes`, encoded as WebP again at quality 85, 80 and so on down to 60, and
     the first that fits kept. A picture that fits at none of them is refused and nothing is written: crop it, or
     show less in it.

The file is written where `--to` says, under the guide's docs folder, with the extension of the format that was kept
(`.webp` or `.png`) whatever extension `--to` carries, and with no metadata of the capture. Its row for the media
provenance page (assets/MEDIA_PROVENANCE.md) is printed after it, to paste under the heading of the page's group:
the picture's path as a page's `figure` block finds it recorded, what it shows, the data in it, the day it was
captured with the application's commit, and the SHA-256 of the file.

It needs Pillow beside the documentation toolchain's PyYAML. Pillow is pinned in
docs/user-guide/requirements-capture.txt, which is installed by hand into the documentation environment
(docs/user-guide/.venv, which the guide's build helper makes) of whoever captures, with that environment's Python,
and never in continuous integration:
    python -m pip install --require-hashes -r docs/user-guide/requirements-capture.txt

Usage, with the same Python:
    python build/scripts/tools/optimize_guide_image.py <source image> --to <destination>
        --shows "<what the picture shows>" --data "<the data in it>" [--commit <application commit>]
        [--date YYYY-MM-DD] [--crop LEFT,TOP,WIDTH,HEIGHT] [--guide docs/user-guide]

`--date` is today unless it is given, and `--commit` the short hash of this repository's HEAD: give the commit of the
application that was captured when it is another. Exits 0 when the file was written; 1 when the source is not a
picture this can read or when it fits at no quality; 2 for a wrong command line, a destination outside the guide's
docs folder, guide data that cannot be read, or a missing Pillow or PyYAML.

Tests: build/scripts/tools/optimize_guide_image_test.py
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import io
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CAPTURE_REQUIREMENTS = 'docs/user-guide/requirements-capture.txt'

WEBP_QUALITY = 90
WEBP_QUALITY_STEP = 5
WEBP_QUALITY_FLOOR = 60
# The slowest and smallest of the encoder's methods: a picture is encoded once and read for years.
WEBP_METHOD = 6
# The extensions `--to` may carry, which the format that is kept replaces.
PICTURE_EXTENSIONS = ('.png', '.webp', '.jpg', '.jpeg')
# The modes of a picture of eight bits a channel, with or without a palette or an alpha channel.
EIGHT_BIT_MODES = ('1', 'L', 'LA', 'La', 'P', 'PA', 'RGB', 'RGBA', 'RGBa', 'RGBX', 'CMYK', 'YCbCr')
ALPHA_MODES = ('LA', 'La', 'PA', 'RGBA', 'RGBa')

_DATE = re.compile(r'^\d{4}-\d{2}-\d{2}$')


class Refused(Exception):
    """The picture cannot be prepared. `code` is the exit code: 1 for the picture itself, 2 for how it was asked."""

    def __init__(self, message, code=1):
        super().__init__(message)
        self.code = code


def _load_model():
    sys.path.insert(0, os.path.join(REPO_ROOT, 'docs', 'user-guide', 'hooks'))
    import guide_model  # noqa: E402 - found beside the guide's hooks, needs PyYAML
    return guide_model


def _load_pillow():
    from PIL import Image, ImageOps  # noqa: E402 - pinned in docs/user-guide/requirements-capture.txt
    return Image, ImageOps


def _display(path):
    """The path as the user gave it, relative to the working directory when it lies under it."""
    absolute = os.path.abspath(path)
    try:
        relative = os.path.relpath(absolute)
    except ValueError:
        return absolute.replace('\\', '/')
    return (absolute if relative.startswith('..') else relative).replace('\\', '/')


def _date(text):
    if not _DATE.match(text):
        raise argparse.ArgumentTypeError('%r is not a date written YYYY-MM-DD' % text)
    try:
        return datetime.date.fromisoformat(text).isoformat()
    except ValueError as error:
        raise argparse.ArgumentTypeError('%r is not a date: %s' % (text, error)) from error


def _crop(text):
    parts = text.split(',')
    if len(parts) != 4 or not all(part.strip().isdigit() for part in parts):
        raise argparse.ArgumentTypeError('%r is not LEFT,TOP,WIDTH,HEIGHT in whole pixels' % text)
    left, top, width, height = (int(part) for part in parts)
    if width < 1 or height < 1:
        raise argparse.ArgumentTypeError('%r names a part without a width or a height' % text)
    return left, top, width, height


def _said(text):
    """A text as one cell of a Markdown table row: on one line, a `|` escaped."""
    return ' '.join(text.split()).replace('|', '\\|')


def head_commit(repo):
    """The short hash of the repository's HEAD, or nothing when git does not give one."""
    try:
        result = subprocess.run(['git', '-C', repo, 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True,
                                timeout=30, check=False)
    except (OSError, subprocess.SubprocessError):
        return ''
    return result.stdout.strip() if result.returncode == 0 else ''


def provenance_row(relative, shows, data, date, commit, digest):
    """The picture's row for the media provenance page: five cells, the path and the hash set as code."""
    captured = date + (', AfterNight %s' % commit if commit else '')
    return '| `%s` | %s | %s | %s | `%s` |' % (relative, _said(shows), _said(data), captured, digest)


def destination_of(given, docs):
    """(the file to write, without its extension; its path under the docs folder, without its extension), from what
    `--to` names."""
    absolute = os.path.abspath(given)
    root, extension = os.path.splitext(absolute)
    if extension.lower() in PICTURE_EXTENSIONS:
        absolute = root
    if given.endswith(('/', '\\')) or not os.path.basename(absolute) or os.path.isdir(absolute):
        raise Refused('--to names %s, a folder; it names the picture, with or without an extension' %
                      _display(given), 2)
    try:
        relative = os.path.relpath(absolute, docs).replace('\\', '/')
    except ValueError:
        relative = '../'  # another drive than the docs folder's
    if relative.startswith('../') or relative == '..':
        raise Refused('--to names %s, which is outside the guide\'s docs folder, %s; a page shows only what lies '
                      'under it' % (_display(given), _display(docs)), 2)
    return absolute, relative


def prepared(image, crop, longest_side, modules):
    """The picture as it will be encoded: cropped, its unused alpha dropped, scaled down to the longest side."""
    Image, ImageOps = modules
    if image.mode not in EIGHT_BIT_MODES:
        raise Refused('the picture is of mode %s, not of eight bits a channel; export the capture as an 8-bit PNG' %
                      image.mode)
    image = ImageOps.exif_transpose(image)
    if crop is not None:
        left, top, width, height = crop
        if left + width > image.width or top + height > image.height:
            raise Refused('--crop %d,%d,%d,%d reaches outside the picture, which is %d x %d px' %
                          (left, top, width, height, image.width, image.height), 2)
        image = image.crop((left, top, left + width, top + height))
    if image.mode in ALPHA_MODES or 'transparency' in image.info:
        image = image.convert('RGBA')
        lowest, highest = image.getchannel('A').getextrema()
        # Opaque throughout, or transparent throughout: a channel that says nothing, or one the capture never wrote.
        if lowest == 255 or highest == 0:
            image = image.convert('RGB')
    else:
        image = image.convert('RGB')
    longest = max(image.size)
    if longest > longest_side:
        scale = longest_side / longest
        size = (max(1, min(longest_side, round(image.width * scale))),
                max(1, min(longest_side, round(image.height * scale))))
        image = image.resize(size, Image.Resampling.LANCZOS)
    # Nothing of the capture's metadata - a profile, a resolution, a comment - travels with the picture.
    image.info = {}
    return image


def encoded(image, file_format, quality=None):
    """The picture's bytes as a WebP file of a quality, or as a PNG file."""
    buffer = io.BytesIO()
    if file_format == 'WEBP':
        image.save(buffer, 'WEBP', quality=quality, method=WEBP_METHOD)
    else:
        image.save(buffer, 'PNG', optimize=True)
    return buffer.getvalue()


def lightest(image, max_bytes):
    """(format name, extension, bytes, WebP quality or None) of the file to keep: the smaller of WebP at the usual
    quality and PNG, or the first lower quality of WebP that is within `max_bytes`. Refused when none is."""
    webp = encoded(image, 'WEBP', WEBP_QUALITY)
    png = encoded(image, 'PNG')
    kept = ('PNG', '.png', png, None) if len(png) < len(webp) else ('WebP', '.webp', webp, WEBP_QUALITY)
    if len(kept[2]) <= max_bytes:
        return kept
    smallest = len(kept[2])
    for quality in range(WEBP_QUALITY - WEBP_QUALITY_STEP, WEBP_QUALITY_FLOOR - 1, -WEBP_QUALITY_STEP):
        data = encoded(image, 'WEBP', quality)
        if len(data) <= max_bytes:
            return 'WebP', '.webp', data, quality
        smallest = min(smallest, len(data))
    raise Refused('the picture, %d x %d px, weighs %d KB at its lightest, WebP at quality %d, over the limit of %d KB; '
                  'crop it with --crop, or capture a window that shows less' %
                  (image.width, image.height, (smallest + 1023) // 1024, WEBP_QUALITY_FLOOR, max_bytes // 1024))


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('source', help='the captured picture: a PNG, WebP, JPEG or any file Pillow opens')
    parser.add_argument('--to', required=True, help='where the picture goes, under the guide\'s docs folder, with or '
                        'without an extension; the format that is kept sets the extension')
    parser.add_argument('--shows', required=True, help='what the picture shows, for its provenance row')
    parser.add_argument('--data', required=True, help='the data in the picture, for its provenance row')
    parser.add_argument('--commit', help='the commit of the application that was captured; default this '
                        'repository\'s HEAD')
    parser.add_argument('--date', type=_date, help='the day it was captured, YYYY-MM-DD; default today')
    parser.add_argument('--crop', type=_crop, metavar='LEFT,TOP,WIDTH,HEIGHT', help='the part of the source to keep, '
                        'in its own pixels')
    parser.add_argument('--guide', help='the guide folder that holds mkdocs.yml; default <repo>/docs/user-guide')
    try:
        arguments = parser.parse_args(argv)
    except SystemExit as stop:
        return stop.code if isinstance(stop.code, int) else 2
    stream = stream or sys.stdout
    guide = arguments.guide or os.path.join(REPO_ROOT, 'docs', 'user-guide')

    try:
        model = _load_model()
    except ImportError as error:
        print('optimize_guide_image.py needs PyYAML (%s): run it with the documentation toolchain\'s Python, '
              'docs/user-guide/.venv, which the guide\'s build helper makes' % error, file=stream)
        return 2
    try:
        modules = _load_pillow()
    except ImportError as error:
        print('optimize_guide_image.py needs Pillow (%s): install %s into the documentation toolchain\'s '
              'environment, docs/user-guide/.venv, with `python -m pip install --require-hashes -r %s`' %
              (error, CAPTURE_REQUIREMENTS, CAPTURE_REQUIREMENTS), file=stream)
        return 2
    try:
        limits = model.load_limits(guide)
        docs = model.docs_dir(guide, model.load_config(guide))
    except model.GuideDataError as error:
        print('the guide\'s data cannot be read: %s' % error, file=stream)
        return 2
    if not arguments.shows.strip() or not arguments.data.strip():
        print('--shows and --data say what the picture shows and the data in it; neither is empty', file=stream)
        return 2

    Image = modules[0]
    try:
        stem, relative = destination_of(arguments.to, docs)
        try:
            with Image.open(arguments.source) as opened:
                opened.load()
                source_size = opened.size
                image = prepared(opened, arguments.crop, limits.image_longest_side, modules)
        except (OSError, ValueError, Image.DecompressionBombError) as error:
            raise Refused('%s is not a picture this can read: %s' % (_display(arguments.source), error)) from error
        name, extension, data, quality = lightest(image, limits.image_max_bytes)
    except Refused as refusal:
        print(refusal, file=stream)
        return refusal.code

    written = stem + extension
    os.makedirs(os.path.dirname(written), exist_ok=True)
    with open(written, 'wb') as handle:
        handle.write(data)
    said = '%s: %s, %d x %d px' % (_display(written), name, image.width, image.height)
    if image.size != source_size:
        said += ' (from %d x %d)' % source_size
    said += ', %d bytes' % len(data)
    if quality is not None and quality != WEBP_QUALITY:
        said += ', quality %d' % quality
    print(said, file=stream)
    for other in ('.webp', '.png'):
        if other != extension and os.path.isfile(stem + other):
            print('note: %s is there as well, from another run; delete it unless a page shows it' %
                  _display(stem + other), file=stream)
    commit = arguments.commit.strip() if arguments.commit is not None else head_commit(REPO_ROOT)
    date = arguments.date or datetime.date.today().isoformat()
    print(provenance_row(relative + extension, arguments.shows, arguments.data, date, commit,
                         hashlib.sha256(data).hexdigest()), file=stream)
    return 0


if __name__ == '__main__':
    sys.exit(main())
