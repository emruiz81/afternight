# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The fixture runner must fail closed before executing a test against substituted input."""

import hashlib
from pathlib import Path
import tempfile
import unittest

from run_raw_codec_corpus import verify


class RawCorpusVerificationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.records = []
        for name in ('CANON', 'CANON_CHANNEL_BLACK', 'NIKON', 'XPRO1', 'XT50', 'FOVEON'):
            payload = name.encode()
            (self.root / name).write_bytes(payload)
            self.records.append({'environment': 'AFTERNIGHT_TEST_RAW_' + name, 'path': name,
                                 'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()})

    def test_complete_manifest_resolves_all_paths(self):
        self.assertEqual(len(verify(self.root, self.records)), 6)

    def test_same_size_substitution_is_refused(self):
        (self.root / 'CANON').write_bytes(b'XXXXX')
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            verify(self.root, self.records)

    def test_truncation_is_refused(self):
        (self.root / 'CANON').write_bytes(b'X')
        with self.assertRaisesRegex(ValueError, 'identity mismatch'):
            verify(self.root, self.records)

    def test_escape_is_refused_before_reading(self):
        self.records[0]['path'] = '../outside'
        with self.assertRaisesRegex(ValueError, 'escapes'):
            verify(self.root, self.records)

    def test_missing_camera_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'six'):
            verify(self.root, self.records[:-1])

    def test_duplicate_environment_is_refused(self):
        self.records[1]['environment'] = self.records[0]['environment']
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            verify(self.root, self.records)


if __name__ == '__main__':
    unittest.main()
