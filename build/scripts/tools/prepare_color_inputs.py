# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prepare the colour conversion's capture input: NGC 253 as encoded RGB.

The colour conversion's real-data row is one real colour frame: the maintainer's QHY183C capture of NGC 253,
demosaiced and encoded outside timing - every colour interpolated from its own RGGB sites by a normalized
[1, 2, 1] x [1, 2, 1] tent with replicated edges, then encoded with the sRGB transfer function, all in NumPy
Float32, with no white balance and no colour calibration. This derives the same RGB again from the verified
UInt16 raw dump and pins it to the recorded SHA-256, so the catalogue rows read byte for byte the recorded
input, and a source or a result that differs is refused before anything is written.

The result is a corpus manifest beside the frame: the original manifest with the dataset added, every path
relative to the original corpus root. ``inputs.json`` records the source, the derivation and the manifest's
SHA-256. The output directory must lie under the corpus root and must not exist yet.
"""
import argparse
import copy
import hashlib
from pathlib import Path
import sys

import numpy as np

from stretch_bench import digest, read_json, write_json

#: Each capture: its dataset id, width, height, and the SHA-256 of its descriptor and of its payload.
CAPTURES = {
    "ngc253": ("ngc253.qhy183c.light", 5544, 3684, "e273c54bf5edf182dcb9166821d03827d1d151ef770886c92bf89a79e053903f",
               "98a327aae369c6484ccb914dcc90a27b451cf3c84677c8d37d83e708c65ef1e1"),
}
#: Each dataset: the capture it comes from and its payload's SHA-256, the audit's.
DATASETS = {"color.ngc253.srgb.f32.v1": ("ngc253", "82536f481dbc53bb2bdb8839e0309bdc23eabefb3f497a0a356c7215b62565c0")}
#: Red, green and blue's sites in an RGGB mosaic, as (row, column) parities.
RGGB_SITES = (((0, 0),), ((0, 1), (1, 0)), ((1, 1),))
FULL_SCALE = np.float32(65535)
VERSION = "color-srgb-f32-1"


def tent(values):
    """The [1, 2, 1] sum along the rows and then the columns, each edge pixel standing in for its missing
    neighbour."""
    padded = np.pad(values, ((0, 0), (1, 1)), mode="edge")
    rows = padded[:, :-2] + 2 * padded[:, 1:-1] + padded[:, 2:]
    padded = np.pad(rows, ((1, 1), (0, 0)), mode="edge")
    return padded[:-2] + 2 * padded[1:-1] + padded[2:]


def interpolate(unit, sites):
    """One colour at every pixel: the tent-weighted mean of that colour's sites around it, in Float32."""
    mask = np.zeros_like(unit)
    for y, x in sites:
        mask[y::2, x::2] = 1
    return tent(unit * mask) / tent(mask)


def encode_srgb(linear):
    """The sRGB transfer function in Float32, with its linear segment."""
    return np.where(linear <= np.float32(0.0031308), np.float32(12.92) * linear,
                    np.float32(1.055) * linear ** np.float32(1 / 2.4) - np.float32(0.055))


def unit_mosaic(corpus_root, entries, name, captures):
    """One RGGB capture divided by the full scale, refused unless its descriptor and payload are the pinned
    ones and it has the shape and mosaic they state."""
    source_id, width, height, descriptor_sha, source_sha = captures[name]
    entry = entries[source_id]
    descriptor = Path(corpus_root) / entry["relativePath"]
    if entry["sha256"] != descriptor_sha or digest(descriptor) != descriptor_sha:
        raise ValueError("capture descriptor changed: " + name)
    raw = read_json(descriptor)
    payload = descriptor.parent / raw["payloadFile"]
    if raw["payloadDigest"] != source_sha or digest(payload) != source_sha:
        raise ValueError("capture payload changed: " + name)
    shape = raw["descriptor"]
    if (shape["width"], shape["height"], shape["channels"], shape["sampleType"], shape.get("cfaPattern")) != (
            width, height, 1, "UInt16", "RGGB"):
        raise ValueError("capture shape changed: " + name)
    values = np.fromfile(payload, dtype="<u2").reshape(height, width).astype(np.float32)
    values /= FULL_SCALE
    return values, dict(shape, channels=3, sampleType="Float32", cfaPattern="")


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
    for identity, (name, pinned) in datasets.items():
        unit, shape = unit_mosaic(corpus_root, entries, name, captures)
        rgb = np.ascontiguousarray(np.stack([encode_srgb(interpolate(unit, sites)) for sites in RGGB_SITES]),
                                   dtype="<f4")
        if hashlib.sha256(rgb.tobytes()).hexdigest() != pinned:
            raise ValueError("%s differs from its pinned payload" % identity)
        derived[identity] = (rgb, shape)

    out.mkdir(parents=True)
    manifest = copy.deepcopy(original)
    provenance = {}
    for identity, (rgb, shape) in derived.items():
        name = datasets[identity][0]
        payload = out / (identity + ".f32")
        rgb.tofile(payload)
        descriptor = out / (identity + ".json")
        write_json(descriptor, {"schemaVersion": 1, "descriptor": shape, "sourcePath": captures[name][0],
                                "sourceFormat": "verified UInt16 raw dump", "exporterVersion": VERSION,
                                "payloadFile": payload.name, "payloadBytes": payload.stat().st_size,
                                "payloadDigest": digest(payload), "cards": []})
        entry = copy.deepcopy(entries[captures[name][0]])
        entry.update(id=identity, relativePath=descriptor.relative_to(corpus_root).as_posix(),
                     sha256=digest(descriptor), sizeBytes=descriptor.stat().st_size, expected=shape,
                     tags=["osc", "rgb", "light", "derived"], representativeFor=["color"],
                     preprocessing="UInt16 / Float32(65535); each RGGB colour a [1, 2, 1] tent-weighted mean of "
                                   "its sites with replicated edges; sRGB-encoded in Float32; outside timing",
                     preprocessingVersion=VERSION, goldenPath="", goldenSha256="", goldenTolerance=0.0)
        manifest["datasets"].append(entry)
        provenance[identity] = {"source": captures[name][0], "sourcePayloadSha256": captures[name][4],
                                "payloadSha256": digest(payload), "descriptorSha256": digest(descriptor)}
    write_json(out / "corpus.json", manifest)
    write_json(out / "inputs.json", {
        "schemaVersion": 1, "datasets": provenance, "sourceManifestSha256": digest(corpus_manifest),
        "corpusManifestSha256": digest(out / "corpus.json"),
        "derivation": "every UInt16 code divided by Float32(65535) in Float32; red, green and blue each the "
                      "[1, 2, 1] x [1, 2, 1] tent-weighted mean of its own RGGB sites, an edge pixel standing in "
                      "for its missing neighbour; then the sRGB transfer function in Float32"})
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--corpus-manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    out = prepare(args.corpus_root, args.corpus_manifest, args.out)
    print("prepared %d dataset: see %s and %s" % (len(DATASETS), out / "corpus.json", out / "inputs.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
