# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for the Linux target-select build script.

Run with:
    python build/scripts/tools/build_afternightengine_test.py

The script decides what to rebuild from a record it writes beside the binaries, because an
instrumented generation and an ordinary one share an output directory and a timestamp cannot tell
them apart. These cases drive the real script with a `make` that records its arguments instead of
building anything, so what is asserted is the decision rather than a copy of it.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / 'build' / 'scripts' / 'linux' / 'build_afternightengine.sh'

FUZZ_TARGETS = ('Fuzz_EngineDescriptor', 'Fuzz_EngineConversion', 'Fuzz_EnginePixelMath',
                'Fuzz_EngineMetadata', 'Fuzz_EngineCodec', 'Fuzz_EngineSequence')

FAKE_MAKE = """#!/bin/bash
# Records the arguments it was called with, in the directory it was called from, and builds nothing.
{
    printf '%s' "$PWD"
    for argument in "$@"; do printf '\t%s' "$argument"; done
    printf '\n'
} >> "$MAKE_CALL_LOG"
"""

FAKE_NPROC = '#!/bin/bash\necho 1\n'


@unittest.skipIf(sys.platform.startswith('win'), 'the script under test is the Linux half of the pair')
class RebuildDecisionTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-target-select-'))
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        scripts = self.root / 'build' / 'scripts' / 'linux'
        scripts.mkdir(parents=True)
        shutil.copy2(SCRIPT, scripts / SCRIPT.name)
        self.tests = self.root / 'build' / 'make-linux' / 'tests'
        self.tests.mkdir(parents=True)
        self.binaries = self.root / 'bin' / 'clang' / 'release'
        self.binaries.mkdir(parents=True)
        self.fake = self.root / 'fake'
        self.fake.mkdir()
        for name, text in (('make', FAKE_MAKE), ('nproc', FAKE_NPROC)):
            path = self.fake / name
            path.write_text(text, encoding='utf-8')
            path.chmod(0o755)
        self.log = self.root / 'make-calls.tsv'

    def generate(self, sanitizer='', fuzzer=False):
        """Writes the two generated makefiles the script reads its decision out of."""
        engine = '-fsanitize=%s ' % sanitizer if sanitizer else ''
        (self.root / 'build' / 'make-linux' / 'AfterNightEngine.make').write_text(
            'ALL_CXXFLAGS += -m64 -O3 %s-std=c++20\n' % engine, encoding='utf-8')
        fuzz = '-fsanitize=fuzzer,address,undefined ' if fuzzer else ''
        for target in FUZZ_TARGETS:
            (self.tests / ('%s.make' % target)).write_text(
                'ALL_CXXFLAGS += -m64 -O3 %s-std=c++20\n' % fuzz, encoding='utf-8')

    def record(self, value):
        (self.binaries / '.engine-instrumentation').write_text('%s\n' % value, encoding='utf-8')

    def recorded(self):
        return (self.binaries / '.engine-instrumentation').read_text(encoding='utf-8').strip()

    def run_script(self, *arguments):
        environment = dict(os.environ)
        environment['PATH'] = '%s%s%s' % (self.fake, os.pathsep, environment.get('PATH', ''))
        environment['MAKE_CALL_LOG'] = str(self.log)
        completed = subprocess.run(
            ['bash', str(self.root / 'build' / 'scripts' / 'linux' / SCRIPT.name), *arguments],
            capture_output=True, text=True, env=environment, check=False)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        calls = {}
        for line in self.log.read_text(encoding='utf-8').splitlines() if self.log.is_file() else []:
            fields = line.split('\t')
            calls[fields[-1]] = fields[1:-1]
        self.log.unlink(missing_ok=True)
        return completed.stdout + completed.stderr, calls

    def assertForced(self, calls, target):
        self.assertIn(target, calls, 'the script never built %s' % target)
        self.assertIn('-B', calls[target], '%s was left to its timestamps' % target)

    def assertNotForced(self, calls, target):
        self.assertIn(target, calls, 'the script never built %s' % target)
        self.assertNotIn('-B', calls[target], '%s was rebuilt for no stated change' % target)

    def test_a_tree_generated_for_libfuzzer_forces_the_fuzz_targets(self):
        # The defect this guards: the engine's own flags do not change with AFTERNIGHT_FUZZ, so
        # nothing in the tree looked different, make found every binary newer than its sources, and
        # the campaign ran the standalone driver it had been told was replaced.
        self.generate(fuzzer=True)
        self.record('none')
        output, calls = self.run_script('release', '--with-tests')
        for target in FUZZ_TARGETS:
            self.assertForced(calls, target)
        self.assertNotForced(calls, 'AfterNightEngine')
        self.assertNotForced(calls, 'Test_EngineImage')
        self.assertIn('Fuzz instrumentation changed', output)
        self.assertEqual(self.recorded(), 'none+fuzzer')

    def test_the_same_generation_twice_rebuilds_nothing(self):
        self.generate(fuzzer=True)
        self.record('none+fuzzer')
        output, calls = self.run_script('release', '--with-tests')
        for target in (*FUZZ_TARGETS, 'AfterNightEngine', 'Test_EngineImage'):
            self.assertNotForced(calls, target)
        self.assertNotIn('changed', output)
        self.assertEqual(self.recorded(), 'none+fuzzer')

    def test_returning_to_an_ordinary_tree_forces_the_fuzz_targets_back(self):
        self.generate(fuzzer=False)
        self.record('none+fuzzer')
        _, calls = self.run_script('release', '--with-tests')
        for target in FUZZ_TARGETS:
            self.assertForced(calls, target)
        self.assertNotForced(calls, 'AfterNightEngine')
        self.assertEqual(self.recorded(), 'none')

    def test_a_sanitizer_change_still_forces_every_target(self):
        self.generate(sanitizer='address')
        self.record('none')
        _, calls = self.run_script('release', '--with-tests')
        for target in (*FUZZ_TARGETS, 'AfterNightEngine', 'Test_EngineImage'):
            self.assertForced(calls, target)
        self.assertEqual(self.recorded(), 'address')

    def test_a_libfuzzer_generation_forces_every_target_and_records_both_halves(self):
        # The libraries carry the generation's coverage counters, so an engine an ordinary build left in
        # place would leave libFuzzer as blind as before; and the generation's flags name the address
        # sanitizer as well, which must not read as the address generation.
        self.generate(sanitizer='fuzzer-no-link,address,undefined', fuzzer=True)
        self.record('none')
        _, calls = self.run_script('release', '--with-tests')
        for target in (*FUZZ_TARGETS, 'AfterNightEngine', 'Test_EngineImage'):
            self.assertForced(calls, target)
        self.assertEqual(self.recorded(), 'fuzzer+fuzzer')

    def test_a_build_without_the_suites_leaves_the_previous_fuzz_claim_standing(self):
        # Nothing here built a fuzz target, so claiming one instrumentation or the other would make
        # the next build with the suites trust a record no invocation earned.
        self.generate(fuzzer=True)
        self.record('none+fuzzer')
        _, calls = self.run_script('release')
        for target in FUZZ_TARGETS:
            self.assertNotIn(target, calls)
        self.assertEqual(self.recorded(), 'none+fuzzer')

    def test_a_thread_tree_still_leaves_the_allocation_replacing_suite_out(self):
        self.generate(sanitizer='thread')
        self.record('none')
        output, calls = self.run_script('release', '--with-tests')
        self.assertNotIn('Test_EngineCore', calls)
        self.assertIn('Skipping Test_EngineCore', output)
        for target in FUZZ_TARGETS:
            self.assertForced(calls, target)
        self.assertEqual(self.recorded(), 'thread')


class ExecutableTests(unittest.TestCase):
    """Every Linux build script runs as the guides spell it, `./<script>`, so the repository records it executable. A
    script a Windows checkout writes anew - a rename writes one - is recorded without the bit and is refused there."""

    @unittest.skipUnless(shutil.which('git') and (REPO_ROOT / '.git').exists(), 'needs the repository and git')
    def test_every_linux_script_is_recorded_executable(self):
        listed = subprocess.run(['git', '-C', str(REPO_ROOT), 'ls-files', '--stage', '--', 'build/scripts/linux/*.sh'],
                                capture_output=True, text=True, check=True).stdout.splitlines()
        self.assertTrue(listed, 'git lists no Linux build script')
        self.assertEqual([line.split('\t', 1)[1] for line in listed if not line.startswith('100755 ')], [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
