# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_safety_register.

Run with:
    python build/scripts/tools/check_safety_register_test.py

Most cases state a register, a provenance register and a test tree of their own in a temporary
directory, so a rule is exercised by the smallest input that breaks it. The last cases read the
repository's own files, because a checker that only ever passes on synthetic input proves nothing
about the register it exists to guard.
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_safety_register as safety  # noqa: E402


PROVENANCE = """\
library zlib
    version     1.3.2
library libtiff
    version     4.7.2 on Windows; the distribution's libtiff-dev on Linux
"""

SUITE = """\
TEST(DecoderTest, RefusesATruncatedStrip)
{
}
TEST_F(ContainerTest, RefusesAnUnlistedDemuxer)
{
}
"""

SCRIPT_TEST = """\
class Cases(unittest.TestCase):
    def test_a_reading_is_refused(self):
        pass
"""


def review(name, version, advisories='none', binaries='z.dll'):
    return ('review %s\n'
            '    version     %s\n'
            '    binaries    %s\n'
            '    reviewed    2026-09-23\n'
            '    source      the NVD\n'
            '    advisories  %s\n'
            '    outcome     nothing applies\n' % (name, version, binaries, advisories))


def finding(identifier, severity='medium', status='repaired', component='engine', advisories='none',
            evidence='test DecoderTest.RefusesATruncatedStrip', record='docs/record.md'):
    return ('finding %s\n'
            '    title       a finding\n'
            '    severity    %s\n'
            '    component   %s\n'
            '    found       a review, 2026-09-23\n'
            '    status      %s\n'
            '    advisories  %s\n'
            '    evidence    %s\n'
            '    record      %s\n' % (identifier, severity, component, status, advisories, evidence, record))


BASE = review('zlib', '1.3.2') + review('libtiff', '4.7.2', binaries='tiff.dll')


