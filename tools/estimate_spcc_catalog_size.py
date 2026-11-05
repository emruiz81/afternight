#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import csv
import io
import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


DEFAULT_TAP_URL = "https://gea.esac.esa.int/tap-server/tap"
TOOL_VERSION = "spcc-size-estimator-phase3-v1"
PRESETS = {
    "sampled-full": {
        "source_family": "spcc_gaia_dr3_xp_sampled",
        "selection": "s.has_xp_sampled = 'true'",
        "spectrum_table": "gaiadr3.xp_sampled_mean_spectrum",
    },
    "continuous-g16": {
        "source_family": "spcc_gaia_dr3_xp_continuous_g16_experimental",
        "selection": "s.has_xp_continuous = 'true' AND s.phot_g_mean_mag <= 16.0",
        "spectrum_table": "gaiadr3.xp_continuous_mean_spectrum",
    },
    "continuous-g17": {
        "source_family": "spcc_gaia_dr3_xp_continuous_g17_experimental",
        "selection": "s.has_xp_continuous = 'true' AND s.phot_g_mean_mag <= 17.0",
        "spectrum_table": "gaiadr3.xp_continuous_mean_spectrum",
    },
    "continuous-g17p65": {
        "source_family": "spcc_gaia_dr3_xp_continuous_g17p65_experimental",
        "selection": "s.has_xp_continuous = 'true' AND s.phot_g_mean_mag <= 17.65",
        "spectrum_table": "gaiadr3.xp_continuous_mean_spectrum",
    },
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Estimate SPCC Gaia XP catalog sizes and write a reproducible TAP-count transcript."
    )
    parser.add_argument("--preset", action="append", choices=sorted(PRESETS), help="Preset to estimate.")
    parser.add_argument("--counts-fixture", type=Path, help="JSON mapping preset IDs to count values.")
    parser.add_argument("--tap-url", default=DEFAULT_TAP_URL)
    parser.add_argument("--tap-official", action="store_true", help="Run live TAP count queries.")
    parser.add_argument("--record-size", type=int, default=spcc.ANXP_RECORD_SIZE_BYTES)
    parser.add_argument("--output-transcript", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        transcript = estimate(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    spcc.write_json(args.output_transcript, transcript)
    print_table(transcript)
    return 0


def estimate(args: argparse.Namespace) -> dict[str, Any]:
    presets = args.preset or ["sampled-full", "continuous-g16"]
    if args.counts_fixture is None and not args.tap_official:
        raise ValueError("Use --counts-fixture for deterministic estimates or --tap-official for live TAP counts")

    fixture_counts = spcc.read_json(args.counts_fixture) if args.counts_fixture is not None else {}
    service_url = "fixture://spcc-counts" if args.counts_fixture is not None else args.tap_url

    queries = []
    for preset in presets:
        adql = build_count_adql(preset)
        count = count_from_fixture(fixture_counts, preset)
        if count is None:
            count = run_tap_count(args.tap_url, adql, args.timeout_seconds)
        raw_size = count * args.record_size
        queries.append(
            {
                "preset": preset,
                "source_family": PRESETS[preset]["source_family"],
                "service_url": service_url,
                "query_timestamp_utc": spcc.utc_now_iso(),
                "gaia_release": "Gaia DR3",
                "gaia_tables": [
                    "gaiadr3.gaia_source",
                    "gaiadr3.xp_summary",
                    PRESETS[preset]["spectrum_table"],
                ],
                "selected_columns": ["COUNT(*) AS source_count"],
                "adql": adql,
                "returned_count": count,
                "record_size_bytes": args.record_size,
                "estimated_raw_size_bytes": raw_size,
                "estimated_raw_size_gib": raw_size / float(1024 ** 3),
            }
        )

    return {
        "tool": "estimate_spcc_catalog_size.py",
        "tool_version": TOOL_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "service_url": service_url,
        "record_size_bytes": args.record_size,
        "queries": queries,
    }


def build_count_adql(preset: str) -> str:
    selection = PRESETS[preset]["selection"]
    return (
        "SELECT COUNT(*) AS source_count\n"
        "FROM gaiadr3.gaia_source AS s\n"
        "LEFT JOIN gaiadr3.xp_summary AS xp ON s.source_id = xp.source_id\n"
        f"WHERE {selection}"
    )


def count_from_fixture(fixture_counts: dict[str, Any], preset: str) -> int | None:
    if preset not in fixture_counts:
        return None
    value = fixture_counts[preset]
    if isinstance(value, dict):
        value = value.get("count")
    if value is None:
        return None
    return int(value)


def run_tap_count(tap_url: str, adql: str, timeout_seconds: float) -> int:
    url = tap_url.rstrip("/") + "/sync"
    body = urllib.parse.urlencode(
        {
            "REQUEST": "doQuery",
            "LANG": "ADQL",
            "FORMAT": "csv",
            "QUERY": adql,
        }
    ).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        headers={"User-Agent": "AfterNight SPCC Catalog Size Estimator"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        text = response.read().decode("utf-8")
    reader = csv.DictReader(io.StringIO(text))
    row = next(reader)
    for key in ("source_count", "count", "COUNT"):
        if key in row and row[key] != "":
            return int(float(row[key]))
    first_value = next(iter(row.values()))
    return int(float(first_value))


def print_table(transcript: dict[str, Any]) -> None:
    print("Preset                         Count          Raw GiB")
    print("-----------------------------  -------------  --------")
    for query in transcript["queries"]:
        print(
            f"{query['preset']:<29}  {query['returned_count']:>13,d}  "
            f"{query['estimated_raw_size_gib']:>8.2f}"
        )


if __name__ == "__main__":
    raise SystemExit(main())
