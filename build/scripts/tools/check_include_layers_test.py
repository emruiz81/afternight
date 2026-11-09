# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for check_include_layers.

Run with:
    python build/scripts/tools/check_include_layers_test.py

Every case builds a throwaway engine tree, so the suite needs nothing from the real repository and
writes nothing outside its own temporary directory. The last case does read the repository's own
engine, because a checker that only ever passes on synthetic input proves nothing about the tree it
exists to guard.
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

import check_include_layers as layers  # noqa: E402


PRAGMA = '#pragma once'


class EngineTreeCase(unittest.TestCase):
    """A minimal, valid engine tree that each case then breaks in exactly one way."""

    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix='an-layers-test-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        for area in layers.PUBLIC_AREAS:
            (self.root / layers.PUBLIC_DIR / area).mkdir(parents=True, exist_ok=True)
        for layer in layers.PRIVATE_LAYERS:
            (self.root / layers.PRIVATE_DIR / layer).mkdir(parents=True, exist_ok=True)
        (self.root / layers.SELF_COMPILE_DIR).mkdir(parents=True, exist_ok=True)
        # The optional adapters and the opt-in GPU backend are part of the tree this checker guards,
        # so every case starts with their directories present and empty. A case that is about one of
        # them writes files into it; every other case proves an empty one is not a finding.
        for adapter in layers.OPTIONAL_COMPONENTS:
            (self.root / adapter.public_dir).mkdir(parents=True, exist_ok=True)
            (self.root / adapter.private_dir).mkdir(parents=True, exist_ok=True)
            (self.root / adapter.self_compile_dir).mkdir(parents=True, exist_ok=True)

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def public_header(self, below, body='', self_compile=True):
        """A public header, with the self-compile unit the checker requires for it."""
        self.write(layers.PUBLIC_DIR + '/' + below, PRAGMA + '\n\n' + body + '\nstruct Marker {};\n')
        if self_compile:
            include = layers.PUBLIC_INCLUDE_PREFIX + below
            unit = layers.self_compile_unit_name(below)
            self.write(layers.SELF_COMPILE_DIR + '/' + unit,
                       '#include <%s>\n#include <%s>\n' % (include, include))

    def private_file(self, below, body=''):
        self.write(layers.PRIVATE_DIR + '/' + below, body)

    def private_header(self, below, body=''):
        self.write(layers.PRIVATE_DIR + '/' + below, PRAGMA + '\n' + body)

    def run_check(self):
        stream = io.StringIO()
        code = layers.main(['--repo-root', str(self.root)], stream=stream)
        return code, stream.getvalue()

    def assertClean(self):
        code, output = self.run_check()
        self.assertEqual(code, 0, output)

    def assertRejected(self, fragment):
        code, output = self.run_check()
        self.assertEqual(code, 1, 'expected a rejection, got:\n' + output)
        self.assertIn(fragment, output)


