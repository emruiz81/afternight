# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_dependency_provenance.

Run with:
    python build/scripts/tools/check_dependency_provenance_test.py

Most cases build a throwaway tree with a register of their own, because what is being tested is
whether each rule fires, and a rule that only ever sees a passing repository proves nothing. The
shape of each case is the same: write a register and the tree it claims, break one thing, and say
which sentence comes back.

The last cases read the repository itself, because the register exists to describe this tree and a
checker that agreed with a synthetic one would still be free to disagree with the real one.
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_dependency_provenance as provenance  # noqa: E402


LIBRARY = """\
library onetbb
    role        The parallelism library.
    kind        packaged
    linkage     shared
    version     2023.1.0
    source      vcpkg port `tbb[hwloc]`
    upstream    https://github.com/uxlfoundation/oneTBB
    license     Apache-2.0
    security    Upstream releases.
    build       build/scripts/msvc/02-install_msvc_deps.bat
    deploy      build/scripts/msvc/04a-deploy_deps_release.bat copies tbb12.dll
    artifacts   tbb12.dll
    boundary    include group `onetbb`, allowed to the runtime layer only
    targets     AfterNightEngine
    links       none
    reported    compiled=yes loaded=yes
"""

PORTED = """\
ported demosaic-rawtherapee
    role        One demosaic method.
    files       src/AfterNightEngine/src/kernels/reference/DemosaicKernelsFast.cpp
    marker      RawTherapee
    upstream    RawTherapee
    author      Emil Martinec
    license     GPL-3.0-or-later
    relation    adapted
    divergence  Owned here.
    record      docs/dev/guides/DEPENDENCIES.md, and the module comment of each file
"""


