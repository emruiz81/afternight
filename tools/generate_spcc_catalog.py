#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


RESUME_FILE_NAME = ".spcc_resume_state.json"
STAGED_SOURCES_FILE_NAME = ".spcc_staged_sources.jsonl"
STAGED_SOURCE_TRANSCRIPT_FILE_NAME = ".spcc_source_transcript.json"
STAGED_REGIONAL_REQUEST_FILE_NAME = ".spcc_regional_cache_request.json"
REGIONAL_REQUEST_FILE_NAME = "regional_cache_request.json"
REGIONAL_REQUEST_SCHEMA_VERSION = "spcc-regional-cache-request-v1"
GENERATOR_VERSION = "spcc-catalog-tooling-phase3-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate an AfterNight SPCC .anxp catalog from deterministic fixtures or local Gaia/DataLink exports."
    )
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "--preset",
        choices=["fixture", "sampled-full", "sampled-region"],
        help="Named SPCC generation preset. sampled-region selects the installed regional-cache access profile.",
    )
    parser.add_argument("--fixture", action="store_true", help="Generate a Gaia-free deterministic miniature catalog.")
    parser.add_argument("--fixture-source-count", type=int, default=8)
    parser.add_argument(
        "--input-jsonl",
        type=Path,
        help="Local source/spectrum JSONL input. Each row must contain source metadata plus flux/flux_error arrays.",
    )
    parser.add_argument(
        "--source-transcript",
        type=Path,
        help="Optional Gaia fetch/count transcript to copy into source_count_transcript.json.",
    )
    parser.add_argument(
        "--regional-request",
        type=Path,
        help=(
            "Optional app-compatible spcc-regional-cache-request-v1 JSON. "
            "Forces installed-regional-cache metadata and stamps descriptor coverage."
        ),
    )
    parser.add_argument("--manifest-id", default="spcc-fixture-manifest-2026a")
    parser.add_argument(
        "--access-profile",
        default="local-full",
        choices=["local-full", "installed-regional-cache", "local_full", "regional-cache"],
    )
    parser.add_argument("--rights-review-status", default="private-local")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--simulate-interruption-after", choices=["staged-sources"])
    parser.add_argument(
        "--tap-official",
        action="store_true",
        help="Reserved for approved production Gaia source discovery; use --input-jsonl for checked-in tests.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        generate_catalog(args)
    except SimulatedInterruption as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


class SimulatedInterruption(RuntimeError):
    pass


def generate_catalog(args: argparse.Namespace) -> None:
    output_dir: Path = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    resume_path = output_dir / RESUME_FILE_NAME

    if args.resume:
        resume_state = spcc.read_json(resume_path)
        staged_sources_path = output_dir / resume_state["staged_sources_file"]
        stars = read_jsonl(staged_sources_path)
        manifest_id = resume_state["manifest_id"]
        access_profile = resume_state["access_profile"]
        rights_review_status = resume_state["rights_review_status"]
        generation_mode = resume_state["generation_mode"]
        source_transcript = read_optional_staged_transcript(output_dir / resume_state.get("source_transcript_file", ""))
        regional_request = read_optional_staged_regional_request(
            output_dir / resume_state.get("regional_request_file", "")
        )
    else:
        if args.tap_official and not args.input_jsonl:
            raise ValueError(
                "--tap-official production retrieval is intentionally gated; provide --input-jsonl with "
                "approved Gaia/DataLink exports or use --fixture for deterministic tests"
            )
        fixture_requested = args.fixture or args.preset == "fixture"
        if not fixture_requested and args.input_jsonl is None:
            raise ValueError("Use --fixture or provide --input-jsonl")
        if fixture_requested:
            stars = generate_fixture_stars(args.fixture_source_count)
            generation_mode = "synthetic-fixture"
            source_transcript = None
        else:
            stars = read_jsonl(args.input_jsonl)
            generation_mode = "local-jsonl"
            source_transcript = spcc.read_json(args.source_transcript) if args.source_transcript is not None else None
        regional_request = read_regional_request(args.regional_request) if args.regional_request is not None else None
        manifest_id = args.manifest_id
        access_profile_arg = args.access_profile
        if args.preset == "sampled-region" and access_profile_arg == "local-full":
            access_profile_arg = "installed-regional-cache"
        access_profile = spcc.normalize_access_profile(access_profile_arg)
        if regional_request is not None:
            access_profile = regional_request["access_profile"]
        rights_review_status = args.rights_review_status
        staged_sources_path = output_dir / STAGED_SOURCES_FILE_NAME
        write_jsonl(staged_sources_path, stars)
        staged_source_transcript_file = ""
        if source_transcript is not None:
            staged_source_transcript_file = STAGED_SOURCE_TRANSCRIPT_FILE_NAME
            spcc.write_json(output_dir / staged_source_transcript_file, source_transcript)
        staged_regional_request_file = ""
        if regional_request is not None:
            staged_regional_request_file = STAGED_REGIONAL_REQUEST_FILE_NAME
            spcc.write_json(output_dir / staged_regional_request_file, regional_request)
        spcc.write_json(
            resume_path,
            {
                "stage": "staged-sources",
                "tool": "generate_spcc_catalog.py",
                "tool_version": GENERATOR_VERSION,
                "generation_mode": generation_mode,
                "manifest_id": manifest_id,
                "access_profile": access_profile,
                "rights_review_status": rights_review_status,
                "staged_sources_file": STAGED_SOURCES_FILE_NAME,
                "source_transcript_file": staged_source_transcript_file,
                "regional_request_file": staged_regional_request_file,
                "staged_source_count": len(stars),
                "updated_at_utc": spcc.utc_now_iso(),
            },
        )
        if args.simulate_interruption_after == "staged-sources":
            raise SimulatedInterruption("simulated interruption after durable SPCC source staging")

    if manifest_id == spcc.CATALOG_FAMILY_ID:
        raise ValueError("manifest-id must be immutable and distinct from the catalog family id")
    if not stars:
        raise ValueError("SPCC catalog generation requires at least one source")

    chunks_dir = output_dir / "chunks"
    if chunks_dir.exists():
        shutil.rmtree(chunks_dir)
    chunks_dir.mkdir(parents=True)

    packed_records = [spcc.pack_record(star) for star in stars]
    chunk_file = chunks_dir / f"{spcc.CATALOG_FAMILY_ID}_chunk_0000.anxp"
    chunk_header = {
        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
        "manifest_id": manifest_id,
        "chunk_index": 0,
        "first_tile": 0,
        "tile_count": 64,
        "record_size_bytes": spcc.ANXP_RECORD_SIZE_BYTES,
        "record_count": len(packed_records),
        "wavelength_grid_id": "gaia-dr3-xp-sampled-336-1020nm-2nm",
    }
    spcc.write_chunk(chunk_file, chunk_header, packed_records)

    source_id_index = {
        str(star["source_id"]): {"chunk_index": 0, "record_index": index}
        for index, star in enumerate(stars)
    }
    source_id_index_path = output_dir / "source_id_index.json"
    spcc.write_json(
        source_id_index_path,
        {
            "catalog_family_id": spcc.CATALOG_FAMILY_ID,
            "manifest_id": manifest_id,
            "source_count": len(source_id_index),
            "sources": source_id_index,
        },
    )

    source_transcript = build_source_transcript(stars, generation_mode, source_transcript)
    source_transcript_path = output_dir / "source_count_transcript.json"
    spcc.write_json(source_transcript_path, source_transcript)

    chunk_entry = {
        "chunk_index": 0,
        "first_tile": 0,
        "tile_count": 64,
        "file": chunk_file.relative_to(output_dir).as_posix(),
        "record_count": len(packed_records),
        "size_bytes": chunk_file.stat().st_size,
        "sha256": spcc.sha256_file(chunk_file),
    }
    chunk_manifest_path = output_dir / "chunk_manifest.json"
    spcc.write_json(
        chunk_manifest_path,
        {
            "catalog_family_id": spcc.CATALOG_FAMILY_ID,
            "manifest_id": manifest_id,
            "chunks": [chunk_entry],
        },
    )

    regional_request_sha256 = ""
    if regional_request is not None:
        regional_request_path = output_dir / REGIONAL_REQUEST_FILE_NAME
        spcc.write_json(regional_request_path, regional_request)
        regional_request_sha256 = spcc.sha256_file(regional_request_path)

    coverage_path = output_dir / "coverage_summary.json"
    spcc.write_json(
        coverage_path,
        build_coverage_summary(
            stars, manifest_id, access_profile, generation_mode, chunk_entry, regional_request, regional_request_sha256
        ),
    )

    meta = build_catalog_meta(
        manifest_id=manifest_id,
        access_profile=access_profile,
        rights_review_status=rights_review_status,
        generation_mode=generation_mode,
        stars=stars,
        chunk_entry=chunk_entry,
        coverage_sha256=spcc.sha256_file(coverage_path),
        chunk_manifest_sha256=spcc.sha256_file(chunk_manifest_path),
        source_id_index_sha256=spcc.sha256_file(source_id_index_path),
        source_transcript_sha256=spcc.sha256_file(source_transcript_path),
        source_transcript=source_transcript,
        regional_request=regional_request,
        regional_request_sha256=regional_request_sha256,
    )
    spcc.write_json(output_dir / "catalog_meta.json", meta)

    if resume_path.exists():
        resume_path.unlink()
    if staged_sources_path.exists():
        staged_sources_path.unlink()
    staged_source_transcript_path = output_dir / STAGED_SOURCE_TRANSCRIPT_FILE_NAME
    if staged_source_transcript_path.exists():
        staged_source_transcript_path.unlink()
    staged_regional_request_path = output_dir / STAGED_REGIONAL_REQUEST_FILE_NAME
    if staged_regional_request_path.exists():
        staged_regional_request_path.unlink()


def generate_fixture_stars(count: int) -> list[dict[str, Any]]:
    if count <= 0:
        raise ValueError("--fixture-source-count must be positive")
    wavelengths = spcc.wavelength_grid_nm()
    stars: list[dict[str, Any]] = []
    for index in range(count):
        color_slope = (index - (count - 1) / 2.0) * 0.0015
        base_flux = 1.0e-16 * (1.0 + index * 0.05)
        flux = [
            max(1.0e-20, base_flux * (1.0 + color_slope * (wavelength - 550.0)) *
                (1.0 + 0.03 * math.sin(sample_index / 17.0 + index)))
            for sample_index, wavelength in enumerate(wavelengths)
        ]
        flux_error = [value * 0.02 for value in flux]
        stars.append(
            {
                "source_id": 910000000000000000 + index,
                "solution_id": "synthetic-fixture",
                "ra": (12.5 + index * 3.75) % 360.0,
                "dec": -25.0 + index * (50.0 / max(count - 1, 1)),
                "ref_epoch": 2016.0,
                "pmra": 0.1 * index,
                "pmdec": -0.05 * index,
                "phot_g_mean_mag": 10.0 + index * 0.2,
                "phot_bp_mean_mag": 10.3 + index * 0.24,
                "phot_rp_mean_mag": 9.7 + index * 0.16,
                "flux": flux,
                "flux_error": flux_error,
                "quality_flags": 0,
                "spectrum_source_kind": 2,
            }
        )
    return stars


def read_jsonl(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        raise ValueError("JSONL path is required")
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                rows.append(json.loads(line))
    return rows


def read_optional_staged_transcript(path: Path) -> dict[str, Any] | None:
    if not str(path) or not path.is_file():
        return None
    return spcc.read_json(path)


def read_optional_staged_regional_request(path: Path) -> dict[str, Any] | None:
    if not str(path) or not path.is_file():
        return None
    return read_regional_request(path)


def read_regional_request(path: Path) -> dict[str, Any]:
    request = spcc.read_json(path)
    if not isinstance(request, dict):
        raise ValueError("--regional-request must be a JSON object")

    region = require_object(request, "region", "--regional-request")
    selection = require_object(request, "selection", "--regional-request")
    image = require_object(request, "image", "--regional-request")

    schema_version = str(request.get("schema_version", "")).strip()
    catalog_family_id = str(request.get("catalog_family_id", "")).strip()
    access_profile = spcc.normalize_access_profile(str(request.get("access_profile", "")))
    target_name = str(request.get("target_name", "")).strip()
    created_at_utc = str(request.get("created_at_utc", "")).strip()
    center_ra_deg = finite_float(region.get("center_ra_deg"), "region.center_ra_deg")
    center_dec_deg = finite_float(region.get("center_dec_deg"), "region.center_dec_deg")
    radius_deg = finite_float(region.get("radius_deg"), "region.radius_deg")
    magnitude_limit = finite_float(selection.get("magnitude_limit"), "selection.magnitude_limit")
    image_width = positive_int(image.get("width"), "image.width")
    image_height = positive_int(image.get("height"), "image.height")

    if schema_version != REGIONAL_REQUEST_SCHEMA_VERSION:
        raise ValueError(f"--regional-request schema_version must be {REGIONAL_REQUEST_SCHEMA_VERSION}")
    if catalog_family_id != spcc.CATALOG_FAMILY_ID:
        raise ValueError(f"--regional-request catalog_family_id must be {spcc.CATALOG_FAMILY_ID}")
    if access_profile != "installed-regional-cache":
        raise ValueError("--regional-request access_profile must be installed-regional-cache")
    if not created_at_utc:
        raise ValueError("--regional-request created_at_utc is required")
    if not (0.0 <= center_ra_deg < 360.0):
        raise ValueError("--regional-request region.center_ra_deg must be in [0, 360)")
    if not (-90.0 <= center_dec_deg <= 90.0):
        raise ValueError("--regional-request region.center_dec_deg must be in [-90, 90]")
    if radius_deg <= 0.0:
        raise ValueError("--regional-request region.radius_deg must be positive")
    if magnitude_limit <= 0.0:
        raise ValueError("--regional-request selection.magnitude_limit must be positive")

    return {
        "schema_version": schema_version,
        "catalog_family_id": catalog_family_id,
        "access_profile": access_profile,
        "target_name": target_name,
        "created_at_utc": created_at_utc,
        "region": {
            "center_ra_deg": center_ra_deg,
            "center_dec_deg": center_dec_deg,
            "radius_deg": radius_deg,
        },
        "selection": {
            "magnitude_limit": magnitude_limit,
        },
        "image": {
            "width": image_width,
            "height": image_height,
        },
    }


def require_object(parent: dict[str, Any], key: str, label: str) -> dict[str, Any]:
    value = parent.get(key)
    if not isinstance(value, dict):
        raise ValueError(f"{label} {key} must be a JSON object")
    return value


def finite_float(value: Any, label: str) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"--regional-request {label} must be numeric") from exc
    if not math.isfinite(result):
        raise ValueError(f"--regional-request {label} must be finite")
    return result


def positive_int(value: Any, label: str) -> int:
    try:
        result = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"--regional-request {label} must be an integer") from exc
    if result <= 0:
        raise ValueError(f"--regional-request {label} must be positive")
    return result


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")


