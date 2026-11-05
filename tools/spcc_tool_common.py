#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import csv
import hashlib
import json
import math
import struct
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


CATALOG_FAMILY_ID = "spcc_gaia_dr3_xp_sampled"
ANXP_MAGIC = b"ANXP"
ANXP_FORMAT_VERSION = 1
ANXP_RECORD_VERSION = 1
ANXP_RECORD_SIZE_BYTES = 1536
ANXP_RESERVED_RECORD_BYTES = 64
SAMPLED_XP_SAMPLE_COUNT = 343
SAMPLED_XP_WAVELENGTH_START_NM = 336.0
SAMPLED_XP_WAVELENGTH_STEP_NM = 2.0
SAMPLED_XP_WAVELENGTH_END_NM = 1020.0
HEALPIX_ORDER = 6
CHUNK_PARENT_HEALPIX_ORDER = 3
SUPPORT_MIN_ESTIMATED_CANDIDATES = 80
SUPPORT_MIN_RUNTIME_USED_STARS = 35
SUPPORT_LOW_RUNTIME_USED_STAR_WARNING = 60
SUPPORT_MIN_BP_RP_P10_P90_SPAN = 0.75
APPROVED_CATALOG_RIGHTS_STATUSES = {"approved-for-managed-download", "approved-for-bundling"}

CHUNK_PREAMBLE_STRUCT = struct.Struct("<4sHHII")
RECORD_HEADER_STRUCT = struct.Struct("<QddffffffffHHBB")
RECORD_FLUX_STRUCT = struct.Struct(f"<{SAMPLED_XP_SAMPLE_COUNT}h")
RECORD_ERROR_STRUCT = struct.Struct(f"<{SAMPLED_XP_SAMPLE_COUNT}H")

REQUIRED_GAIA_SOURCE_FIELDS = [
    "source_id",
    "ra",
    "dec",
    "ref_epoch",
    "pmra",
    "pmdec",
    "phot_g_mean_mag",
    "phot_bp_mean_mag",
    "phot_rp_mean_mag",
    "phot_g_mean_flux_over_error",
    "phot_bp_mean_flux_over_error",
    "phot_rp_mean_flux_over_error",
    "phot_bp_rp_excess_factor",
    "phot_bp_n_contaminated_transits",
    "phot_bp_n_blended_transits",
    "phot_rp_n_contaminated_transits",
    "phot_rp_n_blended_transits",
    "ruwe",
    "duplicated_source",
    "non_single_star",
    "phot_variable_flag",
    "ipd_frac_multi_peak",
    "has_xp_sampled",
]
REQUIRED_XP_SUMMARY_FIELDS = [
    "bp_n_relevant_bases",
    "bp_n_measurements",
    "bp_n_rejected_measurements",
    "bp_chi_squared",
    "bp_n_transits",
    "bp_n_contaminated_transits",
    "bp_n_blended_transits",
    "rp_n_relevant_bases",
    "rp_n_measurements",
    "rp_n_rejected_measurements",
    "rp_chi_squared",
    "rp_n_transits",
    "rp_n_contaminated_transits",
    "rp_n_blended_transits",
]
REQUIRED_SAMPLED_SPECTRUM_FIELDS = ["source_id", "solution_id", "ra", "dec", "flux", "flux_error"]
OPTIONAL_FLOAT_METADATA_DEFAULTS = {
    "pmra": 0.0,
    "pmdec": 0.0,
}


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def canonical_json_bytes(data: Any) -> bytes:
    return json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def wavelength_grid_nm() -> list[float]:
    return [
        SAMPLED_XP_WAVELENGTH_START_NM + index * SAMPLED_XP_WAVELENGTH_STEP_NM
        for index in range(SAMPLED_XP_SAMPLE_COUNT)
    ]


def healpix_cell_count(order: int) -> int:
    if order < 0:
        raise ValueError("HEALPix order must be non-negative")
    return 12 * (4 ** order)


def local_full_tile_coverage_errors(meta: dict[str, Any]) -> list[str]:
    if str(meta.get("access_profile", "")) != "local-full":
        return []
    format_meta = meta.get("format", {})
    try:
        healpix_order = int(format_meta.get("healpix_order", HEALPIX_ORDER))
        parent_order = int(format_meta.get("chunk_parent_healpix_order", CHUNK_PARENT_HEALPIX_ORDER))
    except (TypeError, ValueError):
        return ["local-full SPCC catalog declares invalid HEALPix order metadata"]
    return local_full_chunk_coverage_errors(
        meta.get("chunks", []),
        access_profile=str(meta.get("access_profile", "")),
        source_kind=str(meta.get("source_kind", "")),
        parent_order=parent_order,
        healpix_order=healpix_order,
    )


