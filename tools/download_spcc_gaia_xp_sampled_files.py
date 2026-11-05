#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import fetch_spcc_gaia_validation_fixture as gaia_fetch
from tools import spcc_tool_common as spcc


TOOL_VERSION = "spcc-gaia-xp-sampled-downloader-v1"
STATE_FILE_NAME = ".spcc_gaia_xp_sampled_download_state.json"
WORK_DIR_NAME = ".spcc_gaia_xp_sampled_download_work"
DOWNLOAD_STATE_DIR_NAME = "downloads"
MANIFEST_FILE_NAME = "spcc_gaia_xp_sampled_download_manifest.json"
DEFAULT_BASE_URL = gaia_fetch.CDN_PUBLIC_BASE_URL
DOWNLOAD_BLOCK_BYTES = 1024 * 1024


class SimulatedInterruption(RuntimeError):
    pass


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Download the official Gaia DR3 xp_sampled_mean_spectrum CSV.GZ files into a resumable "
            "AfterNight SPCC staging directory."
        )
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--index-json",
        type=Path,
        help="Fixture or prebuilt index JSON. Accepts a list or an object with a files array.",
    )
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--timeout-seconds", type=float, default=600.0)
    parser.add_argument("--max-files", type=int, help="Limit files for smoke runs or tests.")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--keep-work", action="store_true")
    parser.add_argument(
        "--simulate-interruption-after",
        choices=["first-file"],
        help="Testing hook that leaves durable resume state behind and exits with code 2.",
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        download_xp_sampled_files(args)
    except SimulatedInterruption as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def download_xp_sampled_files(args: argparse.Namespace) -> None:
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
    else:
        if state_path.exists():
            raise ValueError(f"resume state already exists; rerun with --resume or remove {state_path}")
        if (output_dir / MANIFEST_FILE_NAME).exists():
            raise ValueError(f"{output_dir} already contains a download manifest; rerun with --clean to replace it")
        validate_new_download_args(args)
        entries = load_index_entries(args)
        if args.max_files is not None:
            entries = entries[: args.max_files]
        if not entries:
            raise ValueError("Gaia XP sampled file index is empty")
        state = {
            "schema_version": "spcc-gaia-xp-sampled-download-state-v1",
            "tool": "download_spcc_gaia_xp_sampled_files.py",
            "tool_version": TOOL_VERSION,
            "base_url": args.base_url.rstrip("/") + "/",
            "timeout_seconds": args.timeout_seconds,
            "index_json": str(args.index_json) if args.index_json else None,
            "entries": entries,
            "created_at_utc": spcc.utc_now_iso(),
            "updated_at_utc": spcc.utc_now_iso(),
        }
        spcc.write_json(state_path, state)

    ensure_work_dirs(output_dir)
    print_download_context(output_dir, state)
    download_entries(output_dir, state, simulate_first_file=args.simulate_interruption_after == "first-file")
    manifest = build_download_manifest(output_dir, state)
    spcc.write_json(output_dir / MANIFEST_FILE_NAME, manifest)

    if not args.keep_work:
        shutil.rmtree(output_dir / WORK_DIR_NAME, ignore_errors=True)
    if state_path.exists():
        state_path.unlink()

    print(f"SPCC Gaia XP sampled download staged: {output_dir}")
    print(f"Files: {manifest['file_count']}")
    print(f"Size: {format_bytes(manifest['total_size_bytes'])}")


def validate_new_download_args(args: argparse.Namespace) -> None:
    if args.timeout_seconds <= 0.0:
        raise ValueError("--timeout-seconds must be positive")
    if args.max_files is not None and args.max_files <= 0:
        raise ValueError("--max-files must be positive")
    if args.index_json is not None and not args.index_json.is_file():
        raise ValueError(f"index JSON is missing: {args.index_json}")


def load_index_entries(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.index_json:
        raw = spcc.read_json(args.index_json)
        entries = raw["files"] if isinstance(raw, dict) else raw
    else:
        entries = gaia_fetch.list_xp_sampled_files(args.timeout_seconds)
    return [normalize_index_entry(entry, args.base_url) for entry in entries]


def normalize_index_entry(entry: dict[str, Any], base_url: str) -> dict[str, Any]:
    key = str(entry.get("key") or entry.get("path") or "")
    url = str(entry.get("url") or "")
    if not key and url:
        key = Path(urllib.request.url2pathname(urllib.parse.urlparse(url).path)).name
    if not key:
        raise ValueError(f"XP sampled index entry is missing key/path: {entry}")
    if not url:
        url = base_url.rstrip("/") + "/" + key.lstrip("/")
    filename = Path(key).name
    normalized = {
        "key": key,
        "url": url,
        "filename": filename,
        "healpix_start": int(entry["healpix_start"]) if "healpix_start" in entry else None,
        "healpix_end": int(entry["healpix_end"]) if "healpix_end" in entry else None,
        "size_bytes": int(entry["size_bytes"]) if entry.get("size_bytes") is not None else None,
        "sha256": entry.get("sha256"),
        "last_modified": entry.get("last_modified"),
    }
    if not filename.lower().endswith(".csv.gz"):
        raise ValueError(f"XP sampled entry is not a CSV.GZ file: {filename}")
    return normalized


def ensure_work_dirs(output_dir: Path) -> None:
    (output_dir / WORK_DIR_NAME / DOWNLOAD_STATE_DIR_NAME).mkdir(parents=True, exist_ok=True)


def print_download_context(output_dir: Path, state: dict[str, Any]) -> None:
    reconcile_download_markers(output_dir, state)
    completed = len(read_download_markers(output_dir))
    total = len(state["entries"])
    total_size = sum(int(entry.get("size_bytes") or 0) for entry in state["entries"])
    print("SPCC Gaia XP sampled download", flush=True)
    print(f"  output: {output_dir}", flush=True)
    print(f"  files: {total} completed={completed} remaining={max(0, total - completed)}", flush=True)
    print(f"  indexed_size: {format_bytes(total_size)}", flush=True)
    print(f"  base_url: {state['base_url']}", flush=True)
    print(f"  timeout: {state['timeout_seconds']}s", flush=True)


def download_entries(output_dir: Path, state: dict[str, Any], *, simulate_first_file: bool) -> None:
    reconcile_download_markers(output_dir, state)
    simulated = False
    started_at = time.perf_counter()
    completed_at_start = len(read_download_markers(output_dir))
    completed_this_run = 0
    total = len(state["entries"])
    for index, entry in enumerate(state["entries"], start=1):
        marker_path = download_marker_path(output_dir, entry["filename"])
        if marker_path.exists():
            marker = read_download_marker(marker_path)
            if marker is not None and download_marker_matches_entry(output_dir, marker, entry):
                continue
            marker_path.unlink()

        target_path = output_dir / entry["filename"]
        print(
            f"[{index}/{total}] starting {entry['filename']} expected={format_bytes(entry.get('size_bytes'))}",
            flush=True,
        )
        file_started_at = time.perf_counter()
        download_one_file(entry, target_path, float(state["timeout_seconds"]))
        elapsed = time.perf_counter() - file_started_at
        marker = {
            "key": entry["key"],
            "url": entry["url"],
            "path": target_path.name,
            "healpix_start": entry.get("healpix_start"),
            "healpix_end": entry.get("healpix_end"),
            "size_bytes": target_path.stat().st_size,
            "sha256": spcc.sha256_file(target_path),
            "last_modified": entry.get("last_modified"),
            "completed_at_utc": spcc.utc_now_iso(),
        }
        spcc.write_json(marker_path, marker)
        state["updated_at_utc"] = spcc.utc_now_iso()
        spcc.write_json(output_dir / STATE_FILE_NAME, state)
        completed_this_run += 1
        completed_total = completed_at_start + completed_this_run
        avg = (time.perf_counter() - started_at) / completed_this_run
        eta = avg * max(0, total - completed_total)
        print(
            f"[{completed_total}/{total}] completed {entry['filename']} "
            f"size={format_bytes(marker['size_bytes'])} elapsed={format_duration(elapsed)} "
            f"avg={format_duration(avg)} eta={format_duration(eta)}",
            flush=True,
        )
        if simulate_first_file and not simulated:
            simulated = True
            raise SimulatedInterruption("simulated interruption after first Gaia XP sampled file")


def download_one_file(entry: dict[str, Any], target_path: Path, timeout_seconds: float) -> None:
    expected_size = entry.get("size_bytes")
    expected_sha256 = entry.get("sha256")
    if target_path.exists() and file_matches(target_path, expected_size, expected_sha256):
        return

    part_path = target_path.with_name(target_path.name + ".part")
    offset = part_path.stat().st_size if part_path.exists() else 0
    request = urllib.request.Request(entry["url"], headers={"User-Agent": "AfterNight SPCC Gaia XP sampled download"})
    if offset > 0:
        request.add_header("Range", f"bytes={offset}-")
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        status = getattr(response, "status", 200)
        mode = "ab"
        if offset > 0 and status != 206:
            mode = "wb"
        with part_path.open(mode + "") as handle:
            for block in iter(lambda: response.read(DOWNLOAD_BLOCK_BYTES), b""):
                handle.write(block)

    if not file_matches(part_path, expected_size, expected_sha256):
        actual = part_path.stat().st_size
        raise ValueError(f"{part_path}: downloaded file did not match expected metadata; size={actual}")
    part_path.replace(target_path)


def file_matches(path: Path, expected_size: Any, expected_sha256: Any) -> bool:
    if expected_size is not None and path.stat().st_size != int(expected_size):
        return False
    if expected_sha256 and spcc.sha256_file(path) != str(expected_sha256):
        return False
    return True


def build_download_manifest(output_dir: Path, state: dict[str, Any]) -> dict[str, Any]:
    reconcile_download_markers(output_dir, state)
    markers = read_download_markers(output_dir)
    expected_count = len(state["entries"])
    if len(markers) != expected_count:
        raise ValueError(f"download is incomplete: {len(markers)}/{expected_count} files have verified markers")
    files = [
        {
            "path": marker["path"],
            "key": marker["key"],
            "url": marker["url"],
            "healpix_start": marker.get("healpix_start"),
            "healpix_end": marker.get("healpix_end"),
            "size_bytes": marker["size_bytes"],
            "sha256": marker["sha256"],
            "last_modified": marker.get("last_modified"),
        }
        for marker in markers
    ]
    return {
        "schema_version": "spcc-gaia-xp-sampled-download-manifest-v1",
        "tool": "download_spcc_gaia_xp_sampled_files.py",
        "tool_version": TOOL_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "gaia_archive_release": "Gaia DR3",
        "base_url": state["base_url"],
        "index_json": state.get("index_json"),
        "file_count": len(files),
        "total_size_bytes": sum(int(entry["size_bytes"]) for entry in files),
        "part_files": files,
    }


def read_download_markers(output_dir: Path) -> list[dict[str, Any]]:
    marker_dir = output_dir / WORK_DIR_NAME / DOWNLOAD_STATE_DIR_NAME
    markers: list[dict[str, Any]] = []
    for path in sorted(marker_dir.glob("*.complete.json")):
        marker = read_download_marker(path)
        if marker is not None:
            markers.append(marker)
    return markers


def reconcile_download_markers(output_dir: Path, state: dict[str, Any]) -> None:
    marker_dir = output_dir / WORK_DIR_NAME / DOWNLOAD_STATE_DIR_NAME
    entries_by_filename = {entry["filename"]: entry for entry in state["entries"]}
    for marker_path in sorted(marker_dir.glob("*.complete.json")):
        marker = read_download_marker(marker_path)
        entry = entries_by_filename.get(str(marker.get("path", ""))) if marker is not None else None
        if marker is None or entry is None or not download_marker_matches_entry(output_dir, marker, entry):
            marker_path.unlink()


def read_download_marker(path: Path) -> dict[str, Any] | None:
    try:
        marker = spcc.read_json(path)
    except (OSError, json.JSONDecodeError):
        return None
    return marker if isinstance(marker, dict) else None


def download_marker_matches_entry(output_dir: Path, marker: dict[str, Any], entry: dict[str, Any]) -> bool:
    marker_path = Path(str(marker.get("path", "")))
    if marker_path.name != entry["filename"] or marker_path.name != marker_path.as_posix():
        return False

    target_path = output_dir / entry["filename"]
    if not target_path.is_file():
        return False
    if not file_matches(target_path, entry.get("size_bytes"), entry.get("sha256")):
        return False

    try:
        marker_size = int(marker.get("size_bytes", -1))
    except (TypeError, ValueError):
        return False
    target_size = target_path.stat().st_size
    target_sha = spcc.sha256_file(target_path)
    return marker_size == target_size and str(marker.get("sha256", "")) == target_sha


def download_marker_path(output_dir: Path, filename: str) -> Path:
    safe = "".join(char if char.isalnum() or char in "._-" else "_" for char in filename)
    return output_dir / WORK_DIR_NAME / DOWNLOAD_STATE_DIR_NAME / f"{safe}.complete.json"


def format_bytes(value: Any) -> str:
    if value is None:
        return "unknown"
    size = float(value)
    for suffix in ["B", "KB", "MB", "GB", "TB"]:
        if size < 1024.0 or suffix == "TB":
            return f"{size:.1f}{suffix}" if suffix != "B" else f"{int(size)}B"
        size /= 1024.0
    return f"{size:.1f}TB"


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


if __name__ == "__main__":
    raise SystemExit(main())
