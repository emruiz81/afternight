#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


GAIA_DR3_CREDIT_URL = (
    "https://gea.esac.esa.int/archive/documentation/GDR3/"
    "Miscellaneous/sec_credit_and_citation_instructions/"
)
GAIA_DR3_TERMS_URLS = [
    GAIA_DR3_CREDIT_URL,
    "https://gea.esac.esa.int/archive/documentation/GDR3/",
    "https://www.cosmos.esa.int/web/gaia/dr3",
    "https://huggingface.co/terms-of-service",
]
GAIA_DR3_REQUIRED_CREDIT = ["ESA/Gaia/DPAC"]
GAIA_DR3_REQUIRED_CITATIONS = [
    "Gaia Collaboration et al. 2016, A&A, 595, A1",
    "Gaia Collaboration et al. 2023, A&A, 674, A1",
    "Gaia DR3 product-specific documentation and citations required by ESA/Gaia/DPAC",
]
GAIA_DR3_DEFAULT_REVIEW_BASIS = (
    "Gaia DR3 data are open and free to use with ESA/Gaia/DPAC credit and the required Gaia mission, "
    "Gaia DR3 release, and product-specific citations. This AfterNight SPCC catalog is a derived, "
    "checksummed Gaia DR3 XP sampled catalog staged for managed download with those attribution and citation "
    "requirements preserved in the catalog and dataset manifests."
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Record reviewed redistribution and mirror-hosting approval in an AfterNight SPCC catalog_meta.json."
        )
    )
    parser.add_argument("catalog_dir", type=Path)
    parser.add_argument(
        "--review-status",
        choices=sorted(spcc.APPROVED_CATALOG_RIGHTS_STATUSES),
        default="approved-for-managed-download",
    )
    parser.add_argument("--reviewer", required=True)
    parser.add_argument("--reviewed-at-utc", default=spcc.utc_now_iso())
    parser.add_argument("--review-basis", default=GAIA_DR3_DEFAULT_REVIEW_BASIS)
    parser.add_argument(
        "--mirror-hosting-policy",
        choices=["approved-public", "blocked", "staging-only"],
        default="approved-public",
    )
    parser.add_argument(
        "--redistribution-policy",
        default="allowed-with-gaia-credit-and-required-citations",
    )
    parser.add_argument(
        "--derivative-data-policy",
        default="derived-catalog-redistribution-with-gaia-credit-and-required-citations",
    )
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        approve_catalog(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def approve_catalog(args: argparse.Namespace) -> None:
    catalog_dir: Path = args.catalog_dir
    meta_path = catalog_dir / "catalog_meta.json"
    if not meta_path.exists():
        raise ValueError(f"missing catalog metadata: {meta_path}")

    meta = spcc.read_json(meta_path)
    if meta.get("catalog_family_id") != spcc.CATALOG_FAMILY_ID:
        raise ValueError(f"{meta_path} is not an AfterNight SPCC Gaia DR3 XP sampled catalog")
    if not meta.get("manifest_id"):
        raise ValueError("catalog_meta.json is missing manifest_id")

    rights = dict(meta.get("rights") or {})
    source_kind = str(meta.get("source_kind", ""))
    if source_kind != "synthetic_fixture":
        coverage_errors = spcc.local_full_tile_coverage_errors(meta)
        if coverage_errors:
            joined = "\n  - ".join(coverage_errors)
            raise ValueError(f"local-full SPCC catalog is not ready for managed download approval:\n  - {joined}")
    if source_kind == "synthetic_fixture":
        source_terms_urls = ["synthetic://after-night/spcc-fixture"]
        license_name = "synthetic-fixture"
        license_url = "synthetic://after-night/spcc-fixture"
        required_credit = ["AfterNight synthetic SPCC fixture"]
        required_citations = ["Synthetic deterministic fixture; no external Gaia data."]
        notes = "Synthetic SPCC fixture reviewed for managed test distribution."
    else:
        source_terms_urls = unique_preserving_order([*rights.get("source_terms_urls", []), *GAIA_DR3_TERMS_URLS])
        license_name = "Gaia DR3 open data with ESA/Gaia/DPAC attribution"
        license_url = GAIA_DR3_CREDIT_URL
        required_credit = GAIA_DR3_REQUIRED_CREDIT
        required_citations = GAIA_DR3_REQUIRED_CITATIONS
        notes = (
            "Gaia-derived SPCC catalog approved for managed AfterNight download. Gaia DR3 attribution and "
            "citation requirements must remain visible in dataset cards, diagnostics, and release evidence. "
            "Do not describe Gaia DR3 data as public domain."
        )

    rights.update(
        {
            "review_status": args.review_status,
            "reviewed_at_utc": args.reviewed_at_utc,
            "reviewed_by": args.reviewer,
            "review_basis": args.review_basis,
            "source_terms_urls": source_terms_urls,
            "license_name": license_name,
            "license_url": license_url,
            "required_credit": required_credit,
            "required_citations": required_citations,
            "redistribution_policy": args.redistribution_policy,
            "derivative_data_policy": args.derivative_data_policy,
            "mirror_hosting_policy": args.mirror_hosting_policy,
            "notes": notes,
        }
    )
    meta["rights"] = rights

    provenance = dict(meta.get("provenance") or {})
    provenance["rights_review_tool"] = "approve_spcc_catalog_rights.py"
    provenance["rights_reviewed_at_utc"] = args.reviewed_at_utc
    provenance["source_terms_urls"] = unique_preserving_order(
        [*provenance.get("source_terms_urls", []), *source_terms_urls]
    )
    meta["provenance"] = provenance

    if not args.dry_run:
        spcc.write_json(meta_path, meta)

    action = "would record" if args.dry_run else "recorded"
    print(
        f"SPCC catalog rights approval {action}: {meta_path}\n"
        f"  manifest_id: {meta['manifest_id']}\n"
        f"  review_status: {rights['review_status']}\n"
        f"  mirror_hosting_policy: {rights['mirror_hosting_policy']}\n"
        f"  reviewed_by: {rights['reviewed_by']}\n"
        f"  reviewed_at_utc: {rights['reviewed_at_utc']}",
        flush=True,
    )


def unique_preserving_order(values: list[Any]) -> list[str]:
    seen: set[str] = set()
    unique: list[str] = []
    for value in values:
        item = str(value)
        if item and item not in seen:
            seen.add(item)
            unique.append(item)
    return unique


if __name__ == "__main__":
    raise SystemExit(main())
