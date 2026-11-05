#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import json
import shlex
import sys
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc
from tools import validate_spcc_catalog


STATUS_PRODUCTION_READY = "production_ready"
STATUS_BLOCKED_RIGHTS = "blocked_rights"
STATUS_INSUFFICIENT_SUPPORT = "insufficient_support"
STATUS_VALIDATION_MISSING = "validation_missing"
STATUS_VALIDATION_FAILED = "validation_failed"
STATUS_INVALID_CATALOG = "invalid_catalog"
VALIDATION_METRICS_SCHEMA_VERSION = "spcc-support-envelope-validation-v1"
GO_NO_GO_REPORT_SCHEMA_VERSION = "spcc-support-envelope-go-no-go-v1"
DEFAULT_TOLERANCE_VERSION = "spcc-support-envelope-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run SPCC sampled-XP support-envelope go/no-go checks for catalog candidates."
    )
    parser.add_argument("catalog_dirs", nargs="+", type=Path)
    parser.add_argument(
        "--validation-metrics",
        type=Path,
        help=(
            "JSON file with real-image or synthetic validation metrics. Expected schema: "
            "spcc-support-envelope-validation-v1 with a 'runs' array keyed by manifest_id."
        ),
    )
    parser.add_argument("--output-json", type=Path, help="Optional path for a machine-readable go/no-go report.")
    parser.add_argument(
        "--manifest-entry-json",
        type=Path,
        help=(
            "Optional path for a validation-data manifest support_envelope_reports entry. "
            "Requires --validation-metrics and --output-json so artifact checksums can be recorded."
        ),
    )
    parser.add_argument(
        "--manifest-entry-id",
        help="Optional ID for --manifest-entry-json; defaults to the catalog manifest IDs in the report.",
    )
    parser.add_argument(
        "--validation-command",
        help="Optional command string to record in --manifest-entry-json; defaults to the current command line.",
    )
    parser.add_argument(
        "--tolerance-version",
        default=DEFAULT_TOLERANCE_VERSION,
        help=f"Tolerance version recorded in --manifest-entry-json. Default: {DEFAULT_TOLERANCE_VERSION}.",
    )
    parser.add_argument("--sample-count", type=int, default=0, help="Catalog sample count passed to validation.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        validation_metrics = load_validation_metrics(args.validation_metrics)
        report = build_report(args.catalog_dirs, validation_metrics, args.sample_count)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    for result in report["results"]:
        print(format_result_line(result))
        for diagnostic in result["diagnostics"]:
            print(f"  - {diagnostic}")

    if args.output_json is not None:
        spcc.write_json(args.output_json, report)
    if args.manifest_entry_json is not None:
        if args.validation_metrics is None or args.output_json is None:
            print(
                "error: --manifest-entry-json requires --validation-metrics and --output-json",
                file=sys.stderr,
            )
            return 2
        entry = build_validation_manifest_entry(
            report=report,
            validation_metrics_path=args.validation_metrics,
            go_no_go_report_path=args.output_json,
            entry_id=args.manifest_entry_id,
            validation_command=args.validation_command or format_validation_command(sys.argv),
            tolerance_version=args.tolerance_version,
        )
        spcc.write_json(args.manifest_entry_json, entry)

    return 0 if report["all_production_ready"] else 1


def load_validation_metrics(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}

    data = spcc.read_json(path)
    if not isinstance(data, dict):
        raise ValueError("validation metrics must be a JSON object")
    if data.get("schema_version") != VALIDATION_METRICS_SCHEMA_VERSION:
        raise ValueError(
            f"validation metrics schema_version must be {VALIDATION_METRICS_SCHEMA_VERSION}"
        )
    runs = data.get("runs")
    if not isinstance(runs, list):
        raise ValueError("validation metrics must contain a runs array")

    metrics: dict[str, dict[str, Any]] = {}
    for index, run in enumerate(runs):
        if not isinstance(run, dict):
            raise ValueError(f"validation metrics run[{index}] must be an object")
        manifest_id = required_text(run, "manifest_id", f"validation metrics run[{index}]")
        if manifest_id in metrics:
            raise ValueError(f"duplicate validation metrics manifest_id: {manifest_id}")
        normalized = dict(run)
        normalized["manifest_id"] = manifest_id
        normalized["runtime_used_stars"] = required_non_negative_int(
            run, "runtime_used_stars", f"validation metrics run[{index}]"
        )
        normalized["validation_has_stable_gains"] = required_bool(
            run, "validation_has_stable_gains", f"validation metrics run[{index}]"
        )
        normalized["validation_residuals_acceptable"] = required_bool(
            run, "validation_residuals_acceptable", f"validation metrics run[{index}]"
        )
        metrics[manifest_id] = normalized
    return metrics


def required_text(run: dict[str, Any], key: str, label: str) -> str:
    value = run.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label} is missing required text field: {key}")
    return value.strip()


