# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prepare the final stack's input: the aligned lights one run of the integration pipeline produced.

The final-stack stage rows stack the same aligned lights on every run, and those frames exist only
inside a pipeline run. This tool produces them once, outside any measurement: it runs one frozen
runtime's pipeline row with ``--keep-pipeline-scratch``, takes the aligned lights it wrote in the order
the pipeline stacks them, removes the intermediates, and pins every derived light by size and SHA-256
as the frame set ``carina-asi294mm-red-aligned``.

The lights are written in the units the engine reads a floating image in: every sample divided by the
16-bit full scale, 65535, in double and rounded once to Float32, with the header unchanged, because the
pipeline's aligned lights are in camera codes. An exact zero, which is what an aligned light holds where
no source pixel lands, stays zero.

The result is a corpus manifest beside the frames: the original manifest with that frame set added,
every path relative to the original corpus root, so one manifest resolves both the original set and
the aligned one. ``inputs.json`` records where the frames came from - the runtime, its runner, the
pipeline run's own output digest - and the manifest's SHA-256. The output directory must lie under the
corpus root and must not exist yet; the run is refused rather than written into anything else.
"""
import argparse
import copy
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import numpy as np

import stretch_bench
from stretch_bench import (catalogue_dump_command, digest, executable_name, host_id, launch_environment,
                           library_resolution, read_json, verify_frozen, write_json)

SET_ID = "carina-asi294mm-red-aligned"
SOURCE_SET_ID = "carina-asi294mm-red"
PIPELINE_ROW = "pipelines.integration.carina_red"
#: The runner a frozen runtime holds, under the name this host gives it.
RUNNER = executable_name(stretch_bench.RUNNER)
INDEX = re.compile(r"(\d+)")
FULL_SCALE = 65535.0
CARD = 80
BLOCK = 2880


def aligned_frames(directory, expected_count):
    """The aligned lights a kept pipeline scratch holds, in the order the pipeline stacks them.

    The pipeline names each aligned light after the index of the light it came from, as
    ``light_<index>_reg``, and stacks them in that order, so they are ordered by the last number in their
    names, which must be distinct.
    """
    frames = sorted(Path(directory).glob("*.fits"))
    keyed = []
    for frame in frames:
        numbers = INDEX.findall(frame.stem)
        if not numbers:
            raise ValueError("an aligned light names no index: " + frame.name)
        keyed.append((int(numbers[-1]), frame))
    keyed.sort()
    indices = [index for index, _ in keyed]
    if len(set(indices)) != len(indices):
        raise ValueError("two aligned lights share an index")
    if len(keyed) != expected_count:
        raise ValueError("the pipeline kept %d aligned lights, not %d" % (len(keyed), expected_count))
    return [frame for _, frame in keyed]


def fits_header(raw, name):
    """The primary header's length in bytes and its image size, refusing anything but one plane of
    unscaled Float32 samples."""
    values = {}
    for offset in range(0, len(raw), CARD):
        card = raw[offset:offset + CARD].decode("ascii", "replace")
        keyword = card[:8].strip()
        if keyword == "END":
            length = (offset + CARD + BLOCK - 1) // BLOCK * BLOCK
            break
        if card[8:10] == "= ":
            values[keyword] = card[10:].split("/")[0].strip()
    else:
        raise ValueError("%s has no complete header" % name)
    if (values.get("BITPIX") != "-32" or values.get("NAXIS") != "2" or float(values.get("BSCALE", "1")) != 1.0 or
            float(values.get("BZERO", "0")) != 0.0):
        raise ValueError("%s is not one plane of unscaled Float32 samples" % name)
    return length, int(values["NAXIS1"]) * int(values["NAXIS2"])


def normalize_frame(source, target):
    """Writes `source` with every sample divided by the 16-bit full scale in double and rounded once
    to Float32, its header and trailing padding unchanged."""
    raw = Path(source).read_bytes()
    length, count = fits_header(raw, Path(source).name)
    end = length + count * 4
    if len(raw) < end:
        raise ValueError("%s ends before its image does" % Path(source).name)
    samples = np.frombuffer(raw, dtype=">f4", count=count, offset=length)
    if not np.isfinite(samples).all():
        raise ValueError("%s holds a sample that is not finite" % Path(source).name)
    scaled = (samples.astype(np.float64) / FULL_SCALE).astype(">f4")
    with Path(target).open("wb") as stream:
        stream.write(raw[:length])
        stream.write(scaled.tobytes())
        stream.write(raw[end:])


def frame_set(frames, corpus_root, source):
    """The aligned lights as a frame set: the source set's identity fields, a Float32 descriptor and one
    role, ``lights``, with every frame's path relative to the corpus root, its size and its digest."""
    root = Path(corpus_root).resolve()
    expected = dict(source["expected"], sampleType="Float32")
    entries = []
    for frame in frames:
        path = Path(frame).resolve()
        if not path.is_relative_to(root):
            raise ValueError("an aligned light lies outside the corpus root: %s" % path)
        entries.append({"relativePath": path.relative_to(root).as_posix(), "sha256": digest(path),
                        "sizeBytes": path.stat().st_size})
    return {"id": SET_ID, "tags": sorted(set(source.get("tags", [])) - {"uint16", "reference"} | {"float32", "aligned"}),
            "licenseOrProvenance": source["licenseOrProvenance"], "redistribution": source["redistribution"],
            "privacyClass": source["privacyClass"], "expected": expected,
            "roles": [{"role": "lights", "frames": entries}]}


