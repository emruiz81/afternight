# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The runtime SDK's contract as the runtime backend answers it.

Run inside the suite's embedded interpreter with `work` naming an empty directory. It calls every function and
`_RuntimeImage` method of the native `_afternight_runtime` module with its defaults over small NumPy inputs built
here, calls each one at least once in a way it refuses, runs the path-based workflows over a few generated frames, and
records the public signatures of the package modules that reach the engine and of the protocol-side modules.
`contract` is the result: a JSON-shaped dictionary the suite compares with the record checked in beside the other
expectations, or writes as that record on a capture run. The comparison is `contract_compare.py`'s, which the suite
runs first.

A result is recorded whole when it is small and as a summary - shape, representation, extremes, sums and eight
samples at fixed positions - when it is an image larger than 64 samples. A refusal is recorded as its exception type
and message, with the working directory written as `<work>`. The comparison is exact for everything but floating-point
numbers, which are held to `FLOAT_BOUND` absolutely and relatively, since the record is compared again on another host.
The recorded reference contract, `runtime-bindings.json`, is kept beside the current one, and every place the two
differ is named in `runtime-bindings-differences.json`.
"""

import inspect
import json
import math
import os

import numpy as np

import _afternight_runtime as native
from afternight import (calibration, core, io, lite, protocol, registration, runtime, sequences, stacking, ui,
                        ui_protocol)

FLOAT_BOUND = 1e-6
SMALL = 64


def _normalize_text(text):
    return text.replace(work, "<work>").replace(work.replace("\\", "/"), "<work>").replace("\\", "/")


def summary(array):
    """What a result array is recorded as."""
    array = np.asarray(array)
    record = {"shape": list(array.shape), "dtype": str(array.dtype)}
    flat = array.reshape(-1)
    if flat.size <= SMALL:
        record["values"] = [_number(value) for value in flat.tolist()]
        return record
    as_float = flat.astype(np.float64)
    finite = as_float[np.isfinite(as_float)]
    record["finite"] = int(finite.size)
    if finite.size:
        record["min"] = float(finite.min())
        record["max"] = float(finite.max())
        record["sum"] = float(finite.sum())
        record["sum_of_squares"] = float(np.square(finite).sum())
    positions = [int(round(index * (flat.size - 1) / 7.0)) for index in range(8)]
    record["samples"] = [_number(flat[position].item()) for position in positions]
    return record


def _number(value):
    if isinstance(value, float) and math.isnan(value):
        return "nan"
    if isinstance(value, float) and math.isinf(value):
        return "inf" if value > 0 else "-inf"
    return value


def describe(value):
    """A JSON-shaped description of any result the bindings return."""
    if isinstance(value, native._RuntimeImage):
        return {"image": summary(value.to_numpy()), "properties": describe(value.properties())}
    if isinstance(value, np.ndarray):
        return summary(value)
    if isinstance(value, dict):
        return {str(key): describe(value[key]) for key in sorted(value, key=str)}
    if isinstance(value, (list, tuple)):
        return [describe(item) for item in value]
    if isinstance(value, str):
        return _normalize_text(value)
    if isinstance(value, float):
        return _number(value)
    if isinstance(value, (bool, int)) or value is None:
        return value
    if isinstance(value, (np.floating, np.integer, np.bool_)):
        return describe(value.item())
    return repr(type(value).__name__)


def call(name, function, *args, **kwargs):
    """Record one call: its result, or the exception it raised."""
    try:
        return {"call": name, "result": describe(function(*args, **kwargs))}
    except Exception as error:  # noqa: BLE001 - every refusal is part of the contract
        return {"call": name, "raises": type(error).__name__, "message": _normalize_text(str(error))}


# ---- Inputs.

def gradient(height, width, channels=1, dtype=np.float32, low=0.0, high=1.0):
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    base = low + (high - low) * (x + 2.0 * y) / float((width - 1) + 2 * (height - 1))
    if channels == 1:
        values = base
    else:
        values = np.stack([base, (base * 0.5 + 0.25), 1.0 - base], axis=-1)
    if np.issubdtype(np.dtype(dtype), np.integer):
        values = np.round(values * np.iinfo(dtype).max)
    return values.astype(dtype)


def star_field(height=48, width=48, shift=(0.0, 0.0), channels=1):
    y, x = np.mgrid[0:height, 0:width].astype(np.float64)
    field = np.full((height, width), 0.08) + 0.0004 * x + 0.0003 * y
    rng = np.random.default_rng(11)
    field = field + rng.normal(0.0, 0.002, size=field.shape)
    stars = [(12.3, 10.6, 0.8, 3.0), (33.5, 14.2, 0.6, 2.6), (22.1, 30.4, 0.9, 3.4), (38.2, 36.7, 0.5, 2.8),
             (9.4, 38.1, 0.7, 3.1), (27.8, 21.9, 0.4, 2.4), (41.3, 7.2, 0.55, 2.7)]
    for cx, cy, amplitude, fwhm in stars:
        sigma = fwhm / 2.35482004503
        field += amplitude * np.exp(-(((x - cx - shift[0]) ** 2) + ((y - cy - shift[1]) ** 2)) / (2.0 * sigma * sigma))
    field = np.clip(field, 0.0, 1.0).astype(np.float32)
    if channels == 3:
        return np.stack([field, field * 0.9, field * 0.8], axis=-1).astype(np.float32)
    return field


# ---- The contract.

contract = {"format": 1, "calls": []}
calls = contract["calls"]

calls.append(call("available_pixel_types", native.available_pixel_types))

for dtype in (np.int8, np.uint8, np.int16, np.uint16, np.int32, np.uint32, np.float32, np.float64):
    mono = gradient(3, 4, dtype=dtype, high=0.8) if np.issubdtype(np.dtype(dtype), np.floating) else \
        np.arange(12, dtype=dtype).reshape(3, 4)
    calls.append(call("image_from_numpy mono " + np.dtype(dtype).name, native.image_from_numpy, mono))
calls.append(call("image_from_numpy colour hwc", native.image_from_numpy, gradient(3, 4, channels=3)))
calls.append(call("image_from_numpy colour chw", native.image_from_numpy,
                  np.ascontiguousarray(np.moveaxis(gradient(3, 4, channels=3), -1, 0))))
calls.append(call("image_from_numpy metadata", lambda: native.image_from_numpy(
    gradient(2, 2), {"OBJECT": "M31", "EXPTIME": 30, "SIMPLE": "F"}).metadata()))
calls.append(call("image_from_numpy refuses a complex array", native.image_from_numpy, np.zeros((2, 2), np.complex64)))
calls.append(call("image_from_numpy refuses four dimensions", native.image_from_numpy, np.zeros((1, 2, 2, 3), np.float32)))
calls.append(call("image_from_numpy refuses five channels", native.image_from_numpy, np.zeros((2, 2, 5), np.float32)))

image = native.image_from_numpy(gradient(4, 5, channels=3))
calls.append(call("_RuntimeImage.to_numpy", lambda: {
    "array": describe(image.to_numpy()), "c_contiguous": bool(image.to_numpy().flags["C_CONTIGUOUS"]),
    "writeable": bool(image.to_numpy().flags["WRITEABLE"])}))
calls.append(call("_RuntimeImage.properties", image.properties))
calls.append(call("_RuntimeImage.metadata", image.metadata))
calls.append(call("_RuntimeImage.set_metadata", lambda: (image.set_metadata("OBSERVER", "someone"),
                                                         image.set_metadata("NOTE", None), image.metadata())[-1]))
calls.append(call("_RuntimeImage.set_metadata refuses a reserved key", image.set_metadata, "BITPIX", "8"))
calls.append(call("_RuntimeImage.set_metadata refuses an empty key", image.set_metadata, "", "x"))
calls.append(call("_RuntimeImage.clone", lambda: (lambda copy: {
    "equal": bool(np.array_equal(copy.to_numpy(), image.to_numpy())), "copy": describe(copy)})(image.clone())))
calls.append(call("_RuntimeImage.from_numpy", lambda: (lambda target: (target.from_numpy(gradient(2, 3, dtype=np.uint16)),
                                                                       target)[-1])(native.image_from_numpy(gradient(4, 5)))))

source = native.image_from_numpy(gradient(3, 4, low=-0.25, high=1.25))
for pixel_type in native.available_pixel_types():
    calls.append(call("convert_pixel_type " + pixel_type, native.convert_pixel_type, source, pixel_type))
calls.append(call("convert_pixel_type refuses an unknown type", native.convert_pixel_type, source, "int64"))

destination = native.image_from_numpy(np.zeros((3, 4), np.uint16))
calls.append(call("apply_linear_transform defaults", lambda: (native.apply_linear_transform(source, destination),
                                                              destination)[-1]))
calls.append(call("apply_linear_transform scale 2 offset 0.1", lambda: (native.apply_linear_transform(
    source, destination, 2.0, 0.1), destination)[-1]))
calls.append(call("apply_linear_transform refuses another extent", native.apply_linear_transform, source,
                  native.image_from_numpy(np.zeros((2, 2), np.float32))))

colour = native.image_from_numpy(gradient(3, 4, channels=3))
calls.append(call("extract_channel 1", native.extract_channel, colour, 1))
calls.append(call("extract_channel refuses channel 3", native.extract_channel, colour, 3))
calls.append(call("extract_channels", native.extract_channels, colour))
planes = native.extract_channels(colour)
calls.append(call("compose_rgb", native.compose_rgb, planes[0], planes[1], planes[2]))
calls.append(call("compose_rgb refuses a colour plane", native.compose_rgb, colour, planes[1], planes[2]))
calls.append(call("compose_rgb refuses another extent", native.compose_rgb, planes[0], planes[1],
                  native.image_from_numpy(np.zeros((2, 2), np.float32))))

for suffix in (".fits", ".fit", ".fts", ".xisf", ".tif", ".tiff", ".png", ".jpg", ".ser", ".avi", ".txt", ""):
    calls.append(call("is_supported_path " + (suffix or "no suffix"), native.is_supported_path, "frame" + suffix))

saved = native.image_from_numpy(gradient(4, 5), {"OBJECT": "M42", "EXPTIME": 12.5})
for suffix in (".fits", ".tif", ".png"):
    path = os.path.join(work, "saved" + suffix)
    calls.append(call("save_image " + suffix, lambda path=path: (native.save_image(saved, path), os.path.exists(path))[-1]))
    calls.append(call("load_image " + suffix, native.load_image, path))
    calls.append(call("load_image without metadata " + suffix, lambda path=path: native.load_image(
        path, load_metadata=False).metadata()))
calls.append(call("save_image with options", lambda: (native.save_image(
    saved, os.path.join(work, "options.tif"), {"tiff_compression": "zip", "unknown_option": 1}),
    os.path.exists(os.path.join(work, "options.tif")))[-1]))
# cfitsio wraps the path it could not open across its message, so the message is recorded up to the path.
missing = call("load_image refuses a missing file", native.load_image, os.path.join(work, "missing.fits"))
missing["message"] = missing.get("message", "").split(" Error message stack:")[0]
calls.append(missing)
calls.append(call("save_image refuses an unknown format", native.save_image, saved, os.path.join(work, "saved.unknown")))

stars = native.image_from_numpy(star_field())
calls.append(call("detect_fwhm defaults", native.detect_fwhm, stars))
calls.append(call("find_stars defaults", native.find_stars, stars))
calls.append(call("find_stars max_stars 3", native.find_stars, stars, max_stars=3))
calls.append(call("find_stars refuses no stars", native.find_stars, stars, max_stars=0))
calls.append(call("find_stars refuses channel 2 of a mono image", native.find_stars, stars, channel=2))
calls.append(call("find_stars refuses a zero radius", native.find_stars, stars, params={"radius": 0}))

small = native.image_from_numpy(star_field(24, 24))
calls.append(call("richardson_lucy_deconvolution defaults", native.richardson_lucy_deconvolution, small, 2.0))
calls.append(call("richardson_lucy_deconvolution refuses the automatic profile",
                  native.richardson_lucy_deconvolution, small, 2.0, profile="auto"))
calls.append(call("richardson_lucy_deconvolution refuses no iterations", native.richardson_lucy_deconvolution,
                  small, 2.0, iterations=0))
calls.append(call("wiener_deconvolution defaults", native.wiener_deconvolution, small, 2.0))
calls.append(call("wiener_deconvolution refuses no regularization", native.wiener_deconvolution, small, 2.0,
                  regularization=0.0))
calls.append(call("richardson_lucy_star_deconvolution defaults", native.richardson_lucy_star_deconvolution, stars))
calls.append(call("richardson_lucy_star_deconvolution refuses a field without stars",
                  native.richardson_lucy_star_deconvolution, native.image_from_numpy(np.full((24, 24), 0.1, np.float32))))


# ---- The path workflows over generated frames.

def frame(name, array, metadata):
    path = os.path.join(work, name)
    native.save_image(native.image_from_numpy(array, metadata), path)
    return path


bias = [frame("bias_%d.fits" % index, np.full((48, 48), 0.02 + 0.001 * index, np.float32), {"IMAGETYP": "BIAS"})
        for index in range(3)]
darks = [frame("dark_%d.fits" % index, np.full((48, 48), 0.03 + 0.001 * index, np.float32),
               {"IMAGETYP": "DARK", "EXPTIME": 60}) for index in range(3)]
flats = [frame("flat_%d.fits" % index, np.full((48, 48), 0.5 + 0.01 * index, np.float32),
               {"IMAGETYP": "FLAT", "FILTER": "L"}) for index in range(3)]
lights = [frame("light_%d.fits" % index, star_field(shift=(1.5 * index, -1.0 * index)),
                {"IMAGETYP": "LIGHT", "FILTER": "L", "EXPTIME": 60}) for index in range(3)]

masters_directory = os.path.join(work, "masters")
os.makedirs(masters_directory, exist_ok=True)
calls.append(call("create_calibration_masters_paths defaults", native.create_calibration_masters_paths,
                  {"bias": bias, "dark": darks, "flat": flats}, masters_directory))
calls.append(call("create_calibration_masters_paths refuses no output directory",
                  native.create_calibration_masters_paths, {"bias": bias}, ""))
masters = {role: os.path.join(masters_directory, "master_%s_L.xisf" % role) for role in ("bias", "dark", "flat")}
calibrated_directory = os.path.join(work, "calibrated")
os.makedirs(calibrated_directory, exist_ok=True)
calls.append(call("calibrate_sequence_paths defaults", native.calibrate_sequence_paths, lights, masters,
                  calibrated_directory))
registered_directory = os.path.join(work, "registered")
os.makedirs(registered_directory, exist_ok=True)
registered = call("register_sequence_paths defaults", native.register_sequence_paths, lights, registered_directory)
calls.append(registered)
calls.append(call("register_sequence_paths refuses no frames", native.register_sequence_paths, [],
                  registered_directory))
aligned_directory = os.path.join(work, "aligned")
os.makedirs(aligned_directory, exist_ok=True)
registrations = [None, {"dx": -1.5, "dy": 1.0}, {"dx": -3.0, "dy": 2.0}]
calls.append(call("apply_registration_paths defaults", native.apply_registration_paths, lights, registrations,
                  aligned_directory))
calls.append(call("apply_registration_paths refuses mismatched lists", native.apply_registration_paths, lights,
                  registrations[:2], aligned_directory))
calls.append(call("stack_sequence_paths defaults", native.stack_sequence_paths, lights,
                  os.path.join(work, "stacked.fits")))
calls.append(call("stack_sequence_paths refuses no frames", native.stack_sequence_paths, [],
                  os.path.join(work, "empty.fits")))
calls.append(call("stack result", native.load_image, os.path.join(work, "stacked.fits")))
calls.append(call("drizzle_stack_sequence_paths defaults", native.drizzle_stack_sequence_paths, lights, registrations,
                  os.path.join(work, "drizzled.fits")))

# ---- What the package declares: the modules that reach the engine, the sequence helpers, the extension base classes,
# and the protocol-side modules every host serves - the session, settings, view and image facades, the Lite base class
# and the result helpers - each public function, each public method a class defines and each public property, read
# through its getter.

signatures = {}
for module in (calibration, core, io, registration, stacking, sequences, runtime, lite, protocol, ui, ui_protocol):
    for name, function in inspect.getmembers(module, inspect.isfunction):
        if not name.startswith("_") and function.__module__ == module.__name__:
            signatures[module.__name__ + "." + name] = str(inspect.signature(function))
    for class_name, cls in inspect.getmembers(module, inspect.isclass):
        if class_name.startswith("_") or cls.__module__ != module.__name__:
            continue
        for name, function in vars(cls).items():
            if name.startswith("_"):
                continue
            if isinstance(function, property):
                function = function.fget
            if inspect.isfunction(function):
                signatures[module.__name__ + "." + class_name + "." + name] = str(inspect.signature(function))
contract["package_signatures"] = signatures
contract["native_signatures"] = {name: (getattr(native, name).__doc__ or "").split("\n")[0]
                                 for name in sorted(dir(native)) if not name.startswith("_")}


contract_text = json.dumps(contract, indent=1, sort_keys=True)
