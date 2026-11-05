# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for the final stack's input preparation: ordering, normalization, the frame set and a whole run."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

import prepare_final_stack_inputs as pfs  # noqa: E402


def card(keyword, value=None):
    text = keyword.ljust(8)
    if value is not None:
        text += "= " + str(value).rjust(20)
    return text.ljust(80)


def write_fits(path, values, width, height, bitpix=-32, extra=(), padding=b"\0"):
    """A primary image of big-endian samples, with `extra` cards before END."""
    cards = [card("SIMPLE", "T"), card("BITPIX", bitpix), card("NAXIS", 2), card("NAXIS1", width),
             card("NAXIS2", height), *extra, card("END")]
    header = "".join(cards).encode("ascii")
    header += b" " * ((2880 - len(header) % 2880) % 2880)
    data = np.asarray(values, dtype=">f4" if bitpix == -32 else ">i2").tobytes()
    data += padding * ((2880 - len(data) % 2880) % 2880)
    Path(path).write_bytes(header + data)
    return len(header)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class OrderAndFormatTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="final-stack-inputs"))
        self.addCleanup(shutil.rmtree, self.dir, True)

    def test_aligned_lights_are_ordered_by_the_last_number_in_their_names(self):
        pipeline = self.dir / "pipeline"
        pipeline.mkdir()
        for index in (10, 2, 0, 1):
            (pipeline / ("light_%d_reg.fits" % index)).write_bytes(b"x")
        self.assertEqual([f.name for f in pfs.aligned_frames(pipeline, 4)],
                         ["light_0_reg.fits", "light_1_reg.fits", "light_2_reg.fits", "light_10_reg.fits"])
        captures = self.dir / "captures"
        captures.mkdir()
        names = ["2026-03-23_21-09-03_Red_-9.40_120.00s_0001_cal_reg.fits",
                 "2026-03-23_21-07-03_Red_-10.00_120.00s_0000_cal_reg.fits",
                 "2026-03-23_23-44-32_Red_-10.00_120.00s_0010_cal_reg.fits"]
        for name in names:
            (captures / name).write_bytes(b"x")
        self.assertEqual([f.name for f in pfs.aligned_frames(captures, 3)], [names[1], names[0], names[2]])
        with self.assertRaises(ValueError):
            pfs.aligned_frames(pipeline, 5)
        (pipeline / "copy_2_reg.fits").write_bytes(b"x")
        with self.assertRaises(ValueError):
            pfs.aligned_frames(pipeline, 5)

    def test_a_light_is_divided_by_the_full_scale_once_with_its_header_and_padding_kept(self):
        # The last three round differently when multiplied by the reciprocal in single precision.
        values = [0.0, 1.0, 65535.0, -346707.875, 1007415.875, 1489.04296875, 8415.9892578125, 45439.40234375,
                  30655.77734375]
        source = self.dir / "light.fits"
        extra = [card("COMMENT%d" % index) for index in range(40)]
        length = write_fits(source, values, 3, 3, extra=extra, padding=b"\x07")
        self.assertGreater(length, 2880, "a header of more than one block")
        target = self.dir / "normalized.fits"
        pfs.normalize_frame(source, target)
        raw, normalized = source.read_bytes(), target.read_bytes()
        self.assertEqual(len(raw), len(normalized))
        self.assertEqual(raw[:length], normalized[:length])
        self.assertEqual(raw[length + 36:], normalized[length + 36:])
        written = np.frombuffer(normalized, dtype=">f4", count=9, offset=length)
        expected = (np.asarray(values, dtype=np.float64) / 65535.0).astype(np.float32)
        self.assertTrue(np.array_equal(written.astype(np.float32), expected))
        self.assertEqual(written[0], 0.0, "a zero stays the no-data value")
        self.assertEqual(written[2], 1.0)

        for name, change in (("nan", lambda v: v[:3] + [float("nan")] + v[4:]),):
            bad = self.dir / (name + ".fits")
            write_fits(bad, change(values), 3, 3)
            with self.assertRaises(ValueError):
                pfs.normalize_frame(bad, self.dir / (name + "-out.fits"))
        integer = self.dir / "integer.fits"
        write_fits(integer, [1, 2, 3, 4, 5, 6], 3, 2, bitpix=16)
        with self.assertRaises(ValueError):
            pfs.normalize_frame(integer, self.dir / "integer-out.fits")
        zeroed = self.dir / "zeroed.fits"
        write_fits(zeroed, values, 3, 3, extra=[card("BZERO", 1)])
        with self.assertRaises(ValueError):
            pfs.normalize_frame(zeroed, self.dir / "zeroed-out.fits")
        short = self.dir / "short.fits"
        short_length = write_fits(short, values, 3, 3)
        short.write_bytes(short.read_bytes()[:short_length + 10])
        with self.assertRaises(ValueError):
            pfs.normalize_frame(short, self.dir / "short-out.fits")

    def test_the_frame_set_pins_every_light_below_the_corpus_root_beside_the_original(self):
        root = self.dir / "corpus"
        (root / "Prepared" / "set" / "lights").mkdir(parents=True)
        frames = []
        for index in range(2):
            frame = root / "Prepared" / "set" / "lights" / ("light_%d_reg.fits" % index)
            frame.write_bytes(bytes([index]) * 100)
            frames.append(frame)
        source = {"id": pfs.SOURCE_SET_ID, "tags": ["mono", "uint16", "reference"], "licenseOrProvenance": "mine",
                  "redistribution": "local-only", "privacyClass": "none",
                  "expected": {"width": 3, "height": 2, "sampleType": "UInt16"}, "roles": []}
        entry = pfs.frame_set(frames, root, source)
        self.assertEqual(entry["id"], pfs.SET_ID)
        self.assertEqual(entry["expected"], {"width": 3, "height": 2, "sampleType": "Float32"})
        self.assertEqual(entry["roles"][0]["role"], "lights")
        self.assertEqual([f["relativePath"] for f in entry["roles"][0]["frames"]],
                         ["Prepared/set/lights/light_0_reg.fits", "Prepared/set/lights/light_1_reg.fits"])
        self.assertEqual(entry["roles"][0]["frames"][1]["sha256"], sha(frames[1]))
        self.assertEqual(entry["roles"][0]["frames"][1]["sizeBytes"], 100)
        self.assertNotIn("uint16", entry["tags"])
        original = {"schemaVersion": 2, "datasets": [], "frameSets": [source]}
        merged = pfs.merged_manifest(original, entry)
        self.assertEqual([s["id"] for s in merged["frameSets"]], [pfs.SOURCE_SET_ID, pfs.SET_ID])
        self.assertEqual(len(original["frameSets"]), 1, "the source manifest is not changed")
        with self.assertRaises(ValueError):
            pfs.merged_manifest(merged, entry)
        outside = self.dir / "elsewhere.fits"
        outside.write_bytes(b"x")
        with self.assertRaises(ValueError):
            pfs.frame_set([outside], root, source)