def build_source_transcript(
    stars: list[dict[str, Any]], generation_mode: str, input_transcript: dict[str, Any] | None = None
) -> dict[str, Any]:
    if input_transcript is not None:
        transcript = json.loads(json.dumps(input_transcript))
        transcript["catalog_generation_input"] = {
            "tool": "generate_spcc_catalog.py",
            "tool_version": GENERATOR_VERSION,
            "generation_mode": generation_mode,
            "staged_source_count": len(stars),
            "generated_at_utc": spcc.utc_now_iso(),
        }
        return transcript

    generated_at_utc = spcc.utc_now_iso()
    return {
        "tool": "generate_spcc_catalog.py",
        "tool_version": GENERATOR_VERSION,
        "generated_at_utc": generated_at_utc,
        "generation_mode": generation_mode,
        "source_service_url": "synthetic://after-night/spcc-fixture"
        if generation_mode == "synthetic-fixture"
        else "local-jsonl://after-night/spcc",
        "gaia_archive_release": "Gaia DR3",
        "gaia_tables": [
            "gaiadr3.gaia_source",
            "gaiadr3.xp_summary",
            "gaiadr3.xp_sampled_mean_spectrum",
        ],
        "selected_columns": sorted(
            set(spcc.REQUIRED_GAIA_SOURCE_FIELDS)
            | set(spcc.REQUIRED_XP_SUMMARY_FIELDS)
            | set(spcc.REQUIRED_SAMPLED_SPECTRUM_FIELDS)
        ),
        "adql": (
            "SELECT source_id, ra, dec, ref_epoch, pmra, pmdec, phot_g_mean_mag, "
            "phot_bp_mean_mag, phot_rp_mean_mag FROM gaiadr3.gaia_source "
            "WHERE has_xp_sampled = 'true'"
        ),
        "query_timestamp_utc": generated_at_utc,
        "returned_count": len(stars),
        "source_id_min": min(int(star["source_id"]) for star in stars),
        "source_id_max": max(int(star["source_id"]) for star in stars),
    }


