#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import json
import posixpath
import sys
import urllib.parse
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc
from tools import verify_spcc_hf_dataset as hf_verify


TOOL_VERSION = "spcc-catalog-mirror-config-verifier-phase7-v1"
CONFIG_SCHEMA_VERSION = "spcc-catalog-mirror-config-v1"
SUPPORTED_ACCESS_PROFILES = {"local-full", "installed-regional-cache"}
SUPPORTED_PUBLICATION_CHANNELS = {"preview", "stable"}
SUPPORTED_RELEASE_STATUSES = {"current", "superseded"}
REQUIRED_UPDATE_POLICY = "immutable-versioned"
APPROVED_DESCRIPTOR_RIGHTS_STATUSES = {
    "approved-public",
    "approved-for-managed-download",
    "approved-for-bundling",
}
REQUIRED_SIDECARS = {
    "chunk_manifest": "chunk_manifest_sha256",
    "coverage_summary": "coverage_summary_sha256",
    "source_id_index": "source_id_index_sha256",
    "source_count_transcript": "source_count_transcript_sha256",
}


@dataclass(frozen=True)
class CatalogDescriptor:
    manifest_id: str
    display_name: str
    manifest_url: str
    distribution_type: str
    catalog_family_id: str
    access_profile: str
    rights_review_status: str
    catalog_meta_sha256: str
    release_status: str
    recommended: bool
    superseded_by: str
    supersedes: tuple[str, ...]
    has_region_coverage: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify an AfterNight SPCC catalog mirror config and its referenced catalog manifests."
    )
    parser.add_argument("mirror_config", help="URL or local path to spcc_catalog_mirrors.json")
    parser.add_argument(
        "--chunk-policy",
        choices=["none", "head", "first", "all"],
        default="head",
        help="How aggressively to verify .anxp chunks referenced by descriptor manifests.",
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--max-chunk-bytes",
        type=int,
        default=64 * 1024 * 1024,
        help="Safety cap for checksum downloads; use 0 for no cap.",
    )
    parser.add_argument("--token-env", default="HF_TOKEN")
    parser.add_argument("--token-file", type=Path)
    parser.add_argument("--no-token", action="store_true")
    parser.add_argument(
        "--base-url-override",
        help=(
            "Use this base URL or local directory for descriptor manifest_path resolution while keeping the "
            "mirror config bytes unchanged. Useful for verifying a staged dataset before upload."
        ),
    )
    parser.add_argument("--allow-empty", action="store_true", help="Allow configs that contain no catalog_packs.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    warnings: list[str] = []

    try:
        verified_count = verify_mirror_config(args, errors, warnings)
    except Exception as exc:
        errors.append(str(exc))
        verified_count = 0

    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"SPCC catalog mirror config verification passed: {verified_count} descriptor(s)")
    return 0


def verify_mirror_config(args: argparse.Namespace, errors: list[str], warnings: list[str]) -> int:
    token = None if args.no_token else hf_verify.load_token(args)
    config_url = hf_verify.normalize_url_or_path(args.mirror_config)
    config = parse_json_bytes(
        hf_verify.fetch_bytes(config_url, token=token, timeout=args.timeout), "SPCC catalog mirror config"
    )

    if config.get("schema_version") != CONFIG_SCHEMA_VERSION:
        errors.append(f"mirror config schema_version must be {CONFIG_SCHEMA_VERSION}")
        return 0

    mirror_base_url = (
        hf_verify.normalize_url_or_path(getattr(args, "base_url_override", "")).rstrip("/")
        if getattr(args, "base_url_override", "")
        else first_mirror_base_url(config)
    )
    descriptor_values = config.get("catalog_packs", [])
    if not isinstance(descriptor_values, list):
        errors.append("catalog_packs must be a JSON array")
        return 0
    if not descriptor_values and not args.allow_empty:
        errors.append("catalog_packs is empty; pass --allow-empty for intentional empty defaults")
        return 0

    descriptors: list[CatalogDescriptor] = []
    seen_manifest_ids: set[str] = set()
    for index, descriptor_value in enumerate(descriptor_values):
        label = f"catalog_packs[{index}]"
        if not isinstance(descriptor_value, dict):
            errors.append(f"{label}: descriptor must be a JSON object")
            continue
        descriptor = parse_descriptor(descriptor_value, mirror_base_url, label, errors)
        if descriptor is None:
            continue
        if descriptor.manifest_id in seen_manifest_ids:
            errors.append(f"{descriptor.manifest_id}: duplicate descriptor id")
            continue
        seen_manifest_ids.add(descriptor.manifest_id)
        descriptors.append(descriptor)

    if descriptors:
        verify_publication_update_policy(config, descriptors, errors)
    if errors:
        return 0

    verified_count = 0
    for descriptor in descriptors:
        verify_descriptor_manifest(
            descriptor=descriptor,
            token=token,
            timeout=args.timeout,
            chunk_policy=args.chunk_policy,
            max_chunk_bytes=args.max_chunk_bytes,
            errors=errors,
            warnings=warnings,
        )
        verified_count += 1

    return verified_count