def merged_manifest(original, aligned):
    """The original manifest with the aligned frame set added, refused if it already names that set."""
    if any(entry["id"] == SET_ID for entry in original.get("frameSets", [])):
        raise ValueError("the source manifest already names " + SET_ID)
    merged = copy.deepcopy(original)
    merged.setdefault("frameSets", []).append(aligned)
    return merged


def source_set(original):
    """The original frame set the aligned lights come from."""
    for entry in original.get("frameSets", []):
        if entry["id"] == SOURCE_SET_ID:
            return entry
    raise ValueError("the source manifest names no " + SOURCE_SET_ID)


def run(command, cwd, env, log):
    """Runs a runner, through Python when it is a script, with its output in `log`."""
    if command[0].endswith(".py"):
        command = [sys.executable, *command]
    with open(log, "wb") as stream:
        return subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode


def prepare(args):
    runtime = args.runtime.resolve()
    identity = verify_frozen(runtime)
    corpus_root = args.corpus_root.resolve()
    manifest_path = args.corpus_manifest.resolve()
    out = args.out.resolve()
    if not out.is_relative_to(corpus_root) or out == corpus_root:
        raise ValueError("the output must lie under the corpus root, so one manifest resolves both sets")
    if out.exists():
        raise ValueError("the output directory already exists: %s" % out)
    original = read_json(manifest_path)
    source = source_set(original)
    lights = next(role for role in source["roles"] if role["role"] == "lights")
    runner = runtime / "runtime" / RUNNER
    if not runner.is_file():
        raise ValueError("the frozen runtime holds no runner %s" % runner.name)
    out.mkdir(parents=True)
    work = out / "work"
    work.mkdir()
    env = launch_environment(runner.parent)
    if os.name != "nt" and not runner.name.endswith(".py"):
        (work / "libraries.txt").write_text(library_resolution(runner, env, runner.parent))
    lock = digest(runtime / "dependency-lock.json")
    head = identity["sourceHead"]
    common = ["--commit", head, "--engine-commit", head, "--dependency-lock-digest", lock]
    if run(catalogue_dump_command(runner, work / "dry.json", work / "catalogue.json", head, head, lock),
           runner.parent, env, work / "catalogue.log") != 0:
        raise ValueError("the runner could not list its catalogue; see work/catalogue.log")
    rows = [row for row in read_json(work / "catalogue.json")["scenarios"] if row["id"] == PIPELINE_ROW]
    if len(rows) != 1:
        raise ValueError("the runner does not declare " + PIPELINE_ROW)
    write_json(work / "pipeline.json", {"schemaVersion": 2, "scenarios": rows})
    scratch = work / "scratch"
    code = run([str(runner), "--out", str(work / "run.json"), "--scenarios", str(work / "pipeline.json"),
                "--corpus-root", str(corpus_root), "--corpus-manifest", str(manifest_path), "--host-id", args.host_id,
                *common, "--clean", "--threads", str(args.threads), "--repetitions", "1", "--warmup", "0",
                "--keep-pipeline-scratch", str(scratch)], runner.parent, env, work / "run.log")
    result = read_json(work / "run.json")["results"] if (work / "run.json").is_file() else []
    if code != 0 or len(result) != 1 or result[0]["skipped"]:
        reason = result[0].get("skipReason", "") if result else "no result"
        raise ValueError("the pipeline did not run: exit %d, %s; see work/run.log" % (code, reason))
    frames = aligned_frames(scratch / "aligned", len(lights["frames"]))
    destination = out / "lights"
    destination.mkdir()
    kept = []
    for frame in frames:
        target = destination / frame.name
        normalize_frame(frame, target)
        kept.append(target)
    shutil.rmtree(scratch)
    aligned = frame_set(kept, corpus_root, source)
    merged = merged_manifest(original, aligned)
    write_json(out / "corpus.json", merged)
    if run([str(runner), "--verify-corpus", "--out", str(work / "verify.json"), "--corpus-root", str(corpus_root),
            "--corpus-manifest", str(out / "corpus.json"), "--host-id", args.host_id, *common], runner.parent, env,
           work / "verify.log") != 0:
        raise ValueError("the runner does not resolve the prepared manifest; see work/verify.log")
    write_json(out / "inputs.json", {
        "schemaVersion": 1, "frameSet": SET_ID, "frames": len(kept),
        "runtime": str(runtime).replace("\\", "/"), "runtimeFrozenSha256": digest(runtime / "frozen.json"),
        "runner": runner.name, "runnerSha256": digest(runner), "sourceHead": head, "threads": args.threads,
        "sourceManifestSha256": digest(manifest_path), "corpusManifestSha256": digest(out / "corpus.json"),
        "pipelineOutputDigest": result[0]["outputDigest"], "pipelineFixtureDigest": result[0].get("fixtureDigest"),
        "order": [frame.name for frame in kept],
        "derivation": "every sample divided by %g in double and rounded once to Float32; header unchanged" % FULL_SCALE,
        "boundary": "stages 1-7 of " + PIPELINE_ROW + ", outside timing; the intermediates are removed"})
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime", type=Path, required=True, help="a frozen runtime whose runner produces the set")
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--corpus-manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--host-id", default=host_id())
    args = parser.parse_args(argv)
    out = prepare(args)
    print("prepared %s: see %s and %s" % (SET_ID, out / "corpus.json", out / "inputs.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
