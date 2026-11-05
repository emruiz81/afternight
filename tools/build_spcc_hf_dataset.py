#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc
from tools import validate_spcc_catalog


TOOL_VERSION = "spcc-hf-dataset-builder-phase3a-v1"
DEFAULT_REPO_ID = "emruiz81/afternight-spcc-catalogs"
DEFAULT_REVISION = "main"
HF_TERMS_URL = "https://huggingface.co/terms-of-service"
HF_STORAGE_DOCS_URL = "https://huggingface.co/docs/hub/storage-limits"
APPROVED_REVIEW_STATUSES = {"approved-for-managed-download", "approved-for-bundling"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stage validated SPCC .anxp catalog packs into a Hugging Face dataset-ready directory."
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("catalog_dirs", type=Path, nargs="+")
    parser.add_argument("--repo-id", default=DEFAULT_REPO_ID)
    parser.add_argument("--revision", default=DEFAULT_REVISION)
    parser.add_argument(
        "--direct-base-url",
        default="",
        help="Override the direct download base URL written into manifests and app descriptor config.",
    )
    parser.add_argument("--dataset-title", default="AfterNight SPCC Catalogs")
    parser.add_argument(
        "--publication-mode",
        choices=["private-staging", "public-production"],
        default="private-staging",
        help="public-production requires approved catalog rights review and approved mirror hosting policy.",
    )
    parser.add_argument(
        "--mirror-hosting-policy",
        choices=["unknown", "staging-only", "approved-public", "blocked"],
        default=None,
        help="Recorded host-policy state for the generated remote mirror manifest.",
    )
    parser.add_argument("--sample-count", type=int, default=3)
    parser.add_argument("--strict-production-review", action="store_true")
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

    print(f"SPCC Hugging Face dataset staged: {args.output_dir}")
    print(f"Catalogs: {len(manifest['catalogs'])}")
    print(f"Upload: hf upload {args.repo_id} {args.output_dir} . --repo-type dataset --revision {args.revision}")
    return 0


def build_dataset(args: argparse.Namespace) -> dict[str, Any]:
    output_dir: Path = args.output_dir
    print_dataset_context(args)
    if output_dir.exists():
        if not args.clean:
            raise ValueError(f"{output_dir} already exists; pass --clean to replace it")
        print(f"[setup] removing existing output directory: {output_dir}", flush=True)
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)
    print(f"[setup] output directory ready: {output_dir}", flush=True)

    generated_at_utc = spcc.utc_now_iso()
    mirror_policy = args.mirror_hosting_policy
    if mirror_policy is None:
        mirror_policy = "approved-public" if args.publication_mode == "public-production" else "staging-only"

    strict_review = args.strict_production_review or args.publication_mode == "public-production"
    direct_base_url = args.direct_base_url.strip().rstrip("/")
    if not direct_base_url:
        direct_base_url = f"https://huggingface.co/datasets/{args.repo_id}/resolve/{args.revision}"
    repo_url = f"https://huggingface.co/datasets/{args.repo_id}"

    catalog_entries: list[dict[str, Any]] = []
    used_paths: set[str] = set()
    for catalog_index, catalog_dir in enumerate(args.catalog_dirs, start=1):
        catalog_started_at = time.perf_counter()
        catalog_dir = catalog_dir.resolve()
        print(f"[catalog {catalog_index}/{len(args.catalog_dirs)}] reading {catalog_dir}", flush=True)
        meta_path = catalog_dir / "catalog_meta.json"
        if not meta_path.exists():
            raise ValueError(f"{catalog_dir} does not contain catalog_meta.json")
        meta = spcc.read_json(meta_path)
        print(
            f"[catalog {catalog_index}/{len(args.catalog_dirs)}] manifest_id={meta.get('manifest_id')} "
            f"records={meta.get('total_records')} chunks={len(meta.get('chunks', []))} "
            f"rights={meta.get('rights', {}).get('review_status', '')}",
            flush=True,
        )
        enforce_catalog_readiness_gate(meta)

        if not args.no_validate:
            validate_started_at = time.perf_counter()
            print(f"[catalog {catalog_index}/{len(args.catalog_dirs)}] validating source catalog", flush=True)
            validate_catalog_dir(catalog_dir, args.sample_count, strict_review)
            print(
                f"[catalog {catalog_index}/{len(args.catalog_dirs)}] source validation complete "
                f"elapsed={format_duration(time.perf_counter() - validate_started_at)}",
                flush=True,
            )

        enforce_publication_gate(meta, args.publication_mode, mirror_policy)

        relative_path = catalog_dataset_path(meta)
        if relative_path in used_paths:
            raise ValueError(f"duplicate dataset path generated for catalog: {relative_path}")
        used_paths.add(relative_path)

        destination = output_dir / Path(relative_path)
        copy_catalog_artifacts(catalog_dir, destination, meta, catalog_index, len(args.catalog_dirs))

        if not args.no_validate:
            validate_started_at = time.perf_counter()
            print(f"[catalog {catalog_index}/{len(args.catalog_dirs)}] validating staged catalog", flush=True)
            validate_catalog_dir(destination, args.sample_count, strict_review)
            print(
                f"[catalog {catalog_index}/{len(args.catalog_dirs)}] staged validation complete "
                f"elapsed={format_duration(time.perf_counter() - validate_started_at)}",
                flush=True,
            )

        catalog_entries.append(build_catalog_entry(destination, relative_path, meta, direct_base_url))
        print(
            f"[catalog {catalog_index}/{len(args.catalog_dirs)}] staged {relative_path} "
            f"elapsed={format_duration(time.perf_counter() - catalog_started_at)}",
            flush=True,
        )

    print("[manifest] writing dataset and mirror manifests", flush=True)
    dataset_manifest = {
        "schema_version": "spcc-hf-dataset-manifest-v1",
        "dataset_title": args.dataset_title,
        "dataset_repo_id": args.repo_id,
        "dataset_repo_url": repo_url,
        "revision": args.revision,
        "publication_mode": args.publication_mode,
        "generated_at_utc": generated_at_utc,
        "generator": "build_spcc_hf_dataset.py",
        "generator_version": TOOL_VERSION,
        "direct_download_base_url": direct_base_url,
        "host_terms_url": HF_TERMS_URL,
        "host_storage_policy_url": HF_STORAGE_DOCS_URL,
        "upload_command": (
            f"hf upload {args.repo_id} {output_dir.as_posix()} . --repo-type dataset --revision {args.revision}"
        ),
        "catalogs": catalog_entries,
    }
    spcc.write_json(output_dir / "spcc_dataset_manifest.json", dataset_manifest)

    mirror_manifest = build_mirror_manifest(
        args=args,
        generated_at_utc=generated_at_utc,
        direct_base_url=direct_base_url,
        mirror_policy=mirror_policy,
        catalog_entries=catalog_entries,
    )
    spcc.write_json(output_dir / "spcc_mirror_manifest.json", mirror_manifest)
    spcc.write_json(
        output_dir / "spcc_catalog_mirrors.json",
        build_app_mirror_config(
            generated_at_utc=generated_at_utc,
            direct_base_url=direct_base_url,
            publication_channel="stable" if args.publication_mode == "public-production" else "preview",
            catalog_entries=catalog_entries,
        ),
    )

    write_dataset_card(output_dir / "README.md", args, direct_base_url, mirror_policy, catalog_entries)
    write_gitattributes(output_dir / ".gitattributes")
    print("[manifest] dataset card and .gitattributes written", flush=True)
    return dataset_manifest


