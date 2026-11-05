#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import csv
import gzip
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


TAP_URL = "https://gea.esac.esa.int/tap-server/tap"
DATA_SERVER_URL = "https://gea.esac.esa.int/data-server/data"
CDN_LIST_URL = "https://gaia.eu-1.cdn77-storage.com/"
CDN_PUBLIC_BASE_URL = "https://cdn.gea.esac.esa.int/"
XP_SAMPLED_PREFIX = "Gaia/gdr3/Spectroscopy/xp_sampled_mean_spectrum/"
TOOL_VERSION = "spcc-gaia-validation-fetcher-phase3-v1"
FIRST_XP_BULK_FILE_UPPER_SOURCE_ID = (3112 * 256) * (2**35)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch Gaia DR3 XP sampled validation or regional-cache inputs from official Gaia TAP and bulk XP sources."
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--source-count", type=int, default=3)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument("--artifact-prefix", default="tiny_gaia")
    parser.add_argument("--region-name", help="Optional region label recorded in the transcript.")
    parser.add_argument("--ra-deg", type=float, help="ICRS right ascension for a regional cache query.")
    parser.add_argument("--dec-deg", type=float, help="ICRS declination for a regional cache query.")
    parser.add_argument("--radius-deg", type=float, help="Search radius for a regional cache query.")
    parser.add_argument("--mag-min", type=float, default=10.0)
    parser.add_argument("--mag-max", type=float, default=13.0)
    parser.add_argument("--min-bp-rp-snr", type=float, default=50.0)
    parser.add_argument(
        "--allow-multiple-bulk-files",
        action="store_true",
        help="Allow regional source IDs to span multiple Gaia XP sampled bulk files.",
    )
    parser.add_argument(
        "--probe-datalink",
        action="store_true",
        help="Probe the official Gaia DataLink data-server endpoint and record status in the transcript.",
    )
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    try:
        fetch_validation_fixture(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def fetch_validation_fixture(args: argparse.Namespace) -> None:
    args.output_dir.mkdir(parents=True, exist_ok=True)
    validate_selection_args(args)
    tap_rows, tap_adql = query_tap_sources(args)
    source_ids = [str(row["source_id"]) for row in tap_rows]
    datalink_attempts = probe_datalink(source_ids, args.timeout_seconds) if args.probe_datalink else []
    spectrum_rows, spectrum_files = fetch_spectrum_rows(
        source_ids,
        args.timeout_seconds,
        allow_multiple_bulk_files=args.allow_multiple_bulk_files or args.ra_deg is None,
    )

    merged_rows = []
    transcript_sources = []
    for tap_row in tap_rows:
        source_id = str(tap_row["source_id"])
        spectrum_row = spectrum_rows[source_id]
        star = build_generator_row(tap_row, spectrum_row)
        merged_rows.append(star)
        transcript_sources.append(build_transcript_source(tap_row, spectrum_row, star))

    input_jsonl = args.output_dir / f"{safe_file_prefix(args.artifact_prefix)}_spcc_sources.jsonl"
    with input_jsonl.open("w", encoding="utf-8") as handle:
        for row in merged_rows:
            handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")

    transcript = {
        "schema_version": "spcc-gaia-validation-transcript-v1",
        "generated_at_utc": spcc.utc_now_iso(),
        "tool": "fetch_spcc_gaia_validation_fixture.py",
        "tool_version": TOOL_VERSION,
        "tap": {
            "service_url": TAP_URL,
            "adql": tap_adql,
            "gaia_tables": ["gaiadr3.gaia_source", "gaiadr3.xp_summary"],
            "returned_count": len(tap_rows),
        },
        "selection": build_selection_transcript(args),
        "datalink_attempts": datalink_attempts,
        "spectrum_retrieval": {
            "method": "official-gaia-cdn-bulk-xp-sampled",
            "note": "Used the official Gaia DR3 xp_sampled_mean_spectrum bulk product. The Gaia DataLink data-server probe is recorded separately when --probe-datalink is used.",
            "files": spectrum_files,
        },
        "input_jsonl": input_jsonl.name,
        "input_jsonl_sha256": spcc.sha256_file(input_jsonl),
        "sources": transcript_sources,
    }
    transcript_path = args.output_dir / "gaia_validation_transcript.json"
    spcc.write_json(transcript_path, transcript)

    print(f"Wrote {input_jsonl}")
    print(f"Wrote {transcript_path}")


def validate_selection_args(args: argparse.Namespace) -> None:
    if args.source_count <= 0:
        raise ValueError("--source-count must be positive")
    region_values = [args.ra_deg, args.dec_deg, args.radius_deg]
    if any(value is not None for value in region_values) and not all(value is not None for value in region_values):
        raise ValueError("--ra-deg, --dec-deg, and --radius-deg must be provided together")
    if args.ra_deg is not None and not (0.0 <= args.ra_deg < 360.0):
        raise ValueError("--ra-deg must be in [0, 360)")
    if args.dec_deg is not None and not (-90.0 <= args.dec_deg <= 90.0):
        raise ValueError("--dec-deg must be in [-90, 90]")
    if args.radius_deg is not None and args.radius_deg <= 0.0:
        raise ValueError("--radius-deg must be positive")
    if args.mag_min >= args.mag_max:
        raise ValueError("--mag-min must be less than --mag-max")
    if args.min_bp_rp_snr <= 0.0:
        raise ValueError("--min-bp-rp-snr must be positive")


def query_tap_sources(args: argparse.Namespace) -> tuple[list[dict[str, str]], str]:
    adql = build_tap_adql(args)
    body = urllib.parse.urlencode({"REQUEST": "doQuery", "LANG": "ADQL", "FORMAT": "csv", "QUERY": adql}).encode()
    request = urllib.request.Request(
        TAP_URL + "/sync",
        data=body,
        headers={"User-Agent": "AfterNight SPCC Gaia validation"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=args.timeout_seconds) as response:
        text = response.read().decode("utf-8")
    rows = list(csv.DictReader(text.splitlines()))
    if len(rows) != args.source_count:
        raise ValueError(f"Gaia TAP returned {len(rows)} rows, expected {args.source_count}")
    return rows, adql


def build_tap_adql(args: argparse.Namespace) -> str:
    where_clauses = [
        "s.has_xp_sampled = 'true'",
        f"s.phot_g_mean_mag BETWEEN {args.mag_min:g} AND {args.mag_max:g}",
        f"s.phot_bp_mean_flux_over_error > {args.min_bp_rp_snr:g}",
        f"s.phot_rp_mean_flux_over_error > {args.min_bp_rp_snr:g}",
    ]
    order_by = "s.source_id ASC"
    if args.ra_deg is None:
        where_clauses.insert(1, f"s.source_id < {FIRST_XP_BULK_FILE_UPPER_SOURCE_ID}")
    else:
        where_clauses.append(
            "CONTAINS(POINT('ICRS', s.ra, s.dec), "
            f"CIRCLE('ICRS', {args.ra_deg:.8f}, {args.dec_deg:.8f}, {args.radius_deg:.8f})) = 1"
        )
        order_by = "s.phot_g_mean_mag ASC"

    where_text = "\n  AND ".join(where_clauses)
    return f"""
SELECT TOP {args.source_count}
  s.source_id, s.ra, s.dec, s.ref_epoch, s.pmra, s.pmdec,
  s.phot_g_mean_mag, s.phot_bp_mean_mag, s.phot_rp_mean_mag,
  s.phot_g_mean_flux_over_error, s.phot_bp_mean_flux_over_error, s.phot_rp_mean_flux_over_error,
  s.phot_bp_rp_excess_factor, s.phot_bp_n_contaminated_transits, s.phot_bp_n_blended_transits,
  s.phot_rp_n_contaminated_transits, s.phot_rp_n_blended_transits,
  s.ruwe, s.duplicated_source, s.non_single_star, s.phot_variable_flag, s.ipd_frac_multi_peak, s.has_xp_sampled,
  x.bp_n_relevant_bases, x.bp_n_measurements, x.bp_n_rejected_measurements, x.bp_chi_squared,
  x.bp_n_transits, x.bp_n_contaminated_transits, x.bp_n_blended_transits,
  x.rp_n_relevant_bases, x.rp_n_measurements, x.rp_n_rejected_measurements, x.rp_chi_squared,
  x.rp_n_transits, x.rp_n_contaminated_transits, x.rp_n_blended_transits
FROM gaiadr3.gaia_source AS s
JOIN gaiadr3.xp_summary AS x ON s.source_id = x.source_id
WHERE {where_text}
ORDER BY {order_by}
""".strip()


def build_selection_transcript(args: argparse.Namespace) -> dict[str, Any]:
    selection: dict[str, Any] = {
        "source_count": args.source_count,
        "phot_g_mean_mag_min": args.mag_min,
        "phot_g_mean_mag_max": args.mag_max,
        "min_bp_rp_flux_over_error": args.min_bp_rp_snr,
        "artifact_prefix": safe_file_prefix(args.artifact_prefix),
    }
    if args.ra_deg is None:
        selection["mode"] = "tiny-first-bulk-file-validation"
        selection["source_id_upper_bound"] = FIRST_XP_BULK_FILE_UPPER_SOURCE_ID
    else:
        selection["mode"] = "regional-cache-validation"
        selection["region"] = {
            "name": args.region_name or "",
            "ra_deg": args.ra_deg,
            "dec_deg": args.dec_deg,
            "radius_deg": args.radius_deg,
        }
        selection["allow_multiple_bulk_files"] = args.allow_multiple_bulk_files
    return selection


def safe_file_prefix(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return cleaned.strip("._-") or "spcc_gaia"


def probe_datalink(source_ids: list[str], timeout_seconds: float) -> list[dict[str, Any]]:
    attempts = []
    for source_id in source_ids:
        url = DATA_SERVER_URL + "?" + urllib.parse.urlencode(
            {
                "ID": source_id,
                "RELEASE": "Gaia DR3",
                "RETRIEVAL_TYPE": "XP_SAMPLED",
                "FORMAT": "csv",
            }
        )
        attempt: dict[str, Any] = {"source_id": source_id, "url": url}
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "AfterNight SPCC Gaia validation"})
            with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
                payload = response.read(4096)
            attempt["status_code"] = 200
            attempt["payload_prefix_sha256"] = spcc.sha256_bytes(payload)
        except urllib.error.HTTPError as exc:
            attempt["status_code"] = exc.code
            attempt["error_reason"] = exc.reason
        except urllib.error.URLError as exc:
            attempt["status_code"] = None
            attempt["error_reason"] = str(exc.reason)
        attempts.append(attempt)
    return attempts


def fetch_spectrum_rows(
    source_ids: list[str], timeout_seconds: float, *, allow_multiple_bulk_files: bool = True
) -> tuple[dict[str, dict[str, str]], list[dict[str, Any]]]:
    wanted = set(source_ids)
    file_entries = list_xp_sampled_files(timeout_seconds)
    source_to_file: dict[str, dict[str, Any]] = {}
    for source_id in source_ids:
        hp8 = gaia_source_id_to_healpix8(int(source_id))
        for entry in file_entries:
            if entry["healpix_start"] <= hp8 <= entry["healpix_end"]:
                source_to_file[source_id] = entry
                break
        if source_id not in source_to_file:
            raise ValueError(f"Could not find a Gaia XP sampled bulk file for source {source_id}")

    unique_entries = unique_file_entries(source_to_file.values())
    if len(unique_entries) > 1 and not allow_multiple_bulk_files:
        keys = ", ".join(entry["key"] for entry in unique_entries[:5])
        raise ValueError(
            "Regional sample spans multiple Gaia XP sampled bulk files; pass --allow-multiple-bulk-files "
            f"to continue. Files: {keys}"
        )

    rows: dict[str, dict[str, str]] = {}
    used_files = []
    for entry in unique_entries:
        public_url = CDN_PUBLIC_BASE_URL + entry["key"]
        used_files.append(
            {
                "url": public_url,
                "key": entry["key"],
                "size_bytes": entry["size_bytes"],
                "last_modified": entry["last_modified"],
                "healpix_start": entry["healpix_start"],
                "healpix_end": entry["healpix_end"],
            }
        )
        request = urllib.request.Request(public_url, headers={"User-Agent": "AfterNight SPCC Gaia validation"})
        with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
            with gzip.GzipFile(fileobj=response) as gz:
                reader = csv.DictReader(non_comment_lines(gz))
                for row in reader:
                    source_id = row.get("source_id")
                    if source_id in wanted:
                        rows[source_id] = row
                        if set(rows) >= wanted:
                            return rows, used_files
    missing = wanted - set(rows)
    if missing:
        raise ValueError(f"Missing Gaia XP sampled rows for sources: {sorted(missing)}")
    return rows, used_files


def list_xp_sampled_files(timeout_seconds: float) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    marker = None
    while True:
        params = {"prefix": XP_SAMPLED_PREFIX, "delimiter": "/"}
        if marker:
            params["marker"] = marker
        url = CDN_LIST_URL + "?" + urllib.parse.urlencode(params)
        with urllib.request.urlopen(url, timeout=timeout_seconds) as response:
            text = response.read().decode("utf-8")
        entries.extend(parse_s3_contents(text))
        marker_match = re.search(r"<NextMarker>(.*?)</NextMarker>", text)
        if not marker_match:
            break
        marker = marker_match.group(1)
    return entries


def parse_s3_contents(xml_text: str) -> list[dict[str, Any]]:
    entries = []
    for content in re.findall(r"<Contents>(.*?)</Contents>", xml_text, flags=re.DOTALL):
        key = extract_xml_text(content, "Key")
        match = re.search(r"XpSampledMeanSpectrum_(\d+)-(\d+)\.csv\.gz$", key)
        if match is None:
            continue
        entries.append(
            {
                "key": key,
                "healpix_start": int(match.group(1)),
                "healpix_end": int(match.group(2)),
                "size_bytes": int(extract_xml_text(content, "Size")),
                "last_modified": extract_xml_text(content, "LastModified"),
            }
        )
    return entries


def extract_xml_text(xml_text: str, tag: str) -> str:
    match = re.search(rf"<{tag}>(.*?)</{tag}>", xml_text, flags=re.DOTALL)
    if match is None:
        return ""
    return match.group(1)


def gaia_source_id_to_healpix8(source_id: int) -> int:
    healpix_order12 = source_id // (2 ** 35)
    return healpix_order12 // (4 ** 4)


def unique_file_entries(entries: Any) -> list[dict[str, Any]]:
    unique: dict[str, dict[str, Any]] = {}
    for entry in entries:
        unique[entry["key"]] = entry
    return list(unique.values())


def non_comment_lines(gz: gzip.GzipFile) -> Any:
    for raw in gz:
        line = raw.decode("utf-8")
        if not line.startswith("#"):
            yield line


def build_generator_row(tap_row: dict[str, str], spectrum_row: dict[str, str]) -> dict[str, Any]:
    return {
        "source_id": int(tap_row["source_id"]),
        "solution_id": spectrum_row["solution_id"],
        "ra": float(tap_row["ra"]),
        "dec": float(tap_row["dec"]),
        "ref_epoch": float(tap_row["ref_epoch"]),
        "pmra": float_or_zero(tap_row["pmra"]),
        "pmdec": float_or_zero(tap_row["pmdec"]),
        "phot_g_mean_mag": float(tap_row["phot_g_mean_mag"]),
        "phot_bp_mean_mag": float(tap_row["phot_bp_mean_mag"]),
        "phot_rp_mean_mag": float(tap_row["phot_rp_mean_mag"]),
        "flux": parse_ecsv_array(spectrum_row["flux"]),
        "flux_error": parse_ecsv_array(spectrum_row["flux_error"]),
        "quality_flags": 0,
        "spectrum_source_kind": 0,
    }


def build_transcript_source(tap_row: dict[str, str], spectrum_row: dict[str, str], star: dict[str, Any]) -> dict[str, Any]:
    flux = star["flux"]
    flux_error = star["flux_error"]
    packed_record = spcc.pack_record(star)
    return {
        "source_id": str(star["source_id"]),
        "tap_quality_fields": {
            key: tap_row.get(key)
            for key in [
                "has_xp_sampled",
                "phot_g_mean_flux_over_error",
                "phot_bp_mean_flux_over_error",
                "phot_rp_mean_flux_over_error",
                "phot_bp_rp_excess_factor",
                "ruwe",
                "duplicated_source",
                "non_single_star",
                "phot_variable_flag",
                "ipd_frac_multi_peak",
                "bp_n_relevant_bases",
                "bp_n_measurements",
                "bp_n_rejected_measurements",
                "bp_chi_squared",
                "rp_n_relevant_bases",
                "rp_n_measurements",
                "rp_n_rejected_measurements",
                "rp_chi_squared",
            ]
        },
        "xp_sampled_solution_id": spectrum_row.get("solution_id"),
        "flux_sample_count": len(flux),
        "flux_error_sample_count": len(flux_error),
        "flux_sha256": spcc.sha256_bytes(spcc.canonical_json_bytes(flux)),
        "flux_error_sha256": spcc.sha256_bytes(spcc.canonical_json_bytes(flux_error)),
        "packed_record_sha256": spcc.sha256_bytes(packed_record),
    }


def parse_ecsv_array(value: str) -> list[float]:
    parsed = json.loads(value.replace("'", '"'))
    if len(parsed) != spcc.SAMPLED_XP_SAMPLE_COUNT:
        raise ValueError(f"XP sampled array has {len(parsed)} samples, expected {spcc.SAMPLED_XP_SAMPLE_COUNT}")
    return [float(item) for item in parsed]


def float_or_zero(value: str) -> float:
    if value == "":
        return 0.0
    return float(value)


if __name__ == "__main__":
    raise SystemExit(main())
