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


TOOL_VERSION = "spcc-response-repository-config-verifier-phase7-v1"
CONFIG_SCHEMA_VERSION = "spcc-response-repository-config-v1"
APPROVED_RESPONSE_RIGHTS_STATUSES = {
    "approved-for-bundling",
    "approved-for-managed-download",
}
PRODUCTION_ASSET_METADATA_FIELDS = [
    "license_name",
    "source_url",
    "source_citation",
    "attribution_text",
    "transformation_notes",
]
SUPPORTED_DEPLOYMENT_SCOPES = {
    "managed-cache",
    "deployed-runtime",
    "installer",
    "appimage",
}
SUPPORTED_PUBLICATION_CHANNELS = {
    "preview",
    "stable",
}
SUPPORTED_RELEASE_STATUSES = {
    "current",
    "superseded",
}
REQUIRED_UPDATE_POLICY = "immutable-versioned"


@dataclass(frozen=True)
class ResponseRepositoryDescriptor:
    descriptor_id: str
    repository_id: str
    version: str
    display_name: str
    manifest_url: str
    rights_review_status: str
    deployment_scope: str
    response_meta_sha256: str
    release_status: str
    recommended: bool
    superseded_by: str
    supersedes: tuple[str, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify an AfterNight SPCC response repository descriptor config and referenced assets."
    )
    parser.add_argument("repository_config", help="URL or local path to spcc_response_repositories.json")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--token-env", default="HF_TOKEN")
    parser.add_argument("--token-file", type=Path)
    parser.add_argument("--no-token", action="store_true")
    parser.add_argument("--allow-empty", action="store_true", help="Allow configs with no response_repositories.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []

    try:
        verified_count = verify_repository_config(args, errors)
    except Exception as exc:
        errors.append(str(exc))
        verified_count = 0

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"SPCC response repository config verification passed: {verified_count} descriptor(s)")
    return 0


def verify_repository_config(args: argparse.Namespace, errors: list[str]) -> int:
    token = None if args.no_token else hf_verify.load_token(args)
    config_url = hf_verify.normalize_url_or_path(args.repository_config)
    config = parse_json_bytes(
        hf_verify.fetch_bytes(config_url, token=token, timeout=args.timeout),
        "SPCC response repository config",
    )

    if config.get("schema_version") != CONFIG_SCHEMA_VERSION:
        errors.append(f"repository config schema_version must be {CONFIG_SCHEMA_VERSION}")
        return 0

    mirror_base_url = first_mirror_base_url(config)
    descriptor_values = config.get("response_repositories", [])
    if not isinstance(descriptor_values, list):
        errors.append("response_repositories must be a JSON array")
        return 0
    if not descriptor_values and not args.allow_empty:
        errors.append("response_repositories is empty; pass --allow-empty for intentional empty defaults")
        return 0

    descriptors: list[ResponseRepositoryDescriptor] = []
    seen_descriptor_ids: set[str] = set()
    seen_repository_versions: set[tuple[str, str]] = set()
    for index, descriptor_value in enumerate(descriptor_values):
        label = f"response_repositories[{index}]"
        if not isinstance(descriptor_value, dict):
            errors.append(f"{label}: descriptor must be a JSON object")
            continue

        descriptor = parse_descriptor(descriptor_value, mirror_base_url, label, errors)
        if descriptor is None:
            continue

        if descriptor.descriptor_id in seen_descriptor_ids:
            errors.append(f"{descriptor.descriptor_id}: duplicate descriptor id")
            continue
        seen_descriptor_ids.add(descriptor.descriptor_id)

        repository_version = (descriptor.repository_id, descriptor.version)
        if repository_version in seen_repository_versions:
            errors.append(f"{descriptor.descriptor_id}: duplicate repository_id/version descriptor")
            continue
        seen_repository_versions.add(repository_version)

        descriptors.append(descriptor)

    if descriptors:
        verify_publication_update_policy(config, descriptors, errors)
    if errors:
        return 0

    verified_count = 0
    for descriptor in descriptors:
        verify_descriptor_repository(descriptor=descriptor, token=token, timeout=args.timeout, errors=errors)
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
) -> ResponseRepositoryDescriptor | None:
    descriptor_id = str(descriptor.get("id", "")).strip()
    repository_id = str(descriptor.get("repository_id", "")).strip()
    version = str(descriptor.get("version", "")).strip()
    display_name = str(descriptor.get("display_name", "")).strip() or descriptor_id
    rights_review_status = str(descriptor.get("rights_review_status", "")).strip()
    if not rights_review_status and isinstance(descriptor.get("rights"), dict):
        rights_review_status = str(descriptor["rights"].get("review_status", "")).strip()
    deployment_scope = str(descriptor.get("deployment_scope", "managed-cache")).strip()
    response_meta_sha256 = str(descriptor.get("response_meta_sha256", "")).strip()
    manifest_url = descriptor_manifest_url(descriptor, mirror_base_url)
    release_status = str(descriptor.get("release_status", "")).strip()
    recommended_value = descriptor.get("recommended", False)
    recommended = recommended_value if isinstance(recommended_value, bool) else False
    superseded_by = str(descriptor.get("superseded_by", "")).strip()
    supersedes_value = descriptor.get("supersedes", [])
    supersedes: tuple[str, ...] = ()

    local_errors: list[str] = []
    if not descriptor_id:
        local_errors.append("id is required")
    if not repository_id:
        local_errors.append("repository_id is required")
    if not version:
        local_errors.append("version is required")
    if not manifest_url:
        local_errors.append("manifest_url or safe manifest_path is required")
    if rights_review_status not in APPROVED_RESPONSE_RIGHTS_STATUSES:
        local_errors.append("rights_review_status must be approved for managed response use")
    if deployment_scope not in SUPPORTED_DEPLOYMENT_SCOPES:
        local_errors.append("deployment_scope must be managed-cache, deployed-runtime, installer, or appimage")
    if response_meta_sha256 and not is_sha256(response_meta_sha256):
        local_errors.append("response_meta_sha256 must be a 64-character SHA-256 hex digest")
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

    if local_errors:
        errors.extend(f"{label}: {error}" for error in local_errors)
        return None

    return ResponseRepositoryDescriptor(
        descriptor_id=descriptor_id,
        repository_id=repository_id,
        version=version,
        display_name=display_name,
        manifest_url=manifest_url,
        rights_review_status=rights_review_status,
        deployment_scope=deployment_scope,
        response_meta_sha256=response_meta_sha256,
        release_status=release_status,
        recommended=recommended,
        superseded_by=superseded_by,
        supersedes=supersedes,
    )


