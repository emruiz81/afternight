# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for the colour conversion's capture input: the derivation, the pins and a whole run."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

import prepare_color_inputs as pci  # noqa: E402


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def interpolated(unit, sites):
    """One colour at every pixel, written out pixel by pixel: the [1, 2, 1] x [1, 2, 1] weighted mean of
    the colour's sites in the 3 x 3 neighbourhood, a neighbour outside the frame replaced by the nearest
    edge pixel, in Float32 and in the order the script sums."""
    height, width = unit.shape
    weights = (np.float32(1), np.float32(2), np.float32(1))
    out = np.zeros_like(unit)
    for y in range(height):
        for x in range(width):
            numerator = []
            denominator = []
            for dy in (-1, 0, 1):
                row_values = []
                row_counts = []
                yy = min(max(y + dy, 0), height - 1)
                for dx in (-1, 0, 1):
                    xx = min(max(x + dx, 0), width - 1)
                    site = np.float32((yy % 2, xx % 2) in sites)
                    row_values.append(unit[yy, xx] * site)
                    row_counts.append(site)
                numerator.append(row_values[0] + weights[1] * row_values[1] + row_values[2])
                denominator.append(row_counts[0] + weights[1] * row_counts[1] + row_counts[2])
            out[y, x] = ((numerator[0] + weights[1] * numerator[1] + numerator[2]) /
                         (denominator[0] + weights[1] * denominator[1] + denominator[2]))
    return out


class CorpusFixture:
    """A corpus root holding one RGGB UInt16 capture as a verified raw dump, another dataset the derived
    manifest has to keep, and the tables that pin them."""

    def __init__(self, root, width=10, height=8):
        self.root = Path(root)
        dumps = self.root / "RawDumps"
        dumps.mkdir(parents=True)
        codes = (np.arange(width * height, dtype=np.uint32) * 977 % 65536).astype("<u2")
        codes[0], codes[1] = 0, 65535
        payload = dumps / "capture.anraw"
        payload.write_bytes(codes.tobytes())
        self.shape = {"width": width, "height": height, "channels": 1, "sampleType": "UInt16", "layout": "Planar",
                      "byteOrder": "Little", "rowStrideBytes": 0, "planeStrideBytes": 0, "cfaPattern": "RGGB"}
        descriptor = dumps / "capture.json"
        descriptor.write_text(json.dumps({"schemaVersion": 1, "descriptor": self.shape, "payloadFile": payload.name,
                                          "payloadBytes": payload.stat().st_size, "payloadDigest": sha(payload),
                                          "cards": []}))
        other = dumps / "other.json"
        other.write_text("{}")
        self.unit = (codes.astype("<f4") / np.float32(65535)).reshape(height, width)
        self.manifest = self.root / "RawDumps/corpus.json"
        self.entries = [
            {"id": "maintainer.capture", "relativePath": "RawDumps/capture.json", "sha256": sha(descriptor),
             "sizeBytes": descriptor.stat().st_size, "expected": self.shape, "licenseOrProvenance": "maintainer",
             "redistribution": "local-only", "privacyClass": "none", "tags": ["osc", "cfa"], "preprocessing": "dump",
             "preprocessingVersion": "1", "goldenPath": "", "goldenSha256": "", "goldenTolerance": 0.0},
            {"id": "other.frame", "relativePath": "RawDumps/other.json", "sha256": sha(other), "sizeBytes": 2,
             "expected": {}}]
        self.manifest.write_text(json.dumps({"schemaVersion": 1, "datasets": self.entries, "frameSets": []}))
        self.linear = [interpolated(self.unit, sites) for sites in pci.RGGB_SITES]
        self.rgb = np.stack([pci.encode_srgb(plane) for plane in self.linear]).astype("<f4")
        self.captures = {"capture": ("maintainer.capture", width, height, sha(descriptor), sha(payload))}
        self.datasets = {"test.capture.srgb.v1": ("capture", sha_bytes(self.rgb.tobytes()))}

    def prepare(self, out=None, **overrides):
        options = {"captures": self.captures, "datasets": self.datasets}
        options.update(overrides)
        return pci.prepare(self.root, self.manifest, out or self.root / "Prepared/color", **options)