def local_full_chunk_coverage_errors(
    chunks: list[dict[str, Any]],
    *,
    access_profile: str,
    source_kind: str,
    parent_order: int,
    healpix_order: int,
) -> list[str]:
    if access_profile != "local-full":
        return []
    if source_kind == "synthetic_fixture":
        return []
    if parent_order < 0 or healpix_order < 0 or parent_order > 12 or healpix_order > 12:
        return ["local-full SPCC catalog declares invalid HEALPix order metadata"]

    expected_parent_count = healpix_cell_count(parent_order)
    expected_tile_count = healpix_cell_count(healpix_order)
    declared_tile_count = 0
    parent_indices: list[int] = []
    covered_tiles = bytearray(expected_tile_count)
    overlap_count = 0
    invalid_ranges = 0

    for chunk in chunks:
        try:
            parent = int(chunk.get("chunk_index", -1))
            first_tile = int(chunk.get("first_tile", -1))
            tile_count = int(chunk.get("tile_count", 0))
        except (TypeError, ValueError):
            invalid_ranges += 1
            continue
        parent_indices.append(parent)
        declared_tile_count += tile_count
        if parent < 0 or parent >= expected_parent_count:
            invalid_ranges += 1
        if first_tile < 0 or tile_count <= 0 or first_tile + tile_count > expected_tile_count:
            invalid_ranges += 1
            continue
        for tile in range(first_tile, first_tile + tile_count):
            if covered_tiles[tile]:
                overlap_count += 1
            else:
                covered_tiles[tile] = 1

    errors: list[str] = []
    if len(chunks) != expected_parent_count or declared_tile_count != expected_tile_count:
        errors.append(
            "local-full SPCC catalog declares "
            f"{len(chunks)}/{expected_parent_count} parent chunks and "
            f"{declared_tile_count}/{expected_tile_count} HEALPix order-{healpix_order} tiles"
        )

    unique_parent_count = len(set(parent_indices))
    if unique_parent_count != len(parent_indices):
        errors.append("local-full SPCC catalog declares duplicate parent chunks")
    missing_parent_count = expected_parent_count - unique_parent_count
    if missing_parent_count > 0:
        errors.append(f"local-full SPCC catalog is missing {missing_parent_count} parent chunk(s)")
    if invalid_ranges > 0:
        errors.append(f"local-full SPCC catalog has {invalid_ranges} invalid chunk tile range(s)")
    missing_tile_count = expected_tile_count - sum(1 for covered in covered_tiles if covered)
    if missing_tile_count > 0:
        errors.append(f"local-full SPCC catalog is missing {missing_tile_count} HEALPix tile(s)")
    if overlap_count > 0:
        errors.append(f"local-full SPCC catalog has {overlap_count} overlapping HEALPix tile declaration(s)")
    return errors


def normalize_access_profile(value: str) -> str:
    normalized = value.strip().lower().replace("_", "-")
    aliases = {
        "local-full": "local-full",
        "local-full-install": "local-full",
        "installed-regional-cache": "installed-regional-cache",
        "regional-cache": "installed-regional-cache",
    }
    if normalized not in aliases:
        raise ValueError(f"Unsupported SPCC catalog access profile: {value}")
    return aliases[normalized]


def quantize_signed(values: list[float]) -> tuple[float, list[int]]:
    max_abs = max((abs(value) for value in values if math.isfinite(value)), default=0.0)
    scale = max_abs / 32767.0 if max_abs > 0.0 else 1.0
    quantized = []
    for value in values:
        if not math.isfinite(value):
            raise ValueError("SPCC spectra must contain only finite flux samples")
        quantized.append(max(-32768, min(32767, int(round(value / scale)))))
    return scale, quantized


def quantize_unsigned(values: list[float]) -> tuple[float, list[int]]:
    max_value = max((value for value in values if math.isfinite(value)), default=0.0)
    scale = max_value / 65535.0 if max_value > 0.0 else 1.0
    quantized = []
    for value in values:
        if not math.isfinite(value) or value < 0.0:
            raise ValueError("SPCC flux-error spectra must contain only finite non-negative samples")
        quantized.append(max(0, min(65535, int(round(value / scale)))))
    return scale, quantized


def ensure_spectrum_length(values: list[float], field_name: str) -> None:
    if len(values) != SAMPLED_XP_SAMPLE_COUNT:
        raise ValueError(f"{field_name} must contain {SAMPLED_XP_SAMPLE_COUNT} samples")


def pack_record(star: dict[str, Any]) -> bytes:
    flux = [float(value) for value in star["flux"]]
    flux_error = [float(value) for value in star.get("flux_error", [0.0] * SAMPLED_XP_SAMPLE_COUNT)]
    ensure_spectrum_length(flux, "flux")
    ensure_spectrum_length(flux_error, "flux_error")

    flux_scale, flux_quantized = quantize_signed(flux)
    flux_error_scale, error_quantized = quantize_unsigned(flux_error)
    header = RECORD_HEADER_STRUCT.pack(
        int(star["source_id"]),
        float(star["ra"]),
        float(star["dec"]),
        float(star.get("pmra", 0.0)),
        float(star.get("pmdec", 0.0)),
        float(star.get("ref_epoch", 2016.0)),
        float(star.get("phot_g_mean_mag", 0.0)),
        float(star.get("phot_bp_mean_mag", 0.0)),
        float(star.get("phot_rp_mean_mag", 0.0)),
        float(flux_scale),
        float(flux_error_scale),
        int(star.get("quality_flags", 0)),
        SAMPLED_XP_SAMPLE_COUNT,
        int(star.get("spectrum_source_kind", 1)),
        0,
    )
    payload = header + RECORD_FLUX_STRUCT.pack(*flux_quantized) + RECORD_ERROR_STRUCT.pack(*error_quantized)
    if len(payload) > ANXP_RECORD_SIZE_BYTES:
        raise ValueError("Packed SPCC record exceeds the v1 record size")
    return payload + bytes(ANXP_RECORD_SIZE_BYTES - len(payload))


