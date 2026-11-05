#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import shutil
import sys
import zipfile
from collections import defaultdict
from pathlib import Path
from typing import Any

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import spcc_tool_common as spcc


DEFAULT_SOURCE_REPOSITORY_URL = "https://gitlab.com/free-astro/siril-spcc-database"
DEFAULT_LICENSE_NAME = "GPL-3.0-only"
DEFAULT_REPOSITORY_ID = "afternight-spcc-response-baseline"
AFTERNIGHT_SPCC_ID_NAMESPACE = "afternight.spcc"
PRODUCTION_REVIEW_STATUSES = {"approved-for-bundling", "approved-for-managed-download"}
SOURCE_RESPONSE_DIRS = ("osc_sensors", "mono_sensors", "mono_filters", "osc_filters", "wb_refs")
IMPORTABLE_TYPES = {"OSC_SENSOR", "MONO_SENSOR", "MONO_FILTER", "OSC_FILTER", "OSC_LPF", "WB_REF"}
RGB_CHANNELS = ("red", "green", "blue")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Import a pinned SPCC response database checkout or ZIP archive into an AfterNight response source manifest "
            "and curve CSV work area."
        )
    )
    parser.add_argument("source_database", type=Path, help="SPCC response source checkout directory or ZIP archive.")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--repository-id", default=DEFAULT_REPOSITORY_ID)
    parser.add_argument("--version", default="2026a")
    parser.add_argument("--source-repository-url", default=DEFAULT_SOURCE_REPOSITORY_URL)
    parser.add_argument(
        "--source-revision",
        required=True,
        help="Pinned source database commit, tag, or revision used as provenance for generated source URLs.",
    )
    parser.add_argument(
        "--source-acquired-at-utc",
        default=spcc.utc_now_iso(),
        help="UTC timestamp when the source checkout/export was acquired.",
    )
    parser.add_argument(
        "--review-status",
        default="license-review-needed",
        choices=["private-local", "license-review-needed", "approved-for-bundling", "approved-for-managed-download", "blocked"],
    )
    parser.add_argument(
        "--reviewer",
        default="",
        help="Reviewer name or handle. Required when --review-status is approved for production use.",
    )
    parser.add_argument(
        "--reviewed-at-utc",
        default="",
        help="UTC timestamp for the review. Defaults to --source-acquired-at-utc for approved imports.",
    )
    parser.add_argument("--license-name", default=DEFAULT_LICENSE_NAME)
    parser.add_argument(
        "--license-url",
        default="",
        help="License URL. Defaults to the pinned source LICENSE.md blob URL.",
    )
    parser.add_argument(
        "--default-white-reference",
        default="",
        help="White-reference model/name to use for generated profiles. Defaults to the first imported white reference.",
    )
    parser.add_argument(
        "--include-all-white-references",
        action="store_true",
        help="Generate profiles for every imported white reference instead of only the default white reference.",
    )
    parser.add_argument(
        "--include-osc-filter-lpf-combinations",
        dest="include_osc_filter_lpf_combinations",
        action="store_true",
        default=True,
        help="Generate OSC sensor + OSC filter + OSC LPF profile combinations. This is the default.",
    )
    parser.add_argument(
        "--skip-osc-filter-lpf-combinations",
        dest="include_osc_filter_lpf_combinations",
        action="store_false",
        help="Skip generated OSC sensor + OSC filter + OSC LPF profile combinations.",
    )
    parser.add_argument(
        "--max-generated-profiles",
        type=int,
        default=5000,
        help="Safety cap for generated throughput profiles.",
    )
    parser.add_argument("--clean", action="store_true", help="Delete the output directory before writing.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        import_response_database(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


def import_response_database(args: argparse.Namespace) -> None:
    source_input = args.source_database
    output_dir = args.output_dir
    if not source_input.exists():
        raise ValueError(f"SPCC response source input is missing: {source_input}")
    if args.review_status in PRODUCTION_REVIEW_STATUSES and not args.reviewer.strip():
        raise ValueError("--reviewer is required for approved SPCC response database imports")
    if args.max_generated_profiles <= 0:
        raise ValueError("--max-generated-profiles must be positive")
    if output_dir.exists() and args.clean:
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    curves_dir = output_dir / "curves"
    curves_dir.mkdir(parents=True, exist_ok=True)

    source_entries = load_source_entries(source_input)
    components: list[dict[str, Any]] = []
    white_references: list[dict[str, Any]] = []
    component_refs: list[dict[str, Any]] = []
    white_refs: list[dict[str, Any]] = []
    used_ids: set[str] = set()

    review_context = build_review_context(args)
    for entry in source_entries:
        source_type = entry["type"]
        if source_type == "WB_REF":
            white_ref = convert_white_reference(entry, curves_dir, review_context, used_ids)
            white_references.append(white_ref["manifest"])
            white_refs.append(white_ref["ref"])
        else:
            component = convert_component(entry, curves_dir, review_context, used_ids)
            components.append(component["manifest"])
            component_refs.append(component["ref"])

    if not components:
        raise ValueError("SPCC response import produced no response components")
    if not white_references:
        raise ValueError("SPCC response import produced no white references")

    selected_white_refs = select_white_references(white_refs, args.default_white_reference, args.include_all_white_references)
    profiles = build_profiles(
        component_refs,
        selected_white_refs,
        include_osc_filter_lpf_combinations=args.include_osc_filter_lpf_combinations,
        max_generated_profiles=args.max_generated_profiles,
    )
    if not profiles:
        raise ValueError("SPCC response import could not generate any complete RGB throughput profiles")

    manifest = {
        "repository_id": args.repository_id,
        "version": str(args.version),
        "rights": build_rights(args, review_context),
        "components": components,
        "white_references": white_references,
        "throughput_profiles": profiles,
    }
    spcc.write_json(output_dir / "response_source_manifest.json", manifest)

    print(
        "Imported SPCC response database: "
        f"{len(components)} components, {len(white_references)} white references, {len(profiles)} profiles"
    )
    print(f"AfterNight response source manifest: {output_dir / 'response_source_manifest.json'}")


def load_source_entries(root: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for relative_path, raw in iter_source_json_documents(root):
        objects = raw if isinstance(raw, list) else [raw]
        for index, obj in enumerate(objects):
            if not isinstance(obj, dict):
                raise ValueError(f"{relative_path} entry {index} is not an object")
            source_type = str(obj.get("type", "")).strip().upper()
            if source_type not in IMPORTABLE_TYPES:
                continue
            entry = dict(obj)
            entry["type"] = source_type
            entry["_source_path"] = relative_path
            validate_source_entry(entry, relative_path, index)
            entries.append(entry)

    if not entries:
        raise ValueError(f"No importable SPCC response JSON entries found under {root}")
    return entries


def iter_source_json_documents(root: Path) -> list[tuple[str, Any]]:
    if root.is_dir():
        return iter_source_json_documents_from_dir(root)
    if root.is_file() and root.suffix.lower() == ".zip":
        return iter_source_json_documents_from_zip(root)
    raise ValueError(f"SPCC response database input must be a directory or .zip archive: {root}")


def iter_source_json_documents_from_dir(root: Path) -> list[tuple[str, Any]]:
    documents: list[tuple[str, Any]] = []
    candidate_files: list[Path] = []
    for dirname in SOURCE_RESPONSE_DIRS:
        candidate_dir = root / dirname
        if candidate_dir.is_dir():
            candidate_files.extend(sorted(candidate_dir.glob("*.json")))
    candidate_files.extend(
        path
        for path in sorted(root.glob("*.json"))
        if path.name not in {"spcc-database-schema.json"} and not path.name.startswith(".")
    )

    for path in candidate_files:
        documents.append((path.relative_to(root).as_posix(), spcc.read_json(path)))
    return documents


def iter_source_json_documents_from_zip(path: Path) -> list[tuple[str, Any]]:
    documents: list[tuple[str, Any]] = []
    with zipfile.ZipFile(path) as archive:
        for info in sorted(archive.infolist(), key=lambda item: item.filename):
            if info.is_dir() or not info.filename.endswith(".json"):
                continue
            relative_path = strip_archive_root(info.filename)
            if not is_candidate_source_json_path(relative_path):
                continue
            with archive.open(info) as handle:
                documents.append((relative_path, json.loads(handle.read().decode("utf-8"))))
    return documents


def strip_archive_root(path: str) -> str:
    normalized = path.replace("\\", "/").lstrip("/")
    parts = [part for part in normalized.split("/") if part]
    if len(parts) > 1 and parts[0] not in SOURCE_RESPONSE_DIRS and parts[0] != ".":
        parts = parts[1:]
    return "/".join(parts)


def is_candidate_source_json_path(relative_path: str) -> bool:
    parts = relative_path.split("/")
    if not parts:
        return False
    if len(parts) == 1:
        return parts[0] not in {"spcc-database-schema.json"} and not parts[0].startswith(".")
    return parts[0] in SOURCE_RESPONSE_DIRS and not parts[-1].startswith(".")


def validate_source_entry(entry: dict[str, Any], path: str, index: int) -> None:
    wavelength = source_value_array(entry, "wavelength")
    values = source_value_array(entry, "values")
    if not isinstance(wavelength, list) or not isinstance(values, list):
        raise ValueError(f"{path} entry {index} must contain wavelength and values arrays")
    if len(wavelength) != len(values) or len(wavelength) < 2:
        raise ValueError(f"{path} entry {index} wavelength and values arrays must have matching length >= 2")
    if not str(entry.get("model") or entry.get("name") or "").strip():
        raise ValueError(f"{path} entry {index} must contain model or name")


def build_review_context(args: argparse.Namespace) -> dict[str, str]:
    source_url = args.source_repository_url.rstrip("/")
    revision = args.source_revision.strip()
    license_url = args.license_url.strip() or f"{source_url}/-/blob/{revision}/LICENSE.md"
    reviewed_at = args.reviewed_at_utc.strip()
    if not reviewed_at and args.review_status in PRODUCTION_REVIEW_STATUSES:
        reviewed_at = args.source_acquired_at_utc
    return {
        "source_repository_url": source_url,
        "source_revision": revision,
        "source_acquired_at_utc": args.source_acquired_at_utc,
        "review_status": args.review_status,
        "reviewer": args.reviewer.strip(),
        "reviewed_at_utc": reviewed_at,
        "license_name": args.license_name,
        "license_url": license_url,
    }


def build_rights(args: argparse.Namespace, context: dict[str, str]) -> dict[str, str]:
    approved = args.review_status in PRODUCTION_REVIEW_STATUSES
    if approved:
        redistribution = "Allowed under GPLv3 terms with preserved license, source availability, and attribution."
        derivative = "Allowed under GPLv3 terms for normalized AfterNight response payloads."
        managed = "Allowed under GPLv3 terms for AfterNight-managed downloads."
        bundling = "Allowed under GPLv3 terms for GPL-compatible AfterNight distributions."
    else:
        redistribution = "Pending review; do not publish as production managed data."
        derivative = "Pending review; conversion is for local review only."
        managed = "Pending review."
        bundling = "Pending review."

    return {
        "review_status": args.review_status,
        "license_name": context["license_name"],
        "license_url": context["license_url"],
        "reviewer": context["reviewer"],
        "reviewed_at_utc": context["reviewed_at_utc"],
        "redistribution_permission": redistribution,
        "managed_download_permission": managed,
        "bundling_permission": bundling,
        "derivative_permission": derivative,
        "required_attribution": "Siril SPCC Database contributors; Free-Astro/Siril project.",
        "source_repository_url": context["source_repository_url"],
        "source_revision": context["source_revision"],
        "source_acquired_at_utc": context["source_acquired_at_utc"],
        "notes": "Imported from a pinned upstream SPCC response database checkout and converted into AfterNight response assets.",
    }


def convert_component(
    entry: dict[str, Any], curves_dir: Path, context: dict[str, str], used_ids: set[str]
) -> dict[str, Any]:
    source_type = entry["type"]
    model_slug = slug(entry.get("model") or entry.get("name"))
    channel = normalize_channel_label(entry.get("channel"))
    channels = normalize_channels(entry.get("channel"))
    type_slug = source_type.lower()
    if source_type in {"OSC_SENSOR", "MONO_FILTER"}:
        base_id = f"{AFTERNIGHT_SPCC_ID_NAMESPACE}.{type_slug.lower()}.{model_slug}.{channel}"
    else:
        base_id = f"{AFTERNIGHT_SPCC_ID_NAMESPACE}.{type_slug.lower()}.{model_slug}"
    component_id = unique_id(base_id, used_ids)
    curve_csv = write_curve(curves_dir, component_id, entry)
    manifest = {
        "component_id": component_id,
        "version": str(entry.get("version", "1")),
        "role": component_role(source_type, channel),
        "curve_csv": curve_csv,
        "wavelength_unit": "nm",
        "value_semantics": "dimensionless-throughput",
        "interpolation": "akima",
        "out_of_range": "zero",
        "normalization": "preserve-relative-shape",
        "channel": channel,
        **asset_provenance(entry, context),
    }
    ref = {
        "id": component_id,
        "type": source_type,
        "model": str(entry.get("model") or entry.get("name")).strip(),
        "name": str(entry.get("name") or entry.get("model")).strip(),
        "model_slug": model_slug,
        "channel": channel,
        "channels": channels,
        "manufacturer": str(entry.get("manufacturer", "")).strip(),
    }
    return {"manifest": manifest, "ref": ref}


def convert_white_reference(
    entry: dict[str, Any], curves_dir: Path, context: dict[str, str], used_ids: set[str]
) -> dict[str, Any]:
    model_slug = slug(entry.get("model") or entry.get("name"))
    white_reference_id = unique_id(f"{AFTERNIGHT_SPCC_ID_NAMESPACE}.white_reference.{model_slug}", used_ids)
    curve_csv = write_curve(curves_dir, white_reference_id, entry, white_reference_photon_count=True)
    manifest = {
        "white_reference_id": white_reference_id,
        "version": str(entry.get("version", "1")),
        "role": "white-reference",
        "curve_csv": curve_csv,
        "wavelength_unit": "nm",
        "value_semantics": "relative-photon-count",
        "interpolation": "akima",
        "out_of_range": "zero",
        "normalization": "white-reference-wavelength-weighted-550nm-value",
        "photon_count_weighted": True,
        "white_reference_normalization_wavelength_nm": 550.0,
        **asset_provenance(entry, context),
    }
    ref = {
        "id": white_reference_id,
        "model": str(entry.get("model") or entry.get("name")).strip(),
        "name": str(entry.get("name") or entry.get("model")).strip(),
        "model_slug": model_slug,
    }
    return {"manifest": manifest, "ref": ref}


def asset_provenance(entry: dict[str, Any], context: dict[str, str]) -> dict[str, Any]:
    source_path = str(entry["_source_path"])
    source_url = f"{context['source_repository_url']}/-/blob/{context['source_revision']}/{source_path}"
    data_quality = int(entry.get("dataQualityMarker", 0))
    data_source = str(entry.get("dataSource", "")).strip()
    source_citation = (
        "Siril SPCC Database contributors, "
        f"free-astro/siril-spcc-database {context['source_revision']}, {source_path}."
    )
    transformation_notes = (
        "Converted upstream SPCC JSON wavelength/value arrays to AfterNight CSV curves; "
        "wavelength units converted to nm; values divided by the upstream range field when present; "
        "negative digitization artifacts clipped to zero; samples sorted by wavelength and duplicate wavelengths "
        "collapsed by keeping the last value. "
    )
    if entry["type"] == "WB_REF":
        transformation_notes += (
            "White-reference values were converted with the upstream SPCC convention: value multiplied by wavelength "
            "and divided by the first sample value at or above 550 nm. "
        )
    transformation_notes += (
        f"Upstream type={entry['type']}; dataQualityMarker={data_quality}; dataSource={data_source or 'not provided'}."
    )
    return {
        "review_status": context["review_status"],
        "license_name": context["license_name"],
        "license_url": context["license_url"],
        "source_url": source_url,
        "source_citation": source_citation,
        "attribution_text": "Contains data adapted from the Siril SPCC Database contributors under GPLv3.",
        "source_acquired_at_utc": context["source_acquired_at_utc"],
        "reviewer": context["reviewer"],
        "reviewed_at_utc": context["reviewed_at_utc"],
        "redistribution_permission": "Allowed under GPLv3 terms with preserved license and attribution."
        if context["review_status"] in PRODUCTION_REVIEW_STATUSES
        else "Pending review.",
        "derivative_permission": "Allowed under GPLv3 terms for normalized AfterNight response payloads."
        if context["review_status"] in PRODUCTION_REVIEW_STATUSES
        else "Pending review.",
        "transformation_notes": transformation_notes,
        "source_upstream_type": entry["type"],
        "source_upstream_model": str(entry.get("model", "")).strip(),
        "source_upstream_name": str(entry.get("name", "")).strip(),
        "source_upstream_manufacturer": str(entry.get("manufacturer", "")).strip(),
        "source_upstream_version": str(entry.get("version", "")).strip(),
        "source_upstream_data_quality_marker": data_quality,
        "source_upstream_data_source": data_source,
        "source_upstream_path": source_path,
    }


def write_curve(
    curves_dir: Path, asset_id: str, entry: dict[str, Any], *, white_reference_photon_count: bool = False
) -> str:
    samples = normalize_samples(entry, white_reference_photon_count=white_reference_photon_count)
    output_path = curves_dir / f"{asset_id}.csv"
    with output_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["wavelength_nm", "value"])
        for wavelength_nm, value in samples:
            writer.writerow([format_float(wavelength_nm), format_float(value)])
    return output_path.relative_to(curves_dir.parent).as_posix()


def normalize_samples(entry: dict[str, Any], *, white_reference_photon_count: bool = False) -> list[tuple[float, float]]:
    unit_scale = wavelength_scale_to_nm(source_wavelength_unit(entry))
    value_range = parse_float(source_value_range(entry), "range")
    if not math.isfinite(value_range) or value_range <= 0.0:
        raise ValueError(f"{entry['_source_path']} has invalid range: {entry.get('range')}")

    samples: list[tuple[float, float]] = []
    for wavelength_raw, value_raw in zip(source_value_array(entry, "wavelength"), source_value_array(entry, "values")):
        wavelength_nm = parse_float(wavelength_raw, "wavelength") * unit_scale
        value = parse_float(value_raw, "value") / value_range
        if value < 0.0:
            value = 0.0
        if not math.isfinite(wavelength_nm) or not math.isfinite(value) or value < 0.0:
            raise ValueError(f"{entry['_source_path']} contains invalid curve samples")
        samples.append((wavelength_nm, value))

    samples.sort(key=lambda item: item[0])
    deduped: list[tuple[float, float]] = []
    for wavelength_nm, value in samples:
        if deduped and abs(deduped[-1][0] - wavelength_nm) <= 1.0e-12:
            deduped[-1] = (wavelength_nm, value)
        else:
            deduped.append((wavelength_nm, value))
    if len(deduped) < 2:
        raise ValueError(f"{entry['_source_path']} must contain at least two unique wavelength samples")
    if white_reference_photon_count:
        norm_index = 0
        while norm_index < len(deduped) - 1 and deduped[norm_index][0] < 550.0:
            norm_index += 1
        norm = deduped[norm_index][1]
        if not math.isfinite(norm) or norm <= 0.0:
            raise ValueError(f"{entry['_source_path']} has invalid white-reference 550 nm normalization value")
        deduped = [(wavelength_nm, (value * wavelength_nm) / norm) for wavelength_nm, value in deduped]
    return deduped


def source_value_array(entry: dict[str, Any], key: str) -> list[Any]:
    value = entry.get(key)
    if isinstance(value, dict):
        nested = value.get("value")
        return nested if isinstance(nested, list) else []
    return value if isinstance(value, list) else []


def source_wavelength_unit(entry: dict[str, Any]) -> str:
    wavelength = entry.get("wavelength")
    if isinstance(wavelength, dict):
        return str(wavelength.get("units", "nm"))
    return str(entry.get("units", "nm"))


def source_value_range(entry: dict[str, Any]) -> Any:
    values = entry.get("values")
    if isinstance(values, dict):
        return values.get("range", entry.get("range", 1.0))
    return entry.get("range", 1.0)


def wavelength_scale_to_nm(unit: str) -> float:
    normalized = unit.strip().lower().replace("μ", "u")
    aliases = {
        "angstrom": 0.1,
        "angstroms": 0.1,
        "a": 0.1,
        "nm": 1.0,
        "nanometer": 1.0,
        "nanometers": 1.0,
        "micrometre": 1000.0,
        "micrometres": 1000.0,
        "micrometer": 1000.0,
        "micrometers": 1000.0,
        "um": 1000.0,
        "m": 1.0e9,
    }
    if normalized not in aliases:
        raise ValueError(f"unsupported source wavelength units: {unit}")
    return aliases[normalized]


def parse_float(value: Any, label: str) -> float:
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"invalid numeric {label}: {value}") from exc


