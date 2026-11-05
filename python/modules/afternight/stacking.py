# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Workflow stacking helpers for the public AfterNight SDK."""

from __future__ import annotations

import os

from . import _require_runtime_module, io
from .sequences import SequenceHandle


def _materialize_included_frames(sequence: SequenceHandle, output_dir: str):
    frames = sequence.included_frames()
    if not frames:
        raise ValueError("Cannot stack an empty workflow sequence")

    source_paths = [
        _materialize_frame_path(frame, output_dir, index)
        for index, frame in enumerate(frames, start=1)
    ]
    return frames, source_paths


def _materialize_frame_path(frame, output_dir: str, output_index: int) -> str:
    if frame.path and os.path.exists(frame.path):
        return os.fspath(frame.path)

    if frame.image is None:
        raise FileNotFoundError("Workflow sequence frame has no image or path")

    input_dir = os.path.join(output_dir, "_stacking_inputs")
    os.makedirs(input_dir, exist_ok=True)
    path = os.path.join(input_dir, f"input_{output_index:03d}.fit")
    io.save(frame.image, path)
    return path


def _identity_registration():
    return {
        "method": "reference",
        "transform_type": "similarity",
        "matrix": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
    }


def _registration_for_frame(frame, index: int, reference_index: int):
    registration = dict(frame.registration or {})
    if registration:
        return registration
    if index == reference_index:
        return _identity_registration()
    raise ValueError(
        "Drizzle stacking requires registration metadata for every non-reference frame"
    )


def stack(
    sequence: SequenceHandle,
    output_path: str,
    *,
    method: str = "average",
    metadata=None,
    params=None,
    save_options=None,
):
    """Stack included frames through AfterNightEngine's native stacking pipeline."""

    output_path = os.fspath(output_path)
    output_dir = os.path.dirname(output_path) or "."
    os.makedirs(output_dir, exist_ok=True)
    _, source_paths = _materialize_included_frames(sequence, output_dir)

    runtime_module = _require_runtime_module()
    return dict(runtime_module.stack_sequence_paths(
        source_paths,
        output_path,
        str(method),
        params,
        dict(metadata or {}),
        save_options,
    ))


def drizzle(
    sequence: SequenceHandle,
    output_path: str,
    *,
    method: str = "average",
    metadata=None,
    params=None,
    save_options=None,
    reference_index: int = 0,
    interpolation: str = "lanczos4",
    alignment_mode: str = "standard",
):
    """Drizzle-stack included frames through the native registered-image pipeline."""

    output_path = os.fspath(output_path)
    output_dir = os.path.dirname(output_path) or "."
    os.makedirs(output_dir, exist_ok=True)
    frames, source_paths = _materialize_included_frames(sequence, output_dir)
    reference_index = int(reference_index)
    if reference_index < 0 or reference_index >= len(frames):
        raise ValueError("reference_index is out of range")

    registrations = [
        _registration_for_frame(frame, index, reference_index)
        for index, frame in enumerate(frames)
    ]

    resolved_params = dict(params or {})
    raw_drizzle_params = resolved_params.get("drizzle")
    drizzle_params = (
        dict(raw_drizzle_params)
        if isinstance(raw_drizzle_params, dict)
        else {}
    )
    drizzle_params.setdefault("scale", 2.0)
    drizzle_params.setdefault("pixel_fraction", 0.9)
    resolved_params["drizzle"] = drizzle_params

    runtime_module = _require_runtime_module()
    return dict(runtime_module.drizzle_stack_sequence_paths(
        source_paths,
        registrations,
        output_path,
        str(method),
        resolved_params,
        dict(metadata or {}),
        save_options,
        reference_index,
        str(interpolation),
        str(alignment_mode),
    ))


def average(sequence: SequenceHandle, output_path: str, *, metadata=None, params=None, save_options=None):
    """Average-stack the included frames in a workflow sequence."""

    return stack(
        sequence,
        output_path,
        method="average",
        metadata=metadata,
        params=params,
        save_options=save_options,
    )


__all__ = [
    "average",
    "drizzle",
    "stack",
]