class RegisterCase(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(self._remove, self.root)
        self.write('build/scripts/tools/dependencies.txt', PROVENANCE)
        self.write('src/AfterNightTests/Test_EngineImage/DecoderTest.cpp', SUITE)
        self.write('build/scripts/tools/probe_test.py', SCRIPT_TEST)
        self.write('docs/record.md', '# record\n')

    @staticmethod
    def _remove(root):
        for path in sorted(root.rglob('*'), reverse=True):
            if path.is_dir():
                path.rmdir()
            else:
                # Git writes its objects read-only, and Windows does not unlink a read-only file.
                path.chmod(0o600)
                path.unlink()
        root.rmdir()

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def problems(self, register):
        self.write('build/scripts/tools/safety_register.txt', register)
        problems, _ = safety.check(self.root)
        return problems

    def assertFinds(self, register, fragment):
        problems = self.problems(register)
        self.assertTrue(any(fragment in problem for problem in problems),
                        'expected %r among %r' % (fragment, problems))

    def assertClean(self, register):
        self.assertEqual(self.problems(register), [])


class SchemaTests(RegisterCase):
    def test_a_complete_register_is_clean(self):
        self.assertClean(BASE + finding('F01'))

    def test_a_missing_field_is_named(self):
        self.assertFinds(BASE + finding('F01').replace('    title       a finding\n', ''), 'missing title')

    def test_an_unknown_field_is_named(self):
        self.assertFinds(BASE + finding('F01') + '    colour      red\n', 'colour is not a field')

    def test_a_severity_outside_the_scale_is_refused(self):
        self.assertFinds(BASE + finding('F01', severity='serious'), 'severity')

    def test_a_status_outside_the_set_is_refused(self):
        self.assertFinds(BASE + finding('F01', status='fixed'), 'status')

    def test_a_record_declared_twice_is_refused(self):
        self.assertFinds(BASE + finding('F01') + finding('F01'), 'declared twice')

    def test_a_date_that_is_not_one_is_refused(self):
        self.assertFinds(BASE.replace('2026-09-23', 'yesterday', 1) + finding('F01'), 'reviewed')

    def test_a_record_that_does_not_exist_is_refused(self):
        self.assertFinds(BASE + finding('F01', record='docs/missing.md'), 'docs/missing.md')

    def test_a_record_kept_at_the_archive_tag_is_not_looked_for_in_the_tree(self):
        self.assertClean(BASE + finding('F01', record=safety.ARCHIVE_TAG + ':docs/run/README.md#repairs'))

    def test_an_archive_citation_is_held_to_the_tag_where_the_tag_exists(self):
        if shutil.which('git') is None:
            self.skipTest('git is not on PATH')
        git = ['git', '-C', str(self.root), '-c', 'user.name=t', '-c', 'user.email=t@example.test']
        subprocess.run(git + ['init', '-q'], check=True)
        self.write('docs/run/README.md', '# run\n')
        subprocess.run(git + ['add', '-A'], check=True)
        subprocess.run(git + ['commit', '-q', '-m', 'record'], check=True)
        subprocess.run(git + ['tag', safety.ARCHIVE_TAG], check=True)
        self.assertClean(BASE + finding('F01', record=safety.ARCHIVE_TAG + ':docs/run/README.md'))
        self.assertFinds(BASE + finding('F01', record=safety.ARCHIVE_TAG + ':docs/run/MISSING.md'),
                         'docs/run/MISSING.md')

    def test_a_line_that_is_neither_header_nor_field_is_refused(self):
        self.assertFinds(BASE + 'stray words\n', 'neither a record header nor a field')


class SeverityTests(RegisterCase):
    def test_an_open_high_finding_is_unresolved(self):
        self.assertFinds(BASE + finding('F01', severity='high', status='open', evidence='the maintainer owns it'),
                         'unresolved')

    def test_an_accepted_critical_finding_is_unresolved(self):
        self.assertFinds(BASE + finding('F01', severity='critical', status='accepted',
                                        evidence='the maintainer owns it'), 'unresolved')

    def test_an_open_medium_finding_is_recorded_rather_than_refused(self):
        self.assertClean(BASE + finding('F01', severity='medium', status='open', evidence='the maintainer owns it'))

    def test_a_high_finding_that_does_not_apply_passes_with_its_reason(self):
        self.assertClean(BASE + finding('F01', severity='high', status='not-applicable',
                                        evidence='the command-line tool is not built'))

    def test_a_repaired_high_finding_passes_with_its_test(self):
        self.assertClean(BASE + finding('F01', severity='high'))


class EvidenceTests(RegisterCase):
    def test_a_repair_must_name_a_test(self):
        self.assertFinds(BASE + finding('F01', evidence='commit abc'), 'names no test')

    def test_a_repair_naming_a_test_nobody_wrote_is_refused(self):
        self.assertFinds(BASE + finding('F01', evidence='test DecoderTest.RefusesEverything'),
                         'DecoderTest.RefusesEverything')

    def test_a_fixture_test_is_found(self):
        self.assertClean(BASE + finding('F01', evidence='test ContainerTest.RefusesAnUnlistedDemuxer'))

    def test_a_script_test_is_found(self):
        self.assertClean(BASE + finding('F01', evidence='test probe_test.py::test_a_reading_is_refused'))

    def test_a_script_test_nobody_wrote_is_refused(self):
        self.assertFinds(BASE + finding('F01', evidence='test probe_test.py::test_nothing'),
                         'probe_test.py::test_nothing')

    def test_a_non_applicability_needs_a_reason(self):
        self.assertFinds(BASE + finding('F01', status='not-applicable', evidence='none'), 'reason')

    def test_a_repair_that_is_a_release_names_the_pinned_one(self):
        self.assertClean(BASE + finding('F01', component='dependency libtiff', evidence='release libtiff 4.7.2'))

    def test_a_release_other_than_the_pinned_one_is_refused(self):
        self.assertFinds(BASE + finding('F01', component='dependency libtiff', evidence='release libtiff 4.7.3'),
                         'not the one pinned')

    def test_a_release_of_a_library_nobody_reviewed_is_refused(self):
        self.assertFinds(BASE + finding('F01', evidence='release libpng 1.6.58'), 'no review reads libpng')


class AdvisoryTests(RegisterCase):
    def test_an_advisory_a_review_lists_must_be_answered(self):
        register = review('zlib', '1.3.2', advisories='CVE-2026-0001') + review('libtiff', '4.7.2')
        self.assertFinds(register, 'CVE-2026-0001')

    def test_an_answered_advisory_is_clean(self):
        register = (review('zlib', '1.3.2', advisories='CVE-2026-0001') + review('libtiff', '4.7.2')
                    + finding('F01', component='dependency zlib', advisories='CVE-2026-0001'))
        self.assertClean(register)

    def test_an_advisory_answered_twice_is_refused(self):
        register = (review('zlib', '1.3.2', advisories='CVE-2026-0001') + review('libtiff', '4.7.2')
                    + finding('F01', component='dependency zlib', advisories='CVE-2026-0001')
                    + finding('F02', component='dependency zlib', advisories='CVE-2026-0001'))
        self.assertFinds(register, 'answered by 2')

    def test_a_finding_answering_an_advisory_no_review_lists_is_refused(self):
        register = BASE + finding('F01', component='dependency zlib', advisories='CVE-2026-0009')
        self.assertFinds(register, 'CVE-2026-0009')

    def test_an_advisory_answered_under_another_library_is_refused(self):
        register = (review('zlib', '1.3.2', advisories='CVE-2026-0001') + review('libtiff', '4.7.2')
                    + finding('F01', component='dependency libtiff', advisories='CVE-2026-0001'))
        self.assertFinds(register, 'CVE-2026-0001')

    def test_a_dependency_finding_names_a_reviewed_library(self):
        self.assertFinds(BASE + finding('F01', component='dependency libpng'), 'libpng')


class ProvenanceTests(RegisterCase):
    def test_a_registered_library_without_a_review_is_refused(self):
        self.assertFinds(review('zlib', '1.3.2'), 'libtiff')

    def test_a_review_of_another_version_is_stale(self):
        self.assertFinds(review('zlib', '1.3.1') + review('libtiff', '4.7.2'), 'stale')

    def test_a_version_is_matched_as_a_whole_token(self):
        self.assertFinds(review('zlib', '1.3') + review('libtiff', '4.7.2'), 'stale')

    def test_a_closure_library_outside_the_provenance_register_names_its_binaries(self):
        self.assertFinds(BASE + review('brotli', '1.2.0', binaries='none'), 'brotli')

    def test_a_closure_library_with_its_binaries_is_clean(self):
        self.assertClean(BASE + review('brotli', '1.2.0', binaries='brotlidec.dll'))


class ClosureTests(unittest.TestCase):
    REGISTER = review('zlib', '1.3.2', binaries='z.dll') + review('tiff', '4.7.2', binaries='tiff.dll, libtiff.so.6')

    def closure_problems(self, closure, portable_executable=True):
        records, problems = safety.parse_register(self.REGISTER)
        self.assertEqual(problems, [])
        return safety.check_closure(records, closure, portable_executable)

    def test_a_reviewed_closure_is_clean(self):
        closure = {'AfterNightEngine.dll': ['tiff.dll', 'KERNEL32.dll'], 'tiff.dll': ['z.dll'], 'z.dll': []}
        self.assertEqual(self.closure_problems(closure), [])

    def test_a_module_nobody_reviewed_is_refused(self):
        closure = {'AfterNightEngine.dll': ['tiff.dll', 'lzma.dll'], 'tiff.dll': ['z.dll'], 'z.dll': [],
                   'lzma.dll': []}
        problems = self.closure_problems(closure)
        self.assertTrue(any('lzma.dll' in problem for problem in problems), problems)

    def test_a_reviewed_binary_the_closure_no_longer_loads_is_stale(self):
        closure = {'AfterNightEngine.dll': ['tiff.dll'], 'tiff.dll': []}
        problems = self.closure_problems(closure)
        self.assertTrue(any('z.dll' in problem and 'do not load' in problem for problem in problems), problems)

    def test_the_other_platforms_names_are_not_expected_here(self):
        closure = {'AfterNightEngine.dll': ['tiff.dll'], 'tiff.dll': ['z.dll'], 'z.dll': []}
        self.assertEqual(self.closure_problems(closure), [])

    def test_the_engines_own_binaries_need_no_review(self):
        closure = {'AfterNightEngineGmicAdapter.dll': ['AfterNightEngine.dll', 'tiff.dll'],
                   'AfterNightEngine.dll': ['tiff.dll'], 'tiff.dll': ['z.dll'], 'z.dll': []}
        self.assertEqual(self.closure_problems(closure), [])

    def test_a_reading_that_opened_nothing_says_so_rather_than_passing(self):
        with tempfile.TemporaryDirectory() as directory:
            problems, reading = safety.read_closure(directory, tool=None)
        self.assertTrue(problems)
        self.assertEqual(reading, {})


class RepositoryTests(unittest.TestCase):
    def test_the_repository_passes(self):
        stream = io.StringIO()
        code = safety.main([], stream=stream)
        self.assertEqual(code, 0, stream.getvalue())

    def test_every_registered_library_is_reviewed(self):
        records, problems = safety.parse_register(
            (safety.REPO_ROOT / safety.REGISTER).read_text(encoding='utf-8'))
        self.assertEqual(problems, [])
        reviewed = {record.name for record in records if record.kind == 'review'}
        self.assertGreaterEqual(len(reviewed), 25)


if __name__ == '__main__':
    unittest.main()