def first_mirror_base_url(config: dict[str, Any]) -> str:
    mirrors = config.get("mirrors", [])
    if not isinstance(mirrors, list):
        return ""
    for mirror in mirrors:
        if isinstance(mirror, str) and mirror.strip():
            return hf_verify.normalize_url_or_path(mirror.strip()).rstrip("/")
    return ""


def parse_descriptor(
    descriptor: dict[str, Any], mirror_base_url: str, label: str, errors: list[str]
) -> CatalogDescriptor | None:
    manifest_id = str(descriptor.get("id", "")).strip()
    display_name = str(descriptor.get("display_name", "")).strip() or manifest_id
    distribution_type = str(descriptor.get("distribution_type", "")).strip()
    catalog_family_id = str(descriptor.get("catalog_family_id", "")).strip()
    access_profile = str(descriptor.get("access_profile", "")).strip()
    rights_review_status = str(descriptor.get("rights_review_status", "")).strip()
    if not rights_review_status and isinstance(descriptor.get("rights"), dict):
        rights_review_status = str(descriptor["rights"].get("review_status", "")).strip()
    catalog_meta_sha256 = str(descriptor.get("catalog_meta_sha256", "")).strip()
    manifest_url = descriptor_manifest_url(descriptor, mirror_base_url)
    release_status = str(descriptor.get("release_status", "")).strip()
    recommended_value = descriptor.get("recommended", False)
    recommended = recommended_value if isinstance(recommended_value, bool) else False
    superseded_by = str(descriptor.get("superseded_by", "")).strip()
    supersedes_value = descriptor.get("supersedes", [])
    supersedes: tuple[str, ...] = ()

    local_errors: list[str] = []
    if not manifest_id:
        local_errors.append("id is required")
    elif not safe_descriptor_id(manifest_id):
        local_errors.append("id must be a safe path segment")
    if not manifest_url:
        local_errors.append("manifest_url or safe manifest_path is required")
    if distribution_type.lower() != "chunks":
        local_errors.append("distribution_type must be chunks")
    if catalog_family_id != spcc.CATALOG_FAMILY_ID:
        local_errors.append(f"catalog_family_id must be {spcc.CATALOG_FAMILY_ID}")
    if access_profile not in SUPPORTED_ACCESS_PROFILES:
        local_errors.append("access_profile must be local-full or installed-regional-cache")
    if rights_review_status not in APPROVED_DESCRIPTOR_RIGHTS_STATUSES:
        local_errors.append("rights_review_status must be approved for managed use")
    if catalog_meta_sha256 and not is_sha256(catalog_meta_sha256):
        local_errors.append("catalog_meta_sha256 must be a 64-character SHA-256 hex digest")
    if release_status not in SUPPORTED_RELEASE_STATUSES:
        local_errors.append("release_status must be current or superseded")
    if not isinstance(recommended_value, bool):
        local_errors.append("recommended must be a boolean")
    if "supersedes" in descriptor:
        if not isinstance(supersedes_value, list):
            local_errors.append("supersedes must be an array of descriptor IDs")
        else:
            parsed_supersedes = tuple(str(value).strip() for value in supersedes_value if str(value).strip())
            if len(parsed_supersedes) != len(supersedes_value):
                local_errors.append("supersedes entries must be non-empty descriptor IDs")
            supersedes = parsed_supersedes

    has_region_coverage = has_valid_region_coverage(descriptor.get("coverage"))
    if access_profile == "installed-regional-cache" and not has_region_coverage:
        local_errors.append("installed-regional-cache descriptors require valid coverage")

    if local_errors:
        errors.extend(f"{label}: {error}" for error in local_errors)
        return None

    return CatalogDescriptor(
        manifest_id=manifest_id,
        display_name=display_name,
        manifest_url=manifest_url,
        distribution_type=distribution_type,
        catalog_family_id=catalog_family_id,
        access_profile=access_profile,
        rights_review_status=rights_review_status,
        catalog_meta_sha256=catalog_meta_sha256,
        release_status=release_status,
        recommended=recommended,
        superseded_by=superseded_by,
        supersedes=supersedes,
        has_region_coverage=has_region_coverage,
    )


