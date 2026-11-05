#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prepare the local integration-light fixtures: independently captured, registered Carina red lights.

The integration rows need real exposures of one field rather than one capture transformed: whole lights
for the normalization pairs - the first session's first and eighth, which overlap almost completely, and
one light of the second session, rotated against the first by about thirteen degrees, whose footprint
covers part of the reference's - and, for the stacks of 8, 32 and 72 frames, the centre 2048 x 2048 of
every light, a region both sessions cover, so even 72 frames are held resident at once. They are taken from the pinned aligned lights the final-stack rows read - calibrated,
registered to the first light's grid, in the unit interval, exact zero where a light did not reach - and
decoded through the runner's raw-dump exporter into raw dumps the runner holds resident. That frame set is produced
on each host by its own pipeline, so its digests and these are the host's own. Captures and their dumps
remain local; only this recipe and evidence digests belong in Git.
"""

from __future__ import annotations

import argparse
import array
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

#: The frame set the lights are taken from, and its role.
SOURCE_SET = "carina-asi294mm-red-aligned"
SOURCE_ROLE = "lights"
#: The whole lights of the normalization pairs, in the frame set's order: the reference, a light of the same
#: session and a light of the second session, whose footprint covers only part of the reference's.
PAIR_INDICES = (0, 7, 40)
#: Their dataset ids.
PAIR_IDS = ("carina.asi294mm.red.light0.aligned.f32", "carina.asi294mm.red.light7.aligned.f32",
            "carina.asi294mm.red.light40.aligned.f32")
#: How many lights the stacks' crops are taken from, in the frame set's order.
CROP_COUNT = 72
#: The crops' dataset id; the n-th light's is this id with ".n" appended, the first's the id alone.
CROP_ID = "carina.asi294mm.red.lights72.aligned.crop2048.f32"
#: The crop's side and its origin in the light: the centre 2048 x 2048 of 8288 x 5644.
CROP_SIDE = 2048
CROP_ORIGIN = ((8288 - CROP_SIDE) // 2, (5644 - CROP_SIDE) // 2)
#: Kept for the test's frame-set length.
ROTATED_INDEX = 40
#: The one representation the lights are.
EXPECTED = (8288, 5644, 1, "Float32", "Planar", "Little")
RECIPE = "integration-lights-1"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\r\n")


def selected_frames(manifest: dict) -> list[tuple[int, dict]]:
    """The index and source entry of every light the recipe reads, refusing a frame set it cannot use."""
    dataset = next(item for item in manifest["frameSets"] if item["id"] == SOURCE_SET)
    role = next(item for item in dataset["roles"] if item["role"] == SOURCE_ROLE)
    frames = role["frames"]
    if len(frames) < CROP_COUNT:
        raise ValueError(f"the aligned frame set holds {len(frames)} lights; the recipe needs {CROP_COUNT}")
    chosen = list(enumerate(frames[:CROP_COUNT]))
    if len({frame["relativePath"] for _, frame in chosen}) != len(chosen):
        raise ValueError("the recipe's lights must be distinct files")
    return chosen


def crop_id(index: int) -> str:
    """The dataset id of the index-th light's crop."""
    return CROP_ID + (f".{index}" if index else "")


#: The most no-data zeros a crop may hold: calibration leaves a few isolated ones in every light, while a
#: footprint that stops inside the square would leave a band.
CROP_ZERO_LIMIT = CROP_SIDE * CROP_SIDE // 10000


def crop(payload: bytes) -> tuple[bytes, int]:
    """The centre square of a tight little-endian Float32 light and how many no-data zeros it holds, refusing one
    the light's footprint does not cover."""
    width = 8288
    x0, y0 = CROP_ORIGIN
    square = b"".join(payload[((y0 + row) * width + x0) * 4:((y0 + row) * width + x0 + CROP_SIDE) * 4]
                      for row in range(CROP_SIDE))
    samples = array.array("f")
    samples.frombytes(square)
    if sys.byteorder != "little":
        samples.byteswap()
    zeros = samples.count(0.0)
    if zeros > CROP_ZERO_LIMIT:
        raise ValueError(f"a light has no data at {zeros} samples inside the crop")
    return square, zeros


def dataset_entry(entry_id: str, destination: Path, source_root: Path, descriptor: dict, tags: list[str],
                  preprocessing: str, representative: list[str]) -> dict:
    """The corpus manifest's entry for one prepared raw dump."""
    return dict(id=entry_id, tags=tags, relativePath=destination.relative_to(source_root).as_posix(),
                sha256=digest(destination), sizeBytes=destination.stat().st_size,
                licenseOrProvenance="maintainer capture, authorized for benchmarking AfterNightEngine",
                redistribution="local-only", privacyClass="none", expected=descriptor, preprocessing=preprocessing,
                preprocessingVersion=RECIPE, representativeFor=representative)


