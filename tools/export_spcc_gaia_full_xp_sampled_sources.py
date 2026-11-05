#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import csv
import gzip
import json
import math
import shutil
import sys
import time
from collections import OrderedDict
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path
from typing import Any, TextIO

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


TOOL_VERSION = "spcc-gaia-full-xp-sampled-source-exporter-v1"
STATE_FILE_NAME = ".spcc_full_xp_source_export_state.json"
WORK_DIR_NAME = ".spcc_full_xp_source_export_work"
METADATA_INPUTS_DIR_NAME = "metadata_inputs"
SPECTRUM_INPUTS_DIR_NAME = "spectrum_inputs"
JOIN_STATE_DIR_NAME = "join_state"
PARTS_DIR_NAME = "parts"
COMPLETE_MARKER_NAME = "complete.json"
MANIFEST_FILE_NAME = "spcc_full_xp_sampled_source_export_manifest.json"
DEFAULT_PARTITION_COUNT = 4096
DEFAULT_PART_PREFIX = "spcc_full_xp_sampled_sources"
DEFAULT_MAX_OPEN_FILES = 128


class SimulatedInterruption(RuntimeError):
    pass


class PartitionWriter:
    def __init__(self, base_dir: Path, *, max_open_files: int) -> None:
        self.base_dir = base_dir
        self.max_open_files = max(1, max_open_files)
        self.handles: OrderedDict[int, TextIO] = OrderedDict()
        self.base_dir.mkdir(parents=True, exist_ok=True)

    def write(self, partition: int, row: dict[str, Any]) -> None:
        handle = self.handles.pop(partition, None)
        if handle is None:
            path = bucket_path(self.base_dir, partition)
            handle = path.open("a", encoding="utf-8")
        self.handles[partition] = handle
        handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
        if len(self.handles) > self.max_open_files:
            _, old_handle = self.handles.popitem(last=False)
            old_handle.close()

    def close(self) -> None:
        for handle in self.handles.values():
            handle.close()
        self.handles.clear()

    def __enter__(self) -> "PartitionWriter":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Export resumable prejoined Gaia DR3 source metadata + XP sampled spectra JSONL shards "
            "for the AfterNight SPCC full-sky catalog builder."
        )
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--metadata-csv", type=Path, action="append", default=[])
    parser.add_argument(
        "--metadata-manifest",
        type=Path,
        action="append",
        default=[],
        help="Manifest written by export_spcc_gaia_xp_metadata.py; all listed CSV.GZ parts are used as metadata inputs.",
    )
    parser.add_argument("--metadata-jsonl", type=Path, action="append", default=[])
    parser.add_argument("--xp-sampled-csv", type=Path, action="append", default=[])
    parser.add_argument(
        "--xp-sampled-manifest",
        type=Path,
        action="append",
        default=[],
        help="Manifest written by download_spcc_gaia_xp_sampled_files.py; all listed CSV.GZ files are used as spectra inputs.",
    )
    parser.add_argument("--partition-count", type=int, default=DEFAULT_PARTITION_COUNT)
    parser.add_argument("--part-prefix", default=DEFAULT_PART_PREFIX)
    parser.add_argument("--max-open-files", type=int, default=DEFAULT_MAX_OPEN_FILES)
    parser.add_argument(
        "--workers",
        type=int,
        default=1,
        help="Number of worker processes for independent partition/join tasks. Use 1 for deterministic serial output.",
    )
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--keep-work", action="store_true")
    parser.add_argument(
        "--simulate-interruption-after",
        choices=["metadata", "spectra", "first-bucket"],
        help="Testing hook that leaves durable resume state behind and exits with code 2.",
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        export_sources(args)
    except SimulatedInterruption as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def export_sources(args: argparse.Namespace) -> None:
    output_dir: Path = args.output_dir
    state_path = output_dir / STATE_FILE_NAME
    if args.clean and args.resume:
        raise ValueError("--clean and --resume cannot be used together")
    if args.workers <= 0:
        raise ValueError("--workers must be positive")
    if args.clean and output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.resume:
        state = spcc.read_json(state_path)
    else:
        if state_path.exists():
            raise ValueError(f"resume state already exists; rerun with --resume or remove {state_path}")
        if (output_dir / MANIFEST_FILE_NAME).exists():
            raise ValueError(f"{output_dir} already contains an export manifest; rerun with --clean to replace it")
        if args.partition_count <= 0:
            raise ValueError("--partition-count must be positive")
        if args.max_open_files <= 0:
            raise ValueError("--max-open-files must be positive")
        metadata_csv = collect_metadata_csv_paths(args.metadata_csv, args.metadata_manifest)
        xp_sampled_csv = collect_xp_sampled_csv_paths(args.xp_sampled_csv, args.xp_sampled_manifest)
        if not metadata_csv and not args.metadata_jsonl:
            raise ValueError("at least one --metadata-csv, --metadata-manifest, or --metadata-jsonl is required")
        if not xp_sampled_csv:
            raise ValueError("at least one --xp-sampled-csv or --xp-sampled-manifest is required")
        state = {
            "schema_version": "spcc-full-xp-sampled-source-export-state-v1",
            "tool": "export_spcc_gaia_full_xp_sampled_sources.py",
            "tool_version": TOOL_VERSION,
            "metadata_csv": [str(path) for path in metadata_csv],
            "metadata_manifests": [str(path) for path in args.metadata_manifest],
            "metadata_jsonl": [str(path) for path in args.metadata_jsonl],
            "xp_sampled_csv": [str(path) for path in xp_sampled_csv],
            "xp_sampled_manifests": [str(path) for path in args.xp_sampled_manifest],
            "partition_count": args.partition_count,
            "part_prefix": safe_part_prefix(args.part_prefix),
            "max_open_files": args.max_open_files,
            "created_at_utc": spcc.utc_now_iso(),
            "updated_at_utc": spcc.utc_now_iso(),
        }
        spcc.write_json(state_path, state)

    ensure_work_dirs(output_dir)
    print_export_context(output_dir, state)
    partition_metadata_inputs(output_dir, state, workers=args.workers)
    state["metadata_partition_completed_at_utc"] = spcc.utc_now_iso()
    state["updated_at_utc"] = spcc.utc_now_iso()
    spcc.write_json(state_path, state)
    if args.simulate_interruption_after == "metadata":
        raise SimulatedInterruption("simulated interruption after SPCC full-XP metadata partitioning")

    partition_spectrum_inputs(output_dir, state, workers=args.workers)
    state["spectrum_partition_completed_at_utc"] = spcc.utc_now_iso()
    state["updated_at_utc"] = spcc.utc_now_iso()
    spcc.write_json(state_path, state)
    if args.simulate_interruption_after == "spectra":
        raise SimulatedInterruption("simulated interruption after SPCC full-XP spectrum partitioning")

    join_partitioned_inputs(
        output_dir,
        state,
        workers=args.workers,
        simulate_first_bucket=args.simulate_interruption_after == "first-bucket",
    )
    manifest = build_export_manifest(output_dir, state)
    if manifest["joined_rows"] <= 0:
        raise ValueError("SPCC full-XP source export produced no joined rows")
    spcc.write_json(output_dir / MANIFEST_FILE_NAME, manifest)

    if not args.keep_work:
        shutil.rmtree(output_dir / WORK_DIR_NAME, ignore_errors=True)
    if state_path.exists():
        state_path.unlink()

    print(f"SPCC full XP sampled source export staged: {output_dir}")
    print(f"Parts: {len(manifest['part_files'])}")
    print(f"Joined rows: {manifest['joined_rows']}")


def ensure_work_dirs(output_dir: Path) -> None:
    for name in [METADATA_INPUTS_DIR_NAME, SPECTRUM_INPUTS_DIR_NAME, JOIN_STATE_DIR_NAME]:
        (output_dir / WORK_DIR_NAME / name).mkdir(parents=True, exist_ok=True)
    (output_dir / PARTS_DIR_NAME).mkdir(parents=True, exist_ok=True)


def print_export_context(output_dir: Path, state: dict[str, Any]) -> None:
    metadata_done = count_input_markers(output_dir / WORK_DIR_NAME / METADATA_INPUTS_DIR_NAME)
    spectrum_done = count_input_markers(output_dir / WORK_DIR_NAME / SPECTRUM_INPUTS_DIR_NAME)
    join_done = len(read_join_markers(output_dir))
    print("SPCC full XP sampled source export", flush=True)
    print(f"  output: {output_dir}", flush=True)
    print(
        f"  metadata_inputs: {len(state['metadata_csv']) + len(state['metadata_jsonl'])} "
        f"completed={metadata_done}",
        flush=True,
    )
    print(f"  metadata_manifests: {len(state.get('metadata_manifests', []))}", flush=True)
    print(f"  xp_sampled_inputs: {len(state['xp_sampled_csv'])} completed={spectrum_done}", flush=True)
    print(f"  xp_sampled_manifests: {len(state.get('xp_sampled_manifests', []))}", flush=True)
    print(f"  partitions: {state['partition_count']} joined={join_done}", flush=True)
    print(f"  max_open_files: {state['max_open_files']}", flush=True)


def collect_metadata_csv_paths(metadata_csv: list[Path], metadata_manifests: list[Path]) -> list[Path]:
    paths = list(metadata_csv)
    for manifest_path in metadata_manifests:
        if not manifest_path.is_file():
            raise ValueError(f"metadata manifest is missing: {manifest_path}")
        manifest = spcc.read_json(manifest_path)
        if manifest.get("schema_version") != "spcc-gaia-xp-metadata-export-manifest-v1":
            raise ValueError(f"{manifest_path}: unsupported SPCC Gaia XP metadata manifest schema")
        part_files = manifest.get("part_files")
        if not isinstance(part_files, list):
            raise ValueError(f"{manifest_path}: metadata manifest is missing part_files")
        for entry in part_files:
            if not isinstance(entry, dict) or "path" not in entry:
                raise ValueError(f"{manifest_path}: metadata manifest contains an invalid part_files entry")
            path = Path(str(entry["path"]))
            if not path.is_absolute():
                path = manifest_path.parent / path
            paths.append(path)
    return paths


def collect_xp_sampled_csv_paths(xp_sampled_csv: list[Path], xp_sampled_manifests: list[Path]) -> list[Path]:
    paths = list(xp_sampled_csv)
    for manifest_path in xp_sampled_manifests:
        if not manifest_path.is_file():
            raise ValueError(f"XP sampled manifest is missing: {manifest_path}")
        manifest = spcc.read_json(manifest_path)
        if manifest.get("schema_version") != "spcc-gaia-xp-sampled-download-manifest-v1":
            raise ValueError(f"{manifest_path}: unsupported SPCC Gaia XP sampled download manifest schema")
        part_files = manifest.get("part_files")
        if not isinstance(part_files, list):
            raise ValueError(f"{manifest_path}: XP sampled manifest is missing part_files")
        for entry in part_files:
            if not isinstance(entry, dict) or "path" not in entry:
                raise ValueError(f"{manifest_path}: XP sampled manifest contains an invalid part_files entry")
            path = Path(str(entry["path"]))
            if not path.is_absolute():
                path = manifest_path.parent / path
            paths.append(path)
    return paths


def partition_metadata_inputs(output_dir: Path, state: dict[str, Any], *, workers: int) -> None:
    inputs = [(Path(path), "csv") for path in state["metadata_csv"]]
    inputs.extend((Path(path), "jsonl") for path in state["metadata_jsonl"])
    base_dir = output_dir / WORK_DIR_NAME / METADATA_INPUTS_DIR_NAME
    stage_started_at = time.perf_counter()
    completed_at_start = count_input_markers(base_dir)
    completed_this_run = 0
    total = len(inputs)
    if workers > 1:
        partition_input_stage_parallel(
            stage="metadata",
            output_dir=output_dir,
            base_dir=base_dir,
            inputs=inputs,
            partition_count=int(state["partition_count"]),
            max_open_files=int(state["max_open_files"]),
            workers=workers,
        )
        return

    for input_index, (input_path, input_kind) in enumerate(inputs):
        input_dir = base_dir / f"input_{input_index:04d}"
        marker_path = input_dir / COMPLETE_MARKER_NAME
        if marker_path.exists():
            continue
        if not input_path.is_file():
            raise ValueError(f"metadata input is missing: {input_path}")
        if input_dir.exists():
            shutil.rmtree(input_dir)
        input_dir.mkdir(parents=True)

        print(
            f"[metadata {input_index + 1}/{total}] starting {input_path.name} "
            f"size={format_bytes(input_path.stat().st_size)} kind={input_kind}",
            flush=True,
        )
        input_started_at = time.perf_counter()
        row_count = 0
        partition_counts: dict[int, int] = {}
        with PartitionWriter(input_dir, max_open_files=int(state["max_open_files"])) as writer:
            for line_number, row in enumerate(iter_metadata_rows(input_path, input_kind), start=1):
                metadata = normalize_metadata_row(row, input_path, line_number)
                partition = source_partition(int(metadata["source_id"]), int(state["partition_count"]))
                writer.write(partition, metadata)
                partition_counts[partition] = partition_counts.get(partition, 0) + 1
                row_count += 1

        elapsed = time.perf_counter() - input_started_at
        spcc.write_json(
            marker_path,
            {
                "input_path": str(input_path),
                "input_kind": input_kind,
                "input_sha256": spcc.sha256_file(input_path),
                "row_count": row_count,
                "partition_counts": {str(key): value for key, value in sorted(partition_counts.items())},
                "completed_at_utc": spcc.utc_now_iso(),
            },
        )
        completed_this_run += 1
        print_stage_progress(
            "metadata",
            completed_at_start + completed_this_run,
            total,
            input_path.name,
            row_count,
            elapsed,
            stage_started_at,
            completed_this_run,
        )


def partition_spectrum_inputs(output_dir: Path, state: dict[str, Any], *, workers: int) -> None:
    base_dir = output_dir / WORK_DIR_NAME / SPECTRUM_INPUTS_DIR_NAME
    inputs = [(Path(path), "xp_sampled_csv") for path in state["xp_sampled_csv"]]
    if workers > 1:
        partition_input_stage_parallel(
            stage="spectra",
            output_dir=output_dir,
            base_dir=base_dir,
            inputs=inputs,
            partition_count=int(state["partition_count"]),
            max_open_files=int(state["max_open_files"]),
            workers=workers,
        )
        return

    stage_started_at = time.perf_counter()
    completed_at_start = count_input_markers(base_dir)
    completed_this_run = 0
    total = len(inputs)
    for input_index, (input_path, _) in enumerate(inputs):
        input_dir = base_dir / f"input_{input_index:04d}"
        marker_path = input_dir / COMPLETE_MARKER_NAME
        if marker_path.exists():
            continue
        if not input_path.is_file():
            raise ValueError(f"XP sampled input is missing: {input_path}")
        if input_dir.exists():
            shutil.rmtree(input_dir)
        input_dir.mkdir(parents=True)

        print(
            f"[spectra {input_index + 1}/{total}] starting {input_path.name} "
            f"size={format_bytes(input_path.stat().st_size)}",
            flush=True,
        )
        input_started_at = time.perf_counter()
        row_count = 0
        partition_counts: dict[int, int] = {}
        with PartitionWriter(input_dir, max_open_files=int(state["max_open_files"])) as writer:
            for line_number, row in enumerate(iter_csv_rows(input_path), start=1):
                spectrum = normalize_spectrum_row(row, input_path, line_number)
                partition = source_partition(int(spectrum["source_id"]), int(state["partition_count"]))
                writer.write(partition, spectrum)
                partition_counts[partition] = partition_counts.get(partition, 0) + 1
                row_count += 1

        elapsed = time.perf_counter() - input_started_at
        spcc.write_json(
            marker_path,
            {
                "input_path": str(input_path),
                "input_kind": "xp_sampled_csv",
                "input_sha256": spcc.sha256_file(input_path),
                "row_count": row_count,
                "partition_counts": {str(key): value for key, value in sorted(partition_counts.items())},
                "completed_at_utc": spcc.utc_now_iso(),
            },
        )
        completed_this_run += 1
        print_stage_progress(
            "spectra",
            completed_at_start + completed_this_run,
            total,
            input_path.name,
            row_count,
            elapsed,
            stage_started_at,
            completed_this_run,
        )


def join_partitioned_inputs(
    output_dir: Path, state: dict[str, Any], *, workers: int, simulate_first_bucket: bool
) -> None:
    join_state_dir = output_dir / WORK_DIR_NAME / JOIN_STATE_DIR_NAME
    part_dir = output_dir / PARTS_DIR_NAME
    simulated = False
    stage_started_at = time.perf_counter()
    completed_at_start = len(read_join_markers(output_dir))
    completed_this_run = 0
    total = int(state["partition_count"])
    if workers > 1:
        pending = []
        for partition in range(total):
            marker_path = join_state_dir / f"partition_{partition:06d}.complete.json"
            if not marker_path.exists():
                pending.append(partition)
        if not pending:
            return
        with ProcessPoolExecutor(max_workers=workers) as executor:
            futures = [
                executor.submit(run_join_partition_task, str(output_dir), state, partition) for partition in pending
            ]
            for future in as_completed(futures):
                marker = future.result()
                completed_this_run += 1
                elapsed = float(marker["elapsed_seconds"])
                completed_total = completed_at_start + completed_this_run
                avg = (time.perf_counter() - stage_started_at) / completed_this_run
                eta = avg * max(0, total - completed_total)
                print(
                    f"[join {completed_total}/{total}] completed partition {int(marker['partition']):06d} "
                    f"metadata_rows={marker['metadata_rows']} spectrum_rows={marker['spectrum_rows']} "
                    f"joined_rows={marker['joined_rows']} unmatched_spectrum_rows={marker['unmatched_spectrum_rows']} "
                    f"elapsed={format_duration(elapsed)} avg={format_duration(avg)} eta={format_duration(eta)}",
                    flush=True,
                )
        return

    for partition in range(int(state["partition_count"])):
        marker_path = join_state_dir / f"partition_{partition:06d}.complete.json"
        if marker_path.exists():
            continue

        print(f"[join {partition + 1}/{total}] starting partition {partition:06d}", flush=True)
        partition_started_at = time.perf_counter()
        part_path = part_file_path(part_dir, state["part_prefix"], partition)
        if part_path.exists():
            part_path.unlink()

        metadata = load_metadata_partition(output_dir, partition)
        spectrum_rows = 0
        joined_rows = 0
        unmatched_spectrum_rows = 0
        matched_source_ids: set[int] = set()
        if metadata:
            with part_path.open("w", encoding="utf-8") as part_handle:
                for spectrum in iter_spectrum_partition(output_dir, partition):
                    spectrum_rows += 1
                    source_id = int(spectrum["source_id"])
                    metadata_row = metadata.get(source_id)
                    if metadata_row is None:
                        unmatched_spectrum_rows += 1
                        continue
                    joined = build_joined_row(metadata_row, spectrum)
                    validate_joined_row(joined, part_path, joined_rows + 1)
                    part_handle.write(json.dumps(joined, sort_keys=True, separators=(",", ":")) + "\n")
                    matched_source_ids.add(source_id)
                    joined_rows += 1
        else:
            for _ in iter_spectrum_partition(output_dir, partition):
                spectrum_rows += 1
                unmatched_spectrum_rows += 1

        if joined_rows == 0 and part_path.exists():
            part_path.unlink()
        marker: dict[str, Any] = {
            "partition": partition,
            "metadata_rows": len(metadata),
            "spectrum_rows": spectrum_rows,
            "joined_rows": joined_rows,
            "unmatched_spectrum_rows": unmatched_spectrum_rows,
            "metadata_without_spectrum_rows": max(0, len(metadata) - len(matched_source_ids)),
            "completed_at_utc": spcc.utc_now_iso(),
        }
        if joined_rows:
            marker["part_file"] = part_path.relative_to(output_dir).as_posix()
            marker["part_sha256"] = spcc.sha256_file(part_path)
            marker["part_size_bytes"] = part_path.stat().st_size
        spcc.write_json(marker_path, marker)
        completed_this_run += 1
        elapsed = time.perf_counter() - partition_started_at
        completed_total = completed_at_start + completed_this_run
        avg = (time.perf_counter() - stage_started_at) / completed_this_run
        eta = avg * max(0, total - completed_total)
        print(
            f"[join {completed_total}/{total}] completed partition {partition:06d} "
            f"metadata_rows={len(metadata)} spectrum_rows={spectrum_rows} joined_rows={joined_rows} "
            f"unmatched_spectrum_rows={unmatched_spectrum_rows} elapsed={format_duration(elapsed)} "
            f"avg={format_duration(avg)} eta={format_duration(eta)}",
            flush=True,
        )

        if simulate_first_bucket and joined_rows and not simulated:
            simulated = True
            raise SimulatedInterruption("simulated interruption after first SPCC full-XP join bucket")


def partition_input_stage_parallel(
    *,
    stage: str,
    output_dir: Path,
    base_dir: Path,
    inputs: list[tuple[Path, str]],
    partition_count: int,
    max_open_files: int,
    workers: int,
) -> None:
    stage_started_at = time.perf_counter()
    completed_at_start = count_input_markers(base_dir)
    completed_this_run = 0
    total = len(inputs)
    pending: list[tuple[int, Path, str]] = []
    for input_index, (input_path, input_kind) in enumerate(inputs):
        marker_path = base_dir / f"input_{input_index:04d}" / COMPLETE_MARKER_NAME
        if not marker_path.exists():
            pending.append((input_index, input_path, input_kind))
    if not pending:
        return

    with ProcessPoolExecutor(max_workers=workers) as executor:
        futures = [
            executor.submit(
                run_partition_input_task,
                str(base_dir),
                input_index,
                str(input_path),
                input_kind,
                partition_count,
                max_open_files,
            )
            for input_index, input_path, input_kind in pending
        ]
        for future in as_completed(futures):
            marker = future.result()
            completed_this_run += 1
            completed_total = completed_at_start + completed_this_run
            elapsed = float(marker["elapsed_seconds"])
            avg = (time.perf_counter() - stage_started_at) / completed_this_run
            eta = avg * max(0, total - completed_total)
            print(
                f"[{stage} {completed_total}/{total}] completed {Path(marker['input_path']).name} "
                f"rows={marker['row_count']} elapsed={format_duration(elapsed)} "
                f"avg={format_duration(avg)} eta={format_duration(eta)}",
                flush=True,
            )


def run_partition_input_task(
    base_dir_name: str,
    input_index: int,
    input_name: str,
    input_kind: str,
    partition_count: int,
    max_open_files: int,
) -> dict[str, Any]:
    base_dir = Path(base_dir_name)
    input_path = Path(input_name)
    input_dir = base_dir / f"input_{input_index:04d}"
    marker_path = input_dir / COMPLETE_MARKER_NAME
    if marker_path.exists():
        marker = spcc.read_json(marker_path)
        marker["elapsed_seconds"] = 0.0
        return marker
    if not input_path.is_file():
        raise ValueError(f"input is missing: {input_path}")
    if input_dir.exists():
        shutil.rmtree(input_dir)
    input_dir.mkdir(parents=True)

    started_at = time.perf_counter()
    row_count = 0
    partition_counts: dict[int, int] = {}
    with PartitionWriter(input_dir, max_open_files=max_open_files) as writer:
        if input_kind == "csv":
            rows = iter_metadata_rows(input_path, input_kind)
            for line_number, row in enumerate(rows, start=1):
                metadata = normalize_metadata_row(row, input_path, line_number)
                partition = source_partition(int(metadata["source_id"]), partition_count)
                writer.write(partition, metadata)
                partition_counts[partition] = partition_counts.get(partition, 0) + 1
                row_count += 1
        elif input_kind == "jsonl":
            rows = iter_metadata_rows(input_path, input_kind)
            for line_number, row in enumerate(rows, start=1):
                metadata = normalize_metadata_row(row, input_path, line_number)
                partition = source_partition(int(metadata["source_id"]), partition_count)
                writer.write(partition, metadata)
                partition_counts[partition] = partition_counts.get(partition, 0) + 1
                row_count += 1
        elif input_kind == "xp_sampled_csv":
            for line_number, row in enumerate(iter_csv_rows(input_path), start=1):
                spectrum = normalize_spectrum_row(row, input_path, line_number)
                partition = source_partition(int(spectrum["source_id"]), partition_count)
                writer.write(partition, spectrum)
                partition_counts[partition] = partition_counts.get(partition, 0) + 1
                row_count += 1
        else:
            raise ValueError(f"unsupported input kind: {input_kind}")

    marker = {
        "input_path": str(input_path),
        "input_kind": input_kind if input_kind != "xp_sampled_csv" else "xp_sampled_csv",
        "input_sha256": spcc.sha256_file(input_path),
        "row_count": row_count,
        "partition_counts": {str(key): value for key, value in sorted(partition_counts.items())},
        "completed_at_utc": spcc.utc_now_iso(),
        "elapsed_seconds": time.perf_counter() - started_at,
    }
    spcc.write_json(marker_path, {key: value for key, value in marker.items() if key != "elapsed_seconds"})
    return marker


def run_join_partition_task(output_dir_name: str, state: dict[str, Any], partition: int) -> dict[str, Any]:
    output_dir = Path(output_dir_name)
    join_state_dir = output_dir / WORK_DIR_NAME / JOIN_STATE_DIR_NAME
    part_dir = output_dir / PARTS_DIR_NAME
    marker_path = join_state_dir / f"partition_{partition:06d}.complete.json"
    if marker_path.exists():
        marker = spcc.read_json(marker_path)
        marker["elapsed_seconds"] = 0.0
        return marker

    started_at = time.perf_counter()
    part_path = part_file_path(part_dir, state["part_prefix"], partition)
    if part_path.exists():
        part_path.unlink()

    metadata = load_metadata_partition(output_dir, partition)
    spectrum_rows = 0
    joined_rows = 0
    unmatched_spectrum_rows = 0
    matched_source_ids: set[int] = set()
    if metadata:
        with part_path.open("w", encoding="utf-8") as part_handle:
            for spectrum in iter_spectrum_partition(output_dir, partition):
                spectrum_rows += 1
                source_id = int(spectrum["source_id"])
                metadata_row = metadata.get(source_id)
                if metadata_row is None:
                    unmatched_spectrum_rows += 1
                    continue
                joined = build_joined_row(metadata_row, spectrum)
                validate_joined_row(joined, part_path, joined_rows + 1)
                part_handle.write(json.dumps(joined, sort_keys=True, separators=(",", ":")) + "\n")
                matched_source_ids.add(source_id)
                joined_rows += 1
    else:
        for _ in iter_spectrum_partition(output_dir, partition):
            spectrum_rows += 1
            unmatched_spectrum_rows += 1

    if joined_rows == 0 and part_path.exists():
        part_path.unlink()
    marker: dict[str, Any] = {
        "partition": partition,
        "metadata_rows": len(metadata),
        "spectrum_rows": spectrum_rows,
        "joined_rows": joined_rows,
        "unmatched_spectrum_rows": unmatched_spectrum_rows,
        "metadata_without_spectrum_rows": max(0, len(metadata) - len(matched_source_ids)),
        "completed_at_utc": spcc.utc_now_iso(),
        "elapsed_seconds": time.perf_counter() - started_at,
    }
    if joined_rows:
        marker["part_file"] = part_path.relative_to(output_dir).as_posix()
        marker["part_sha256"] = spcc.sha256_file(part_path)
        marker["part_size_bytes"] = part_path.stat().st_size
    spcc.write_json(marker_path, {key: value for key, value in marker.items() if key != "elapsed_seconds"})
    return marker


def build_export_manifest(output_dir: Path, state: dict[str, Any]) -> dict[str, Any]:
    metadata_inputs = read_input_markers(output_dir / WORK_DIR_NAME / METADATA_INPUTS_DIR_NAME)
    spectrum_inputs = read_input_markers(output_dir / WORK_DIR_NAME / SPECTRUM_INPUTS_DIR_NAME)
    join_markers = read_join_markers(output_dir)
    part_files = [
        {
            "path": marker["part_file"],
            "partition": marker["partition"],
            "row_count": marker["joined_rows"],
            "size_bytes": marker["part_size_bytes"],
            "sha256": marker["part_sha256"],
        }
        for marker in join_markers
        if marker.get("joined_rows", 0) > 0
    ]
    return {
        "schema_version": "spcc-full-xp-sampled-source-export-manifest-v1",
        "tool": "export_spcc_gaia_full_xp_sampled_sources.py",
        "tool_version": TOOL_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "gaia_archive_release": "Gaia DR3",
        "metadata_inputs": metadata_inputs,
        "metadata_manifests": [metadata_manifest_entry(Path(path)) for path in state.get("metadata_manifests", [])],
        "spectrum_inputs": spectrum_inputs,
        "xp_sampled_manifests": [input_manifest_entry(Path(path)) for path in state.get("xp_sampled_manifests", [])],
        "partition_count": state["partition_count"],
        "part_prefix": state["part_prefix"],
        "metadata_rows": sum(int(entry["row_count"]) for entry in metadata_inputs),
        "spectrum_rows": sum(int(entry["row_count"]) for entry in spectrum_inputs),
        "joined_rows": sum(int(marker["joined_rows"]) for marker in join_markers),
        "unmatched_spectrum_rows": sum(int(marker["unmatched_spectrum_rows"]) for marker in join_markers),
        "metadata_without_spectrum_rows": sum(int(marker["metadata_without_spectrum_rows"]) for marker in join_markers),
        "part_files": part_files,
        "required_gaia_source_fields": spcc.REQUIRED_GAIA_SOURCE_FIELDS,
        "required_xp_summary_fields": spcc.REQUIRED_XP_SUMMARY_FIELDS,
        "required_sampled_spectrum_fields": spcc.REQUIRED_SAMPLED_SPECTRUM_FIELDS,
    }


def input_manifest_entry(path: Path) -> dict[str, Any]:
    return {
        "path": str(path),
        "sha256": spcc.sha256_file(path) if path.is_file() else None,
        "size_bytes": path.stat().st_size if path.is_file() else None,
    }


def metadata_manifest_entry(path: Path) -> dict[str, Any]:
    return input_manifest_entry(path)


def read_input_markers(base_dir: Path) -> list[dict[str, Any]]:
    entries = []
    for marker_path in sorted(base_dir.glob(f"input_*/{COMPLETE_MARKER_NAME}")):
        marker = spcc.read_json(marker_path)
        entries.append(
            {
                "path": marker["input_path"],
                "kind": marker["input_kind"],
                "sha256": marker["input_sha256"],
                "row_count": marker["row_count"],
            }
        )
    return entries


def count_input_markers(base_dir: Path) -> int:
    return sum(1 for _ in base_dir.glob(f"input_*/{COMPLETE_MARKER_NAME}"))


def read_join_markers(output_dir: Path) -> list[dict[str, Any]]:
    markers = []
    join_state_dir = output_dir / WORK_DIR_NAME / JOIN_STATE_DIR_NAME
    for marker_path in sorted(join_state_dir.glob("partition_*.complete.json")):
        markers.append(spcc.read_json(marker_path))
    return markers


def iter_metadata_rows(path: Path, input_kind: str) -> Any:
    if input_kind == "jsonl":
        with open_text(path) as handle:
            for line in handle:
                if line.strip():
                    yield json.loads(line)
        return
    yield from iter_csv_rows(path)


def iter_csv_rows(path: Path) -> Any:
    with open_text(path) as handle:
        reader = csv.DictReader(non_comment_lines(handle))
        for row in reader:
            yield row


def open_text(path: Path) -> TextIO:
    if "".join(path.suffixes[-2:]).lower() == ".csv.gz" or path.suffix.lower() == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", newline="")
    return path.open("r", encoding="utf-8", newline="")


def non_comment_lines(handle: TextIO) -> Any:
    for line in handle:
        if not line.startswith("#"):
            yield line


def normalize_metadata_row(row: dict[str, Any], path: Path, line_number: int) -> dict[str, Any]:
    required = list(dict.fromkeys(spcc.REQUIRED_GAIA_SOURCE_FIELDS + spcc.REQUIRED_XP_SUMMARY_FIELDS))
    for field in required:
        if field not in row or (value_is_blank(row[field]) and field not in spcc.OPTIONAL_FLOAT_METADATA_DEFAULTS):
            raise ValueError(f"{path}:{line_number}: missing required metadata field {field}")
    if not parse_bool(row["has_xp_sampled"]):
        raise ValueError(f"{path}:{line_number}: has_xp_sampled must be true")

    output: dict[str, Any] = {}
    for field in required:
        value = row[field]
        if field in {"source_id"}:
            output[field] = int(value)
        elif field in spcc.OPTIONAL_FLOAT_METADATA_DEFAULTS and value_is_blank(value):
            output[field] = spcc.OPTIONAL_FLOAT_METADATA_DEFAULTS[field]
        elif field in INTEGER_METADATA_FIELDS:
            output[field] = int(float(value))
        elif field in BOOLEAN_METADATA_FIELDS:
            output[field] = parse_bool(value)
        elif field in STRING_METADATA_FIELDS:
            output[field] = str(value)
        else:
            output[field] = finite_float(value, path, line_number, field)
    return output


def normalize_spectrum_row(row: dict[str, Any], path: Path, line_number: int) -> dict[str, Any]:
    for field in ["source_id", "solution_id", "flux", "flux_error"]:
        if field not in row or value_is_blank(row[field]):
            raise ValueError(f"{path}:{line_number}: missing required spectrum field {field}")
    flux = parse_xp_array(row["flux"], path, line_number, "flux")
    flux_error = parse_xp_array(row["flux_error"], path, line_number, "flux_error")
    for sample in flux_error:
        if sample < 0.0:
            raise ValueError(f"{path}:{line_number}: flux_error samples must be non-negative")
    return {
        "source_id": int(row["source_id"]),
        "solution_id": str(row["solution_id"]),
        "flux": flux,
        "flux_error": flux_error,
    }


def parse_xp_array(value: Any, path: Path, line_number: int, field: str) -> list[float]:
    if isinstance(value, list):
        parsed = value
    else:
        try:
            parsed = json.loads(str(value).replace("'", '"'))
        except json.JSONDecodeError as exc:
            raise ValueError(f"{path}:{line_number}: {field} is not a JSON/ECSV array") from exc
    samples = [float(item) for item in parsed]
    spcc.ensure_spectrum_length(samples, field)
    for sample in samples:
        if not math.isfinite(sample):
            raise ValueError(f"{path}:{line_number}: {field} samples must be finite")
    return samples


def build_joined_row(metadata_row: dict[str, Any], spectrum: dict[str, Any]) -> dict[str, Any]:
    row = dict(metadata_row)
    row["solution_id"] = spectrum["solution_id"]
    row["flux"] = spectrum["flux"]
    row["flux_error"] = spectrum["flux_error"]
    row["quality_flags"] = 0
    row["spectrum_source_kind"] = 0
    return row


def validate_joined_row(row: dict[str, Any], path: Path, line_number: int) -> None:
    for field in ["source_id", "ra", "dec", "phot_g_mean_mag", "phot_bp_mean_mag", "phot_rp_mean_mag"]:
        if field not in row:
            raise ValueError(f"{path}:{line_number}: joined row missing {field}")
    spcc.ensure_spectrum_length(row["flux"], "flux")
    spcc.ensure_spectrum_length(row["flux_error"], "flux_error")


def load_metadata_partition(output_dir: Path, partition: int) -> dict[int, dict[str, Any]]:
    metadata: dict[int, dict[str, Any]] = {}
    base_dir = output_dir / WORK_DIR_NAME / METADATA_INPUTS_DIR_NAME
    for bucket in sorted(base_dir.glob(f"input_*/bucket_{partition:06d}.jsonl")):
        with bucket.open("r", encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, start=1):
                row = json.loads(line)
                source_id = int(row["source_id"])
                if source_id in metadata:
                    raise ValueError(f"{bucket}:{line_number}: duplicate metadata source_id {source_id}")
                metadata[source_id] = row
    return metadata


def iter_spectrum_partition(output_dir: Path, partition: int) -> Any:
    base_dir = output_dir / WORK_DIR_NAME / SPECTRUM_INPUTS_DIR_NAME
    for bucket in sorted(base_dir.glob(f"input_*/bucket_{partition:06d}.jsonl")):
        with bucket.open("r", encoding="utf-8") as handle:
            for line in handle:
                yield json.loads(line)


def finite_float(value: Any, path: Path, line_number: int, field: str) -> float:
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{path}:{line_number}: metadata field {field} must be finite")
    return number


def parse_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    text = str(value).strip().lower()
    if text in {"true", "t", "1", "yes", "y"}:
        return True
    if text in {"false", "f", "0", "no", "n"}:
        return False
    raise ValueError(f"cannot parse boolean value: {value!r}")


def value_is_blank(value: Any) -> bool:
    return value is None or str(value).strip() == ""


def source_partition(source_id: int, partition_count: int) -> int:
    return source_id % partition_count


def bucket_path(base_dir: Path, partition: int) -> Path:
    return base_dir / f"bucket_{partition:06d}.jsonl"


def part_file_path(part_dir: Path, part_prefix: str, partition: int) -> Path:
    return part_dir / f"{part_prefix}_partition_{partition:06d}.jsonl"


def print_stage_progress(
    stage: str,
    completed_total: int,
    total: int,
    name: str,
    row_count: int,
    elapsed: float,
    stage_started_at: float,
    completed_this_run: int,
) -> None:
    avg = (time.perf_counter() - stage_started_at) / completed_this_run
    eta = avg * max(0, total - completed_total)
    print(
        f"[{stage} {completed_total}/{total}] completed {name} rows={row_count} "
        f"elapsed={format_duration(elapsed)} avg={format_duration(avg)} eta={format_duration(eta)}",
        flush=True,
    )


def format_duration(seconds: float) -> str:
    seconds = max(0, int(round(seconds)))
    if seconds < 60:
        return f"{seconds}s"
    minutes, seconds = divmod(seconds, 60)
    if minutes < 60:
        return f"{minutes}m{seconds:02d}s"
    hours, minutes = divmod(minutes, 60)
    if hours < 48:
        return f"{hours}h{minutes:02d}m"
    days, hours = divmod(hours, 24)
    return f"{days}d{hours:02d}h"


def format_bytes(value: int) -> str:
    size = float(value)
    for suffix in ["B", "KB", "MB", "GB", "TB"]:
        if size < 1024.0 or suffix == "TB":
            return f"{size:.1f}{suffix}" if suffix != "B" else f"{int(size)}B"
        size /= 1024.0
    return f"{size:.1f}TB"


def safe_part_prefix(value: str) -> str:
    safe = "".join(char if char.isalnum() or char in "._-" else "_" for char in value.strip())
    return safe.strip("._-") or DEFAULT_PART_PREFIX


INTEGER_METADATA_FIELDS = {
    "phot_bp_n_contaminated_transits",
    "phot_bp_n_blended_transits",
    "phot_rp_n_contaminated_transits",
    "phot_rp_n_blended_transits",
    "non_single_star",
    "ipd_frac_multi_peak",
    "bp_n_relevant_bases",
    "bp_n_measurements",
    "bp_n_rejected_measurements",
    "bp_n_transits",
    "bp_n_contaminated_transits",
    "bp_n_blended_transits",
    "rp_n_relevant_bases",
    "rp_n_measurements",
    "rp_n_rejected_measurements",
    "rp_n_transits",
    "rp_n_contaminated_transits",
    "rp_n_blended_transits",
}
BOOLEAN_METADATA_FIELDS = {"duplicated_source", "has_xp_sampled"}
STRING_METADATA_FIELDS = {"phot_variable_flag"}


if __name__ == "__main__":
    raise SystemExit(main())
