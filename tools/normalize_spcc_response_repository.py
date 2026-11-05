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
from tools import validate_spcc_response_source_manifest


TOOL_VERSION = "spcc-response-normalizer-phase3-v1"
ASSET_PROVENANCE_FIELDS = [
    "license_url",
    "source_citation",
    "attribution_text",
    "source_acquired_at_utc",
    "reviewer",
    "reviewed_at_utc",
    "redistribution_permission",
    "derivative_permission",
    "transformation_notes",
]
ASSET_OPTIONAL_FIELDS = [
    "channel",
    "source_upstream_type",
    "source_upstream_model",
    "source_upstream_name",
    "source_upstream_manufacturer",
    "source_upstream_version",
    "source_upstream_data_quality_marker",
    "source_upstream_data_source",
    "source_upstream_path",
    "photon_count_weighted",
    "white_reference_normalization_wavelength_nm",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Normalize licensed SPCC response curves and white references into an AfterNight response repository."
    )
    parser.add_argument("input_manifest", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--strict-production-review",
        action="store_true",
        help="Require production-approved source-manifest provenance before normalizing.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        normalize_repository(args.input_manifest, args.output_dir, args.strict_production_review)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def normalize_repository(input_manifest: Path, output_dir: Path, strict_production_review: bool = False) -> None:
    errors: list[str] = []
    validate_spcc_response_source_manifest.validate_source_manifest(input_manifest, strict_production_review, errors)
    if errors:
        joined = "\n  - ".join(errors)
        raise ValueError(f"SPCC response source manifest validation failed:\n  - {joined}")

    source = spcc.read_json(input_manifest)
    source_root = input_manifest.parent
    output_dir.mkdir(parents=True, exist_ok=True)
    components_dir = output_dir / "components"
    white_dir = output_dir / "white_references"
    components_dir.mkdir(parents=True, exist_ok=True)
    white_dir.mkdir(parents=True, exist_ok=True)

    normalized_components = [
        normalize_asset(
            asset,
            source_root=source_root,
            output_dir=components_dir,
            id_key="component_id",
            asset_kind="component",
        )
        for asset in source.get("components", [])
    ]
    normalized_white_references = [
        normalize_asset(
            asset,
            source_root=source_root,
            output_dir=white_dir,
            id_key="white_reference_id",
            asset_kind="white_reference",
        )
        for asset in source.get("white_references", [])
    ]

    component_ids = {asset["component_id"] for asset in normalized_components}
    white_ids = {asset["white_reference_id"] for asset in normalized_white_references}
    profiles = []
    for profile in source.get("throughput_profiles", []):
        missing_components = [component for component in profile.get("component_ids", []) if component not in component_ids]
        if missing_components:
            raise ValueError(f"throughput profile {profile.get('profile_id')} references missing components")
        if profile.get("white_reference_id") not in white_ids:
            raise ValueError(f"throughput profile {profile.get('profile_id')} references a missing white reference")
        profiles.append(
            {
                "profile_id": profile["profile_id"],
                "version": str(profile.get("version", "1")),
                "component_ids": list(profile.get("component_ids", [])),
                "white_reference_id": profile["white_reference_id"],
                "compatible_channel_bases": list(profile.get("compatible_channel_bases", [])),
                "models_raw_white_balance": bool(profile.get("models_raw_white_balance", False)),
                "models_camera_matrix": bool(profile.get("models_camera_matrix", False)),
                "algorithm_version": profile.get("algorithm_version", "spcc-synthetic-photometry-v2"),
            }
        )
        if "channel_component_ids" in profile:
            profiles[-1]["channel_component_ids"] = {
                str(channel): list(component_ids)
                for channel, component_ids in profile.get("channel_component_ids", {}).items()
            }
        if "aliases" in profile:
            profiles[-1]["aliases"] = [dict(alias) for alias in profile.get("aliases", [])]

    if not normalized_components:
        raise ValueError("response repository must contain at least one component")
    if not normalized_white_references:
        raise ValueError("response repository must contain at least one white reference")
    if not profiles:
        raise ValueError("response repository must contain at least one throughput profile")

    response_meta = {
        "schema_version": "spcc-response-repository-v1",
        "repository_id": source["repository_id"],
        "version": str(source.get("version", "1")),
        "rights": source.get("rights", {}),
        "generated_at_utc": spcc.utc_now_iso(),
        "generator": "normalize_spcc_response_repository.py",
        "generator_version": TOOL_VERSION,
        "components": normalized_components,
        "white_references": normalized_white_references,
        "throughput_profiles": profiles,
        "deployment_scopes": [
            "source-tree",
            "deployed-runtime",
            "installer",
            "appimage",
            "user-private",
            "managed-cache",
        ],
    }
    spcc.write_json(output_dir / "response_meta.json", response_meta)


def normalize_asset(
    asset: dict[str, Any], *, source_root: Path, output_dir: Path, id_key: str, asset_kind: str
) -> dict[str, Any]:
    asset_id = asset[id_key]
    curve_path = source_root / asset["curve_csv"]
    samples = spcc.read_curve_csv(curve_path)
    data_sha256 = spcc.curve_data_sha256(samples)
    output_file = output_dir / f"{sanitize_id(asset_id)}.json"
    asset_payload = {
        "schema_version": "spcc-response-asset-v1",
        "asset_kind": asset_kind,
        id_key: asset_id,
        "version": str(asset.get("version", "1")),
        "role": asset.get("role", asset_kind),
        "wavelength_unit": asset.get("wavelength_unit", "nm"),
        "value_semantics": asset.get("value_semantics", "dimensionless-throughput"),
        "interpolation": asset.get("interpolation", "linear"),
        "out_of_range": asset.get("out_of_range", "zero"),
        "normalization": asset.get("normalization", "preserve-relative-shape"),
        "review_status": asset.get("review_status", "private-local"),
        "license_name": asset.get("license_name", ""),
        "source_url": asset.get("source_url", ""),
        "data_sha256": data_sha256,
        "samples": samples,
    }
    for key in ASSET_PROVENANCE_FIELDS:
        if key in asset:
            asset_payload[key] = asset[key]
    for key in ASSET_OPTIONAL_FIELDS:
        if key in asset:
            asset_payload[key] = asset[key]
    spcc.write_json(output_file, asset_payload)
    asset_summary = {
        id_key: asset_id,
        "version": str(asset.get("version", "1")),
        "role": asset_payload["role"],
        "file": output_file.relative_to(output_dir.parents[0]).as_posix(),
        "wavelength_unit": asset_payload["wavelength_unit"],
        "value_semantics": asset_payload["value_semantics"],
        "review_status": asset_payload["review_status"],
        "license_name": asset_payload["license_name"],
        "source_url": asset_payload["source_url"],
        "data_sha256": data_sha256,
        "file_sha256": spcc.sha256_file(output_file),
    }
    for key in ASSET_PROVENANCE_FIELDS:
        if key in asset_payload:
            asset_summary[key] = asset_payload[key]
    for key in ASSET_OPTIONAL_FIELDS:
        if key in asset_payload:
            asset_summary[key] = asset_payload[key]
    return asset_summary


def sanitize_id(asset_id: str) -> str:
    return "".join(ch if ch.isalnum() else "_" for ch in asset_id).strip("_")


if __name__ == "__main__":
    raise SystemExit(main())