def verify_publication_update_policy(
    config: dict[str, Any], descriptors: list[CatalogDescriptor], errors: list[str]
) -> None:
    policy = config.get("publication_policy")
    if not isinstance(policy, dict):
        errors.append("publication_policy is required for non-empty catalog_packs")
        return

    channel = str(policy.get("channel", "")).strip()
    if channel not in SUPPORTED_PUBLICATION_CHANNELS:
        errors.append("publication_policy.channel must be stable or preview")

    update_policy = str(policy.get("update_policy", "")).strip()
    if update_policy != REQUIRED_UPDATE_POLICY:
        errors.append(f"publication_policy.update_policy must be {REQUIRED_UPDATE_POLICY}")

    published_at_utc = str(policy.get("published_at_utc", "")).strip()
    if not is_utc_timestamp(published_at_utc):
        errors.append("publication_policy.published_at_utc must be an ISO-8601 UTC timestamp")

    descriptors_by_id = {descriptor.manifest_id: descriptor for descriptor in descriptors}
    for descriptor in descriptors:
        if descriptor.release_status == "current":
            if not descriptor.recommended:
                errors.append(f"{descriptor.manifest_id}: current descriptor must be recommended")
            if descriptor.superseded_by:
                errors.append(f"{descriptor.manifest_id}: current descriptor must not declare superseded_by")
        elif descriptor.release_status == "superseded":
            if descriptor.recommended:
                errors.append(f"{descriptor.manifest_id}: superseded descriptor must not be recommended")
            if not descriptor.superseded_by:
                errors.append(f"{descriptor.manifest_id}: superseded descriptor must declare superseded_by")

        for superseded_id in descriptor.supersedes:
            superseded = descriptors_by_id.get(superseded_id)
            if superseded is None:
                errors.append(f"{descriptor.manifest_id}: supersedes references unknown descriptor {superseded_id}")
                continue
            if superseded.release_status != "superseded":
                errors.append(f"{descriptor.manifest_id}: supersedes target must have release_status superseded")
            if (superseded.catalog_family_id, superseded.access_profile) != (
                descriptor.catalog_family_id,
                descriptor.access_profile,
            ):
                errors.append(f"{descriptor.manifest_id}: supersedes target is from a different catalog channel")
            if superseded.superseded_by and superseded.superseded_by != descriptor.manifest_id:
                errors.append(f"{descriptor.manifest_id}: supersedes target does not point back via superseded_by")

        if descriptor.release_status != "superseded":
            continue
        replacement = descriptors_by_id.get(descriptor.superseded_by)
        if replacement is None:
            errors.append(f"{descriptor.manifest_id}: superseded_by references unknown descriptor")
            continue
        if replacement.release_status != "current":
            errors.append(f"{descriptor.manifest_id}: superseded_by must reference a current descriptor")
        if (replacement.catalog_family_id, replacement.access_profile) != (
            descriptor.catalog_family_id,
            descriptor.access_profile,
        ):
            errors.append(f"{descriptor.manifest_id}: superseded_by references a different catalog channel")


def descriptor_manifest_url(descriptor: dict[str, Any], mirror_base_url: str) -> str:
    manifest_url = str(descriptor.get("manifest_url", "")).strip()
    if manifest_url:
        parsed = urllib.parse.urlparse(manifest_url)
        if parsed.scheme and not parsed.fragment:
            return manifest_url
        return ""

    manifest_path = safe_manifest_path(str(descriptor.get("manifest_path", "")).strip())
    if not manifest_path or not mirror_base_url:
        return ""
    return hf_verify.resolve_relative(mirror_base_url.rstrip("/") + "/", manifest_path)


