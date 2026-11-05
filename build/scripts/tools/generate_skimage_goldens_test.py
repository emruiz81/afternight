# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for generate_skimage_goldens.py.

Nothing here imports the imaging library. What is worth testing is the half that runs without it:
the fixture the recording is of, the header it renders, and the check that holds a recorded header
to what this generator would produce. The library's own answers are the one thing a test cannot
assert, which is why they are recorded rather than computed.
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import generate_skimage_goldens as goldens  # noqa: E402


def recorded_header(versions=None, arrays=None):
    """A header the generator could have written, for a check to read.

    Args:
        versions: the versions to record, or None for a complete set.
        arrays: {name: length} to record, or None for the plane and every case.

    Returns:
        The header text.
    """
    if versions is None:
        versions = {'numpy': '2.5.3', 'python': '3.14.7', 'scikit-image': '0.26.0', 'scipy': '1.18.1'}
    length = goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT
    if arrays is None:
        arrays = {'kPlane': length}
        for case in goldens.CASES:
            arrays[goldens.case_identifier(case['id'])] = length
    lines = ['// Generated.', '//', '// Recorded against:']
    for package in sorted(versions):
        lines.append('//   %s %s' % (package, versions[package]))
    lines.append('')
    for name, count in arrays.items():
        lines.append('inline constexpr std::array<float, %du> %s{{' % (count, name))
        lines.append('    0x0p+0f')
        lines.append('}};')
    return '\r\n'.join(lines) + '\r\n'


class FixtureTestCase(unittest.TestCase):
    def test_the_plane_is_as_long_as_its_extent_and_lies_inside_the_unit_interval(self):
        samples = goldens.plane_samples()
        self.assertEqual(len(samples), goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT)
        self.assertTrue(all(0.0 <= value <= 1.0 for value in samples), 'a sample left the unit interval')

    def test_the_plane_is_a_function_of_the_generator_alone(self):
        self.assertEqual(goldens.plane_samples(), goldens.plane_samples())

    def test_the_plane_is_not_constant_so_a_filter_has_something_to_do(self):
        samples = goldens.plane_samples()
        self.assertGreater(max(samples) - min(samples), 0.2)


class HeaderTestCase(unittest.TestCase):
    def test_a_case_identifier_is_pascal_case_with_the_constant_prefix(self):
        self.assertEqual(goldens.case_identifier('unsharp-r1-a25'), 'kUnsharpR1A25')
        self.assertEqual(goldens.case_identifier('chambolle-w005-i50'), 'kChambolleW005I50')

    def test_every_case_identifier_is_distinct(self):
        names = [goldens.case_identifier(case['id']) for case in goldens.CASES]
        self.assertEqual(len(names), len(set(names)))

    def test_a_float_literal_round_trips_exactly(self):
        for value in (0.0, 1.0, 0.1, 2.0 ** -53, 1.0 - 2.0 ** -53):
            literal = goldens.float_literal(value)
            self.assertTrue(literal.endswith('f'))
            self.assertEqual(float.fromhex(literal[:-1]), value)

    def test_the_rendered_header_is_crlf_and_declares_every_case(self):
        outputs = {case['id']: [0.5] * (goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT)
                   for case in goldens.CASES}
        text = goldens.render_header(goldens.plane_samples(), outputs,
                                     {'numpy': '1', 'python': '2', 'scikit-image': '3', 'scipy': '4'})
        self.assertNotIn('\n', text.replace('\r\n', ''), 'the header must be CRLF per AGENTS.md')
        arrays, versions = goldens.read_header(text)
        self.assertEqual(arrays['kPlane'], goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT)
        for case in goldens.CASES:
            self.assertIn(goldens.case_identifier(case['id']), arrays)
        self.assertEqual(sorted(versions), ['numpy', 'python', 'scikit-image', 'scipy'])

    def test_the_rendered_header_states_its_licence_and_copyright(self):
        import check_license_headers  # noqa: E402
        outputs = {case['id']: [0.5] * (goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT) for case in goldens.CASES}
        text = goldens.render_header(goldens.plane_samples(), outputs,
                                     {'numpy': '1', 'python': '2', 'scikit-image': '3', 'scipy': '4'})
        self.assertEqual(check_license_headers.check_text(goldens.HEADER_PATH, text), [])

    def test_the_rendered_header_survives_a_round_trip_through_its_own_reader(self):
        outputs = {case['id']: list(range(goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT))
                   for case in goldens.CASES}
        text = goldens.render_header(goldens.plane_samples(), outputs,
                                     {'numpy': '1', 'python': '2', 'scikit-image': '3', 'scipy': '4'})
        arrays, _versions = goldens.read_header(text)
        self.assertEqual(len(arrays), len(goldens.CASES) + 1)