def verify_publication_update_policy(
    config: dict[str, Any], descriptors: list[ResponseRepositoryDescriptor], errors: list[str]
) -> None:
    policy = config.get("publication_policy")
    if not isinstance(policy, dict):
        errors.append("publication_policy is required for non-empty response_repositories")
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

    descriptors_by_id = {descriptor.descriptor_id: descriptor for descriptor in descriptors}
    descriptors_by_channel: dict[tuple[str, str], list[ResponseRepositoryDescriptor]] = {}
    for descriptor in descriptors:
        key = (descriptor.repository_id, descriptor.deployment_scope)
        descriptors_by_channel.setdefault(key, []).append(descriptor)

        if descriptor.release_status == "current":
            if not descriptor.recommended:
                errors.append(f"{descriptor.descriptor_id}: current descriptor must be recommended")
            if descriptor.superseded_by:
                errors.append(f"{descriptor.descriptor_id}: current descriptor must not declare superseded_by")
        elif descriptor.release_status == "superseded":
            if descriptor.recommended:
                errors.append(f"{descriptor.descriptor_id}: superseded descriptor must not be recommended")
            if not descriptor.superseded_by:
                errors.append(f"{descriptor.descriptor_id}: superseded descriptor must declare superseded_by")

        for superseded_id in descriptor.supersedes:
            superseded = descriptors_by_id.get(superseded_id)
            if superseded is None:
                errors.append(f"{descriptor.descriptor_id}: supersedes references unknown descriptor {superseded_id}")
                continue
            if superseded.release_status != "superseded":
                errors.append(f"{descriptor.descriptor_id}: supersedes target must have release_status superseded")
            if (superseded.repository_id, superseded.deployment_scope) != (
                descriptor.repository_id,
                descriptor.deployment_scope,
            ):
                errors.append(f"{descriptor.descriptor_id}: supersedes target is from a different repository channel")
            if superseded.superseded_by and superseded.superseded_by != descriptor.descriptor_id:
                errors.append(f"{descriptor.descriptor_id}: supersedes target does not point back via superseded_by")

    for descriptor in descriptors:
        if descriptor.release_status != "superseded":
            continue
        replacement = descriptors_by_id.get(descriptor.superseded_by)
        if replacement is None:
            errors.append(f"{descriptor.descriptor_id}: superseded_by references unknown descriptor")
            continue
        if replacement.release_status != "current":
            errors.append(f"{descriptor.descriptor_id}: superseded_by must reference a current descriptor")
        if (replacement.repository_id, replacement.deployment_scope) != (
            descriptor.repository_id,
            descriptor.deployment_scope,
        ):
            errors.append(f"{descriptor.descriptor_id}: superseded_by references a different repository channel")

    for (repository_id, deployment_scope), channel_descriptors in descriptors_by_channel.items():
        current_descriptors = [
            descriptor for descriptor in channel_descriptors if descriptor.release_status == "current"
        ]
        if len(current_descriptors) != 1:
            errors.append(
                f"{repository_id}/{deployment_scope}: exactly one current descriptor is required per repository channel"
            )


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
    if not cleaned.endswith("response_meta.json"):
        return ""
    return cleaned


