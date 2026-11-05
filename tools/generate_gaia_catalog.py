#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import gzip
import hashlib
import io
import json
import math
import os
import shutil
import struct
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import OrderedDict, deque
from dataclasses import dataclass
from pathlib import Path
from typing import IO


STAR_STRUCT_V3 = struct.Struct("<ddfffffQHhhHh10x")
ANSP_PREAMBLE_STRUCT = struct.Struct("<4sHHII")
ANSP_INDEX_ENTRY_STRUCT = struct.Struct("<IQ")
ANSP_MAGIC = b"ANSP"
ANSP_FORMAT_VERSION = 1
GAIA_CATALOG_FORMAT_VERSION = 3
HEALPIX_TILES_PER_CHUNK = 256
TEFF_SENTINEL = 0
SIGNED_SENTINEL = -32768
UNSIGNED_SENTINEL = 65535
DEFAULT_TAP_URL = "https://gea.esac.esa.int/tap-server/tap"
PRESETS = {
    "g12": {
        "catalog_name": "Gaia DR3 G<=12",
        "catalog_id": "gaia_dr3_g12",
        "mag_limit": 12.0,
    },
    "g14": {
        "catalog_name": "Gaia DR3 G<=14",
        "catalog_id": "gaia_dr3_g14",
        "mag_limit": 14.0,
    },
    "g16": {
        "catalog_name": "Gaia DR3 G<=16",
        "catalog_id": "gaia_dr3_g16",
        "mag_limit": 16.0,
    },
    "g18": {
        "catalog_name": "Gaia DR3 G<=18",
        "catalog_id": "gaia_dr3_g18",
        "mag_limit": 18.0,
    },
    "g20": {
        "catalog_name": "Gaia DR3 G<=20",
        "catalog_id": "gaia_dr3_g20",
        "mag_limit": 20.0,
    },
}
DEFAULT_COLUMNS = {
    "source_id": "source_id",
    "ra": "ra",
    "dec": "dec",
    "g_mag": "phot_g_mean_mag",
    "bp_mag": "phot_bp_mean_mag",
    "rp_mag": "phot_rp_mean_mag",
    "pmra": "pmra",
    "pmdec": "pmdec",
    "teff": "teff_gspphot",
    "logg": "logg_gspphot",
    "feh": "mh_gspphot",
    "ag": "ag_gspphot",
    "ebpminrp": "ebpminrp_gspphot",
}
DEFAULT_TAP_MAX_OUTAGE_SECONDS = 300.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a chunked Gaia DR3 binary catalog from official CSV/CSV.GZ exports."
    )
    parser.add_argument(
        "input_csv",
        type=Path,
        nargs="?",
        default=None,
        help="Primary Gaia input: CSV, CSV.GZ, or a directory containing GaiaSource_*.csv(.gz) files. "
             "Not required when --tap-official is used.",
    )
    parser.add_argument("output_dir", type=Path, help="Output catalog directory")
    parser.add_argument("--extra-input", action="append", type=Path, default=[],
                        help="Additional Gaia CSV or CSV.GZ input file. Can be passed multiple times.")
    parser.add_argument("--input-dir", action="append", type=Path, default=[],
                        help="Directory containing GaiaSource_*.csv or GaiaSource_*.csv.gz files. Can be passed multiple times.")
    parser.add_argument("--preset", choices=sorted(PRESETS.keys()),
                        help="Catalog preset to use for catalog id/name/magnitude defaults.")
    parser.add_argument("--catalog-name", default=None)
    parser.add_argument("--catalog-id", default=None)
    parser.add_argument("--mag-limit", type=float, default=None)
    parser.add_argument("--healpix-order", type=int, default=5)
    parser.add_argument("--encoding", default="utf-8")
    parser.add_argument("--max-open-files", type=int, default=None,
                        help="Maximum tile file handles to keep open concurrently. "
                             "Defaults to min(total_tiles, OS_fd_limit - 64). "
                             "Pass an explicit value to override.")
    parser.add_argument("--ra-column", default=DEFAULT_COLUMNS["ra"])
    parser.add_argument("--dec-column", default=DEFAULT_COLUMNS["dec"])
    parser.add_argument("--source-id-column", default=DEFAULT_COLUMNS["source_id"])
    parser.add_argument("--g-column", default=DEFAULT_COLUMNS["g_mag"])
    parser.add_argument("--bp-column", default=DEFAULT_COLUMNS["bp_mag"])
    parser.add_argument("--rp-column", default=DEFAULT_COLUMNS["rp_mag"])
    parser.add_argument("--pmra-column", default=DEFAULT_COLUMNS["pmra"])
    parser.add_argument("--pmdec-column", default=DEFAULT_COLUMNS["pmdec"])
    parser.add_argument("--teff-column", default=DEFAULT_COLUMNS["teff"])
    parser.add_argument("--logg-column", default=DEFAULT_COLUMNS["logg"])
    parser.add_argument("--feh-column", default=DEFAULT_COLUMNS["feh"])
    parser.add_argument("--ag-column", default=DEFAULT_COLUMNS["ag"])
    parser.add_argument("--ebpminrp-column", default=DEFAULT_COLUMNS["ebpminrp"])
    parser.add_argument("--tap-official", action="store_true",
                        help="Query the official Gaia Archive TAP service instead of reading local CSV input files.")
    parser.add_argument("--tap-url", default=DEFAULT_TAP_URL,
                        help="Base URL of the Gaia TAP service.")
    parser.add_argument("--tap-initial-healpix-level", type=int, default=3,
                        help="Initial HEALPix level for official TAP partitioning.")
    parser.add_argument("--tap-max-healpix-level", type=int, default=6,
                        help="Maximum HEALPix level for official TAP partition refinement.")
    parser.add_argument("--tap-max-rows-per-query", type=int, default=200000,
                        help="Refine TAP partitions until each query is at or below this estimated row count.")
    parser.add_argument("--resume", action="store_true",
                        help="Resume a previously interrupted run. Re-uses existing tile files "
                             "and skips TAP queries that were already completed (tracked via checkpoint).")
    parser.add_argument("--tap-include-pixel", action="append", type=int, default=[],
                        help="Restrict initial official TAP work to specific HEALPix pixels at the initial level. "
                             "Useful for validation or partial builds.")
    parser.add_argument("--tap-timeout-seconds", type=float, default=120.0,
                        help="HTTP timeout for each Gaia TAP request.")
    parser.add_argument("--tap-poll-seconds", type=float, default=1.0,
                        help="Polling interval for Gaia TAP async jobs.")
    parser.add_argument("--tap-job-timeout-seconds", type=float, default=1800.0,
                        help="Maximum time to wait for a single Gaia TAP async job.")
    parser.add_argument("--tap-heartbeat-seconds", type=float, default=60.0,
                        help="If no TAP pixel finishes for this long, print a heartbeat describing "
                             "the oldest in-flight pixel. Set to 0 to disable.")
    parser.add_argument("--tap-max-outage-seconds", type=float, default=DEFAULT_TAP_MAX_OUTAGE_SECONDS,
                        help="Abort with a resumable error if TAP requests keep failing transiently "
                             "for this long. Set to 0 to disable.")
    parser.add_argument("--tap-user-agent", default="AfterNight Gaia Catalog Builder",
                        help="User-Agent string sent to the official Gaia TAP service.")
    parser.add_argument("--workers", type=int, default=4,
                        help="Parallel worker threads for TAP pixel queries and pass-2 chunk writes. "
                             "Default: 4. Increase on fast network connections.")
    return parser.parse_args()


def normalize_ra_deg(ra_deg: float) -> float:
    ra_deg = math.fmod(ra_deg, 360.0)
    if ra_deg < 0.0:
        ra_deg += 360.0
    return ra_deg


def nside_for_order(order: int) -> int:
    if order < 0 or order > 13:
        raise ValueError(f"Unsupported HEALPix order: {order}")
    return 1 << order


def pixel_count_for_order(order: int) -> int:
    nside = nside_for_order(order)
    return 12 * nside * nside