def build_profiles(
    components: list[dict[str, Any]],
    white_refs: list[dict[str, Any]],
    *,
    include_osc_filter_lpf_combinations: bool,
    max_generated_profiles: int,
) -> list[dict[str, Any]]:
    by_type_model: dict[str, dict[str, list[dict[str, Any]]]] = defaultdict(lambda: defaultdict(list))
    for component in components:
        by_type_model[component["type"]][component["model_slug"]].append(component)

    profiles: list[dict[str, Any]] = []
    profile_namespace = f"{AFTERNIGHT_SPCC_ID_NAMESPACE}.profile"
    for white_ref in white_refs:
        suffix = white_ref["profile_suffix"]
        for sensor_slug, sensor_components in sorted(by_type_model["OSC_SENSOR"].items()):
            rgb = rgb_component_ids(sensor_components)
            if rgb is None:
                continue
            sensor_model = sensor_components[0]["model"]
            profiles.append(
                make_profile(
                    f"{profile_namespace}.osc_sensor.{sensor_slug}.{suffix}",
                    rgb,
                    white_ref["id"],
                    ["camera-rgb", "linear-rgb"],
                    [{"camera_model": sensor_model, "filter": ""}],
                )
            )

            all_channel_filters = list(all_channel_components(by_type_model["OSC_FILTER"]))
            all_channel_lpfs = list(all_channel_components(by_type_model["OSC_LPF"]))
            for filter_component in all_channel_filters:
                profiles.append(
                    make_profile(
                        f"{profile_namespace}.osc_sensor.{sensor_slug}.osc_filter.{filter_component['model_slug']}.{suffix}",
                        add_all_channel_component(rgb, filter_component["id"]),
                        white_ref["id"],
                        ["camera-rgb", "linear-rgb"],
                        [{"camera_model": sensor_model, "filter": filter_component["model"]}],
                    )
                )
            for lpf_component in all_channel_lpfs:
                profiles.append(
                    make_profile(
                        f"{profile_namespace}.osc_sensor.{sensor_slug}.osc_lpf.{lpf_component['model_slug']}.{suffix}",
                        add_all_channel_component(rgb, lpf_component["id"]),
                        white_ref["id"],
                        ["camera-rgb", "linear-rgb"],
                        [{"camera_model": sensor_model, "filter": lpf_component["model"]}],
                    )
                )
            if include_osc_filter_lpf_combinations:
                for filter_component in all_channel_filters:
                    for lpf_component in all_channel_lpfs:
                        profiles.append(
                            make_profile(
                                (
                                    f"{profile_namespace}.osc_sensor.{sensor_slug}.osc_filter."
                                    f"{filter_component['model_slug']}.osc_lpf.{lpf_component['model_slug']}.{suffix}"
                                ),
                                add_all_channel_component(
                                    add_all_channel_component(rgb, filter_component["id"]), lpf_component["id"]
                                ),
                                white_ref["id"],
                                ["camera-rgb", "linear-rgb"],
                                [
                                    {
                                        "camera_model": sensor_model,
                                        "filter": f"{filter_component['model']} + {lpf_component['model']}",
                                    }
                                ],
                            )
                        )

        mono_filter_sets = {
            model_slug: rgb_component_ids(entries)
            for model_slug, entries in sorted(by_type_model["MONO_FILTER"].items())
        }
        mono_filter_sets = {model_slug: value for model_slug, value in mono_filter_sets.items() if value is not None}
        for sensor_slug, sensor_entries in sorted(by_type_model["MONO_SENSOR"].items()):
            sensor_component = sensor_entries[0]
            sensor_only_components = {channel: [sensor_component["id"]] for channel in RGB_CHANNELS}
            profiles.append(
                make_profile(
                    f"{profile_namespace}.mono_sensor.{sensor_slug}.{suffix}",
                    sensor_only_components,
                    white_ref["id"],
                    ["linear-rgb"],
                    [{"camera_model": sensor_component["model"], "filter": ""}],
                )
            )
            for filter_slug, filter_rgb in mono_filter_sets.items():
                assert filter_rgb is not None
                channel_components = {
                    channel: [sensor_component["id"], filter_rgb[channel]] for channel in RGB_CHANNELS
                }
                filter_model = by_type_model["MONO_FILTER"][filter_slug][0]["model"]
                profiles.append(
                    make_profile(
                        f"{profile_namespace}.mono_sensor.{sensor_slug}.mono_filter.{filter_slug}.{suffix}",
                        channel_components,
                        white_ref["id"],
                        ["linear-rgb"],
                        [{"camera_model": sensor_component["model"], "filter": filter_model}],
                    )
                )

        if not by_type_model["MONO_SENSOR"]:
            for filter_slug, filter_rgb in mono_filter_sets.items():
                assert filter_rgb is not None
                profiles.append(
                    make_profile(
                        f"{profile_namespace}.mono_filter.{filter_slug}.{suffix}",
                        {channel: [component_id] for channel, component_id in filter_rgb.items()},
                        white_ref["id"],
                        ["linear-rgb"],
                        [{"camera_model": "", "filter": by_type_model["MONO_FILTER"][filter_slug][0]["model"]}],
                    )
                )

        if len(profiles) > max_generated_profiles:
            raise ValueError(
                f"SPCC response import generated {len(profiles)} profiles, exceeding --max-generated-profiles "
                f"({max_generated_profiles})"
            )

    return profiles


