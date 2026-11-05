#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Generate bright_star_catalog.csv from the HYG Database archive.

Source:
- HYG Database v4.2 (astronexus) — https://www.astronexus.com/hyg
  License: CC BY-SA 4.0

Output format matches an::star_catalog CSV parser:
  id, common_name, bayer_designation, ra_deg, dec_deg, magnitude, spectral_type

This generator keeps only stars brighter than or equal to visual magnitude 6.5
and emits J2000 ICRS coordinates in degrees.
"""

from __future__ import annotations

import csv
import gzip
import hashlib
import io
import math
import sys
import urllib.request

HYG_URL = "https://www.astronexus.com/downloads/catalogs/hygdata_v42.csv.gz"
# SHA-256 of the compressed download — update if the upstream file is updated.
HYG_EXPECTED_SHA256 = None  # Set to a hex string after verifying a known-good download
MAGNITUDE_LIMIT = 6.5

GREEK_MAP = {
    "Alp": "α",
    "Bet": "β",
    "Gam": "γ",
    "Del": "δ",
    "Eps": "ε",
    "Zet": "ζ",
    "Eta": "η",
    "The": "θ",
    "Iot": "ι",
    "Kap": "κ",
    "Lam": "λ",
    "Mu": "μ",
    "Nu": "ν",
    "Xi": "ξ",
    "Omi": "ο",
    "Pi": "π",
    "Rho": "ρ",
    "Sig": "σ",
    "Tau": "τ",
    "Ups": "υ",
    "Phi": "φ",
    "Chi": "χ",
    "Psi": "ψ",
    "Ome": "ω",
}


def parse_float(value: str) -> float | None:
    value = value.strip()
    if not value:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def build_star_id(row: dict[str, str]) -> str:
    if row["hip"].strip():
        return f"HIP {row['hip'].strip()}"
    if row["hr"].strip():
        return f"HR {row['hr'].strip()}"
    if row["hd"].strip():
        return f"HD {row['hd'].strip()}"
    return f"HYG {row['id'].strip()}"


def build_designation(row: dict[str, str]) -> str:
    bayer = row["bayer"].strip()
    con = row["con"].strip()
    if bayer and con:
        prefix = bayer[:3]
        suffix = bayer[3:]
        greek = GREEK_MAP.get(prefix, bayer)
        if greek != bayer:
            return f"{greek}{suffix} {con}"
        return f"{bayer} {con}"

    flam = row["flam"].strip()
    if flam and con:
        return f"{flam} {con}"

    return ""


def load_catalog() -> list[dict[str, object]]:
    with urllib.request.urlopen(HYG_URL) as response:
        payload = response.read()

    if HYG_EXPECTED_SHA256 is not None:
        actual_hash = hashlib.sha256(payload).hexdigest()
        if actual_hash != HYG_EXPECTED_SHA256:
            print(
                f"ERROR: SHA-256 mismatch for {HYG_URL}\n"
                f"  expected: {HYG_EXPECTED_SHA256}\n"
                f"  actual:   {actual_hash}",
                file=sys.stderr,
            )
            raise SystemExit(1)
    else:
        print(
            f"Warning: no expected SHA-256 configured. "
            f"Downloaded file hash: {hashlib.sha256(payload).hexdigest()}",
            file=sys.stderr,
        )

    with gzip.GzipFile(fileobj=io.BytesIO(payload)) as compressed:
        text = compressed.read().decode("utf-8")

    stars: list[dict[str, object]] = []
    reader = csv.DictReader(io.StringIO(text))
    for row in reader:
        if row["proper"].strip() == "Sol":
            continue

        ra_hours = parse_float(row["ra"])
        dec_deg = parse_float(row["dec"])
        magnitude = parse_float(row["mag"])
        if ra_hours is None or dec_deg is None or magnitude is None:
            continue
        if not math.isfinite(ra_hours) or not math.isfinite(dec_deg) or not math.isfinite(magnitude):
            continue
        if magnitude > MAGNITUDE_LIMIT:
            continue

        stars.append(
            {
                "id": build_star_id(row),
                "common_name": row["proper"].strip(),
                "bayer_designation": build_designation(row),
                "ra_deg": ra_hours * 15.0,
                "dec_deg": dec_deg,
                "magnitude": magnitude,
                "spectral_type": row["spect"].strip(),
            }
        )

    stars.sort(key=lambda star: (star["magnitude"], star["id"]))
    return stars


def write_csv(stars: list[dict[str, object]], path: str) -> None:
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write("# Generated from the HYG Database v4.2 (astronexus)\n")
        handle.write("# Source URL: https://www.astronexus.com/hyg\n")
        handle.write("# Source license: CC BY-SA 4.0\n")
        handle.write("# Filter: visual magnitude <= 6.5; J2000 ICRS coordinates in degrees\n")

        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(
            [
                "id",
                "common_name",
                "bayer_designation",
                "ra_deg",
                "dec_deg",
                "magnitude",
                "spectral_type",
            ]
        )

        for star in stars:
            writer.writerow(
                [
                    star["id"],
                    star["common_name"],
                    star["bayer_designation"],
                    f"{star['ra_deg']:.8f}",
                    f"{star['dec_deg']:.8f}",
                    f"{star['magnitude']:.2f}",
                    star["spectral_type"],
                ]
            )


def main() -> int:
    output_path = sys.argv[1] if len(sys.argv) > 1 else "bright_star_catalog.csv"
    stars = load_catalog()
    write_csv(stars, output_path)
    print(f"Wrote {len(stars)} bright stars to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