def ang2pix_ring(order: int, ra_deg: float, dec_deg: float) -> int:
    nside = nside_for_order(order)
    npix = pixel_count_for_order(order)
    phi = math.radians(normalize_ra_deg(ra_deg))
    z = math.sin(math.radians(max(-90.0, min(90.0, dec_deg))))
    za = abs(z)
    tt = (phi % (2.0 * math.pi)) / (0.5 * math.pi)
    ncap = 2 * nside * (nside - 1)

    if za <= (2.0 / 3.0):
        temp1 = nside * (0.5 + tt)
        temp2 = nside * (z * 0.75)
        jp = math.floor(temp1 - temp2)
        jm = math.floor(temp1 + temp2)
        ring = nside + 1 + jp - jm
        kshift = ring & 1
        ip = (jp + jm - nside + kshift + 1) // 2
        pixels_in_ring = 4 * nside
        ip %= pixels_in_ring
        if ip <= 0:
            ip += pixels_in_ring
        pixel_1based = ncap + (ring - 1) * pixels_in_ring + ip
    else:
        tp = tt - math.floor(tt)
        tmp = nside * math.sqrt(max(3.0 * (1.0 - za), 0.0))
        jp = math.floor(tp * tmp)
        jm = math.floor((1.0 - tp) * tmp)
        ring = jp + jm + 1
        ip = math.floor(tt * ring) + 1
        pixels_in_ring = 4 * ring
        if ip > pixels_in_ring:
            ip -= pixels_in_ring
        if z >= 0.0:
            pixel_1based = 2 * ring * (ring - 1) + ip
        else:
            pixel_1based = npix - (2 * ring * (ring + 1)) + ip

    if pixel_1based <= 0 or pixel_1based > npix:
        raise ValueError(f"Computed invalid pixel index {pixel_1based} for ({ra_deg}, {dec_deg})")

    return pixel_1based - 1


def parse_required_float(row: dict[str, str], column: str) -> float:
    raw = row.get(column, "")
    if raw is None:
        raise ValueError(f"Missing required column: {column}")
    value = raw.strip()
    if not value:
        raise ValueError(f"Empty value for required column: {column}")
    return float(value)


def parse_optional_float(row: dict[str, str], column: str, default: float) -> float:
    raw = row.get(column, "")
    if raw is None:
        return default
    value = raw.strip()
    if not value:
        return default
    return float(value)


def parse_optional_uint64(row: dict[str, str], column: str, default: int = 0) -> int:
    raw = row.get(column, "")
    if raw is None:
        return default
    value = raw.strip()
    if not value:
        return default
    return int(value)


def parse_optional_scaled_unsigned(row: dict[str, str],
                                   column: str,
                                   scale: int,
                                   sentinel: int,
                                   max_value: int) -> int:
    raw = row.get(column, "")
    if raw is None:
        return sentinel
    value = raw.strip()
    if not value:
        return sentinel
    parsed = float(value)
    if not math.isfinite(parsed):
        return sentinel
    scaled = int(round(parsed * scale))
    if scaled < 0 or scaled > max_value:
        return sentinel
    return scaled


def parse_optional_scaled_signed(row: dict[str, str],
                                 column: str,
                                 scale: int,
                                 sentinel: int,
                                 min_value: int,
                                 max_value: int) -> int:
    raw = row.get(column, "")
    if raw is None:
        return sentinel
    value = raw.strip()
    if not value:
        return sentinel
    parsed = float(value)
    if not math.isfinite(parsed):
        return sentinel
    scaled = int(round(parsed * scale))
    if scaled < min_value or scaled > max_value:
        return sentinel
    return scaled


def resolve_inputs(primary_input: Path, extra_inputs: list[Path], input_dirs: list[Path]) -> list[Path]:
    inputs: list[Path] = []
    pending_paths = [primary_input]
    pending_paths.extend(extra_inputs)
    pending_paths.extend(input_dirs)

    for path in pending_paths:
        if not path.exists():
            raise FileNotFoundError(f"Input path does not exist: {path}")

        if path.is_dir():
            discovered = sorted(path.glob("GaiaSource_*.csv")) + sorted(path.glob("GaiaSource_*.csv.gz"))
            if not discovered:
                raise FileNotFoundError(f"No GaiaSource_*.csv or GaiaSource_*.csv.gz files found in {path}")
            inputs.extend(discovered)
            continue

        inputs.append(path)

    unique_inputs: list[Path] = []
    seen: set[Path] = set()
    for path in inputs:
        resolved = path.resolve()
        if resolved in seen:
            continue
        if not resolved.exists():
            raise FileNotFoundError(f"Input file does not exist: {resolved}")
        seen.add(resolved)
        unique_inputs.append(resolved)

    return unique_inputs


def open_catalog_input(path: Path, encoding: str) -> IO[str]:
    if path.suffix.lower() == ".gz":
        return gzip.open(path, "rt", encoding=encoding, newline="")
    return path.open("r", encoding=encoding, newline="")


# ---------------------------------------------------------------------------
# TTY progress indicator (stderr only; stdout keeps the AFTERNIGHT_PROGRESS
# machine-readable lines for callers that parse them)
# ---------------------------------------------------------------------------

_STDERR_IS_TTY: bool = sys.stderr.isatty()
_STDOUT_IS_TTY: bool = sys.stdout.isatty()
_SPINNER_CHARS = ("|", "/", "-", "\\")
_tty: dict = {"phase": "", "start": 0.0, "spin": 0, "last_len": 0}


@dataclass(frozen=True)
class ProgressSnapshot:
    phase: str
    current: int
    total: int
    detail: str
    stars: int = 0
    percent: float | None = None
    overall_percent: float | None = None


@dataclass(frozen=True)
class OverallProgressTracker:
    tap_total: int
    tile_total: int
    chunk_total: int

    def __init__(self, total_rows: int, tile_total: int | None = None, chunk_total: int | None = None) -> None:
        object.__setattr__(self, "tap_total", max(total_rows, 0))
        object.__setattr__(self, "tile_total", max(tile_total if tile_total is not None else total_rows, 0))
        object.__setattr__(self, "chunk_total", max(chunk_total if chunk_total is not None else total_rows, 0))

    def snapshot(self,
                 phase: str,
                 current: int,
                 total: int,
                 detail: str = "",
                 stars: int = 0) -> ProgressSnapshot:
        phase_total = max(total, 0)
        bounded_current = max(0, min(current, phase_total)) if phase_total else max(0, current)
        percent = None
        if phase_total > 0:
            percent = bounded_current * 100.0 / phase_total

        overall_offset = 0
        overall_total = self.tap_total + self.tile_total + self.chunk_total
        if phase == "tap":
            overall_offset = 0
        elif phase == "tile":
            overall_offset = self.tap_total
        elif phase == "chunk":
            overall_offset = self.tap_total + self.tile_total
        else:
            overall_total = phase_total

        overall_percent = None
        if overall_total > 0:
            overall_percent = (overall_offset + bounded_current) * 100.0 / overall_total

        return ProgressSnapshot(
            phase=phase,
            current=current,
            total=total,
            detail=detail,
            stars=stars,
            percent=percent,
            overall_percent=overall_percent,
        )


def _elapsed_str(since: float) -> str:
    s = int(time.monotonic() - since)
    if s < 60:
        return f"{s}s"
    m, s = divmod(s, 60)
    if m < 60:
        return f"{m}m {s:02d}s"
    h, m = divmod(m, 60)
    return f"{h}h {m:02d}m"


def _render_tty_progress(snapshot: ProgressSnapshot) -> None:
    """Overwrite the current stderr line with a compact progress indicator."""
    if not _STDERR_IS_TTY:
        return
    state = _tty
    now = time.monotonic()
    if not state["start"]:
        state["start"] = now
    if state["phase"] != snapshot.phase:
        if state["phase"]:
            sys.stderr.write("\n")
            sys.stderr.flush()
        state["phase"] = snapshot.phase
    state["spin"] = (state["spin"] + 1) % len(_SPINNER_CHARS)
    spin = _SPINNER_CHARS[state["spin"]]
    elapsed = _elapsed_str(state["start"])

    percent_str = f"{snapshot.percent:6.2f}%" if snapshot.percent is not None else "--.--%"

    if snapshot.phase == "chunk":
        bar_w = 20
        filled = int(bar_w * max(min(snapshot.current, snapshot.total), 0) / max(snapshot.total, 1))
        if filled >= bar_w:
            bar = "=" * bar_w
        else:
            bar = "=" * filled + ">" + " " * (bar_w - filled - 1)
        line = (
            f"\r  [{bar}] {percent_str}   "
            f"{snapshot.current:,}/{snapshot.total:,} rows   {elapsed}"
        )
    elif snapshot.phase == "tile":
        star_str = f"   {snapshot.stars:,} stars" if snapshot.stars else ""
        line = (
            f"\r{spin} Tile Build   {percent_str}   "
            f"{snapshot.current:,}/{snapshot.total:,} rows{star_str}   {elapsed}"
        )
    else:
        star_str = f"   {snapshot.stars:,} stars" if snapshot.stars else ""
        line = (
            f"\r{spin} ESA TAP   {percent_str}   "
            f"{snapshot.current:,}/{snapshot.total:,} rows{star_str}   {elapsed}"
        )

    # Pad to fully overwrite a previously longer line
    pad = " " * max(0, state["last_len"] - len(line))
    sys.stderr.write(line + pad)
    sys.stderr.flush()
    state["last_len"] = len(line)