def select_white_references(
    white_refs: list[dict[str, Any]], default_white_reference: str, include_all: bool
) -> list[dict[str, Any]]:
    if not white_refs:
        return []
    default_key = normalized_lookup_key(default_white_reference)
    selected_default = white_refs[0]
    if default_key:
        for ref in white_refs:
            if default_key in {normalized_lookup_key(ref["model"]), normalized_lookup_key(ref["name"])}:
                selected_default = ref
                break
        else:
            raise ValueError(f"default white reference was not found in source import: {default_white_reference}")

    if include_all:
        selected = []
        for ref in white_refs:
            item = dict(ref)
            item["profile_suffix"] = item["model_slug"]
            selected.append(item)
        return selected
    item = dict(selected_default)
    item["profile_suffix"] = "default"
    return [item]


def rgb_component_ids(entries: list[dict[str, Any]]) -> dict[str, str] | None:
    by_channel: dict[str, str] = {}
    for entry in entries:
        for channel in entry.get("channels", [entry["channel"]]):
            if channel in RGB_CHANNELS and channel not in by_channel:
                by_channel[channel] = entry["id"]
    if all(channel in by_channel for channel in RGB_CHANNELS):
        return by_channel
    return None


def all_channel_components(by_model: dict[str, list[dict[str, Any]]]) -> list[dict[str, Any]]:
    components = []
    for entries in by_model.values():
        if entries:
            components.append(entries[0])
    return sorted(components, key=lambda component: component["id"])


