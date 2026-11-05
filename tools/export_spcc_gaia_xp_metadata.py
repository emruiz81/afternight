#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import csv
import gzip
import io
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Iterable, TextIO

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


TOOL_VERSION = "spcc-gaia-xp-metadata-exporter-v1"
DEFAULT_TAP_URL = "https://gea.esac.esa.int/tap-server/tap"
STATE_FILE_NAME = ".spcc_gaia_xp_metadata_export_state.json"
WORK_DIR_NAME = ".spcc_gaia_xp_metadata_export_work"
RANGE_STATE_DIR_NAME = "ranges"
PARTS_DIR_NAME = "parts"
MANIFEST_FILE_NAME = "spcc_gaia_xp_metadata_export_manifest.json"
DEFAULT_OUTPUT_PREFIX = "gaia_source_xp_summary"
HEALPIX8_ORDER = 8
HEALPIX12_ORDER = 12
HEALPIX8_TILE_COUNT = 12 * (4**HEALPIX8_ORDER)
ORDER8_TO_ORDER12_FACTOR = 4 ** (HEALPIX12_ORDER - HEALPIX8_ORDER)
SOURCE_ID_HEALPIX12_FACTOR = 2**35
METADATA_FIELDS = list(dict.fromkeys(spcc.REQUIRED_GAIA_SOURCE_FIELDS + spcc.REQUIRED_XP_SUMMARY_FIELDS))
DEFAULT_TAP_MAX_RETRIES = 6
DEFAULT_TAP_RETRY_DELAY_SECONDS = 10.0
MAX_TAP_RETRY_DELAY_SECONDS = 300.0