def build_coverage_summary(
    stars: list[dict[str, Any]],
    manifest_id: str,
    access_profile: str,
    generation_mode: str,
    chunk_entry: dict[str, Any],
    regional_request: dict[str, Any] | None = None,
    regional_request_sha256: str = "",
) -> dict[str, Any]:
    bp_rp_values = sorted(float(star["phot_bp_mean_mag"]) - float(star["phot_rp_mean_mag"]) for star in stars)
    p10_index = int((len(bp_rp_values) - 1) * 0.10)
    p90_index = int((len(bp_rp_values) - 1) * 0.90)
    summary = {
        "schema_version": "spcc-coverage-summary-v1",
        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
        "manifest_id": manifest_id,
        "access_profile": access_profile,
        "healpix_order": spcc.HEALPIX_ORDER,
        "chunk_parent_healpix_order": spcc.CHUNK_PARENT_HEALPIX_ORDER,
        "generated_at_utc": spcc.utc_now_iso(),
        "total_records": len(stars),
        "estimated_usable_candidates": len(stars),
        "bp_rp_p10_p90_span": bp_rp_values[p90_index] - bp_rp_values[p10_index],
        "quality_cut_class": "synthetic-fixture-all-usable"
        if generation_mode == "synthetic-fixture"
        else "gaia-dr3-xp-sampled-private-validation",
        "chunk_status": [
            {
                "chunk_index": chunk_entry["chunk_index"],
                "available": True,
                "stale": False,
                "record_count": chunk_entry["record_count"],
                "sha256": chunk_entry["sha256"],
            }
        ],
        "diagnostics": [],
    }
    if regional_request is not None:
        summary["coverage"] = descriptor_coverage_from_request(regional_request)
        summary["regional_cache_request"] = regional_request_summary(regional_request, regional_request_sha256)
    return summary