class RegisterCase(unittest.TestCase):
    """A throwaway repository holding exactly what one rule needs."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.write('build/scripts/msvc/02-install_msvc_deps.bat', 'vcpkg install tbb[hwloc] --triplet=x64-windows\n')
        self.write('build/scripts/msvc/04a-deploy_deps_release.bat', 'xcopy /Y ..\\vcpkg\\bin\\tbb12.dll ..\\bin\\\n')
        self.write('docs/dev/guides/DEPENDENCIES.md', '# Dependencies\n')
        self.write('src/AfterNightEngine/src/kernels/reference/DemosaicKernelsFast.cpp',
                   '// Derived from the fast demosaic of RawTherapee, copyright 2008-2010 Emil Martinec, GPL-3.\n')
        self.write('src/AfterNightEngine/src/runtime/DependencyReport.cpp',
                   'void reportOneTbb(DependencyVersion& entry)\n{\n    entry.name = "onetbb";\n}\n')
        self.write('src/AfterNightTests/Test_EngineCore/DependencyReportTest.cpp',
                   'constexpr std::array<Expectation, 1u> kExpected{{\n    {"onetbb", true, true},\n}};\n')

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def register(self, text):
        self.write(provenance.REGISTER, text)

    def findings(self, text=None):
        if text is not None:
            self.register(text)
        records, problems = provenance.parse_register(
            (self.root / provenance.REGISTER).read_text(encoding='utf-8'))
        provenance.check_schema(records, problems)
        if problems:
            return problems
        provenance.check_build_and_deploy(str(self.root), records, problems)
        provenance.check_engine_report(str(self.root), records, problems)
        provenance.check_ported(str(self.root), records, problems)
        return problems


class SchemaCase(RegisterCase):
    """The shape of a record, before anything is compared against the tree."""

    def test_a_complete_register_has_nothing_to_say(self):
        self.assertEqual(self.findings(LIBRARY + '\n' + PORTED), [])

    def test_a_missing_field_is_named(self):
        text = LIBRARY.replace('    license     Apache-2.0\n', '')
        self.assertTrue(any('missing license' in problem for problem in self.findings(text + '\n' + PORTED)))

    def test_an_empty_field_is_the_same_as_a_missing_one(self):
        text = LIBRARY.replace('    license     Apache-2.0\n', '    license     \n')
        self.assertTrue(any('missing license' in problem for problem in self.findings(text + '\n' + PORTED)))

    def test_an_unknown_field_is_refused(self):
        text = LIBRARY + '    notes       something\n'
        self.assertTrue(any('not a field of a library record' in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_a_kind_outside_the_set_is_refused(self):
        text = LIBRARY.replace('    kind        packaged\n', '    kind        borrowed\n')
        self.assertTrue(any("kind 'borrowed' is not one of" in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_a_relation_outside_the_set_is_refused(self):
        text = PORTED.replace('    relation    adapted\n', '    relation    copied\n')
        self.assertTrue(any("relation 'copied' is not one of" in problem
                            for problem in self.findings(LIBRARY + '\n' + text)))

    def test_a_reported_field_that_is_not_the_shape_is_refused(self):
        text = LIBRARY.replace('    reported    compiled=yes loaded=yes\n', '    reported    both\n')
        self.assertTrue(any('is not compiled=' in problem for problem in self.findings(text + '\n' + PORTED)))

    def test_a_record_declared_twice_is_refused(self):
        self.assertTrue(any('declared twice' in problem
                            for problem in self.findings(LIBRARY + '\n' + LIBRARY + '\n' + PORTED)))

    def test_a_field_stated_twice_is_refused(self):
        text = LIBRARY + '    kind        vendored\n'
        self.assertTrue(any('states kind twice' in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_a_carried_port_without_a_marker_is_refused(self):
        text = PORTED.replace('    marker      RawTherapee\n', '    marker      none\n')
        self.assertTrue(any('names the phrase that keeps it' in problem
                            for problem in self.findings(LIBRARY + '\n' + text)))

    def test_a_not_carried_port_with_a_marker_is_refused(self):
        text = PORTED.replace('    relation    adapted\n', '    relation    not-carried\n')
        self.assertTrue(any('has no attribution to keep' in problem
                            for problem in self.findings(LIBRARY + '\n' + text)))

    def test_a_field_value_continues_onto_the_next_line(self):
        text = LIBRARY.replace('    license     Apache-2.0\n', '    license     Apache-2.0\n                and nothing else\n')
        records, problems = provenance.parse_register(text)
        self.assertEqual(problems, [])
        self.assertEqual(records[0].fields['license'], 'Apache-2.0 and nothing else')


class BuildAndDeployCase(RegisterCase):
    """Whether the scripts a record names exist and do what it says they do."""

    def test_a_build_script_that_does_not_exist_is_named(self):
        text = LIBRARY.replace('build/scripts/msvc/02-install_msvc_deps.bat',
                               'build/scripts/msvc/99-nothing.bat')
        self.assertTrue(any('which does not exist' in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_a_port_no_named_script_installs_is_named(self):
        text = LIBRARY.replace('`tbb[hwloc]`', '`tbb[nothing]`')
        self.assertTrue(any('installs `tbb[nothing]`' in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_a_packaged_record_that_names_no_port_is_refused(self):
        text = LIBRARY.replace('vcpkg port `tbb[hwloc]`', 'a package manager')
        self.assertTrue(any('names the port it comes from in backticks' in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_an_artifact_no_named_script_deploys_is_named(self):
        text = LIBRARY.replace('    artifacts   tbb12.dll\n', '    artifacts   tbb13.dll\n')
        self.assertTrue(any('deploys tbb13.dll' in problem
                            for problem in self.findings(text + '\n' + PORTED)))

    def test_nothing_deployed_needs_no_deploy_script(self):
        text = LIBRARY.replace('    artifacts   tbb12.dll\n', '    artifacts   none\n')
        text = text.replace('    deploy      build/scripts/msvc/04a-deploy_deps_release.bat copies tbb12.dll\n',
                            '    deploy      nothing to deploy: header-only\n')
        self.assertEqual(self.findings(text + '\n' + PORTED), [])

    def test_a_glob_is_matched_the_way_the_script_writes_it(self):
        self.write('build/scripts/msvc/04a-deploy_deps_release.bat',
                   'xcopy /Y /E ..\\..\\..\\3rdparty\\bin\\msvc\\release\\*.dll ..\\bin\\\n')
        text = LIBRARY.replace('    artifacts   tbb12.dll\n',
                               '    artifacts   3rdparty/bin/msvc/release/*.dll\n')
        self.assertEqual(self.findings(text + '\n' + PORTED), [])


COMMIT = 'ceca3adcfc06b66face78dd22a1422cc3ccd85cd'


class FetchedCase(RegisterCase):
    """A library fetched from its upstream's published release and built by scripts of this repository."""

    def setUp(self):
        super().setUp()
        script = ('set "REPOSITORY=https://github.com/example/library.git"\nset "TAG=v1.1.0"\nset "COMMIT=%s"\n'
                  % COMMIT)
        self.write('build/scripts/msvc/_build_library.bat', script)
        self.write('build/scripts/linux/_build_library.sh', script.replace('set "', '').replace('"', ''))
        self.write('build/scripts/msvc/04a-deploy_deps_release.bat',
                   'xcopy /Y ..\\..\\..\\vcpkg\\bin\\tbb12.dll ..\\bin\\\nxcopy /Y library-1.1.dll ..\\bin\\\n')

    def record(self, source=None, build=None):
        return ('library library\n'
                '    role        A library.\n'
                '    kind        fetched\n'
                '    linkage     shared\n'
                '    version     1.1.0\n'
                '    source      ' + (source or 'release `example/library@v1.1.0`, commit ' + COMMIT) + '\n'
                '    upstream    https://github.com/example/library\n'
                '    license     Apache-2.0\n'
                '    security    Upstream releases.\n'
                '    build       ' + (build or 'build/scripts/msvc/_build_library.bat, '
                                              'build/scripts/linux/_build_library.sh') + '\n'
                '    deploy      build/scripts/msvc/04a-deploy_deps_release.bat\n'
                '    artifacts   library-1.1.dll\n'
                '    boundary    none\n'
                '    targets     AfterNightEngine\n'
                '    links       none\n')

    def test_a_release_every_build_script_names_has_nothing_to_say(self):
        self.assertEqual(self.findings(LIBRARY + '\n' + self.record() + '\n' + PORTED), [])

    def test_a_fetched_record_names_its_release_and_its_commit(self):
        for source in ('release `example/library` at its latest tag', 'release `example/library@v1.1.0`'):
            problems = self.findings(LIBRARY + '\n' + self.record(source=source) + '\n' + PORTED)
            self.assertTrue(any('names the release it is fetched from' in problem for problem in problems),
                            (source, problems))

    def test_a_build_script_that_fetches_another_release_is_named(self):
        self.write('build/scripts/linux/_build_library.sh',
                   'REPOSITORY=https://github.com/example/library.git\nTAG=v1.0.1\nCOMMIT=%s\n' % COMMIT)
        problems = self.findings(LIBRARY + '\n' + self.record() + '\n' + PORTED)
        self.assertTrue(any('_build_library.sh does not fetch v1.1.0' in problem for problem in problems), problems)

    def test_a_build_script_that_pins_another_commit_is_named(self):
        self.write('build/scripts/msvc/_build_library.bat',
                   'set "REPOSITORY=https://github.com/example/library.git"\nset "TAG=v1.1.0"\nset "COMMIT=%s"\n'
                   % ('0' * 40))
        problems = self.findings(LIBRARY + '\n' + self.record() + '\n' + PORTED)
        self.assertTrue(any('_build_library.bat does not pin %s' % COMMIT in problem for problem in problems),
                        problems)

    def test_a_build_script_that_fetches_another_repository_is_named(self):
        self.write('build/scripts/linux/_build_library.sh',
                   'REPOSITORY=https://github.com/other/library.git\nTAG=v1.1.0\nCOMMIT=%s\n' % COMMIT)
        problems = self.findings(LIBRARY + '\n' + self.record() + '\n' + PORTED)
        self.assertTrue(any('_build_library.sh does not fetch example/library' in problem for problem in problems),
                        problems)