def _finish_tty_progress() -> None:
    """End the current progress line so subsequent prints start on a fresh line."""
    if _STDERR_IS_TTY and _tty["phase"]:
        sys.stderr.write("\n")
        sys.stderr.flush()


def _emit_status_line(message: str) -> None:
    _finish_tty_progress()
    print(message, flush=True)


def log_progress(snapshot_or_phase: ProgressSnapshot | str,
                 current: int | None = None,
                 total: int | None = None,
                 detail: str = "",
                 stars: int = 0,
                 percent: float | None = None,
                 overall_percent: float | None = None) -> None:
    if isinstance(snapshot_or_phase, ProgressSnapshot):
        snapshot = snapshot_or_phase
    else:
        if current is None or total is None:
            raise ValueError("current and total are required when logging progress without a snapshot")
        snapshot = ProgressSnapshot(
            phase=snapshot_or_phase,
            current=current,
            total=total,
            detail=detail,
            stars=stars,
            percent=percent,
            overall_percent=overall_percent,
        )

    # AFTERNIGHT_PROGRESS is a machine-readable protocol for callers that parse
    # stdout (e.g. the UI progress bridge). When running interactively both
    # stdout and stderr go to the same terminal — emitting these lines would
    # interleave with the TTY spinner and corrupt the display.
    if not _STDOUT_IS_TTY:
        parts = [
            "AFTERNIGHT_PROGRESS",
            f"phase={snapshot.phase}",
            f"current={snapshot.current}",
            f"total={snapshot.total}",
        ]
        if snapshot.percent is not None:
            parts.append(f"percent={snapshot.percent:.3f}")
        if snapshot.overall_percent is not None:
            parts.append(f"overall_percent={snapshot.overall_percent:.3f}")
        if snapshot.stars:
            parts.append(f"stars={snapshot.stars}")
        parts.append(f"detail={snapshot.detail}")
        print(" ".join(parts), flush=True)
    _render_tty_progress(snapshot)


_TAP_RETRY_ATTEMPTS = 3
_TAP_RETRY_BASE_DELAY_SECONDS = 2.0
_TAP_RETRY_MAX_DELAY_SECONDS = 60.0
_TAP_OUTAGE_THRESHOLD_SECONDS = DEFAULT_TAP_MAX_OUTAGE_SECONDS
# HTTP status codes that are safe to retry (server-side transient errors)
_TAP_RETRYABLE_HTTP_CODES = {429, 500, 502, 503, 504}


class _TapRequestThrottle:
    """Shared cooldown gate for TAP retries.

    When the ESA service starts returning transient 429/5xx failures, letting
    all worker threads retry immediately creates a thundering herd and tends to
    prolong the outage. A process-wide cooldown makes subsequent attempts back
    off together instead of continuing to hammer the service.
    """

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._next_attempt_at = 0.0

    def wait(self) -> None:
        with self._lock:
            delay = self._next_attempt_at - time.monotonic()
        if delay > 0.0:
            time.sleep(delay)

    def defer(self, delay_seconds: float) -> None:
        if delay_seconds <= 0.0:
            return
        with self._lock:
            self._next_attempt_at = max(
                self._next_attempt_at,
                time.monotonic() + delay_seconds,
            )


_TAP_REQUEST_THROTTLE = _TapRequestThrottle()


class TapTransientOutageError(RuntimeError):
    """Raised when TAP transient failures persist long enough to abort the run."""