def add_all_channel_component(channel_components: dict[str, list[str] | str], component_id: str) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    for channel in RGB_CHANNELS:
        value = channel_components[channel]
        result[channel] = list(value) if isinstance(value, list) else [value]
        result[channel].append(component_id)
    return result


def make_profile(
    profile_id: str,
    channel_components: dict[str, list[str] | str],
    white_reference_id: str,
    compatible_channel_bases: list[str],
    aliases: list[dict[str, str]],
) -> dict[str, Any]:
    normalized_channel_components = {
        channel: list(value) if isinstance(value, list) else [value] for channel, value in channel_components.items()
    }
    component_ids = unique_ordered(
        component_id for channel in RGB_CHANNELS for component_id in normalized_channel_components[channel]
    )
    return {
        "profile_id": profile_id,
        "version": "1",
        "component_ids": component_ids,
        "channel_component_ids": normalized_channel_components,
        "white_reference_id": white_reference_id,
        "compatible_channel_bases": compatible_channel_bases,
        "models_raw_white_balance": False,
        "models_camera_matrix": False,
        "algorithm_version": "spcc-synthetic-photometry-v2",
        "aliases": aliases,
    }


def unique_ordered(values: Any) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for value in values:
        if value not in seen:
            result.append(value)
            seen.add(value)
    return result