class EngineReportCase(RegisterCase):
    """Whether the register, the engine's report and the suite agree."""

    def test_a_library_the_engine_reports_and_the_register_does_not_is_named(self):
        self.write('src/AfterNightEngine/src/runtime/DependencyReport.cpp',
                   'entry.name = "onetbb";\nentry.name = "libtiff";\n')
        self.assertTrue(any('the engine reports libtiff' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_library_the_register_reports_and_the_engine_does_not_is_named(self):
        self.write('src/AfterNightEngine/src/runtime/DependencyReport.cpp', '\n')
        self.assertTrue(any('the engine reports no such dependency' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_shape_the_suite_pins_differently_is_named(self):
        self.write('src/AfterNightTests/Test_EngineCore/DependencyReportTest.cpp',
                   '{"onetbb", true, false},\n')
        self.assertTrue(any('is pinned as compiled=yes loaded=no' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_library_the_suite_pins_and_the_register_does_not_is_named(self):
        self.write('src/AfterNightTests/Test_EngineCore/DependencyReportTest.cpp',
                   '{"onetbb", true, true},\n{"libtiff", true, true},\n')
        self.assertTrue(any('the suite pins libtiff' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_missing_suite_is_a_finding_rather_than_a_pass(self):
        (self.root / 'src/AfterNightTests/Test_EngineCore/DependencyReportTest.cpp').unlink()
        self.assertTrue(any('the suite that pins the report does not exist' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))


class PortedCase(RegisterCase):
    """Whether a ported subsystem's files still say where they came from."""

    def test_a_file_that_lost_its_attribution_is_named(self):
        self.write('src/AfterNightEngine/src/kernels/reference/DemosaicKernelsFast.cpp',
                   '// The gradient-weighted estimate.\n')
        self.assertTrue(any('no longer contains' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_file_a_record_names_and_the_tree_does_not_have_is_named(self):
        (self.root / 'src/AfterNightEngine/src/kernels/reference/DemosaicKernelsFast.cpp').unlink()
        self.assertTrue(any('which does not exist' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_document_that_does_not_exist_is_named(self):
        (self.root / 'docs/dev/guides/DEPENDENCIES.md').unlink()
        self.assertTrue(any('record names docs/dev/guides/DEPENDENCIES.md, which does not exist' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_a_record_that_names_no_document_is_named(self):
        text = PORTED.replace('docs/dev/guides/DEPENDENCIES.md, and the module comment of each file',
                              'the module comment of each file')
        self.assertTrue(any('record names no document of this tree' in problem
                            for problem in self.findings(LIBRARY + '\n' + text)))

    def test_the_documents_a_record_names_are_read_out_of_its_sentence(self):
        self.assertEqual(provenance.record_documents('docs/dev/guides/DEPENDENCIES.md, and the module comment'),
                         ['docs/dev/guides/DEPENDENCIES.md'])
        self.assertEqual(provenance.record_documents('docs/dev/guides/DEPENDENCIES.md and docs/dev/A_PROVENANCE.md.'),
                         ['docs/dev/guides/DEPENDENCIES.md', 'docs/dev/A_PROVENANCE.md'])
        self.assertEqual(provenance.record_documents('the module comment of the file'), [])

    def test_a_new_file_carrying_an_attribution_and_no_record_is_named(self):
        self.write('src/AfterNightEngine/src/kernels/reference/DemosaicKernelsOther.cpp',
                   '// Derived from somebody, copyright 2011 Someone, GPL-3.\n')
        self.assertTrue(any('and no ported record claims it' in problem
                            for problem in self.findings(LIBRARY + '\n' + PORTED)))

    def test_prose_about_a_copyright_is_not_an_attribution(self):
        # The narrowing this rule needs to be usable: the engine's own comments describe a library's
        # copyright banner without carrying one, and a sweep that reported those would report
        # sentences rather than obligations.
        self.write('src/AfterNightEngine/src/io/tiff/TiffCodec.cpp',
                   '// libtiff answers with a multi-line copyright banner and the version is read out of it.\n')
        self.assertEqual(self.findings(LIBRARY + '\n' + PORTED), [])


class LinkSpellingCase(unittest.TestCase):
    """How a Debug link name is reduced to the library it is a build of."""

    def test_a_trailing_d_is_a_debug_spelling(self):
        self.assertIn('gsl', list(provenance.link_spellings('gsld')))

    def test_a_debug_suffix_is_a_debug_spelling(self):
        self.assertIn('tbb12', list(provenance.link_spellings('tbb12_debug')))

    def test_the_token_itself_comes_first(self):
        self.assertEqual(list(provenance.link_spellings('tiff'))[0], 'tiff')

    def test_a_conditional_link_is_returned_as_conditional(self):
        records, problems = provenance.parse_register(
            LIBRARY.replace('    links       none\n',
                            '    links       afternight_rawspeed (AFTERNIGHT_RAWSPEED)\n'))
        self.assertEqual(problems, [])
        claimed, conditional = provenance.registered_links(records)
        self.assertIn('afternight_rawspeed', claimed)
        self.assertIn('afternight_rawspeed', conditional)


class GeneratedTreeCase(RegisterCase):
    """What the link rules do when there is no current generated tree to read."""

    def test_a_root_with_no_generated_tree_says_so_rather_than_passing(self):
        self.register(LIBRARY + '\n' + PORTED)
        problems = []
        provenance.check_links(str(self.root), provenance.parse_register(
            (self.root / provenance.REGISTER).read_text(encoding='utf-8'))[0], problems, out=io.StringIO())
        self.assertTrue(any('no current generated tree' in problem for problem in problems), problems)

    def test_this_repository_has_at_least_one_current_tree(self):
        usable, _ = provenance.usable_trees(provenance.REPO_ROOT)
        self.assertTrue(usable, 'generate projects before running this suite')

    def test_a_tree_that_is_missing_or_stale_is_named_rather_than_ignored(self):
        report = io.StringIO()
        problems = []
        records, _ = provenance.parse_register(
            (pathlib.Path(provenance.REPO_ROOT) / provenance.REGISTER).read_text(encoding='utf-8'))
        provenance.check_links(str(provenance.REPO_ROOT), records, problems, out=report)
        _, unusable = provenance.usable_trees(provenance.REPO_ROOT)
        if unusable:
            self.assertIn('not checking for links no target carries', report.getvalue())
        else:
            self.assertEqual(report.getvalue(), '')


class InstalledCase(RegisterCase):
    """The package database is compared with the ports acquired for its own platform."""

    def setUp(self):
        super().setUp()
        self.vcpkg = self.root / 'vcpkg'
        self.write('build/scripts/linux/02-install_linux_deps.sh',
                   "./vcpkg install 'tbb[hwloc]' --triplet x64-linux\n")

    def status(self, package='sentinel', version='1.0', architecture='x64-linux'):
        self.write('vcpkg/installed/vcpkg/status',
                   'Package: %s\nVersion: %s\nArchitecture: %s\n'
                   % (package, version, architecture))

    def linux_record(self):
        return LIBRARY.replace(
            '    build       build/scripts/msvc/02-install_msvc_deps.bat\n',
            '    build       build/scripts/msvc/02-install_msvc_deps.bat, '
            'build/scripts/linux/02-install_linux_deps.sh\n')

    def installed_findings(self, text):
        records, problems = provenance.parse_register(text)
        self.assertEqual(problems, [])
        provenance.check_installed(records, str(self.vcpkg), problems, repo_root=str(self.root))
        return problems

    def test_a_windows_only_port_is_not_required_by_a_linux_database(self):
        self.status()
        self.assertEqual(self.installed_findings(LIBRARY), [])

    def test_a_distribution_package_name_is_not_a_vcpkg_install(self):
        self.status()
        self.write('build/scripts/linux/02-install_linux_deps.sh',
                   'apt-get install libtbb-dev\n')
        self.assertEqual(self.installed_findings(self.linux_record()), [])

    def test_a_port_the_linux_script_installs_must_be_present(self):
        self.status()
        self.assertTrue(any('`tbb` is not installed' in problem
                            for problem in self.installed_findings(self.linux_record())))

    def test_a_port_at_the_recorded_version_passes(self):
        self.status(package='tbb', version='2023.1.0')
        self.assertEqual(self.installed_findings(self.linux_record()), [])

    def test_a_port_at_another_version_is_named(self):
        self.status(package='tbb', version='2022.0.0')
        self.assertTrue(any('installed at 2022.0.0' in problem
                            for problem in self.installed_findings(self.linux_record())))


def archive_patterns():
    """The archive set's patterns as `check_public_hygiene.py` reads them, or none where that check is absent - the
    public tree carries neither the check nor its manifest."""
    try:
        import check_public_hygiene as hygiene  # noqa: E402
    except ImportError:
        return []
    path = pathlib.Path(provenance.REPO_ROOT) / hygiene.MANIFEST
    if not path.is_file():
        return []
    entries, _ = hygiene.parse_manifest(path.read_text(encoding='utf-8'))
    return [hygiene.pattern_regex(entry.pattern) for entry in entries if entry.klass != hygiene.MOVE]


class RepositoryCase(unittest.TestCase):
    """The real register against the real tree."""

    def setUp(self):
        self.records, problems = provenance.parse_register(
            (pathlib.Path(provenance.REPO_ROOT) / provenance.REGISTER).read_text(encoding='utf-8'))
        self.assertEqual(problems, [])

    def test_the_register_and_the_tree_agree(self):
        self.assertEqual(provenance.check(str(provenance.REPO_ROOT), out=io.StringIO()), [])

    def test_every_library_this_engine_reports_carries_its_shape(self):
        reported = {record.name for record in provenance.libraries(self.records) if 'reported' in record.fields}
        self.assertEqual(reported, set(provenance.engine_reported_names(provenance.REPO_ROOT)))

    def test_every_external_library_group_the_layer_checker_declares_is_decided(self):
        # A group is either a library somebody registered, a group this checker names as belonging to
        # no library, or a group nothing reaches. A fourth answer would be a library with no record.
        import check_include_layers as layers  # noqa: E402
        claimed = provenance.registered_groups(self.records)
        reached = provenance.reached_groups(provenance.REPO_ROOT)
        for group in layers.EXTERNAL_LIBRARIES:
            decided = (group in claimed or group in provenance.NON_LIBRARY_GROUPS or group not in reached)
            self.assertTrue(decided, group)

    def test_every_ported_record_names_a_document_of_the_public_tree_that_exists(self):
        # A document the archive set takes out of the tree is not one a reader of the register can open.
        archived = archive_patterns()
        for record in provenance.ported(self.records):
            documents = provenance.record_documents(record.fields['record'])
            self.assertTrue(documents, record.name)
            for relative in documents:
                self.assertTrue((pathlib.Path(provenance.REPO_ROOT) / relative).is_file(), relative)
                self.assertFalse(any(pattern.match(relative) for pattern in archived), relative)

    def test_the_records_that_carry_upstream_code_are_the_ones_that_say_so(self):
        # The field a licence question is answered from, stated as a property of the register rather
        # than as a consequence of whichever rows happen to be in it: these eleven carry upstream code,
        # and every other record re-derives a method, replaces one or carries nothing.
        carried = {record.name for record in provenance.ported(self.records)
                   if record.fields['relation'] == 'adapted'}
        self.assertEqual(carried, {'demosaic-rawtherapee', 'nl-bayes-patch-estimator', 'median-networks',
                                   'drizzle-corner-interpolation', 'siril-auto-gradient-removal',
                                   'cosmic-photons-nbcolormapper', 'setiastro-narrowband-palettes',
                                   'deepskystacker-weighted-mean', 'deepskystacker-star-mask',
                                   'siril-star-detection', 'siril-background-sampling'})


class LicenceHeaderCase(RegisterCase):
    """Every file carries the program's own licence header, which is not a third party's attribution."""

    HEADER = '// SPDX-License-Identifier: GPL-3.0-or-later\n// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz\n\n'

    def test_the_licence_header_is_not_a_third_party_attribution(self):
        self.write('src/AfterNightEngine/src/io/tiff/TiffCodec.cpp', self.HEADER + '// Reads TIFF.\nint a;\n')
        self.assertEqual(self.findings(LIBRARY + '\n' + PORTED), [])

    def test_a_header_stating_another_licence_or_holder_is_still_read(self):
        for header in ('// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause\n',
                       '// SPDX-FileCopyrightText: 2011 Someone Else\n'):
            self.write('src/AfterNightEngine/src/io/tiff/TiffCodec.cpp', header + 'int a;\n')
            self.assertTrue(any('and no ported record claims it' in problem
                                for problem in self.findings(LIBRARY + '\n' + PORTED)), header)


if __name__ == '__main__':
    unittest.main(verbosity=2)
