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
PLACEHOLDER_TOKENS = ("TODO", "<TODO", "TBD")
REQUIRED_TOP_LEVEL_FIELDS = ["repository_id", "version", "rights", "components", "white_references", "throughput_profiles"]
PRODUCTION_RIGHTS_FIELDS = [
    "license_name",
    "license_url",
    "reviewer",
    "reviewed_at_utc",
    "redistribution_permission",
    "derivative_permission",
    "required_attribution",
]
PRODUCTION_ASSET_FIELDS = [
    "role",
    "wavelength_unit",
    "value_semantics",
    "license_name",
    "license_url",
    "source_url",
    "source_citation",
    "attribution_text",
    "source_acquired_at_utc",
    "reviewer",
    "reviewed_at_utc",
    "redistribution_permission",
    "derivative_permission",
    "transformation_notes",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate an SPCC response source manifest before normalization.")
    parser.add_argument("source_manifest", type=Path)
    parser.add_argument(
        "--strict-production-review",
        action="store_true",
        help="Require production-approved review state, complete provenance metadata, and no placeholders.",
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        help="Optional path for a structured response-source readiness audit report.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    try:
        validate_source_manifest(args.source_manifest, args.strict_production_review, errors)
    except Exception as exc:
        errors.append(str(exc))

    if args.report_json is not None:
        try:
            spcc.write_json(
                args.report_json,
                build_audit_report(args.source_manifest, args.strict_production_review, errors),
            )
        except Exception as exc:
            errors.append(f"could not write audit report: {exc}")

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"SPCC response source manifest validation passed: {args.source_manifest}")
    return 0


def validate_source_manifest(source_manifest: Path, strict_production_review: bool, errors: list[str]) -> None:
    if not source_manifest.exists():
        errors.append(f"source manifest is missing: {source_manifest}")
        return

    manifest = spcc.read_json(source_manifest)
    for field in REQUIRED_TOP_LEVEL_FIELDS:
        if field not in manifest:
            errors.append(f"source manifest missing required field: {field}")
    if errors:
        return

    components = require_object_list(manifest, "components", "source manifest", errors)
    white_references = require_object_list(manifest, "white_references", "source manifest", errors)
    profiles = require_object_list(manifest, "throughput_profiles", "source manifest", errors)
    if strict_production_review:
        reject_placeholders(manifest, "source manifest", errors)
        validate_production_rights(manifest.get("rights"), errors)

    component_ids = validate_assets(
        components,
        source_manifest.parent,
        "component_id",
        "components",
        strict_production_review,
        errors,
    )
    white_reference_ids = validate_assets(
        white_references,
        source_manifest.parent,
        "white_reference_id",
        "white_references",
        strict_production_review,
        errors,
    )
    validate_profiles(profiles, component_ids, white_reference_ids, errors)


def build_audit_report(
    source_manifest: Path,
    strict_production_review: bool,
    errors: list[str],
) -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema_version": "spcc-response-source-manifest-audit-v1",
        "source_manifest": source_manifest.as_posix(),
        "strict_production_review": strict_production_review,
        "passed": not errors,
        "errors": list(errors),
    }
    if not source_manifest.exists():
        return report

    try:
        manifest = spcc.read_json(source_manifest)
    except Exception as exc:
        report["load_error"] = str(exc)
        return report

    if not isinstance(manifest, dict):
        report["load_error"] = "source manifest must be a JSON object"
        return report

    components = manifest.get("components", [])
    white_references = manifest.get("white_references", [])
    profiles = manifest.get("throughput_profiles", [])
    report.update(
        {
            "repository_id": manifest.get("repository_id", ""),
            "version": manifest.get("version", ""),
            "rights_review_status": manifest.get("rights", {}).get("review_status", "")
            if isinstance(manifest.get("rights"), dict)
            else "",
            "counts": {
                "components": len(components) if isinstance(components, list) else 0,
                "white_references": len(white_references) if isinstance(white_references, list) else 0,
                "throughput_profiles": len(profiles) if isinstance(profiles, list) else 0,
            },
            "placeholder_paths": collect_placeholder_paths(manifest),
            "curve_artifacts": collect_curve_artifacts(manifest, source_manifest.parent),
        }
    )
    return report


def collect_placeholder_paths(value: Any, label: str = "source_manifest") -> list[str]:
    paths: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            paths.extend(collect_placeholder_paths(child, f"{label}.{key}"))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            paths.extend(collect_placeholder_paths(child, f"{label}[{index}]"))
    elif isinstance(value, str):
        normalized = value.strip().upper()
        if any(token in normalized for token in PLACEHOLDER_TOKENS):
            paths.append(label)
    return paths


def collect_curve_artifacts(manifest: dict[str, Any], source_root: Path) -> list[dict[str, Any]]:
    artifacts: list[dict[str, Any]] = []
    for section, id_key in [("components", "component_id"), ("white_references", "white_reference_id")]:
        entries = manifest.get(section, [])
        if not isinstance(entries, list):
            continue
        for index, entry in enumerate(entries):
            if not isinstance(entry, dict):
                continue
            curve_csv = entry.get("curve_csv")
            artifact: dict[str, Any] = {
                "section": section,
                "index": index,
                "id": entry.get(id_key, ""),
                "path": curve_csv if isinstance(curve_csv, str) else "",
                "exists": False,
                "valid_curve_csv": False,
            }
            if isinstance(curve_csv, str) and curve_csv.strip():
                curve_path = (source_root / curve_csv).resolve()
                artifact["exists"] = curve_path.is_file()
                try:
                    spcc.read_curve_csv(curve_path)
                    artifact["valid_curve_csv"] = True
                except Exception as exc:
                    artifact["error"] = str(exc)
            else:
                artifact["error"] = "curve_csv is missing"
            artifacts.append(artifact)
    return artifacts