def required_bool(run: dict[str, Any], key: str, label: str) -> bool:
    value = run.get(key)
    if not isinstance(value, bool):
        raise ValueError(f"{label} field {key} must be a boolean")
    return value


def required_non_negative_int(run: dict[str, Any], key: str, label: str) -> int:
    value = run.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{label} field {key} must be a non-negative integer")
    return value


def build_report(catalog_dirs: list[Path], validation_metrics: dict[str, dict[str, Any]], sample_count: int) -> dict[str, Any]:
    results = [evaluate_catalog(catalog_dir, validation_metrics, sample_count) for catalog_dir in catalog_dirs]
    return {
        "schema_version": GO_NO_GO_REPORT_SCHEMA_VERSION,
        "generated_at_utc": spcc.utc_now_iso(),
        "support_envelope": {
            "minimum_estimated_catalog_candidates": spcc.SUPPORT_MIN_ESTIMATED_CANDIDATES,
            "minimum_runtime_used_stars": spcc.SUPPORT_MIN_RUNTIME_USED_STARS,
            "low_support_runtime_used_star_warning": spcc.SUPPORT_LOW_RUNTIME_USED_STAR_WARNING,
            "minimum_bp_rp_p10_p90_span": spcc.SUPPORT_MIN_BP_RP_P10_P90_SPAN,
            "approved_catalog_rights_statuses": sorted(spcc.APPROVED_CATALOG_RIGHTS_STATUSES),
        },
        "all_production_ready": all(result["status"] == STATUS_PRODUCTION_READY for result in results),
        "results": results,
    }


def evaluate_catalog(catalog_dir: Path, validation_metrics: dict[str, dict[str, Any]], sample_count: int) -> dict[str, Any]:
    diagnostics: list[str] = []
    validation_errors: list[str] = []
    validate_args = argparse.Namespace(
        catalog_dir=catalog_dir,
        sample_count=sample_count,
        gaia_validation_transcript=None,
        strict_production_review=False,
    )
    try:
        validate_spcc_catalog.validate_catalog(validate_args, validation_errors)
    except Exception as exc:
        validation_errors.append(str(exc))

    if validation_errors:
        return {
            "catalog_dir": str(catalog_dir),
            "manifest_id": "",
            "catalog_family_id": "",
            "access_profile": "",
            "rights_review_status": "",
            "status": STATUS_INVALID_CATALOG,
            "estimated_catalog_candidates": 0,
            "bp_rp_p10_p90_span": 0.0,
            "runtime_used_stars": None,
            "validation_has_stable_gains": False,
            "validation_residuals_acceptable": False,
            "diagnostics": validation_errors,
        }

    meta = spcc.read_json(catalog_dir / "catalog_meta.json")
    coverage = spcc.read_json(catalog_dir / meta["files"]["coverage_summary"])
    manifest_id = str(meta.get("manifest_id", ""))
    rights_review_status = str(meta.get("rights", {}).get("review_status", ""))
    access_profile = str(meta.get("access_profile", ""))
    estimated_candidates = int(coverage.get("estimated_usable_candidates", 0))
    bp_rp_span = float(coverage.get("bp_rp_p10_p90_span", 0.0))
    metrics = validation_metrics.get(manifest_id)

    runtime_used_stars = None
    stable_gains = False
    residuals_acceptable = False
    if metrics is not None:
        runtime_used_stars = metrics["runtime_used_stars"]
        stable_gains = metrics["validation_has_stable_gains"]
        residuals_acceptable = metrics["validation_residuals_acceptable"]

    status = STATUS_PRODUCTION_READY
    if rights_review_status not in spcc.APPROVED_CATALOG_RIGHTS_STATUSES:
        status = STATUS_BLOCKED_RIGHTS
        diagnostics.append(
            "catalog rights review is not approved for managed download or bundling; production SPCC remains blocked"
        )
    elif access_profile not in {"local-full", "installed-regional-cache"}:
        status = STATUS_INSUFFICIENT_SUPPORT
        diagnostics.append("catalog access profile is outside the v1 local-full/regional-cache support envelope")
    elif estimated_candidates < spcc.SUPPORT_MIN_ESTIMATED_CANDIDATES:
        status = STATUS_INSUFFICIENT_SUPPORT
        diagnostics.append(
            f"estimated usable candidates {estimated_candidates} is below "
            f"{spcc.SUPPORT_MIN_ESTIMATED_CANDIDATES}"
        )
    elif bp_rp_span < spcc.SUPPORT_MIN_BP_RP_P10_P90_SPAN:
        status = STATUS_INSUFFICIENT_SUPPORT
        diagnostics.append(
            f"BP-RP p10-p90 span {bp_rp_span:.3f} is below {spcc.SUPPORT_MIN_BP_RP_P10_P90_SPAN:.3f}"
        )
    elif metrics is None:
        status = STATUS_VALIDATION_MISSING
        diagnostics.append("no validation metrics were supplied for this manifest")
    elif runtime_used_stars is None or runtime_used_stars < spcc.SUPPORT_MIN_RUNTIME_USED_STARS:
        status = STATUS_INSUFFICIENT_SUPPORT
        diagnostics.append(
            f"runtime used stars {runtime_used_stars or 0} is below {spcc.SUPPORT_MIN_RUNTIME_USED_STARS}"
        )
    elif not stable_gains or not residuals_acceptable:
        status = STATUS_VALIDATION_FAILED
        diagnostics.append("validation metrics do not prove stable gains and acceptable residuals")

    if status == STATUS_PRODUCTION_READY and runtime_used_stars is not None:
        if runtime_used_stars < spcc.SUPPORT_LOW_RUNTIME_USED_STAR_WARNING:
            diagnostics.append(
                f"runtime support is production-pass but below comfort threshold "
                f"{spcc.SUPPORT_LOW_RUNTIME_USED_STAR_WARNING}; keep low-support diagnostics enabled"
            )
        else:
            diagnostics.append("sampled-XP support envelope passed for this catalog/validation metric set")

    return {
        "catalog_dir": str(catalog_dir),
        "manifest_id": manifest_id,
        "catalog_family_id": str(meta.get("catalog_family_id", "")),
        "access_profile": access_profile,
        "rights_review_status": rights_review_status,
        "status": status,
        "estimated_catalog_candidates": estimated_candidates,
        "bp_rp_p10_p90_span": bp_rp_span,
        "runtime_used_stars": runtime_used_stars,
        "validation_has_stable_gains": stable_gains,
        "validation_residuals_acceptable": residuals_acceptable,
        "diagnostics": diagnostics,
    }