class CheckTestCase(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix='an-goldens-')
        self.addCleanup(shutil.rmtree, self.root, True)
        self.header = os.path.join(self.root, 'SkimageGoldens.h')

    def write(self, text):
        with io.open(self.header, 'w', encoding='utf-8', newline='') as handle:
            handle.write(text)

    def test_check_accepts_a_complete_recording(self):
        self.write(recorded_header())
        self.assertEqual(goldens.check(self.header), [])

    def test_check_reports_a_missing_recording(self):
        problems = goldens.check(self.header)
        self.assertEqual(len(problems), 1)
        self.assertIn('run `generate`', problems[0])

    def test_check_reports_a_case_with_no_array(self):
        arrays = {'kPlane': goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT}
        for case in goldens.CASES[1:]:
            arrays[goldens.case_identifier(case['id'])] = goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT
        self.write(recorded_header(arrays=arrays))
        problems = goldens.check(self.header)
        self.assertTrue(any(goldens.CASES[0]['id'] in problem and 'no recorded array' in problem
                            for problem in problems), problems)

    def test_check_reports_an_array_that_names_no_case(self):
        arrays = {'kPlane': goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT}
        for case in goldens.CASES:
            arrays[goldens.case_identifier(case['id'])] = goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT
        arrays['kSomethingRetired'] = goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT
        self.write(recorded_header(arrays=arrays))
        problems = goldens.check(self.header)
        self.assertTrue(any('kSomethingRetired' in problem for problem in problems), problems)

    def test_check_reports_an_array_of_the_wrong_length(self):
        arrays = {'kPlane': goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT}
        for index, case in enumerate(goldens.CASES):
            arrays[goldens.case_identifier(case['id'])] = 7 if index == 0 else (
                goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT)
        self.write(recorded_header(arrays=arrays))
        problems = goldens.check(self.header)
        self.assertTrue(any('records 7 samples' in problem for problem in problems), problems)

    def test_check_reports_a_recording_that_does_not_say_what_produced_it(self):
        self.write(recorded_header(versions={'numpy': '2.5.3'}))
        problems = goldens.check(self.header)
        self.assertTrue(any('does not say which scikit-image produced it' in problem
                            for problem in problems), problems)

    def test_check_reports_non_crlf_line_endings(self):
        self.write(recorded_header().replace('\r\n', '\n'))
        problems = goldens.check(self.header)
        self.assertTrue(any('non-CRLF' in problem for problem in problems), problems)


def opening(text):
    """The comment a header opens with, up to the versions it was recorded against."""
    lines = text.split('\r\n')
    return lines[:lines.index('// Recorded against:')]


class RecordedGoldensTestCase(unittest.TestCase):
    def setUp(self):
        self.header = os.path.join(goldens.REPO_ROOT, *goldens.HEADER_PATH.split('/'))
        if not os.path.exists(self.header):
            self.skipTest('the goldens have not been recorded in this checkout')

    def test_the_recording_in_the_repository_passes_its_own_check(self):
        self.assertEqual(goldens.check(self.header), [])

    def test_the_recording_opens_with_the_comment_this_generator_writes(self):
        outputs = {case['id']: [0.5] * (goldens.PLANE_WIDTH * goldens.PLANE_HEIGHT) for case in goldens.CASES}
        rendered = goldens.render_header(goldens.plane_samples(), outputs,
                                         {'numpy': '1', 'python': '2', 'scikit-image': '3', 'scipy': '4'})
        with io.open(self.header, 'r', encoding='utf-8', newline='') as handle:
            recorded = handle.read()
        self.assertEqual(opening(recorded), opening(rendered))


if __name__ == '__main__':
    unittest.main(verbosity=2)
