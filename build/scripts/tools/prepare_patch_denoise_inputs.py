# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prepare the guided patch denoise's capture inputs: two full frames and a 512 x 512 crop of each.

The guided patch denoise has one real-data row, a 512 x 512 public frame, and a patch denoise's cost
depends on how its frame divides into tiles, so a result at that size says nothing about a capture of
another. These inputs let the same operation run on two of the maintainer's captures - the QHY533M Ha
Eagle and the ASI294MM Red Carina - whole, and as the 512 x 512 crop at each one's centre.

A capture is read from its verified UInt16 raw dump and divided by the 16-bit full scale in Float32,
the stretch rows' own derivation, so a full frame is byte for byte the stretch rows' input and shares
its pin; a crop is a rectangle of that frame. Every derived payload is pinned by SHA-256 here, so both
hosts denoise the same bytes, and a source or a result that differs is refused before anything is
written.

The result is a corpus manifest beside the frames: the original manifest with the four datasets added,
every path relative to the original corpus root, so one manifest resolves the public frame the older
row reads and the four new ones. ``inputs.json`` records each dataset's source and rectangle and the
manifest's SHA-256. The output directory must lie under the corpus root and must not exist yet.
"""
import argparse
import copy
import hashlib
from pathlib import Path
import sys

import numpy as np

from stretch_bench import CAPTURES, digest, read_json, write_json

#: Each dataset: the capture it comes from, its rectangle's origin and size, and its payload's SHA-256.
DATASETS = {
    "patch_denoise.eagle.full.unit_f32.v1": ("eagle", 0, 0, 3008, 3004, CAPTURES["eagle"][5]),
    "patch_denoise.carina.full.unit_f32.v1": ("carina", 0, 0, 8288, 5644, CAPTURES["carina"][5]),
    "patch_denoise.eagle.crop512.unit_f32.v1": (
        "eagle", 1248, 1246, 512, 512, "b70527f60fc7a68ec994dc02566304430796dc54c3fd0f592d5835e0236eb25d"),
    "patch_denoise.carina.crop512.unit_f32.v1": (
        "carina", 3888, 2566, 512, 512, "fc5b01b1b4e30a70ffd2a21f7e25aab3b4ff6367142ec61f9b564cb7c0403663"),
}
FULL_SCALE = np.float32(65535)
VERSION = "patch-denoise-unit-f32-1"


def unit_frame(corpus_root, entries, name, captures):
    """One capture divided by the full scale, refused unless its descriptor and payload are the pinned
    ones and it has the shape they state."""
    source_id, width, height, descriptor_sha, source_sha, unit_sha = captures[name]
    entry = entries[source_id]
    descriptor = Path(corpus_root) / entry["relativePath"]
    if entry["sha256"] != descriptor_sha or digest(descriptor) != descriptor_sha:
        raise ValueError("capture descriptor changed: " + name)
    raw = read_json(descriptor)
    payload = descriptor.parent / raw["payloadFile"]
    if raw["payloadDigest"] != source_sha or digest(payload) != source_sha:
        raise ValueError("capture payload changed: " + name)
    shape = raw["descriptor"]
    if (shape["width"], shape["height"], shape["channels"], shape["sampleType"]) != (width, height, 1, "UInt16"):
        raise ValueError("capture shape changed: " + name)
    values = np.fromfile(payload, dtype="<u2").astype("<f4")
    values /= FULL_SCALE
    if hashlib.sha256(values.tobytes()).hexdigest() != unit_sha:
        raise ValueError("Float32 derivation differs from the pinned one: " + name)
    return values.reshape(height, width), dict(shape, sampleType="Float32")


def prepare(corpus_root, corpus_manifest, out, captures=CAPTURES, datasets=DATASETS):
    """Writes every dataset, its descriptor, the extended manifest and the provenance record into `out`."""
    corpus_root = Path(corpus_root).resolve()
    corpus_manifest = Path(corpus_manifest).resolve()
    out = Path(out).resolve()
    if not out.is_relative_to(corpus_root) or out == corpus_root:
        raise ValueError("the output must lie under the corpus root, so one manifest resolves every dataset")
    if out.exists():
        raise ValueError("the output directory already exists: %s" % out)
    original = read_json(corpus_manifest)
    entries = {entry["id"]: entry for entry in original["datasets"]}
    named = sorted(set(entries) & set(datasets))
    if named:
        raise ValueError("the source manifest already names " + ", ".join(named))

    derived = {}
    frames = {}
    for identity, (name, x0, y0, width, height, pinned) in datasets.items():
        if name not in frames:
            frames[name] = unit_frame(corpus_root, entries, name, captures)
        frame, shape = frames[name]
        if x0 + width > frame.shape[1] or y0 + height > frame.shape[0]:
            raise ValueError("%s reaches outside its capture" % identity)
        samples = np.ascontiguousarray(frame[y0:y0 + height, x0:x0 + width])
        if hashlib.sha256(samples.tobytes()).hexdigest() != pinned:
            raise ValueError("%s differs from its pinned payload" % identity)
        derived[identity] = (samples, dict(shape, width=width, height=height))

    out.mkdir(parents=True)
    manifest = copy.deepcopy(original)
    provenance = {}
    for identity, (samples, shape) in derived.items():
        name, x0, y0 = datasets[identity][:3]
        payload = out / (identity + ".f32")
        samples.tofile(payload)
        descriptor = out / (identity + ".json")
        write_json(descriptor, {"schemaVersion": 1, "descriptor": shape, "sourcePath": captures[name][0],
                                "sourceFormat": "verified UInt16 raw dump", "exporterVersion": VERSION,
                                "payloadFile": payload.name, "payloadBytes": payload.stat().st_size,
                                "payloadDigest": digest(payload), "cards": []})
        entry = copy.deepcopy(entries[captures[name][0]])
        entry.update(id=identity, relativePath=descriptor.relative_to(corpus_root).as_posix(),
                     sha256=digest(descriptor), sizeBytes=descriptor.stat().st_size, expected=shape,
                     preprocessing="UInt16 / Float32(65535), then the rectangle at (%d, %d), outside timing" % (x0, y0),
                     preprocessingVersion=VERSION, goldenPath="", goldenSha256="", goldenTolerance=0.0)
        manifest["datasets"].append(entry)
        provenance[identity] = {"source": captures[name][0], "sourcePayloadSha256": captures[name][4],
                                "origin": [x0, y0], "size": [shape["width"], shape["height"]],
                                "payloadSha256": digest(payload), "descriptorSha256": digest(descriptor)}
    write_json(out / "corpus.json", manifest)
    write_json(out / "inputs.json", {
        "schemaVersion": 1, "datasets": provenance, "sourceManifestSha256": digest(corpus_manifest),
        "corpusManifestSha256": digest(out / "corpus.json"),
        "derivation": "every UInt16 code divided by Float32(65535) in Float32, then the stated rectangle"})
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--corpus-manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    out = prepare(args.corpus_root, args.corpus_manifest, args.out)
    print("prepared %d datasets: see %s and %s" % (len(DATASETS), out / "corpus.json", out / "inputs.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