class ValidTreeTests(EngineTreeCase):
    def test_fits_metadata_reuses_only_the_card_bridge(self):
        self.public_header('Algorithms/Astrometry/WcsCards.h')
        self.private_file('io/fits/FitsMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>\n')
        self.assertClean()

    def test_fits_cannot_reach_an_unrelated_algorithm(self):
        self.public_header('Algorithms/Astrometry/PlateSolver.h')
        self.private_file('io/fits/FitsMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>\n')
        self.assertRejected('may not include Algorithms')

    def test_xisf_metadata_reuses_only_the_card_bridge(self):
        self.public_header('Algorithms/Astrometry/WcsCards.h')
        self.private_file('io/xisf/XisfMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>\n')
        self.assertClean()

    def test_xisf_cannot_reach_an_unrelated_algorithm(self):
        self.public_header('Algorithms/Astrometry/PlateSolver.h')
        self.private_file('io/xisf/XisfMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>\n')
        self.assertRejected('may not include Algorithms')

    def test_xisf_reaches_openxisf_as_a_codec_library(self):
        self.private_file('io/xisf/XisfFile.cpp',
                          '#include <openxisf/reader.h>\n#include <openxisf/version.h>\n')
        self.assertClean()

    # OpenXISF is the library behind the XISF codec; neither another XISF library's spelling nor a header merely
    # named like the format is a library the engine declares.
    def test_other_xisf_library_spellings_are_no_library_the_engine_declares(self):
        self.private_file('io/xisf/XisfFile.cpp', '#include <libxisf/libxisf.h>\n')
        self.assertRejected('neither a standard header nor a library this engine declares')
        self.private_file('io/xisf/XisfFile.cpp', '#include <xisf.h>\n')
        self.assertRejected('neither a standard header nor a library this engine declares')

    def test_openxisf_is_a_codec_library_no_other_layer_may_include(self):
        self.private_file('algorithms/stacking/StackPlan.cpp', '#include <openxisf/reader.h>\n')
        self.assertRejected('may not include codecs')

    def test_fits_reaches_openfits_as_a_codec_library(self):
        self.private_file('io/fits/FitsFile.cpp',
                          '#include <openfits/reader.h>\n#include <openfits/version.h>\n')
        self.assertClean()

    # OpenFITS is the library behind the FITS codec; the headers of the library it replaced are no library the
    # engine declares any more.
    def test_cfitsio_spellings_are_no_library_the_engine_declares(self):
        self.private_file('io/fits/FitsFile.cpp', '#include <fitsio.h>\n')
        self.assertRejected('neither a standard header nor a library this engine declares')
        self.private_file('io/fits/FitsFile.cpp', '#include <longnam.h>\n')
        self.assertRejected('neither a standard header nor a library this engine declares')

    def test_openfits_is_a_codec_library_no_other_layer_may_include(self):
        self.private_file('algorithms/stacking/StackPlan.cpp', '#include <openfits/reader.h>\n')
        self.assertRejected('may not include codecs')

    def test_card_metadata_reuses_only_the_card_bridge(self):
        self.public_header('Algorithms/Astrometry/WcsCards.h')
        self.private_file('io/metadata/CombinedMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>\n')
        self.assertClean()

    def test_card_metadata_cannot_reach_an_unrelated_algorithm(self):
        self.public_header('Algorithms/Astrometry/PlateSolver.h')
        self.private_file('io/metadata/CombinedMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>\n')
        self.assertRejected('may not include Algorithms')

    def test_other_codecs_do_not_acquire_the_fits_bridge_dependency(self):
        self.public_header('Algorithms/Astrometry/WcsCards.h')
        self.private_file('io/png/PngMetadata.cpp',
                          '#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>\n')
        self.assertRejected('may not include Algorithms')

    def test_a_minimal_valid_tree_passes(self):
        self.public_header('Core/Version.h')
        self.private_file('core/Version.cpp',
                          '#include <AfterNightEngine/Core/Version.h>\n#include <cstdint>\n')
        self.assertClean()

    def test_an_include_inside_a_block_comment_is_not_a_directive(self):
        self.public_header('Core/Version.h',
                           '/* Once this existed:\n#include <opencv2/core.hpp>\n*/\n')
        self.assertClean()

    def test_an_include_inside_a_line_comment_is_not_a_directive(self):
        self.public_header('Core/Version.h', '// #include <opencv2/core.hpp>\n')
        self.assertClean()


class PrefixAndForeignTests(EngineTreeCase):

    def test_a_public_header_reaches_another_by_the_engine_prefix(self):
        self.public_header('Core/Version.h')
        self.public_header('Core/Api.h', '#include <AfterNightEngine/Core/Version.h>\n')
        self.assertClean()

    def test_another_first_party_component_is_refused(self):
        self.private_file('core/Version.cpp',
                          '#include <AfterNightControls/theme/AnDesignTokens.h>\n')
        self.assertRejected('belongs to AfterNightControls')

    def test_a_relative_include_that_leaves_the_tree_is_refused(self):
        self.private_file('core/Version.cpp', '#include "../../AfterNightControls/theme/AnDesignTokens.h"\n')
        self.assertRejected('leaves the engine tree')


