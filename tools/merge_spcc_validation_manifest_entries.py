#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc
from tools import validate_spcc_validation_manifest


VALIDATION_MANIFEST_SCHEMA = "spcc-validation-data-manifest-v1"
ENTRY_SECTIONS = [
    "fixture_catalogs",
    "response_repositories",
    "support_envelope_reports",
    "real_image_reference_sets",
    "external_comparison_outputs",
    "gaia_validation_artifacts",
    "packaged_qa_reports",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Merge generated SPCC validation-manifest entries into a validation-data manifest."
    )
    parser.add_argument("manifest", type=Path)
    parser.add_argument("entry_json", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, help="Write the merged manifest to this path.")
    parser.add_argument("--in-place", action="store_true", help="Replace the input manifest after validation.")
    parser.add_argument(
        "--strict-production-review",
        action="store_true",
        help="Validate the merged manifest with strict production review checks.",
    )
    parser.add_argument(
        "--release-ready",
        action="store_true",
        help="Validate the merged manifest with the final release-ready evidence checks.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.in_place == bool(args.output):
        print("error: pass exactly one of --output or --in-place", file=sys.stderr)
        return 2

    try:
        manifest, merge_counts = merge_entry_files(args.manifest, args.entry_json)
        validate_merged_manifest(
            manifest,
            strict_production_review=args.strict_production_review or args.release_ready,
            release_ready=args.release_ready,
        )
        output_path = args.manifest if args.in_place else args.output
        assert output_path is not None
        spcc.write_json(output_path, manifest)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"SPCC validation-data manifest written: {output_path}")
    for section in ENTRY_SECTIONS:
        if merge_counts.get(section, 0):
            print(f"{section}: merged {merge_counts[section]} entries")
    return 0


def merge_entry_files(
    manifest_path: Path,
    entry_paths: list[Path],
) -> tuple[dict[str, Any], dict[str, int]]:
    manifest = spcc.read_json(manifest_path)
    if not isinstance(manifest, dict):
        raise ValueError("validation manifest must be a JSON object")
    if manifest.get("schema_version") != VALIDATION_MANIFEST_SCHEMA:
        raise ValueError(f"validation manifest schema_version must be {VALIDATION_MANIFEST_SCHEMA}")

    for section in ENTRY_SECTIONS:
        if not isinstance(manifest.get(section), list):
            raise ValueError(f"validation manifest section must be an array: {section}")

    merge_counts = {section: 0 for section in ENTRY_SECTIONS}
    incoming_seen: dict[str, set[str]] = {section: set() for section in ENTRY_SECTIONS}
    for entry_path in entry_paths:
        sections = load_entry_sections(entry_path)
        for section, entries in sections.items():
            validate_incoming_entries(entry_path, section, entries, incoming_seen[section])
            merge_entries(manifest[section], entries)
            merge_counts[section] += len(entries)

    manifest["updated_at_utc"] = spcc.utc_now_iso()
    return manifest, merge_counts


def load_entry_sections(entry_path: Path) -> dict[str, list[dict[str, Any]]]:
    data = spcc.read_json(entry_path)
    if not isinstance(data, dict):
        raise ValueError(f"{entry_path} must contain a JSON object")

    sections: dict[str, list[dict[str, Any]]] = {}
    for section in ENTRY_SECTIONS:
        if section not in data:
            continue
        value = data[section]
        if not isinstance(value, list):
            raise ValueError(f"{entry_path}: {section} must be an array")
        sections[section] = require_entry_objects(entry_path, section, value)

    if sections:
        return sections

    if looks_like_support_envelope_entry(data):
        return {"support_envelope_reports": [data]}

    raise ValueError(f"{entry_path} does not contain supported SPCC validation-manifest entries")


def require_entry_objects(entry_path: Path, section: str, value: list[Any]) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            raise ValueError(f"{entry_path}: {section}[{index}] must be an object")
        entries.append(item)
    return entries


def looks_like_support_envelope_entry(data: dict[str, Any]) -> bool:
    return all(
        key in data
        for key in [
            "id",
            "catalog_manifest_ids",
            "validation_metrics",
            "go_no_go_report",
            "expected_catalog_statuses",
        ]
    )


def validate_incoming_entries(
    entry_path: Path,
    section: str,
    entries: list[dict[str, Any]],
    seen_ids: set[str],
) -> None:
    for index, entry in enumerate(entries):
        entry_id = entry.get("id")
        if not isinstance(entry_id, str) or not entry_id.strip():
            raise ValueError(f"{entry_path}: {section}[{index}] missing required text field: id")
        normalized_id = entry_id.strip()
        if normalized_id in seen_ids:
            raise ValueError(f"{entry_path}: duplicate incoming {section} id: {normalized_id}")
        seen_ids.add(normalized_id)


def merge_entries(existing_entries: list[dict[str, Any]], incoming_entries: list[dict[str, Any]]) -> None:
    index_by_id = {
        str(entry.get("id", "")).strip(): index
        for index, entry in enumerate(existing_entries)
        if isinstance(entry, dict) and str(entry.get("id", "")).strip()
    }
    for incoming in incoming_entries:
        entry_id = str(incoming["id"]).strip()
        if entry_id in index_by_id:
            existing_entries[index_by_id[entry_id]] = incoming
        else:
            index_by_id[entry_id] = len(existing_entries)
            existing_entries.append(incoming)


def validate_merged_manifest(
    manifest: dict[str, Any],
    strict_production_review: bool,
    release_ready: bool,
) -> None:
    with tempfile.TemporaryDirectory() as temp_dir_raw:
        temp_manifest = Path(temp_dir_raw) / "spcc-validation-data-manifest.json"
        spcc.write_json(temp_manifest, manifest)
        errors: list[str] = []
        validate_spcc_validation_manifest.validate_manifest(
            temp_manifest,
            strict_production_review,
            errors,
            release_ready,
        )
        if errors:
            joined = "\n  - ".join(errors)
            raise ValueError(f"merged validation manifest failed validation:\n  - {joined}")


if __name__ == "__main__":
    raise SystemExit(main())
