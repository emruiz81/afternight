# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prepare pinned stretch captures and record/verify observed local build identities.

These are reliability tools, not a performance verdict: they record what a build was made from,
freeze its runtime so a later run can measure against exactly those bytes, and pin the stretch
captures the stretch rows read.
"""

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import zipfile


ROOT = Path(__file__).resolve().parents[3]
DEPENDENCY_LOCK = "docs/dev/dependency-lock.json"
SOURCE_ROOTS = ("src/AfterNightBench",
                "src/AfterNightEngine",
                "src/AfterNightTests/Test_EngineImageOps",
                "src/AfterNightTests/Test_EngineBenchCommon", "build/premake", "build/scripts/msvc", "build/scripts/tools", "build/vcpkg",
                DEPENDENCY_LOCK)
# The audit's immutable capture identities, independent of a host's manifest paths.
CAPTURES = {
    "eagle": ("eagle.qhy533m.ha.light", 3008, 3004,
              "e1e3d7534f9135558a6834a88ab4ae4cdd88f113a598e163c39f76252454d01a",
              "604d296021a786507e14fa32069d83e0ca8f51ff54ad84ff8a23c7dde4cd5542",
              "dc66fba8140344e3bf7b3d37e556cafc6179f38aa89450ec2b54a4ec7c95dafe"),
    "carina": ("carina.asi294mm.red.light", 8288, 5644,
               "8975efc4e96664bb7986b942338c7f26d071302d8cbad612018b8fc815f85ff6",
               "bac6bc43e33c3437c8e31d70463b138cff6482136a7e7fdc0c0d454444f832f4",
               "f697112e206322d970e04dee768cb0c86c84808df092b5ecd02aa7ffab410509"),
}


#: Where a distribution installs the libraries a Linux runner may load from outside its frozen runtime.
SYSTEM_LIBRARY_DIRECTORIES = ("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/")

#: The benchmark runner a frozen runtime holds.
RUNNER = "AfterNightEngineBench"


def host_id(system=os.name):
    """The benchmark host this checkout runs on: each has one, with its own protocol."""
    return "bench-windows" if system == "nt" else "bench-linux"


def engine_lto_flag(system=os.name):
    """How the runners state the engines' link: the Microsoft release engines are linked with link-time
    optimization and the Linux ones are not."""
    return "--engine-lto" if system == "nt" else "--engine-no-lto"


def executable_name(name, system=os.name):
    return name + ".exe" if system == "nt" else name


def frozen_runner(runtime, system=os.name):
    """The runner a frozen runtime directory holds."""
    return Path(runtime) / executable_name(RUNNER, system)


def launch_environment(runtime, system=os.name, base=None):
    """The environment a frozen runtime's runners start in, so each loads that runtime's libraries only.

    A Windows process loads the libraries beside its executable first, so the search path is cut to
    System32. A Linux runner carries no run path and loads what the dynamic loader finds, so its library
    path is set to the runtime and to nothing else - an inherited one would reach a build directory.
    """
    environment = dict(os.environ if base is None else base)
    if system == "nt":
        environment["PATH"] = os.path.join(environment.get("SystemRoot", "C:\\Windows"), "System32")
    else:
        environment["LD_LIBRARY_PATH"] = str(runtime)
    return environment


def catalogue_dump_command(executable, out, catalogue, head, engine_commit, lock_digest):
    """A runner's dry run that writes its catalogue, stating this host: a Linux runner otherwise takes its
    host from HOSTNAME, which a shell does not export, and refuses the manifest it writes."""
    return [str(executable), "--dry-run", "--out", str(out), "--host-id", host_id(), "--commit", head,
            "--engine-commit", engine_commit, "--dependency-lock-digest", lock_digest,
            "--dump-scenarios", str(catalogue)]


def unresolved_libraries(ldd_output, runtime):
    """Every library `ldd` resolved neither into `runtime` nor into the distribution's own directories."""
    # ldd prints POSIX paths, and the runtime is read the same way on the Windows host that tests this.
    inside = runtime.as_posix().rstrip("/") + "/"
    refused = []
    for line in ldd_output.splitlines():
        if "=>" not in line:
            continue
        name, target = (part.strip() for part in line.split("=>", 1))
        path = target.split(" (", 1)[0].strip()
        if path == "not found" or not path.startswith((inside,) + SYSTEM_LIBRARY_DIRECTORIES):
            refused.append("%s => %s" % (name, path))
    return refused