class DerivationTest(unittest.TestCase):
    def test_each_colour_is_the_weighted_mean_of_its_own_sites_with_replicated_edges(self):
        corpus = CorpusFixture(Path(tempfile.mkdtemp(prefix="color-inputs")))
        self.addCleanup(shutil.rmtree, corpus.root, True)
        for sites, expected in zip(pci.RGGB_SITES, corpus.linear):
            np.testing.assert_array_equal(pci.interpolate(corpus.unit, sites), expected)
        # No other red or blue site lies within one pixel of one, so each keeps its own value; a green
        # site's diagonal neighbours are green too, so it does not.
        red = pci.interpolate(corpus.unit, pci.RGGB_SITES[0])
        blue = pci.interpolate(corpus.unit, pci.RGGB_SITES[2])
        self.assertEqual(float(red[0, 0]), float(corpus.unit[0, 0]))
        self.assertEqual(float(red[2, 4]), float(corpus.unit[2, 4]))
        self.assertEqual(float(blue[3, 5]), float(corpus.unit[3, 5]))
        self.assertNotEqual(float(pci.interpolate(corpus.unit, pci.RGGB_SITES[1])[0, 1]), float(corpus.unit[0, 1]))

    def test_the_encoding_is_the_srgb_transfer_function_with_its_linear_segment(self):
        linear = np.array([0.0, 0.0031308, 0.0031309, 0.18, 1.0], dtype=np.float32)
        encoded = pci.encode_srgb(linear)
        self.assertEqual(encoded.dtype, np.float32)
        self.assertEqual(float(encoded[0]), 0.0)
        self.assertEqual(float(encoded[1]), float(np.float32(12.92) * np.float32(0.0031308)))
        reference = [0.0, 12.92 * 0.0031308, 1.055 * 0.0031309 ** (1 / 2.4) - 0.055, 1.055 * 0.18 ** (1 / 2.4) - 0.055,
                     1.0]
        np.testing.assert_allclose(encoded, np.array(reference), rtol=0, atol=2e-7)


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="color-inputs"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.corpus = CorpusFixture(self.dir / "corpus")

    def test_a_capture_is_interpolated_encoded_and_written_as_a_pinned_rgb_dataset(self):
        out = self.corpus.prepare()
        manifest = json.loads((out / "corpus.json").read_text())
        self.assertEqual(manifest["datasets"][:2], self.corpus.entries, "the original entries stay as they were")
        self.assertEqual(len(manifest["datasets"]), 3)
        entry = manifest["datasets"][2]
        self.assertEqual(entry["id"], "test.capture.srgb.v1")
        descriptor = self.corpus.root / entry["relativePath"]
        self.assertTrue(entry["relativePath"].startswith("Prepared/color/"), entry["relativePath"])
        self.assertEqual(entry["sha256"], sha(descriptor))
        self.assertEqual(entry["sizeBytes"], descriptor.stat().st_size)
        shape = dict(self.corpus.shape, channels=3, sampleType="Float32", cfaPattern="")
        self.assertEqual(entry["expected"], shape)
        self.assertEqual(entry["licenseOrProvenance"], "maintainer")
        self.assertIn("rgb", entry["tags"])
        self.assertNotIn("cfa", entry["tags"])
        raw = json.loads(descriptor.read_text())
        self.assertEqual(raw["descriptor"], shape)
        samples = np.fromfile(descriptor.parent / raw["payloadFile"], dtype="<f4").reshape(3, *self.corpus.unit.shape)
        np.testing.assert_array_equal(samples, self.corpus.rgb)
        self.assertEqual(raw["payloadDigest"], sha(descriptor.parent / raw["payloadFile"]))
        self.assertEqual(raw["payloadBytes"], self.corpus.rgb.nbytes)
        inputs = json.loads((out / "inputs.json").read_text())
        self.assertEqual(inputs["corpusManifestSha256"], sha(out / "corpus.json"))
        self.assertEqual(inputs["sourceManifestSha256"], sha(self.corpus.manifest))
        self.assertEqual(inputs["datasets"]["test.capture.srgb.v1"]["payloadSha256"],
                         sha(descriptor.parent / raw["payloadFile"]))

    def test_a_capture_that_is_not_the_pinned_one_is_refused(self):
        payload = self.corpus.root / "RawDumps/capture.anraw"
        data = bytearray(payload.read_bytes())
        data[4] ^= 1
        payload.write_bytes(bytes(data))
        with self.assertRaisesRegex(ValueError, "capture payload"):
            self.corpus.prepare()
        self.assertFalse((self.corpus.root / "Prepared/color").exists(), "nothing is written for a refused source")

    def test_a_descriptor_that_is_not_the_pinned_one_is_refused(self):
        descriptor = self.corpus.root / "RawDumps/capture.json"
        descriptor.write_text(descriptor.read_text() + " ")
        with self.assertRaisesRegex(ValueError, "capture descriptor"):
            self.corpus.prepare()

    def test_a_capture_of_another_mosaic_is_refused(self):
        descriptor = self.corpus.root / "RawDumps/capture.json"
        document = json.loads(descriptor.read_text())
        document["descriptor"]["cfaPattern"] = "GRBG"
        descriptor.write_text(json.dumps(document))
        captures = {"capture": self.corpus.captures["capture"][:3] + (sha(descriptor),) +
                    self.corpus.captures["capture"][4:]}
        entries = json.loads(self.corpus.manifest.read_text())
        entries["datasets"][0]["sha256"] = sha(descriptor)
        self.corpus.manifest.write_text(json.dumps(entries))
        with self.assertRaisesRegex(ValueError, "capture shape"):
            self.corpus.prepare(captures=captures)

    def test_a_derived_frame_that_is_not_the_pinned_one_is_refused(self):
        with self.assertRaisesRegex(ValueError, "test.capture.srgb.v1"):
            self.corpus.prepare(datasets={"test.capture.srgb.v1": ("capture", "0" * 64)})
        self.assertFalse((self.corpus.root / "Prepared/color").exists())

    def test_the_output_must_be_new_and_under_the_corpus_root(self):
        with self.assertRaisesRegex(ValueError, "under the corpus root"):
            self.corpus.prepare(out=self.dir / "elsewhere")
        existing = self.corpus.root / "Prepared/existing"
        existing.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.corpus.prepare(out=existing)

    def test_a_manifest_that_already_names_the_dataset_is_refused(self):
        document = json.loads(self.corpus.manifest.read_text())
        document["datasets"].append({"id": "test.capture.srgb.v1", "relativePath": "x"})
        self.corpus.manifest.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, "already names"):
            self.corpus.prepare()

    def test_the_committed_tables_pin_the_real_data_audits_capture_and_its_rgb(self):
        # The audit's provenance record states these four digests for the frame it measured.
        self.assertEqual(pci.CAPTURES, {"ngc253": (
            "ngc253.qhy183c.light", 5544, 3684, "e273c54bf5edf182dcb9166821d03827d1d151ef770886c92bf89a79e053903f",
            "98a327aae369c6484ccb914dcc90a27b451cf3c84677c8d37d83e708c65ef1e1")})
        self.assertEqual(pci.DATASETS, {"color.ngc253.srgb.f32.v1": (
            "ngc253", "82536f481dbc53bb2bdb8839e0309bdc23eabefb3f497a0a356c7215b62565c0")})


if __name__ == "__main__":
    unittest.main()
