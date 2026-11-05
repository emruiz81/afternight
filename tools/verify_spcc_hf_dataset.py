#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


TOOL_VERSION = "spcc-hf-dataset-verifier-phase3a-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify a hosted or local Hugging Face-style AfterNight SPCC catalog dataset."
    )
    parser.add_argument("dataset_manifest_url", help="URL or local path to spcc_dataset_manifest.json")
    parser.add_argument(
        "--base-url-override",
        help="Rewrite catalog URLs from the manifest's direct_download_base_url to this URL or local file:// root.",
    )
    parser.add_argument(
        "--chunk-policy",
        choices=["none", "head", "first", "all"],
        default="first",
        help="How aggressively to verify .anxp chunks. Use head for large production catalogs.",
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
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    warnings: list[str] = []

    try:
        verify_dataset(args, errors, warnings)
    except Exception as exc:
        errors.append(str(exc))

    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    print("SPCC hosted dataset verification passed")
    return 0


def verify_dataset(args: argparse.Namespace, errors: list[str], warnings: list[str]) -> None:
    token = None if args.no_token else load_token(args)
    dataset_manifest_url = normalize_url_or_path(args.dataset_manifest_url)
    dataset_bytes = fetch_bytes(dataset_manifest_url, token=token, timeout=args.timeout)
    dataset = parse_json_bytes(dataset_bytes, "dataset manifest")

    if dataset.get("schema_version") != "spcc-hf-dataset-manifest-v1":
        errors.append("dataset manifest has an unexpected schema_version")
        return

    catalog_entries = dataset.get("catalogs", [])
    if not catalog_entries:
        errors.append("dataset manifest does not list any catalogs")
        return

    mirror_url = resolve_relative(dataset_manifest_url, "spcc_mirror_manifest.json")
    if args.base_url_override:
        mirror_url = resolve_relative(args.base_url_override.rstrip("/") + "/", "spcc_mirror_manifest.json")
    mirror = parse_json_bytes(fetch_bytes(mirror_url, token=token, timeout=args.timeout), "mirror manifest")
    if mirror.get("schema_version") != "spcc-remote-mirror-manifest-v1":
        errors.append("mirror manifest has an unexpected schema_version")

    direct_base_url = str(dataset.get("direct_download_base_url", "")).rstrip("/")
    override_base_url = normalize_url_or_path(args.base_url_override).rstrip("/") if args.base_url_override else ""

    for entry in catalog_entries:
        verify_catalog_entry(
            entry=entry,
            direct_base_url=direct_base_url,
            override_base_url=override_base_url,
            token=token,
            timeout=args.timeout,
            chunk_policy=args.chunk_policy,
            max_chunk_bytes=args.max_chunk_bytes,
            errors=errors,
            warnings=warnings,
        )


def verify_catalog_entry(
    *,
    entry: dict[str, Any],
    direct_base_url: str,
    override_base_url: str,
    token: str | None,
    timeout: float,
    chunk_policy: str,
    max_chunk_bytes: int,
    errors: list[str],
    warnings: list[str],
) -> None:
    manifest_id = str(entry.get("manifest_id", "<unknown>"))
    manifest_url = rewrite_manifest_url(str(entry.get("manifest_url", "")), direct_base_url, override_base_url)
    if not manifest_url:
        errors.append(f"{manifest_id}: missing manifest_url")
        return

    meta_bytes = fetch_bytes(manifest_url, token=token, timeout=timeout)
    expected_meta_sha = str(entry.get("catalog_meta_sha256", ""))
    actual_meta_sha = spcc.sha256_bytes(meta_bytes)
    if expected_meta_sha and actual_meta_sha != expected_meta_sha:
        errors.append(f"{manifest_id}: catalog_meta.json checksum mismatch")
        return

    meta = parse_json_bytes(meta_bytes, f"{manifest_id} catalog_meta.json")
    if meta.get("catalog_family_id") != entry.get("catalog_family_id"):
        errors.append(f"{manifest_id}: catalog_family_id mismatch")
    if meta.get("manifest_id") != entry.get("manifest_id"):
        errors.append(f"{manifest_id}: manifest_id mismatch")
    if meta.get("access_profile") != entry.get("access_profile"):
        errors.append(f"{manifest_id}: access_profile mismatch")
    if int(meta.get("total_records", -1)) != int(entry.get("total_records", -2)):
        errors.append(f"{manifest_id}: total_records mismatch")

    verify_sidecar(manifest_url, meta, "chunk_manifest", "chunk_manifest_sha256", token, timeout, errors)
    verify_sidecar(manifest_url, meta, "coverage_summary", "coverage_summary_sha256", token, timeout, errors)
    verify_sidecar(manifest_url, meta, "source_id_index", "source_id_index_sha256", token, timeout, errors)
    verify_sidecar(
        manifest_url, meta, "source_count_transcript", "source_count_transcript_sha256", token, timeout, errors
    )

    chunks = list(meta.get("chunks", []))
    if len(chunks) != int(entry.get("chunk_count", -1)):
        errors.append(f"{manifest_id}: chunk_count mismatch")
    if chunk_policy == "none":
        return

    chunks_to_check = chunks[:1] if chunk_policy in {"head", "first"} else chunks
    for chunk in chunks_to_check:
        chunk_file = str(chunk.get("file", ""))
        chunk_url = resolve_relative(manifest_url, chunk_file)
        expected_size = int(chunk.get("size_bytes", 0))
        if chunk_policy == "head":
            verify_head(chunk_url, expected_size, token, timeout, manifest_id, errors, warnings)
            continue
        if max_chunk_bytes > 0 and expected_size > max_chunk_bytes:
            warnings.append(
                f"{manifest_id}: skipped checksum download for {chunk_file}; "
                f"{expected_size} bytes exceeds --max-chunk-bytes"
            )
            verify_head(chunk_url, expected_size, token, timeout, manifest_id, errors, warnings)
            continue
        payload = fetch_bytes(chunk_url, token=token, timeout=timeout)
        if len(payload) != expected_size:
            errors.append(f"{manifest_id}: chunk size mismatch for {chunk_file}")
        expected_sha = str(chunk.get("sha256", ""))
        actual_sha = spcc.sha256_bytes(payload)
        if expected_sha and actual_sha != expected_sha:
            errors.append(f"{manifest_id}: chunk checksum mismatch for {chunk_file}")


def verify_sidecar(
    manifest_url: str,
    meta: dict[str, Any],
    file_key: str,
    checksum_key: str,
    token: str | None,
    timeout: float,
    errors: list[str],
) -> None:
    relative_file = str(meta.get("files", {}).get(file_key, ""))
    expected_sha = str(meta.get(checksum_key, ""))
    if not relative_file or not expected_sha:
        errors.append(f"catalog_meta.json missing sidecar declaration for {file_key}")
        return
    payload = fetch_bytes(resolve_relative(manifest_url, relative_file), token=token, timeout=timeout)
    actual_sha = spcc.sha256_bytes(payload)
    if actual_sha != expected_sha:
        errors.append(f"sidecar checksum mismatch for {relative_file}")


def verify_head(
    url: str,
    expected_size: int,
    token: str | None,
    timeout: float,
    manifest_id: str,
    errors: list[str],
    warnings: list[str],
) -> None:
    headers = fetch_head(url, token=token, timeout=timeout)
    content_length = headers.get("Content-Length") or headers.get("content-length")
    if content_length is None:
        warnings.append(f"{manifest_id}: HEAD response did not include Content-Length for {url}")
        return
    if int(content_length) != expected_size:
        errors.append(f"{manifest_id}: HEAD Content-Length mismatch for {url}")


def load_token(args: argparse.Namespace) -> str | None:
    env_token = os.environ.get(args.token_env) or os.environ.get("HUGGINGFACE_HUB_TOKEN")
    if env_token:
        return env_token.strip()
    if args.token_file is not None and args.token_file.exists():
        return args.token_file.read_text(encoding="utf-8").strip()
    cache_token = Path.home() / ".cache" / "huggingface" / "token"
    if cache_token.exists():
        return cache_token.read_text(encoding="utf-8").strip()
    return None


def normalize_url_or_path(value: str | None) -> str:
    if not value:
        return ""
    if len(value) >= 3 and value[1] == ":" and value[2] in {"/", "\\"}:
        return Path(value).resolve().as_uri()
    parsed = urllib.parse.urlparse(value)
    if parsed.scheme:
        return value
    return Path(value).resolve().as_uri()


def rewrite_manifest_url(url: str, direct_base_url: str, override_base_url: str) -> str:
    if not override_base_url or not direct_base_url:
        return url
    prefix = direct_base_url.rstrip("/") + "/"
    if url.startswith(prefix):
        return override_base_url.rstrip("/") + "/" + url[len(prefix) :]
    return url


def resolve_relative(base_url: str, relative_file: str) -> str:
    if not relative_file:
        return base_url
    base = normalize_url_or_path(base_url)
    parsed = urllib.parse.urlparse(base)
    if parsed.scheme == "file":
        base_path = Path(urllib.request.url2pathname(parsed.path))
        parent = base_path if base_path.exists() and base_path.is_dir() else base_path.parent
        return (parent / relative_file).resolve().as_uri()
    return urllib.parse.urljoin(base.rsplit("/", 1)[0] + "/", relative_file)


def fetch_bytes(url: str, *, token: str | None, timeout: float) -> bytes:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme == "file":
        return Path(urllib.request.url2pathname(parsed.path)).read_bytes()
    request = urllib.request.Request(url, headers=request_headers(token))
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def fetch_head(url: str, *, token: str | None, timeout: float) -> dict[str, str]:
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme == "file":
        path = Path(urllib.request.url2pathname(parsed.path))
        return {"Content-Length": str(path.stat().st_size)}
    request = urllib.request.Request(url, method="HEAD", headers=request_headers(token))
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return dict(response.headers.items())


def request_headers(token: str | None) -> dict[str, str]:
    headers = {"User-Agent": f"AfterNight-SPCC-Dataset-Verifier/{TOOL_VERSION}"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers


def parse_json_bytes(payload: bytes, label: str) -> dict[str, Any]:
    try:
        data = json.loads(payload.decode("utf-8"))
    except Exception as exc:
        raise ValueError(f"could not parse {label}: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError(f"{label} is not a JSON object")
    return data


if __name__ == "__main__":
    raise SystemExit(main())
