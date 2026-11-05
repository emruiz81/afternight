# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Record the imaging library's own answers for the operations this engine computes itself.

Two operations scikit-image offers - an unsharp mask and total-variation denoising - have a native
implementation in this engine, and the question a differential answers is whether the native one is
the same operation or merely a similar one. The library cannot be linked into a C++ suite - it is a
Python package - so its answers are recorded once, by this script, into a generated header the
parity suite compiles.

That is the same arrangement the raw-decoder corpus already uses, and it has the same two
properties: the suite needs nothing installed to run, and the recording is reproducible by anyone
who has the library, because the generator is in the repository beside its output.

Usage:
    python build/scripts/tools/generate_skimage_goldens.py generate [--python <interpreter>]
    python build/scripts/tools/generate_skimage_goldens.py check

`generate` re-runs the library and rewrites the header. It imports the library, so it must be run
with an interpreter that has it; `--python` re-executes this script under one, which is how the
repository's own bundled interpreter is used from a shell whose `python` is a different one.

`check` needs no library and no interpreter but this one. It reads the recorded header and holds it
to what this script would produce structurally: every case in the registry recorded, every recorded
case in the registry, every array as long as its plane, and a library version stated for each.
It does not re-run the library, because a check that silently needs an optional package is a check
that is quietly skipped.

Tests: build/scripts/tools/generate_skimage_goldens_test.py
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

HEADER_PATH = 'src/AfterNightTests/Test_EngineLibraryParity/SkimageGoldens.h'

# The interpreter that carries the library in this repository. It is a path rather than a name
# because the shell's `python` is a different installation and does not have the package.
BUNDLED_PYTHON = 'python/python/tools_msvc/python.exe'

# The one plane every case reads. Small on purpose: the header is compiled by a test suite, and a
# larger plane would buy nothing the border and the interior do not already cover at this size.
PLANE_WIDTH = 24
PLANE_HEIGHT = 18

# The fixture's own generator, written out rather than taken from a library so that the recorded
# input is a function of these lines alone.
PLANE_SEED = 20260920


def plane_samples():
    """The fixture plane: a smooth gradient, two blobs, and a reproducible speckle over both.

    Returns:
        A list of float values in [0, 1], row-major, PLANE_WIDTH * PLANE_HEIGHT long.
    """
    import math

    samples = []
    state = PLANE_SEED
    for y in range(PLANE_HEIGHT):
        for x in range(PLANE_WIDTH):
            # One multiplicative congruential step per sample, so the speckle is a function of the
            # coordinate order and nothing else.
            state = (state * 1103515245 + 12345) & 0x7FFFFFFF
            speckle = (state / float(0x7FFFFFFF)) - 0.5
            gradient = 0.25 + 0.5 * (x / float(PLANE_WIDTH - 1))
            first = 0.3 * math.exp(-(((x - 7.0) ** 2 + (y - 6.0) ** 2) / 12.0))
            second = -0.2 * math.exp(-(((x - 17.0) ** 2 + (y - 12.0) ** 2) / 20.0))
            value = gradient + first + second + 0.06 * speckle
            samples.append(min(max(value, 0.0), 1.0))
    return samples


# Every case the header records: an identifier, the library call, and its keyword arguments.
#
# The unsharp cases are the ones whose native implementation is meant to be the same operation: the
# same Gaussian truncation rounding, the same reflecting border, the same clip. The total-variation
# cases run with a zero tolerance so that both sides take exactly the stated number of iterations,
# which is what isolates the arithmetic from the stopping rule.
CASES = (
    {'id': 'unsharp-r1-a1', 'operation': 'unsharp_mask', 'parameters': {'radius': 1.0, 'amount': 1.0}},
    {'id': 'unsharp-r1-a25', 'operation': 'unsharp_mask', 'parameters': {'radius': 1.0, 'amount': 2.5}},
    {'id': 'unsharp-r2-a1', 'operation': 'unsharp_mask', 'parameters': {'radius': 2.0, 'amount': 1.0}},
    {'id': 'unsharp-r2-a05', 'operation': 'unsharp_mask', 'parameters': {'radius': 2.0, 'amount': 0.5}},
    {'id': 'chambolle-w005-i50', 'operation': 'denoise_tv_chambolle',
     'parameters': {'weight': 0.05, 'eps': 0.0, 'max_num_iter': 50}},
    {'id': 'chambolle-w02-i50', 'operation': 'denoise_tv_chambolle',
     'parameters': {'weight': 0.2, 'eps': 0.0, 'max_num_iter': 50}},
    {'id': 'chambolle-w005-i200', 'operation': 'denoise_tv_chambolle',
     'parameters': {'weight': 0.05, 'eps': 0.0, 'max_num_iter': 200}},
)