class PublicSurfaceTests(EngineTreeCase):
    def test_a_quoted_include_in_a_public_header_is_refused(self):
        self.public_header('Core/Version.h', '#include "core/Detail.h"\n')
        self.assertRejected('quoted include in a public header')

    def test_an_external_library_in_a_public_header_is_refused(self):
        self.public_header('Core/Version.h', '#include <opencv2/core.hpp>\n')
        self.assertRejected('no external library type crosses')

    def test_an_unknown_header_in_a_public_header_is_refused(self):
        self.public_header('Core/Version.h', '#include <mystery/thing.h>\n')
        self.assertRejected('neither a standard header nor part of this engine')

    def test_the_standard_library_is_available_to_a_public_header(self):
        self.public_header('Core/Version.h', '#include <cstdint>\n#include <stdint.h>\n')
        self.assertClean()

    def test_a_public_header_naming_no_public_header_is_refused(self):
        self.public_header('Core/Version.h', '#include <AfterNightEngine/Core/Absent.h>\n')
        self.assertRejected('names no public header')

    def test_core_may_not_include_runtime(self):
        self.public_header('Runtime/Context.h')
        self.public_header('Core/Version.h', '#include <AfterNightEngine/Runtime/Context.h>\n')
        self.assertRejected('a Core header may not include Runtime')

    def test_runtime_may_include_core(self):
        self.public_header('Core/Version.h')
        self.public_header('Runtime/Context.h', '#include <AfterNightEngine/Core/Version.h>\n')
        self.assertClean()

    def test_a_source_file_in_the_public_root_is_refused(self):
        self.write(layers.PUBLIC_DIR + '/Core/Version.cpp', '')
        self.assertRejected('headers only')

    def test_a_header_beside_the_public_root_is_refused(self):
        self.write(layers.ENGINE_DIR + '/include/Stray.h', PRAGMA + '\n')
        self.assertRejected('nothing else may sit beside it')

    def test_a_header_without_a_guard_is_refused(self):
        self.write(layers.PUBLIC_DIR + '/Core/Version.h', 'struct Marker {};\n')
        self.write(layers.SELF_COMPILE_DIR + '/CoreVersion.cpp',
                   '#include <AfterNightEngine/Core/Version.h>\n'
                   '#include <AfterNightEngine/Core/Version.h>\n')
        self.assertRejected('pragma-once guard')

    def test_an_umbrella_header_is_refused(self):
        self.public_header('Core/Version.h')
        self.public_header('Core/Api.h')
        self.write(layers.PUBLIC_DIR + '/Core/All.h',
                   PRAGMA + '\n#include <AfterNightEngine/Core/Version.h>\n'
                   '#include <AfterNightEngine/Core/Api.h>\n')
        self.write(layers.SELF_COMPILE_DIR + '/CoreAll.cpp',
                   '#include <AfterNightEngine/Core/All.h>\n'
                   '#include <AfterNightEngine/Core/All.h>\n')
        self.assertRejected('umbrella header the engine does not have')

    def test_a_header_that_re_exports_and_also_declares_is_not_an_umbrella(self):
        self.public_header('Core/Version.h')
        self.public_header('Core/Api.h')
        self.public_header('Core/Facade.h',
                           '#include <AfterNightEngine/Core/Version.h>\n'
                           '#include <AfterNightEngine/Core/Api.h>\n')
        self.assertClean()


class LayerGraphTests(EngineTreeCase):
    def test_a_reference_kernel_may_implement_a_public_algorithm_contract(self):
        self.public_header('Algorithms/Kernel/Contract.h')
        self.private_file('kernels/reference/Transfer.cpp',
                          '#include <AfterNightEngine/Algorithms/Kernel/Contract.h>\n')
        self.assertClean()

    def test_a_kernel_may_not_include_the_io_surface(self):
        self.public_header('IO/Codec.h')
        self.private_file('kernels/reference/Transfer.cpp',
                          '#include <AfterNightEngine/IO/Codec.h>\n')
        self.assertRejected('the kernels/reference layer may not include IO')

    def test_a_backend_may_include_a_kernel_header(self):
        self.private_header('kernels/reference/Transfer.h')
        self.private_file('backends/cpu/Transfer.cpp',
                          '#include "kernels/reference/Transfer.h"\n')
        self.assertClean()

    def test_a_kernel_may_not_include_a_backend_header(self):
        self.private_header('backends/cpu/Dispatch.h')
        self.private_file('kernels/reference/Transfer.cpp',
                          '#include "backends/cpu/Dispatch.h"\n')
        self.assertRejected('may not include the backends/cpu layer')

    def test_one_algorithm_domain_may_not_include_another_domains_private_header(self):
        self.private_header('algorithms/stacking/Accumulator.h')
        self.private_file('algorithms/registration/Solve.cpp',
                          '#include "algorithms/stacking/Accumulator.h"\n')
        self.assertRejected('may not reach into it')

    def test_an_algorithm_domain_may_include_its_own_private_header(self):
        self.private_header('algorithms/registration/Solve.h')
        self.private_file('algorithms/registration/Solve.cpp',
                          '#include "algorithms/registration/Solve.h"\n')
        self.assertClean()

    def test_a_quoted_include_naming_no_implementation_header_is_refused(self):
        self.private_file('core/Version.cpp', '#include "core/Absent.h"\n')
        self.assertRejected('names no implementation header')

    def test_an_implementation_header_included_with_angle_brackets_is_refused(self):
        self.private_header('core/Detail.h')
        self.private_file('image/Storage.cpp', '#include <core/Detail.h>\n')
        self.assertRejected('include it with quotes')