def prepare(args: argparse.Namespace) -> None:
    source_root = args.source_root.resolve(strict=True)
    output_root = args.output_root.resolve()
    runner = args.runner.resolve(strict=True)
    engine = args.engine.resolve(strict=True)
    if engine.parent != runner.parent:
        raise ValueError("the decoder library must be deployed beside the runner")
    manifest = json.loads(args.source_manifest.read_text(encoding="utf-8-sig"))
    chosen = selected_frames(manifest)
    if output_root.exists():
        raise ValueError("the output directory must be new")
    if output_root == source_root or not output_root.is_relative_to(source_root):
        raise ValueError("the output must be a new directory under the corpus root, which the rows resolve against")
    for _, frame in chosen:
        source = (source_root / frame["relativePath"]).resolve(strict=True)
        if not source.is_relative_to(source_root):
            raise ValueError("a light's path leaves the corpus root")
        if source.stat().st_size != frame["sizeBytes"] or digest(source) != frame["sha256"]:
            raise ValueError(f"light size or digest mismatch: {source}")
    output_root.mkdir(parents=True)
    entries = []
    provenance = []
    for index, frame in chosen:
        source = (source_root / frame["relativePath"]).resolve(strict=True)
        with tempfile.TemporaryDirectory(prefix="integration-decode-", dir=output_root) as temporary:
            decoded_path = Path(temporary) / "decoded.json"
            with (output_root / f"light_{index}.decode.log").open("wb") as log:
                subprocess.run([str(runner), "--export", str(source), "--export-to", str(decoded_path)],
                               cwd=runner.parent, stdout=log, stderr=subprocess.STDOUT, check=True)
            decoded = json.loads(decoded_path.read_text(encoding="utf-8-sig"))
            descriptor = decoded["descriptor"]
            if (descriptor["width"], descriptor["height"], descriptor["channels"], descriptor["sampleType"],
                    descriptor["layout"], descriptor["byteOrder"]) != EXPECTED:
                raise ValueError(f"unexpected decoded representation for {source}")
            pixels = EXPECTED[0] * EXPECTED[1]
            payload_path = decoded_path.parent / decoded["payloadFile"]
            if (payload_path.stat().st_size != pixels * 4 or digest(payload_path) != decoded["payloadDigest"] or
                    descriptor.get("rowStrideBytes", 0) not in (0, EXPECTED[0] * 4) or
                    descriptor.get("planeStrideBytes", 0) not in (0, pixels * 4)):
                raise ValueError(f"decoded payload is not the verified tight plane: {source}")
            descriptor.update(rowStrideBytes=0, planeStrideBytes=0)
            payload = payload_path.read_bytes()
            record = dict(index=index, source=frame, decodedPayloadSha256=decoded["payloadDigest"], datasets=[])
            if index in PAIR_INDICES:
                entry_id = PAIR_IDS[PAIR_INDICES.index(index)]
                stem = entry_id.replace(".", "_")
                stored = output_root / f"{stem}.anraw"
                stored.write_bytes(payload)
                whole = dict(decoded, exporterVersion=f"{RECIPE}: decode of a registered Float32 light",
                             payloadFile=stored.name, payloadDigest=digest(stored))
                destination = output_root / f"{stem}.json"
                write_json(destination, whole)
                entries.append(dataset_entry(entry_id, destination, source_root, descriptor,
                                             ["mono", "light", "registered", "47mp", "integration"],
                                             "the host's pinned aligned light, decoded; no crop or resampling",
                                             ["normalization"]))
                record["datasets"].append(entry_id)
            square, zeros = crop(payload)
            record["cropZeros"] = zeros
            stem = crop_id(index).replace(".", "_")
            stored = output_root / f"{stem}.anraw"
            stored.write_bytes(square)
            cropped_descriptor = dict(descriptor, width=CROP_SIDE, height=CROP_SIDE)
            cropped = dict(decoded, descriptor=cropped_descriptor,
                           exporterVersion=f"{RECIPE}: the centre {CROP_SIDE} x {CROP_SIDE} of a registered light",
                           payloadFile=stored.name, payloadBytes=len(square), payloadDigest=digest(stored))
            destination = output_root / f"{stem}.json"
            write_json(destination, cropped)
            entries.append(dataset_entry(crop_id(index), destination, source_root, cropped_descriptor,
                                         ["mono", "light", "registered", "crop", "integration"],
                                         f"the host's pinned aligned light, decoded; the {CROP_SIDE}-pixel square at "
                                         f"{CROP_ORIGIN[0]}, {CROP_ORIGIN[1]}", ["stacking"]))
            record["datasets"].append(crop_id(index))
            provenance.append(record)
        print(f"Prepared {index + 1}/{len(chosen)}", flush=True)
    write_json(output_root / "corpus.json", dict(schemaVersion=2, datasets=entries, frameSets=[]))
    write_json(output_root / "provenance.json", dict(recipe=RECIPE, sourceManifestSha256=digest(args.source_manifest),
                                                     runnerSha256=digest(runner),
                                                     engineSha256=digest(engine), crop=dict(
                                                         side=CROP_SIDE, originX=CROP_ORIGIN[0],
                                                         originY=CROP_ORIGIN[1]), frames=provenance))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True, help="the corpus root")
    parser.add_argument("--source-manifest", type=Path, required=True,
                        help="the manifest that registers the aligned lights")
    parser.add_argument("--output-root", type=Path, required=True, help="a new directory under the corpus root")
    parser.add_argument("--runner", type=Path, required=True, help="the benchmark runner whose exporter decodes")
    parser.add_argument("--engine", type=Path, required=True, help="the engine library deployed beside it")
    try:
        prepare(parser.parse_args())
    except (OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as error:
        print(f"integration corpus preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
