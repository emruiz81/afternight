#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc
from tools import validate_spcc_response_repository


TOOL_VERSION = "spcc-response-repository-dataset-builder-phase7-v1"
DEFAULT_REPO_ID = "emruiz81/afternight-spcc-responses"
DEFAULT_REVISION = "main"
APPROVED_RESPONSE_RIGHTS_STATUSES = {"approved-for-managed-download", "approved-for-bundling"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stage validated SPCC response repositories into a hosted dataset-ready directory."
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("response_repository_dirs", type=Path, nargs="+")
    parser.add_argument("--repo-id", default=DEFAULT_REPO_ID)
    parser.add_argument("--revision", default=DEFAULT_REVISION)
    parser.add_argument("--dataset-title", default="AfterNight SPCC Response Repositories")
    parser.add_argument("--publication-channel", choices=["preview", "stable"], default="preview")
    parser.add_argument("--deployment-scope", default="managed-cache")
    parser.add_argument("--direct-base-url", default="")
    parser.add_argument(
        "--validation-manifest-entries-json",
        type=Path,
        help="Optional path for ready-to-merge validation manifest response_repositories entries.",
    )
    parser.add_argument("--no-validate", action="store_true")
    parser.add_argument("--clean", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = build_dataset(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"SPCC response repository dataset staged: {args.output_dir}")
    print(f"Response repositories: {len(manifest['response_repositories'])}")
    print(f"Upload: hf upload {args.repo_id} {args.output_dir} . --repo-type dataset --revision {args.revision}")
    return 0


def build_dataset(args: argparse.Namespace) -> dict[str, Any]:
    output_dir: Path = args.output_dir
    if output_dir.exists():
        if not args.clean:
            raise ValueError(f"{output_dir} already exists; pass --clean to replace it")
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    generated_at_utc = spcc.utc_now_iso()
    direct_base_url = args.direct_base_url.strip().rstrip("/") or (
        f"https://huggingface.co/datasets/{args.repo_id}/resolve/{args.revision}"
    )

    entries: list[dict[str, Any]] = []
    used_paths: set[str] = set()
    for repository_dir in args.response_repository_dirs:
        repository_dir = repository_dir.resolve()
        meta_path = repository_dir / "response_meta.json"
        if not meta_path.exists():
            raise ValueError(f"{repository_dir} does not contain response_meta.json")
        meta = spcc.read_json(meta_path)
        validate_source_repository(repository_dir, args)
        enforce_managed_publication_gate(meta)

        relative_path = response_repository_dataset_path(meta)
        if relative_path in used_paths:
            raise ValueError(f"duplicate response repository dataset path: {relative_path}")
        used_paths.add(relative_path)

        destination = output_dir / Path(relative_path)
        copy_response_repository_artifacts(repository_dir, destination, meta)
        validate_source_repository(destination, args)
        entries.append(build_repository_entry(destination, relative_path, meta, args, direct_base_url))

    apply_release_status(entries)

    config = build_descriptor_config(args, generated_at_utc, direct_base_url, entries)
    spcc.write_json(output_dir / "spcc_response_repositories.json", config)

    dataset_manifest = {
        "schema_version": "spcc-response-dataset-manifest-v1",
        "dataset_title": args.dataset_title,
        "dataset_repo_id": args.repo_id,
        "revision": args.revision,
        "publication_channel": args.publication_channel,
        "generated_at_utc": generated_at_utc,
        "generator": "build_spcc_response_repository_dataset.py",
        "generator_version": TOOL_VERSION,
        "direct_download_base_url": direct_base_url,
        "upload_command": (
            f"hf upload {args.repo_id} {output_dir.as_posix()} . --repo-type dataset --revision {args.revision}"
        ),
        "response_repositories": entries,
    }
    spcc.write_json(output_dir / "spcc_response_dataset_manifest.json", dataset_manifest)
    if args.validation_manifest_entries_json is not None:
        spcc.write_json(
            args.validation_manifest_entries_json,
            build_validation_manifest_entries(args, entries, output_dir),
        )

    write_dataset_card(output_dir / "README.md", args, direct_base_url, entries)
    write_gitattributes(output_dir / ".gitattributes")
    return dataset_manifest


def build_validation_manifest_entries(
    args: argparse.Namespace,
    entries: list[dict[str, Any]],
    output_dir: Path,
) -> dict[str, Any]:
    return {
        "schema_version": "spcc-response-validation-manifest-entries-v1",
        "generated_at_utc": spcc.utc_now_iso(),
        "validation_command": (
            "python tools/validate_spcc_response_repository.py <repository-dir> --runtime-root && "
            "python tools/validate_spcc_response_repository.py <repository-dir> --strict-production-review"
        ),
        "response_repositories": [
            build_validation_manifest_entry(args, entry, output_dir)
            for entry in sorted(entries, key=lambda value: value["id"])
        ],
    }


def build_validation_manifest_entry(
    args: argparse.Namespace,
    entry: dict[str, Any],
    output_dir: Path,
) -> dict[str, Any]:
    response_meta_path = output_dir / entry["manifest_path"]
    return {
        "id": entry["id"],
        "repository_id": entry["repository_id"],
        "version": entry["version"],
        "generator": "tools/normalize_spcc_response_repository.py",
        "validation_command": (
            "python tools/validate_spcc_response_repository.py <repository-dir> --runtime-root && "
            "python tools/validate_spcc_response_repository.py <repository-dir> --strict-production-review"
        ),
        "license": entry["license_name"],
        "rights_review_status": entry["rights_review_status"],
        "response_meta": {
            "path": entry["manifest_path"],
            "sha256": spcc.sha256_file(response_meta_path),
            "schema_version": "spcc-response-repository-v1",
        },
        "descriptor_config": {
            "path": "spcc_response_repositories.json",
            "sha256": spcc.sha256_file(output_dir / "spcc_response_repositories.json"),
            "schema_version": "spcc-response-repository-config-v1",
        },
        "expected_metrics": {
            "minimum_components": 1,
            "minimum_white_references": 1,
            "minimum_throughput_profiles": 1,
            "component_count": entry["component_count"],
            "white_reference_count": entry["white_reference_count"],
            "throughput_profile_count": entry["throughput_profile_count"],
            "deployment_scope": args.deployment_scope,
        },
        "tolerance_version": "spcc-response-repository-v1",
    }


def validate_source_repository(repository_dir: Path, args: argparse.Namespace) -> None:
    if args.no_validate:
        return
    errors: list[str] = []
    validate_args = argparse.Namespace(
        repository_dir=repository_dir,
        runtime_root=False,
        strict_production_review=True,
    )
    validate_spcc_response_repository.validate_repository(validate_args, errors)
    if errors:
        joined = "\n  - ".join(errors)
        raise ValueError(f"SPCC response repository validation failed for {repository_dir}:\n  - {joined}")


def enforce_managed_publication_gate(meta: dict[str, Any]) -> None:
    rights = meta.get("rights", {})
    review_status = rights.get("review_status", "")
    if review_status not in APPROVED_RESPONSE_RIGHTS_STATUSES:
        raise ValueError(
            "managed response repository publication requires rights.review_status to be "
            "approved-for-managed-download or approved-for-bundling"
        )


def response_repository_dataset_path(meta: dict[str, Any]) -> str:
    repository_id = safe_path_segment(str(meta["repository_id"]), "repository_id")
    version = safe_path_segment(str(meta["version"]), "version")
    return f"responses/{repository_id}/{version}"


def safe_path_segment(value: str, field_name: str) -> str:
    if not value:
        raise ValueError(f"{field_name} is required")
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
    if any(char not in allowed for char in value):
        raise ValueError(f"{field_name} contains characters unsafe for a dataset path: {value}")
    return value


def copy_response_repository_artifacts(source_dir: Path, destination_dir: Path, meta: dict[str, Any]) -> None:
    destination_dir.mkdir(parents=True, exist_ok=True)
    files_to_copy = {"response_meta.json"}
    for asset in list(meta.get("components", [])) + list(meta.get("white_references", [])):
        relative_file = str(asset.get("file", "")).strip()
        if not relative_file:
            raise ValueError("response asset is missing file")
        files_to_copy.add(relative_file)

    for relative_file in sorted(files_to_copy):
        source_file = source_dir / relative_file
        if not source_file.is_file():
            raise ValueError(f"response repository artifact is missing: {source_file}")
        destination_file = destination_dir / relative_file
        destination_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, destination_file)


def build_repository_entry(
    destination_dir: Path,
    relative_path: str,
    meta: dict[str, Any],
    args: argparse.Namespace,
    direct_base_url: str,
) -> dict[str, Any]:
    files = sorted(path for path in destination_dir.rglob("*") if path.is_file())
    rights = meta.get("rights", {})
    repository_id = str(meta["repository_id"])
    version = str(meta["version"])
    descriptor_id = f"{repository_id}-{version}"
    total_size_bytes = sum(path.stat().st_size for path in files)
    display_name = response_repository_display_name(repository_id, version)
    return {
        "id": descriptor_id,
        "repository_id": repository_id,
        "version": version,
        "display_name": display_name,
        "description": response_repository_description(display_name, rights),
        "path": relative_path,
        "manifest_path": f"{relative_path}/response_meta.json",
        "manifest_url": f"{direct_base_url}/{relative_path}/response_meta.json",
        "response_meta_sha256": spcc.sha256_file(destination_dir / "response_meta.json"),
        "license_name": str(rights.get("license_name", "")),
        "rights_review_status": str(rights.get("review_status", "")),
        "deployment_scope": args.deployment_scope,
        "download_size_bytes": total_size_bytes,
        "installed_size_bytes": total_size_bytes,
        "component_count": len(meta.get("components", [])),
        "white_reference_count": len(meta.get("white_references", [])),
        "throughput_profile_count": len(meta.get("throughput_profiles", [])),
        "file_count": len(files),
    }


def response_repository_display_name(repository_id: str, version: str) -> str:
    if repository_id == "afternight-spcc-response-baseline":
        return f"AfterNight SPCC Response Baseline {version}"
    return f"{repository_id} {version}"


def response_repository_description(display_name: str, rights: dict[str, Any]) -> str:
    return f"Reviewed SPCC response repository {display_name}."


def apply_release_status(entries: list[dict[str, Any]]) -> None:
    by_channel: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for entry in entries:
        by_channel.setdefault((entry["repository_id"], entry["deployment_scope"]), []).append(entry)

    for channel_entries in by_channel.values():
        channel_entries.sort(key=lambda entry: str(entry["version"]))
        current = channel_entries[-1]
        superseded_ids: list[str] = []
        for entry in channel_entries[:-1]:
            entry["release_status"] = "superseded"
            entry["recommended"] = False
            entry["superseded_by"] = current["id"]
            superseded_ids.append(entry["id"])
        current["release_status"] = "current"
        current["recommended"] = True
        if superseded_ids:
            current["supersedes"] = superseded_ids


def build_descriptor_config(
    args: argparse.Namespace,
    generated_at_utc: str,
    direct_base_url: str,
    entries: list[dict[str, Any]],
) -> dict[str, Any]:
    descriptors = []
    for entry in sorted(entries, key=lambda value: (value["repository_id"], value["version"])):
        descriptor = {
            "id": entry["id"],
            "repository_id": entry["repository_id"],
            "version": entry["version"],
            "display_name": entry["display_name"],
            "description": entry["description"],
            "manifest_path": entry["manifest_path"],
            "rights_review_status": entry["rights_review_status"],
            "deployment_scope": entry["deployment_scope"],
            "response_meta_sha256": entry["response_meta_sha256"],
            "download_size_bytes": entry["download_size_bytes"],
            "installed_size_bytes": entry["installed_size_bytes"],
            "release_status": entry["release_status"],
            "recommended": entry["recommended"],
        }
        if "superseded_by" in entry:
            descriptor["superseded_by"] = entry["superseded_by"]
        if "supersedes" in entry:
            descriptor["supersedes"] = entry["supersedes"]
        descriptors.append(descriptor)

    return {
        "schema_version": "spcc-response-repository-config-v1",
        "publication_policy": {
            "channel": args.publication_channel,
            "published_at_utc": generated_at_utc,
            "update_policy": "immutable-versioned",
        },
        "mirrors": [direct_base_url],
        "response_repositories": descriptors,
    }


def write_dataset_card(path: Path, args: argparse.Namespace, direct_base_url: str, entries: list[dict[str, Any]]) -> None:
    rows = [
        "| Repository ID | Version | Status | Profiles | Size | Rights | Path |",
        "|---|---|---|---:|---:|---|---|",
    ]
    for entry in sorted(entries, key=lambda value: (value["repository_id"], value["version"])):
        rows.append(
            "| {repo} | {version} | {status} | {profiles:,} | {size} | {rights} | `{path}` |".format(
                repo=entry["repository_id"],
                version=entry["version"],
                status=entry["release_status"],
                profiles=entry["throughput_profile_count"],
                size=format_bytes(entry["installed_size_bytes"]),
                rights=entry["rights_review_status"],
                path=entry["path"],
            )
        )

    card = f"""---
pretty_name: {args.dataset_title}
license: other
tags:
- astronomy
- spectrophotometry
- afternight
---

# {args.dataset_title}

This dataset contains reviewed AfterNight Spectrophotometric Color Calibration response repositories staged for direct
HTTPS download.

Publication channel: `{args.publication_channel}`

Direct download base: `{direct_base_url}`

## Response Repositories

{chr(10).join(rows)}

## Descriptor Config

The app-facing descriptor config is `spcc_response_repositories.json`. It uses immutable versioned updates: old
descriptors remain addressable as `superseded`, and exactly one current descriptor is recommended for each repository
and deployment-scope channel.

## Upload

```bash
hf upload {args.repo_id} <this-directory> . --repo-type dataset --revision {args.revision}
```

## Verify

```bash
python tools/verify_spcc_response_repository_config.py {direct_base_url}/spcc_response_repositories.json
```
"""
    path.write_text(card, encoding="utf-8")


def write_gitattributes(path: Path) -> None:
    path.write_text(
        "*.json text eol=lf\n"
        "*.md text eol=lf\n",
        encoding="utf-8",
    )


def format_bytes(value: int) -> str:
    units = ["B", "KiB", "MiB", "GiB", "TiB"]
    amount = float(value)
    for unit in units:
        if amount < 1024.0 or unit == units[-1]:
            return f"{amount:.1f} {unit}" if unit != "B" else f"{int(amount)} B"
        amount /= 1024.0
    return f"{value} B"


if __name__ == "__main__":
    raise SystemExit(main())