class ExternalLibraryTests(EngineTreeCase):
    def test_a_codec_may_implement_the_shared_prepared_write_interface(self):
        self.private_header('io/registry/PreparedCodecWrite.h')
        self.private_file('io/jpeg/Writer.cpp', '#include "io/registry/PreparedCodecWrite.h"\n')
        self.assertClean()

    def test_an_algorithm_cannot_reach_the_prepared_write_interface(self):
        self.private_header('io/registry/PreparedCodecWrite.h')
        self.private_file('algorithms/stacking/Writer.cpp', '#include "io/registry/PreparedCodecWrite.h"\n')
        self.assertRejected('may not include the io layer')

    def test_a_codec_may_implement_the_shared_prepared_read_interface(self):
        self.private_header('io/registry/PreparedCodecRead.h')
        self.private_file('io/fits/Reader.cpp', '#include "io/registry/PreparedCodecRead.h"\n')
        self.assertClean()

    def test_a_codec_and_the_output_transaction_may_reach_the_shared_reservation_interface(self):
        self.private_header('io/registry/ReservedCodecWrite.h')
        self.private_file('io/xisf/Writer.cpp', '#include "io/registry/ReservedCodecWrite.h"\n')
        self.private_file('io/output/Transaction.cpp', '#include "io/registry/ReservedCodecWrite.h"\n')
        self.assertClean()

    def test_an_algorithm_cannot_reach_the_reservation_interface(self):
        self.private_header('io/registry/ReservedCodecWrite.h')
        self.private_file('algorithms/stacking/Writer.cpp', '#include "io/registry/ReservedCodecWrite.h"\n')
        self.assertRejected('may not include the io layer')

    def test_a_codec_may_read_the_shared_rule_of_payload_forms(self):
        self.private_header('io/registry/PayloadForm.h')
        self.private_file('io/tiff/Writer.cpp', '#include "io/registry/PayloadForm.h"\n')
        self.assertClean()

    def test_a_codec_still_cannot_reach_registry_implementation(self):
        self.private_header('io/registry/CodecRegistryAccess.h')
        self.private_file('io/fits/Reader.cpp', '#include "io/registry/CodecRegistryAccess.h"\n')
        self.assertRejected('may not reach into it')

    def test_a_codec_may_hold_a_whole_file_through_the_shared_metadata_file(self):
        self.private_header('io/metadata/MetadataFile.h')
        self.private_file('io/raw/Metadata.cpp', '#include "io/metadata/MetadataFile.h"\n')
        self.assertClean()

    def test_a_codec_still_cannot_reach_the_metadata_domains_other_headers(self):
        self.private_header('io/metadata/FitsCardEdit.h')
        self.private_file('io/raw/Metadata.cpp', '#include "io/metadata/FitsCardEdit.h"\n')
        self.assertRejected('may not reach into it')

    def test_an_algorithm_cannot_reach_the_shared_metadata_file(self):
        self.private_header('io/metadata/MetadataFile.h')
        self.private_file('algorithms/stacking/Loader.cpp', '#include "io/metadata/MetadataFile.h"\n')
        self.assertRejected('may not include the io layer')

    def test_an_algorithm_cannot_reach_the_prepared_read_interface(self):
        self.private_header('io/registry/PreparedCodecRead.h')
        self.private_file('algorithms/stacking/Loader.cpp', '#include "io/registry/PreparedCodecRead.h"\n')
        self.assertRejected('may not include the io layer')

    def test_the_runtime_may_include_onetbb(self):
        self.private_file('runtime/Scheduler.cpp', '#include <tbb/task_arena.h>\n')
        self.assertClean()

    def test_core_may_not_include_onetbb(self):
        self.private_file('core/Extent.cpp', '#include <tbb/task_arena.h>\n')
        self.assertRejected('the core layer may not include onetbb')

    def test_an_adapter_may_include_the_library_its_directory_names(self):
        self.private_file('interop/opencv/Bridge.cpp', '#include <opencv2/core.hpp>\n')
        self.assertClean()

    def test_an_adapter_may_not_include_another_library(self):
        self.private_file('interop/opencv/Bridge.cpp', '#include <gsl/gsl_multifit.h>\n')
        self.assertRejected('the interop layer may not include gsl')

    def test_the_metadata_adapter_may_include_the_library_its_directory_names(self):
        self.private_file('interop/exiv2/ExifCards.cpp', '#include <exiv2/exif.hpp>\n')
        self.assertClean()

    def test_the_document_adapter_may_include_the_json_library(self):
        self.private_file('interop/json/CalibrationDocument.cpp', '#include <nlohmann/json.hpp>\n')
        self.assertClean()

    def test_a_metadata_domain_may_not_include_the_json_library(self):
        self.private_file('io/metadata/ProcessingStateCards.cpp', '#include <nlohmann/json.hpp>\n')
        self.assertRejected('the io layer may not include json')

    def test_the_video_adapter_directory_may_include_ffmpeg(self):
        self.private_file('io/sequences/ffmpeg/FfmpegInput.cpp',
                          '#include <libavformat/avformat.h>\n#include <libswscale/swscale.h>\n')
        self.assertClean()

    def test_another_sequence_adapter_may_not_include_ffmpeg(self):
        self.private_file('io/sequences/SerSource.cpp', '#include <libavcodec/avcodec.h>\n')
        self.assertRejected('the io layer may not include ffmpeg')

    def test_a_codec_may_not_include_ffmpeg(self):
        self.private_file('io/tiff/TiffRead.cpp', '#include <libavutil/pixdesc.h>\n')
        self.assertRejected('the io layer may not include ffmpeg')

    def test_a_public_header_may_not_include_ffmpeg(self):
        self.public_header('IO/Video.h', '#include <libavformat/avformat.h>\n')
        self.assertRejected('no external library type crosses the published surface')

    def test_a_codec_may_not_include_the_metadata_library(self):
        self.private_file('io/tiff/TiffRead.cpp', '#include <exiv2/exif.hpp>\n')
        self.assertRejected('the io layer may not include exiv2')

    def test_a_gpu_sdk_header_outside_the_gpu_backend_is_refused(self):
        self.private_file('backends/cpu/Dispatch.cpp', '#include <cuda_runtime.h>\n')
        self.assertRejected('only the GPU backend may include one')

    def test_the_gpu_backend_may_include_a_gpu_sdk_header(self):
        self.private_file('backends/gpu/Device.cpp', '#include <cuda_runtime.h>\n')
        self.assertClean()

    def test_the_io_layer_may_include_the_platform_file_headers(self):
        self.private_file('io/output/FileSystem.cpp', '#include <fcntl.h>\n#include <sys/stat.h>\n')
        self.assertClean()

    def test_another_layer_may_not_include_a_platform_header(self):
        self.private_file('algorithms/stacking/Stack.cpp', '#include <unistd.h>\n')
        self.assertRejected('the algorithms layer may not include platform')

    def test_native_windows_file_calls_belong_to_io(self):
        self.private_file('io/dump/Dump.cpp', '#include <Windows.h>\n')
        self.private_file('io/tiff/Tiff.cpp', '#include <Windows.h>\n')
        self.assertClean()

    def test_native_windows_headers_cannot_escape_io(self):
        self.private_file('image/Storage.cpp', '#include <Windows.h>\n')
        self.assertRejected('the image layer may not include platform')

    def test_no_engine_layer_may_start_a_thread_of_its_own(self):
        # The engine's parallel work goes through its task scheduler, so the thread header is a
        # declared group that no engine layer takes.
        self.private_file('runtime/Scheduler.cpp', '#include <pthread.h>\n')
        self.assertRejected('the runtime layer may not include threads')

    def test_the_io_platform_allowance_does_not_include_threads(self):
        self.private_file('io/output/FileSystem.cpp', '#include <pthread.h>\n')
        self.assertRejected('the io layer may not include threads')

    def test_an_undeclared_library_is_refused(self):
        self.private_file('runtime/Scheduler.cpp', '#include <mystery/thing.h>\n')
        self.assertRejected('nor a library this engine declares')


