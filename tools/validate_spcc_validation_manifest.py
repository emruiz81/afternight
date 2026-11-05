#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
PRODUCTION_REVIEW_STATUSES = {"approved-for-bundling", "approved-for-managed-download"}
SUPPORT_ENVELOPE_METRICS_SCHEMA = "spcc-support-envelope-validation-v1"
SUPPORT_ENVELOPE_REPORT_SCHEMA = "spcc-support-envelope-go-no-go-v1"
RESPONSE_META_SCHEMA = "spcc-response-repository-v1"
RESPONSE_CONFIG_SCHEMA = "spcc-response-repository-config-v1"
CATALOG_CONFIG_SCHEMA = "spcc-catalog-mirror-config-v1"
PACKAGED_QA_REPORT_SCHEMA = "spcc-packaged-qa-report-v1"
SUPPORT_ENVELOPE_STATUSES = {
    "production_ready",
    "blocked_rights",
    "insufficient_support",
    "validation_missing",
    "validation_failed",
    "invalid_catalog",
}
PACKAGED_QA_STATUSES = {
    "passed",
    "failed",
    "blocked",
    "not-run",
}
PACKAGED_QA_REQUIRED_CHECKS = [
    "hosted_catalog_download_smoke",
    "hosted_response_download_smoke",
    "runtime_response_discovery",
    "runtime_response_checksum_validation",
    "catalog_repair_resume_cancel_smoke",
    "install_marker_eviction_smoke",
    "visual_qa_1080p",
    "visual_qa_720p",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate the AfterNight SPCC validation-data manifest.")
    parser.add_argument("manifest", type=Path)
    parser.add_argument(
        "--strict-production-review",
        action="store_true",
        help="Require production-approved rights status for real image sets and external comparison outputs.",
    )
    parser.add_argument(
        "--release-ready",
        action="store_true",
        help=(
            "Require the manifest to contain the production evidence needed before SPCC can be called release-ready."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    try:
        validate_manifest(
            args.manifest,
            args.strict_production_review or args.release_ready,
            errors,
            args.release_ready,
        )
    except Exception as exc:
        errors.append(str(exc))

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"SPCC validation-data manifest validation passed: {args.manifest}")
    return 0


def validate_manifest(
    manifest_path: Path,
    strict_production_review: bool,
    errors: list[str],
    release_ready: bool = False,
) -> None:
    if not manifest_path.exists():
        errors.append(f"manifest is missing: {manifest_path}")
        return

    manifest = spcc.read_json(manifest_path)
    if manifest.get("schema_version") != "spcc-validation-data-manifest-v1":
        errors.append("unsupported or missing schema_version")
    require_text(manifest, "updated_at_utc", "manifest", errors)
    require_text(manifest, "purpose", "manifest", errors)

    array_sections = [
        "fixture_catalogs",
        "response_repositories",
        "real_image_reference_sets",
        "external_comparison_outputs",
        "gaia_validation_artifacts",
        "support_envelope_reports",
        "packaged_qa_reports",
    ]
    for section in array_sections:
        if not isinstance(manifest.get(section), list):
            errors.append(f"{section} must be an array")

    if errors:
        return

    validate_unique_ids(manifest, array_sections, errors)
    validate_fixture_catalogs(manifest["fixture_catalogs"], errors)
    validate_response_repositories(manifest["response_repositories"], errors)
    validate_gaia_validation_artifacts(manifest["gaia_validation_artifacts"], errors)
    validate_support_envelope_reports(
        manifest["support_envelope_reports"],
        strict_production_review,
        errors,
    )
    validate_real_image_reference_sets(
        manifest["real_image_reference_sets"],
        strict_production_review,
        errors,
    )
    real_image_ids = {
        entry["id"]
        for entry in manifest["real_image_reference_sets"]
        if isinstance(entry, dict) and entry.get("id")
    }
    validate_external_comparison_outputs(
        manifest["external_comparison_outputs"],
        real_image_ids,
        strict_production_review,
        errors,
    )
    validate_packaged_qa_reports(manifest["packaged_qa_reports"], errors)
    if release_ready:
        validate_release_ready_evidence(manifest, errors)


def validate_fixture_catalogs(entries: list[dict[str, Any]], errors: list[str]) -> None:
    for entry in entries:
        label = entry_label("fixture_catalogs", entry)
        require_text(entry, "id", label, errors)
        require_text(entry, "catalog_family_id", label, errors)
        require_text(entry, "source_kind", label, errors)
        require_text(entry, "generator", label, errors)
        require_text(entry, "generation_command", label, errors)
        require_text(entry, "validation_command", label, errors)
        require_text(entry, "license", label, errors)
        require_text(entry, "rights_review_status", label, errors)
        require_object(entry, "expected_metrics", label, errors)
        require_text(entry, "tolerance_version", label, errors)
        validate_optional_schema_artifact(entry, label, "response_meta", RESPONSE_META_SCHEMA, errors)
        validate_optional_schema_artifact(entry, label, "descriptor_config", RESPONSE_CONFIG_SCHEMA, errors)


def validate_response_repositories(entries: list[dict[str, Any]], errors: list[str]) -> None:
    for entry in entries:
        label = entry_label("response_repositories", entry)
        require_text(entry, "id", label, errors)
        require_text(entry, "generator", label, errors)
        require_text(entry, "validation_command", label, errors)
        require_text(entry, "license", label, errors)
        require_text(entry, "rights_review_status", label, errors)
        require_object(entry, "expected_metrics", label, errors)
        require_text(entry, "tolerance_version", label, errors)


def validate_release_ready_evidence(manifest: dict[str, Any], errors: list[str]) -> None:
    if not has_production_response_repository(manifest["response_repositories"]):
        errors.append("release-ready manifest requires at least one production-approved response repository")

    if not has_production_support_envelope_report(manifest["support_envelope_reports"]):
        errors.append("release-ready manifest requires at least one production-ready support envelope report")

    if not has_production_reviewed_entry(manifest["real_image_reference_sets"]):
        errors.append("release-ready manifest requires at least one production-approved real image reference set")

    if not has_production_reviewed_entry(manifest["external_comparison_outputs"]):
        errors.append("release-ready manifest requires at least one production-approved external comparison output")

    if not has_passed_packaged_qa_report(manifest["packaged_qa_reports"]):
        errors.append("release-ready manifest requires at least one passed packaged QA report")


def has_production_response_repository(entries: list[dict[str, Any]]) -> bool:
    return any(
        isinstance(entry, dict) and entry.get("rights_review_status") in PRODUCTION_REVIEW_STATUSES
        for entry in entries
    )


def has_production_support_envelope_report(entries: list[dict[str, Any]]) -> bool:
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        if entry.get("status") != "production-ready":
            continue
        if entry.get("rights_review_status") not in PRODUCTION_REVIEW_STATUSES:
            continue
        expected_statuses = entry.get("expected_catalog_statuses")
        if not isinstance(expected_statuses, dict) or not expected_statuses:
            continue
        if all(status == "production_ready" for status in expected_statuses.values()):
            return True
    return False


def has_production_reviewed_entry(entries: list[dict[str, Any]]) -> bool:
    return any(
        isinstance(entry, dict) and entry.get("rights_review_status") in PRODUCTION_REVIEW_STATUSES
        for entry in entries
    )


def has_passed_packaged_qa_report(entries: list[dict[str, Any]]) -> bool:
    for entry in entries:
        if not isinstance(entry, dict) or entry.get("status") != "passed":
            continue
        checklist = entry.get("checklist")
        if not isinstance(checklist, dict):
            continue
        if all(checklist.get(check_name) is True for check_name in PACKAGED_QA_REQUIRED_CHECKS):
            return True
    return False


def validate_gaia_validation_artifacts(entries: list[dict[str, Any]], errors: list[str]) -> None:
    for entry in entries:
        label = entry_label("gaia_validation_artifacts", entry)
        require_text(entry, "id", label, errors)
        require_text(entry, "status", label, errors)
        require_text(entry, "transcript", label, errors)
        require_sha256(entry, "transcript_sha256", label, errors)
        require_text(entry, "source_kind", label, errors)
        require_text(entry, "rights_review_status", label, errors)
        require_text(entry, "generation_command", label, errors)
        require_text(entry, "catalog_generation_command", label, errors)
        require_text(entry, "validation_command", label, errors)


def validate_support_envelope_reports(
    entries: list[dict[str, Any]],
    strict_production_review: bool,
    errors: list[str],
) -> None:
    for entry in entries:
        label = entry_label("support_envelope_reports", entry)
        require_text(entry, "id", label, errors)
        require_text(entry, "status", label, errors)
        require_text(entry, "catalog_family_id", label, errors)
        require_text(entry, "rights_review_status", label, errors)
        require_text(entry, "validation_command", label, errors)
        require_text(entry, "tolerance_version", label, errors)
        catalog_manifest_ids = require_text_list(entry, "catalog_manifest_ids", label, errors)
        validate_schema_artifact(
            entry.get("validation_metrics"),
            label,
            "validation_metrics",
            SUPPORT_ENVELOPE_METRICS_SCHEMA,
            errors,
        )
        validate_schema_artifact(
            entry.get("go_no_go_report"),
            label,
            "go_no_go_report",
            SUPPORT_ENVELOPE_REPORT_SCHEMA,
            errors,
        )
        validate_support_envelope_statuses(
            entry.get("expected_catalog_statuses"),
            catalog_manifest_ids,
            label,
            errors,
        )
        if strict_production_review:
            require_production_review(entry, label, errors)


def validate_real_image_reference_sets(
    entries: list[dict[str, Any]],
    strict_production_review: bool,
    errors: list[str],
) -> None:
    for entry in entries:
        label = entry_label("real_image_reference_sets", entry)
        require_text(entry, "id", label, errors)
        require_text(entry, "status", label, errors)
        require_text(entry, "source_url_or_acquisition_path", label, errors)
        require_text(entry, "license", label, errors)
        require_text(entry, "rights_review_status", label, errors)
        require_text(entry, "target_name", label, errors)
        require_text(entry, "camera_or_sensor", label, errors)
        require_text(entry, "field_class", label, errors)
        require_object(entry, "expected_metrics", label, errors)
        require_text(entry, "tolerance_version", label, errors)
        validate_file_list(entry.get("files"), label, "files", errors)
        if strict_production_review:
            require_production_review(entry, label, errors)


def validate_external_comparison_outputs(
    entries: list[dict[str, Any]],
    real_image_ids: set[str],
    strict_production_review: bool,
    errors: list[str],
) -> None:
    for entry in entries:
        label = entry_label("external_comparison_outputs", entry)
        require_text(entry, "id", label, errors)
        reference_set_id = require_text(entry, "reference_set_id", label, errors)
        if reference_set_id and reference_set_id not in real_image_ids:
            errors.append(f"{label} references unknown real image set: {reference_set_id}")
        require_text(entry, "tool", label, errors)
        require_text(entry, "tool_version", label, errors)
        require_text(entry, "run_command_or_notes", label, errors)
        require_text(entry, "license", label, errors)
        require_text(entry, "rights_review_status", label, errors)
        require_object(entry, "expected_metrics", label, errors)
        require_text(entry, "tolerance_version", label, errors)
        validate_file_list(entry.get("output_files"), label, "output_files", errors)
        if strict_production_review:
            require_production_review(entry, label, errors)


def validate_packaged_qa_reports(entries: list[dict[str, Any]], errors: list[str]) -> None:
    for entry in entries:
        label = entry_label("packaged_qa_reports", entry)
        require_text(entry, "id", label, errors)
        status = require_text(entry, "status", label, errors)
        if status and status not in PACKAGED_QA_STATUSES:
            errors.append(f"{label} unsupported packaged QA status: {status}")
        require_text(entry, "platform", label, errors)
        require_text(entry, "package_type", label, errors)
        require_text(entry, "build_configuration", label, errors)
        require_text(entry, "build_artifact", label, errors)
        require_text(entry, "validation_command", label, errors)
        require_text(entry, "tolerance_version", label, errors)
        validate_schema_artifact(
            entry.get("catalog_mirror_config"),
            label,
            "catalog_mirror_config",
            CATALOG_CONFIG_SCHEMA,
            errors,
        )
        validate_schema_artifact(
            entry.get("response_repository_config"),
            label,
            "response_repository_config",
            RESPONSE_CONFIG_SCHEMA,
            errors,
        )
        validate_schema_artifact(
            entry.get("qa_report"),
            label,
            "qa_report",
            PACKAGED_QA_REPORT_SCHEMA,
            errors,
        )
        validate_packaged_qa_checklist(entry.get("checklist"), label, errors)
        validate_file_list(entry.get("artifacts"), label, "artifacts", errors)


def validate_packaged_qa_checklist(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, dict) or not value:
        errors.append(f"{label} missing required object field: checklist")
        return
    for check_name in PACKAGED_QA_REQUIRED_CHECKS:
        if not isinstance(value.get(check_name), bool):
            errors.append(f"{label}.checklist.{check_name} must be a boolean")


def validate_unique_ids(manifest: dict[str, Any], sections: list[str], errors: list[str]) -> None:
    for section in sections:
        seen: set[str] = set()
        for entry in manifest.get(section, []):
            if not isinstance(entry, dict):
                errors.append(f"{section} contains a non-object entry")
                continue
            entry_id = entry.get("id")
            if not isinstance(entry_id, str) or not entry_id.strip():
                continue
            if entry_id in seen:
                errors.append(f"duplicate validation manifest {section} id: {entry_id}")
            seen.add(entry_id)


def validate_file_list(value: Any, label: str, key: str, errors: list[str]) -> None:
    if not isinstance(value, list) or not value:
        errors.append(f"{label} missing non-empty {key}")
        return

    for index, item in enumerate(value):
        item_label = f"{label}.{key}[{index}]"
        if not isinstance(item, dict):
            errors.append(f"{item_label} must be an object")
            continue
        require_text(item, "path", item_label, errors)
        require_text(item, "role", item_label, errors)
        require_sha256(item, "sha256", item_label, errors)


def validate_schema_artifact(
    value: Any,
    label: str,
    key: str,
    expected_schema_version: str,
    errors: list[str],
) -> None:
    if not isinstance(value, dict):
        errors.append(f"{label} missing required object field: {key}")
        return
    artifact_label = f"{label}.{key}"
    require_text(value, "path", artifact_label, errors)
    require_sha256(value, "sha256", artifact_label, errors)
    schema_version = require_text(value, "schema_version", artifact_label, errors)
    if schema_version and schema_version != expected_schema_version:
        errors.append(f"{artifact_label} schema_version must be {expected_schema_version}")


def validate_optional_schema_artifact(
    entry: dict[str, Any],
    label: str,
    key: str,
    expected_schema_version: str,
    errors: list[str],
) -> None:
    if key not in entry:
        return
    validate_schema_artifact(entry.get(key), label, key, expected_schema_version, errors)


def validate_support_envelope_statuses(
    value: Any,
    catalog_manifest_ids: list[str],
    label: str,
    errors: list[str],
) -> None:
    if not isinstance(value, dict) or not value:
        errors.append(f"{label} missing required object field: expected_catalog_statuses")
        return

    for manifest_id in catalog_manifest_ids:
        if manifest_id not in value:
            errors.append(f"{label} missing expected_catalog_statuses entry for {manifest_id}")

    for manifest_id, status in value.items():
        if not isinstance(manifest_id, str) or not manifest_id.strip():
            errors.append(f"{label} expected_catalog_statuses contains an invalid manifest_id")
            continue
        if manifest_id not in catalog_manifest_ids:
            errors.append(f"{label} expected_catalog_statuses references unknown manifest_id: {manifest_id}")
        if status not in SUPPORT_ENVELOPE_STATUSES:
            errors.append(f"{label} unsupported support-envelope status: {status}")


def require_text(entry: dict[str, Any], key: str, label: str, errors: list[str]) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{label} missing required text field: {key}")
        return ""
    return value


def require_text_list(entry: dict[str, Any], key: str, label: str, errors: list[str]) -> list[str]:
    value = entry.get(key)
    if not isinstance(value, list) or not value:
        errors.append(f"{label} missing required non-empty text array: {key}")
        return []

    items: list[str] = []
    seen: set[str] = set()
    for index, item in enumerate(value):
        if not isinstance(item, str) or not item.strip():
            errors.append(f"{label}.{key}[{index}] must be non-empty text")
            continue
        normalized = item.strip()
        if normalized in seen:
            errors.append(f"{label}.{key} contains duplicate value: {normalized}")
        seen.add(normalized)
        items.append(normalized)
    return items


def require_object(entry: dict[str, Any], key: str, label: str, errors: list[str]) -> None:
    value = entry.get(key)
    if not isinstance(value, dict) or not value:
        errors.append(f"{label} missing required object field: {key}")


def require_sha256(entry: dict[str, Any], key: str, label: str, errors: list[str]) -> None:
    value = entry.get(key)
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        errors.append(f"{label} missing valid sha256 field: {key}")


def require_production_review(entry: dict[str, Any], label: str, errors: list[str]) -> None:
    if entry.get("rights_review_status") not in PRODUCTION_REVIEW_STATUSES:
        errors.append(f"{label} lacks approved production rights review")


def entry_label(section: str, entry: Any) -> str:
    if isinstance(entry, dict):
        entry_id = entry.get("id")
        if isinstance(entry_id, str) and entry_id:
            return f"{section}.{entry_id}"
    return section


if __name__ == "__main__":
    raise SystemExit(main())
