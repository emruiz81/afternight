#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate an AfterNight SPCC .anxp catalog directory.")
    parser.add_argument("catalog_dir", type=Path)
    parser.add_argument("--sample-count", type=int, default=0, help="Number of records to deeply inspect per chunk.")
    parser.add_argument(
        "--gaia-validation-transcript",
        type=Path,
        help="Optional captured Gaia/DataLink comparison transcript with source IDs expected in the catalog.",
    )
    parser.add_argument("--strict-production-review", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    try:
        validate_catalog(args, errors)
    except Exception as exc:
        errors.append(str(exc))

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"SPCC catalog validation passed: {args.catalog_dir}")
    return 0


def validate_catalog(args: argparse.Namespace, errors: list[str]) -> None:
    catalog_dir: Path = args.catalog_dir
    meta_path = catalog_dir / "catalog_meta.json"
    if not meta_path.exists():
        errors.append("missing catalog_meta.json")
        return

    meta = spcc.read_json(meta_path)
    validate_meta_schema(meta, args.strict_production_review, errors)
    if errors:
        return
    if should_enforce_local_full_coverage(meta, args.strict_production_review):
        errors.extend(spcc.local_full_tile_coverage_errors(meta))

    verify_checksum_file(catalog_dir, meta["files"]["chunk_manifest"], meta["chunk_manifest_sha256"], errors)
    verify_checksum_file(catalog_dir, meta["files"]["coverage_summary"], meta["coverage_summary_sha256"], errors)
    verify_checksum_file(catalog_dir, meta["files"]["source_id_index"], meta["source_id_index_sha256"], errors)
    verify_checksum_file(
        catalog_dir, meta["files"]["source_count_transcript"], meta["source_count_transcript_sha256"], errors
    )
    regional_request_file = meta.get("files", {}).get("regional_cache_request")
    regional_request_sha256 = meta.get("regional_cache_request_sha256")
    if regional_request_file or regional_request_sha256:
        if not regional_request_file or not regional_request_sha256:
            errors.append("regional_cache_request sidecar must declare both file and checksum")
        else:
            verify_checksum_file(catalog_dir, regional_request_file, regional_request_sha256, errors)

    chunk_manifest = spcc.read_json(catalog_dir / meta["files"]["chunk_manifest"])
    coverage = spcc.read_json(catalog_dir / meta["files"]["coverage_summary"])
    source_id_index = spcc.read_json(catalog_dir / meta["files"]["source_id_index"])
    validate_related_manifest(meta, chunk_manifest, "chunk_manifest", errors)
    validate_related_manifest(meta, coverage, "coverage_summary", errors)
    validate_related_manifest(meta, source_id_index, "source_id_index", errors)

    all_source_ids: set[str] = set()
    packed_record_hashes: dict[str, str] = {}
    total_records = 0
    for chunk in meta["chunks"]:
        chunk_path = catalog_dir / chunk["file"]
        if not chunk_path.exists():
            errors.append(f"missing chunk file: {chunk['file']}")
            continue
        actual_sha = spcc.sha256_file(chunk_path)
        if actual_sha != chunk["sha256"]:
            errors.append(f"chunk checksum mismatch for {chunk['file']}: expected {chunk['sha256']} got {actual_sha}")
            continue
        try:
            header, records = spcc.read_chunk(chunk_path)
        except Exception as exc:
            errors.append(str(exc))
            continue
        validate_chunk_header(meta, chunk, header, errors)
        if len(records) != chunk["record_count"]:
            errors.append(f"chunk {chunk['chunk_index']} record count mismatch")
        total_records += len(records)
        sample_limit = args.sample_count if args.sample_count > 0 else len(records)
        for record in records[:sample_limit]:
            validate_record(record, errors)
        all_source_ids.update(str(record["source_id"]) for record in records)
        packed_record_hashes.update(read_packed_record_hashes(chunk_path))

    if total_records != int(meta["total_records"]):
        errors.append(f"catalog total_records mismatch: expected {meta['total_records']} got {total_records}")

    indexed_sources = set(source_id_index.get("sources", {}).keys())
    if indexed_sources != all_source_ids:
        errors.append("source_id_index does not exactly match the chunk record source IDs")

    coverage_records = int(coverage.get("total_records", -1))
    if coverage_records != total_records:
        errors.append("coverage_summary total_records does not match chunk records")

    if args.gaia_validation_transcript is not None:
        validate_gaia_transcript(args.gaia_validation_transcript, all_source_ids, packed_record_hashes, errors)


