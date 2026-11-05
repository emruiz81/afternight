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


SOURCE_SCHEMA_VERSION = "spcc-real-image-validation-source-v1"
OUTPUT_SCHEMA_VERSION = "spcc-real-image-validation-manifest-entries-v1"
PRODUCTION_REVIEW_STATUSES = {"approved-for-bundling", "approved-for-managed-download"}
PLACEHOLDER_TOKENS = ("TODO", "<TODO", "TBD")

REAL_IMAGE_REQUIRED_TEXT_FIELDS = [
    "id",
    "status",
    "source_url_or_acquisition_path",
    "license",
    "rights_review_status",
    "target_name",
    "camera_or_sensor",
    "field_class",
    "tolerance_version",
]
EXTERNAL_REQUIRED_TEXT_FIELDS = [
    "id",
    "reference_set_id",
    "tool",
    "tool_version",
    "run_command_or_notes",
    "license",
    "rights_review_status",
    "tolerance_version",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build ready-to-merge SPCC real-image and external-comparison validation manifest entries "
            "from reviewed source artifacts."
        )
    )
    parser.add_argument("source_manifest", type=Path)
    parser.add_argument("output_json", type=Path)
    parser.add_argument(
        "--strict-production-review",
        action="store_true",
        help="Require production-approved rights status and reject unresolved placeholders.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        entries = build_manifest_entries(args.source_manifest, args.strict_production_review)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    spcc.write_json(args.output_json, entries)
    print(f"SPCC real-image validation manifest entries written: {args.output_json}")
    print(f"Real image reference sets: {len(entries['real_image_reference_sets'])}")
    print(f"External comparison outputs: {len(entries['external_comparison_outputs'])}")
    return 0


def build_manifest_entries(source_manifest: Path, strict_production_review: bool) -> dict[str, Any]:
    errors: list[str] = []
    manifest = load_and_validate_source_manifest(source_manifest, strict_production_review, errors)
    if errors:
        joined = "\n  - ".join(errors)
        raise ValueError(f"SPCC real-image validation source manifest is not ready:\n  - {joined}")
    assert manifest is not None

    source_root = source_manifest.parent
    return {
        "schema_version": OUTPUT_SCHEMA_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "source_manifest": source_manifest.as_posix(),
        "real_image_reference_sets": [
            build_entry_with_file_hashes(entry, source_root, "files")
            for entry in sorted(manifest["real_image_reference_sets"], key=lambda value: value["id"])
        ],
        "external_comparison_outputs": [
            build_entry_with_file_hashes(entry, source_root, "output_files")
            for entry in sorted(manifest["external_comparison_outputs"], key=lambda value: value["id"])
        ],
    }


def validate_source_manifest(source_manifest: Path, strict_production_review: bool, errors: list[str]) -> None:
    load_and_validate_source_manifest(source_manifest, strict_production_review, errors)


def load_and_validate_source_manifest(
    source_manifest: Path,
    strict_production_review: bool,
    errors: list[str],
) -> dict[str, Any] | None:
    if not source_manifest.exists():
        errors.append(f"source manifest is missing: {source_manifest}")
        return None

    manifest = spcc.read_json(source_manifest)
    if not isinstance(manifest, dict):
        errors.append("source manifest must be a JSON object")
        return None

    if manifest.get("schema_version") != SOURCE_SCHEMA_VERSION:
        errors.append(f"source manifest schema_version must be {SOURCE_SCHEMA_VERSION}")

    real_image_sets = require_object_list(manifest, "real_image_reference_sets", "source manifest", errors)
    external_outputs = require_object_list(manifest, "external_comparison_outputs", "source manifest", errors)
    if strict_production_review:
        reject_placeholders(manifest, "source manifest", errors)

    real_image_ids = validate_real_image_reference_sets(
        real_image_sets,
        source_manifest.parent,
        strict_production_review,
        errors,
    )
    validate_external_comparison_outputs(
        external_outputs,
        real_image_ids,
        source_manifest.parent,
        strict_production_review,
        errors,
    )
    validate_unique_ids("external_comparison_outputs", external_outputs, errors)
    return manifest


def validate_real_image_reference_sets(
    entries: list[dict[str, Any]],
    source_root: Path,
    strict_production_review: bool,
    errors: list[str],
) -> set[str]:
    ids: set[str] = set()
    for index, entry in enumerate(entries):
        label = entry_label("real_image_reference_sets", entry, index)
        entry_id = validate_required_text_fields(entry, REAL_IMAGE_REQUIRED_TEXT_FIELDS, label, errors)
        if entry_id:
            if entry_id in ids:
                errors.append(f"{label} duplicate id: {entry_id}")
            ids.add(entry_id)
        validate_expected_metrics(entry, label, errors)
        validate_file_list(entry.get("files"), source_root, label, "files", errors)
        if strict_production_review:
            require_production_review(entry, label, errors)
    return ids


def validate_external_comparison_outputs(
    entries: list[dict[str, Any]],
    real_image_ids: set[str],
    source_root: Path,
    strict_production_review: bool,
    errors: list[str],
) -> None:
    for index, entry in enumerate(entries):
        label = entry_label("external_comparison_outputs", entry, index)
        validate_required_text_fields(entry, EXTERNAL_REQUIRED_TEXT_FIELDS, label, errors)
        reference_set_id = entry.get("reference_set_id")
        if isinstance(reference_set_id, str) and reference_set_id.strip() and reference_set_id not in real_image_ids:
            errors.append(f"{label} references unknown real image set: {reference_set_id}")
        validate_expected_metrics(entry, label, errors)
        validate_file_list(entry.get("output_files"), source_root, label, "output_files", errors)
        if strict_production_review:
            require_production_review(entry, label, errors)


def validate_unique_ids(
    section: str,
    entries: list[dict[str, Any]],
    errors: list[str],
) -> None:
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        entry_id = entry.get("id")
        if not isinstance(entry_id, str) or not entry_id.strip():
            continue
        if entry_id in seen:
            errors.append(f"{section}[{index}] duplicate validation id in section: {entry_id}")
        seen.add(entry_id)


def validate_required_text_fields(
    entry: dict[str, Any],
    fields: list[str],
    label: str,
    errors: list[str],
) -> str:
    first_value = ""
    for field in fields:
        value = entry.get(field)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"{label} missing required text field: {field}")
        elif not first_value:
            first_value = value.strip()
    return first_value


