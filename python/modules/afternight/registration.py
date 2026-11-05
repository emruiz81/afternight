# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Workflow registration helpers for the public AfterNight SDK."""

from __future__ import annotations

import json
import os

from . import _coerce_to_runtime_image, _require_runtime_module
from . import io
from .sequences import SequenceHandle, create


def _materialize_frame_path(frame, output_dir: str, output_index: int) -> str:
    if frame.path and os.path.exists(frame.path):
        return os.fspath(frame.path)

    if frame.image is None:
        raise FileNotFoundError("Workflow sequence frame has no image or path")

    input_dir = os.path.join(output_dir, "_registration_inputs")
    os.makedirs(input_dir, exist_ok=True)
    path = os.path.join(input_dir, f"input_{output_index:03d}.fit")
    io.save(frame.image, path)
    return path


def find_stars(image, *, channel: int = -1, max_stars: int = 512, params=None, **overrides):
    """Find stars in an image using AfterNightEngine's native PSF star profiler."""

    finder_params = dict(params or {})
    finder_params.update(overrides)
    runtime_module = _require_runtime_module()
    return list(
        runtime_module.find_stars(
            _coerce_to_runtime_image(image),
            int(channel),
            int(max_stars),
            finder_params,
        )
    )


def register(
    sequence: SequenceHandle,
    output_dir: str,
    *,
    manifest_name: str = "L.sequence.json",
    name: str = "registered_lights",
    reference_index: int = 0,
    max_stars: int = 512,
    min_stars: int = 6,
    star_params=None,
    transform_type: str = "similarity",
    polynomial_order: int = 0,
    auto_select_polynomial_order: bool = False,
    interpolation: str = "lanczos4",
):
    """Register and align a sequence through AfterNightEngine's native pipeline.

    Frames are star-detected with ``star_profiler_v2``, matched through the
    engine registration solver, warped into reference space, and returned as a
    new sequence with per-frame transform and quality metadata.
    """

    output_dir = os.fspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)
    frames = sequence.included_frames()
    if not frames:
        raise ValueError("Cannot register an empty workflow sequence")

    source_paths = [
        _materialize_frame_path(frame, output_dir, index)
        for index, frame in enumerate(frames, start=1)
    ]
    runtime_module = _require_runtime_module()
    native_result = runtime_module.register_sequence_paths(
        source_paths,
        output_dir,
        int(reference_index),
        int(max_stars),
        int(min_stars),
        star_params,
        str(transform_type),
        int(polynomial_order),
        bool(auto_select_polynomial_order),
        str(interpolation),
    )

    native_frames = {
        os.path.abspath(str(frame.get("input_path", ""))): dict(frame)
        for frame in list(native_result.get("frames", []))
    }
    registered = create(name)
    manifest = {
        "sequence": registered.name,
        "frame_count": 0,
        "reference_index": int(reference_index),
        "method": "native_star_alignment",
        "frames": [],
    }

    for output_index, (frame, source_path) in enumerate(zip(frames, source_paths, strict=True), start=1):
        native_frame = native_frames.get(os.path.abspath(source_path))
        if not native_frame:
            raise RuntimeError(f"Native registration did not return a result for {source_path}")
        if not bool(native_frame.get("succeeded", False)):
            detail = str(native_frame.get("error_message", "") or "registration failed")
            raise RuntimeError(f"Native registration failed for {source_path}: {detail}")

        destination_path = os.fspath(native_frame.get("output_path", ""))
        if not destination_path:
            raise RuntimeError(f"Native registration did not produce an aligned output for {source_path}")

        transform = dict(native_frame.get("registration", {}) or {})
        metadata = dict(frame.metadata)
        metadata["REGISTERED"] = True
        metadata["REGMETHOD"] = transform.get("method", "star_matching")
        registered.add_frame(
            path=destination_path,
            metadata=metadata,
            registration=transform,
            included=True,
        )
        manifest["frames"].append({
            "index": output_index - 1,
            "source_path": frame.path,
            "registered_path": destination_path,
            "metadata": metadata,
            "registration": transform,
            "native": native_frame,
        })

    manifest["frame_count"] = len(manifest["frames"])
    manifest["native"] = dict(native_result)
    manifest_path = os.path.join(output_dir, manifest_name)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)

    return {
        "sequence": registered,
        "manifest_path": manifest_path,
        "frame_count": manifest["frame_count"],
        "reference_index": manifest["reference_index"],
    }