class SimulatedInterruption(RuntimeError):
    pass


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Export resumable Gaia DR3 gaia_source + xp_summary metadata CSV.GZ parts for the "
            "AfterNight SPCC full-sky XP sampled catalog pipeline."
        )
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--source-csv",
        type=Path,
        action="append",
        default=[],
        help="Existing local metadata CSV/CSV.GZ to split by Gaia source_id HEALPix-8 range.",
    )
    parser.add_argument(
        "--tap-official",
        action="store_true",
        help="Fetch metadata from the official Gaia TAP service instead of splitting local CSV inputs.",
    )
    parser.add_argument("--tap-url", default=DEFAULT_TAP_URL)
    parser.add_argument("--healpix8-start", type=int, default=0)
    parser.add_argument("--healpix8-end", type=int, default=HEALPIX8_TILE_COUNT)
    parser.add_argument("--healpix8-step", type=int, default=64)
    parser.add_argument("--output-prefix", default=DEFAULT_OUTPUT_PREFIX)
    parser.add_argument("--timeout-seconds", type=float, default=600.0)
    parser.add_argument("--tap-max-retries", type=int, default=DEFAULT_TAP_MAX_RETRIES)
    parser.add_argument("--tap-retry-delay-seconds", type=float, default=DEFAULT_TAP_RETRY_DELAY_SECONDS)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--keep-work", action="store_true")
    parser.add_argument(
        "--simulate-interruption-after",
        choices=["first-range"],
        help="Testing hook that leaves durable resume state behind and exits with code 2.",
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        export_metadata(args)
    except SimulatedInterruption as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def export_metadata(args: argparse.Namespace) -> None:
    output_dir: Path = args.output_dir
    state_path = output_dir / STATE_FILE_NAME
    if args.clean and args.resume:
        raise ValueError("--clean and --resume cannot be used together")
    if args.clean and output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.resume:
        if not state_path.is_file():
            raise ValueError(f"resume state is missing: {state_path}")
        state = spcc.read_json(state_path)
        state.setdefault("tap_max_retries", args.tap_max_retries)
        state.setdefault("tap_retry_delay_seconds", args.tap_retry_delay_seconds)
    else:
        if state_path.exists():
            raise ValueError(f"resume state already exists; rerun with --resume or remove {state_path}")
        if (output_dir / MANIFEST_FILE_NAME).exists():
            raise ValueError(f"{output_dir} already contains a metadata manifest; rerun with --clean to replace it")
        validate_new_export_args(args)
        state = build_initial_state(args)
        spcc.write_json(state_path, state)

    ensure_work_dirs(output_dir)
    print_export_context(output_dir, state)
    export_ranges(output_dir, state, simulate_first_range=args.simulate_interruption_after == "first-range")
    manifest = build_export_manifest(output_dir, state)
    spcc.write_json(output_dir / MANIFEST_FILE_NAME, manifest)

    if not args.keep_work:
        shutil.rmtree(output_dir / WORK_DIR_NAME, ignore_errors=True)
    if state_path.exists():
        state_path.unlink()

    print(f"SPCC Gaia XP metadata export staged: {output_dir}")
    print(f"Parts: {len(manifest['part_files'])}")
    print(f"Rows: {manifest['total_rows']}")


def validate_new_export_args(args: argparse.Namespace) -> None:
    if args.clean and args.resume:
        raise ValueError("--clean and --resume cannot be used together")
    if args.tap_official and args.source_csv:
        raise ValueError("--tap-official cannot be combined with --source-csv")
    if not args.tap_official and not args.source_csv:
        raise ValueError("provide --tap-official or at least one --source-csv")
    if not (0 <= args.healpix8_start < args.healpix8_end <= HEALPIX8_TILE_COUNT):
        raise ValueError(f"HEALPix-8 range must satisfy 0 <= start < end <= {HEALPIX8_TILE_COUNT}")
    if args.healpix8_step <= 0:
        raise ValueError("--healpix8-step must be positive")
    if args.timeout_seconds <= 0.0:
        raise ValueError("--timeout-seconds must be positive")
    if args.tap_max_retries < 0:
        raise ValueError("--tap-max-retries must be non-negative")
    if args.tap_retry_delay_seconds < 0.0:
        raise ValueError("--tap-retry-delay-seconds must be non-negative")
    for path in args.source_csv:
        if not path.is_file():
            raise ValueError(f"metadata source CSV is missing: {path}")


def build_initial_state(args: argparse.Namespace) -> dict[str, Any]:
    ranges = build_range_specs(args.healpix8_start, args.healpix8_end, args.healpix8_step)
    return {
        "schema_version": "spcc-gaia-xp-metadata-export-state-v1",
        "tool": "export_spcc_gaia_xp_metadata.py",
        "tool_version": TOOL_VERSION,
        "mode": "official-gaia-tap" if args.tap_official else "local-source-csv",
        "source_csv": [str(path) for path in args.source_csv],
        "tap_url": args.tap_url.rstrip("/"),
        "timeout_seconds": args.timeout_seconds,
        "tap_max_retries": args.tap_max_retries,
        "tap_retry_delay_seconds": args.tap_retry_delay_seconds,
        "output_prefix": safe_output_prefix(args.output_prefix),
        "healpix8_order": HEALPIX8_ORDER,
        "healpix8_start": args.healpix8_start,
        "healpix8_end": args.healpix8_end,
        "healpix8_step": args.healpix8_step,
        "ranges": ranges,
        "completed_range_count": 0,
        "created_at_utc": spcc.utc_now_iso(),
        "updated_at_utc": spcc.utc_now_iso(),
    }


def build_range_specs(start: int, end: int, step: int) -> list[dict[str, int]]:
    ranges = []
    for range_start in range(start, end, step):
        range_end = min(end, range_start + step)
        ranges.append({"healpix8_start": range_start, "healpix8_end": range_end})
    return ranges


def ensure_work_dirs(output_dir: Path) -> None:
    (output_dir / WORK_DIR_NAME / RANGE_STATE_DIR_NAME).mkdir(parents=True, exist_ok=True)
    (output_dir / PARTS_DIR_NAME).mkdir(parents=True, exist_ok=True)


def print_export_context(output_dir: Path, state: dict[str, Any]) -> None:
    completed = len(read_range_markers(output_dir))
    total = len(state["ranges"])
    remaining = max(0, total - completed)
    print("SPCC Gaia XP metadata export", flush=True)
    print(f"  output: {output_dir}", flush=True)
    print(f"  mode: {state['mode']}", flush=True)
    print(
        "  healpix8: "
        f"{state['healpix8_start']}..{state['healpix8_end'] - 1} "
        f"step={state['healpix8_step']} ranges={total} completed={completed} remaining={remaining}",
        flush=True,
    )
    if state["mode"] == "official-gaia-tap":
        print(f"  tap: {state['tap_url']} timeout={state['timeout_seconds']}s", flush=True)
        print("  filters: non-null production quality fields; blank pmra/pmdec -> 0.0", flush=True)
    else:
        print(f"  source_csv_inputs: {len(state.get('source_csv', []))}", flush=True)


def export_ranges(output_dir: Path, state: dict[str, Any], *, simulate_first_range: bool) -> None:
    simulated = False
    run_started_at = time.perf_counter()
    completed_at_start = len(read_range_markers(output_dir))
    completed_this_run = 0
    total_ranges = len(state["ranges"])
    for range_index, range_spec in enumerate(state["ranges"], start=1):
        range_start = int(range_spec["healpix8_start"])
        range_end = int(range_spec["healpix8_end"])
        marker_path = range_marker_path(output_dir, range_start, range_end)
        if marker_path.exists():
            continue

        part_path = metadata_part_path(output_dir, state["output_prefix"], range_start, range_end)
        if part_path.exists():
            part_path.unlink()

        adql = build_tap_adql(range_start, range_end) if state["mode"] == "official-gaia-tap" else None
        source_id_start, source_id_end = source_id_bounds_for_healpix8_range(range_start, range_end)
        print(
            f"[{range_index}/{total_ranges}] starting hp8 {range_start:06d}-{range_end - 1:06d} "
            f"source_id=[{source_id_start},{source_id_end})",
            flush=True,
        )
        range_started_at = time.perf_counter()
        row_count = write_metadata_part_with_retries(output_dir, state, range_start, range_end, part_path, adql)
        range_elapsed = time.perf_counter() - range_started_at
        marker: dict[str, Any] = {
            "healpix8_start": range_start,
            "healpix8_end": range_end,
            "row_count": row_count,
            "part_file": part_path.relative_to(output_dir).as_posix(),
            "part_sha256": spcc.sha256_file(part_path),
            "part_size_bytes": part_path.stat().st_size,
            "completed_at_utc": spcc.utc_now_iso(),
        }
        if adql is not None:
            marker["tap_adql"] = adql
        spcc.write_json(marker_path, marker)
        state["completed_range_count"] = int(state.get("completed_range_count", 0)) + 1
        state["updated_at_utc"] = spcc.utc_now_iso()
        spcc.write_json(output_dir / STATE_FILE_NAME, state)
        completed_this_run += 1
        completed_total = completed_at_start + completed_this_run
        remaining = max(0, total_ranges - completed_total)
        run_elapsed = time.perf_counter() - run_started_at
        avg = run_elapsed / completed_this_run
        eta = avg * remaining
        print(
            f"[{completed_total}/{total_ranges}] completed hp8 {range_start:06d}-{range_end - 1:06d} "
            f"rows={row_count} elapsed={format_duration(range_elapsed)} avg={format_duration(avg)} "
            f"eta={format_duration(eta)} part={part_path.relative_to(output_dir).as_posix()}",
            flush=True,
        )

        if simulate_first_range and not simulated:
            simulated = True
            raise SimulatedInterruption("simulated interruption after first SPCC Gaia XP metadata range")


def write_metadata_part(
    output_dir: Path,
    state: dict[str, Any],
    range_start: int,
    range_end: int,
    part_path: Path,
    adql: str | None,
) -> int:
    del output_dir
    row_count = 0
    with gzip.open(part_path, "wt", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=METADATA_FIELDS, lineterminator="\n")
        writer.writeheader()
        if state["mode"] == "official-gaia-tap":
            if adql is None:
                raise ValueError("missing TAP ADQL for official Gaia metadata export")
            rows = iter_tap_rows(str(state["tap_url"]), adql, float(state["timeout_seconds"]))
        else:
            rows = iter_local_source_rows([Path(path) for path in state["source_csv"]], range_start, range_end)
        for line_number, row in enumerate(rows, start=1):
            normalized = normalize_metadata_row(row, part_path, line_number)
            writer.writerow({field: csv_value(normalized[field]) for field in METADATA_FIELDS})
            row_count += 1
    return row_count


def write_metadata_part_with_retries(
    output_dir: Path,
    state: dict[str, Any],
    range_start: int,
    range_end: int,
    part_path: Path,
    adql: str | None,
) -> int:
    if state["mode"] != "official-gaia-tap":
        return write_metadata_part(output_dir, state, range_start, range_end, part_path, adql)

    max_retries = int(state.get("tap_max_retries", DEFAULT_TAP_MAX_RETRIES))
    retry_delay = float(state.get("tap_retry_delay_seconds", DEFAULT_TAP_RETRY_DELAY_SECONDS))
    for attempt in range(max_retries + 1):
        try:
            return write_metadata_part(output_dir, state, range_start, range_end, part_path, adql)
        except Exception as exc:
            if not is_transient_tap_error(exc) or attempt >= max_retries:
                raise
            if part_path.exists():
                part_path.unlink()
            delay = min(MAX_TAP_RETRY_DELAY_SECONDS, retry_delay * (2**attempt))
            print(
                f"[retry {attempt + 1}/{max_retries}] Gaia TAP transient failure for "
                f"hp8 {range_start:06d}-{range_end - 1:06d}: {exc}; retrying in {format_duration(delay)}",
                file=sys.stderr,
                flush=True,
            )
            if delay > 0.0:
                time.sleep(delay)
    raise RuntimeError("unreachable TAP retry state")


def is_transient_tap_error(exc: Exception) -> bool:
    if isinstance(exc, urllib.error.HTTPError):
        return exc.code in {408, 409, 425, 429} or 500 <= exc.code <= 599
    if isinstance(exc, urllib.error.URLError):
        return True
    return isinstance(exc, TimeoutError)


def iter_local_source_rows(paths: list[Path], range_start: int, range_end: int) -> Iterable[dict[str, Any]]:
    for path in paths:
        with open_text(path) as handle:
            reader = csv.DictReader(non_comment_lines(handle))
            for row in reader:
                source_id = int(str(row["source_id"]).strip())
                healpix8 = source_id_to_healpix8(source_id)
                if range_start <= healpix8 < range_end:
                    yield row


def iter_tap_rows(tap_url: str, adql: str, timeout_seconds: float) -> Iterable[dict[str, Any]]:
    body = urllib.parse.urlencode(
        {"REQUEST": "doQuery", "LANG": "ADQL", "FORMAT": "csv", "MAXREC": "-1", "QUERY": adql}
    ).encode()
    request = urllib.request.Request(
        tap_url.rstrip("/") + "/sync",
        data=body,
        headers={"User-Agent": "AfterNight SPCC Gaia XP metadata export"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        text = io.TextIOWrapper(response, encoding="utf-8", newline="")
        reader = csv.DictReader(non_comment_lines(text))
        for row in reader:
            yield row


def build_tap_adql(healpix8_start: int, healpix8_end: int) -> str:
    source_id_start, source_id_end = source_id_bounds_for_healpix8_range(healpix8_start, healpix8_end)
    select_columns = []
    for field in spcc.REQUIRED_GAIA_SOURCE_FIELDS:
        select_columns.append(f"s.{field} AS {field}")
    for field in spcc.REQUIRED_XP_SUMMARY_FIELDS:
        select_columns.append(f"x.{field} AS {field}")
    select_text = ",\n  ".join(select_columns)
    where_clauses = [
        "s.has_xp_sampled = 'true'",
        f"s.source_id >= {source_id_start}",
        f"s.source_id < {source_id_end}",
    ]
    where_clauses.extend(build_tap_non_null_clauses())
    where_text = "\n  AND ".join(where_clauses)
    return f"""
SELECT
  {select_text}
FROM gaiadr3.gaia_source AS s
JOIN gaiadr3.xp_summary AS x ON s.source_id = x.source_id
WHERE {where_text}
ORDER BY s.source_id ASC
""".strip()


def build_tap_non_null_clauses() -> list[str]:
    clauses = []
    skipped_fields = {"source_id", "has_xp_sampled", *spcc.OPTIONAL_FLOAT_METADATA_DEFAULTS.keys()}
    for field in spcc.REQUIRED_GAIA_SOURCE_FIELDS:
        if field not in skipped_fields:
            clauses.append(f"s.{field} IS NOT NULL")
    for field in spcc.REQUIRED_XP_SUMMARY_FIELDS:
        clauses.append(f"x.{field} IS NOT NULL")
    return clauses


def build_export_manifest(output_dir: Path, state: dict[str, Any]) -> dict[str, Any]:
    markers = read_range_markers(output_dir)
    part_files = [
        {
            "path": marker["part_file"],
            "healpix8_start": marker["healpix8_start"],
            "healpix8_end": marker["healpix8_end"],
            "row_count": marker["row_count"],
            "size_bytes": marker["part_size_bytes"],
            "sha256": marker["part_sha256"],
        }
        for marker in markers
    ]
    manifest: dict[str, Any] = {
        "schema_version": "spcc-gaia-xp-metadata-export-manifest-v1",
        "tool": "export_spcc_gaia_xp_metadata.py",
        "tool_version": TOOL_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "gaia_archive_release": "Gaia DR3",
        "mode": state["mode"],
        "source_csv_inputs": source_csv_manifest_entries(state),
        "tap_url": state["tap_url"] if state["mode"] == "official-gaia-tap" else None,
        "healpix8_order": HEALPIX8_ORDER,
        "healpix8_start": state["healpix8_start"],
        "healpix8_end": state["healpix8_end"],
        "healpix8_step": state["healpix8_step"],
        "metadata_fields": METADATA_FIELDS,
        "total_rows": sum(int(marker["row_count"]) for marker in markers),
        "part_files": part_files,
    }
    if state["mode"] == "official-gaia-tap":
        manifest["tap_query_template"] = build_tap_adql(int(state["healpix8_start"]), int(state["healpix8_start"]) + 1)
    return manifest


def source_csv_manifest_entries(state: dict[str, Any]) -> list[dict[str, Any]]:
    entries = []
    for name in state.get("source_csv", []):
        path = Path(name)
        entries.append(
            {
                "path": str(path),
                "sha256": spcc.sha256_file(path) if path.is_file() else None,
                "size_bytes": path.stat().st_size if path.is_file() else None,
            }
        )
    return entries


def read_range_markers(output_dir: Path) -> list[dict[str, Any]]:
    markers = []
    marker_dir = output_dir / WORK_DIR_NAME / RANGE_STATE_DIR_NAME
    for marker_path in sorted(marker_dir.glob("range_*.complete.json")):
        markers.append(spcc.read_json(marker_path))
    return markers


def range_marker_path(output_dir: Path, range_start: int, range_end: int) -> Path:
    return output_dir / WORK_DIR_NAME / RANGE_STATE_DIR_NAME / f"range_{range_start:06d}_{range_end - 1:06d}.complete.json"


def metadata_part_path(output_dir: Path, prefix: str, range_start: int, range_end: int) -> Path:
    return output_dir / PARTS_DIR_NAME / f"{prefix}_hp8_{range_start:06d}_{range_end - 1:06d}.csv.gz"


def source_id_bounds_for_healpix8_range(range_start: int, range_end: int) -> tuple[int, int]:
    scale = ORDER8_TO_ORDER12_FACTOR * SOURCE_ID_HEALPIX12_FACTOR
    return range_start * scale, range_end * scale


def source_id_to_healpix8(source_id: int) -> int:
    healpix12 = source_id // SOURCE_ID_HEALPIX12_FACTOR
    return healpix12 // ORDER8_TO_ORDER12_FACTOR


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


def normalize_metadata_row(row: dict[str, Any], path: Path, line_number: int) -> dict[str, Any]:
    normalized: dict[str, Any] = {}
    for field in METADATA_FIELDS:
        if field not in row:
            raise ValueError(f"{path}:{line_number}: missing required metadata field {field}")
        value = row[field]
        if value_is_blank(value):
            if field not in spcc.OPTIONAL_FLOAT_METADATA_DEFAULTS:
                raise ValueError(f"{path}:{line_number}: missing required metadata field {field}")
            value = spcc.OPTIONAL_FLOAT_METADATA_DEFAULTS[field]
        normalized[field] = value
    if not parse_bool(normalized["has_xp_sampled"]):
        raise ValueError(f"{path}:{line_number}: has_xp_sampled must be true")
    return normalized


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


def csv_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def open_text(path: Path) -> TextIO:
    if "".join(path.suffixes[-2:]).lower() == ".csv.gz" or path.suffix.lower() == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", newline="")
    return path.open("r", encoding="utf-8", newline="")


def non_comment_lines(handle: TextIO) -> Iterable[str]:
    for line in handle:
        if not line.startswith("#"):
            yield line


def safe_output_prefix(value: str) -> str:
    safe = "".join(char if char.isalnum() or char in "._-" else "_" for char in value.strip())
    return safe.strip("._-") or DEFAULT_OUTPUT_PREFIX


if __name__ == "__main__":
    raise SystemExit(main())