def safe_manifest_path(value: str) -> str:
    normalized = value.replace("\\", "/").strip()
    if not normalized or normalized.startswith("/") or normalized.startswith("//"):
        return ""
    parsed = urllib.parse.urlparse(normalized)
    if parsed.scheme or parsed.netloc:
        return ""
    cleaned = posixpath.normpath(normalized)
    if cleaned in {".", ".."} or cleaned.startswith("../") or "/../" in cleaned:
        return ""
    if not cleaned.endswith("catalog_meta.json"):
        return ""
    return cleaned


def safe_descriptor_id(value: str) -> bool:
    normalized = value.strip()
    if not normalized or "/" in normalized or "\\" in normalized:
        return False
    parsed = urllib.parse.urlparse(normalized)
    if parsed.scheme or parsed.netloc or parsed.fragment or parsed.query:
        return False
    cleaned = posixpath.normpath(normalized)
    return cleaned not in {".", ".."} and not cleaned.startswith("../") and cleaned == normalized


def has_valid_region_coverage(value: Any) -> bool:
    if not isinstance(value, dict):
        return False
    try:
        center_ra = float(value["center_ra_deg"])
        center_dec = float(value["center_dec_deg"])
        radius = float(value["radius_deg"])
        magnitude_limit = float(value["magnitude_limit"])
    except (KeyError, TypeError, ValueError):
        return False
    return (
        0.0 <= center_ra < 360.0
        and -90.0 <= center_dec <= 90.0
        and radius > 0.0
        and magnitude_limit > 0.0
    )


def verify_descriptor_manifest(
    *,
    descriptor: CatalogDescriptor,
    token: str | None,
    timeout: float,
    chunk_policy: str,
    max_chunk_bytes: int,
    errors: list[str],
    warnings: list[str],
) -> None:
    meta_payload = hf_verify.fetch_bytes(descriptor.manifest_url, token=token, timeout=timeout)
    if descriptor.catalog_meta_sha256:
        actual_meta_sha = spcc.sha256_bytes(meta_payload)
        if actual_meta_sha != descriptor.catalog_meta_sha256:
            errors.append(f"{descriptor.manifest_id}: catalog_meta_sha256 mismatch")
            return

    meta = parse_json_bytes(meta_payload, f"{descriptor.manifest_id} catalog_meta.json")
    if meta.get("schema_version") != "spcc-catalog-meta-v1":
        errors.append(f"{descriptor.manifest_id}: catalog_meta.json has an unexpected schema_version")
    if meta.get("catalog_family_id") != descriptor.catalog_family_id:
        errors.append(f"{descriptor.manifest_id}: catalog_family_id mismatch")
    if meta.get("manifest_id") != descriptor.manifest_id:
        errors.append(f"{descriptor.manifest_id}: manifest_id mismatch")
    if meta.get("access_profile") != descriptor.access_profile:
        errors.append(f"{descriptor.manifest_id}: access_profile mismatch")
    rights_status = str(meta.get("rights", {}).get("review_status", ""))
    if rights_status != descriptor.rights_review_status:
        errors.append(f"{descriptor.manifest_id}: rights_review_status mismatch")
    if rights_status not in APPROVED_DESCRIPTOR_RIGHTS_STATUSES:
        errors.append(f"{descriptor.manifest_id}: catalog_meta.json rights are not approved for managed use")

    total_records = int(meta.get("total_records", 0))
    if total_records <= 0:
        errors.append(f"{descriptor.manifest_id}: total_records must be positive")
    chunks = meta.get("chunks", [])
    if not isinstance(chunks, list) or not chunks:
        errors.append(f"{descriptor.manifest_id}: catalog_meta.json does not list chunks")
        return

    coverage = verify_sidecars(descriptor.manifest_url, meta, descriptor.manifest_id, token, timeout, errors)
    if descriptor.access_profile == "installed-regional-cache" and coverage is not None:
        if coverage.get("access_profile") != descriptor.access_profile:
            errors.append(f"{descriptor.manifest_id}: coverage_summary access_profile mismatch")
        if int(coverage.get("total_records", -1)) != total_records:
            errors.append(f"{descriptor.manifest_id}: coverage_summary total_records mismatch")

    verify_chunks(
        descriptor=descriptor,
        chunks=chunks,
        token=token,
        timeout=timeout,
        chunk_policy=chunk_policy,
        max_chunk_bytes=max_chunk_bytes,
        errors=errors,
        warnings=warnings,
    )