class StructureTests(EngineTreeCase):
    def test_a_file_in_an_undeclared_implementation_directory_is_refused(self):
        self.private_file('scratchpad/Thing.cpp', '')
        self.assertRejected('no implementation layer owns this file')

    def test_a_file_in_an_undeclared_public_area_is_refused(self):
        self.write(layers.PUBLIC_DIR + '/Extras/Thing.h', PRAGMA + '\n')
        self.assertRejected('no public subject area owns this file')

    def test_a_missing_layer_directory_is_refused(self):
        os.rmdir(self.root / layers.PRIVATE_DIR / 'ported')
        self.assertRejected('a declared implementation layer has no directory')

    def test_a_missing_public_area_directory_is_refused(self):
        os.rmdir(self.root / layers.PUBLIC_DIR / 'IO')
        self.assertRejected('a declared public subject area has no directory')

    def test_an_absent_engine_tree_is_refused(self):
        shutil.rmtree(self.root / layers.ENGINE_DIR)
        self.assertRejected('the engine tree does not exist')


class OwnHeaderFirstTests(EngineTreeCase):
    def test_an_implementation_file_must_include_its_own_public_header_first(self):
        self.public_header('Core/Version.h')
        self.private_file('core/Version.cpp',
                          '#include <cstdint>\n#include <AfterNightEngine/Core/Version.h>\n')
        self.assertRejected('the first include is <cstdint>')

    def test_an_implementation_file_must_include_its_own_private_header_first(self):
        self.private_header('runtime/Scheduler.h')
        self.private_file('runtime/Scheduler.cpp',
                          '#include <cstdint>\n#include "runtime/Scheduler.h"\n')
        self.assertRejected('the first include is <cstdint>')

    def test_a_file_with_no_header_of_its_own_name_is_unconstrained(self):
        self.private_file('runtime/Bootstrap.cpp', '#include <cstdint>\n')
        self.assertClean()

    def test_a_file_that_includes_nothing_but_has_a_header_is_refused(self):
        self.public_header('Core/Version.h')
        self.private_file('core/Version.cpp', 'int placeholder = 0;\n')
        self.assertRejected('includes nothing')