def case_identifier(case_id):
    """The C++ identifier a case's array is named with.

    Args:
        case_id: the case's own identifier.

    Returns:
        The identifier, in the PascalCase the engine's naming table requires.
    """
    return 'k' + ''.join(part.capitalize() for part in case_id.replace('_', '-').split('-'))


def float_literal(value):
    """One float as a C++ literal that round-trips exactly.

    Args:
        value: the value.

    Returns:
        A hexadecimal float literal, which no decimal rounding can change.
    """
    return float(value).hex().replace('0x', '0x').replace('p', 'p') + 'f'


def format_array(name, values, documentation):
    """One documented constexpr array of float samples.

    Args:
        name: the C++ identifier.
        values: the samples.
        documentation: the one-line comment above it.

    Returns:
        The lines of the declaration, without a trailing blank line.
    """
    lines = ['/// %s' % documentation,
             'inline constexpr std::array<float, %du> %s{{' % (len(values), name)]
    row = []
    for index, value in enumerate(values):
        row.append(float_literal(value))
        if len(row) == 4 or index + 1 == len(values):
            lines.append('    ' + ', '.join(row) + ('' if index + 1 == len(values) else ','))
            row = []
    lines.append('}};')
    return lines


def render_header(samples, outputs, versions):
    """The whole generated header.

    Args:
        samples: the fixture plane.
        outputs: a mapping from case identifier to that case's recorded samples.
        versions: a mapping from package name to version string.

    Returns:
        The header text, with CRLF line endings.
    """
    lines = [
        '// SPDX-License-Identifier: GPL-3.0-or-later',
        '// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz',
        '',
        '// Generated by build/scripts/tools/generate_skimage_goldens.py. Do not edit by hand.',
        '//',
        '// The repository formatter is asked to leave this file alone: what follows is one array of',
        '// hex float literals per case, and reflowing them changes nothing a reader of it wants.',
        '// clang-format off',
        '//',
        '// The imaging library is a Python package and cannot be linked into this suite, so its answers for',
        '// the operations this engine computes itself are recorded here and compiled. The generator is in',
        '// the repository beside this file: re-running it with an',
        '// interpreter that has the library rewrites these numbers, and a difference is a difference in',
        '// the library rather than in the recording.',
        '//',
        '// Recorded against:',
    ]
    for package in sorted(versions):
        lines.append('//   %s %s' % (package, versions[package]))
    lines.extend([
        '',
        '#pragma once',
        '',
        '#include <array>',
        '#include <cstdint>',
        '',
        'namespace parity_test::skimage_goldens',
        '{',
        '',
        '/// Samples per row of the one plane every case reads.',
        'inline constexpr std::uint32_t kPlaneWidth = %du;' % PLANE_WIDTH,
        '',
        '/// Rows of the one plane every case reads.',
        'inline constexpr std::uint32_t kPlaneHeight = %du;' % PLANE_HEIGHT,
        '',
    ])
    lines.extend(format_array('kPlane', samples,
                              'The fixture the library and this engine were both given.'))
    for case in CASES:
        lines.append('')
        arguments = ', '.join('%s=%s' % (key, case['parameters'][key]) for key in sorted(case['parameters']))
        lines.extend(format_array(case_identifier(case['id']), outputs[case['id']],
                                  "The library's `%s(%s)`." % (case['operation'], arguments)))
    lines.extend(['', '} // namespace parity_test::skimage_goldens', ''])
    return '\r\n'.join(lines)


def library_outputs():
    """Run the library over the fixture, once per case.

    Returns:
        A pair of the outputs by case identifier and the versions of the packages involved.

    Raises:
        ImportError: when the interpreter running this does not have the library.
    """
    import numpy
    import scipy
    import skimage
    from skimage.filters import unsharp_mask
    from skimage.restoration import denoise_tv_chambolle

    plane = numpy.asarray(plane_samples(), dtype=numpy.float32).reshape(PLANE_HEIGHT, PLANE_WIDTH)
    calls = {'unsharp_mask': unsharp_mask, 'denoise_tv_chambolle': denoise_tv_chambolle}
    outputs = {}
    for case in CASES:
        answer = calls[case['operation']](plane, **case['parameters'])
        outputs[case['id']] = [float(value) for value in numpy.asarray(answer, dtype=numpy.float32).ravel()]
    versions = {'numpy': numpy.__version__, 'python': sys.version.split()[0],
                'scikit-image': skimage.__version__, 'scipy': scipy.__version__}
    return outputs, versions


