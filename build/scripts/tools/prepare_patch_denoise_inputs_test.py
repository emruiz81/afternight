# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for the guided patch denoise's capture inputs: the derivation, the pins and a whole run."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

import prepare_patch_denoise_inputs as ppd  # noqa: E402


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


class CorpusFixture:
    """A corpus root holding one UInt16 capture as a verified raw dump, a public frame the derived
    manifest has to keep, and the tables that pin them."""

    def __init__(self, root, width=12, height=10):
        self.root = Path(root)
        dumps = self.root / "RawDumps"
        dumps.mkdir(parents=True)
        codes = (np.arange(width * height, dtype=np.uint32) * 977 % 65536).astype("<u2")
        codes[0], codes[1] = 0, 65535
        payload = dumps / "capture.anraw"
        payload.write_bytes(codes.tobytes())
        self.shape = {"width": width, "height": height, "channels": 1, "sampleType": "UInt16", "layout": "Planar",
                      "byteOrder": "Little", "rowStrideBytes": 0, "planeStrideBytes": 0, "cfaPattern": ""}
        descriptor = dumps / "capture.json"
        descriptor.write_text(json.dumps({"schemaVersion": 1, "descriptor": self.shape, "payloadFile": payload.name,
                                          "payloadBytes": payload.stat().st_size, "payloadDigest": sha(payload),
                                          "cards": []}))
        public = dumps / "public.json"
        public.write_text("{}")
        self.unit = (codes.astype("<f4") / np.float32(65535)).reshape(height, width)
        self.manifest = self.root / "RawDumps/corpus.json"
        self.entries = [
            {"id": "maintainer.capture", "relativePath": "RawDumps/capture.json", "sha256": sha(descriptor),
             "sizeBytes": descriptor.stat().st_size, "expected": self.shape, "licenseOrProvenance": "maintainer",
             "redistribution": "local-only", "privacyClass": "none", "tags": ["mono"], "preprocessing": "dump",
             "preprocessingVersion": "1", "goldenPath": "", "goldenSha256": "", "goldenTolerance": 0.0},
            {"id": "public.frame", "relativePath": "RawDumps/public.json", "sha256": sha(public), "sizeBytes": 2,
             "expected": {}}]
        self.manifest.write_text(json.dumps({"schemaVersion": 1, "datasets": self.entries, "frameSets": []}))
        self.captures = {"capture": ("maintainer.capture", width, height, sha(descriptor), sha(payload),
                                     sha_bytes(self.unit.tobytes()))}
        crop = np.ascontiguousarray(self.unit[3:7, 2:6])
        self.datasets = {
            "test.capture.full.v1": ("capture", 0, 0, width, height, sha_bytes(self.unit.tobytes())),
            "test.capture.crop4.v1": ("capture", 2, 3, 4, 4, sha_bytes(crop.tobytes()))}
        self.crop = crop

    def prepare(self, out=None, **overrides):
        options = {"captures": self.captures, "datasets": self.datasets}
        options.update(overrides)
        return ppd.prepare(self.root, self.manifest, out or self.root / "Prepared/patch", **options)


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="patch-denoise-inputs"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.corpus = CorpusFixture(self.dir / "corpus")

    def test_a_capture_is_divided_by_the_full_scale_and_cropped_into_pinned_datasets(self):
        out = self.corpus.prepare()
        manifest = json.loads((out / "corpus.json").read_text())
        self.assertEqual(manifest["datasets"][:2], self.corpus.entries, "the original entries stay as they were")
        derived = {entry["id"]: entry for entry in manifest["datasets"][2:]}
        self.assertEqual(sorted(derived), sorted(self.corpus.datasets))
        for identity, (_, x0, y0, width, height, _) in self.corpus.datasets.items():
            entry = derived[identity]
            descriptor = self.corpus.root / entry["relativePath"]
            self.assertTrue(entry["relativePath"].startswith("Prepared/patch/"), entry["relativePath"])
            self.assertEqual(entry["sha256"], sha(descriptor))
            self.assertEqual(entry["sizeBytes"], descriptor.stat().st_size)
            self.assertEqual(entry["expected"], dict(self.corpus.shape, sampleType="Float32", width=width,
                                                     height=height))
            self.assertEqual(entry["licenseOrProvenance"], "maintainer")
            raw = json.loads(descriptor.read_text())
            self.assertEqual(raw["descriptor"], entry["expected"])
            samples = np.fromfile(descriptor.parent / raw["payloadFile"], dtype="<f4").reshape(height, width)
            np.testing.assert_array_equal(samples, self.corpus.unit[y0:y0 + height, x0:x0 + width])
            self.assertEqual(raw["payloadDigest"], sha(descriptor.parent / raw["payloadFile"]))
            self.assertEqual(raw["payloadBytes"], width * height * 4)
        self.assertEqual(float(np.fromfile(out / "test.capture.full.v1.f32", dtype="<f4")[1]), 1.0)
        inputs = json.loads((out / "inputs.json").read_text())
        self.assertEqual(inputs["corpusManifestSha256"], sha(out / "corpus.json"))
        self.assertEqual(inputs["sourceManifestSha256"], sha(self.corpus.manifest))
        self.assertEqual(inputs["datasets"]["test.capture.crop4.v1"]["origin"], [2, 3])

    def test_a_capture_that_is_not_the_pinned_one_is_refused(self):
        payload = self.corpus.root / "RawDumps/capture.anraw"
        data = bytearray(payload.read_bytes())
        data[4] ^= 1
        payload.write_bytes(bytes(data))
        with self.assertRaisesRegex(ValueError, "capture payload"):
            self.corpus.prepare()
        self.assertFalse((self.corpus.root / "Prepared/patch").exists(), "nothing is written for a refused source")

    def test_a_descriptor_that_is_not_the_pinned_one_is_refused(self):
        descriptor = self.corpus.root / "RawDumps/capture.json"
        descriptor.write_text(descriptor.read_text() + " ")
        with self.assertRaisesRegex(ValueError, "capture descriptor"):
            self.corpus.prepare()

    def test_a_derived_frame_that_is_not_the_pinned_one_is_refused(self):
        moved = dict(self.corpus.datasets)
        _, x0, y0, width, height, pinned = moved["test.capture.crop4.v1"]
        moved["test.capture.crop4.v1"] = ("capture", x0 + 1, y0, width, height, pinned)
        with self.assertRaisesRegex(ValueError, "test.capture.crop4.v1"):
            self.corpus.prepare(datasets=moved)

    def test_a_crop_that_leaves_its_capture_is_refused(self):
        moved = dict(self.corpus.datasets)
        _, _, y0, width, height, pinned = moved["test.capture.crop4.v1"]
        moved["test.capture.crop4.v1"] = ("capture", 9, y0, width, height, pinned)
        with self.assertRaisesRegex(ValueError, "outside"):
            self.corpus.prepare(datasets=moved)

    def test_the_output_must_be_new_and_under_the_corpus_root(self):
        with self.assertRaisesRegex(ValueError, "under the corpus root"):
            self.corpus.prepare(out=self.dir / "elsewhere")
        existing = self.corpus.root / "Prepared/existing"
        existing.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.corpus.prepare(out=existing)

    def test_a_manifest_that_already_names_a_dataset_is_refused(self):
        document = json.loads(self.corpus.manifest.read_text())
        document["datasets"].append({"id": "test.capture.full.v1", "relativePath": "x"})
        self.corpus.manifest.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, "already names"):
            self.corpus.prepare()

    def test_the_committed_tables_derive_every_dataset_from_a_pinned_capture(self):
        # The full frames are the stretch rows' own inputs byte for byte, so their pins are shared.
        self.assertEqual(set(ppd.CAPTURES), {"eagle", "carina"})
        for identity, (capture, x0, y0, width, height, pinned) in ppd.DATASETS.items():
            _, full_width, full_height, _, _, unit = ppd.CAPTURES[capture]
            self.assertLessEqual(x0 + width, full_width, identity)
            self.assertLessEqual(y0 + height, full_height, identity)
            if (width, height) == (full_width, full_height):
                self.assertEqual(pinned, unit, identity)
            else:
                self.assertEqual((width, height), (512, 512), identity)
                self.assertEqual((x0, y0), ((full_width - 512) // 2, (full_height - 512) // 2), identity)
        self.assertEqual(len(ppd.DATASETS), 4)


if __name__ == "__main__":
    unittest.main()