class SelfCompileCoverageTests(EngineTreeCase):
    def test_a_public_header_without_a_self_compile_unit_is_refused(self):
        self.public_header('Core/Version.h', self_compile=False)
        self.assertRejected('has no self-compile unit')

    def test_a_self_compile_unit_without_a_public_header_is_refused(self):
        self.write(layers.SELF_COMPILE_DIR + '/CoreGhost.cpp',
                   '#include <AfterNightEngine/Core/Ghost.h>\n'
                   '#include <AfterNightEngine/Core/Ghost.h>\n')
        self.assertRejected('no public header corresponds to this self-compile unit')

    def test_a_self_compile_unit_that_includes_its_header_once_is_refused(self):
        self.public_header('Core/Version.h', self_compile=False)
        self.write(layers.SELF_COMPILE_DIR + '/CoreVersion.cpp',
                   '#include <AfterNightEngine/Core/Version.h>\n')
        self.assertRejected('exactly twice and nothing else')

    def test_a_self_compile_unit_that_includes_a_second_header_is_refused(self):
        self.public_header('Core/Version.h', self_compile=False)
        self.write(layers.SELF_COMPILE_DIR + '/CoreVersion.cpp',
                   '#include <AfterNightEngine/Core/Version.h>\n'
                   '#include <AfterNightEngine/Core/Version.h>\n'
                   '#include <cstdint>\n')
        self.assertRejected('exactly twice and nothing else')

    def test_a_missing_self_compile_directory_is_refused(self):
        shutil.rmtree(self.root / layers.SELF_COMPILE_DIR)
        self.assertRejected('the self-compile directory does not exist')

    def test_a_domain_header_maps_to_one_unit_name(self):
        self.assertEqual(layers.self_compile_unit_name('Algorithms/Registration/Params.h'),
                         'AlgorithmsRegistrationParams.cpp')