def validate_production_rights(value: Any, errors: list[str]) -> None:
    if not isinstance(value, dict):
        errors.append("source manifest rights must be an object")
        return
    if value.get("review_status") not in PRODUCTION_REVIEW_STATUSES:
        errors.append("source manifest rights.review_status must be approved for production use")
    for field in PRODUCTION_RIGHTS_FIELDS:
        if not non_empty_text(value.get(field)):
            errors.append(f"source manifest rights missing production metadata field: {field}")


def validate_assets(
    assets: list[dict[str, Any]],
    source_root: Path,
    id_key: str,
    section: str,
    strict_production_review: bool,
    errors: list[str],
) -> set[str]:
    ids: set[str] = set()
    resolved_root = source_root.resolve()
    for index, asset in enumerate(assets):
        label = f"{section}[{index}]"
        asset_id = require_text(asset, id_key, label, errors)
        if asset_id:
            if asset_id in ids:
                errors.append(f"{label} duplicate {id_key}: {asset_id}")
            ids.add(asset_id)
        require_text(asset, "curve_csv", label, errors)

        if strict_production_review:
            if asset.get("review_status") not in PRODUCTION_REVIEW_STATUSES:
                errors.append(f"{label} review_status must be approved for production use")
            for field in PRODUCTION_ASSET_FIELDS:
                if not non_empty_text(asset.get(field)):
                    errors.append(f"{label} missing production metadata field: {field}")

        curve_csv = asset.get("curve_csv")
        if isinstance(curve_csv, str) and curve_csv.strip():
            curve_path = (source_root / curve_csv).resolve()
            if not is_relative_to(curve_path, resolved_root):
                errors.append(f"{label} curve_csv must stay under the source manifest directory")
                continue
            try:
                spcc.read_curve_csv(curve_path)
            except Exception as exc:
                errors.append(f"{label} curve_csv invalid: {exc}")
    return ids


def validate_profiles(
    profiles: list[dict[str, Any]],
    component_ids: set[str],
    white_reference_ids: set[str],
    errors: list[str],
) -> None:
    for index, profile in enumerate(profiles):
        label = f"throughput_profiles[{index}]"
        profile_id = require_text(profile, "profile_id", label, errors) or "<unknown>"
        component_refs = profile.get("component_ids")
        if not isinstance(component_refs, list) or not component_refs:
            errors.append(f"{label} missing required non-empty array: component_ids")
        else:
            for component_id in component_refs:
                if not isinstance(component_id, str) or not component_id.strip():
                    errors.append(f"{label} component_ids contains an invalid component ID")
                elif component_id not in component_ids:
                    errors.append(f"throughput profile {profile_id} references missing component {component_id}")
        channel_component_ids = profile.get("channel_component_ids")
        if channel_component_ids is not None:
            if not isinstance(channel_component_ids, dict):
                errors.append(f"{label} channel_component_ids must be an object when provided")
            else:
                for channel, channel_refs in channel_component_ids.items():
                    channel_label = f"{label}.channel_component_ids.{channel}"
                    if not isinstance(channel_refs, list) or not channel_refs:
                        errors.append(f"{channel_label} must be a non-empty array")
                        continue
                    for component_id in channel_refs:
                        if not isinstance(component_id, str) or not component_id.strip():
                            errors.append(f"{channel_label} contains an invalid component ID")
                        elif component_id not in component_ids:
                            errors.append(
                                f"throughput profile {profile_id} channel {channel} references missing component "
                                f"{component_id}"
                            )
        white_reference_id = require_text(profile, "white_reference_id", label, errors)
        if white_reference_id and white_reference_id not in white_reference_ids:
            errors.append(f"throughput profile {profile_id} references missing white reference {white_reference_id}")
        compatible_bases = profile.get("compatible_channel_bases")
        if not isinstance(compatible_bases, list) or not compatible_bases:
            errors.append(f"{label} missing required non-empty array: compatible_channel_bases")


def require_object_list(entry: dict[str, Any], key: str, label: str, errors: list[str]) -> list[dict[str, Any]]:
    value = entry.get(key)
    if not isinstance(value, list) or not value:
        errors.append(f"{label} missing required non-empty array: {key}")
        return []
    objects: list[dict[str, Any]] = []
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            errors.append(f"{key}[{index}] must be an object")
            continue
        objects.append(item)
    return objects


def require_text(entry: dict[str, Any], key: str, label: str, errors: list[str]) -> str:
    value = entry.get(key)
    if not non_empty_text(value):
        errors.append(f"{label} missing required text field: {key}")
        return ""
    return str(value).strip()


def reject_placeholders(value: Any, label: str, errors: list[str]) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            reject_placeholders(child, f"{label}.{key}", errors)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            reject_placeholders(child, f"{label}[{index}]", errors)
    elif isinstance(value, str):
        normalized = value.strip().upper()
        if any(token in normalized for token in PLACEHOLDER_TOKENS):
            errors.append(f"{label} contains unresolved placeholder text")


def non_empty_text(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


if __name__ == "__main__":
    raise SystemExit(main())