def descriptor_coverage_from_request(request: dict[str, Any]) -> dict[str, Any]:
    return {
        "center_ra_deg": request["region"]["center_ra_deg"],
        "center_dec_deg": request["region"]["center_dec_deg"],
        "radius_deg": request["region"]["radius_deg"],
        "magnitude_limit": request["selection"]["magnitude_limit"],
    }


def regional_request_summary(request: dict[str, Any], sha256: str) -> dict[str, Any]:
    return {
        "schema_version": request["schema_version"],
        "target_name": request["target_name"],
        "created_at_utc": request["created_at_utc"],
        "sha256": sha256,
        "region": request["region"],
        "selection": request["selection"],
        "image": request["image"],
    }


def build_catalog_meta(
    *,
    manifest_id: str,
    access_profile: str,
    rights_review_status: str,
    generation_mode: str,
    stars: list[dict[str, Any]],
    chunk_entry: dict[str, Any],
    coverage_sha256: str,
    chunk_manifest_sha256: str,
    source_id_index_sha256: str,
    source_transcript_sha256: str,
    source_transcript: dict[str, Any],
    regional_request: dict[str, Any] | None = None,
    regional_request_sha256: str = "",
) -> dict[str, Any]:
    generated_at_utc = spcc.utc_now_iso()
    transcript_tap = source_transcript.get("tap", {}) if isinstance(source_transcript, dict) else {}
    source_query_text = transcript_tap.get("adql", "")
    source_query_timestamp = source_transcript.get("generated_at_utc", generated_at_utc)
    source_service_urls = []
    if generation_mode == "synthetic-fixture":
        source_service_urls.append("synthetic://after-night/spcc-fixture")
    elif transcript_tap.get("service_url"):
        source_service_urls.append(str(transcript_tap["service_url"]))
    for file_entry in source_transcript.get("spectrum_retrieval", {}).get("files", []):
        if file_entry.get("url"):
            source_service_urls.append(str(file_entry["url"]))
    return {
        "schema_version": "spcc-catalog-meta-v1",
        "catalog_family_id": spcc.CATALOG_FAMILY_ID,
        "manifest_id": manifest_id,
        "access_profile": access_profile,
        "source_kind": "synthetic_fixture" if generation_mode == "synthetic-fixture" else "gaia_dr3_xp_sampled",
        "payload_kind": "sampled_spectra_with_optional_synthetic_cache",
        "total_records": len(stars),
        **({"coverage": descriptor_coverage_from_request(regional_request)} if regional_request is not None else {}),
        "rights": {
            "review_status": rights_review_status,
            "reviewed_at_utc": None,
            "reviewed_by": None,
            "review_basis": "",
            "source_terms_urls": []
            if generation_mode == "synthetic-fixture"
            else [
                "https://gea.esac.esa.int/archive/documentation/GDR3/",
                "https://www.cosmos.esa.int/web/gaia/dr3",
                "https://huggingface.co/terms-of-service",
            ],
            "license_name": "synthetic-fixture" if generation_mode == "synthetic-fixture" else "",
            "license_url": "",
            "required_credit": [],
            "required_citations": []
            if generation_mode == "synthetic-fixture"
            else [
                "ESA/Gaia/DPAC credit and Gaia DR3 citations required by the generated catalog manifest.",
            ],
            "redistribution_policy": "private-only"
            if rights_review_status == "private-local"
            else "unknown",
            "derivative_data_policy": "allowed" if generation_mode == "synthetic-fixture" else "unknown",
            "mirror_hosting_policy": "staging-only" if rights_review_status == "private-local" else "unknown",
            "notes": "Synthetic deterministic fixture data."
            if generation_mode == "synthetic-fixture"
            else "Gaia-derived SPCC catalogs require explicit redistribution and mirror-hosting review before public upload.",
        },
        "provenance": {
            "source_family": "synthetic-fixture"
            if generation_mode == "synthetic-fixture"
            else "gaia-dr3-xp-sampled-local-export",
            "source_release": "Gaia DR3",
            "source_service_urls": source_service_urls,
            "source_query_text": "" if generation_mode == "synthetic-fixture" else source_query_text,
            "retrieval_method": generation_mode,
            "retrieval_tool": "generate_spcc_catalog.py",
            "retrieval_tool_version": GENERATOR_VERSION,
            "generated_at_utc": generated_at_utc,
            "source_query_timestamp_utc": source_query_timestamp,
            "source_query_sha256": source_transcript_sha256,
            **({"regional_cache_request_sha256": regional_request_sha256} if regional_request is not None else {}),
            "input_manifest_ids": [],
            "output_manifest_id": manifest_id,
            "checksum_algorithm": "sha256",
        },
        "format": {
            "magic": spcc.ANXP_MAGIC.decode("ascii"),
            "format_version": spcc.ANXP_FORMAT_VERSION,
            "record_version": spcc.ANXP_RECORD_VERSION,
            "record_size_bytes": spcc.ANXP_RECORD_SIZE_BYTES,
            "reserved_record_bytes": spcc.ANXP_RESERVED_RECORD_BYTES,
            "byte_order": "little-endian",
            "healpix_ordering": "nested",
            "healpix_order": spcc.HEALPIX_ORDER,
            "chunk_parent_healpix_order": spcc.CHUNK_PARENT_HEALPIX_ORDER,
        },
        "wavelength_grid": {
            "unit": "nm",
            "sample_count": spcc.SAMPLED_XP_SAMPLE_COUNT,
            "start_nm": spcc.SAMPLED_XP_WAVELENGTH_START_NM,
            "step_nm": spcc.SAMPLED_XP_WAVELENGTH_STEP_NM,
            "end_nm": spcc.SAMPLED_XP_WAVELENGTH_END_NM,
        },
        "flux_unit": "W.m**-2.nm**-1",
        "uncertainty_payload": {
            "flux_error_samples": "present",
            "uncertainty_policy": "propagate_xp_flux_error",
            "unweighted_fit_fallback_allowed": False,
        },
        "quality_fields": {
            "required_gaia_source_fields": spcc.REQUIRED_GAIA_SOURCE_FIELDS,
            "required_xp_summary_fields": spcc.REQUIRED_XP_SUMMARY_FIELDS,
            "required_sampled_spectrum_fields": spcc.REQUIRED_SAMPLED_SPECTRUM_FIELDS,
            "missing_mandatory_field_sentinel": "manifest-required",
        },
        "files": {
            "chunk_manifest": "chunk_manifest.json",
            "coverage_summary": "coverage_summary.json",
            "source_id_index": "source_id_index.json",
            "source_count_transcript": "source_count_transcript.json",
            **({"regional_cache_request": REGIONAL_REQUEST_FILE_NAME} if regional_request is not None else {}),
        },
        "chunk_manifest_sha256": chunk_manifest_sha256,
        "coverage_summary_sha256": coverage_sha256,
        "source_id_index_sha256": source_id_index_sha256,
        "source_count_transcript_sha256": source_transcript_sha256,
        **({"regional_cache_request_sha256": regional_request_sha256} if regional_request is not None else {}),
        "chunks": [chunk_entry],
    }


if __name__ == "__main__":
    raise SystemExit(main())