class RepositoryTests(unittest.TestCase):
    def test_the_repository_engine_passes(self):
        stream = io.StringIO()
        code = layers.main([], stream=stream)
        self.assertEqual(code, 0, stream.getvalue())


# One include spelling that identifies each adapted library, so the cases below can be written once
# and run for every adapter rather than for whichever one happens to be first in the table. A library
# with no entry here is an adapter whose rules nothing proves, which is why the cases assert the
# table covers the adapters.
ADAPTED_LIBRARY_HEADERS = {
    'gmic': 'gmic/gmic_libc.h',
    'gpu': 'CL/cl.h',
    'python': 'Python.h',
}

# A library no adapter declares, for the case that proves an adapter gets exactly one.
FOREIGN_LIBRARY_HEADER = 'opencv2/core.hpp'


class OptionalAdapterCases:
    """An optional adapter's own rules: its surface, its one library, and where each may appear.

    A mixin rather than a case class, because there is more than one adapter and the rules are the
    same for each: one concrete subclass per adapter follows, so a rule that holds for the first
    adapter and not the second is a failure rather than something nobody ran.
    """

    #: Which adapter's rules this subclass runs, by the library it declares.
    LIBRARY = ''

    def setUp(self):
        super().setUp()
        self.adapter = next(adapter for adapter in layers.OPTIONAL_COMPONENTS
                            if adapter.library == self.LIBRARY)
        self.library_header = ADAPTED_LIBRARY_HEADERS[self.LIBRARY]

    def adapter_public(self, below, body='', self_compile=True):
        self.write(self.adapter.public_dir + '/' + below,
                   PRAGMA + '\n\n' + body + '\nstruct AdapterMarker {};\n')
        if self_compile:
            include = self.adapter.prefix + below
            unit = layers.self_compile_unit_name(below)
            self.write(self.adapter.self_compile_dir + '/' + unit,
                       '#include <%s>\n#include <%s>\n' % (include, include))

    def adapter_private(self, below, body=''):
        self.write(self.adapter.private_dir + '/' + below, body)

    def own_header(self, below='Adapter.h'):
        return '#include <%s%s>\n' % (self.adapter.prefix, below)

    def test_a_public_header_over_the_engine_surface_passes(self):
        self.public_header('Core/Status.h')
        self.adapter_public('Adapter.h',
                            '#include <AfterNightEngine/Core/Status.h>\n#include <cstdint>\n')
        self.assertClean()

    def test_a_public_header_may_not_include_the_adapted_library(self):
        self.adapter_public('Adapter.h', '#include <%s>\n' % self.library_header)
        self.assertRejected('does not cross the %s\'s own published surface' % self.adapter.kind)

    def test_an_implementation_file_may_include_the_adapted_library(self):
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include <%s>\n' % self.library_header)
        self.assertClean()

    def test_an_implementation_file_may_not_include_another_library(self):
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include <%s>\n' % FOREIGN_LIBRARY_HEADER)
        self.assertRejected('this %s declares %s and nothing else' % (self.adapter.kind, self.LIBRARY))

    def test_an_implementation_file_may_not_include_the_other_adapters_library(self):
        # The rule that keeps two optional components from becoming one: each declares a library,
        # and another's is as foreign to it as a library none of them adapts.
        other = next(spelling for library, spelling in ADAPTED_LIBRARY_HEADERS.items()
                     if library != self.LIBRARY)
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include <%s>\n' % other)
        self.assertRejected('this %s declares %s and nothing else' % (self.adapter.kind, self.LIBRARY))

    def test_an_adapter_may_not_include_an_engine_implementation_header(self):
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include "runtime/EngineContextState.h"\n')
        self.assertRejected('names no implementation header of this %s' % self.adapter.kind)


    def test_a_public_header_naming_no_engine_header_fails(self):
        self.adapter_public('Adapter.h', '#include <AfterNightEngine/Core/NotThere.h>\n')
        self.assertRejected('names no public header of the engine')

    def test_a_public_adapter_header_needs_a_self_compile_unit(self):
        self.adapter_public('Adapter.h', self_compile=False)
        self.assertRejected('has no self-compile unit')

    def test_an_engine_header_may_not_include_an_adapter_header(self):
        self.adapter_public('Adapter.h')
        self.public_header('Interop/ExternalBuffer.h', self.own_header())
        self.assertRejected('belongs to %s' % self.adapter.name)