def validate_meta_schema(meta: dict[str, Any], strict_production_review: bool, errors: list[str]) -> None:
    for key in [
        "schema_version",
        "catalog_family_id",
        "manifest_id",
        "access_profile",
        "rights",
        "provenance",
        "format",
        "wavelength_grid",
        "uncertainty_payload",
        "quality_fields",
        "chunks",
        "files",
    ]:
        if key not in meta:
            errors.append(f"catalog_meta.json missing required field: {key}")

    if errors:
        return

    if meta["catalog_family_id"] != spcc.CATALOG_FAMILY_ID:
        errors.append("catalog_meta.json has an unexpected catalog_family_id")
    if not meta["manifest_id"] or meta["manifest_id"] == meta["catalog_family_id"]:
        errors.append("catalog_meta.json manifest_id must be immutable and distinct from catalog_family_id")
    if meta["format"].get("magic") != spcc.ANXP_MAGIC.decode("ascii"):
        errors.append("catalog_meta.json format magic must be ANXP")
    if int(meta["format"].get("record_size_bytes", 0)) != spcc.ANXP_RECORD_SIZE_BYTES:
        errors.append("catalog_meta.json record_size_bytes must be 1536")
    grid = meta["wavelength_grid"]
    if int(grid.get("sample_count", 0)) != spcc.SAMPLED_XP_SAMPLE_COUNT:
        errors.append("wavelength_grid sample_count does not match sampled XP")
    if meta["uncertainty_payload"].get("flux_error_samples") != "present":
        errors.append("uncertainty_payload must declare present flux_error samples or an explicit fallback")

    quality = meta["quality_fields"]
    for field in spcc.REQUIRED_GAIA_SOURCE_FIELDS:
        if field not in quality.get("required_gaia_source_fields", []):
            errors.append(f"quality_fields missing Gaia source field: {field}")
    for field in spcc.REQUIRED_XP_SUMMARY_FIELDS:
        if field not in quality.get("required_xp_summary_fields", []):
            errors.append(f"quality_fields missing XP summary field: {field}")
    for field in spcc.REQUIRED_SAMPLED_SPECTRUM_FIELDS:
        if field not in quality.get("required_sampled_spectrum_fields", []):
            errors.append(f"quality_fields missing sampled spectrum field: {field}")

    review_status = meta["rights"].get("review_status", "")
    if not review_status:
        errors.append("rights.review_status is required")
    if strict_production_review and review_status not in {
        "approved-for-managed-download",
        "approved-for-bundling",
    }:
        errors.append("strict production validation requires approved SPCC catalog rights review")


def should_enforce_local_full_coverage(meta: dict[str, Any], strict_production_review: bool) -> bool:
    if meta.get("access_profile") != "local-full":
        return False
    if meta.get("source_kind") == "synthetic_fixture":
        return False
    review_status = meta.get("rights", {}).get("review_status", "")
    return strict_production_review or review_status in spcc.APPROVED_CATALOG_RIGHTS_STATUSES


def verify_checksum_file(catalog_dir: Path, relative_file: str, expected_sha: str, errors: list[str]) -> None:
    path = catalog_dir / relative_file
    if not path.exists():
        errors.append(f"missing manifest sidecar: {relative_file}")
        return
    actual_sha = spcc.sha256_file(path)
    if actual_sha != expected_sha:
        errors.append(f"sidecar checksum mismatch for {relative_file}: expected {expected_sha} got {actual_sha}")


def validate_related_manifest(
    meta: dict[str, Any], manifest: dict[str, Any], manifest_name: str, errors: list[str]
) -> None:
    if manifest.get("catalog_family_id") != meta["catalog_family_id"]:
        errors.append(f"{manifest_name} catalog_family_id mismatch")
    if manifest.get("manifest_id") != meta["manifest_id"]:
        errors.append(f"{manifest_name} manifest_id mismatch")


