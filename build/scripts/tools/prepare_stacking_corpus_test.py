# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Refusal checks for local stack-corpus preparation; no capture data is needed."""

import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_stacking_corpus as recipe


class PrepareStackingCorpusTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="stacking-corpus-test-")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        source = root / "captures"
        source.mkdir()
        frames = []
        for index in range(8):
            payload = bytes([index])
            name = f"flat_{index}.fits"
            (source / name).write_bytes(payload)
            frames.append(dict(relativePath=name, sizeBytes=len(payload),
                               sha256=hashlib.sha256(payload).hexdigest()))
        self.frames = frames
        self.manifest = dict(frameSets=[dict(id="carina-asi294mm-red", licenseOrProvenance="test",
                                             roles=[dict(role="flats", frames=frames)])])
        self.args = argparse.Namespace(source_root=source, source_manifest=root / "source.json",
                                       output_root=root / "output", runner=root / "runner.exe",
                                       engine=root / "AfterNightEngine.dll")
        self.args.runner.write_bytes(b"test runner")
        self.args.engine.write_bytes(b"test library")

    def refuse_before_decode(self, reason):
        self.args.source_manifest.write_text(json.dumps(self.manifest))
        with patch.object(recipe.subprocess, "run") as decode:
            with self.assertRaisesRegex(ValueError, reason):
                recipe.prepare(self.args)
            decode.assert_not_called()
        self.assertFalse((self.args.output_root / "corpus.json").exists())

    def test_refuses_incomplete_frame_set(self):
        self.frames.pop()
        self.refuse_before_decode("eight distinct")

    def test_refuses_duplicate_captures(self):
        self.frames[1] = dict(self.frames[0])
        self.refuse_before_decode("eight distinct")

    def test_refuses_modified_capture_before_invoking_decoder(self):
        (self.args.source_root / "flat_0.fits").write_bytes(b"modified")
        self.refuse_before_decode("size or digest mismatch")

    def test_refuses_capture_outside_source_root(self):
        outside = self.args.source_root.parent / "outside.fits"
        outside.write_bytes(b"x")
        self.frames[0]["relativePath"] = "../outside.fits"
        self.refuse_before_decode("leaves the source root")

    def test_refuses_library_from_another_deployment(self):
        other = self.args.source_root.parent / "other"
        other.mkdir()
        self.args.engine = other / "AfterNightEngine.dll"
        self.args.engine.write_bytes(b"different library")
        self.refuse_before_decode("deployed beside")

    def test_refuses_decoder_representation_drift(self):
        self.args.source_manifest.write_text(json.dumps(self.manifest))

        def decode(command, **kwargs):
            Path(command[-1]).write_text(json.dumps(dict(descriptor=dict(
                width=8288, height=5644, channels=1, sampleType="Float32", layout="Planar", byteOrder="Little"))))

        with patch.object(recipe.subprocess, "run", side_effect=decode):
            with self.assertRaisesRegex(ValueError, "unexpected decoded representation"):
                recipe.prepare(self.args)
        self.assertFalse((self.args.output_root / "corpus.json").exists())


if __name__ == "__main__":
    unittest.main()