def library_resolution(executable, environment, runtime):
    """What a Linux runner loads under `environment`, refused unless every library comes from `runtime` or
    the system: a runner that reached a build directory would measure an engine nobody froze."""
    output = subprocess.run(["ldd", str(executable)], env=environment, capture_output=True, text=True,
                            check=True).stdout
    refused = unresolved_libraries(output, runtime)
    if refused:
        raise ValueError("%s loads libraries from outside its frozen runtime: %s" % (executable, "; ".join(refused)))
    return output


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8", newline="\r\n")


def git(*arguments):
    return subprocess.check_output(["git", *arguments], cwd=ROOT).decode("utf-8").strip()


def source_identity():
    names = git("ls-files", "--cached", "--others", "--exclude-standard", "--", *SOURCE_ROOTS).splitlines()
    files = {name: digest(ROOT / name) if (ROOT / name).is_file() else "missing"
             for name in sorted(set(names))}
    return {"head": git("rev-parse", "HEAD"),
            "status": git("status", "--porcelain", "--untracked-files=all", "--", *SOURCE_ROOTS),
            "files": files,
            "engineTreesAtHead": {name: git("rev-parse", "HEAD:src/" + name)
                                  for name in (
                                      "AfterNightEngine",)}}


def binary_identity(directory):
    directory = Path(directory)
    # Keep all deployed dynamic libraries: naming just the engine library omits its runtime.
    names = [p for p in directory.iterdir() if p.is_file() and
             (p.suffix.lower() == ".dll" or ".so" in p.name or p.name.startswith(RUNNER)) and
             p.suffix.lower() not in (".pdb", ".debug")]
    if not any(p.name in (RUNNER, RUNNER + ".exe", RUNNER + "_d.exe") for p in names):
        raise ValueError("missing benchmark binary: " + RUNNER)
    if not any(p.name in ("AfterNightEngine.dll", "AfterNightEngine_d.dll", "libAfterNightEngine.so", "AfterNightEngine.so") for p in names):
        raise ValueError("missing engine library")
    return {p.name: {"sha256": digest(p), "bytes": p.stat().st_size} for p in sorted(names)}