class GmicAdapterTests(OptionalAdapterCases, EngineTreeCase):
    """The optional G'MIC adapter's rules."""

    LIBRARY = 'gmic'

    def test_the_implementation_may_start_a_thread_with_a_stated_stack(self):
        # The library's interpreter needs more stack than a default Windows thread has, and
        # std::thread cannot state a size, so this adapter's implementation reaches the operating
        # system's thread API for the one call that can.
        self.adapter_public('Adapter.h')
        self.adapter_private('CallThread.cpp',
                             self.own_header() + '#include <Windows.h>\n#include <process.h>\n#include <pthread.h>\n')
        self.assertClean()

    def test_the_published_surface_may_not_name_a_thread_header(self):
        self.adapter_public('Adapter.h', '#include <pthread.h>\n')
        self.assertRejected('carries no operating-system header')


class PythonAdapterTests(OptionalAdapterCases, EngineTreeCase):
    """The optional Python adapter's rules."""

    LIBRARY = 'python'

    def test_the_implementation_may_not_include_a_thread_header(self):
        # The thread allowance belongs to the adapter whose library needs a stated stack, not to
        # every adapter.
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include <pthread.h>\n')
        self.assertRejected('this adapter declares python and nothing else')

    def test_the_implementation_may_not_include_a_gpu_sdk_header(self):
        # A GPU SDK is the opt-in backend's library and nobody else's, whichever tree reaches for it.
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include <CL/cl.h>\n')
        self.assertRejected('this adapter declares python and nothing else')


class OpenClBackendTests(OptionalAdapterCases, EngineTreeCase):
    """The opt-in OpenCL proof backend's rules: one GPU SDK, behind a surface that names none of it."""

    LIBRARY = 'gpu'

    def test_the_implementation_may_reach_the_operating_systems_loader(self):
        # The backend loads the OpenCL loader the graphics driver installed, from the system
        # directory, before it calls anything - which is the operating system's API, not the SDK's.
        self.adapter_public('Adapter.h')
        self.adapter_private('Loader.cpp', self.own_header() + '#include <Windows.h>\n')
        self.assertClean()

    def test_the_published_surface_may_not_name_an_operating_system_header(self):
        self.adapter_public('Adapter.h', '#include <Windows.h>\n')
        self.assertRejected('carries no operating-system header')

    def test_the_implementation_may_not_include_a_thread_header(self):
        # Its worker is a std::thread; the stated-stack allowance belongs to the G'MIC adapter alone.
        self.adapter_public('Adapter.h')
        self.adapter_private('Adapter.cpp', self.own_header() + '#include <pthread.h>\n')
        self.assertRejected('this backend declares gpu and nothing else')

    def test_the_backend_is_named_as_a_backend_in_what_it_is_refused(self):
        self.assertEqual(self.adapter.kind, 'backend')
        self.assertEqual(self.adapter.name, 'AfterNightEngineOpenClBackend')


class AdapterTableTests(unittest.TestCase):
    """Every optional component the checker knows has its rules run above."""

    def test_every_adapter_has_a_library_header_and_a_case_class(self):
        declared = {adapter.library for adapter in layers.OPTIONAL_COMPONENTS}
        self.assertEqual(declared, set(ADAPTED_LIBRARY_HEADERS))
        covered = {case.LIBRARY for case in (GmicAdapterTests, OpenClBackendTests, PythonAdapterTests)}
        self.assertEqual(declared, covered)
        self.assertNotIn(FOREIGN_LIBRARY_HEADER, ADAPTED_LIBRARY_HEADERS.values())

    def test_the_components_are_the_adapters_and_the_backends(self):
        self.assertEqual(layers.OPTIONAL_COMPONENTS, layers.OPTIONAL_ADAPTERS + layers.OPTIONAL_BACKENDS)
        self.assertEqual({adapter.kind for adapter in layers.OPTIONAL_ADAPTERS}, {'adapter'})
        self.assertEqual({backend.kind for backend in layers.OPTIONAL_BACKENDS}, {'backend'})


if __name__ == '__main__':
    unittest.main(verbosity=2)