class _TapRequestOutageMonitor:
    """Track the last successful TAP request to detect prolonged outages."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._last_success_at = time.monotonic()

    def record_success(self) -> None:
        with self._lock:
            self._last_success_at = time.monotonic()

    def failure_error(self,
                      method: str,
                      url: str,
                      exc: Exception,
                      threshold_seconds: float) -> TapTransientOutageError | None:
        if threshold_seconds <= 0.0:
            return None

        with self._lock:
            outage_seconds = time.monotonic() - self._last_success_at

        if outage_seconds < threshold_seconds:
            return None

        rounded_outage = max(1, int(round(outage_seconds)))
        rounded_threshold = max(1, int(round(threshold_seconds)))
        return TapTransientOutageError(
            f"Gaia TAP {method} requests have seen only transient failures for "
            f"{rounded_outage}s (threshold {rounded_threshold}s). "
            f"Last error while requesting {url}: {exc}. "
            "This run can be resumed safely with --resume once connectivity recovers. "
            "If the ESA service remains flaky, retry with fewer TAP workers (for example --workers 1)."
        )


_TAP_REQUEST_OUTAGE_MONITOR = _TapRequestOutageMonitor()


def configure_tap_request_policy(outage_threshold_seconds: float) -> None:
    global _TAP_OUTAGE_THRESHOLD_SECONDS, _TAP_REQUEST_THROTTLE, _TAP_REQUEST_OUTAGE_MONITOR
    _TAP_OUTAGE_THRESHOLD_SECONDS = max(float(outage_threshold_seconds), 0.0)
    _TAP_REQUEST_THROTTLE = _TapRequestThrottle()
    _TAP_REQUEST_OUTAGE_MONITOR = _TapRequestOutageMonitor()


def _is_retryable_http_error(exc: Exception) -> bool:
    if isinstance(exc, urllib.error.HTTPError):
        # HTTPError is a subclass of URLError, so check it first.
        # Only retry server-side transient codes; never retry client errors.
        return exc.code in _TAP_RETRYABLE_HTTP_CODES
    return isinstance(exc, (urllib.error.URLError, TimeoutError, ConnectionError))


def _retry_delay_seconds(exc: Exception, attempt: int) -> float:
    if isinstance(exc, urllib.error.HTTPError) and exc.headers is not None:
        retry_after = exc.headers.get("Retry-After")
        if isinstance(retry_after, str) and retry_after.strip():
            try:
                return max(float(retry_after.strip()), 0.1)
            except ValueError:
                pass
        elif isinstance(retry_after, (int, float)):
            return max(float(retry_after), 0.1)
    return min(_TAP_RETRY_BASE_DELAY_SECONDS * (2 ** attempt), _TAP_RETRY_MAX_DELAY_SECONDS)


def http_get_text(url: str, timeout_seconds: float, user_agent: str) -> str:
    last_exc: Exception | None = None
    for attempt in range(_TAP_RETRY_ATTEMPTS):
        _TAP_REQUEST_THROTTLE.wait()
        try:
            request = urllib.request.Request(url, headers={"User-Agent": user_agent})
            with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
                payload = response.read().decode("utf-8", errors="replace")
            _TAP_REQUEST_OUTAGE_MONITOR.record_success()
            return payload
        except Exception as exc:  # noqa: BLE001
            if not _is_retryable_http_error(exc):
                raise
            outage_error = _TAP_REQUEST_OUTAGE_MONITOR.failure_error(
                "GET",
                url,
                exc,
                _TAP_OUTAGE_THRESHOLD_SECONDS,
            )
            if outage_error is not None:
                raise outage_error from exc
            if attempt == _TAP_RETRY_ATTEMPTS - 1:
                raise
            delay = _retry_delay_seconds(exc, attempt)
            _TAP_REQUEST_THROTTLE.defer(delay)
            _emit_status_line(
                f"Warning: TAP GET transient error (attempt {attempt + 1}/{_TAP_RETRY_ATTEMPTS}): {exc}; "
                f"retrying in {delay:.1f}s"
            )
            last_exc = exc
    raise last_exc  # type: ignore[misc]


def http_post_form(url: str,
                   form: dict[str, str],
                   timeout_seconds: float,
                   user_agent: str) -> tuple[str, bytes]:
    last_exc: Exception | None = None
    for attempt in range(_TAP_RETRY_ATTEMPTS):
        _TAP_REQUEST_THROTTLE.wait()
        try:
            payload = urllib.parse.urlencode(form).encode("utf-8")
            request = urllib.request.Request(url,
                                             data=payload,
                                             headers={
                                                 "Content-Type": "application/x-www-form-urlencoded",
                                                 "User-Agent": user_agent,
                                             })
            with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
                response_url = response.geturl()
                response_body = response.read()
            _TAP_REQUEST_OUTAGE_MONITOR.record_success()
            return response_url, response_body
        except Exception as exc:  # noqa: BLE001
            if not _is_retryable_http_error(exc):
                raise
            outage_error = _TAP_REQUEST_OUTAGE_MONITOR.failure_error(
                "POST",
                url,
                exc,
                _TAP_OUTAGE_THRESHOLD_SECONDS,
            )
            if outage_error is not None:
                raise outage_error from exc
            if attempt == _TAP_RETRY_ATTEMPTS - 1:
                raise
            delay = _retry_delay_seconds(exc, attempt)
            _TAP_REQUEST_THROTTLE.defer(delay)
            _emit_status_line(
                f"Warning: TAP POST transient error (attempt {attempt + 1}/{_TAP_RETRY_ATTEMPTS}): {exc}; "
                f"retrying in {delay:.1f}s"
            )
            last_exc = exc
    raise last_exc  # type: ignore[misc]


def submit_tap_async_query(base_url: str,
                           query: str,
                           response_format: str,
                           timeout_seconds: float,
                           user_agent: str) -> str:
    job_url, _ = http_post_form(
        base_url.rstrip("/") + "/async",
        {
            "PHASE": "run",
            "LANG": "ADQL",
            "REQUEST": "doQuery",
            "FORMAT": response_format,
            "QUERY": query,
        },
        timeout_seconds,
        user_agent,
    )
    return job_url


def wait_for_tap_job(job_url: str,
                     job_timeout_seconds: float,
                     poll_seconds: float,
                     http_timeout_seconds: float,
                     user_agent: str) -> None:
    start = time.monotonic()
    while True:
        phase = http_get_text(job_url.rstrip("/") + "/phase", http_timeout_seconds, user_agent).strip().upper()
        if phase == "COMPLETED":
            return
        if phase in {"ERROR", "ABORTED"}:
            error_text = http_get_text(job_url.rstrip("/") + "/error", http_timeout_seconds, user_agent).strip()
            raise RuntimeError(f"Gaia TAP job failed with phase {phase}: {error_text}")
        if time.monotonic() - start > job_timeout_seconds:
            raise TimeoutError(f"Timed out waiting for Gaia TAP job: {job_url}")
        time.sleep(max(poll_seconds, 0.1))


def tap_query_csv_text(base_url: str,
                       query: str,
                       timeout_seconds: float,
                       poll_seconds: float,
                       job_timeout_seconds: float,
                       user_agent: str) -> str:
    job_url = submit_tap_async_query(base_url, query, "csv", timeout_seconds, user_agent)
    wait_for_tap_job(job_url, job_timeout_seconds, poll_seconds, timeout_seconds, user_agent)
    return http_get_text(job_url.rstrip("/") + "/results/result", timeout_seconds, user_agent)


def tap_count_rows(base_url: str,
                   query: str,
                   timeout_seconds: float,
                   poll_seconds: float,
                   job_timeout_seconds: float,
                   user_agent: str) -> int:
    csv_text = tap_query_csv_text(base_url, query, timeout_seconds, poll_seconds, job_timeout_seconds, user_agent)
    reader = csv.DictReader(io.StringIO(csv_text))
    row = next(reader, None)
    if row is None:
        return 0
    value = row.get("row_count", row.get("count", "")).strip()
    return int(value or "0")


def query_where_clause(mag_limit: float, healpix_level: int, healpix_index: int, source_alias: str = "") -> str:
    prefix = f"{source_alias}." if source_alias else ""
    return (
        f"{prefix}phot_g_mean_mag <= {mag_limit:.6f} "
        f"AND GAIA_HEALPIX_INDEX({healpix_level}, {prefix}source_id) = {healpix_index}"
    )


def count_query_for(mag_limit: float, healpix_level: int, healpix_index: int) -> str:
    return (
        "SELECT COUNT(*) AS row_count "
        "FROM gaiadr3.gaia_source AS s "
        f"WHERE {query_where_clause(mag_limit, healpix_level, healpix_index, 's')}"
    )


def total_count_query_for(mag_limit: float) -> str:
    return (
        "SELECT COUNT(*) AS row_count "
        "FROM gaiadr3.gaia_source AS s "
        f"WHERE s.phot_g_mean_mag <= {mag_limit:.6f}"
    )


def data_query_for(mag_limit: float, healpix_level: int, healpix_index: int) -> str:
    return (
        "SELECT "
        "s.source_id, s.ra, s.dec, "
        "s.phot_g_mean_mag, s.phot_bp_mean_mag, s.phot_rp_mean_mag, "
        "s.pmra, s.pmdec, "
        "ap.teff_gspphot, ap.logg_gspphot, ap.mh_gspphot, "
        "ap.ag_gspphot, ap.ebpminrp_gspphot "
        "FROM gaiadr3.gaia_source AS s "
        "LEFT JOIN gaiadr3.astrophysical_parameters AS ap "
        "ON s.source_id = ap.source_id "
        f"WHERE {query_where_clause(mag_limit, healpix_level, healpix_index, 's')}"
    )


def child_healpix_indices(index: int) -> list[int]:
    return [index * 4 + child for child in range(4)]


def pack_row_for_tile(row: dict[str, str],
                      columns: dict[str, str],
                      healpix_order: int) -> tuple[int, bytes]:
    ra_deg = parse_required_float(row, columns["ra"])
    dec_deg = parse_required_float(row, columns["dec"])
    source_id = parse_optional_uint64(row, columns["source_id"])
    g_mag = parse_required_float(row, columns["g_mag"])
    # Missing BP/RP is stored as NaN in the packed record and remains NaN in the
    # decoded in-memory star so downstream color-based consumers can treat it as
    # "unknown" instead of a valid BP-RP = 0 measurement.
    bp_mag = parse_optional_float(row, columns["bp_mag"], math.nan)
    rp_mag = parse_optional_float(row, columns["rp_mag"], math.nan)
    pmra = parse_optional_float(row, columns["pmra"], 0.0)
    pmdec = parse_optional_float(row, columns["pmdec"], 0.0)
    teff = parse_optional_scaled_unsigned(row, columns["teff"], 1, TEFF_SENTINEL, 65535)
    logg = parse_optional_scaled_signed(row, columns["logg"], 100, SIGNED_SENTINEL, -32767, 32767)
    feh = parse_optional_scaled_signed(row, columns["feh"], 100, SIGNED_SENTINEL, -32767, 32767)
    ag = parse_optional_scaled_unsigned(row, columns["ag"], 10000, UNSIGNED_SENTINEL, 65534)
    ebpminrp = parse_optional_scaled_signed(row, columns["ebpminrp"], 10000, SIGNED_SENTINEL, -32767, 32767)

    tile_index = ang2pix_ring(healpix_order, ra_deg, dec_deg)
    payload = STAR_STRUCT_V3.pack(
        normalize_ra_deg(ra_deg),
        max(-90.0, min(90.0, dec_deg)),
        g_mag,
        bp_mag,
        rp_mag,
        pmra,
        pmdec,
        source_id,
        teff,
        logg,
        feh,
        ag,
        ebpminrp,
    )
    return tile_index, payload


_MAX_SKIP_WARNINGS = 5


def write_rows_from_csv_text(csv_text: str,
                             columns: dict[str, str],
                             healpix_order: int,
                             writer: "TileWriter") -> tuple[int, int]:
    kept_rows = 0
    skipped_rows = 0
    reader = csv.DictReader(io.StringIO(csv_text))
    for row in reader:
        try:
            tile_index, payload = pack_row_for_tile(row, columns, healpix_order)
            writer.write(tile_index, payload)
            kept_rows += 1
        except (KeyError, ValueError) as exc:
            skipped_rows += 1
            if skipped_rows <= _MAX_SKIP_WARNINGS:
                _emit_status_line(f"Warning: skipping row {kept_rows + skipped_rows}: {exc}")
    return kept_rows, skipped_rows


def _process_tap_pixel(
    healpix_level: int,
    healpix_index: int,
    args: argparse.Namespace,
    mag_limit: float,
    columns: dict[str, str],
) -> tuple:
    """Worker: count + optionally fetch data for one HEALPix pixel.

    Returns (action, healpix_level, healpix_index, payload) where:
      action='empty'     payload=None
      action='subdivide' payload=[(child_level, child_index), ...]
      action='data'      payload=(packed_rows, total_rows, kept_rows, skipped_rows, warnings)
    and packed_rows is a list of (tile_index, bytes).
    """
    row_count = tap_count_rows(
        args.tap_url,
        count_query_for(mag_limit, healpix_level, healpix_index),
        args.tap_timeout_seconds,
        args.tap_poll_seconds,
        args.tap_job_timeout_seconds,
        args.tap_user_agent,
    )
    if row_count <= 0:
        return ("empty", healpix_level, healpix_index, None)

    if row_count > args.tap_max_rows_per_query and healpix_level < args.tap_max_healpix_level:
        children = [
            (healpix_level + 1, child_idx)
            for child_idx in child_healpix_indices(healpix_index)
        ]
        return ("subdivide", healpix_level, healpix_index, children)

    csv_text = tap_query_csv_text(
        args.tap_url,
        data_query_for(mag_limit, healpix_level, healpix_index),
        args.tap_timeout_seconds,
        args.tap_poll_seconds,
        args.tap_job_timeout_seconds,
        args.tap_user_agent,
    )

    packed_rows: list[tuple[int, bytes]] = []
    skipped = 0
    warnings: list[str] = []
    reader = csv.DictReader(io.StringIO(csv_text))
    for row in reader:
        try:
            tile_idx, payload = pack_row_for_tile(row, columns, args.healpix_order)
            packed_rows.append((tile_idx, payload))
        except (KeyError, ValueError) as exc:
            skipped += 1
            if len(warnings) < _MAX_SKIP_WARNINGS:
                warnings.append(
                    f"healpix={healpix_level}:{healpix_index} row {len(packed_rows) + skipped}: {exc}"
                )

    return ("data", healpix_level, healpix_index,
            (packed_rows, row_count, len(packed_rows), skipped, warnings))


def _emit_tap_worker_heartbeat(
    future_contexts: dict[concurrent.futures.Future, tuple[int, int, float]],
    total_rows: int,
    tap_total_rows: int,
) -> None:
    if not future_contexts:
        return

    active = sorted((started, level, index) for level, index, started in future_contexts.values())
    oldest_started, oldest_level, oldest_index = active[0]
    listed = ", ".join(f"{level}:{index}" for _, level, index in active[:3])
    if len(active) > 3:
        listed += f", +{len(active) - 3} more"

    progress_suffix = ""
    if tap_total_rows > 0:
        percent = total_rows * 100.0 / tap_total_rows
        progress_suffix = f" Progress is {total_rows:,}/{tap_total_rows:,} rows ({percent:.2f}%)."

    _emit_status_line(
        f"Info: TAP heartbeat: {len(active)} in-flight pixel(s); "
        f"oldest healpix={oldest_level}:{oldest_index} running {_elapsed_str(oldest_started)}. "
        f"Active pixels: {listed}.{progress_suffix}"
    )


_PIXEL_DATA_MAGIC = b"ANPX"
_PIXEL_DATA_FORMAT_VERSION = 1
_PIXEL_DATA_HEADER_STRUCT = struct.Struct("<4sHHIIIII")
_PIXEL_TILE_ENTRY_STRUCT = struct.Struct("<I")
_TAP_METADATA_FILE = "tap_resume_metadata.json"


@dataclass(frozen=True)
class TapPixelDataRecord:
    healpix_level: int
    healpix_index: int
    row_count: int
    kept_rows: int
    skipped_rows: int
    packed_rows: list[tuple[int, bytes]]


def _atomic_write_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.parent / f"{path.name}.tmp"
    try:
        with tmp_path.open("wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        tmp_path.replace(path)
    finally:
        if tmp_path.exists():
            tmp_path.unlink()


def _atomic_write_text(path: Path, text: str) -> None:
    _atomic_write_bytes(path, text.encode("utf-8"))


class TapPixelStore:
    """Durable per-pixel TAP results committed atomically."""

    def __init__(self, root_dir: Path) -> None:
        self._root_dir = root_dir
        self._root_dir.mkdir(parents=True, exist_ok=True)
        self._statuses: dict[tuple[int, int], str] = {}
        self._data_records: dict[tuple[int, int], TapPixelDataRecord] = {}
        self._load()

    @property
    def root_dir(self) -> Path:
        return self._root_dir

    @property
    def metadata_path(self) -> Path:
        return self._root_dir / _TAP_METADATA_FILE

    def data_path(self, healpix_level: int, healpix_index: int) -> Path:
        return self._root_dir / f"{self._pixel_stem(healpix_level, healpix_index)}.data"

    def pixel_status(self, healpix_level: int, healpix_index: int) -> str | None:
        return self._statuses.get((healpix_level, healpix_index))

    def completed_totals(self) -> tuple[int, int, int]:
        total_rows = sum(record.row_count for record in self._data_records.values())
        kept_rows = sum(record.kept_rows for record in self._data_records.values())
        skipped_rows = sum(record.skipped_rows for record in self._data_records.values())
        return total_rows, kept_rows, skipped_rows

    def data_file_count(self) -> int:
        return len(self._data_records)

    def iter_data_records(self) -> list[TapPixelDataRecord]:
        return [self._data_records[key] for key in sorted(self._data_records)]

    def load_metadata(self) -> dict[str, object]:
        if not self.metadata_path.exists():
            return {}
        with self.metadata_path.open("r", encoding="utf-8") as handle:
            return json.load(handle)

    def save_metadata(self, data: dict[str, object]) -> None:
        _atomic_write_text(self.metadata_path, json.dumps(data, indent=2, sort_keys=True) + "\n")

    def mark_subdivided(self, healpix_level: int, healpix_index: int) -> None:
        _atomic_write_bytes(self._marker_path(healpix_level, healpix_index, "subdivide"), b"")
        self._statuses[(healpix_level, healpix_index)] = "subdivide"

    def commit_empty(self, healpix_level: int, healpix_index: int) -> None:
        _atomic_write_bytes(self._marker_path(healpix_level, healpix_index, "empty"), b"")
        self._statuses[(healpix_level, healpix_index)] = "empty"

    def commit_data(self,
                    healpix_level: int,
                    healpix_index: int,
                    row_count: int,
                    kept_rows: int,
                    skipped_rows: int,
                    packed_rows: list[tuple[int, bytes]]) -> None:
        header = _PIXEL_DATA_HEADER_STRUCT.pack(
            _PIXEL_DATA_MAGIC,
            _PIXEL_DATA_FORMAT_VERSION,
            0,
            healpix_level,
            healpix_index,
            row_count,
            kept_rows,
            skipped_rows,
        )
        payload = bytearray(header)
        for tile_index, star_payload in packed_rows:
            payload.extend(_PIXEL_TILE_ENTRY_STRUCT.pack(tile_index))
            payload.extend(star_payload)

        _atomic_write_bytes(self.data_path(healpix_level, healpix_index), bytes(payload))
        key = (healpix_level, healpix_index)
        self._statuses[key] = "data"
        self._data_records[key] = TapPixelDataRecord(
            healpix_level=healpix_level,
            healpix_index=healpix_index,
            row_count=row_count,
            kept_rows=kept_rows,
            skipped_rows=skipped_rows,
            packed_rows=list(packed_rows),
        )

    def _load(self) -> None:
        for path in sorted(self._root_dir.iterdir()):
            if not path.is_file() or path.name.endswith(".tmp") or path.name == _TAP_METADATA_FILE:
                continue
            parsed = self._parse_pixel_key(path)
            if parsed is None:
                continue
            healpix_level, healpix_index, status = parsed
            key = (healpix_level, healpix_index)
            if status == "data":
                self._statuses[key] = "data"
                self._data_records[key] = self._read_data_record(path)
            elif key not in self._statuses:
                self._statuses[key] = status

    def _pixel_stem(self, healpix_level: int, healpix_index: int) -> str:
        return f"l{healpix_level:02d}_p{healpix_index:08d}"

    def _marker_path(self, healpix_level: int, healpix_index: int, status: str) -> Path:
        return self._root_dir / f"{self._pixel_stem(healpix_level, healpix_index)}.{status}"

    def _parse_pixel_key(self, path: Path) -> tuple[int, int, str] | None:
        if "." not in path.name:
            return None
        stem, status = path.name.rsplit(".", 1)
        if status not in {"data", "empty", "subdivide"}:
            return None
        if not stem.startswith("l") or "_p" not in stem:
            return None
        level_part, index_part = stem.split("_p", 1)
        try:
            return int(level_part[1:]), int(index_part), status
        except ValueError:
            return None

    def _read_data_record(self, path: Path) -> TapPixelDataRecord:
        raw = path.read_bytes()
        if len(raw) < _PIXEL_DATA_HEADER_STRUCT.size:
            raise ValueError(f"Corrupted TAP pixel file: {path}")

        magic, version, flags, healpix_level, healpix_index, row_count, kept_rows, skipped_rows = (
            _PIXEL_DATA_HEADER_STRUCT.unpack_from(raw, 0)
        )
        if magic != _PIXEL_DATA_MAGIC or version != _PIXEL_DATA_FORMAT_VERSION or flags != 0:
            raise ValueError(f"Unexpected TAP pixel file header: {path}")

        packed_rows: list[tuple[int, bytes]] = []
        cursor = _PIXEL_DATA_HEADER_STRUCT.size
        entry_size = _PIXEL_TILE_ENTRY_STRUCT.size + STAR_STRUCT_V3.size
        while cursor < len(raw):
            if len(raw) - cursor < entry_size:
                raise ValueError(f"Truncated TAP pixel file: {path}")
            tile_index = _PIXEL_TILE_ENTRY_STRUCT.unpack_from(raw, cursor)[0]
            cursor += _PIXEL_TILE_ENTRY_STRUCT.size
            payload = raw[cursor:cursor + STAR_STRUCT_V3.size]
            cursor += STAR_STRUCT_V3.size
            packed_rows.append((tile_index, payload))

        return TapPixelDataRecord(
            healpix_level=healpix_level,
            healpix_index=healpix_index,
            row_count=row_count,
            kept_rows=kept_rows,
            skipped_rows=skipped_rows,
            packed_rows=packed_rows,
        )


def populate_tiles_from_official_tap(args: argparse.Namespace,
                                     mag_limit: float,
                                     columns: dict[str, str],
                                     pixel_store: TapPixelStore,
                                     tap_total_rows: int = 0,
                                     progress_tracker: OverallProgressTracker | None = None) -> tuple[int, int, int]:
    total_rows, kept_rows, skipped_rows = pixel_store.completed_totals()
    warnings_printed = 0
    resumed_leaf_pixels = 0
    resumed_subdivide_pixels = 0

    initial_pixels = (
        args.tap_include_pixel
        if args.tap_include_pixel
        else range(pixel_count_for_order(args.tap_initial_healpix_level))
    )

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending_futures: set[concurrent.futures.Future] = set()
        future_contexts: dict[concurrent.futures.Future, tuple[int, int, float]] = {}
        heartbeat_seconds = max(float(args.tap_heartbeat_seconds), 0.0)
        next_heartbeat_at = time.monotonic() + heartbeat_seconds if heartbeat_seconds > 0.0 else 0.0

        def schedule_pixel(healpix_level: int, healpix_index: int) -> None:
            nonlocal resumed_leaf_pixels, resumed_subdivide_pixels
            status = pixel_store.pixel_status(healpix_level, healpix_index)
            if status == "subdivide":
                resumed_subdivide_pixels += 1
                for child_index in child_healpix_indices(healpix_index):
                    schedule_pixel(healpix_level + 1, child_index)
                return
            if status in {"data", "empty"}:
                resumed_leaf_pixels += 1
                return
            future = pool.submit(
                _process_tap_pixel,
                healpix_level,
                healpix_index,
                args,
                mag_limit,
                columns,
            )
            pending_futures.add(future)
            future_contexts[future] = (healpix_level, healpix_index, time.monotonic())

        for pixel in initial_pixels:
            schedule_pixel(args.tap_initial_healpix_level, pixel)

        if resumed_leaf_pixels or resumed_subdivide_pixels:
            _emit_status_line(
                "Resume: recovered "
                f"{resumed_leaf_pixels} completed leaf TAP pixels and "
                f"{resumed_subdivide_pixels} subdivided TAP nodes"
            )

        if tap_total_rows > 0 and total_rows > 0:
            snapshot = (
                progress_tracker.snapshot("tap", total_rows, tap_total_rows, detail="resume", stars=kept_rows)
                if progress_tracker
                else ProgressSnapshot(
                    phase="tap",
                    current=total_rows,
                    total=tap_total_rows,
                    detail="resume",
                    stars=kept_rows,
                    percent=(total_rows * 100.0 / tap_total_rows),
                )
            )
            log_progress(snapshot)

        while pending_futures:
            timeout = None
            if heartbeat_seconds > 0.0:
                timeout = max(0.0, next_heartbeat_at - time.monotonic())
            done, pending_futures = concurrent.futures.wait(
                pending_futures,
                timeout=timeout,
                return_when=concurrent.futures.FIRST_COMPLETED,
            )
            if not done:
                _emit_tap_worker_heartbeat(future_contexts, total_rows, tap_total_rows)
                next_heartbeat_at = time.monotonic() + heartbeat_seconds
                continue
            for fut in done:
                future_contexts.pop(fut, None)
                action, hlevel, hindex, payload = fut.result()

                if action == "subdivide":
                    pixel_store.mark_subdivided(hlevel, hindex)
                    for child_level, child_index in payload:
                        schedule_pixel(child_level, child_index)
                elif action == "data":
                    packed_rows, total_delta, kept_delta, skipped_delta, warnings = payload
                    pixel_store.commit_data(
                        hlevel,
                        hindex,
                        row_count=total_delta,
                        kept_rows=kept_delta,
                        skipped_rows=skipped_delta,
                        packed_rows=packed_rows,
                    )
                    total_rows += total_delta
                    kept_rows += kept_delta
                    skipped_rows += skipped_delta
                    for w in warnings:
                        warnings_printed += 1
                        if warnings_printed <= _MAX_SKIP_WARNINGS:
                            _emit_status_line(f"Warning: skipping row {w}")
                elif action == "empty":
                    pixel_store.commit_empty(hlevel, hindex)

                snapshot = (
                    progress_tracker.snapshot(
                        "tap",
                        total_rows,
                        tap_total_rows,
                        detail=f"healpix={hlevel}:{hindex}",
                        stars=kept_rows,
                    )
                    if tap_total_rows > 0 and progress_tracker
                    else ProgressSnapshot(
                        phase="tap",
                        current=total_rows,
                        total=max(tap_total_rows, total_rows, 1),
                        detail=f"healpix={hlevel}:{hindex}",
                        stars=kept_rows,
                        percent=(total_rows * 100.0 / tap_total_rows) if tap_total_rows > 0 else None,
                    )
                )
                log_progress(snapshot)
            if heartbeat_seconds > 0.0:
                next_heartbeat_at = time.monotonic() + heartbeat_seconds

    return total_rows, kept_rows, skipped_rows


class TileWriter:
    def __init__(self, tile_dir: Path, max_open_files: int) -> None:
        self._tile_dir = tile_dir
        self._max_open_files = max(max_open_files, 1)
        self._open_files: OrderedDict[int, IO[bytes]] = OrderedDict()
        self._lock = threading.Lock()

    def write(self, tile_index: int, payload: bytes) -> None:
        with self._lock:
            handle = self._get_handle(tile_index)
            handle.write(payload)

    def close(self) -> None:
        with self._lock:
            while self._open_files:
                _, handle = self._open_files.popitem(last=False)
                handle.close()

    def _get_handle(self, tile_index: int) -> IO[bytes]:
        # Must be called with self._lock held.
        handle = self._open_files.pop(tile_index, None)
        if handle is None:
            if len(self._open_files) >= self._max_open_files:
                _, oldest = self._open_files.popitem(last=False)
                oldest.close()
            path = self._tile_dir / f"tile_{tile_index:05d}.bin"
            path.parent.mkdir(parents=True, exist_ok=True)
            handle = path.open("ab")
        self._open_files[tile_index] = handle
        return handle


def ensure_clean_temp_tile_dir(output_dir: Path, healpix_order: int) -> Path:
    tile_dir = output_dir / f".tmp_healpix_order{healpix_order}"
    if tile_dir.exists() and any(tile_dir.glob("tile_*.bin")):
        raise RuntimeError(
            f"Temporary tile directory already exists with tile files: {tile_dir}. "
            "Remove it before re-running the generator, or pass --resume to continue."
        )
    tile_dir.mkdir(parents=True, exist_ok=True)
    return tile_dir


def prepare_tile_dir_for_resume(output_dir: Path, healpix_order: int) -> Path:
    """Return or create the temporary tile directory, preserving existing tiles on resume."""
    tile_dir = output_dir / f".tmp_healpix_order{healpix_order}"
    tile_dir.mkdir(parents=True, exist_ok=True)
    existing = list(tile_dir.glob("tile_*.bin"))
    if existing:
        total_bytes = sum(p.stat().st_size for p in existing)
        _emit_status_line(f"Resume: found {len(existing)} existing tile files ({total_bytes:,} bytes)")
    return tile_dir


def materialize_tiles_from_tap_store(pixel_store: TapPixelStore,
                                     tile_dir: Path,
                                     max_open_files: int,
                                     progress_tracker: OverallProgressTracker | None = None,
                                     total_rows: int | None = None) -> int:
    shutil.rmtree(tile_dir, ignore_errors=True)
    tile_dir.mkdir(parents=True, exist_ok=True)

    phase_total = total_rows if total_rows is not None else pixel_store.completed_totals()[1]
    rows_written = 0
    writer = TileWriter(tile_dir, max_open_files)
    try:
        for record in pixel_store.iter_data_records():
            for tile_index, payload in record.packed_rows:
                writer.write(tile_index, payload)
            rows_written += record.kept_rows
            if phase_total > 0:
                snapshot = (
                    progress_tracker.snapshot(
                        "tile",
                        rows_written,
                        phase_total,
                        detail=f"healpix={record.healpix_level}:{record.healpix_index}",
                        stars=rows_written,
                    )
                    if progress_tracker
                    else ProgressSnapshot(
                        phase="tile",
                        current=rows_written,
                        total=phase_total,
                        detail=f"healpix={record.healpix_level}:{record.healpix_index}",
                        stars=rows_written,
                        percent=(rows_written * 100.0 / phase_total),
                    )
                )
                log_progress(snapshot)
    finally:
        writer.close()

    return rows_written


def write_hashed(handle: IO[bytes], sha256, payload: bytes) -> None:
    handle.write(payload)
    sha256.update(payload)


def tile_star_count(path: Path) -> int:
    size = path.stat().st_size
    if size % STAR_STRUCT_V3.size != 0:
        raise ValueError(f"Tile file has unexpected size: {path}")
    return size // STAR_STRUCT_V3.size


def chunk_file_name(catalog_id: str, chunk_index: int, chunk_count: int) -> str:
    width = max(2, len(str(chunk_count - 1)))
    return f"{catalog_id}_chunk_{chunk_index:0{width}d}.ansp"


def build_chunk_manifest(tile_dir: Path,
                         output_dir: Path,
                         catalog_name: str,
                         catalog_id: str,
                         healpix_order: int,
                         mag_limit: float,
                         chunk_index: int,
                         chunk_count: int,
                         first_tile: int,
                         tile_count: int) -> dict[str, object]:
    tile_entries: list[tuple[Path, int, int]] = []
    total_stars_in_chunk = 0
    data_offset = 0

    for tile_index in range(first_tile, first_tile + tile_count):
        tile_path = tile_dir / f"tile_{tile_index:05d}.bin"
        star_count = tile_star_count(tile_path) if tile_path.exists() else 0
        tile_entries.append((tile_path, star_count, data_offset))
        data_offset += star_count * STAR_STRUCT_V3.size
        total_stars_in_chunk += star_count

    chunk_filename = chunk_file_name(catalog_id, chunk_index, chunk_count)
    header = {
        "catalog_name": catalog_name,
        "catalog_id": catalog_id,
        "epoch_year": 2016.0,
        "magnitude_limit": mag_limit,
        "healpix_order": healpix_order,
        "pixel_ordering": "ring",
        "record_version": 3,
        "record_size_bytes": STAR_STRUCT_V3.size,
        "chunk_index": chunk_index,
        "chunk_count": chunk_count,
        "first_tile": first_tile,
        "tile_count": tile_count,
        "total_stars_in_chunk": total_stars_in_chunk,
    }
    header_json = json.dumps(header, separators=(",", ":"), sort_keys=True).encode("utf-8")
    padding = b"\x00" * ((-(ANSP_PREAMBLE_STRUCT.size + len(header_json))) % 8)
    chunk_path = output_dir / chunk_filename
    sha256 = hashlib.sha256()

    try:
        with chunk_path.open("wb") as handle:
            write_hashed(handle, sha256,
                         ANSP_PREAMBLE_STRUCT.pack(ANSP_MAGIC,
                                                   ANSP_FORMAT_VERSION,
                                                   0,
                                                   len(header_json),
                                                   0))
            write_hashed(handle, sha256, header_json)
            if padding:
                write_hashed(handle, sha256, padding)

            for _, star_count, offset in tile_entries:
                write_hashed(handle, sha256, ANSP_INDEX_ENTRY_STRUCT.pack(star_count, offset))

            for tile_path, star_count, _ in tile_entries:
                if star_count == 0:
                    continue
                with tile_path.open("rb") as tile_handle:
                    while True:
                        payload = tile_handle.read(1024 * 1024)
                        if not payload:
                            break
                        write_hashed(handle, sha256, payload)
    except Exception:
        if chunk_path.exists():
            chunk_path.unlink()
        raise

    for tile_path, _, _ in tile_entries:
        if tile_path.exists():
            tile_path.unlink()

    return {
        "index": chunk_index,
        "file": chunk_filename,
        "first_tile": first_tile,
        "tile_count": tile_count,
        "stars": total_stars_in_chunk,
        "size_bytes": chunk_path.stat().st_size,
        "sha256": sha256.hexdigest(),
    }


def _load_prior_chunk_manifests(output_dir: Path) -> dict[str, dict[str, object]]:
    """Load chunk manifest entries from a prior catalog_meta.json, keyed by filename."""
    meta_path = output_dir / "catalog_meta.json"
    if not meta_path.exists():
        return {}
    try:
        with meta_path.open("r", encoding="utf-8") as f:
            meta = json.load(f)
        return {chunk["file"]: chunk for chunk in meta.get("chunks", [])}
    except (json.JSONDecodeError, KeyError, TypeError):
        return {}


_CHUNK_CHECKPOINT_FILE = ".tmp_chunk_manifests.json"


class ChunkManifestCheckpoint:
    def __init__(self, output_dir: Path) -> None:
        self._path = output_dir / _CHUNK_CHECKPOINT_FILE
        self._manifests: dict[int, dict[str, object]] = {}
        self._load()

    def manifests_by_file(self) -> dict[str, dict[str, object]]:
        return {str(manifest["file"]): manifest for manifest in self._manifests.values()}

    def record_manifest(self, manifest: dict[str, object]) -> None:
        self._manifests[int(manifest["index"])] = manifest
        payload = {
            "chunks": [self._manifests[index] for index in sorted(self._manifests)],
        }
        _atomic_write_text(self._path, json.dumps(payload, indent=2, sort_keys=True) + "\n")

    def remove(self) -> None:
        if self._path.exists():
            self._path.unlink()

    def _load(self) -> None:
        if not self._path.exists():
            return
        try:
            with self._path.open("r", encoding="utf-8") as handle:
                payload = json.load(handle)
            for manifest in payload.get("chunks", []):
                self._manifests[int(manifest["index"])] = manifest
        except (json.JSONDecodeError, KeyError, TypeError, ValueError):
            self._manifests = {}


def _verify_chunk_sha256(chunk_path: Path, expected_sha256: str) -> bool:
    """Check an existing chunk file against its expected SHA-256."""
    sha256 = hashlib.sha256()
    try:
        with chunk_path.open("rb") as f:
            while True:
                block = f.read(1024 * 1024)
                if not block:
                    break
                sha256.update(block)
    except OSError:
        return False
    return sha256.hexdigest() == expected_sha256


def build_chunked_catalog(tile_dir: Path,
                          output_dir: Path,
                          catalog_name: str,
                          catalog_id: str,
                          healpix_order: int,
                          mag_limit: float,
                          workers: int = 1,
                          resume: bool = False,
                          progress_tracker: OverallProgressTracker | None = None,
                          total_rows: int | None = None) -> list[dict[str, object]]:
    total_tiles = pixel_count_for_order(healpix_order)
    if total_tiles % HEALPIX_TILES_PER_CHUNK != 0:
        raise ValueError(
            f"Order {healpix_order} has {total_tiles} tiles, which is not divisible by {HEALPIX_TILES_PER_CHUNK}"
        )

    chunk_count = total_tiles // HEALPIX_TILES_PER_CHUNK
    manifests_by_index: dict[int, dict[str, object]] = {}
    chunk_checkpoint = ChunkManifestCheckpoint(output_dir)
    if not resume:
        chunk_checkpoint.remove()
        chunk_checkpoint = ChunkManifestCheckpoint(output_dir)

    prior_manifests: dict[str, dict[str, object]] = {}
    if resume:
        prior_manifests.update(_load_prior_chunk_manifests(output_dir))
        prior_manifests.update(chunk_checkpoint.manifests_by_file())
    chunks_skipped = 0

    def _build_one(chunk_index: int) -> tuple[int, dict[str, object], bool]:
        first_tile = chunk_index * HEALPIX_TILES_PER_CHUNK

        # On resume, check if this chunk already exists with valid SHA-256
        if prior_manifests:
            fname = chunk_file_name(catalog_id, chunk_index, chunk_count)
            prior = prior_manifests.get(fname)
            chunk_path = output_dir / fname
            if prior and chunk_path.exists() and "sha256" in prior:
                if _verify_chunk_sha256(chunk_path, prior["sha256"]):
                    return chunk_index, prior, True

        manifest = build_chunk_manifest(
            tile_dir, output_dir, catalog_name, catalog_id,
            healpix_order, mag_limit,
            chunk_index, chunk_count, first_tile, HEALPIX_TILES_PER_CHUNK,
        )
        return chunk_index, manifest, False

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = [pool.submit(_build_one, ci) for ci in range(chunk_count)]
        for fut in concurrent.futures.as_completed(futs):
            chunk_index, manifest, reused = fut.result()
            manifests_by_index[chunk_index] = manifest
            if reused:
                chunks_skipped += 1
            else:
                chunk_checkpoint.record_manifest(manifest)

            rows_completed = sum(int(item["stars"]) for item in manifests_by_index.values())
            phase_total = total_rows if total_rows is not None else chunk_count
            snapshot = (
                progress_tracker.snapshot(
                    "chunk",
                    rows_completed,
                    phase_total,
                    detail=f"chunk={chunk_index}",
                    stars=rows_completed,
                )
                if progress_tracker and total_rows is not None
                else ProgressSnapshot(
                    phase="chunk",
                    current=rows_completed if total_rows is not None else len(manifests_by_index),
                    total=phase_total,
                    detail=f"chunk={chunk_index}",
                    stars=rows_completed,
                    percent=((rows_completed * 100.0 / phase_total) if total_rows is not None and phase_total > 0 else None),
                )
            )
            log_progress(snapshot)

    if resume and chunks_skipped:
        _emit_status_line(f"Resume: reused {chunks_skipped} existing chunks with valid SHA-256")

    return [manifests_by_index[i] for i in range(chunk_count)]


def tap_pixel_store_dir(output_dir: Path, healpix_order: int) -> Path:
    return output_dir / f".tmp_tap_pixels_order{healpix_order}"


def prepare_tap_pixel_store(output_dir: Path, healpix_order: int, resume: bool) -> TapPixelStore:
    root_dir = tap_pixel_store_dir(output_dir, healpix_order)
    if not resume and root_dir.exists() and any(root_dir.iterdir()):
        raise RuntimeError(
            f"Temporary TAP pixel directory already exists with committed data: {root_dir}. "
            "Remove it before re-running the generator, or pass --resume to continue."
        )

    store = TapPixelStore(root_dir)
    if resume:
        total_rows, kept_rows, skipped_rows = store.completed_totals()
        if store.data_file_count():
            _emit_status_line(
                f"Resume: found {store.data_file_count()} committed TAP pixel files "
                f"({kept_rows:,} stars, {total_rows:,} rows, {skipped_rows:,} skipped)"
            )
    return store


def load_or_query_tap_total_rows(args: argparse.Namespace, pixel_store: TapPixelStore, mag_limit: float) -> int:
    metadata = pixel_store.load_metadata()
    metadata_mag_limit = metadata.get("mag_limit")
    metadata_total_rows = metadata.get("tap_total_rows")
    if isinstance(metadata_mag_limit, (int, float)) and math.isclose(float(metadata_mag_limit), float(mag_limit)) \
            and isinstance(metadata_total_rows, int) and metadata_total_rows >= 0:
        return metadata_total_rows

    total_rows = tap_count_rows(
        args.tap_url,
        total_count_query_for(mag_limit),
        args.tap_timeout_seconds,
        args.tap_poll_seconds,
        args.tap_job_timeout_seconds,
        args.tap_user_agent,
    )
    pixel_store.save_metadata(
        {
            "tap_total_rows": total_rows,
            "mag_limit": mag_limit,
            "healpix_order": args.healpix_order,
            "tap_initial_healpix_level": args.tap_initial_healpix_level,
            "tap_max_healpix_level": args.tap_max_healpix_level,
            "tap_max_rows_per_query": args.tap_max_rows_per_query,
            "tap_url": args.tap_url,
        }
    )
    return total_rows


def main() -> int:
    args = parse_args()
    configure_tap_request_policy(args.tap_max_outage_seconds)

    preset = PRESETS.get(args.preset or "", {})
    if args.tap_official:
        input_paths = []
        if args.extra_input or args.input_dir:
            _emit_status_line("Warning: --extra-input and --input-dir are ignored when --tap-official is used.")
    else:
        if args.input_csv is None:
            _emit_status_line("Error: input_csv is required when --tap-official is not used.")
            return 1
        input_paths = resolve_inputs(args.input_csv, args.extra_input, args.input_dir)

    output_dir: Path = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    tile_dir = prepare_tile_dir_for_resume(output_dir, args.healpix_order) if args.resume else ensure_clean_temp_tile_dir(output_dir, args.healpix_order)

    if args.max_open_files is None:
        total_tiles = pixel_count_for_order(args.healpix_order)
        try:
            import resource as _resource
            soft, _ = _resource.getrlimit(_resource.RLIMIT_NOFILE)
            args.max_open_files = min(total_tiles, max(128, soft - 64))
        except Exception:
            args.max_open_files = min(total_tiles, 4096)

    catalog_name = args.catalog_name or preset.get("catalog_name", "Gaia DR3 Local Catalog")
    catalog_id = args.catalog_id or preset.get("catalog_id", "gaia_dr3")
    mag_limit = args.mag_limit if args.mag_limit is not None else preset.get("mag_limit", 14.0)

    columns = {
        "ra": args.ra_column,
        "dec": args.dec_column,
        "source_id": args.source_id_column,
        "g_mag": args.g_column,
        "bp_mag": args.bp_column,
        "rp_mag": args.rp_column,
        "pmra": args.pmra_column,
        "pmdec": args.pmdec_column,
        "teff": args.teff_column,
        "logg": args.logg_column,
        "feh": args.feh_column,
        "ag": args.ag_column,
        "ebpminrp": args.ebpminrp_column,
    }

    total_rows = 0
    kept_rows = 0
    skipped_rows = 0
    progress_tracker: OverallProgressTracker | None = None
    pixel_store: TapPixelStore | None = None

    if args.tap_official:
        pixel_store = prepare_tap_pixel_store(output_dir, args.healpix_order, resume=args.resume)
        total_expected_rows = load_or_query_tap_total_rows(args, pixel_store, mag_limit)
        total_rows, kept_rows, skipped_rows = pixel_store.completed_totals()
        progress_tracker = OverallProgressTracker(
            total_expected_rows,
            tile_total=total_expected_rows,
            chunk_total=total_expected_rows,
        )
        total_rows, kept_rows, skipped_rows = populate_tiles_from_official_tap(
            args,
            mag_limit,
            columns,
            pixel_store,
            tap_total_rows=total_expected_rows,
            progress_tracker=progress_tracker,
        )
        progress_tracker = OverallProgressTracker(total_expected_rows, tile_total=kept_rows, chunk_total=kept_rows)
        materialize_tiles_from_tap_store(
            pixel_store,
            tile_dir,
            args.max_open_files,
            progress_tracker=progress_tracker,
            total_rows=kept_rows,
        )
    else:
        writer = TileWriter(tile_dir, args.max_open_files)
        try:
            for input_path in input_paths:
                with open_catalog_input(input_path, args.encoding) as handle:
                    reader = csv.DictReader(handle)
                    for row in reader:
                        total_rows += 1
                        try:
                            g_mag = parse_required_float(row, columns["g_mag"])
                            if g_mag > mag_limit:
                                continue
                            tile_index, payload = pack_row_for_tile(row, columns, args.healpix_order)
                            writer.write(tile_index, payload)
                            kept_rows += 1
                        except (KeyError, ValueError):
                            skipped_rows += 1
        finally:
            writer.close()
        if args.resume:
            kept_rows_from_tiles = 0
            for tile_path in sorted(tile_dir.glob("tile_*.bin")):
                kept_rows_from_tiles += tile_star_count(tile_path)
            if kept_rows_from_tiles > kept_rows:
                _emit_status_line(
                    f"Resume: tile files contain {kept_rows_from_tiles:,} stars "
                    f"({kept_rows_from_tiles - kept_rows:,} from prior run)"
                )
                kept_rows = kept_rows_from_tiles

    chunk_manifests = build_chunked_catalog(tile_dir,
                                            output_dir,
                                            catalog_name,
                                            catalog_id,
                                            args.healpix_order,
                                            mag_limit,
                                            workers=args.workers,
                                            resume=args.resume,
                                            progress_tracker=progress_tracker,
                                            total_rows=kept_rows if progress_tracker else None)
    total_chunk_stars = sum(int(chunk["stars"]) for chunk in chunk_manifests)
    if total_chunk_stars != kept_rows:
        raise RuntimeError(f"Chunk output star count mismatch: pass1={kept_rows}, pass2={total_chunk_stars}")

    ChunkManifestCheckpoint(output_dir).remove()
    shutil.rmtree(tile_dir, ignore_errors=True)
    if pixel_store is not None:
        shutil.rmtree(pixel_store.root_dir, ignore_errors=True)

    meta = {
        "format_version": GAIA_CATALOG_FORMAT_VERSION,
        "storage": "chunks",
        "record_size_bytes": STAR_STRUCT_V3.size,
        "catalog_name": catalog_name,
        "catalog_id": catalog_id,
        "healpix_order": args.healpix_order,
        "pixel_ordering": "ring",
        "epoch_year": 2016.0,
        "magnitude_limit": mag_limit,
        "total_stars": kept_rows,
        "chunk_count": len(chunk_manifests),
        "chunk_prefix": f"{catalog_id}_chunk_",
        "chunk_extension": ".ansp",
        "chunks": chunk_manifests,
        "input_rows": total_rows,
        "skipped_rows": skipped_rows,
        "input_files": [str(path) for path in input_paths],
        "source_mode": "official_tap" if args.tap_official else "csv",
    }

    with (output_dir / "catalog_meta.json").open("w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2, sort_keys=True)
        handle.write("\n")

    _emit_status_line(
        f"Wrote {kept_rows} Gaia rows into {len(chunk_manifests)} ANSP chunks "
        f"under {output_dir}"
    )
    if skipped_rows:
        _emit_status_line(f"Skipped {skipped_rows} malformed rows")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