def verify_sidecars(
    manifest_url: str,
    meta: dict[str, Any],
    manifest_id: str,
    token: str | None,
    timeout: float,
    errors: list[str],
) -> dict[str, Any] | None:
    files = meta.get("files", {})
    coverage_payload: bytes | None = None
    for file_key, checksum_key in REQUIRED_SIDECARS.items():
        relative_file = str(files.get(file_key, ""))
        expected_sha = str(meta.get(checksum_key, ""))
        if not relative_file or not expected_sha:
            errors.append(f"{manifest_id}: missing sidecar declaration for {file_key}")
            continue
        payload = hf_verify.fetch_bytes(hf_verify.resolve_relative(manifest_url, relative_file), token=token, timeout=timeout)
        actual_sha = spcc.sha256_bytes(payload)
        if actual_sha != expected_sha:
            errors.append(f"{manifest_id}: sidecar checksum mismatch for {relative_file}")
        if file_key == "coverage_summary":
            coverage_payload = payload

    if coverage_payload is None:
        return None
    coverage = parse_json_bytes(coverage_payload, f"{manifest_id} coverage_summary.json")
    if coverage.get("schema_version") != "spcc-coverage-summary-v1":
        errors.append(f"{manifest_id}: coverage_summary has an unexpected schema_version")
    if coverage.get("catalog_family_id") != meta.get("catalog_family_id"):
        errors.append(f"{manifest_id}: coverage_summary catalog_family_id mismatch")
    if coverage.get("manifest_id") != meta.get("manifest_id"):
        errors.append(f"{manifest_id}: coverage_summary manifest_id mismatch")
    return coverage


def verify_chunks(
    *,
    descriptor: CatalogDescriptor,
    chunks: list[Any],
    token: str | None,
    timeout: float,
    chunk_policy: str,
    max_chunk_bytes: int,
    errors: list[str],
    warnings: list[str],
) -> None:
    if chunk_policy == "none":
        return
    chunks_to_check = chunks[:1] if chunk_policy in {"head", "first"} else chunks
    for chunk in chunks_to_check:
        if not isinstance(chunk, dict):
            errors.append(f"{descriptor.manifest_id}: chunk entry is not a JSON object")
            continue
        chunk_file = str(chunk.get("file", ""))
        expected_size = int(chunk.get("size_bytes", 0))
        expected_sha = str(chunk.get("sha256", ""))
        if not chunk_file or expected_size <= 0:
            errors.append(f"{descriptor.manifest_id}: chunk entry is missing file or size_bytes")
            continue
        chunk_url = hf_verify.resolve_relative(descriptor.manifest_url, chunk_file)
        if chunk_policy == "head":
            hf_verify.verify_head(chunk_url, expected_size, token, timeout, descriptor.manifest_id, errors, warnings)
            continue
        if max_chunk_bytes > 0 and expected_size > max_chunk_bytes:
            warnings.append(
                f"{descriptor.manifest_id}: skipped checksum download for {chunk_file}; "
                f"{expected_size} bytes exceeds --max-chunk-bytes"
            )
            hf_verify.verify_head(chunk_url, expected_size, token, timeout, descriptor.manifest_id, errors, warnings)
            continue
        payload = hf_verify.fetch_bytes(chunk_url, token=token, timeout=timeout)
        if len(payload) != expected_size:
            errors.append(f"{descriptor.manifest_id}: chunk size mismatch for {chunk_file}")
        actual_sha = spcc.sha256_bytes(payload)
        if expected_sha and actual_sha != expected_sha:
            errors.append(f"{descriptor.manifest_id}: chunk checksum mismatch for {chunk_file}")


def parse_json_bytes(payload: bytes, label: str) -> dict[str, Any]:
    try:
        data = json.loads(payload.decode("utf-8"))
    except Exception as exc:
        raise ValueError(f"could not parse {label}: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError(f"{label} is not a JSON object")
    return data


def is_sha256(value: str) -> bool:
    return len(value) == 64 and all(char in "0123456789abcdefABCDEF" for char in value)


def is_utc_timestamp(value: str) -> bool:
    if not value.endswith("Z"):
        return False
    try:
        datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        return False
    return True


if __name__ == "__main__":
    raise SystemExit(main())