def print_dataset_context(args: argparse.Namespace) -> None:
    print("SPCC Hugging Face dataset staging", flush=True)
    print(f"  output: {args.output_dir}", flush=True)
    print(f"  catalogs: {len(args.catalog_dirs)}", flush=True)
    print(f"  repo_id: {args.repo_id}", flush=True)
    print(f"  revision: {args.revision}", flush=True)
    print(f"  publication_mode: {args.publication_mode}", flush=True)
    print(f"  mirror_hosting_policy: {args.mirror_hosting_policy or 'auto'}", flush=True)
    print(f"  sample_count: {args.sample_count}", flush=True)
    print(f"  validation: {'disabled' if args.no_validate else 'enabled'}", flush=True)


def validate_catalog_dir(catalog_dir: Path, sample_count: int, strict_review: bool) -> None:
    errors: list[str] = []
    validate_args = argparse.Namespace(
        catalog_dir=catalog_dir,
        sample_count=sample_count,
        gaia_validation_transcript=None,
        strict_production_review=strict_review,
    )
    validate_spcc_catalog.validate_catalog(validate_args, errors)
    if errors:
        joined = "\n  - ".join(errors)
        raise ValueError(f"SPCC catalog validation failed for {catalog_dir}:\n  - {joined}")


def enforce_publication_gate(meta: dict[str, Any], publication_mode: str, mirror_policy: str) -> None:
    if publication_mode != "public-production":
        return

    rights = meta.get("rights", {})
    review_status = rights.get("review_status", "")
    if review_status not in APPROVED_REVIEW_STATUSES:
        raise ValueError(
            "public-production SPCC datasets require rights.review_status to be "
            "approved-for-managed-download or approved-for-bundling"
        )
    catalog_mirror_policy = rights.get("mirror_hosting_policy", mirror_policy)
    if catalog_mirror_policy != "approved-public" or mirror_policy != "approved-public":
        raise ValueError("public-production SPCC datasets require approved-public mirror hosting policy")