def unpack_record(payload: bytes) -> dict[str, Any]:
    if len(payload) != ANXP_RECORD_SIZE_BYTES:
        raise ValueError("SPCC record payload has the wrong byte size")
    header_end = RECORD_HEADER_STRUCT.size
    flux_end = header_end + RECORD_FLUX_STRUCT.size
    header = RECORD_HEADER_STRUCT.unpack(payload[:header_end])
    flux_quantized = RECORD_FLUX_STRUCT.unpack(payload[header_end:flux_end])
    error_quantized = RECORD_ERROR_STRUCT.unpack(payload[flux_end:flux_end + RECORD_ERROR_STRUCT.size])
    flux_scale = header[9]
    error_scale = header[10]
    return {
        "source_id": header[0],
        "ra": header[1],
        "dec": header[2],
        "pmra": header[3],
        "pmdec": header[4],
        "ref_epoch": header[5],
        "phot_g_mean_mag": header[6],
        "phot_bp_mean_mag": header[7],
        "phot_rp_mean_mag": header[8],
        "flux": [sample * flux_scale for sample in flux_quantized],
        "flux_error": [sample * error_scale for sample in error_quantized],
        "quality_flags": header[11],
        "valid_sample_count": header[12],
        "spectrum_source_kind": header[13],
    }


def write_chunk(path: Path, header: dict[str, Any], packed_records: list[bytes]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    header_bytes = canonical_json_bytes(header)
    with path.open("wb") as handle:
        handle.write(
            CHUNK_PREAMBLE_STRUCT.pack(
                ANXP_MAGIC,
                ANXP_FORMAT_VERSION,
                ANXP_RECORD_VERSION,
                len(header_bytes),
                len(packed_records),
            )
        )
        handle.write(header_bytes)
        for record in packed_records:
            if len(record) != ANXP_RECORD_SIZE_BYTES:
                raise ValueError("Packed SPCC record has the wrong byte size")
            handle.write(record)


def read_chunk(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    with path.open("rb") as handle:
        preamble = handle.read(CHUNK_PREAMBLE_STRUCT.size)
        if len(preamble) != CHUNK_PREAMBLE_STRUCT.size:
            raise ValueError(f"{path} is too short for an ANXP preamble")
        magic, format_version, record_version, header_size, record_count = CHUNK_PREAMBLE_STRUCT.unpack(preamble)
        if magic != ANXP_MAGIC:
            raise ValueError(f"{path} has invalid ANXP magic")
        if format_version != ANXP_FORMAT_VERSION or record_version != ANXP_RECORD_VERSION:
            raise ValueError(f"{path} has unsupported ANXP version")
        header = json.loads(handle.read(header_size).decode("utf-8"))
        records = []
        for _ in range(record_count):
            payload = handle.read(ANXP_RECORD_SIZE_BYTES)
            if len(payload) != ANXP_RECORD_SIZE_BYTES:
                raise ValueError(f"{path} ended inside an SPCC record")
            records.append(unpack_record(payload))
    return header, records


def read_curve_csv(path: Path) -> list[dict[str, float]]:
    samples: list[dict[str, float]] = []
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            wavelength = float(row.get("wavelength_nm") or row.get("wavelength") or "")
            value = float(row["value"])
            if not math.isfinite(wavelength) or not math.isfinite(value):
                raise ValueError(f"{path} contains non-finite curve samples")
            if value < 0.0:
                raise ValueError(f"{path} contains a negative response value")
            samples.append({"wavelength_nm": wavelength, "value": value})
    validate_curve_samples(samples, path)
    return samples


def validate_curve_samples(samples: list[dict[str, float]], path: Path | None = None) -> None:
    if len(samples) < 2:
        raise ValueError(f"{path or 'curve'} must contain at least two samples")
    previous = -math.inf
    for sample in samples:
        wavelength = float(sample["wavelength_nm"])
        value = float(sample["value"])
        if not math.isfinite(wavelength) or not math.isfinite(value) or value < 0.0:
            raise ValueError(f"{path or 'curve'} contains invalid samples")
        if wavelength <= previous:
            raise ValueError(f"{path or 'curve'} wavelengths must be strictly increasing")
        previous = wavelength


def curve_data_sha256(samples: list[dict[str, float]]) -> str:
    normalized = [
        {"wavelength_nm": round(float(sample["wavelength_nm"]), 9), "value": round(float(sample["value"]), 12)}
        for sample in samples
    ]
    return sha256_bytes(canonical_json_bytes(normalized))
