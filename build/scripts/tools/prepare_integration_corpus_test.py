# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Refusal checks for local integration-light preparation; no capture data is needed."""

import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_integration_corpus as recipe


class PrepareIntegrationCorpusTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="integration-corpus-test-")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name) / "corpus"
        (root / "lights").mkdir(parents=True)
        frames = []
        for index in range(recipe.CROP_COUNT):
            payload = index.to_bytes(2, "little")
            name = f"lights/light_{index}_reg.fits"
            (root / name).write_bytes(payload)
            frames.append(dict(relativePath=name, sizeBytes=len(payload),
                               sha256=hashlib.sha256(payload).hexdigest()))
        self.frames = frames
        self.manifest = dict(frameSets=[dict(id=recipe.SOURCE_SET, licenseOrProvenance="test",
                                             roles=[dict(role=recipe.SOURCE_ROLE, frames=frames)])])
        self.args = argparse.Namespace(source_root=root, source_manifest=root / "source.json",
                                       output_root=root / "prepared", runner=root / "runner",
                                       engine=root / "libAfterNightEngine.so")
        self.args.runner.write_bytes(b"test runner")
        self.args.engine.write_bytes(b"test library")

    def refuse_before_decode(self, reason):
        self.args.source_manifest.write_text(json.dumps(self.manifest))
        with patch.object(recipe.subprocess, "run") as decode:
            with self.assertRaisesRegex(ValueError, reason):
                recipe.prepare(self.args)
            decode.assert_not_called()
        self.assertFalse((self.args.output_root / "corpus.json").exists())

    def test_reads_every_light_and_names_the_pairs_and_crops(self):
        chosen = recipe.selected_frames(self.manifest)
        self.assertEqual([index for index, _ in chosen], list(range(recipe.CROP_COUNT)))
        self.assertEqual(recipe.PAIR_INDICES, (0, 7, 40))
        self.assertEqual(recipe.crop_id(0), recipe.CROP_ID)
        self.assertEqual(recipe.crop_id(71), recipe.CROP_ID + ".71")
        self.assertEqual(recipe.CROP_ORIGIN, (3120, 1798))

    def test_refuses_a_frame_set_too_short_for_the_crops(self):
        del self.frames[recipe.CROP_COUNT - 1:]
        self.refuse_before_decode("the recipe needs")

    def test_refuses_duplicate_lights(self):
        self.frames[recipe.ROTATED_INDEX] = dict(self.frames[0])
        self.refuse_before_decode("distinct files")

    def test_a_crop_is_the_centre_square_and_refuses_no_data(self):
        import array
        width, height = 8288, 5644
        light = array.array("f", [0.5]) * (width * height)
        light[(recipe.CROP_ORIGIN[1] + 1) * width + recipe.CROP_ORIGIN[0] + 2] = 0.25
        square = array.array("f")
        payload, zeros = recipe.crop(light.tobytes())
        square.frombytes(payload)
        self.assertEqual(len(square), recipe.CROP_SIDE * recipe.CROP_SIDE)
        self.assertEqual(square[recipe.CROP_SIDE + 2], 0.25)
        self.assertEqual(zeros, 0)
        # An isolated zero left by calibration is counted; a band the footprint does not reach is refused.
        light[(recipe.CROP_ORIGIN[1] + 5) * width + recipe.CROP_ORIGIN[0] + 5] = 0.0
        light[0] = 0.0
        self.assertEqual(recipe.crop(light.tobytes())[1], 1)
        for x in range(recipe.CROP_SIDE):
            light[(recipe.CROP_ORIGIN[1] + 7) * width + recipe.CROP_ORIGIN[0] + x] = 0.0
        with self.assertRaisesRegex(ValueError, "no data at"):
            recipe.crop(light.tobytes())

    def test_refuses_a_modified_light_before_invoking_the_decoder(self):
        (self.args.source_root / "lights/light_3_reg.fits").write_bytes(b"modified")
        self.refuse_before_decode("size or digest mismatch")

    def test_refuses_an_output_outside_the_corpus_root(self):
        self.args.output_root = self.args.source_root.parent / "elsewhere"
        self.refuse_before_decode("under the corpus root")

    def test_refuses_an_existing_output(self):
        self.args.output_root.mkdir()
        self.refuse_before_decode("must be new")

    def test_refuses_a_decoder_library_away_from_its_runner(self):
        library = self.args.source_root.parent / "elsewhere.so"
        library.write_bytes(b"x")
        self.args.engine = library
        self.refuse_before_decode("beside the runner")


if __name__ == "__main__":
    unittest.main()