def validate_expected_metrics(entry: dict[str, Any], label: str, errors: list[str]) -> None:
    value = entry.get("expected_metrics")
    if not isinstance(value, dict) or not value:
        errors.append(f"{label} missing required object field: expected_metrics")


def validate_file_list(value: Any, source_root: Path, label: str, key: str, errors: list[str]) -> None:
    if not isinstance(value, list) or not value:
        errors.append(f"{label} missing non-empty {key}")
        return

    for index, item in enumerate(value):
        item_label = f"{label}.{key}[{index}]"
        if not isinstance(item, dict):
            errors.append(f"{item_label} must be an object")
            continue
        path_value = item.get("path")
        if not isinstance(path_value, str) or not path_value.strip():
            errors.append(f"{item_label} missing required text field: path")
        else:
            resolve_artifact_path(source_root, path_value, item_label, errors)
        role_value = item.get("role")
        if not isinstance(role_value, str) or not role_value.strip():
            errors.append(f"{item_label} missing required text field: role")


def resolve_artifact_path(
    source_root: Path,
    relative_path: str,
    label: str,
    errors: list[str],
) -> Path | None:
    candidate = Path(relative_path)
    if candidate.is_absolute():
        errors.append(f"{label} path must be relative to the source manifest directory")
        return None

    resolved_root = source_root.resolve()
    resolved = (source_root / candidate).resolve()
    if not is_relative_to(resolved, resolved_root):
        errors.append(f"{label} path must stay under the source manifest directory")
        return None
    if not resolved.is_file():
        errors.append(f"{label} artifact file is missing: {relative_path}")
        return None
    return resolved


def build_entry_with_file_hashes(entry: dict[str, Any], source_root: Path, key: str) -> dict[str, Any]:
    result = dict(entry)
    result[key] = [build_file_entry(item, source_root) for item in entry[key]]
    return result


def build_file_entry(item: dict[str, Any], source_root: Path) -> dict[str, Any]:
    relative_path = Path(str(item["path"]).strip()).as_posix()
    artifact_path = (source_root / relative_path).resolve()
    result = dict(item)
    result["path"] = relative_path
    result["role"] = str(item["role"]).strip()
    result["sha256"] = spcc.sha256_file(artifact_path)
    return result


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


def require_production_review(entry: dict[str, Any], label: str, errors: list[str]) -> None:
    if entry.get("rights_review_status") not in PRODUCTION_REVIEW_STATUSES:
        errors.append(f"{label} lacks approved production rights review")


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


def entry_label(section: str, entry: dict[str, Any], index: int) -> str:
    entry_id = entry.get("id")
    if isinstance(entry_id, str) and entry_id.strip():
        return f"{section}.{entry_id.strip()}"
    return f"{section}[{index}]"


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


if __name__ == "__main__":
    raise SystemExit(main())
