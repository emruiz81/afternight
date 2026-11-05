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


CATALOG_DATASET_MANIFEST = "spcc_dataset_manifest.json"
CATALOG_CONFIG = "spcc_catalog_mirrors.json"
RESPONSE_DATASET_MANIFEST = "spcc_response_dataset_manifest.json"
RESPONSE_CONFIG = "spcc_response_repositories.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Write the final SPCC Hugging Face upload and hosted-verification instructions from staged "
            "catalog and response datasets."
        )
    )
    parser.add_argument("--catalog-dataset-dir", type=Path, required=True)
    parser.add_argument("--response-dataset-dir", type=Path, required=True)
    parser.add_argument("--catalog-repo-id", help="Hugging Face dataset repo for SPCC catalogs.")
    parser.add_argument("--response-repo-id", help="Hugging Face dataset repo for SPCC response repositories.")
    parser.add_argument("--catalog-revision", help="Catalog dataset branch/tag/revision.")
    parser.add_argument("--response-revision", help="Response dataset branch/tag/revision.")
    parser.add_argument("--validation-manifest", default="docs/dev/spcc/spcc-validation-data-manifest.json")
    parser.add_argument("--response-source-manifest", default="<reviewed-response-source-manifest.json>")
    parser.add_argument(
        "--real-image-validation-source-manifest",
        default="<reviewed-real-image-validation-source-manifest.json>",
    )
    parser.add_argument("--response-manifest-entries", default="<response-manifest-entries.json>")
    parser.add_argument("--support-envelope-manifest-entry", default="<support-envelope-manifest-entry.json>")
    parser.add_argument("--real-image-manifest-entries", default="<real-image-validation-manifest-entries.json>")
    parser.add_argument("--packaged-qa-manifest-entry", default="<packaged-qa-manifest-entry.json>")
    parser.add_argument("--merged-validation-manifest", default="<merged-validation-manifest.json>")
    parser.add_argument("--chunk-policy", choices=["head", "first", "all", "none"], default="head")
    parser.add_argument("--local-chunk-policy", choices=["head", "first", "all", "none"], default="first")
    parser.add_argument("--private", action="store_true", help="Include --private in hf upload commands.")
    parser.add_argument("--output-markdown", type=Path, help="Write instructions to this Markdown file.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors: list[str] = []
    try:
        instructions = build_instructions(args, errors)
    except Exception as exc:
        errors.append(str(exc))
        instructions = ""

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1

    if args.output_markdown is not None:
        args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
        args.output_markdown.write_text(instructions, encoding="utf-8")
        print(f"SPCC Hugging Face upload instructions written: {args.output_markdown}")
    else:
        print(instructions)
    return 0


def build_instructions(args: argparse.Namespace, errors: list[str]) -> str:
    catalog_dataset = read_json_file(args.catalog_dataset_dir / CATALOG_DATASET_MANIFEST, errors)
    catalog_config = read_json_file(args.catalog_dataset_dir / CATALOG_CONFIG, errors)
    response_dataset = read_json_file(args.response_dataset_dir / RESPONSE_DATASET_MANIFEST, errors)
    response_config = read_json_file(args.response_dataset_dir / RESPONSE_CONFIG, errors)
    if errors:
        return ""

    validate_staged_inputs(catalog_dataset, catalog_config, response_dataset, response_config, errors)
    if errors:
        return ""

    catalog_repo_id = args.catalog_repo_id or text_or_default(catalog_dataset.get("dataset_repo_id"), "")
    response_repo_id = args.response_repo_id or text_or_default(response_dataset.get("dataset_repo_id"), "")
    catalog_revision = args.catalog_revision or text_or_default(catalog_dataset.get("revision"), "main")
    response_revision = args.response_revision or text_or_default(response_dataset.get("revision"), "main")
    if not catalog_repo_id:
        errors.append("catalog repo id is missing; pass --catalog-repo-id or set dataset_repo_id")
    if not response_repo_id:
        errors.append("response repo id is missing; pass --response-repo-id or set dataset_repo_id")
    if errors:
        return ""

    catalog_base_url = hf_resolve_base_url(catalog_repo_id, catalog_revision)
    response_base_url = hf_resolve_base_url(response_repo_id, response_revision)
    private_flag = " --private" if args.private else ""

    local_catalog_manifest = args.catalog_dataset_dir / CATALOG_DATASET_MANIFEST
    local_catalog_config = args.catalog_dataset_dir / CATALOG_CONFIG
    local_response_config = args.response_dataset_dir / RESPONSE_CONFIG
    catalog_pack_count = len(catalog_config["catalog_packs"])
    response_repo_count = len(response_config["response_repositories"])

    return f"""# SPCC Hugging Face Production Upload Instructions

Generated from staged local datasets:

- Catalog dataset: `{args.catalog_dataset_dir.as_posix()}` ({catalog_pack_count} app-facing descriptor(s))
- Response dataset: `{args.response_dataset_dir.as_posix()}` ({response_repo_count} app-facing descriptor(s))

Do not run the upload commands until the staged manifests record approved production rights/provenance and the local
verification commands below pass.

## 1. Local Verification

```powershell
{command("python", "tools/verify_spcc_hf_dataset.py", local_catalog_manifest, "--base-url-override", args.catalog_dataset_dir.resolve().as_uri(), "--chunk-policy", args.local_chunk_policy, "--no-token")}
{command("python", "tools/verify_spcc_catalog_mirror_config.py", local_catalog_config, "--chunk-policy", args.local_chunk_policy, "--no-token")}
{command("python", "tools/verify_spcc_response_repository_config.py", local_response_config, "--no-token")}
```

## 2. Upload

```powershell
{command("hf", "upload", catalog_repo_id, args.catalog_dataset_dir, ".", "--repo-type", "dataset", "--revision", catalog_revision, "--commit-message", f"Publish SPCC catalog dataset {catalog_revision}")}{private_flag}
{command("hf", "upload", response_repo_id, args.response_dataset_dir, ".", "--repo-type", "dataset", "--revision", response_revision, "--commit-message", f"Publish SPCC response dataset {response_revision}")}{private_flag}
```

## 3. Hosted Verification

```powershell
{command("python", "tools/verify_spcc_hf_dataset.py", f"{catalog_base_url}/{CATALOG_DATASET_MANIFEST}", "--chunk-policy", args.chunk_policy)}
{command("python", "tools/verify_spcc_catalog_mirror_config.py", f"{catalog_base_url}/{CATALOG_CONFIG}", "--chunk-policy", args.chunk_policy)}
{command("python", "tools/verify_spcc_response_repository_config.py", f"{response_base_url}/{RESPONSE_CONFIG}")}
```

## 4. Merge Evidence

```powershell
{command("python", "tools/merge_spcc_validation_manifest_entries.py", args.validation_manifest, args.response_manifest_entries, args.support_envelope_manifest_entry, args.real_image_manifest_entries, args.packaged_qa_manifest_entry, "--output", args.merged_validation_manifest, "--release-ready")}
```

## 5. Final Release Gate

```powershell
{command("python", "tools/check_spcc_release_readiness.py", "--validation-manifest", args.merged_validation_manifest, "--catalog-mirror-config", f"{catalog_base_url}/{CATALOG_CONFIG}", "--response-repository-config", f"{response_base_url}/{RESPONSE_CONFIG}", "--response-source-manifest", args.response_source_manifest, "--real-image-validation-source-manifest", args.real_image_validation_source_manifest, "--chunk-policy", args.chunk_policy)}
```
"""


def read_json_file(path: Path, errors: list[str]) -> dict[str, Any]:
    if not path.is_file():
        errors.append(f"required staged file is missing: {path}")
        return {}
    data = spcc.read_json(path)
    if not isinstance(data, dict):
        errors.append(f"required staged file must contain a JSON object: {path}")
        return {}
    return data


def validate_staged_inputs(
    catalog_dataset: dict[str, Any],
    catalog_config: dict[str, Any],
    response_dataset: dict[str, Any],
    response_config: dict[str, Any],
    errors: list[str],
) -> None:
    if catalog_dataset.get("schema_version") != "spcc-hf-dataset-manifest-v1":
        errors.append(f"{CATALOG_DATASET_MANIFEST} has an unexpected schema_version")
    if catalog_config.get("schema_version") != "spcc-catalog-mirror-config-v1":
        errors.append(f"{CATALOG_CONFIG} has an unexpected schema_version")
    if response_dataset.get("schema_version") != "spcc-response-dataset-manifest-v1":
        errors.append(f"{RESPONSE_DATASET_MANIFEST} has an unexpected schema_version")
    if response_config.get("schema_version") != "spcc-response-repository-config-v1":
        errors.append(f"{RESPONSE_CONFIG} has an unexpected schema_version")
    require_non_empty_array(catalog_dataset, "catalogs", CATALOG_DATASET_MANIFEST, errors)
    require_non_empty_array(catalog_config, "catalog_packs", CATALOG_CONFIG, errors)
    require_non_empty_array(response_dataset, "response_repositories", RESPONSE_DATASET_MANIFEST, errors)
    require_non_empty_array(response_config, "response_repositories", RESPONSE_CONFIG, errors)
    require_object(catalog_config, "publication_policy", CATALOG_CONFIG, errors)
    require_object(response_config, "publication_policy", RESPONSE_CONFIG, errors)


def require_non_empty_array(data: dict[str, Any], key: str, label: str, errors: list[str]) -> None:
    value = data.get(key)
    if not isinstance(value, list) or not value:
        errors.append(f"{label} must contain a non-empty {key} array")


def require_object(data: dict[str, Any], key: str, label: str, errors: list[str]) -> None:
    value = data.get(key)
    if not isinstance(value, dict) or not value:
        errors.append(f"{label} must contain a non-empty {key} object")


def text_or_default(value: Any, default: str) -> str:
    return value.strip() if isinstance(value, str) and value.strip() else default


def hf_resolve_base_url(repo_id: str, revision: str) -> str:
    return f"https://huggingface.co/datasets/{repo_id}/resolve/{revision}"


def command(*parts: Any) -> str:
    return " ".join(powershell_quote(str(part)) for part in parts)


def powershell_quote(value: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9_./:<>@{}=,+\\-]+", value):
        return value
    return "'" + value.replace("'", "''") + "'"


if __name__ == "__main__":
    raise SystemExit(main())