# A runner that lists the pipeline row, keeps a scratch like the pipeline does and verifies a corpus.
RUNNER = r'''
import json, sys
from pathlib import Path
import numpy as np
args = sys.argv[1:]
def value(name):
    return args[args.index(name) + 1] if name in args else None
if "--dry-run" in args:
    Path(value("--dump-scenarios")).write_text(json.dumps({"schemaVersion": 2, "scenarios": [
        {"id": "pipelines.integration.carina_red"}, {"id": "stages.master_bias.carina_red.cold"}]}))
    sys.exit(0)
manifest = json.loads(Path(value("--corpus-manifest")).read_text())
root = Path(value("--corpus-root"))
if "--verify-corpus" in args:
    for entry in manifest["frameSets"]:
        for role in entry["roles"]:
            for frame in role["frames"]:
                if not (root / frame["relativePath"]).is_file():
                    sys.exit(4)
    sys.exit(0)
scratch = Path(value("--keep-pipeline-scratch"))
lights = next(r for r in manifest["frameSets"][0]["roles"] if r["role"] == "lights")["frames"]
(scratch / "aligned").mkdir(parents=True)
(scratch / "calibrated").mkdir()
for index in range(len(lights)):
    header = "".join(c.ljust(80) for c in ["SIMPLE  = T", "BITPIX  = -32", "NAXIS   = 2", "NAXIS1  = 2",
                                           "NAXIS2  = 1", "END"]).encode().ljust(2880)
    data = np.asarray([0.0, 1000.0 * (index + 1)], dtype=">f4").tobytes().ljust(2880, b"\0")
    (scratch / "aligned" / ("light_%d_reg.fits" % index)).write_bytes(header + data)
    (scratch / "calibrated" / ("light_%d_cal.fits" % index)).write_bytes(b"calibrated")
Path(value("--out")).write_text(json.dumps({"results": [{"scenarioId": "pipelines.integration.carina_red",
    "skipped": False, "skipReason": "", "outputDigest": "pipeline-digest", "fixtureDigest": "fixture"}]}))
'''


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="final-stack-prepare"))
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.runtime = self.dir / "runtime-frozen"
        (self.runtime / "runtime").mkdir(parents=True)
        (self.runtime / "runtime" / "runner.py").write_text(RUNNER)
        (self.runtime / "dependency-lock.json").write_text("{}")
        files = {name: sha(self.runtime / name) for name in ("runtime/runner.py", "dependency-lock.json")}
        (self.runtime / "frozen.json").write_text(json.dumps({"schemaVersion": 1, "role": "frozen-pre-repair-runtime",
                                                              "sourceHead": "abc123", "files": files}))
        self.root = self.dir / "corpus"
        self.root.mkdir()
        self.manifest = self.root / "corpus.json"
        frames = [{"relativePath": "l%d.fits" % i, "sha256": "0", "sizeBytes": 1} for i in range(3)]
        for frame in frames:
            (self.root / frame["relativePath"]).write_bytes(b"x")
        self.manifest.write_text(json.dumps({"schemaVersion": 2, "datasets": [], "frameSets": [
            {"id": pfs.SOURCE_SET_ID, "tags": [], "licenseOrProvenance": "mine", "redistribution": "local-only",
             "privacyClass": "none", "expected": {"width": 2, "height": 1, "sampleType": "UInt16"},
             "roles": [{"role": "lights", "frames": frames}]}]}))
        self.addCleanup(setattr, pfs, "RUNNER", pfs.RUNNER)
        pfs.RUNNER = "runner.py"

    def args(self, out):
        return SimpleNamespace(runtime=self.runtime, corpus_root=self.root,
                               corpus_manifest=self.manifest, out=out, threads=4, host_id="host")

    def test_a_run_leaves_the_normalized_lights_and_a_manifest_that_resolves_both_sets(self):
        out = pfs.prepare(self.args(self.root / "Prepared" / "aligned-v1"))
        lights = sorted(p.name for p in (out / "lights").iterdir())
        self.assertEqual(lights, ["light_0_reg.fits", "light_1_reg.fits", "light_2_reg.fits"])
        self.assertFalse((out / "work" / "scratch").exists(), "the intermediates are removed")
        values = np.frombuffer((out / "lights" / "light_2_reg.fits").read_bytes(), dtype=">f4", count=2, offset=2880)
        self.assertEqual(float(values[1]), float(np.float32(3000.0 / 65535.0)))
        merged = json.loads((out / "corpus.json").read_text())
        self.assertEqual([s["id"] for s in merged["frameSets"]], [pfs.SOURCE_SET_ID, pfs.SET_ID])
        aligned = merged["frameSets"][1]["roles"][0]["frames"]
        self.assertEqual(aligned[0]["relativePath"], "Prepared/aligned-v1/lights/light_0_reg.fits")
        self.assertEqual(aligned[0]["sha256"], sha(out / "lights" / "light_0_reg.fits"))
        inputs = json.loads((out / "inputs.json").read_text())
        self.assertEqual(inputs["pipelineOutputDigest"], "pipeline-digest")
        self.assertEqual(inputs["sourceHead"], "abc123")
        self.assertEqual(inputs["corpusManifestSha256"], sha(out / "corpus.json"))
        self.assertEqual(inputs["order"], lights)

    def test_refuses_an_output_it_cannot_own(self):
        with self.assertRaises(ValueError):
            pfs.prepare(self.args(self.dir / "outside"))
        taken = self.root / "Prepared" / "taken"
        taken.mkdir(parents=True)
        with self.assertRaises(ValueError):
            pfs.prepare(self.args(taken))
        with self.assertRaises(ValueError):
            pfs.prepare(self.args(self.root))
        (self.runtime / "runtime" / "runner.py").write_text(RUNNER + "\n# changed\n")
        with self.assertRaises(ValueError):
            pfs.prepare(self.args(self.root / "Prepared" / "fresh"))


if __name__ == "__main__":
    unittest.main(verbosity=1)
