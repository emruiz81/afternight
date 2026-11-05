# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_forbidden_constructs.

Run with:
    python build/scripts/tools/check_forbidden_constructs_test.py

Most cases build a throwaway tree, so the suite needs nothing from the real repository and writes
nothing outside its own temporary directory. The last two read the repository itself, because a
checker that only ever passes on synthetic input proves nothing about the tree it exists to guard -
and because this checker's one real hazard is the engine's own prose, which explains the constructs
it refuses and names them while doing it.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_forbidden_constructs as forbidden  # noqa: E402


class ForbiddenConstructCase(unittest.TestCase):
    """A throwaway engine tree each case then writes one file into."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-forbidden-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        for directory in forbidden.SCANNED_DIRS:
            (self.root / directory).mkdir(parents=True, exist_ok=True)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def check(self):
        return forbidden.check(self.root)

    def test_a_clean_tree_reports_nothing(self):
        self.write('src/AfterNightEngine/src/runtime/Fine.cpp',
                   '#include <oneapi/tbb/task_arena.h>\n'
                   'void run(tbb::task_arena& arena) { arena.execute([]{}); }\n')
        self.assertEqual(self.check(), [])

    def test_global_control_is_refused(self):
        self.write('src/AfterNightEngine/src/runtime/Bad.cpp',
                   '#include <oneapi/tbb/global_control.h>\n'
                   'void cap() { tbb::global_control limit(tbb::global_control::max_allowed_parallelism, 4); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 2, problems)
        for problem in problems:
            self.assertIn('src/AfterNightEngine/src/runtime/Bad.cpp', problem)
            self.assertIn('whole process', problem)

    def test_the_removed_scheduler_initialiser_is_refused(self):
        self.write('src/AfterNightEngine/src/runtime/Old.cpp',
                   'void cap() { tbb::task_scheduler_init init(4); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('removed spelling', problems[0])

    def test_the_generic_pixel_type_switch_macro_is_refused(self):
        self.write('src/AfterNightEngine/src/image/Dispatch.cpp',
                   'void run() { CALL_TEMPLATE_BY_PIXEL_TYPE(convert, type, source, destination); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('generic pixel-type switch', problems[0])

    def test_a_second_sample_dispatch_is_refused_anywhere_but_its_home(self):
        self.write('src/AfterNightEngine/src/algorithms/color/Local.cpp',
                   'template <typename BodyT> bool dispatchSampleType(SampleType sampleType, BodyT&& body)\n'
                   '{\n    return false;\n}\n')
        self.write('src/AfterNightTests/Test_EngineImageOps/LocalTest.cpp',
                   'template <typename BodyT> bool dispatchSampleType(an::SampleType type, BodyT body);\n')
        problems = self.check()
        self.assertEqual(len(problems), 2, problems)
        for problem in problems:
            self.assertIn('second sample-type dispatch', problem)

    def test_the_published_sample_dispatch_and_its_calls_are_not_findings(self):
        self.write(forbidden.SAMPLE_DISPATCH_HOME,
                   'template <typename BodyT>\n'
                   '[[nodiscard]] constexpr bool dispatchSampleType(SampleType sampleType, BodyT&& body)\n'
                   '{\n    return true;\n}\n')
        self.write('src/AfterNightEngine/src/algorithms/color/Caller.cpp',
                   'bool run(an::SampleType sampleType)\n{\n'
                   '    return dispatchSampleType(sampleType, [&]<typename SampleT>() {});\n}\n')
        self.assertEqual(self.check(), [])

    def test_the_engine_may_not_change_ffmpegs_process_wide_state(self):
        self.write('src/AfterNightEngine/src/io/sequences/ffmpeg/Quiet.cpp',
                   'void quiet() { av_log_set_level(AV_LOG_QUIET); avformat_network_init(); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('process-wide FFmpeg setting', problems[0])

    def test_a_test_process_may_quiet_ffmpegs_log(self):
        self.write('src/AfterNightTests/Test_EngineImage/VideoFixture.cpp',
                   'void quiet() { av_log_set_level(AV_LOG_QUIET); }\n')
        self.assertEqual(self.check(), [])

    def test_a_comment_naming_an_ffmpeg_setting_is_not_a_finding(self):
        self.write('src/AfterNightEngine/src/io/sequences/ffmpeg/Note.cpp',
                   '// The engine never calls av_log_set_callback( on the process.\n')
        self.assertEqual(self.check(), [])

    def test_a_void_pointer_is_refused_in_the_typed_image_core(self):
        self.write('src/AfterNightEngine/include/AfterNightEngine/Image/PlaneView.h',
                   'const void* erase(const unsigned short* samples);\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('erases the sample type', problems[0])

    def test_type_erasure_is_allowed_at_an_explicit_byte_boundary(self):
        self.write('src/AfterNightEngine/include/AfterNightEngine/Image/SampleBuffer.h',
                   'void adopt(void* bytes);\n')
        self.assertEqual(self.check(), [])

    def test_an_open_ended_holder_is_refused_in_the_typed_image_core(self):
        self.write('src/AfterNightEngine/include/AfterNightEngine/Image/Image.h',
                   'std::any sample;\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('std::any', problems[0])

    def test_a_conversion_kernel_cannot_own_a_hidden_frame_buffer(self):
        self.write('src/AfterNightEngine/src/backends/cpu/ConversionKernelsCpu.cpp',
                   'void convert() { std::vector<float> hiddenFullFrame; }\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('sample-movement hot path', problems[0])

    def test_a_vector_layout_kernel_cannot_own_a_hidden_frame_buffer_either(self):
        """The rule follows the shape of the work, not the name of the one family that first had it."""
        self.write('src/AfterNightEngine/src/backends/cpu/SimdKernelTables.cpp',
                   'void copyRun() { std::vector<float> hiddenFullFrame; }' + chr(10))
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('sample-movement hot path', problems[0])

    def test_an_estimator_cannot_allocate_one_buffer_per_call(self):
        self.write('src/AfterNightEngine/src/algorithms/statistics/RobustStatistics.cpp',
                   'double mad() { std::vector<float> deviations; return 0.0; }' + chr(10))
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('estimator path', problems[0])

    def test_an_estimator_cannot_decide_per_sample_through_an_indirect_call(self):
        self.write('src/AfterNightEngine/src/algorithms/reduction/ReductionPrimitives.cpp',
                   'void reduce(const std::function<bool(double)>& filter) { (void)filter; }' + chr(10))
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('indirect call', problems[0])

    def test_rejection_estimators_and_their_band_driver_cannot_allocate(self):
        for name in ('RejectionEstimators.cpp', 'RejectionEstimators.h', 'StackRejection.cpp'):
            self.write('src/AfterNightEngine/src/algorithms/stacking/' + name,
                       'void reject() { std::vector<float> perPixel; }\n')
        problems = self.check()
        self.assertEqual(len(problems), 3, problems)
        self.assertTrue(all('estimator path' in problem for problem in problems))

    def test_the_local_weight_surface_and_the_quality_diagnostics_cannot_allocate(self):
        for name in ('LocalWeights.cpp', 'StackQuality.cpp'):
            self.write('src/AfterNightEngine/src/algorithms/stacking/' + name,
                       'void measure() { std::vector<double> perNode; }\n')
        problems = self.check()
        self.assertEqual(len(problems), 2, problems)
        self.assertTrue(all('estimator path' in problem for problem in problems))

    def test_the_stack_executor_cannot_allocate_beside_its_plan(self):
        self.write('src/AfterNightEngine/src/algorithms/stacking/StackExecutor.cpp',
                   'void combine() { std::vector<float> bands; auto* records = new int[4]; (void)records; }'
                   + chr(10))
        problems = self.check()
        self.assertEqual(len(problems), 2, problems)
        self.assertTrue(all('estimator path' in problem for problem in problems))

    def test_the_stack_source_row_cannot_own_a_frame_buffer(self):
        self.write('src/AfterNightEngine/src/kernels/reference/StackSourceRow.inl',
                   'void widen() { std::vector<float> band; }\n')
        self.write('src/AfterNightEngine/src/backends/cpu/StackSourceCpu.cpp',
                   'void* row() { return new float[8]; }\n')
        problems = self.check()
        self.assertEqual(len(problems), 2, problems)
        self.assertTrue(all('sample-movement hot path' in problem for problem in problems))

    def test_an_owner_outside_the_estimator_hot_path_is_not_mistaken_for_one(self):
        self.write('src/AfterNightEngine/src/algorithms/pointwise/PointwiseProgram.cpp',
                   'void keep() { std::vector<float> persistentProgram; }' + chr(10))
        self.assertEqual(self.check(), [])

    def test_every_declared_hot_path_file_exists_in_the_tree(self):
        """A renamed hot-path file would otherwise stop being guarded with nothing saying so."""
        for relative in sorted(forbidden.CONVERSION_HOT_PATHS | forbidden.ESTIMATOR_HOT_PATHS):
            self.assertTrue(os.path.isfile(os.path.join(str(forbidden.REPO_ROOT), relative)),
                            relative)

    def test_an_owner_outside_the_conversion_hot_path_is_not_mistaken_for_one(self):
        self.write('src/AfterNightEngine/src/image/OrdinaryOwner.cpp',
                   'void keep() { std::vector<float> persistentImage; }\n')
        self.assertEqual(self.check(), [])

    def test_a_finding_names_the_line_it_is_on(self):
        self.write('src/AfterNightEngine/src/runtime/Line.cpp',
                   'void first() {}\n'
                   'void second() {}\n'
                   'void third() { tbb::global_control limit(0, 1); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn(':3:', problems[0])

    def test_prose_explaining_the_ban_is_not_a_finding(self):
        # The hazard this checker actually has. Every explanation of the rule names the construct,
        # so a checker that read comments would report the engine's own documentation.
        self.write('src/AfterNightEngine/src/runtime/Explained.cpp',
                   '// Nothing here constructs a tbb::global_control: it is process-wide.\n'
                   '/* task_scheduler_init is the removed spelling of the same mistake. */\n'
                   'void run() {}\n')
        self.assertEqual(self.check(), [])


    def test_an_exported_function_template_in_the_published_surface_is_a_finding(self):
        self.write('src/AfterNightEngine/include/AfterNightEngine/Algorithms/Domain/Op.h',
                   'namespace an {\n'
                   'template <typename SampleT>\n'
                   '[[nodiscard]] AFTERNIGHTENGINE_API int reduce(const SampleT* samples);\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn(':2:', problems[0])
        self.assertIn('non-template entry point', problems[0])

    def test_an_exported_member_of_a_class_template_is_a_finding(self):
        self.write('src/AfterNightEngine/include/AfterNightEngine/Image/Owned.h',
                   'namespace an {\n'
                   'template <typename SampleT> class Owned\n'
                   '{\n'
                   'public:\n'
                   '    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const;\n'
                   '};\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn(':2:', problems[0])

    def test_a_typed_wrapper_forwarding_to_an_exported_entry_point_is_not_a_finding(self):
        # The shape the surface actually uses, and the one the rule has to permit: an inline typed
        # convenience that reads its arguments off a view and calls the untyped exported entry
        # point next door. The export marker is on the declaration above the template, not in it.
        self.write('src/AfterNightEngine/include/AfterNightEngine/Algorithms/Domain/Wrap.h',
                   'namespace an {\n'
                   '[[nodiscard]] AFTERNIGHTENGINE_API int reduceImage(const void* samples, int type);\n'
                   '\n'
                   'template <typename SampleT>\n'
                   '[[nodiscard]] int reduce(const SampleT* samples)\n'
                   '{\n'
                   '    return reduceImage(samples, 0);\n'
                   '}\n'
                   '}\n')
        self.assertEqual(self.check(), [])

    def test_an_exported_template_outside_the_published_surface_is_not_a_finding(self):
        # The rule is about what crosses the library boundary. A private implementation header is
        # free to be templated however its layer needs, and the export marker there is a mistake the
        # linker finds rather than a decision this checker owns.
        self.write('src/AfterNightEngine/src/algorithms/domain/Private.h',
                   'template <typename SampleT> AFTERNIGHTENGINE_API int reduce(const SampleT*);\n')
        self.assertEqual(self.check(), [])

    def test_a_comment_mentioning_an_exported_template_is_not_a_finding(self):
        self.write('src/AfterNightEngine/include/AfterNightEngine/Core/Prose.h',
                   '// A template <typename T> carrying AFTERNIGHTENGINE_API would be refused.\n'
                   'namespace an { int value(); }\n')
        self.assertEqual(self.check(), [])

    def test_an_exported_template_in_an_optional_adapters_surface_is_a_finding(self):
        # A second library boundary in the same tree, with its own export marker. The rule is not
        # about the engine's marker; it is about a template whose meaning is decided by whoever
        # instantiated it crossing a library boundary.
        self.write('src/AfterNightEnginePythonAdapter/include/AfterNightEnginePythonAdapter/'
                   'Bridge.h',
                   'namespace an {\n'
                   'template <typename SampleT>\n'
                   '[[nodiscard]] AFTERNIGHTENGINEPYTHONADAPTER_API int share(const SampleT*);\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('non-template entry point', problems[0])

    def test_the_engines_marker_in_an_adapter_surface_is_not_this_rule(self):
        # The adapter declares nothing with the engine's marker, and if it did, the linker is what
        # says so. Each surface is judged by its own marker so a finding names the boundary it is
        # about.
        self.write('src/AfterNightEnginePythonAdapter/include/AfterNightEnginePythonAdapter/'
                   'Other.h',
                   'template <typename SampleT> AFTERNIGHTENGINE_API int share(const SampleT*);\n')
        self.assertEqual(self.check(), [])

    def test_an_optional_adapter_may_not_cap_parallelism_either(self):
        self.write('src/AfterNightEnginePythonAdapter/src/Bridge.cpp',
                   'void cap() { tbb::global_control limit(0, 4); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('whole process', problems[0])

    def test_an_exported_template_in_the_gmic_adapters_surface_is_a_finding(self):
        self.write('src/AfterNightEngineGmicAdapter/include/AfterNightEngineGmicAdapter/Bridge.h',
                   'namespace an {\n'
                   'template <typename SampleT>\n'
                   '[[nodiscard]] AFTERNIGHTENGINEGMICADAPTER_API int share(const SampleT*);\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('non-template entry point', problems[0])

    def test_the_gmic_adapter_and_its_suite_may_not_cap_parallelism_either(self):
        self.write('src/AfterNightEngineGmicAdapter/src/Session.cpp',
                   'void cap() { tbb::global_control limit(0, 4); }\n')
        self.write('src/AfterNightTests/Test_EngineGmicAdapter/SessionTest.cpp',
                   'void cap() { tbb::global_control limit(0, 4); }\n')
        problems = self.check()
        self.assertEqual(len(problems), 2, problems)

    def test_an_exported_template_in_the_gpu_backends_surface_is_a_finding(self):
        self.write('src/AfterNightEngineOpenClBackend/include/AfterNightEngineOpenClBackend/Buffer.h',
                   'namespace an {\n'
                   'template <typename SampleT>\n'
                   '[[nodiscard]] AFTERNIGHTENGINEOPENCLBACKEND_API int upload(const SampleT*);\n'
                   '}\n')
        problems = self.check()
        self.assertEqual(len(problems), 1, problems)
        self.assertIn('non-template entry point', problems[0])

    def test_every_optional_adapter_the_build_graph_declares_is_scanned(self):
        # An adapter added to the build and not to this checker is a published surface nobody judges.
        # The opt-in GPU backend is the same kind of library boundary, generated or not.
        import check_build_graph as graph  # noqa: E402
        surfaces = dict(forbidden.PUBLIC_SURFACES)
        for adapter, suite in list(graph.OPTIONAL_ADAPTER_SUITES.items()) + list(
                graph.OPTIONAL_BACKEND_SUITES.items()):
            self.assertIn('src/' + adapter, forbidden.SCANNED_DIRS)
            self.assertIn('src/AfterNightTests/' + suite, forbidden.SCANNED_DIRS)
            self.assertEqual(surfaces.get('src/%s/include/%s/' % (adapter, adapter)), adapter.upper() + '_API')

    def test_every_engine_suite_in_the_repository_is_scanned(self):
        # The same omission from the other side: a suite that links the engine and is not in the list
        # can cap parallelism for its whole process with nothing to say so. The benchmark layer's own suite is
        # engine-neutral and not one of them, whatever its name begins with.
        import check_build_graph as graph  # noqa: E402
        suites = sorted(path.name for path in (forbidden.REPO_ROOT / 'src/AfterNightTests').glob('Test_Engine*')
                        if path.is_dir() and graph.is_engine_target(path.name))
        self.assertGreater(len(suites), 10)
        for suite in suites:
            self.assertIn('src/AfterNightTests/' + suite, forbidden.SCANNED_DIRS)

    def test_the_real_repository_is_clean(self):
        problems = forbidden.check(forbidden.REPO_ROOT)
        self.assertEqual(problems, [], '\n'.join(problems))

    def test_the_real_repository_has_something_to_scan(self):
        # A checker whose scanned directories had all been renamed would pass the case above by
        # finding no files at all, which is the way a green check goes quiet.
        sources = forbidden.collect_sources(forbidden.REPO_ROOT)
        self.assertGreater(len(sources), 20, 'the scanned directories reached almost nothing')
        self.assertTrue(any('AfterNightEngine' in relative for _, relative in sources))


if __name__ == '__main__':
    unittest.main(verbosity=2)