def enforce_catalog_readiness_gate(meta: dict[str, Any]) -> None:
    review_status = meta.get("rights", {}).get("review_status", "")
    if review_status not in APPROVED_REVIEW_STATUSES:
        return
    coverage_errors = spcc.local_full_tile_coverage_errors(meta)
    if not coverage_errors:
        return
    joined = "\n  - ".join(coverage_errors)
    raise ValueError(f"approved local-full SPCC catalog is not ready for managed download:\n  - {joined}")


def catalog_dataset_path(meta: dict[str, Any]) -> str:
    family = safe_path_segment(str(meta["catalog_family_id"]), "catalog_family_id")
    access_profile = safe_path_segment(str(meta.get("access_profile", "local-full")), "access_profile")
    manifest_id = safe_path_segment(str(meta["manifest_id"]), "manifest_id")
    return f"{family}/{access_profile}/{manifest_id}"


def safe_path_segment(value: str, field_name: str) -> str:
    if not value:
        raise ValueError(f"{field_name} is required")
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")
    if any(char not in allowed for char in value):
        raise ValueError(f"{field_name} contains characters unsafe for a dataset path: {value}")
    return value


def copy_catalog_artifacts(
    source_dir: Path, destination_dir: Path, meta: dict[str, Any], catalog_index: int, catalog_total: int
) -> None:
    destination_dir.mkdir(parents=True, exist_ok=True)
    files_to_copy = {"catalog_meta.json"}
    files_to_copy.update(str(path) for path in meta.get("files", {}).values())
    files_to_copy.update(str(chunk["file"]) for chunk in meta.get("chunks", []))

    sorted_files = sorted(files_to_copy)
    total_bytes = sum((source_dir / relative_file).stat().st_size for relative_file in sorted_files if (source_dir / relative_file).is_file())
    copied_bytes = 0
    copy_started_at = time.perf_counter()
    print(
        f"[catalog {catalog_index}/{catalog_total}] copying {len(sorted_files)} files "
        f"total={format_bytes(total_bytes)}",
        flush=True,
    )
    for file_index, relative_file in enumerate(sorted_files, start=1):
        source_file = source_dir / relative_file
        if not source_file.is_file():
            raise ValueError(f"catalog artifact is missing: {source_file}")
        destination_file = destination_dir / relative_file
        destination_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, destination_file)
        copied_bytes += source_file.stat().st_size
        print(
            f"[catalog {catalog_index}/{catalog_total}] copied {file_index}/{len(sorted_files)} "
            f"{relative_file} ({format_bytes(copied_bytes)}/{format_bytes(total_bytes)}) "
            f"elapsed={format_duration(time.perf_counter() - copy_started_at)}",
            flush=True,
        )