def validate_chunk_header(
    meta: dict[str, Any], chunk: dict[str, Any], header: dict[str, Any], errors: list[str]
) -> None:
    if header.get("catalog_family_id") != meta["catalog_family_id"]:
        errors.append(f"chunk {chunk['chunk_index']} catalog_family_id mismatch")
    if header.get("manifest_id") != meta["manifest_id"]:
        errors.append(f"chunk {chunk['chunk_index']} manifest_id mismatch")
    if int(header.get("record_size_bytes", 0)) != spcc.ANXP_RECORD_SIZE_BYTES:
        errors.append(f"chunk {chunk['chunk_index']} record_size_bytes mismatch")
    if int(header.get("chunk_index", -1)) != int(chunk["chunk_index"]):
        errors.append(f"chunk {chunk['chunk_index']} header index mismatch")


def validate_record(record: dict[str, Any], errors: list[str]) -> None:
    if int(record["source_id"]) <= 0:
        errors.append("SPCC record has an invalid source_id")
    if not (0.0 <= float(record["ra"]) < 360.0):
        errors.append(f"SPCC record {record['source_id']} has invalid RA")
    if not (-90.0 <= float(record["dec"]) <= 90.0):
        errors.append(f"SPCC record {record['source_id']} has invalid Dec")
    if int(record["valid_sample_count"]) != spcc.SAMPLED_XP_SAMPLE_COUNT:
        errors.append(f"SPCC record {record['source_id']} has an invalid wavelength sample count")
    if len(record["flux"]) != spcc.SAMPLED_XP_SAMPLE_COUNT or len(record["flux_error"]) != spcc.SAMPLED_XP_SAMPLE_COUNT:
        errors.append(f"SPCC record {record['source_id']} has invalid spectrum payload lengths")
    if any(not math.isfinite(float(value)) for value in record["flux"]):
        errors.append(f"SPCC record {record['source_id']} has non-finite flux")
    if any((not math.isfinite(float(value))) or float(value) < 0.0 for value in record["flux_error"]):
        errors.append(f"SPCC record {record['source_id']} has invalid flux_error")


def read_packed_record_hashes(chunk_path: Path) -> dict[str, str]:
    hashes: dict[str, str] = {}
    with chunk_path.open("rb") as handle:
        preamble = handle.read(spcc.CHUNK_PREAMBLE_STRUCT.size)
        if len(preamble) != spcc.CHUNK_PREAMBLE_STRUCT.size:
            return hashes
        _, _, _, header_size, record_count = spcc.CHUNK_PREAMBLE_STRUCT.unpack(preamble)
        handle.seek(header_size, 1)
        for _ in range(record_count):
            payload = handle.read(spcc.ANXP_RECORD_SIZE_BYTES)
            if len(payload) != spcc.ANXP_RECORD_SIZE_BYTES:
                return hashes
            source_id = str(spcc.unpack_record(payload)["source_id"])
            hashes[source_id] = spcc.sha256_bytes(payload)
    return hashes


def validate_gaia_transcript(
    path: Path, source_ids: set[str], packed_record_hashes: dict[str, str], errors: list[str]
) -> None:
    transcript = spcc.read_json(path)
    tap_service_url = transcript.get("tap", {}).get("service_url", "")
    if tap_service_url and "gea.esac.esa.int" not in tap_service_url:
        errors.append("Gaia validation transcript must identify the official Gaia TAP service")

    expected_sources = {str(source["source_id"]) for source in transcript.get("sources", [])}
    missing = expected_sources - source_ids
    if missing:
        errors.append(f"Gaia validation transcript sources missing from catalog: {sorted(missing)[:5]}")

    for source in transcript.get("sources", []):
        source_id = str(source["source_id"])
        if source.get("flux_sample_count") not in (None, spcc.SAMPLED_XP_SAMPLE_COUNT):
            errors.append(f"Gaia validation transcript source {source_id} has the wrong flux sample count")
        if source.get("flux_error_sample_count") not in (None, spcc.SAMPLED_XP_SAMPLE_COUNT):
            errors.append(f"Gaia validation transcript source {source_id} has the wrong flux-error sample count")

        expected_hash = source.get("packed_record_sha256")
        if expected_hash and packed_record_hashes.get(source_id) != expected_hash:
            errors.append(f"Gaia validation transcript packed record hash mismatch for source {source_id}")

        quality_fields = source.get("tap_quality_fields", {})
        if quality_fields and str(quality_fields.get("has_xp_sampled", "")).lower() not in {"true", "1"}:
            errors.append(f"Gaia validation transcript source {source_id} is not TAP-marked has_xp_sampled")


if __name__ == "__main__":
    raise SystemExit(main())