_ARRAY_RE = re.compile(r'inline constexpr std::array<float, (\d+)u> (\w+)\{\{')
_VERSION_RE = re.compile(r'^//   (\S+) (\S+)$')


def read_header(text):
    """What a recorded header declares.

    Args:
        text: the header's contents.

    Returns:
        A pair of a mapping from array name to declared length and a mapping of recorded versions.
    """
    arrays = {name: int(length) for length, name in _ARRAY_RE.findall(text)}
    versions = {}
    for line in text.splitlines():
        found = _VERSION_RE.match(line)
        if found is not None:
            versions[found.group(1)] = found.group(2)
    return arrays, versions


def check(header_path):
    """Return one message per problem with the recorded header; empty means it is usable.

    Args:
        header_path: where the header is.

    Returns:
        A list of messages.
    """
    problems = []
    if not os.path.exists(header_path):
        return ['recorded goldens not found: %s (run `generate`)' % header_path]
    with open(header_path, 'r', encoding='utf-8', newline='') as handle:
        text = handle.read()
    if '\n' in text.replace('\r\n', ''):
        problems.append('recorded goldens have non-CRLF line endings; AGENTS.md requires CRLF')

    arrays, versions = read_header(text)
    expected_length = PLANE_WIDTH * PLANE_HEIGHT
    if arrays.get('kPlane') != expected_length:
        problems.append('the fixture plane is %s samples, expected %d'
                        % (arrays.get('kPlane'), expected_length))
    for case in CASES:
        name = case_identifier(case['id'])
        if name not in arrays:
            problems.append('case %s has no recorded array %s' % (case['id'], name))
        elif arrays[name] != expected_length:
            problems.append('case %s records %d samples, expected %d' % (case['id'], arrays[name],
                                                                         expected_length))
    known = {case_identifier(case['id']) for case in CASES} | {'kPlane'}
    for name in sorted(set(arrays) - known):
        problems.append('recorded array %s names no case this generator produces' % name)
    for package in ('numpy', 'python', 'scikit-image', 'scipy'):
        if package not in versions:
            problems.append('the recording does not say which %s produced it' % package)
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('command', choices=('generate', 'check'))
    parser.add_argument('--header', default=os.path.join(REPO_ROOT, *HEADER_PATH.split('/')))
    parser.add_argument('--python', default=None,
                        help='re-run `generate` under this interpreter, for one that has the library')
    args = parser.parse_args(argv)

    if args.command == 'check':
        problems = check(args.header)
        for problem in problems:
            print(problem)
        if problems:
            print('%d problem(s); the recorded goldens are not usable.' % len(problems))
            return 1
        print('recorded goldens are complete: %d case(s) over one %dx%d plane.'
              % (len(CASES), PLANE_WIDTH, PLANE_HEIGHT))
        return 0

    if args.python is not None:
        # A path given relative to the repository is resolved against it, because that is how the
        # bundled interpreter is written down everywhere else.
        interpreter = args.python
        if not os.path.isabs(interpreter):
            candidate = os.path.join(REPO_ROOT, *interpreter.split('/'))
            if os.path.exists(candidate):
                interpreter = candidate
        command = [interpreter, os.path.abspath(__file__), 'generate', '--header', args.header]
        return subprocess.call(command, cwd=REPO_ROOT)

    bundled = os.path.join(REPO_ROOT, *BUNDLED_PYTHON.split('/'))
    try:
        outputs, versions = library_outputs()
    except ImportError as failure:
        print('this interpreter does not have the imaging library: %s' % failure)
        if os.path.exists(bundled):
            print('re-run with --python %s' % bundled)
        return 1

    text = render_header(plane_samples(), outputs, versions)
    with open(args.header, 'w', encoding='utf-8', newline='') as handle:
        handle.write(text)
    print('wrote %s: %d case(s) over one %dx%d plane, from scikit-image %s'
          % (args.header, len(CASES), PLANE_WIDTH, PLANE_HEIGHT, versions['scikit-image']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