def _identity_registration():
    return {
        "method": "reference",
        "transform_type": "similarity",
        "matrix": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
    }


def apply(
    sequence: SequenceHandle,
    output_dir: str,
    *,
    manifest_name: str = "applied_registration.sequence.json",
    name: str = "applied_registered_lights",
    reference_index: int = 0,
    interpolation: str = "lanczos4",
    alignment_mode: str = "standard",
    output_options=None,
):
    """Apply stored sequence registration transforms through AfterNightEngine.

    This is the SDK equivalent of Siril-style ``seqapplyreg``: it does not run
    star detection or solve new transforms, it warps each included frame using
    the registration metadata already attached to the sequence frames.
    """

    output_dir = os.fspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)
    frames = sequence.included_frames()
    if not frames:
        raise ValueError("Cannot apply registration to an empty workflow sequence")
    if reference_index < 0 or reference_index >= len(frames):
        raise ValueError("reference_index is out of range")

    source_paths = [
        _materialize_frame_path(frame, output_dir, index)
        for index, frame in enumerate(frames, start=1)
    ]
    registrations = []
    for index, frame in enumerate(frames):
        registration_data = dict(frame.registration or {})
        if not registration_data:
            if index != int(reference_index):
                raise ValueError(f"Sequence frame {index} has no registration transform")
            registration_data = _identity_registration()
        registrations.append(registration_data)

    runtime_module = _require_runtime_module()
    native_result = runtime_module.apply_registration_paths(
        source_paths,
        registrations,
        output_dir,
        int(reference_index),
        str(interpolation),
        str(alignment_mode),
        output_options,
    )

    native_frames = {
        os.path.abspath(str(frame.get("input_path", ""))): dict(frame)
        for frame in list(native_result.get("frames", []))
    }
    registered = create(name)
    manifest = {
        "sequence": registered.name,
        "frame_count": 0,
        "reference_index": int(reference_index),
        "method": "native_apply_registration",
        "frames": [],
    }

    for output_index, (frame, source_path) in enumerate(zip(frames, source_paths, strict=True), start=1):
        native_frame = native_frames.get(os.path.abspath(source_path))
        if not native_frame:
            raise RuntimeError(f"Native apply-registration did not return a result for {source_path}")
        if not bool(native_frame.get("succeeded", False)):
            detail = str(native_frame.get("error_message", "") or "apply registration failed")
            raise RuntimeError(f"Native apply-registration failed for {source_path}: {detail}")

        destination_path = os.fspath(native_frame.get("output_path", ""))
        if not destination_path:
            raise RuntimeError(f"Native apply-registration did not produce an aligned output for {source_path}")

        original_transform = dict(frame.registration or {})
        transform = dict(native_frame.get("registration", {}) or original_transform)
        transform["method"] = "applied_registration"
        if original_transform.get("method"):
            transform["source_method"] = original_transform["method"]

        metadata = dict(frame.metadata)
        metadata["REGISTERED"] = True
        metadata["REGMETHOD"] = "applied_registration"
        registered.add_frame(
            path=destination_path,
            metadata=metadata,
            registration=transform,
            included=True,
        )
        manifest["frames"].append({
            "index": output_index - 1,
            "source_path": frame.path,
            "registered_path": destination_path,
            "metadata": metadata,
            "registration": transform,
            "native": native_frame,
        })

    manifest["frame_count"] = len(manifest["frames"])
    manifest["native"] = dict(native_result)
    manifest_path = os.path.join(output_dir, manifest_name)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)

    return registered


__all__ = [
    "apply",
    "find_stars",
    "register",
]