def verify_descriptor_repository(
    *,
    descriptor: ResponseRepositoryDescriptor,
    token: str | None,
    timeout: float,
    errors: list[str],
) -> None:
    meta_payload = hf_verify.fetch_bytes(descriptor.manifest_url, token=token, timeout=timeout)
    if descriptor.response_meta_sha256:
        actual_meta_sha = spcc.sha256_bytes(meta_payload)
        if actual_meta_sha != descriptor.response_meta_sha256:
            errors.append(f"{descriptor.descriptor_id}: response_meta_sha256 mismatch")
            return

    meta = parse_json_bytes(meta_payload, f"{descriptor.descriptor_id} response_meta.json")
    if meta.get("schema_version") != "spcc-response-repository-v1":
        errors.append(f"{descriptor.descriptor_id}: response_meta.json has an unexpected schema_version")
    if meta.get("repository_id") != descriptor.repository_id:
        errors.append(f"{descriptor.descriptor_id}: repository_id mismatch")
    if str(meta.get("version", "")) != descriptor.version:
        errors.append(f"{descriptor.descriptor_id}: version mismatch")

    rights = meta.get("rights", {})
    if not isinstance(rights, dict):
        errors.append(f"{descriptor.descriptor_id}: rights must be a JSON object")
        rights = {}
    rights_status = str(rights.get("review_status", ""))
    if rights_status != descriptor.rights_review_status:
        errors.append(f"{descriptor.descriptor_id}: rights_review_status mismatch")
    if rights_status not in APPROVED_RESPONSE_RIGHTS_STATUSES:
        errors.append(f"{descriptor.descriptor_id}: response_meta.json rights are not approved for managed use")
    if not str(rights.get("license_name", "")).strip():
        errors.append(f"{descriptor.descriptor_id}: response_meta.json rights missing license_name")

    deployment_scopes = set(value for value in meta.get("deployment_scopes", []) if isinstance(value, str))
    if descriptor.deployment_scope not in deployment_scopes:
        errors.append(f"{descriptor.descriptor_id}: deployment_scope is missing from response_meta.json")

    component_ids = verify_assets(
        descriptor=descriptor,
        assets=meta.get("components", []),
        id_key="component_id",
        asset_kind="component",
        token=token,
        timeout=timeout,
        errors=errors,
    )
    white_reference_ids = verify_assets(
        descriptor=descriptor,
        assets=meta.get("white_references", []),
        id_key="white_reference_id",
        asset_kind="white_reference",
        token=token,
        timeout=timeout,
        errors=errors,
    )
    verify_throughput_profiles(
        descriptor=descriptor,
        profiles=meta.get("throughput_profiles", []),
        component_ids=component_ids,
        white_reference_ids=white_reference_ids,
        errors=errors,
    )


def verify_assets(
    *,
    descriptor: ResponseRepositoryDescriptor,
    assets: Any,
    id_key: str,
    asset_kind: str,
    token: str | None,
    timeout: float,
    errors: list[str],
) -> set[str]:
    if not isinstance(assets, list) or not assets:
        errors.append(f"{descriptor.descriptor_id}: response_meta.json must list {asset_kind} assets")
        return set()

    asset_ids: set[str] = set()
    for index, asset_value in enumerate(assets):
        label = f"{descriptor.descriptor_id}: {asset_kind}[{index}]"
        if not isinstance(asset_value, dict):
            errors.append(f"{label}: asset entry must be a JSON object")
            continue

        asset_id = str(asset_value.get(id_key, "")).strip()
        relative_file = str(asset_value.get("file", "")).strip()
        expected_file_sha = str(asset_value.get("file_sha256", "")).strip()
        expected_data_sha = str(asset_value.get("data_sha256", "")).strip()
        if not asset_id:
            errors.append(f"{label}: {id_key} is required")
            continue
        if asset_id in asset_ids:
            errors.append(f"{label}: duplicate asset id {asset_id}")
            continue
        asset_ids.add(asset_id)
        if not relative_file:
            errors.append(f"{label}: file is required")
            continue
        if not is_safe_relative_file(relative_file):
            errors.append(f"{label}: file path is not a safe relative path")
            continue
        if not is_sha256(expected_file_sha):
            errors.append(f"{label}: file_sha256 must be a 64-character SHA-256 hex digest")
            continue
        if not is_sha256(expected_data_sha):
            errors.append(f"{label}: data_sha256 must be a 64-character SHA-256 hex digest")
            continue

        asset_url = hf_verify.resolve_relative(descriptor.manifest_url, relative_file)
        payload_bytes = hf_verify.fetch_bytes(asset_url, token=token, timeout=timeout)
        actual_file_sha = spcc.sha256_bytes(payload_bytes)
        if actual_file_sha != expected_file_sha:
            errors.append(f"{label}: file_sha256 mismatch for {relative_file}")
            continue

        payload = parse_json_bytes(payload_bytes, f"{asset_id} response asset")
        verify_asset_payload(
            label=label,
            payload=payload,
            id_key=id_key,
            asset_id=asset_id,
            asset_kind=asset_kind,
            expected_data_sha=expected_data_sha,
            errors=errors,
        )

    return asset_ids


