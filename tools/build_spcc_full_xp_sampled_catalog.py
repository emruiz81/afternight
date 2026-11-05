#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import heapq
import json
import math
import shutil
import sys
import time
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


TOOL_VERSION = "spcc-full-xp-sampled-catalog-builder-v1"
STATE_FILE_NAME = ".spcc_full_export_state.json"
WORK_DIR_NAME = ".spcc_full_export_work"
PARTITIONS_DIR_NAME = "partitions"
COMPLETE_MARKER_NAME = "complete.json"
DEFAULT_CAP_PER_HEALPIX8 = 127
DEFAULT_CHUNK_PARENT_ORDER = spcc.CHUNK_PARENT_HEALPIX_ORDER
HEALPIX8_ORDER = 8


class SimulatedInterruption(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build a resumable full-sky AfterNight SPCC Gaia DR3 XP sampled catalog from durable "
            "prejoined source/spectrum JSONL exports."
        )
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--input-jsonl",
        type=Path,
        action="append",
        default=[],
        help=(
            "Prejoined Gaia source + XP sampled JSONL. Can be supplied multiple times. "
            "Rows must contain source_id, astrometry/photometry metadata, flux, and flux_error."
        ),
    )
    parser.add_argument(
        "--input-manifest",
        type=Path,
        help=(
            "Manifest written by export_spcc_gaia_full_xp_sampled_sources.py. "
            "Its part_files entries are used as --input-jsonl files."
        ),
    )
    parser.add_argument("--manifest-id", default="spcc-gaia-dr3-xp-sampled-full-2026b")
    parser.add_argument("--rights-review-status", default="private-local")
    parser.add_argument("--cap-per-healpix8", type=int, default=DEFAULT_CAP_PER_HEALPIX8)
    parser.add_argument("--chunk-parent-order", type=int, default=DEFAULT_CHUNK_PARENT_ORDER)
    parser.add_argument(
        "--allow-partial-full-sky",
        action="store_true",
        help="Testing/development only; permits local-full fixture builds that do not cover every sky chunk.",
    )
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--keep-work", action="store_true")
    parser.add_argument(
        "--simulate-interruption-after",
        choices=["partition", "first-chunk"],
        help="Testing hook that leaves durable resume state behind and exits with code 2.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        build_catalog(args)
    except SimulatedInterruption as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def build_catalog(args: argparse.Namespace) -> None:
    output_dir: Path = args.output_dir
    state_path = output_dir / STATE_FILE_NAME

    if args.clean and args.resume:
        raise ValueError("--clean and --resume cannot be used together")
    if args.clean and output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.resume:
        state = spcc.read_json(state_path)
    else:
        if state_path.exists():
            raise ValueError(f"resume state already exists; rerun with --resume or remove {state_path}")
        if (output_dir / "catalog_meta.json").exists():
            raise ValueError(f"{output_dir} already contains a catalog; rerun with --clean to replace it")
        input_jsonl = collect_input_jsonl(args)
        if not input_jsonl:
            raise ValueError("at least one --input-jsonl or --input-manifest is required for a new full SPCC catalog build")
        if args.cap_per_healpix8 <= 0:
            raise ValueError("--cap-per-healpix8 must be positive")
        if args.chunk_parent_order < 0 or args.chunk_parent_order > HEALPIX8_ORDER:
            raise ValueError("--chunk-parent-order must be in [0, 8]")
        if args.manifest_id == spcc.CATALOG_FAMILY_ID:
            raise ValueError("manifest-id must be immutable and distinct from the catalog family id")
        state = {
            "schema_version": "spcc-full-xp-sampled-build-state-v1",
            "tool": "build_spcc_full_xp_sampled_catalog.py",
            "tool_version": TOOL_VERSION,
            "manifest_id": args.manifest_id,
            "rights_review_status": args.rights_review_status,
            "cap_per_healpix8": args.cap_per_healpix8,
            "chunk_parent_order": args.chunk_parent_order,
            "input_jsonl": [str(path) for path in input_jsonl],
            "input_manifest": str(args.input_manifest) if args.input_manifest else None,
            "input_manifest_sha256": spcc.sha256_file(args.input_manifest) if args.input_manifest else "",
            "allow_partial_full_sky": args.allow_partial_full_sky,
            "created_at_utc": spcc.utc_now_iso(),
            "updated_at_utc": spcc.utc_now_iso(),
        }
        spcc.write_json(state_path, state)

    work_dir = output_dir / WORK_DIR_NAME
    partitions_dir = work_dir / PARTITIONS_DIR_NAME
    chunks_dir = output_dir / "chunks"
    partitions_dir.mkdir(parents=True, exist_ok=True)
    chunks_dir.mkdir(parents=True, exist_ok=True)

    print_build_context(output_dir, state)
    partition_inputs(output_dir, state)
    state["partition_completed_at_utc"] = spcc.utc_now_iso()
    state["updated_at_utc"] = spcc.utc_now_iso()
    spcc.write_json(state_path, state)
    if args.simulate_interruption_after == "partition":
        raise SimulatedInterruption("simulated interruption after durable SPCC full-export partitioning")

    chunk_entries = write_chunks(output_dir, state, simulate_first_chunk=args.simulate_interruption_after == "first-chunk")
    if not chunk_entries:
        raise ValueError("full SPCC catalog build produced no chunks")
    validate_local_full_chunk_coverage(state, chunk_entries)

    finalize_catalog(output_dir, state, chunk_entries)
    if not args.keep_work and work_dir.exists():
        shutil.rmtree(work_dir)
    if state_path.exists():
        state_path.unlink()

    print(f"SPCC full XP sampled catalog staged: {output_dir}")
    print(f"Chunks: {len(chunk_entries)}")
    print(f"Records: {sum(int(chunk['record_count']) for chunk in chunk_entries)}")


def collect_input_jsonl(args: argparse.Namespace) -> list[Path]:
    input_paths = list(args.input_jsonl)
    if args.input_manifest:
        manifest = spcc.read_json(args.input_manifest)
        manifest_dir = args.input_manifest.parent
        for entry in manifest.get("part_files", []):
            raw_path = entry["path"] if isinstance(entry, dict) else str(entry)
            path = Path(raw_path)
            input_paths.append(path if path.is_absolute() else manifest_dir / path)
    return input_paths


def print_build_context(output_dir: Path, state: dict[str, Any]) -> None:
    partitions_dir = output_dir / WORK_DIR_NAME / PARTITIONS_DIR_NAME
    chunk_state_dir = output_dir / WORK_DIR_NAME / "chunk_state"
    partition_done = count_partition_markers(partitions_dir)
    chunk_done = sum(1 for _ in chunk_state_dir.glob("chunk_*.complete.json")) if chunk_state_dir.exists() else 0
    print("SPCC full XP sampled catalog build", flush=True)
    print(f"  output: {output_dir}", flush=True)
    print(f"  manifest_id: {state['manifest_id']}", flush=True)
    print(f"  rights_review_status: {state['rights_review_status']}", flush=True)
    print(f"  input_jsonl: {len(state['input_jsonl'])} partitioned={partition_done}", flush=True)
    print(f"  cap_per_healpix8: {state['cap_per_healpix8']}", flush=True)
    print(f"  chunk_parent_order: {state['chunk_parent_order']} chunks_completed={chunk_done}", flush=True)
    print(f"  allow_partial_full_sky: {bool(state.get('allow_partial_full_sky', False))}", flush=True)


def partition_inputs(output_dir: Path, state: dict[str, Any]) -> None:
    partitions_dir = output_dir / WORK_DIR_NAME / PARTITIONS_DIR_NAME
    stage_started_at = time.perf_counter()
    completed_at_start = count_partition_markers(partitions_dir)
    completed_this_run = 0
    total = len(state["input_jsonl"])
    for input_index, input_name in enumerate(state["input_jsonl"]):
        input_path = Path(input_name)
        if not input_path.is_file():
            raise ValueError(f"input JSONL is missing: {input_path}")
        input_dir = partitions_dir / f"input_{input_index:04d}"
        marker_path = input_dir / COMPLETE_MARKER_NAME
        if marker_path.exists():
            continue
        if input_dir.exists():
            shutil.rmtree(input_dir)
        input_dir.mkdir(parents=True)

        print(
            f"[partition {input_index + 1}/{total}] starting {input_path.name} "
            f"size={format_bytes(input_path.stat().st_size)}",
            flush=True,
        )
        input_started_at = time.perf_counter()
        row_count = 0
        parent_counts: dict[int, int] = {}
        with input_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                if not line.strip():
                    continue
                row = json.loads(line)
                validate_input_row(row, input_path, row_count + 1)
                parent = parent_chunk_from_source_id(int(row["source_id"]), int(state["chunk_parent_order"]))
                parent_path = input_dir / f"parent_{parent:04d}.jsonl"
                with parent_path.open("a", encoding="utf-8") as parent_handle:
                    parent_handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
                row_count += 1
                parent_counts[parent] = parent_counts.get(parent, 0) + 1

        elapsed = time.perf_counter() - input_started_at
        spcc.write_json(
            marker_path,
            {
                "input_jsonl": str(input_path),
                "input_sha256": spcc.sha256_file(input_path),
                "row_count": row_count,
                "parent_counts": {str(key): value for key, value in sorted(parent_counts.items())},
                "completed_at_utc": spcc.utc_now_iso(),
            },
        )
        completed_this_run += 1
        print_stage_progress(
            "partition",
            completed_at_start + completed_this_run,
            total,
            input_path.name,
            row_count,
            elapsed,
            stage_started_at,
            completed_this_run,
        )


def validate_input_row(row: dict[str, Any], path: Path, line_number: int) -> None:
    required = [
        "source_id",
        "ra",
        "dec",
        "phot_g_mean_mag",
        "phot_bp_mean_mag",
        "phot_rp_mean_mag",
        "flux",
        "flux_error",
    ]
    for key in required:
        if key not in row:
            raise ValueError(f"{path}:{line_number}: missing required field {key}")
    spcc.ensure_spectrum_length(list(row["flux"]), "flux")
    spcc.ensure_spectrum_length(list(row["flux_error"]), "flux_error")
    if not math.isfinite(float(row["phot_g_mean_mag"])):
        raise ValueError(f"{path}:{line_number}: phot_g_mean_mag must be finite")


def write_chunks(output_dir: Path, state: dict[str, Any], *, simulate_first_chunk: bool) -> list[dict[str, Any]]:
    chunks_dir = output_dir / "chunks"
    chunk_state_dir = output_dir / WORK_DIR_NAME / "chunk_state"
    chunk_state_dir.mkdir(parents=True, exist_ok=True)
    parent_ids = discover_parent_ids(output_dir)
    chunk_entries: list[dict[str, Any]] = []
    simulated = False
    stage_started_at = time.perf_counter()
    completed_at_start = sum(1 for _ in chunk_state_dir.glob("chunk_*.complete.json"))
    completed_this_run = 0
    total = len(parent_ids)
    print(f"[chunk] discovered {total} parent chunks to write/check", flush=True)
    for parent_index, parent in enumerate(parent_ids, start=1):
        marker_path = chunk_state_dir / f"chunk_{parent:04d}.complete.json"
        chunk_path = chunk_file_path(chunks_dir, parent)
        if marker_path.exists() and not chunk_path.exists():
            marker_path.unlink()
        if not marker_path.exists():
            print(f"[chunk {parent_index}/{total}] starting parent {parent:04d}", flush=True)
            chunk_started_at = time.perf_counter()
            rows = select_parent_rows(output_dir, parent, int(state["cap_per_healpix8"]))
            if not rows:
                continue
            write_parent_chunk(chunk_path, state, parent, rows)
            chunk_entry = build_chunk_entry(output_dir, chunk_path, parent, int(state["chunk_parent_order"]), len(rows))
            spcc.write_json(
                marker_path,
                {
                    "chunk_index": parent,
                    "record_count": len(rows),
                    "sha256": chunk_entry["sha256"],
                    "completed_at_utc": spcc.utc_now_iso(),
                },
            )
            completed_this_run += 1
            print_stage_progress(
                "chunk",
                completed_at_start + completed_this_run,
                total,
                f"parent_{parent:04d}",
                len(rows),
                time.perf_counter() - chunk_started_at,
                stage_started_at,
                completed_this_run,
            )
            if simulate_first_chunk and not simulated:
                simulated = True
                raise SimulatedInterruption("simulated interruption after first SPCC full-export chunk")
        if chunk_path.exists():
            _, records = spcc.read_chunk(chunk_path)
            chunk_entries.append(build_chunk_entry(output_dir, chunk_path, parent, int(state["chunk_parent_order"]), len(records)))
    return chunk_entries


def discover_parent_ids(output_dir: Path) -> list[int]:
    parent_ids: set[int] = set()
    partitions_dir = output_dir / WORK_DIR_NAME / PARTITIONS_DIR_NAME
    for parent_path in partitions_dir.glob("input_*/parent_*.jsonl"):
        parent_ids.add(int(parent_path.stem.split("_", 1)[1]))
    return sorted(parent_ids)


def select_parent_rows(output_dir: Path, parent: int, cap_per_healpix8: int) -> list[dict[str, Any]]:
    heaps: dict[int, list[tuple[float, int, dict[str, Any]]]] = {}
    sequence = 0
    partitions_dir = output_dir / WORK_DIR_NAME / PARTITIONS_DIR_NAME
    for parent_path in sorted(partitions_dir.glob(f"input_*/parent_{parent:04d}.jsonl")):
        with parent_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                if not line.strip():
                    continue
                row = json.loads(line)
                healpix8 = healpix8_from_source_id(int(row["source_id"]))
                heap = heaps.setdefault(healpix8, [])
                entry = (-float(row["phot_g_mean_mag"]), sequence, row)
                sequence += 1
                if len(heap) < cap_per_healpix8:
                    heapq.heappush(heap, entry)
                elif entry > heap[0]:
                    heapq.heapreplace(heap, entry)

    selected: list[dict[str, Any]] = []
    for heap in heaps.values():
        selected.extend(entry[2] for entry in heap)
    return sorted(
        selected,
        key=lambda row: (
            healpix8_from_source_id(int(row["source_id"])),
            float(row["phot_g_mean_mag"]),
            int(row["source_id"]),
        ),
    )


def write_parent_chunk(chunk_path: Path, state: dict[str, Any], parent: int, rows: list[dict[str, Any]]) -> None:
    packed_records = [spcc.pack_record(row) for row in rows]
    first_tile = first_order6_tile_for_parent(parent, int(state["chunk_parent_order"]))
    tile_count = order6_tile_count_for_parent(int(state["chunk_parent_order"]))
    header = {
        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
        "manifest_id": state["manifest_id"],
        "chunk_index": parent,
        "first_tile": first_tile,
        "tile_count": tile_count,
        "record_size_bytes": spcc.ANXP_RECORD_SIZE_BYTES,
        "record_count": len(packed_records),
        "wavelength_grid_id": "gaia-dr3-xp-sampled-336-1020nm-2nm",
    }
    spcc.write_chunk(chunk_path, header, packed_records)


def build_chunk_entry(
    output_dir: Path, chunk_path: Path, parent: int, parent_order: int, record_count: int
) -> dict[str, Any]:
    return {
        "chunk_index": parent,
        "first_tile": first_order6_tile_for_parent(parent, parent_order),
        "tile_count": order6_tile_count_for_parent(parent_order),
        "file": chunk_path.relative_to(output_dir).as_posix(),
        "record_count": record_count,
        "size_bytes": chunk_path.stat().st_size,
        "sha256": spcc.sha256_file(chunk_path),
    }


def finalize_catalog(output_dir: Path, state: dict[str, Any], chunk_entries: list[dict[str, Any]]) -> None:
    chunk_entries = sorted(chunk_entries, key=lambda chunk: int(chunk["chunk_index"]))
    total_records = sum(int(chunk["record_count"]) for chunk in chunk_entries)
    chunk_manifest_path = output_dir / "chunk_manifest.json"
    spcc.write_json(
        chunk_manifest_path,
        {
            "catalog_family_id": spcc.CATALOG_FAMILY_ID,
            "manifest_id": state["manifest_id"],
            "chunks": chunk_entries,
        },
    )

    source_id_index_path = output_dir / "source_id_index.json"
    stats = write_source_id_index_and_collect_stats(output_dir, source_id_index_path, state, chunk_entries)
    source_transcript_path = output_dir / "source_count_transcript.json"
    spcc.write_json(source_transcript_path, build_source_transcript(output_dir, state, total_records, stats))
    coverage_path = output_dir / "coverage_summary.json"
    spcc.write_json(coverage_path, build_coverage_summary(state, chunk_entries, total_records, stats))

    meta = build_catalog_meta(
        output_dir=output_dir,
        state=state,
        chunk_entries=chunk_entries,
        total_records=total_records,
        stats=stats,
        chunk_manifest_path=chunk_manifest_path,
        coverage_path=coverage_path,
        source_id_index_path=source_id_index_path,
        source_transcript_path=source_transcript_path,
    )
    spcc.write_json(output_dir / "catalog_meta.json", meta)


def validate_local_full_chunk_coverage(state: dict[str, Any], chunk_entries: list[dict[str, Any]]) -> None:
    if bool(state.get("allow_partial_full_sky", False)):
        return
    errors = spcc.local_full_chunk_coverage_errors(
        chunk_entries,
        access_profile="local-full",
        source_kind="gaia_dr3_xp_sampled",
        parent_order=int(state["chunk_parent_order"]),
        healpix_order=spcc.HEALPIX_ORDER,
    )
    if not errors:
        return
    raise ValueError(
        "; ".join(errors)
        + ". Re-run the metadata export over HEALPix-8 0..786431, download all XP sampled files, "
        "and rebuild from the complete prejoin manifest. Use --allow-partial-full-sky only for test fixtures."
    )


def write_source_id_index_and_collect_stats(
    output_dir: Path, source_id_index_path: Path, state: dict[str, Any], chunk_entries: list[dict[str, Any]]
) -> dict[str, Any]:
    source_count = 0
    source_id_min: int | None = None
    source_id_max: int | None = None
    bp_rp_values: list[float] = []
    with source_id_index_path.open("w", encoding="utf-8") as handle:
        handle.write("{\n")
        handle.write(f'  "catalog_family_id": {json.dumps(spcc.CATALOG_FAMILY_ID)},\n')
        handle.write(f'  "manifest_id": {json.dumps(state["manifest_id"])},\n')
        handle.write('  "sources": {\n')
        first = True
        for chunk in chunk_entries:
            _, records = spcc.read_chunk(output_dir / chunk["file"])
            for record_index, record in enumerate(records):
                source_id = int(record["source_id"])
                source_id_min = source_id if source_id_min is None else min(source_id_min, source_id)
                source_id_max = source_id if source_id_max is None else max(source_id_max, source_id)
                bp_rp_values.append(float(record["phot_bp_mean_mag"]) - float(record["phot_rp_mean_mag"]))
                if not first:
                    handle.write(",\n")
                first = False
                handle.write(
                    "    "
                    + json.dumps(str(source_id))
                    + ": "
                    + json.dumps({"chunk_index": chunk["chunk_index"], "record_index": record_index})
                )
                source_count += 1
        handle.write("\n  },\n")
        handle.write(f'  "source_count": {source_count}\n')
        handle.write("}\n")

    return {
        "source_count": source_count,
        "source_id_min": source_id_min or 0,
        "source_id_max": source_id_max or 0,
        "bp_rp_p10_p90_span": percentile_span(bp_rp_values, 0.10, 0.90),
    }


def build_source_transcript(
    output_dir: Path, state: dict[str, Any], total_records: int, stats: dict[str, Any]
) -> dict[str, Any]:
    input_entries = []
    for input_index, input_name in enumerate(state["input_jsonl"]):
        marker = (
            output_dir / WORK_DIR_NAME / PARTITIONS_DIR_NAME / f"input_{input_index:04d}" / COMPLETE_MARKER_NAME
        )
        marker_data = spcc.read_json(marker) if marker.exists() else {}
        input_entries.append(
            {
                "path": input_name,
                "sha256": marker_data.get("input_sha256", ""),
                "row_count": marker_data.get("row_count", 0),
            }
        )
    return {
        "schema_version": "spcc-full-xp-sampled-source-transcript-v1",
        "tool": "build_spcc_full_xp_sampled_catalog.py",
        "tool_version": TOOL_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "generation_mode": "local-jsonl-full-xp-sampled",
        "source_service_url": "local-jsonl://after-night/spcc-full-xp-sampled",
        "gaia_archive_release": "Gaia DR3",
        "gaia_tables": [
            "gaiadr3.gaia_source",
            "gaiadr3.xp_summary",
            "gaiadr3.xp_sampled_mean_spectrum",
        ],
        "selected_columns": sorted(
            set(spcc.REQUIRED_GAIA_SOURCE_FIELDS)
            | set(spcc.REQUIRED_XP_SUMMARY_FIELDS)
            | set(spcc.REQUIRED_SAMPLED_SPECTRUM_FIELDS)
        ),
        "selection_policy": {
            "cap_per_healpix8": state["cap_per_healpix8"],
            "brightness_field": "phot_g_mean_mag",
            "chunk_parent_order": state["chunk_parent_order"],
        },
        "input_jsonl": input_entries,
        "returned_count": total_records,
        "source_id_min": stats["source_id_min"],
        "source_id_max": stats["source_id_max"],
    }


def count_partition_markers(partitions_dir: Path) -> int:
    return sum(1 for _ in partitions_dir.glob(f"input_*/{COMPLETE_MARKER_NAME}"))


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


def build_coverage_summary(
    state: dict[str, Any], chunk_entries: list[dict[str, Any]], total_records: int, stats: dict[str, Any]
) -> dict[str, Any]:
    return {
        "schema_version": "spcc-coverage-summary-v1",
        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
        "manifest_id": state["manifest_id"],
        "access_profile": "local-full",
        "healpix_order": spcc.HEALPIX_ORDER,
        "chunk_parent_healpix_order": state["chunk_parent_order"],
        "generated_at_utc": spcc.utc_now_iso(),
        "total_records": total_records,
        "estimated_usable_candidates": total_records,
        "bp_rp_p10_p90_span": stats["bp_rp_p10_p90_span"],
        "quality_cut_class": "gaia-dr3-xp-sampled-full-export",
        "chunk_status": [
            {
                "chunk_index": chunk["chunk_index"],
                "available": True,
                "stale": False,
                "record_count": chunk["record_count"],
                "sha256": chunk["sha256"],
            }
            for chunk in chunk_entries
        ],
        "diagnostics": [],
    }


def build_catalog_meta(
    *,
    output_dir: Path,
    state: dict[str, Any],
    chunk_entries: list[dict[str, Any]],
    total_records: int,
    stats: dict[str, Any],
    chunk_manifest_path: Path,
    coverage_path: Path,
    source_id_index_path: Path,
    source_transcript_path: Path,
) -> dict[str, Any]:
    generated_at_utc = spcc.utc_now_iso()
    source_service_urls = ["local-jsonl://after-night/spcc-full-xp-sampled"]
    return {
        "schema_version": "spcc-catalog-meta-v1",
        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
        "manifest_id": state["manifest_id"],
        "access_profile": "local-full",
        "source_kind": "gaia_dr3_xp_sampled",
        "payload_kind": "sampled_spectra_with_optional_synthetic_cache",
        "total_records": total_records,
        "rights": {
            "review_status": state["rights_review_status"],
            "reviewed_at_utc": None,
            "reviewed_by": None,
            "review_basis": "",
            "source_terms_urls": [
                "https://gea.esac.esa.int/archive/documentation/GDR3/",
                "https://www.cosmos.esa.int/web/gaia/dr3",
                "https://huggingface.co/terms-of-service",
            ],
            "license_name": "",
            "license_url": "",
            "required_credit": [],
            "required_citations": [
                "ESA/Gaia/DPAC credit and Gaia DR3 citations required by the generated catalog manifest.",
            ],
            "redistribution_policy": "private-only"
            if state["rights_review_status"] == "private-local"
            else "unknown",
            "derivative_data_policy": "unknown",
            "mirror_hosting_policy": "staging-only"
            if state["rights_review_status"] == "private-local"
            else "unknown",
            "notes": "Gaia-derived SPCC catalogs require explicit redistribution and mirror-hosting review before public upload.",
        },
        "provenance": {
            "source_family": "gaia-dr3-xp-sampled-full-local-export",
            "source_release": "Gaia DR3",
            "source_service_urls": source_service_urls,
            "source_query_text": "prejoined local JSONL export; see source_count_transcript.json",
            "retrieval_method": "local-jsonl-full-xp-sampled",
            "retrieval_tool": "build_spcc_full_xp_sampled_catalog.py",
            "retrieval_tool_version": TOOL_VERSION,
            "generated_at_utc": generated_at_utc,
            "source_query_timestamp_utc": generated_at_utc,
            "source_query_sha256": spcc.sha256_file(source_transcript_path),
            "input_manifest_ids": [],
            "output_manifest_id": state["manifest_id"],
            "checksum_algorithm": "sha256",
        },
        "format": {
            "magic": spcc.ANXP_MAGIC.decode("ascii"),
            "format_version": spcc.ANXP_FORMAT_VERSION,
            "record_version": spcc.ANXP_RECORD_VERSION,
            "record_size_bytes": spcc.ANXP_RECORD_SIZE_BYTES,
            "reserved_record_bytes": spcc.ANXP_RESERVED_RECORD_BYTES,
            "byte_order": "little-endian",
            "healpix_ordering": "nested",
            "healpix_order": spcc.HEALPIX_ORDER,
            "chunk_parent_healpix_order": state["chunk_parent_order"],
        },
        "wavelength_grid": {
            "unit": "nm",
            "sample_count": spcc.SAMPLED_XP_SAMPLE_COUNT,
            "start_nm": spcc.SAMPLED_XP_WAVELENGTH_START_NM,
            "step_nm": spcc.SAMPLED_XP_WAVELENGTH_STEP_NM,
            "end_nm": spcc.SAMPLED_XP_WAVELENGTH_END_NM,
        },
        "flux_unit": "W.m**-2.nm**-1",
        "uncertainty_payload": {
            "flux_error_samples": "present",
            "uncertainty_policy": "propagate_xp_flux_error",
            "unweighted_fit_fallback_allowed": False,
        },
        "quality_fields": {
            "required_gaia_source_fields": spcc.REQUIRED_GAIA_SOURCE_FIELDS,
            "required_xp_summary_fields": spcc.REQUIRED_XP_SUMMARY_FIELDS,
            "required_sampled_spectrum_fields": spcc.REQUIRED_SAMPLED_SPECTRUM_FIELDS,
            "missing_mandatory_field_sentinel": "manifest-required",
        },
        "files": {
            "chunk_manifest": chunk_manifest_path.relative_to(output_dir).as_posix(),
            "coverage_summary": coverage_path.relative_to(output_dir).as_posix(),
            "source_id_index": source_id_index_path.relative_to(output_dir).as_posix(),
            "source_count_transcript": source_transcript_path.relative_to(output_dir).as_posix(),
        },
        "chunk_manifest_sha256": spcc.sha256_file(chunk_manifest_path),
        "coverage_summary_sha256": spcc.sha256_file(coverage_path),
        "source_id_index_sha256": spcc.sha256_file(source_id_index_path),
        "source_count_transcript_sha256": spcc.sha256_file(source_transcript_path),
        "chunks": chunk_entries,
        "build_summary": {
            "cap_per_healpix8": state["cap_per_healpix8"],
            "source_id_min": stats["source_id_min"],
            "source_id_max": stats["source_id_max"],
        },
    }


def percentile_span(values: list[float], lower: float, upper: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    lower_index = int((len(values) - 1) * lower)
    upper_index = int((len(values) - 1) * upper)
    return values[upper_index] - values[lower_index]


def chunk_file_path(chunks_dir: Path, parent: int) -> Path:
    return chunks_dir / f"{spcc.CATALOG_FAMILY_ID}_chunk_{parent:04d}.anxp"


def healpix8_from_source_id(source_id: int) -> int:
    healpix_order12 = source_id // (2**35)
    return healpix_order12 // (4 ** (12 - HEALPIX8_ORDER))


def parent_chunk_from_source_id(source_id: int, parent_order: int) -> int:
    healpix8 = healpix8_from_source_id(source_id)
    return healpix8 // (4 ** (HEALPIX8_ORDER - parent_order))


def first_order6_tile_for_parent(parent: int, parent_order: int) -> int:
    if parent_order <= spcc.HEALPIX_ORDER:
        return parent * (4 ** (spcc.HEALPIX_ORDER - parent_order))
    return parent // (4 ** (parent_order - spcc.HEALPIX_ORDER))


def order6_tile_count_for_parent(parent_order: int) -> int:
    if parent_order <= spcc.HEALPIX_ORDER:
        return 4 ** (spcc.HEALPIX_ORDER - parent_order)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
