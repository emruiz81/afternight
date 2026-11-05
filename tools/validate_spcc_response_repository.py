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


PRODUCTION_REVIEW_STATUSES = {"approved-for-bundling", "approved-for-managed-download"}
PRODUCTION_ASSET_METADATA_FIELDS = [
    "license_name",
    "source_url",
    "source_citation",
    "attribution_text",
    "transformation_notes",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate an AfterNight SPCC response repository.")
    parser.add_argument("repository_dir", type=Path)
    parser.add_argument(
        "--runtime-root",
        action="store_true",
        help="Validate as a deployed/runtime path: all referenced files must be present under this root.",
    )
    parser.add_argument("--strict-production-review", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    try:
        validate_repository(args, errors)
    except Exception as exc:
        errors.append(str(exc))

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"SPCC response repository validation passed: {args.repository_dir}")
    return 0


def validate_repository(args: argparse.Namespace, errors: list[str]) -> None:
    repo_dir: Path = args.repository_dir
    meta_path = repo_dir / "response_meta.json"
    if not meta_path.exists():
        errors.append("missing response_meta.json")
        return
    meta = spcc.read_json(meta_path)

    for key in ["schema_version", "repository_id", "version", "components", "white_references", "throughput_profiles"]:
        if key not in meta:
            errors.append(f"response_meta.json missing required field: {key}")
    if errors:
        return

    if args.strict_production_review and meta.get("rights", {}).get("review_status") not in PRODUCTION_REVIEW_STATUSES:
        errors.append("strict production validation requires approved response repository review")
    if args.strict_production_review:
        rights = meta.get("rights", {})
        if not non_empty_text(rights.get("license_name")):
            errors.append("strict production validation requires rights.license_name")

    component_ids = validate_assets(
        repo_dir,
        meta.get("components", []),
        id_key="component_id",
        asset_kind="component",
        strict_production_review=args.strict_production_review,
        errors=errors,
    )
    white_reference_ids = validate_assets(
        repo_dir,
        meta.get("white_references", []),
        id_key="white_reference_id",
        asset_kind="white_reference",
        strict_production_review=args.strict_production_review,
        errors=errors,
    )

    for profile in meta.get("throughput_profiles", []):
        profile_id = profile.get("profile_id", "<unknown>")
        for component_id in profile.get("component_ids", []):
            if component_id not in component_ids:
                errors.append(f"throughput profile {profile_id} references missing component {component_id}")
        white_reference_id = profile.get("white_reference_id")
        if white_reference_id not in white_reference_ids:
            errors.append(f"throughput profile {profile_id} references missing white reference {white_reference_id}")
        if not profile.get("compatible_channel_bases"):
            errors.append(f"throughput profile {profile_id} has no compatible channel bases")

    if args.runtime_root:
        scopes = set(meta.get("deployment_scopes", []))
        if "deployed-runtime" not in scopes:
            errors.append("runtime validation requires deployed-runtime in deployment_scopes")


def validate_assets(
    repo_dir: Path,
    assets: list[dict[str, Any]],
    *,
    id_key: str,
    asset_kind: str,
    strict_production_review: bool,
    errors: list[str],
) -> set[str]:
    ids: set[str] = set()
    for asset in assets:
        asset_id = asset.get(id_key)
        if not asset_id:
            errors.append(f"response asset missing {id_key}")
            continue
        ids.add(asset_id)
        relative_file = asset.get("file")
        if not relative_file:
            errors.append(f"response asset {asset_id} missing file")
            continue
        path = repo_dir / relative_file
        if not path.exists():
            errors.append(f"response asset {asset_id} file is missing: {relative_file}")
            continue
        actual_file_sha = spcc.sha256_file(path)
        if actual_file_sha != asset.get("file_sha256"):
            errors.append(f"response asset {asset_id} file checksum mismatch")
            continue

        payload = spcc.read_json(path)
        samples = payload.get("samples", [])
        try:
            spcc.validate_curve_samples(samples, path)
        except Exception as exc:
            errors.append(str(exc))
            continue
        actual_data_sha = spcc.curve_data_sha256(samples)
        if actual_data_sha != asset.get("data_sha256") or actual_data_sha != payload.get("data_sha256"):
            errors.append(f"response asset {asset_id} data checksum mismatch")
        if payload.get("wavelength_unit") != "nm":
            errors.append(f"response asset {asset_id} wavelength_unit must be nm")
        if not payload.get("value_semantics"):
            errors.append(f"response asset {asset_id} value_semantics is required")
        if (
            asset_kind == "white_reference"
            and payload.get("value_semantics") == "relative-photon-count"
            and payload.get("photon_count_weighted") is not True
        ):
            errors.append(f"response white reference {asset_id} must declare photon_count_weighted=true")
        if strict_production_review and payload.get("review_status") not in PRODUCTION_REVIEW_STATUSES:
            errors.append(f"response asset {asset_id} lacks approved production review")
        if strict_production_review:
            for field in PRODUCTION_ASSET_METADATA_FIELDS:
                if not non_empty_text(payload.get(field)):
                    errors.append(f"response asset {asset_id} missing production metadata field: {field}")
    return ids


def non_empty_text(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


if __name__ == "__main__":
    raise SystemExit(main())
