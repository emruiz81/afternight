# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Regression checks for the private RAW qualification package selection."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'build/scripts/tools/rawspeed'))
from check_runtime import private_raw_symbols, unexpected_raw_dependencies


class PrivateSymbolTests(unittest.TestCase):
    def test_private_exports_are_refused_by_owner(self):
        names = ['libraw_version', 'anRsDecode', 'anLibRawIdentify', 'jpeg_read_header',
                 'png_create_read_struct', 'inflate', 'deflateEnd', 'LibRaw::unpack()',
                 'LibRaw_buffer_datastream::read(void*, unsigned long, unsigned long)',
                 'rawspeed::RawDecoder::decodeRaw()', 'vtable for LibRaw',
                 'typeinfo name for LibRaw_buffer_datastream', 'non-virtual thunk to LibRaw::unpack()']
        symbols = '\n'.join(f'00000000 T {name}' for name in names)
        self.assertEqual(private_raw_symbols(symbols), names)

    def test_engine_function_parameters_do_not_look_like_private_exports(self):
        symbols = ('00000000 T an::rawMetadata(LibRaw const&, bool, bool)\n'
                   '00000000 T an::rawFullColor(LibRaw const&)\n'
                   '00000000 T an::decode(rawspeed::RawDecoder&)\n'
                   '00000000 W png_text_struct* std::vector<png_text_struct>::data()\n'
                   '00000000 W LibRaw_buffer_datastream* std::get<LibRaw_buffer_datastream*>()\n')
        self.assertEqual(private_raw_symbols(symbols), [])


class WithheldObserverTests(unittest.TestCase):
    """A golden may withhold a third party's address; the stock oracle still checks what the engine reads."""

    def setUp(self):
        import qualify
        self.qualify = qualify

    def test_a_withheld_observer_matches_whatever_the_engine_read(self):
        golden = {'artist': self.qualify.WITHHELD, 'camera': {'black': 1}}
        actual = {'artist': 'someone@example.test', 'camera': {'black': 1}}
        self.assertEqual(self.qualify.pinned_metadata(actual, golden), golden)
        self.assertEqual(actual['artist'], 'someone@example.test')

    def test_the_rest_of_a_withheld_golden_is_still_pinned(self):
        golden = {'artist': self.qualify.WITHHELD, 'camera': {'black': 1}}
        self.assertNotEqual(self.qualify.pinned_metadata({'artist': 'x', 'camera': {'black': 2}}, golden), golden)

    def test_an_observer_the_golden_states_is_compared_as_it_is(self):
        golden = {'artist': 'A. Person'}
        self.assertEqual(self.qualify.pinned_metadata({'artist': 'A. Person'}, golden), golden)
        self.assertNotEqual(self.qualify.pinned_metadata({'artist': 'B. Person'}, golden), golden)

    def test_the_goldens_publish_no_address(self):
        text = (REPO / 'docs/dev/rawspeed-goldens.json').read_text(encoding='utf-8')
        self.assertNotRegex(text, r'[\w.+-]+@[\w-]+\.[A-Za-z]{2,}')


class DirectDependencyTests(unittest.TestCase):
    """What the engine itself names, which is the only thing this check can hold it to."""

    DYNAMIC = (' 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]\n'
               ' 0x0000000000000001 (NEEDED)             Shared library: [libtbb.so.12]\n'
               ' 0x0000000000000001 (NEEDED)             Shared library: [libavcodec.so.62]\n'
               ' 0x000000000000000f (RPATH)              Library rpath: [$ORIGIN]\n')

    def test_a_private_raw_or_openmp_library_the_engine_names_is_refused(self):
        for library in ('libraw_r.so.23', 'libgomp.so.1', 'libomp.so.5', 'libiomp5.so'):
            dynamic = self.DYNAMIC + (' 0x0000000000000001 (NEEDED)             '
                                      'Shared library: [%s]\n' % library)
            self.assertEqual(unexpected_raw_dependencies(dynamic), [library])

    def test_openmp_reached_through_another_library_is_not_the_engines_dependency(self):
        """The distribution's FFmpeg pulls libgomp in through libsoxr. It is in the engine's
        closure and not in its dynamic section, and the RAW contract is about what RAW links."""
        self.assertEqual(unexpected_raw_dependencies(self.DYNAMIC), [])


@unittest.skipUnless(shutil.which('cmake'), 'CMake is required for package-selection tests')
class PrivatePackageTests(unittest.TestCase):
    def test_instrumented_prefix_overrides_cached_ordinary_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            instrumented, ordinary = root / 'instrumented', root / 'ordinary'
            for prefix in (instrumented, ordinary):
                for package, port, target in (
                    ('AfterNightLibRaw', 'afternight-libraw', 'afternight_libraw_r'),
                    ('AfterNightRawSpeed', 'afternight-rawspeed', 'afternight_rawspeed'),
                ):
                    directory = prefix / 'share' / port
                    directory.mkdir(parents=True)
                    body = f'add_library(AfterNight::{target} INTERFACE IMPORTED)\n'
                    if package == 'AfterNightLibRaw':
                        body += 'include(CMakeFindDependencyMacro)\nfind_dependency(AfterNightRawSpeed CONFIG)\n'
                    if prefix == ordinary:
                        body = 'message(FATAL_ERROR "Ordinary private package selected")\n'
                    (directory / f'{package}Config.cmake').write_text(body, encoding='utf-8')
            result = subprocess.run([
                'cmake', '-S', str(REPO / 'build/scripts/tools/rawspeed'), '-B', str(root / 'build'),
                '-DAN_ENGINE_DRIVERS=OFF', f'-DAN_PRIVATE_PREFIX={instrumented}',
                f'-DCMAKE_PREFIX_PATH={ordinary}',
                f'-DAfterNightLibRaw_DIR={ordinary}/share/afternight-libraw',
                f'-DAfterNightRawSpeed_DIR={ordinary}/share/afternight-rawspeed',
            ], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # An incomplete instrumentation tree must fail rather than select an ordinary package.
            (instrumented / 'share/afternight-rawspeed/AfterNightRawSpeedConfig.cmake').unlink()
            result = subprocess.run([
                'cmake', '-S', str(REPO / 'build/scripts/tools/rawspeed'), '-B', str(root / 'build'),
            ], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Missing private qualification package', result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