def build_catalog_entry(
    destination_dir: Path, relative_path: str, meta: dict[str, Any], direct_base_url: str
) -> dict[str, Any]:
    files = sorted(path for path in destination_dir.rglob("*") if path.is_file())
    chunk_size_bytes = sum(int(chunk.get("size_bytes", 0)) for chunk in meta.get("chunks", []))
    total_size_bytes = sum(path.stat().st_size for path in files)
    rights = meta.get("rights", {})
    review_status = rights.get("review_status", "")
    mirror_hosting_policy = rights.get(
        "mirror_hosting_policy", "staging-only" if review_status == "private-local" else "unknown"
    )
    entry = {
        "catalog_family_id": meta["catalog_family_id"],
        "manifest_id": meta["manifest_id"],
        "access_profile": meta.get("access_profile", ""),
        "source_kind": meta.get("source_kind", ""),
        "path": relative_path,
        "manifest_file": f"{relative_path}/catalog_meta.json",
        "manifest_url": f"{direct_base_url}/{relative_path}/catalog_meta.json",
        "catalog_meta_sha256": spcc.sha256_file(destination_dir / "catalog_meta.json"),
        "rights_review_status": review_status,
        "mirror_hosting_policy": mirror_hosting_policy,
        "total_records": int(meta.get("total_records", 0)),
        "chunk_count": len(meta.get("chunks", [])),
        "chunk_size_bytes": chunk_size_bytes,
        "total_size_bytes": total_size_bytes,
        "file_count": len(files),
    }
    if isinstance(meta.get("coverage"), dict):
        entry["coverage"] = meta["coverage"]
    return entry


def format_duration(seconds: float) -> str:
    seconds = max(0, int(round(seconds)))
    if seconds < 60:
        return f"{seconds}s"
    minutes, seconds = divmod(seconds, 60)
    if minutes < 60:
        return f"{minutes}m{seconds:02d}s"
    hours, minutes = divmod(minutes, 60)
    if hours < 48:
        return f"{hours}h{minutes:02d}m"
    days, hours = divmod(hours, 24)
    return f"{days}d{hours:02d}h"


def format_bytes(value: int) -> str:
    size = float(value)
    for suffix in ["B", "KB", "MB", "GB", "TB"]:
        if size < 1024.0 or suffix == "TB":
            return f"{size:.1f}{suffix}" if suffix != "B" else f"{int(size)}B"
        size /= 1024.0
    return f"{size:.1f}TB"


def build_mirror_manifest(
    *,
    args: argparse.Namespace,
    generated_at_utc: str,
    direct_base_url: str,
    mirror_policy: str,
    catalog_entries: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema_version": "spcc-remote-mirror-manifest-v1",
        "mirror_id": f"huggingface:{args.repo_id}@{args.revision}",
        "mirror_host": "huggingface",
        "mirror_base_url": direct_base_url,
        "mirror_host_terms_url": HF_TERMS_URL,
        "mirror_hosting_policy": mirror_policy,
        "publication_timestamp_utc": generated_at_utc,
        "range_request_support": "not-required-for-v1-full-file-download",
        "cache_key_policy": "catalog_family_id + manifest_id + access_profile + catalog_meta_sha256",
        "redirect_policy": "HTTPS direct download URLs only; redirects must not downgrade to HTTP.",
        "catalogs": [
            {
                "catalog_family_id": entry["catalog_family_id"],
                "upstream_manifest_id": entry["manifest_id"],
                "access_profile": entry["access_profile"],
                "upstream_catalog_meta_sha256": entry["catalog_meta_sha256"],
                "manifest_url": entry["manifest_url"],
                "cache_key": (
                    f"{entry['catalog_family_id']}:{entry['manifest_id']}:"
                    f"{entry['access_profile']}:{entry['catalog_meta_sha256']}"
                ),
                **({"coverage": entry["coverage"]} if "coverage" in entry else {}),
            }
            for entry in catalog_entries
        ],
    }


def build_app_mirror_config(
    *,
    generated_at_utc: str,
    direct_base_url: str,
    publication_channel: str,
    catalog_entries: list[dict[str, Any]],
) -> dict[str, Any]:
    catalog_packs = [
        build_app_catalog_descriptor(entry)
        for entry in catalog_entries
        if entry["rights_review_status"] in APPROVED_REVIEW_STATUSES
    ]
    config = {
        "schema_version": "spcc-catalog-mirror-config-v1",
        "generated_at_utc": generated_at_utc,
        "generator": "build_spcc_hf_dataset.py",
        "generator_version": TOOL_VERSION,
        "mirrors": [direct_base_url],
        "catalog_packs": catalog_packs,
    }
    if catalog_packs:
        config["publication_policy"] = {
            "channel": publication_channel,
            "published_at_utc": generated_at_utc,
            "update_policy": "immutable-versioned",
        }
    return config