def record_build(args):
    if not args.command:
        raise ValueError("build requires an actual command after --command")
    args.out.mkdir(parents=True, exist_ok=False)
    before = source_identity()
    build_inputs = {str(path.resolve()): digest(path) for path in args.build_input}
    write_json(args.out / "source-before.json", before)
    write_json(args.out / "command.json", args.command)
    with (args.out / "build.log").open("wb") as log:
        completed = subprocess.run(args.command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    after = source_identity()
    write_json(args.out / "source-after.json", after)
    if (completed.returncode or before != after or
            build_inputs != {str(path.resolve()): digest(path) for path in args.build_input}):
        raise ValueError("build failed or source changed while building; retained log, no valid identity")
    write_json(args.out / "identity.json", {
        "schemaVersion": 1, "role": getattr(args, "role", "same-checkout-pre-repair"),
        "claim": "observed build inputs and deployed artifacts; not an embedded source revision or immutable baseline",
        "source": after, "buildInputs": build_inputs, "binaryDirectory": str(args.binaries.resolve()),
        "binaries": binary_identity(args.binaries), "command": args.command,
        "buildLogSha256": digest(args.out / "build.log"),
    })


def verify_identity(path, directory):
    identity = read_json(path)
    if identity.get("schemaVersion") != 1 or identity.get("role") not in ("same-checkout-pre-repair", "same-checkout-repair"):
        raise ValueError("unsupported build identity")
    if identity["source"]["files"] != source_identity()["files"]:
        raise ValueError("build source inputs changed")
    if identity["binaries"] != binary_identity(directory):
        raise ValueError("deployed binaries changed")
    if identity["buildLogSha256"] != digest(Path(path).parent / "build.log"):
        raise ValueError("build log changed")
    if any(digest(path) != sha for path, sha in identity.get("buildInputs", {}).items()):
        raise ValueError("build recipe or project changed")
    return identity


def verify_frozen(directory):
    """Verify archived bytes without consulting the evolving checkout or deployed bin directory."""
    directory = Path(directory).resolve()
    identity = read_json(directory / "frozen.json")
    if identity.get("schemaVersion") != 1 or identity.get("role") not in ("frozen-pre-repair-runtime", "frozen-repair-runtime") or not identity.get("files"):
        raise ValueError("unsupported frozen identity")
    for name, sha in identity["files"].items():
        path = (directory / name).resolve()
        if not path.is_relative_to(directory) or not path.is_file() or digest(path) != sha:
            raise ValueError("frozen artifact changed or missing: " + name)
    actual = {p.relative_to(directory).as_posix() for p in directory.rglob("*") if p.is_file()}
    if actual != set(identity["files"]) | {"frozen.json"}:
        raise ValueError("unrecorded frozen artifact")
    return identity


def freeze(args):
    identity = verify_identity(args.identity, args.binaries)
    args.out.mkdir(parents=True, exist_ok=False)
    runtime = args.out / "runtime"
    runtime.mkdir()
    for name in identity["binaries"]:
        shutil.copy2(args.binaries / name, runtime / name)
    shutil.copy2(args.identity, args.out / "build-identity.json")
    shutil.copy2(args.identity.parent / "build.log", args.out / "build.log")
    shutil.copy2(ROOT / DEPENDENCY_LOCK, args.out / "dependency-lock.json")
    with zipfile.ZipFile(args.out / "sources.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for name, sha in identity["source"]["files"].items():
            if sha != "missing":
                archive.write(ROOT / name, name)
        for index, name in enumerate(identity["buildInputs"]):
            archive.write(name, "observed-build-inputs/" + str(index) + "-" + Path(name).name)
    verify_identity(args.identity, args.binaries)
    if binary_identity(runtime) != identity["binaries"]:
        raise ValueError("runtime changed during freeze")
    repair = identity["role"] == "same-checkout-repair"
    write_json(args.out / "frozen.json", {
        "schemaVersion": 1, "role": "frozen-repair-runtime" if repair else "frozen-pre-repair-runtime",
        "claim": "the repaired engine's runtime" if repair else "the engine's pre-repair runtime",
        "sourceHead": identity["source"]["head"], "engineTreesAtHead": identity["source"]["engineTreesAtHead"],
        "files": {p.relative_to(args.out).as_posix(): digest(p) for p in sorted(args.out.rglob("*")) if p.is_file()}})
    verify_frozen(args.out)


#: Generalized-hyperbolic boundary profiles: each bias regime at each limit, plus two regime-free cases.
GHS_BOUNDARY_PROFILES = ("zero", "coincident") + tuple(
    f"{limit}_{regime}" for regime in ("log", "near_log", "integral", "below_exponential", "exponential",
                                       "above_exponential", "hyperbolic")
    for limit in ("tiny", "protected", "large", "max_float", "symmetry_black", "symmetry_white"))
#: Midtones transfer boundary profiles: each balance class over the unit interval and between
#: interior endpoints, plus the endpoint limits.
MTF_BOUNDARY_PROFILES = ("identity", "zero_midtones", "tiny_midtones", "near_one_midtones", "one_midtones",
                         "shadows", "highlights", "clipped", "clipped_tiny_midtones", "clipped_near_one_midtones",
                         "narrow", "step", "step_black")
#: Statistical stretch boundary profiles: a sky with a black margin and cold pixels far below its black
#: point, the capture whose minimum the bounded sample misses, and the non-quantized control's limits.
STATISTICAL_BOUNDARY_PROFILES = ("sky", "sky_no_black_clip", "sky_normalize_curves", "eagle_no_black_clip",
                                 "uniform_no_black_clip", "uniform_sigma_black", "uniform_zero_sigma",
                                 "uniform_normalize", "uniform_curves", "uniform_full_boost", "uniform_tiny_target",
                                 "uniform_near_one_target")


def stretch_catalogue(document):
    """The stretch rows of a runner's dumped catalogue, refused unless they are exactly the stretch family."""
    rows = [r for r in document["scenarios"] if r["group"] == "stretch"]
    indexed = {r["id"]: r for r in rows}
    if len(indexed) != len(rows):
        raise ValueError("duplicate stretch key")
    expected = {f"stretch.function_{i}.f32" for i in range(10)}
    expected |= {f"stretch.uniform_1mp.function_{i}.{profile}.mono_f32.ordinary"
                 for i in (0, 1, 2, 3, 5, 6, 8, 9) for profile in ("audit", "interactive")}
    expected |= {f"stretch.boundaries.function_{i}.{profile}.mono_f32.ordinary"
                 for i in (2, 3) for profile in ("zero", "tiny", "large", "protected", "coincident", "symmetry_black", "symmetry_white", "max_float")}
    expected |= {f"stretch.boundaries.function_{i}.{profile}.mono_f32.ordinary"
                 for i in (0, 1) for profile in GHS_BOUNDARY_PROFILES}
    expected |= {f"stretch.boundaries.function_8.{profile}.mono_f32.ordinary" for profile in MTF_BOUNDARY_PROFILES}
    expected |= {f"stretch.boundaries.function_9.{profile}.mono_f32.ordinary"
                 for profile in STATISTICAL_BOUNDARY_PROFILES}
    expected |= {f"stretch.{dataset}.function_9.prepared.mono_f32.ordinary" for dataset in CAPTURES}
    for dataset in CAPTURES:
        expected |= {f"stretch.{dataset}.function_{i}.{profile}.mono_f32.ordinary"
                     for i in range(10) for profile in ("audit", "interactive")
                     if profile == "audit" or i != 7}
    if set(indexed) != expected or any(r["operationVersion"] != 2 for r in indexed.values()):
        raise ValueError("missing, unexpected or obsolete stretch cases")
    return list(indexed.values())


def verify_prepared(directory, catalogue):
    """Check pinned bytes and exact catalogue membership, not just a self-reported manifest hash."""
    directory = Path(directory)
    inputs = read_json(directory / "inputs.json")
    if (inputs["scenariosSha256"] != digest(directory / "scenarios.json") or
            inputs["corpusManifestSha256"] != digest(directory / "corpus.json")):
        raise ValueError("prepared input manifest changed")
    if read_json(directory / "scenarios.json")["scenarios"] != catalogue:
        raise ValueError("prepared catalogue or parameters changed")
    entries = read_json(directory / "corpus.json")["datasets"]
    if len(entries) != len(CAPTURES) or {e["id"] for e in entries} != {
            "stretch." + name + ".unit_f32.v1" for name in CAPTURES}:
        raise ValueError("prepared dataset coverage changed")
    for name, (_, width, height, descriptor_sha, source_sha, output_sha) in CAPTURES.items():
        entry = next(e for e in entries if e["id"] == "stretch." + name + ".unit_f32.v1")
        descriptor = directory / entry["relativePath"]
        raw = read_json(descriptor)
        payload = descriptor.parent / raw["payloadFile"]
        if (digest(descriptor) != entry["sha256"] or digest(payload) != output_sha or
                raw["payloadDigest"] != output_sha or payload.stat().st_size != width * height * 4 or
                inputs["captures"][name]["sourceDescriptorSha256"] != descriptor_sha or
                inputs["captures"][name]["sourcePayloadSha256"] != source_sha):
            raise ValueError("prepared capture identity changed: " + name)
    return inputs


def prepare(args):
    import numpy as np

    rows = stretch_catalogue(read_json(args.catalogue))
    original = read_json(args.corpus_manifest)
    entries = {row["id"]: row for row in original["datasets"]}
    args.out.mkdir(parents=True, exist_ok=False)
    fixtures = args.out / "corpus"
    fixtures.mkdir()
    datasets, provenance = [], {}
    for name, (source_id, width, height, descriptor_sha, source_sha, output_sha) in CAPTURES.items():
        entry = entries[source_id]
        descriptor = args.corpus_root / entry["relativePath"]
        if entry["sha256"] != descriptor_sha or digest(descriptor) != descriptor_sha:
            raise ValueError("capture descriptor changed: " + name)
        raw = read_json(descriptor)
        payload = descriptor.parent / raw["payloadFile"]
        expected = {"width": width, "height": height, "channels": 1, "sampleType": "UInt16",
                    "layout": "Planar", "byteOrder": "Little", "rowStrideBytes": 0,
                    "planeStrideBytes": 0, "cfaPattern": ""}
        if raw["descriptor"] != expected or entry["expected"] != expected:
            raise ValueError("capture shape changed: " + name)
        if raw["payloadDigest"] != source_sha or digest(payload) != source_sha:
            raise ValueError("capture payload changed: " + name)
        if payload.stat().st_size != width * height * 2:
            raise ValueError("capture size changed: " + name)
        values = np.fromfile(payload, dtype="<u2").astype("<f4")
        values /= np.float32(65535)
        target = fixtures / (name + ".f32")
        values.tofile(target)
        if digest(target) != output_sha:
            raise ValueError("Float32 derivation differs from the pinned audit: " + name)
        expected["sampleType"] = "Float32"
        derived = fixtures / (name + ".json")
        write_json(derived, {"schemaVersion": 1, "descriptor": expected, "sourcePath": source_id,
                             "sourceFormat": "verified UInt16 raw dump", "exporterVersion": "stretch-unit-f32-1",
                             "payloadFile": target.name, "payloadBytes": target.stat().st_size,
                             "payloadDigest": output_sha, "cards": []})
        result = copy.deepcopy(entry)
        result.update(id=f"stretch.{name}.unit_f32.v1", relativePath="corpus/" + derived.name,
                      sha256=digest(derived), sizeBytes=derived.stat().st_size, expected=expected,
                      preprocessing="UInt16 / Float32(65535), outside timing", preprocessingVersion="stretch-unit-f32-1",
                      goldenPath="", goldenSha256="", goldenTolerance=0.0)
        datasets.append(result)
        provenance[name] = {"sourceDataset": source_id, "sourceDescriptorSha256": descriptor_sha,
                            "sourcePayloadSha256": source_sha, "payloadSha256": output_sha,
                            "descriptorSha256": digest(derived)}
    manifest = {"schemaVersion": original["schemaVersion"], "datasets": datasets, "frameSets": []}
    write_json(args.out / "corpus.json", manifest)
    write_json(args.out / "scenarios.json", {"schemaVersion": 2, "scenarios": rows})
    write_json(args.out / "inputs.json", {"captures": provenance, "numpy": np.__version__,
               "scenariosSha256": digest(args.out / "scenarios.json"),
               "corpusManifestSha256": digest(args.out / "corpus.json"),
               "timedBoundary": "resident input and destination; complete stretch call including per-call planning/analysis; fixture conversion, allocation and output checks excluded",
               "policy": "ordinary DecisionFeeding; requested threads supplied at launch, admitted threads in each result",
               "qualification": "pending full-output comparison and paired duration-qualified campaign"})
    verify_prepared(args.out, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    build = actions.add_parser("build")
    build.add_argument("--out", type=Path, required=True)
    build.add_argument("--binaries", type=Path, required=True)
    build.add_argument("--role", choices=("same-checkout-pre-repair", "same-checkout-repair"), default="same-checkout-pre-repair")
    build.add_argument("--build-input", type=Path, action="append", default=[])
    build.add_argument("--command", nargs=argparse.REMAINDER, required=True)
    verify = actions.add_parser("verify")
    verify.add_argument("--identity", type=Path, required=True)
    verify.add_argument("--binaries", type=Path, required=True)
    frozen = actions.add_parser("freeze")
    for option in ("identity", "binaries", "out"):
        frozen.add_argument("--" + option, type=Path, required=True)
    frozen_verify = actions.add_parser("verify-frozen")
    frozen_verify.add_argument("--directory", type=Path, required=True)
    preparation = actions.add_parser("prepare")
    for option in ("catalogue", "corpus-manifest", "corpus-root", "out"):
        preparation.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    if args.action == "build":
        record_build(args)
    elif args.action == "verify":
        verify_identity(args.identity, args.binaries)
    elif args.action == "freeze":
        freeze(args)
    elif args.action == "verify-frozen":
        verify_frozen(args.directory)
    else:
        prepare(args)
    print("stretch benchmark " + args.action + " passed")


if __name__ == "__main__":
    main()