def verify_asset_payload(
    *,
    label: str,
    payload: dict[str, Any],
    id_key: str,
    asset_id: str,
    asset_kind: str,
    expected_data_sha: str,
    errors: list[str],
) -> None:
    if payload.get("schema_version") != "spcc-response-asset-v1":
        errors.append(f"{label}: asset payload has an unexpected schema_version")
    if payload.get("asset_kind") != asset_kind:
        errors.append(f"{label}: asset_kind mismatch")
    if payload.get(id_key) != asset_id:
        errors.append(f"{label}: payload {id_key} mismatch")
    if payload.get("wavelength_unit") != "nm":
        errors.append(f"{label}: wavelength_unit must be nm")
    if not str(payload.get("value_semantics", "")).strip():
        errors.append(f"{label}: value_semantics is required")
    if payload.get("review_status") not in APPROVED_RESPONSE_RIGHTS_STATUSES:
        errors.append(f"{label}: review_status is not approved for managed use")
    for field in PRODUCTION_ASSET_METADATA_FIELDS:
        if not str(payload.get(field, "")).strip():
            errors.append(f"{label}: {field} is required")

    samples = payload.get("samples", [])
    try:
        spcc.validate_curve_samples(samples)
    except Exception as exc:
        errors.append(f"{label}: {exc}")
        return

    actual_data_sha = spcc.curve_data_sha256(samples)
    payload_data_sha = str(payload.get("data_sha256", "")).strip()
    if actual_data_sha != expected_data_sha or payload_data_sha != expected_data_sha:
        errors.append(f"{label}: data_sha256 mismatch")


def verify_throughput_profiles(
    *,
    descriptor: ResponseRepositoryDescriptor,
    profiles: Any,
    component_ids: set[str],
    white_reference_ids: set[str],
    errors: list[str],
) -> None:
    if not isinstance(profiles, list) or not profiles:
        errors.append(f"{descriptor.descriptor_id}: response_meta.json must list throughput_profiles")
        return

    profile_ids: set[str] = set()
    for index, profile_value in enumerate(profiles):
        label = f"{descriptor.descriptor_id}: throughput_profiles[{index}]"
        if not isinstance(profile_value, dict):
            errors.append(f"{label}: profile entry must be a JSON object")
            continue

        profile_id = str(profile_value.get("profile_id", "")).strip()
        if not profile_id:
            errors.append(f"{label}: profile_id is required")
            continue
        if profile_id in profile_ids:
            errors.append(f"{label}: duplicate profile id {profile_id}")
        profile_ids.add(profile_id)

        for component_id in profile_value.get("component_ids", []):
            if component_id not in component_ids:
                errors.append(f"{label}: references missing component {component_id}")
        white_reference_id = profile_value.get("white_reference_id")
        if white_reference_id not in white_reference_ids:
            errors.append(f"{label}: references missing white reference {white_reference_id}")
        if not profile_value.get("compatible_channel_bases"):
            errors.append(f"{label}: compatible_channel_bases is required")


def is_safe_relative_file(value: str) -> bool:
    normalized = value.replace("\\", "/").strip()
    if not normalized or normalized.startswith("/") or normalized.startswith("//"):
        return False
    parsed = urllib.parse.urlparse(normalized)
    if parsed.scheme or parsed.netloc or parsed.fragment:
        return False
    cleaned = posixpath.normpath(normalized)
    return not (cleaned in {".", ".."} or cleaned.startswith("../") or "/../" in cleaned)


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