def build_app_catalog_descriptor(entry: dict[str, Any]) -> dict[str, Any]:
    descriptor = {
        "id": entry["manifest_id"],
        "display_name": catalog_display_name(entry),
        "description": catalog_description(entry),
        "manifest_path": entry["manifest_file"],
        "distribution_type": "chunks",
        "catalog_family_id": entry["catalog_family_id"],
        "access_profile": entry["access_profile"],
        "rights_review_status": entry["rights_review_status"],
        "catalog_meta_sha256": entry["catalog_meta_sha256"],
        "release_status": "current",
        "recommended": True,
        "supersedes": [],
        "download_size_bytes": entry["chunk_size_bytes"],
        "installed_size_bytes": entry["total_size_bytes"],
    }
    if "coverage" in entry:
        descriptor["coverage"] = entry["coverage"]
    return descriptor


def catalog_display_name(entry: dict[str, Any]) -> str:
    access_label = "Regional Cache" if entry["access_profile"] == "installed-regional-cache" else "Local Full"
    return f"SPCC Gaia DR3 XP Sampled {access_label} ({entry['manifest_id']})"


def catalog_description(entry: dict[str, Any]) -> str:
    return (
        f"AfterNight SPCC {entry['access_profile']} catalog pack with "
        f"{entry['total_records']:,} sampled-XP record(s)."
    )


def write_dataset_card(
    path: Path,
    args: argparse.Namespace,
    direct_base_url: str,
    mirror_policy: str,
    catalog_entries: list[dict[str, Any]],
) -> None:
    rows = [
        "| Catalog family | Manifest ID | Access profile | Records | Size | Rights | Path |",
        "|---|---|---|---:|---:|---|---|",
    ]
    for entry in catalog_entries:
        rows.append(
            "| {family} | {manifest} | {access} | {records:,} | {size} | {rights} | `{path}` |".format(
                family=entry["catalog_family_id"],
                manifest=entry["manifest_id"],
                access=entry["access_profile"],
                records=entry["total_records"],
                size=format_bytes(entry["total_size_bytes"]),
                rights=entry["rights_review_status"],
                path=entry["path"],
            )
        )

    card = f"""---
pretty_name: {args.dataset_title}
license: other
tags:
- astronomy
- gaia-dr3
- spectrophotometry
- afternight
---

# {args.dataset_title}

This dataset contains AfterNight Spectrophotometric Color Calibration catalog artifacts staged for direct HTTPS
download from Hugging Face.

Publication mode: `{args.publication_mode}`

Mirror hosting policy: `{mirror_policy}`

Direct download base: `{direct_base_url}`

## Catalogs

{chr(10).join(rows)}

## Required Files

Each catalog pack contains `catalog_meta.json`, `chunk_manifest.json`, `coverage_summary.json`,
`source_id_index.json`, `source_count_transcript.json`, and `.anxp` chunk files. Chunk URLs are resolved relative
to each pack's `catalog_meta.json` URL.

## Rights And Citations

Gaia-derived SPCC catalogs must not be uploaded to a public production dataset unless `catalog_meta.json` records
`rights.review_status` as `approved-for-managed-download` or `approved-for-bundling` and records approved mirror
hosting policy. Private staging datasets may be used for development validation only.

## Upload

```bash
hf upload {args.repo_id} <this-directory> . --repo-type dataset --revision {args.revision}
```

## Verify

```bash
python tools/verify_spcc_hf_dataset.py {direct_base_url}/spcc_dataset_manifest.json --chunk-policy first
```

For full catalogs, use `--chunk-policy head` first and reserve full checksum downloads for CI/cache repair smoke tests.
"""
    path.write_text(card, encoding="utf-8")


def write_gitattributes(path: Path) -> None:
    path.write_text(
        "*.anxp filter=lfs diff=lfs merge=lfs -text\n"
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
