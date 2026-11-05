# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Deployment refuses incomplete or conflicting runtime closures before copying any file."""
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import deploy_raw_deps as deploy


def pe(*imports):
    data = bytearray(2048); data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128); data[128:132] = b'PE\0\0'
    struct.pack_into('<HH', data, 132, 0x8664, 1)
    struct.pack_into('<H', data, 148, 240); struct.pack_into('<H', data, 152, 0x20b)
    struct.pack_into('<IIII', data, 400, 1536, 4096, 1536, 512)
    struct.pack_into('<II', data, 272, 4096, 20 * (len(imports) + 1))
    cursor = 512 + 20 * (len(imports) + 1)
    for index, name in enumerate(imports):
        struct.pack_into('<I', data, 512 + index * 20 + 12, cursor - 512 + 4096)
        encoded = name.encode('ascii') + b'\0'; data[cursor:cursor + len(encoded)] = encoded
        cursor += len(encoded)
    return bytes(data)


class RawDeploymentTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'vcpkg/installed/x64-windows-raw/bin'; self.source.mkdir(parents=True)
        self.destination = self.root / 'deployed'
        self.addCleanup(patch.stopall)
        patch.object(deploy, 'REPO', self.root).start()
        patch.dict(os.environ, {'AFTERNIGHT_RAWSPEED': '1'}).start()
        (self.source / 'afternight_libraw_r.dll').write_bytes(pe('afternight_rawspeed.dll'))
        (self.source / 'afternight_rawspeed.dll').write_bytes(pe('kernel32.dll'))

    def test_complete_private_pair(self):
        self.assertEqual(len(deploy.deploy('release', 'msvc', self.destination)), 2)

    def test_missing_runtime_is_not_native_only(self):
        (self.source / 'afternight_rawspeed.dll').unlink()
        with self.assertRaises(FileNotFoundError): deploy.deploy('release', 'msvc', self.destination)
        self.assertFalse(self.destination.exists())

    def test_openmp_and_stock_alias_are_refused(self):
        for dependency in ('libomp.dll', 'raw_r.dll', 'unknown-jpeg.dll'):
            (self.source / 'afternight_rawspeed.dll').write_bytes(pe(dependency))
            with self.assertRaises((ValueError, FileNotFoundError)): deploy.deploy('release', 'msvc', self.destination)
            self.assertFalse(self.destination.exists())

    def test_conflicting_shared_transitive_is_refused(self):
        (self.source / 'afternight_rawspeed.dll').write_bytes(pe('zlib.dll'))
        (self.source / 'zlib.dll').write_bytes(pe('kernel32.dll'))
        self.destination.mkdir(); (self.destination / 'zlib.dll').write_bytes(b'competing version')
        with self.assertRaises(ValueError): deploy.deploy('release', 'msvc', self.destination)
        self.assertEqual(list(self.destination.iterdir()), [self.destination / 'zlib.dll'])

    def test_explicit_native_only_skips_packages(self):
        with patch.dict(os.environ, {'AFTERNIGHT_RAWSPEED': '0'}):
            self.assertEqual(deploy.deploy('release', 'msvc', self.destination), [])
        self.assertFalse(self.destination.exists())


if __name__ == '__main__': unittest.main()
