# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Workflow calibration helpers for the public AfterNight SDK."""

from __future__ import annotations

import os
from typing import Any

from . import _require_runtime_module, io
from .sequences import SequenceHandle, create


def _metadata_value(metadata: dict[str, Any], *keys: str) -> str:
    upper_metadata = {str(key).upper(): value for key, value in dict(metadata or {}).items()}
    for key in keys:
        value = upper_metadata.get(key.upper())
        if value is not None:
            return str(value)
    return ""


def _role_for_frame(frame) -> str:
    role = _metadata_value(frame.metadata, "IMAGETYP", "FRAME", "ROLE").upper()
    if "BIAS" in role:
        return "bias"
    if "DARK" in role:
        return "dark"
    if "FLAT" in role:
        return "flat"
    if "LIGHT" in role:
        return "light"
    return role.lower()


def _filter_for_frame(frame) -> str:
    return _metadata_value(frame.metadata, "FILTER", "FILTER_NAME") or "L"


def _included_role_frames(sequence: SequenceHandle, role: str, filter_name: str | None = None):
    frames = []
    for frame in sequence.frames():
        if not frame.included or _role_for_frame(frame) != role:
            continue
        if filter_name is not None and _filter_for_frame(frame) != str(filter_name):
            continue
        frames.append(frame)
    return frames


def _materialize_frame_path(frame, output_dir: str, output_index: int, prefix: str) -> str:
    if frame.path and os.path.exists(frame.path):
        return os.fspath(frame.path)

    if frame.image is None:
        raise FileNotFoundError("Workflow sequence frame has no image or path")

    input_dir = os.path.join(output_dir, "_calibration_inputs")
    os.makedirs(input_dir, exist_ok=True)
    path = os.path.join(input_dir, f"{prefix}_{output_index:03d}.fit")
    io.save(frame.image, path)
    return path


def _master_path_map(masters: dict[str, dict[str, Any]]) -> dict[str, str]:
    paths: dict[str, str] = {}
    for role in ("bias", "dark", "flat"):
        record = dict(masters.get(role, {}) or {})
        path = os.fspath(record.get("path", "") or "")
        if path:
            paths[role] = path
    return paths


def create_masters(
    sequence: SequenceHandle,
    output_dir: str,
    filter_name: str = "L",
    *,
    method: str = "average",
    params=None,
    save_options=None,
):
    """Create bias, dark, and flat masters through AfterNightEngine."""

    output_dir = os.fspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)

    role_paths: dict[str, list[str]] = {}
    for role in ("bias", "dark", "flat"):
        frames = _included_role_frames(sequence, role, filter_name)
        if not frames:
            continue
        role_paths[role] = [
            _materialize_frame_path(frame, output_dir, index, role)
            for index, frame in enumerate(frames, start=1)
        ]

    if not role_paths:
        return {}

    runtime_module = _require_runtime_module()
    return dict(runtime_module.create_calibration_masters_paths(
        role_paths,
        output_dir,
        str(filter_name),
        str(method),
        params,
        save_options,
    ))


def calibrate_lights(
    sequence: SequenceHandle,
    masters: dict[str, dict[str, Any]],
    output_dir: str,
    *,
    filter_name: str = "L",
    name: str = "calibrated_lights",
    params=None,
    output_options=None,
):
    """Apply calibration masters through AfterNightEngine and return a sequence."""

    output_dir = os.fspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)
    calibrated = create(name)

    frames = _included_role_frames(sequence, "light", filter_name)
    if not frames:
        return calibrated

    source_paths = [
        _materialize_frame_path(frame, output_dir, index, "light")
        for index, frame in enumerate(frames, start=1)
    ]
    runtime_module = _require_runtime_module()
    native_result = runtime_module.calibrate_sequence_paths(
        source_paths,
        _master_path_map(masters),
        output_dir,
        params,
        output_options,
    )

    native_frames = {
        os.path.abspath(str(frame.get("input_path", ""))): dict(frame)
        for frame in list(native_result.get("frames", []))
    }

    for frame, source_path in zip(frames, source_paths, strict=True):
        native_frame = native_frames.get(os.path.abspath(source_path))
        if not native_frame:
            raise RuntimeError(f"Native calibration did not return a result for {source_path}")
        if not bool(native_frame.get("succeeded", False)):
            detail = str(native_frame.get("error_message", "") or "calibration failed")
            raise RuntimeError(f"Native calibration failed for {source_path}: {detail}")

        destination_path = os.fspath(native_frame.get("output_path", ""))
        if not destination_path:
            raise RuntimeError(f"Native calibration did not produce an output for {source_path}")

        metadata = dict(frame.metadata)
        metadata.update({
            "CALIBRATED": True,
            "CALMETHOD": "native_engine",
            "FILTER": filter_name,
        })
        calibrated.add_frame(
            path=destination_path,
            metadata=metadata,
            registration=dict(frame.registration),
            included=frame.included,
        )

    return calibrated


__all__ = [
    "calibrate_lights",
    "create_masters",
]
