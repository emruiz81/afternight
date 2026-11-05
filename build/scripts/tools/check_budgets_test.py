# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_budgets.

Run with:
    python build/scripts/tools/check_budgets_test.py

The include half is measured out of synthetic source, so those cases are exact and need no compiler.
The compile half is tested with the compiler replaced by a stub, because what needs proving is which
numbers count as a breach - not that a compiler compiles. The last case reads the repository's own
budget file, so a ceiling that has quietly been crossed fails here too.
"""

from __future__ import annotations

import io
import json
import os
import pathlib
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_budgets as budgets  # noqa: E402
import check_include_layers as layers  # noqa: E402


class RawBackendCompileTest(unittest.TestCase):
    def test_private_raw_package_changes_only_raw_implementation_measurements(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            unit = 'src/AfterNightEngine/src/io/raw/RawBackend.cpp'
            self.assertEqual(budgets.raw_backend_defines(root, unit), ())
            header = root / 'vcpkg/installed/x64-windows-raw/include/afternight-libraw/libraw/libraw.h'
            header.parent.mkdir(parents=True); header.touch()
            with patch.dict(os.environ, {'AFTERNIGHT_RAWSPEED': '1'}):
                self.assertEqual(budgets.raw_backend_defines(root, unit), ('AFTERNIGHT_HAS_RAWSPEED',))
                self.assertEqual(budgets.raw_backend_defines(root, 'src/io/fits/FitsCodec.cpp'), ())
                self.assertEqual(budgets.raw_backend_defines(root, 'src/AfterNightTests/Test_EnginePublicHeaders/SelfCompile/IORawCodec.cpp'), ())
            with patch.dict(os.environ, {'AFTERNIGHT_RAWSPEED': '0'}):
                self.assertEqual(budgets.raw_backend_defines(root, unit), ())


LIMITS = {
    'maxSiblingIncludesPerPublicHeader': 2,
    'maxStandardIncludesPerPublicHeader': 2,
    'maxStandardIncludesAcrossSurface': 3,
}

ALLOWANCES = {'wallTimeAllowance': 1.5, 'preprocessedLineAllowance': 1.25}


def baseline(unit, milliseconds, lines):
    return {'units': {unit: {'minMilliseconds': milliseconds, 'preprocessedLines': lines}}}


class IncludeWeightCase(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-budget-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)

    def public_header(self, below, includes=()):
        path = self.root / layers.PUBLIC_DIR / below
        path.parent.mkdir(parents=True, exist_ok=True)
        body = '#pragma once\n'
        for include in includes:
            body += '#include <%s>\n' % include
        path.write_text(body, encoding='utf-8')

    def measure(self):
        return budgets.measure_includes(self.root)


class TransitiveIncludeTests(IncludeWeightCase):
    def test_a_header_that_includes_nothing_weighs_nothing(self):
        self.public_header('Core/Api.h')
        measured = self.measure()
        self.assertEqual(measured['perHeader']['AfterNightEngine/Core/Api.h'],
                         {'sibling': 0, 'standard': 0, 'external': 0})

    def test_weight_is_transitive_rather_than_direct(self):
        self.public_header('Core/Api.h', ['cstdint'])
        self.public_header('Core/Version.h', ['AfterNightEngine/Core/Api.h'])
        self.public_header('Runtime/Context.h', ['AfterNightEngine/Core/Version.h', 'vector'])
        counts = self.measure()['perHeader']['AfterNightEngine/Runtime/Context.h']
        self.assertEqual(counts['sibling'], 2)
        self.assertEqual(counts['standard'], 2)

    def test_a_header_reached_twice_is_counted_once(self):
        self.public_header('Core/Api.h', ['cstdint'])
        self.public_header('Core/Version.h', ['AfterNightEngine/Core/Api.h', 'cstdint'])
        self.public_header('Runtime/Context.h', ['AfterNightEngine/Core/Version.h',
                                                 'AfterNightEngine/Core/Api.h'])
        counts = self.measure()['perHeader']['AfterNightEngine/Runtime/Context.h']
        self.assertEqual(counts['sibling'], 2)
        self.assertEqual(counts['standard'], 1)

    def test_the_surface_total_is_the_union_rather_than_the_sum(self):
        self.public_header('Core/Api.h', ['cstdint'])
        self.public_header('Core/Version.h', ['cstdint'])
        self.public_header('Runtime/Context.h', ['vector'])
        measured = self.measure()
        self.assertEqual(measured['standardAcrossSurface'], 2)
        self.assertEqual(measured['publicHeaders'], 3)

    def test_a_cycle_between_two_headers_terminates(self):
        self.public_header('Core/Api.h', ['AfterNightEngine/Core/Version.h'])
        self.public_header('Core/Version.h', ['AfterNightEngine/Core/Api.h'])
        counts = self.measure()['perHeader']['AfterNightEngine/Core/Api.h']
        self.assertEqual(counts['sibling'], 1)


class IncludeBudgetTests(IncludeWeightCase):
    def test_a_surface_within_its_budget_passes(self):
        self.public_header('Core/Api.h', ['cstdint'])
        self.assertEqual(budgets.check_includes(self.measure(), LIMITS), [])

    def test_too_many_public_headers_in_one_header_is_a_breach(self):
        self.public_header('Core/A.h')
        self.public_header('Core/B.h')
        self.public_header('Core/C.h')
        self.public_header('Core/D.h', ['AfterNightEngine/Core/A.h',
                                        'AfterNightEngine/Core/B.h',
                                        'AfterNightEngine/Core/C.h'])
        problems = budgets.check_includes(self.measure(), LIMITS)
        self.assertTrue(any('pulls in 3 public headers' in problem for problem in problems),
                        problems)

    def test_too_many_standard_headers_in_one_header_is_a_breach(self):
        self.public_header('Core/Api.h', ['cstdint', 'vector', 'string'])
        problems = budgets.check_includes(self.measure(), LIMITS)
        self.assertTrue(any('pulls in 3 standard headers' in problem for problem in problems),
                        problems)

    def test_too_many_standard_headers_across_the_surface_is_a_breach(self):
        self.public_header('Core/A.h', ['cstdint', 'vector'])
        self.public_header('Core/B.h', ['string', 'memory'])
        problems = budgets.check_includes(self.measure(), LIMITS)
        self.assertTrue(any('across the surface' not in problem and 'public surface' in problem
                            for problem in problems), problems)

    def test_a_header_outside_the_standard_library_and_this_engine_is_a_breach(self):
        self.public_header('Core/Api.h', ['opencv2/core.hpp'])
        problems = budgets.check_includes(self.measure(), LIMITS)
        self.assertTrue(any('neither public nor standard' in problem for problem in problems),
                        problems)


class CompileBudgetTests(unittest.TestCase):
    def test_a_measurement_inside_both_allowances_passes(self):
        measurement = budgets.Measurement('Unit.cpp', 110.0, 1000)
        self.assertEqual(
            budgets.check_compile([measurement], baseline('Unit.cpp', 100.0, 1000), ALLOWANCES), [])

    def test_wall_time_beyond_its_allowance_is_a_breach(self):
        measurement = budgets.Measurement('Unit.cpp', 160.0, 1000)
        problems = budgets.check_compile([measurement], baseline('Unit.cpp', 100.0, 1000),
                                         ALLOWANCES)
        self.assertTrue(any('took 160 ms' in problem for problem in problems), problems)

    def test_preprocessed_size_beyond_its_allowance_is_a_breach(self):
        measurement = budgets.Measurement('Unit.cpp', 100.0, 1300)
        problems = budgets.check_compile([measurement], baseline('Unit.cpp', 100.0, 1000),
                                         ALLOWANCES)
        self.assertTrue(any('preprocesses to 1300 lines' in problem for problem in problems),
                        problems)

    def test_a_tighter_line_allowance_catches_growth_a_time_allowance_would_absorb(self):
        """The reason the two allowances differ: 1.3x of header weight is inside the timing noise."""
        measurement = budgets.Measurement('Unit.cpp', 130.0, 1300)
        problems = budgets.check_compile([measurement], baseline('Unit.cpp', 100.0, 1000),
                                         ALLOWANCES)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('preprocesses', problems[0])

    def test_a_unit_with_no_baseline_is_reported_rather_than_passed(self):
        measurement = budgets.Measurement('New.cpp', 100.0, 1000)
        problems = budgets.check_compile([measurement], baseline('Unit.cpp', 100.0, 1000),
                                         ALLOWANCES)
        self.assertTrue(any('no recorded baseline' in problem for problem in problems), problems)


ISA_ALLOWANCES = {"objectSizeAllowance": 1.20, "wallTimeAllowance": 1.75}


def isa_baseline(unit, label, object_bytes, milliseconds):
    return {"variants": {budgets.isa_variant_key(unit, label):
                         {"objectBytes": object_bytes, "minMilliseconds": milliseconds}}}


class IsaBudgetTests(unittest.TestCase):
    """The per-instruction-set half: what one more compiled target costs in object bytes.

    Object size is the number template and instruction-set bloat shows up in, and it is only
    meaningful at the optimization level that ships - so these variants are measured with it, and
    separately from the header-weight numbers next door.
    """

    def test_a_variant_inside_both_allowances_passes(self):
        measured = [budgets.IsaMeasurement("U.cpp", "ladder", 100.0, 11000)]
        self.assertEqual(
            budgets.check_isa(measured, isa_baseline("U.cpp", "ladder", 10000, 100.0),
                              ISA_ALLOWANCES), [])

    def test_object_growth_beyond_its_allowance_is_a_breach(self):
        measured = [budgets.IsaMeasurement("U.cpp", "ladder", 100.0, 13000)]
        problems = budgets.check_isa(measured, isa_baseline("U.cpp", "ladder", 10000, 100.0),
                                     ISA_ALLOWANCES)
        self.assertTrue(any("13000 bytes" in problem for problem in problems), problems)

    def test_compile_time_growth_beyond_its_allowance_is_a_breach(self):
        measured = [budgets.IsaMeasurement("U.cpp", "ladder", 200.0, 10000)]
        problems = budgets.check_isa(measured, isa_baseline("U.cpp", "ladder", 10000, 100.0),
                                     ISA_ALLOWANCES)
        self.assertTrue(any("took 200 ms" in problem for problem in problems), problems)

    def test_a_variant_with_no_baseline_is_reported_rather_than_passed(self):
        measured = [budgets.IsaMeasurement("U.cpp", "floor", 100.0, 10000)]
        problems = budgets.check_isa(measured, isa_baseline("U.cpp", "ladder", 10000, 100.0),
                                     ISA_ALLOWANCES)
        self.assertTrue(any("no recorded baseline" in problem for problem in problems), problems)

    def test_the_report_names_what_the_extra_target_costs(self):
        stream = io.StringIO()
        budgets.report_isa([budgets.IsaMeasurement("U.cpp", "floor", 90.0, 10000),
                            budgets.IsaMeasurement("U.cpp", "ladder", 140.0, 17000)], stream)
        text = stream.getvalue()
        self.assertIn("floor", text)
        self.assertIn("ladder", text)
        self.assertIn("7,000", text)

    def test_a_variant_command_carries_its_defines_and_its_optimization(self):
        clang = budgets.compile_command("clang++", pathlib.Path("a.cpp"), pathlib.Path("a.o"),
                                        ["include"], extra_flags=["-O2", "-mavx2"],
                                        defines=["HWY_DISABLED_TARGETS=~HWY_AVX2"])
        self.assertIn("-O2", clang)
        self.assertIn("-mavx2", clang)
        self.assertIn("-DHWY_DISABLED_TARGETS=~HWY_AVX2", clang)
        msvc = budgets.compile_command("cl", pathlib.Path("a.cpp"), pathlib.Path("a.obj"),
                                       ["include"], extra_flags=["/O2", "/arch:AVX2"],
                                       defines=["HWY_DISABLED_TARGETS=~HWY_AVX2"])
        self.assertIn("/O2", msvc)
        self.assertIn("/arch:AVX2", msvc)
        self.assertIn("/DHWY_DISABLED_TARGETS=~HWY_AVX2", msvc)

    def test_every_declared_isa_unit_exists_in_the_tree(self):
        """A declared unit that has been renamed would otherwise stop being measured silently."""
        document = budgets.load_budget(budgets.REPO_ROOT)
        for entry in document["isa"]["units"]:
            self.assertTrue((budgets.REPO_ROOT / entry["unit"]).is_file(), entry["unit"])
            self.assertGreaterEqual(len(entry["variants"]), 2, entry["unit"])

class CommandTests(unittest.TestCase):
    def test_a_clang_command_names_the_dialect_and_the_include_roots(self):
        command = budgets.compile_command('clang++', pathlib.Path('a.cpp'), pathlib.Path('a.o'),
                                          ['include', 'src'])
        self.assertIn('-std=c++20', command)
        self.assertIn('include', command)
        self.assertIn('src', command)
        self.assertIn('-c', command)

    def test_an_msvc_command_uses_msvc_spellings(self):
        command = budgets.compile_command('cl', pathlib.Path('a.cpp'), pathlib.Path('a.obj'),
                                          ['include'])
        self.assertIn('/std:c++20', command)
        self.assertIn('/Iinclude', command)
        self.assertIn('/c', command)

    def test_the_preprocess_command_produces_text_rather_than_an_object(self):
        self.assertIn('-E', budgets.preprocess_command('clang++', pathlib.Path('a.cpp'), []))
        self.assertIn('/EP', budgets.preprocess_command('cl', pathlib.Path('a.cpp'), []))

    def test_an_explicit_compiler_that_does_not_exist_is_not_silently_replaced(self):
        self.assertIsNone(budgets.find_compiler('no-such-compiler-anywhere', environment={}))

    def test_the_environment_names_the_compiler_when_no_argument_does(self):
        self.assertEqual(budgets.find_compiler(None, environment={'CXX': sys.executable}),
                         sys.executable)


class BudgetFileTests(unittest.TestCase):
    def test_recording_adds_new_units_without_refreshing_retained_evidence(self):
        existing = {'compiler': 'same compiler',
                    'units': {'Old.cpp': {'minMilliseconds': 10.0, 'preprocessedLines': 100}}}
        recorded, added = budgets.record_compile_baseline(
            existing, 'same compiler', [budgets.Measurement('Old.cpp', 99.0, 999),
                                        budgets.Measurement('New.cpp', 20.0, 200)])
        self.assertEqual(added, 1)
        self.assertEqual(recorded['units']['Old.cpp'], existing['units']['Old.cpp'])
        self.assertEqual(recorded['units']['New.cpp'],
                         {'minMilliseconds': 20.0, 'preprocessedLines': 200})

    def test_rebaseline_is_the_explicit_way_to_replace_retained_evidence(self):
        existing = {'compiler': 'old compiler',
                    'units': {'Old.cpp': {'minMilliseconds': 10.0, 'preprocessedLines': 100}}}
        recorded, added = budgets.record_compile_baseline(
            existing, 'new compiler', [budgets.Measurement('Old.cpp', 25.0, 250)], rebaseline=True)
        self.assertEqual(added, 1)
        self.assertEqual(recorded, {'compiler': 'new compiler',
                                    'units': {'Old.cpp': {'minMilliseconds': 25.0,
                                                          'preprocessedLines': 250}}})

    def test_recording_refuses_to_mix_measurements_from_two_compilers(self):
        existing = {'compiler': 'old compiler', 'variants': {}}
        with self.assertRaisesRegex(ValueError, '--rebaseline'):
            budgets.record_isa_baseline(existing, 'new compiler', [], rebaseline=False)

    def test_the_budget_file_is_written_with_the_repositorys_line_endings(self):
        root = pathlib.Path(tempfile.mkdtemp(prefix='an-budget-file-'))
        self.addCleanup(shutil.rmtree, root, True)
        (root / 'docs/dev').mkdir(parents=True)
        budgets.save_budget(root, {'schema': 1, 'includes': {}, 'compile': {}})
        raw = (root / budgets.BUDGET_FILE).read_bytes()
        self.assertNotIn(b'\n', raw.replace(b'\r\n', b''))
        self.assertEqual(json.loads(raw.decode('utf-8'))['schema'], 1)

    def test_a_budget_file_of_another_schema_is_refused(self):
        root = pathlib.Path(tempfile.mkdtemp(prefix='an-budget-file-'))
        self.addCleanup(shutil.rmtree, root, True)
        (root / 'docs/dev').mkdir(parents=True)
        (root / budgets.BUDGET_FILE).write_text('{"schema": 99}', encoding='utf-8')
        with self.assertRaises(ValueError):
            budgets.load_budget(root)


class RepositoryTests(unittest.TestCase):
    def test_the_repository_is_inside_its_include_budget_when_explicitly_requested(self):
        stream = io.StringIO()
        code = budgets.main(['check', '--includes-only'], stream=stream,
                            environment={'CXX': 'no-such-compiler'})
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn('include-only mode', stream.getvalue())

    def test_a_normal_check_fails_when_no_compiler_can_measure_compile_budgets(self):
        stream = io.StringIO()
        code = budgets.main(['check'], stream=stream, environment={'CXX': 'no-such-compiler'})
        self.assertEqual(code, 1, stream.getvalue())
        self.assertIn('no C++ compiler was found', stream.getvalue())

    def test_the_recorded_measurement_matches_the_tree(self):
        """A recorded number that has drifted from the tree is a stale baseline, not a budget."""
        document = budgets.load_budget(budgets.REPO_ROOT)
        self.assertEqual(document['includes']['measured'],
                         budgets.measure_includes(budgets.REPO_ROOT))


if __name__ == '__main__':
    unittest.main(verbosity=2)