def component_role(source_type: str, channel: str) -> str:
    if source_type == "OSC_SENSOR":
        return f"sensor-{channel}-response"
    if source_type == "MONO_SENSOR":
        return "sensor-luminance-response"
    if source_type == "MONO_FILTER":
        return f"filter-{channel}-transmission"
    if source_type == "OSC_LPF":
        return "lpf-transmission"
    return "filter-transmission"


def normalize_channels(value: Any) -> list[str]:
    normalized = str(value or "").strip().lower()
    if not normalized or normalized in {"all", "rgb", "red green blue", "blue green red"}:
        return list(RGB_CHANNELS)

    aliases = {"r": "red", "red": "red", "g": "green", "green": "green", "b": "blue", "blue": "blue"}
    channels: list[str] = []
    for token in re.split(r"[^a-z0-9]+", normalized):
        channel = aliases.get(token)
        if channel is not None and channel not in channels:
            channels.append(channel)

    if channels:
        return channels
    if normalized in {"l", "lum", "luminance"}:
        return ["luminance"]
    return list(RGB_CHANNELS)


def normalize_channel_label(value: Any) -> str:
    normalized = str(value or "").strip().lower()
    channels = normalize_channels(value)
    if channels == list(RGB_CHANNELS) and (
        not normalized or normalized in {"all", "rgb", "red green blue", "blue green red"}
    ):
        return "all"
    if len(channels) == 1:
        return channels[0]
    return "_".join(channels)


def slug(value: Any) -> str:
    text = str(value or "").strip().lower()
    text = re.sub(r"[^a-z0-9]+", "_", text)
    return text.strip("_") or "unnamed"


def normalized_lookup_key(value: str) -> str:
    if not str(value or "").strip():
        return ""
    return slug(value).replace("_", "")


def unique_id(base_id: str, used_ids: set[str]) -> str:
    candidate = base_id
    suffix = 2
    while candidate in used_ids:
        candidate = f"{base_id}.{suffix}"
        suffix += 1
    used_ids.add(candidate)
    return candidate


def format_float(value: float) -> str:
    return f"{value:.12g}"


if __name__ == "__main__":
    raise SystemExit(main())
