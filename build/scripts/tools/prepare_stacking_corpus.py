#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prepare the local Carina red-flat stack fixtures through the runner's raw-dump exporter.

The canonical capture manifest supplies source sizes and SHA-256 digests. Decoding
and explicit UInt16 full-scale conversion happen before benchmarking. Captures and
their dumps remain local; only this recipe and evidence digests belong in Git.
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


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\r\n")


def prepare(args: argparse.Namespace) -> None:
    source_root = args.source_root.resolve(strict=True)
    output_root = args.output_root.resolve()
    runner = args.runner.resolve(strict=True)
    engine = args.engine.resolve(strict=True)
    if engine.parent != runner.parent:
        raise ValueError("the decoder library must be deployed beside the runner")
    manifest = json.loads(args.source_manifest.read_text(encoding="utf-8-sig"))
    dataset = next(item for item in manifest["frameSets"] if item["id"] == "carina-asi294mm-red")
    role = next(item for item in dataset["roles"] if item["role"] == "flats")
    sources = sorted(role["frames"], key=lambda item: item["relativePath"])[:8]
    if len(sources) != 8 or len({frame["relativePath"] for frame in sources}) != 8:
        raise ValueError("the canonical dataset must contain eight distinct red flats")
    if source_root == output_root or source_root.is_relative_to(output_root):
        raise ValueError("output must be a separate directory from the capture root")
    output_root.mkdir(parents=True, exist_ok=True)
    entries = []
    provenance = []
    for index, frame in enumerate(sources):
        source = (source_root / frame["relativePath"]).resolve(strict=True)
        if not source.is_relative_to(source_root):
            raise ValueError("a capture path leaves the source root")
        if source.stat().st_size != frame["sizeBytes"] or digest(source) != frame["sha256"]:
            raise ValueError(f"capture size or digest mismatch: {source}")
        stem = f"carina_red_flat_{index}"
        destination = output_root / f"{stem}.json"
        with tempfile.TemporaryDirectory(prefix="stacking-decode-", dir=output_root) as temporary:
            decoded_path = Path(temporary) / "decoded.json"
            with (output_root / f"{stem}.decode.log").open("wb") as log:
                subprocess.run([str(runner), "--export", str(source), "--export-to", str(decoded_path)],
                               cwd=runner.parent, stdout=log, stderr=subprocess.STDOUT, check=True)
            decoded = json.loads(decoded_path.read_text(encoding="utf-8-sig"))
            descriptor = decoded["descriptor"]
            if (descriptor["width"], descriptor["height"], descriptor["channels"],
                    descriptor["sampleType"], descriptor["layout"], descriptor["byteOrder"]) != (
                    8288, 5644, 1, "UInt16", "Planar", "Little"):
                raise ValueError(f"unexpected decoded representation for {source}")
            pixels = 8288 * 5644
            payload = decoded_path.parent / decoded["payloadFile"]
            if (payload.stat().st_size != pixels * 2 or digest(payload) != decoded["payloadDigest"] or
                    descriptor.get("rowStrideBytes", 0) not in (0, 8288 * 2) or
                    descriptor.get("planeStrideBytes", 0) not in (0, pixels * 2)):
                raise ValueError(f"decoded payload is not the verified tight plane: {source}")
            integers = array.array("H")
            with payload.open("rb") as stream:
                integers.fromfile(stream, pixels)
            if sys.byteorder != "little":
                integers.byteswap()
            samples = array.array("f", (value / 65535.0 for value in integers))
            if samples.itemsize != 4:
                raise ValueError("this host cannot write IEEE Float32 array elements")
            if sys.byteorder != "little":
                samples.byteswap()
            converted = output_root / f"{stem}.anraw"
            with converted.open("wb") as stream:
                samples.tofile(stream)
            descriptor.update(sampleType="Float32", rowStrideBytes=0, planeStrideBytes=0)
            decoded.update(exporterVersion="stacking-corpus-1: decode; UInt16 / 65535 -> Float32",
                           payloadFile=converted.name, payloadBytes=pixels * 4, payloadDigest=digest(converted))
            write_json(destination, decoded)
        entry_id = "carina.asi294mm.red.flats8.f32" + (f".{index}" if index else "")
        entries.append(dict(id=entry_id, tags=["mono", "flat", "47mp", "stacking"],
                            relativePath=destination.name, sha256=digest(destination),
                            sizeBytes=destination.stat().st_size,
                            licenseOrProvenance=dataset["licenseOrProvenance"],
                            redistribution="local-only", privacyClass="none", expected=descriptor,
                            preprocessing="decode, UInt16 full scale to Float32; no crop or resampling",
                            preprocessingVersion="stacking-corpus-1", representativeFor=["stacking"]))
        provenance.append(dict(id=entry_id, source=frame, decodedPayloadSha256=decoded["payloadDigest"]))
        print(f"Prepared {index + 1}/8: {entry_id}", flush=True)
    write_json(output_root / "corpus.json", dict(schemaVersion=2, datasets=entries, frameSets=[]))
    write_json(output_root / "provenance.json", dict(recipe="stacking-corpus-1",
               sourceManifestSha256=digest(args.source_manifest), runnerSha256=digest(runner),
               engineSha256=digest(engine), frames=provenance))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--source-manifest", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True, help="the benchmark runner whose exporter decodes")
    parser.add_argument("--engine", type=Path, required=True, help="the engine library deployed beside it")
    try:
        prepare(parser.parse_args())
    except (OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as error:
        print(f"stacking corpus preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
