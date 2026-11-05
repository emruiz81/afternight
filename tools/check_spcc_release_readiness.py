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

from tools import build_spcc_real_image_validation_entries
from tools import spcc_tool_common as spcc
from tools import validate_spcc_response_source_manifest
from tools import validate_spcc_validation_manifest
from tools import verify_spcc_catalog_mirror_config
from tools import verify_spcc_response_repository_config


DEFAULT_VALIDATION_MANIFEST = Path("docs/dev/spcc/spcc-validation-data-manifest.json")
REAL_IMAGE_MANIFEST_KEYS = [
    "id",
    "status",
    "source_url_or_acquisition_path",
    "license",
    "rights_review_status",
    "target_name",
    "camera_or_sensor",
    "field_class",
    "files",
    "expected_metrics",
    "tolerance_version",
]
EXTERNAL_COMPARISON_MANIFEST_KEYS = [
    "id",
    "reference_set_id",
    "tool",
    "tool_version",
    "run_command_or_notes",
    "license",
    "rights_review_status",
    "output_files",
    "expected_metrics",
    "tolerance_version",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the production SPCC release-readiness gate checks.")
    parser.add_argument("--validation-manifest", type=Path, default=DEFAULT_VALIDATION_MANIFEST)
    parser.add_argument("--catalog-mirror-config", help="Path or URL to a non-empty spcc_catalog_mirrors.json.")
    parser.add_argument(
        "--response-repository-config",
        help="Path or URL to a non-empty spcc_response_repositories.json.",
    )
    parser.add_argument(
        "--response-source-manifest",
        type=Path,
        action="append",
        default=[],
        help="Reviewed response source manifest. May be supplied more than once.",
    )
    parser.add_argument(
        "--real-image-validation-source-manifest",
        type=Path,
        action="append",
        default=[],
        help=(
            "Reviewed real-image validation source manifest. May be supplied more than once. "
            "Generated entries must match the validation manifest."
        ),
    )
    parser.add_argument(
        "--chunk-policy",
        choices=["none", "head", "first", "all"],
        default="head",
        help="Catalog chunk verification policy passed to the catalog mirror verifier.",
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--token-env", default="HF_TOKEN")
    parser.add_argument("--token-file", type=Path)
    parser.add_argument("--no-token", action="store_true")
    parser.add_argument("--max-chunk-bytes", type=int, default=64 * 1024 * 1024)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    gates = run_release_readiness(args)

    for gate in gates:
        prefix = "PASS" if gate["passed"] else "FAIL"
        print(f"{prefix}: {gate['name']}")
        for diagnostic in gate["diagnostics"]:
            print(f"  - {diagnostic}")

    return 0 if all(gate["passed"] for gate in gates) else 1


def run_release_readiness(args: argparse.Namespace) -> list[dict[str, Any]]:
    gates = [
        validation_manifest_gate(args.validation_manifest),
        catalog_mirror_config_gate(args),
        response_repository_config_gate(args),
        response_source_manifests_gate(args.response_source_manifest),
        real_image_validation_source_manifests_gate(
            args.validation_manifest,
            args.real_image_validation_source_manifest,
        ),
    ]
    return gates


def validation_manifest_gate(path: Path) -> dict[str, Any]:
    errors: list[str] = []
    try:
        validate_spcc_validation_manifest.validate_manifest(path, True, errors, True)
    except Exception as exc:
        errors.append(str(exc))
    return make_gate("validation manifest release evidence", errors)


def catalog_mirror_config_gate(args: argparse.Namespace) -> dict[str, Any]:
    if not args.catalog_mirror_config:
        return make_gate("catalog mirror config", ["non-empty catalog mirror config is required"])
    errors: list[str] = []
    warnings: list[str] = []
    try:
        verify_args = argparse.Namespace(
            mirror_config=args.catalog_mirror_config,
            chunk_policy=args.chunk_policy,
            timeout=args.timeout,
            max_chunk_bytes=args.max_chunk_bytes,
            token_env=args.token_env,
            token_file=args.token_file,
            no_token=args.no_token,
            allow_empty=False,
        )
        verify_spcc_catalog_mirror_config.verify_mirror_config(verify_args, errors, warnings)
    except Exception as exc:
        errors.append(str(exc))
    diagnostics = [*errors, *[f"warning: {warning}" for warning in warnings]]
    return make_gate("catalog mirror config", diagnostics, passed=not errors)


def response_repository_config_gate(args: argparse.Namespace) -> dict[str, Any]:
    if not args.response_repository_config:
        return make_gate("response repository config", ["non-empty response repository config is required"])
    errors: list[str] = []
    try:
        verify_args = argparse.Namespace(
            repository_config=args.response_repository_config,
            timeout=args.timeout,
            token_env=args.token_env,
            token_file=args.token_file,
            no_token=args.no_token,
            allow_empty=False,
        )
        verify_spcc_response_repository_config.verify_repository_config(verify_args, errors)
    except Exception as exc:
        errors.append(str(exc))
    return make_gate("response repository config", errors)


def response_source_manifests_gate(paths: list[Path]) -> dict[str, Any]:
    if not paths:
        return make_gate(
            "reviewed response source manifests",
            ["at least one reviewed response source manifest is required"],
        )

    errors: list[str] = []
    for path in paths:
        manifest_errors: list[str] = []
        try:
            validate_spcc_response_source_manifest.validate_source_manifest(path, True, manifest_errors)
        except Exception as exc:
            manifest_errors.append(str(exc))
        errors.extend(f"{path}: {error}" for error in manifest_errors)
    return make_gate("reviewed response source manifests", errors)


def real_image_validation_source_manifests_gate(
    validation_manifest_path: Path,
    paths: list[Path],
) -> dict[str, Any]:
    if not paths:
        return make_gate(
            "reviewed real-image validation source manifests",
            ["at least one reviewed real-image validation source manifest is required"],
        )

    errors: list[str] = []
    try:
        validation_manifest = spcc.read_json(validation_manifest_path)
    except Exception as exc:
        return make_gate(
            "reviewed real-image validation source manifests",
            [f"could not read validation manifest for generated-entry comparison: {exc}"],
        )

    real_image_entries = entries_by_id(validation_manifest, "real_image_reference_sets", errors)
    external_entries = entries_by_id(validation_manifest, "external_comparison_outputs", errors)

    for path in paths:
        manifest_errors: list[str] = []
        try:
            build_spcc_real_image_validation_entries.validate_source_manifest(path, True, manifest_errors)
            if not manifest_errors:
                generated = build_spcc_real_image_validation_entries.build_manifest_entries(path, True)
                compare_generated_entries(
                    path,
                    "real_image_reference_sets",
                    generated["real_image_reference_sets"],
                    real_image_entries,
                    REAL_IMAGE_MANIFEST_KEYS,
                    manifest_errors,
                )
                compare_generated_entries(
                    path,
                    "external_comparison_outputs",
                    generated["external_comparison_outputs"],
                    external_entries,
                    EXTERNAL_COMPARISON_MANIFEST_KEYS,
                    manifest_errors,
                )
        except Exception as exc:
            manifest_errors.append(str(exc))
        errors.extend(f"{path}: {error}" for error in manifest_errors)
    return make_gate("reviewed real-image validation source manifests", errors)


def entries_by_id(
    validation_manifest: dict[str, Any],
    section: str,
    errors: list[str],
) -> dict[str, dict[str, Any]]:
    entries = validation_manifest.get(section)
    if not isinstance(entries, list):
        errors.append(f"validation manifest section is missing or not an array: {section}")
        return {}

    result: dict[str, dict[str, Any]] = {}
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            errors.append(f"validation manifest {section}[{index}] must be an object")
            continue
        entry_id = entry.get("id")
        if not isinstance(entry_id, str) or not entry_id.strip():
            continue
        result[entry_id] = entry
    return result


def compare_generated_entries(
    source_manifest_path: Path,
    section: str,
    generated_entries: list[dict[str, Any]],
    validation_entries: dict[str, dict[str, Any]],
    keys: list[str],
    errors: list[str],
) -> None:
    for generated_entry in generated_entries:
        entry_id = generated_entry["id"]
        validation_entry = validation_entries.get(entry_id)
        if validation_entry is None:
            errors.append(f"{section}.{entry_id} is missing from validation manifest")
            continue
        if project_manifest_entry(generated_entry, keys) != project_manifest_entry(validation_entry, keys):
            errors.append(
                f"{section}.{entry_id} in validation manifest does not match entries generated from "
                f"{source_manifest_path}"
            )


def project_manifest_entry(entry: dict[str, Any], keys: list[str]) -> dict[str, Any]:
    projected: dict[str, Any] = {}
    for key in keys:
        value = entry.get(key)
        if key in {"files", "output_files"} and isinstance(value, list):
            projected[key] = [
                {
                    "path": item.get("path"),
                    "role": item.get("role"),
                    "sha256": item.get("sha256"),
                }
                for item in value
                if isinstance(item, dict)
            ]
        else:
            projected[key] = value
    return projected


def make_gate(name: str, diagnostics: list[str], passed: bool | None = None) -> dict[str, Any]:
    return {
        "name": name,
        "passed": not diagnostics if passed is None else passed,
        "diagnostics": diagnostics,
    }


if __name__ == "__main__":
    raise SystemExit(main())