def format_result_line(result: dict[str, Any]) -> str:
    return (
        f"{result['manifest_id'] or result['catalog_dir']}: {result['status']} "
        f"(estimated_candidates={result['estimated_catalog_candidates']}, "
        f"runtime_used_stars={result['runtime_used_stars']}, "
        f"bp_rp_span={result['bp_rp_p10_p90_span']:.3f})"
    )


def build_validation_manifest_entry(
    *,
    report: dict[str, Any],
    validation_metrics_path: Path,
    go_no_go_report_path: Path,
    entry_id: str | None,
    validation_command: str,
    tolerance_version: str,
) -> dict[str, Any]:
    results = report.get("results", [])
    manifest_ids = [
        str(result.get("manifest_id", "")).strip()
        for result in results
        if str(result.get("manifest_id", "")).strip()
    ]
    if not manifest_ids:
        raise ValueError("cannot build a manifest entry without at least one valid catalog manifest ID")

    resolved_entry_id = (entry_id or "").strip()
    if not resolved_entry_id:
        resolved_entry_id = "support-envelope-" + "-".join(manifest_ids)

    return {
        "id": resolved_entry_id,
        "status": support_envelope_entry_status(results),
        "catalog_family_id": common_or_mixed(result.get("catalog_family_id", "") for result in results),
        "catalog_manifest_ids": manifest_ids,
        "rights_review_status": common_or_mixed(result.get("rights_review_status", "") for result in results),
        "validation_metrics": {
            "path": artifact_path(validation_metrics_path),
            "sha256": spcc.sha256_file(validation_metrics_path),
            "schema_version": VALIDATION_METRICS_SCHEMA_VERSION,
        },
        "go_no_go_report": {
            "path": artifact_path(go_no_go_report_path),
            "sha256": spcc.sha256_file(go_no_go_report_path),
            "schema_version": GO_NO_GO_REPORT_SCHEMA_VERSION,
        },
        "expected_catalog_statuses": {
            str(result.get("manifest_id", "")).strip(): str(result.get("status", ""))
            for result in results
            if str(result.get("manifest_id", "")).strip()
        },
        "validation_command": validation_command,
        "tolerance_version": tolerance_version,
    }


def support_envelope_entry_status(results: list[dict[str, Any]]) -> str:
    statuses = {str(result.get("status", "")) for result in results}
    if statuses == {STATUS_PRODUCTION_READY}:
        return "production-ready"
    if STATUS_BLOCKED_RIGHTS in statuses:
        return "private-validation"
    if STATUS_INVALID_CATALOG in statuses:
        return "invalid-catalog"
    if STATUS_VALIDATION_MISSING in statuses:
        return "validation-missing"
    if STATUS_VALIDATION_FAILED in statuses:
        return "validation-failed"
    return "support-envelope-record"


def common_or_mixed(values: Any) -> str:
    normalized = sorted({str(value).strip() for value in values if str(value).strip()})
    if len(normalized) == 1:
        return normalized[0]
    return "mixed"


def artifact_path(path: Path) -> str:
    return path.as_posix()


def format_validation_command(argv: list[str]) -> str:
    return "python " + " ".join(shlex.quote(arg) for arg in argv)


if __name__ == "__main__":
    raise SystemExit(main())
