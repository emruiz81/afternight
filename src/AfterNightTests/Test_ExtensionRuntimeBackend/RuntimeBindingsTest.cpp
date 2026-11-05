// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime SDK's calls as extensions make them: through the `afternight` package over the embedded module, each
/// case the behaviour the SDK documents for its domain - core image operations, the deconvolutions, star
/// finding, calibration, registration, the stored-transform apply, stacking with its quality outputs, drizzle - and the
/// package's own UI and progress facades. The domain matrix in docs/dev/PLUGIN_SDK_SUPPORTED_DOMAIN_MATRIX.md names
/// these cases as each domain's baseline.

#include "RuntimeBackendTestSupport.h"

#include <pybind11/eval.h>

#include <gtest/gtest.h>

#include <string>

namespace py = pybind11;

namespace
{

/// The shared runtime, or the case fails with the reason it is missing.
#define ASSERT_SHARED_RUNTIME()                                                                      \
    do                                                                                               \
    {                                                                                                \
        const std::string sharedRuntimeFailure = an::tests::runtime_backend::requireSharedRuntime(); \
        ASSERT_TRUE(sharedRuntimeFailure.empty()) << sharedRuntimeFailure;                           \
    } while (false)

} // namespace

TEST(ExtensionRuntimeBackendModuleTest, LoadingModuleBeforeInterpreterRegistersRuntimePythonBindings)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    const py::tuple version_info = py::module_::import("sys").attr("version_info").cast<py::tuple>();
    ASSERT_GE(version_info.size(), 3);
    EXPECT_EQ(version_info[0].cast<int>(), PY_MAJOR_VERSION);
    EXPECT_EQ(version_info[1].cast<int>(), PY_MINOR_VERSION);
    EXPECT_EQ(version_info[2].cast<int>(), PY_MICRO_VERSION);
    py::module_ runtime = py::module_::import("_afternight_runtime");
    py::module_ calibration = py::module_::import("afternight.calibration");
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ afternight = py::module_::import("afternight");
    py::module_ registration = py::module_::import("afternight.registration");
    py::module_ stacking = py::module_::import("afternight.stacking");
    py::module_ ui = py::module_::import("afternight.ui");
    py::list pixel_types = core.attr("available_pixel_types")().cast<py::list>();
    EXPECT_GT(py::len(pixel_types), 0);
    EXPECT_TRUE(py::cast<bool>(pixel_types.attr("__contains__")(py::str("float32"))));
    EXPECT_TRUE(py::cast<bool>(io.attr("is_supported_path")(std::string("runtime_roundtrip.fit"))));
    EXPECT_TRUE(py::hasattr(runtime, "extract_channels"));
    EXPECT_EQ(afternight.attr("calibration").ptr(), calibration.ptr());
    EXPECT_EQ(afternight.attr("registration").ptr(), registration.ptr());
    EXPECT_EQ(afternight.attr("stacking").ptr(), stacking.ptr());
    EXPECT_EQ(PyCallable_Check(calibration.attr("create_masters").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(calibration.attr("calibrate_lights").ptr()), 1);
    EXPECT_TRUE(py::hasattr(runtime, "create_calibration_masters_paths"));
    EXPECT_TRUE(py::hasattr(runtime, "calibrate_sequence_paths"));
    EXPECT_EQ(PyCallable_Check(core.attr("richardson_lucy_deconvolution").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(core.attr("richardson_lucy_star_deconvolution").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(registration.attr("find_stars").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(registration.attr("register").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(registration.attr("apply").ptr()), 1);
    EXPECT_TRUE(py::hasattr(runtime, "register_sequence_paths"));
    EXPECT_TRUE(py::hasattr(runtime, "apply_registration_paths"));
    EXPECT_TRUE(py::hasattr(runtime, "stack_sequence_paths"));
    EXPECT_TRUE(py::hasattr(runtime, "drizzle_stack_sequence_paths"));
    EXPECT_EQ(PyCallable_Check(stacking.attr("average").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(stacking.attr("drizzle").ptr()), 1);

    py::object settings = afternight.attr("Settings")();
    py::object context = afternight.attr("ExtensionContext")(settings);
    py::object session_info = afternight.attr("current_session")();
    py::object paths = afternight.attr("session_paths")();

    ASSERT_TRUE(py::hasattr(context, "settings"));
    EXPECT_EQ(py::str(py::type::of(context.attr("settings")).attr("__name__")).cast<std::string>(), "Settings");
    ASSERT_TRUE(py::hasattr(session_info, "session_id"));
    ASSERT_TRUE(py::hasattr(session_info, "extension_id"));
    ASSERT_TRUE(py::hasattr(paths, "temp_dir"));
    ASSERT_TRUE(py::hasattr(paths, "artifacts_dir"));
    EXPECT_TRUE(paths.attr("temp_dir")().cast<std::string>().empty());
    EXPECT_TRUE(paths.attr("artifacts_dir")().cast<std::string>().empty());

    EXPECT_EQ(PyObject_IsSubclass(ui.attr("ProcessWindow").ptr(), afternight.attr("ProcessExtension").ptr()), 1);
    EXPECT_EQ(PyObject_IsSubclass(ui.attr("RTPreviewProcess").ptr(), afternight.attr("RTPreviewExtension").ptr()), 1);
    EXPECT_TRUE(py::isinstance<py::dict>(ui.attr("tokens")()));
    EXPECT_EQ(PyCallable_Check(ui.attr("show_error").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("show_warning").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("show_info").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("confirm").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("hide_process_window").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("close_process_window").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("process_window_meta").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("commit_image").ptr()), 1);
    EXPECT_EQ(PyCallable_Check(ui.attr("open_image").ptr()), 1);
    EXPECT_TRUE(ui.attr("ImageView").is(afternight.attr("ViewRef")));
}

TEST(ExtensionRuntimeBackendModuleTest, CoreDetectFwhmUsesEngineStarProfiler)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");

    py::dict locals;
    locals["core"] = core;
    py::exec(R"PY(
import numpy as np

height = 256
width = 256
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
fwhm = 4.0
sigma = fwhm / 2.35482004503
background = np.full((height, width), 0.1, dtype=np.float32)
star = 0.85 * np.exp(-(((x - 128.0) ** 2) + ((y - 128.0) ** 2)) / (2.0 * sigma * sigma))
image = np.clip(background + star, 0.0, 1.0)
runtime_image = core.from_numpy(image)
detected_fwhm = core.detect_fwhm(runtime_image)
)PY",
             py::globals(), locals);

    const double detected_fwhm = locals["detected_fwhm"].cast<double>();
    EXPECT_GT(detected_fwhm, 0.0);
    EXPECT_NEAR(detected_fwhm, 4.0, 1.5);
}

TEST(ExtensionRuntimeBackendModuleTest, CoreRichardsonLucyDeconvolutionSupportsMoffatPsf)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");

    py::dict locals;
    locals["core"] = core;
    py::exec(R"PY(
import numpy as np

height = 65
width = 65
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
fwhm = 4.0
sigma = fwhm / 2.35482004503
image = 0.75 * np.exp(-(((x - 32.0) ** 2) + ((y - 32.0) ** 2)) / (2.0 * sigma * sigma))
runtime_image = core.from_numpy(image.astype(np.float32))
restored = core.richardson_lucy_deconvolution(
    runtime_image,
    fwhm=fwhm,
    profile="moffat",
    beta=3.4,
    iterations=3,
    tv_regularization=0.4,
    damping=0.2,
    regularization_mode="frobenius_hessian",
    update_method="gradient_descent",
    gradient_descent_step=0.04,
    relative_stopping_delta=0.5,
    edge_taper_radius=4.0,
    convolution_backend="fft",
    psf_size=17,
)
restored_no_taper = core.richardson_lucy_deconvolution(
    runtime_image,
    fwhm=fwhm,
    profile="moffat",
    beta=3.4,
    iterations=2,
    edge_taper_mode="off",
    edge_taper_iterations=0,
    psf_size=17,
)
restored_array = restored.to_numpy()
restored_no_taper_array = restored_no_taper.to_numpy()
restored_shape = restored_array.shape
restored_dtype = str(restored_array.dtype)
restored_finite = bool(np.isfinite(restored_array).all())
restored_no_taper_finite = bool(np.isfinite(restored_no_taper_array).all())
restored_sum = float(restored_array.sum())
)PY",
             locals, locals);

    const auto shape = locals["restored_shape"].cast<py::tuple>();
    ASSERT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0].cast<int>(), 65);
    EXPECT_EQ(shape[1].cast<int>(), 65);
    EXPECT_EQ(locals["restored_dtype"].cast<std::string>(), "float32");
    EXPECT_TRUE(locals["restored_finite"].cast<bool>());
    EXPECT_TRUE(locals["restored_no_taper_finite"].cast<bool>());
    EXPECT_GT(locals["restored_sum"].cast<double>(), 0.0);
}

TEST(ExtensionRuntimeBackendModuleTest, CoreRichardsonLucyDeconvolutionSupportsLuminanceColorMode)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");

    py::dict locals;
    locals["core"] = core;
    py::exec(R"PY(
import numpy as np

height = 37
width = 39
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
sigma = 4.5 / 2.35482004503
core_signal = np.exp(-(((x - 19.0) ** 2) + ((y - 18.0) ** 2)) / (2.0 * sigma * sigma))
image = np.empty((height, width, 3), dtype=np.float32)
image[..., 0] = 0.03 + 0.32 * core_signal
image[..., 1] = 0.04 + 0.22 * core_signal
image[..., 2] = 0.05 + 0.12 * core_signal
runtime_image = core.from_numpy(image)
restored = core.richardson_lucy_deconvolution(
    runtime_image,
    fwhm=4.5,
    iterations=3,
    psf_size=15,
    edge_taper_mode="manual",
    edge_taper_radius=4.0,
    edge_taper_iterations=2,
    color_mode="luminance",
    convolution_backend="direct",
    clamp_to_unit_range=False,
)
restored_array = restored.to_numpy()
weights = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
input_luma = np.tensordot(image, weights, axes=([-1], [0]))
restored_luma = np.tensordot(restored_array, weights, axes=([-1], [0]))
input_chroma = image / input_luma[..., None]
restored_chroma = restored_array / restored_luma[..., None]
restored_shape = restored_array.shape
restored_dtype = str(restored_array.dtype)
restored_finite = bool(np.isfinite(restored_array).all())
max_chroma_drift = float(np.max(np.abs(restored_chroma - input_chroma)))
)PY",
             py::globals(), locals);

    const auto shape = locals["restored_shape"].cast<py::tuple>();
    ASSERT_EQ(shape.size(), 3);
    EXPECT_EQ(shape[0].cast<int>(), 37);
    EXPECT_EQ(shape[1].cast<int>(), 39);
    EXPECT_EQ(shape[2].cast<int>(), 3);
    EXPECT_EQ(locals["restored_dtype"].cast<std::string>(), "float32");
    EXPECT_TRUE(locals["restored_finite"].cast<bool>());
    EXPECT_LT(locals["max_chroma_drift"].cast<double>(), 2.0e-5);
}

TEST(ExtensionRuntimeBackendModuleTest, CoreWienerDeconvolutionSupportsMoffatPsf)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");

    py::dict locals;
    locals["core"] = core;
    py::exec(R"PY(
import numpy as np

height = 65
width = 65
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
fwhm = 4.0
sigma = fwhm / 2.35482004503
image = 0.65 * np.exp(-(((x - 32.0) ** 2) + ((y - 32.0) ** 2)) / (2.0 * sigma * sigma))
runtime_image = core.from_numpy(image.astype(np.float32))
restored = core.wiener_deconvolution(
    runtime_image,
    fwhm=fwhm,
    profile="moffat",
    beta=3.4,
    regularization=0.025,
    edge_taper_mode="manual",
    edge_taper_radius=4.0,
    edge_taper_iterations=2,
    psf_size=17,
)
restored_array = restored.to_numpy()
restored_shape = restored_array.shape
restored_dtype = str(restored_array.dtype)
restored_finite = bool(np.isfinite(restored_array).all())
restored_sum = float(restored_array.sum())
)PY",
             py::globals(), locals);

    const auto shape = locals["restored_shape"].cast<py::tuple>();
    ASSERT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0].cast<int>(), 65);
    EXPECT_EQ(shape[1].cast<int>(), 65);
    EXPECT_EQ(locals["restored_dtype"].cast<std::string>(), "float32");
    EXPECT_TRUE(locals["restored_finite"].cast<bool>());
    EXPECT_GT(locals["restored_sum"].cast<double>(), 0.0);
}

TEST(ExtensionRuntimeBackendModuleTest, CoreRichardsonLucyStarDeconvolutionAutoBuildsPsfFromFittedStars)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");

    py::dict locals;
    locals["core"] = core;
    py::exec(R"PY(
import numpy as np

height = 128
width = 128
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
image = np.full((height, width), 0.03, dtype=np.float32)
targets = [(32.0, 36.0, 0.72, 4.0), (87.0, 54.0, 0.68, 4.4), (62.0, 96.0, 0.64, 4.8)]
for cx, cy, amplitude, fwhm in targets:
    sigma = fwhm / 2.35482004503
    image += amplitude * np.exp(-(((x - cx) ** 2) + ((y - cy) ** 2)) / (2.0 * sigma * sigma))

runtime_image = core.from_numpy(np.clip(image, 0.0, 1.0).astype(np.float32))
restored = core.richardson_lucy_star_deconvolution(
    runtime_image,
    channel=0,
    max_stars=16,
    star_params={"profile": "gaussian"},
    profile="auto",
    minimum_star_count=2,
    iterations=3,
    relative_stopping_delta=0.5,
    edge_taper_radius=4.0,
    convolution_backend="fft",
    psf_size=17,
)
restored_array = restored.to_numpy()
restored_shape = restored_array.shape
restored_dtype = str(restored_array.dtype)
restored_finite = bool(np.isfinite(restored_array).all())
restored_sum = float(restored_array.sum())
)PY",
             py::globals(), locals);

    const auto shape = locals["restored_shape"].cast<py::tuple>();
    ASSERT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0].cast<int>(), 128);
    EXPECT_EQ(shape[1].cast<int>(), 128);
    EXPECT_EQ(locals["restored_dtype"].cast<std::string>(), "float32");
    EXPECT_TRUE(locals["restored_finite"].cast<bool>());
    EXPECT_GT(locals["restored_sum"].cast<double>(), 0.0);
}

TEST(ExtensionRuntimeBackendModuleTest, RegistrationFindStarsUsesEngineStarProfiler)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");
    py::module_ registration = py::module_::import("afternight.registration");

    py::dict locals;
    locals["core"] = core;
    locals["registration"] = registration;
    py::exec(R"PY(
import numpy as np

height = 256
width = 256
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
image = np.full((height, width), 0.03, dtype=np.float32)
targets = [(64.0, 72.0, 0.88, 3.8), (173.0, 121.0, 0.82, 4.4), (118.0, 190.0, 0.76, 5.0)]
for cx, cy, amplitude, fwhm in targets:
    sigma = fwhm / 2.35482004503
    image += amplitude * np.exp(-(((x - cx) ** 2) + ((y - cy) ** 2)) / (2.0 * sigma * sigma))

runtime_image = core.from_numpy(np.clip(image, 0.0, 1.0).astype(np.float32))
stars = registration.find_stars(runtime_image, max_stars=20)

matched_targets = 0
for tx, ty, _, _ in targets:
    matched = False
    for star in stars:
        if (
            abs(star["x"] - tx) <= 2.5
            and abs(star["y"] - ty) <= 2.5
            and star["fwhm"] > 0.0
        ):
            matched = True
    if matched:
        matched_targets += 1
)PY",
             py::globals(), locals);

    const auto stars = locals["stars"].cast<py::list>();
    EXPECT_GE(py::len(stars), 3);
    EXPECT_GE(locals["matched_targets"].cast<int>(), 2);
}

TEST(ExtensionRuntimeBackendModuleTest, RegistrationRegisterSequenceUsesNativeStarAlignment)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ registration = py::module_::import("afternight.registration");
    py::module_ sequences = py::module_::import("afternight.sequences");

    py::dict locals;
    locals["core"] = core;
    locals["io"] = io;
    locals["registration"] = registration;
    locals["sequences"] = sequences;
    py::exec(R"PY(
import json
import os
import tempfile

import numpy as np

height = 192
width = 192
y, x = np.mgrid[0:height, 0:width].astype(np.float32)
stars = [
    (30.0, 34.0, 0.55, 3.2), (78.0, 24.0, 0.45, 3.6),
    (142.0, 31.0, 0.50, 3.4), (48.0, 82.0, 0.62, 3.8),
    (112.0, 72.0, 0.48, 3.5), (160.0, 95.0, 0.42, 3.9),
    (32.0, 142.0, 0.44, 3.3), (88.0, 130.0, 0.58, 3.7),
    (136.0, 150.0, 0.52, 3.4), (168.0, 164.0, 0.46, 3.6),
]

def make_frame(dx=0.0, dy=0.0):
    image = np.full((height, width), 0.025, dtype=np.float32)
    image += (x / float(width - 1)) * 0.015
    image += (y / float(height - 1)) * 0.012
    for cx, cy, amplitude, fwhm in stars:
        sigma = fwhm / 2.35482004503
        image += amplitude * np.exp(-(((x - (cx + dx)) ** 2) + ((y - (cy + dy)) ** 2)) / (2.0 * sigma * sigma))
    return np.clip(image, 0.0, 1.0).astype(np.float32)

tmp = tempfile.TemporaryDirectory()
root = tmp.name
ref_path = os.path.join(root, "light_001.fit")
shifted_path = os.path.join(root, "light_002.fit")
io.save(core.from_numpy(make_frame(), {"IMAGETYP": "LIGHT", "FILTER": "L"}), ref_path)
io.save(core.from_numpy(make_frame(4.0, -3.0), {"IMAGETYP": "LIGHT", "FILTER": "L"}), shifted_path)

sequence = sequences.create("native_registration_test")
sequence.add_frame(path=ref_path, metadata={"IMAGETYP": "LIGHT", "FILTER": "L"})
sequence.add_frame(path=shifted_path, metadata={"IMAGETYP": "LIGHT", "FILTER": "L"})

result = registration.register(
    sequence,
    os.path.join(root, "registered"),
    star_params={"radius": 8, "sigma": 1.5, "profile": "gaussian"},
    min_stars=6,
    max_stars=64,
    transform_type="similarity",
    interpolation="bilinear",
)
registered_sequence = result["sequence"]
registered_frames = registered_sequence.frames()
manifest = json.load(open(result["manifest_path"], "r", encoding="utf-8"))
second_registration = registered_frames[1].registration

output_paths_exist = all(os.path.exists(frame.path) for frame in registered_frames)
native_methods = [frame.registration.get("method") for frame in registered_frames]
translated_x = float(second_registration.get("dx", 0.0))
translated_y = float(second_registration.get("dy", 0.0))
matched_stars = int(second_registration.get("matched_stars", 0))
rms_pixels = float(second_registration.get("rms_pixels", 999.0))
)PY",
             locals, locals);

    EXPECT_TRUE(locals["output_paths_exist"].cast<bool>());
    EXPECT_EQ(locals["native_methods"].cast<py::list>()[0].cast<std::string>(), "reference");
    EXPECT_EQ(locals["native_methods"].cast<py::list>()[1].cast<std::string>(), "star_matching");
    EXPECT_EQ(locals["result"].cast<py::dict>()["frame_count"].cast<int>(), 2);
    EXPECT_EQ(locals["manifest"].cast<py::dict>()["frame_count"].cast<int>(), 2);
    EXPECT_GE(locals["matched_stars"].cast<int>(), 6);
    EXPECT_LT(locals["rms_pixels"].cast<double>(), 1.0);
    EXPECT_NEAR(locals["translated_x"].cast<double>(), -4.0, 1.5);
    EXPECT_NEAR(locals["translated_y"].cast<double>(), 3.0, 1.5);
}

TEST(ExtensionRuntimeBackendModuleTest, CalibrationCalibrateLightsUsesNativeBatchPipeline)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ calibration = py::module_::import("afternight.calibration");
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ sequences = py::module_::import("afternight.sequences");

    py::dict locals;
    locals["calibration"] = calibration;
    locals["core"] = core;
    locals["io"] = io;
    locals["sequences"] = sequences;
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
sequence = sequences.create("native_calibration_test")

def add_frame(role, index, value):
    path = os.path.join(root, f"{role}_{index:03d}.fit")
    metadata = {"IMAGETYP": role.upper(), "FILTER": "L"}
    io.save(core.from_numpy(np.full((32, 32), value, dtype=np.float32), metadata), path)
    sequence.add_frame(path=path, metadata=metadata)
    return path

add_frame("bias", 1, 0.01)
add_frame("bias", 2, 0.03)
add_frame("dark", 1, 0.04)
add_frame("dark", 2, 0.06)
add_frame("flat", 1, 1.0)
add_frame("flat", 2, 1.0)
light_path = add_frame("light", 1, 0.42)

masters = calibration.create_masters(sequence, os.path.join(root, "masters"), filter_name="L")
calibrated = calibration.calibrate_lights(sequence, masters, os.path.join(root, "calibrated"), filter_name="L")
frame = calibrated.frames()[0]
pixels = np.asarray(io.load(frame.path).to_numpy(), dtype=np.float32)

master_paths_exist = all(os.path.exists(masters[role]["path"]) for role in ("bias", "dark", "flat"))
masters_are_native = all(bool(masters[role].get("native")) for role in ("bias", "dark", "flat"))
calibrated_exists = os.path.exists(frame.path)
calibrated_pixel = float(pixels[0, 0])
calibration_method = frame.metadata.get("CALMETHOD", "")
)PY",
             locals, locals);

    EXPECT_TRUE(locals["master_paths_exist"].cast<bool>());
    EXPECT_TRUE(locals["masters_are_native"].cast<bool>());
    EXPECT_TRUE(locals["calibrated_exists"].cast<bool>());
    EXPECT_NEAR(locals["calibrated_pixel"].cast<double>(), 0.35, 0.02);
    EXPECT_EQ(locals["calibration_method"].cast<std::string>(), "native_engine");
}

TEST(ExtensionRuntimeBackendModuleTest, RegistrationApplyUsesNativeAlignmentFromStoredTransforms)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ registration = py::module_::import("afternight.registration");
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ sequences = py::module_::import("afternight.sequences");

    py::dict locals;
    locals["registration"] = registration;
    locals["core"] = core;
    locals["io"] = io;
    locals["sequences"] = sequences;
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
sequence = sequences.create("native_apply_registration_test")

reference = np.zeros((48, 48), dtype=np.float32)
reference[24, 24] = 1.0
moving = np.zeros((48, 48), dtype=np.float32)
moving[20, 29] = 1.0

reference_path = os.path.join(root, "reference.fit")
moving_path = os.path.join(root, "moving.fit")
metadata = {"IMAGETYP": "LIGHT", "FILTER": "L"}
io.save(core.from_numpy(reference, metadata), reference_path)
io.save(core.from_numpy(moving, metadata), moving_path)

identity = {
    "method": "reference",
    "transform_type": "similarity",
    "matrix": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
}
shift_to_reference = {
    "method": "star_matching",
    "transform_type": "similarity",
    "matrix": [[1.0, 0.0, -5.0], [0.0, 1.0, 4.0], [0.0, 0.0, 1.0]],
    "matched_stars": 8,
    "rms_pixels": 0.1,
}

sequence.add_frame(path=reference_path, metadata=metadata, registration=identity)
sequence.add_frame(path=moving_path, metadata=metadata, registration=shift_to_reference)

applied = registration.apply(
    sequence,
    os.path.join(root, "applied"),
    reference_index=0,
    interpolation="nearest",
)
applied_frames = applied.frames()
output_paths_exist = all(os.path.exists(frame.path) for frame in applied_frames)
moving_pixels = np.asarray(io.load(applied_frames[1].path).to_numpy(), dtype=np.float32)
aligned_peak = float(moving_pixels[24, 24])
source_peak = float(moving_pixels[20, 29])
applied_method = applied_frames[1].registration.get("method", "")
)PY",
             locals, locals);

    EXPECT_TRUE(locals["output_paths_exist"].cast<bool>());
    EXPECT_GT(locals["aligned_peak"].cast<double>(), 0.9);
    EXPECT_LT(locals["source_peak"].cast<double>(), 0.1);
    EXPECT_EQ(locals["applied_method"].cast<std::string>(), "applied_registration");
}

TEST(ExtensionRuntimeBackendModuleTest, StackingStackSequenceUsesNativeEngineMedian)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ sequences = py::module_::import("afternight.sequences");
    py::module_ stacking = py::module_::import("afternight.stacking");

    py::dict locals;
    locals["core"] = core;
    locals["io"] = io;
    locals["sequences"] = sequences;
    locals["stacking"] = stacking;
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
values = [0.10, 0.20, 0.90]
sequence = sequences.create("native_median_stack_test")
for index, value in enumerate(values, start=1):
    frame = np.full((24, 24), value, dtype=np.float32)
    path = os.path.join(root, f"light_{index:03d}.fit")
    io.save(core.from_numpy(frame, {"IMAGETYP": "LIGHT", "FILTER": "L"}), path)
    sequence.add_frame(path=path, metadata={"IMAGETYP": "LIGHT", "FILTER": "L"})

output_path = os.path.join(root, "stacked_median.fit")
result = stacking.stack(
    sequence,
    output_path,
    method="median",
    metadata={"OBJECT": "AN_NATIVE_MEDIAN"},
)
stacked = np.asarray(io.load(output_path).to_numpy(), dtype=np.float32)
median_pixel = float(stacked[0, 0])
output_exists = os.path.exists(output_path)
)PY",
             locals, locals);

    EXPECT_TRUE(locals["output_exists"].cast<bool>());
    EXPECT_EQ(locals["result"].cast<py::dict>()["frame_count"].cast<int>(), 3);
    EXPECT_EQ(locals["result"].cast<py::dict>()["method"].cast<std::string>(), "median");
    EXPECT_NEAR(locals["median_pixel"].cast<double>(), 0.20, 0.01);
}

TEST(ExtensionRuntimeBackendModuleTest, StackingStackSequenceWritesNativeQualityMaps)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ sequences = py::module_::import("afternight.sequences");
    py::module_ stacking = py::module_::import("afternight.stacking");

    py::dict locals;
    locals["core"] = core;
    locals["io"] = io;
    locals["sequences"] = sequences;
    locals["stacking"] = stacking;
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
sequence = sequences.create("native_quality_map_stack_test")
for index, value in enumerate((0.10, 0.20, 0.30), start=1):
    frame = np.full((20, 20), value, dtype=np.float32)
    path = os.path.join(root, f"light_{index:03d}.fit")
    io.save(core.from_numpy(frame, {"IMAGETYP": "LIGHT", "FILTER": "L"}), path)
    sequence.add_frame(path=path, metadata={"IMAGETYP": "LIGHT", "FILTER": "L"})

output_path = os.path.join(root, "stacked_quality.fit")
result = stacking.stack(
    sequence,
    output_path,
    method="average",
    params={
        "quality_report": True,
        "quality_maps": {
            "coverage": True,
            "variance": True,
            "snr_gain": True,
            "weight_sum": True,
        },
    },
)
quality_maps = dict(result.get("quality_maps", {}))
quality_report = dict(result.get("quality_report", {}))
quality_map_paths_exist = all(os.path.exists(path) for path in quality_maps.values())
coverage = np.asarray(io.load(quality_maps["coverage"]).to_numpy(), dtype=np.float32)
coverage_shape = tuple(coverage.shape[:2])
coverage_pixel = float(coverage[0, 0])
report_valid = bool(quality_report.get("valid", False))
effective_frame_count = float(quality_report.get("effective_frame_count", 0.0))
)PY",
             locals, locals);

    EXPECT_TRUE(locals["quality_map_paths_exist"].cast<bool>());
    EXPECT_EQ(locals["coverage_shape"].cast<py::tuple>()[0].cast<int>(), 20);
    EXPECT_EQ(locals["coverage_shape"].cast<py::tuple>()[1].cast<int>(), 20);
    EXPECT_GT(locals["coverage_pixel"].cast<double>(), 0.0);
    EXPECT_TRUE(locals["report_valid"].cast<bool>());
    EXPECT_GT(locals["effective_frame_count"].cast<double>(), 0.0);
}

TEST(ExtensionRuntimeBackendModuleTest, StackingDrizzleSequenceUsesNativeStoredTransforms)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ core = py::module_::import("afternight.core");
    py::module_ io = py::module_::import("afternight.io");
    py::module_ sequences = py::module_::import("afternight.sequences");
    py::module_ stacking = py::module_::import("afternight.stacking");

    py::dict locals;
    locals["core"] = core;
    locals["io"] = io;
    locals["sequences"] = sequences;
    locals["stacking"] = stacking;
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
sequence = sequences.create("native_drizzle_stack_test")

reference = np.zeros((24, 24), dtype=np.float32)
reference[12, 12] = 1.0
moving = np.zeros((24, 24), dtype=np.float32)
moving[12, 13] = 1.0

reference_path = os.path.join(root, "reference.fit")
moving_path = os.path.join(root, "moving.fit")
metadata = {"IMAGETYP": "LIGHT", "FILTER": "L"}
io.save(core.from_numpy(reference, metadata), reference_path)
io.save(core.from_numpy(moving, metadata), moving_path)

sequence.add_frame(
    path=reference_path,
    metadata=metadata,
    registration={
        "method": "reference",
        "transform_type": "similarity",
        "matrix": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
    },
)
sequence.add_frame(
    path=moving_path,
    metadata=metadata,
    registration={
        "method": "star_matching",
        "transform_type": "similarity",
        "matrix": [[1.0, 0.0, -1.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
        "matched_stars": 8,
        "rms_pixels": 0.1,
    },
)

output_path = os.path.join(root, "drizzle.fit")
result = stacking.drizzle(
    sequence,
    output_path,
    method="average",
    reference_index=0,
    interpolation="nearest",
    params={
        "quality_report": True,
        "drizzle": {
            "scale": 2.0,
            "pixel_fraction": 0.9,
            "kernel": "square",
        },
    },
)
drizzled = np.asarray(io.load(output_path).to_numpy(), dtype=np.float32)
output_exists = os.path.exists(output_path)
drizzle_shape = tuple(drizzled.shape[:2])
drizzle_info = dict(result.get("drizzle", {}))
report_valid = bool(result.get("quality_report", {}).get("valid", False))
)PY",
             locals, locals);

    EXPECT_TRUE(locals["output_exists"].cast<bool>());
    EXPECT_EQ(locals["drizzle_shape"].cast<py::tuple>()[0].cast<int>(), 48);
    EXPECT_EQ(locals["drizzle_shape"].cast<py::tuple>()[1].cast<int>(), 48);
    EXPECT_TRUE(locals["drizzle_info"].cast<py::dict>()["enabled"].cast<bool>());
    EXPECT_NEAR(locals["drizzle_info"].cast<py::dict>()["scale"].cast<double>(), 2.0, 0.01);
    EXPECT_TRUE(locals["report_valid"].cast<bool>());
}

// The registration and alignment calls read a mosaic demosaiced by the Bayer fast estimate whatever its arrangement,
// which the engine refuses over an X-Trans sensor's six-by-six table, so no X-Trans frame was aligned: it is read by
// the tabulated fast estimate, under its own table.
TEST(ExtensionRuntimeBackendModuleTest, RegistrationApplyDemosaicsAnXTransMosaicUnderItsOwnTable)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::dict locals;
    locals["rt"] = py::module_::import("_afternight_runtime");
    py::exec(R"PY(
import base64
import os
import tempfile

import numpy as np

# An X-Trans sensor's six-by-six table row by row, and a mosaic whose sites of each colour all hold one level.
pattern = "GBGGRGRGRBGBGBGGRGGRGGBGBGBRGRGRGGBG"
levels = {"R": 0.6, "G": 0.3, "B": 0.15}
height, width = 36, 48
mosaic = np.array([[levels[pattern[(y % 6) * 6 + x % 6]] for x in range(width)] for y in range(height)],
                  dtype=np.float32)


def write_xtrans(path):
    # One grey image stating the table, its samples inline: XISF states a six-by-six array, which FITS cannot.
    data = base64.b64encode(mosaic.astype("<f4").tobytes()).decode("ascii")
    header = ('<?xml version="1.0" encoding="UTF-8"?>'
              '<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">'
              '<Image geometry="%d:%d:1" sampleFormat="Float32" bounds="0:1" colorSpace="Gray" '
              'location="inline:base64">%s<ColorFilterArray pattern="%s" width="6" height="6"/></Image>'
              '</xisf>' % (width, height, data, pattern)).encode("utf-8")
    with open(path, "wb") as stream:
        stream.write(b"XISF0100" + len(header).to_bytes(4, "little") + bytes(4) + header)


tmp = tempfile.TemporaryDirectory()
root = tmp.name
paths = [os.path.join(root, "reference.xisf"), os.path.join(root, "frame.xisf")]
for path in paths:
    write_xtrans(path)
identity = {"matrix": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]], "transform_type": "similarity"}
failure = ""
aligned_shape = []
centre = []
try:
    result = rt.apply_registration_paths(paths, [None, identity], os.path.join(root, "aligned"),
                                         interpolation="nearest")
    failure = "; ".join(frame["error_message"] for frame in result["frames"] if not frame["succeeded"])
    if result["succeeded_count"] == 2:
        aligned = np.asarray(rt.load_image(result["frames"][1]["output_path"]).to_numpy(), dtype=np.float64)
        aligned_shape = list(aligned.shape)
        centre = [float(value) for value in aligned[height // 2, width // 2]]
except Exception as error:
    failure = str(error)
)PY",
             locals, locals);

    EXPECT_EQ(locals["failure"].cast<std::string>(), "");
    const py::list shape = locals["aligned_shape"].cast<py::list>();
    ASSERT_EQ(py::len(shape), 3u);
    EXPECT_EQ(shape[0].cast<int>(), 36);
    EXPECT_EQ(shape[1].cast<int>(), 48);
    EXPECT_EQ(shape[2].cast<int>(), 3) << "the frame is aligned in colour";
    // Every site of a colour holds one level, so that colour's estimate is its level wherever the sites lie.
    const py::list centre = locals["centre"].cast<py::list>();
    ASSERT_EQ(py::len(centre), 3u);
    EXPECT_NEAR(centre[0].cast<double>(), 0.6, 1e-4);
    EXPECT_NEAR(centre[1].cast<double>(), 0.3, 1e-4);
    EXPECT_NEAR(centre[2].cast<double>(), 0.15, 1e-4);
}

// A frame whose sky has no spread measures a background noise of zero, which the stack call handed the stack as a
// figure, and the stack refuses a measured figure that is not finite and positive whatever its weighting reads, so a
// stack of such frames under any measured weighting failed: the figure is left out, and the stack runs.
TEST(ExtensionRuntimeBackendModuleTest, StackingUnderAMeasuredWeightingTakesFramesWhoseSkyHasNoNoise)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::dict locals;
    locals["rt"] = py::module_::import("_afternight_runtime");
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
paths = []
for index, value in enumerate((0.2, 0.3, 0.4)):
    path = os.path.join(root, "flat_%d.fits" % index)
    rt.save_image(rt.image_from_numpy(np.full((96, 96), value, np.float32), {"IMAGETYP": "LIGHT"}), path)
    paths.append(path)
output_path = os.path.join(root, "stacked.fits")
failure = ""
centre = float("nan")
try:
    rt.stack_sequence_paths(paths, output_path, method="average", params={"weighting": "star_count"})
    centre = float(np.asarray(rt.load_image(output_path).to_numpy(), dtype=np.float64)[48, 48])
except Exception as error:
    failure = str(error)
)PY",
             locals, locals);

    EXPECT_EQ(locals["failure"].cast<std::string>(), "");
    EXPECT_NEAR(locals["centre"].cast<double>(), 0.3, 1e-4);
}

// The stack call measures one star width per frame, so per-channel FWHM weighting can only weigh each channel by its
// frame's width: the call says so and leaves the option out. It took the option and said nothing.
TEST(ExtensionRuntimeBackendModuleTest, PerChannelFwhmWeightingIsSaidToWeighEachChannelByItsFramesWidth)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::dict locals;
    locals["rt"] = py::module_::import("_afternight_runtime");
    py::exec(R"PY(
import os
import tempfile
import warnings

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
paths = []
for index, value in enumerate((0.2, 0.3, 0.4)):
    path = os.path.join(root, "flat_%d.fits" % index)
    rt.save_image(rt.image_from_numpy(np.full((96, 96), value, np.float32), {"IMAGETYP": "LIGHT"}), path)
    paths.append(path)
output_path = os.path.join(root, "stacked.fits")
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    rt.stack_sequence_paths(paths, output_path, method="average",
                            params={"weighting": "fwhm", "enable_per_channel_fwhm_weighting": True})
said = [str(warning.message) for warning in caught]
)PY",
             locals, locals);

    std::string joined;
    bool saidSo = false;
    for (const py::handle warning : locals["said"].cast<py::list>())
    {
        const std::string text = warning.cast<std::string>();
        joined += text + "\n";
        saidSo = saidSo || text.find("one star width per frame") != std::string::npos;
    }
    EXPECT_TRUE(saidSo) << joined;
}

// The drizzle keeps the flux each light's pixel carried, so a frame drizzled onto a canvas twice as fine holds a
// quarter of the light's level, and the drizzled stack the call wrote lay that far below its lights. Each drizzled
// frame is restated at its lights' level before it is stacked.
TEST(ExtensionRuntimeBackendModuleTest, StackingDrizzleSequenceKeepsTheLightsLevel)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::dict locals;
    locals["rt"] = py::module_::import("_afternight_runtime");
    py::exec(R"PY(
import os
import tempfile

import numpy as np

tmp = tempfile.TemporaryDirectory()
root = tmp.name
paths = []
for index in range(3):
    path = os.path.join(root, "light_%d.fits" % index)
    rt.save_image(rt.image_from_numpy(np.full((24, 24), 0.25, np.float32), {"IMAGETYP": "LIGHT"}), path)
    paths.append(path)
identity = {"matrix": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]], "transform_type": "similarity"}
output_path = os.path.join(root, "drizzled.fits")
rt.drizzle_stack_sequence_paths(paths, [None, identity, identity], output_path,
                                params={"drizzle": {"scale": 2.0, "pixel_fraction": 1.0, "kernel": "square"}})
drizzled = np.asarray(rt.load_image(output_path).to_numpy(), dtype=np.float64)
drizzle_shape = list(drizzled.shape)
inner = drizzled[8:40, 8:40]
inner_low = float(np.nanmin(inner))
inner_high = float(np.nanmax(inner))
)PY",
             locals, locals);

    const py::list shape = locals["drizzle_shape"].cast<py::list>();
    ASSERT_EQ(py::len(shape), 2u);
    EXPECT_EQ(shape[0].cast<int>(), 48);
    EXPECT_EQ(shape[1].cast<int>(), 48);
    // Away from the canvas's edges every output pixel is covered whole; the lights hold 0.25 and so does the stack.
    EXPECT_NEAR(locals["inner_low"].cast<double>(), 0.25, 1e-4);
    EXPECT_NEAR(locals["inner_high"].cast<double>(), 0.25, 1e-4);
}

TEST(ExtensionRuntimeBackendModuleTest, UiModuleSupportsNativeProcessWindowSurface)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ afternight = py::module_::import("afternight");
    py::module_ protocol = py::module_::import("afternight.protocol");
    py::module_ ui = py::module_::import("afternight.ui");

    py::dict locals;
    locals["afternight"] = afternight;
    locals["protocol"] = protocol;
    locals["ui"] = ui;
    py::exec(R"PY(
class HostBridge:
    def __init__(self):
        self.calls = []

    def current_view_info(self):
        return {
            "width": 321,
            "height": 123,
            "channels": 3,
            "pixel_type": "float32",
            "view_name": "host-view",
            "file_name": "host.fit",
        }

    def theme_tokens(self):
        return {
            "colors": {"accent": "#123456"},
            "spacing": {"sm": 4},
            "is_dark": True,
        }

    def ui_show_error(self, title, message):
        self.calls.append(("error", title, message))

    def ui_show_warning(self, title, message):
        self.calls.append(("warning", title, message))

    def ui_show_info(self, title, message):
        self.calls.append(("info", title, message))

    def ui_confirm(self, title, message, warn):
        self.calls.append(("confirm", title, message, warn))
        return True

    def process_window_hide(self):
        self.calls.append(("hide",))
        return True

    def process_window_close(self):
        self.calls.append(("close",))
        return True

    def view_commit_numpy(self,
                          array,
                          metadata,
                          history_step_name,
                          history_step_description,
                          allow_resized_output=False):
        self.calls.append(
            (
                "commit",
                tuple(array.shape),
                dict(metadata),
                history_step_name,
                history_step_description,
                bool(allow_resized_output),
            )
        )
        return True

    def ui_open_image(self, array, metadata, title):
        self.calls.append(
            (
                "open_image",
                tuple(array.shape),
                dict(metadata),
                title,
            )
        )
        return True

saved_host = protocol._host_runtime
try:
    protocol._host_runtime = None
    local_view = afternight.ViewRef(
        view_id="local",
        view_name="local-view",
        file_name="local.fit",
        width=640,
        height=480,
        channels=1,
        pixel_type="uint16",
    )
    local_results = {
        "width": local_view.width,
        "height": local_view.height,
        "channels": local_view.channels,
        "pixel_type": local_view.pixel_type,
    }

    bridge = HostBridge()
    protocol._host_runtime = bridge

    host_view = afternight.ViewRef()
    tokens = ui.tokens()
    ui.show_error("boom")
    ui.show_warning("careful")
    ui.show_info("hello")
    confirm_result = ui.confirm("continue?", warn=True)
    ui.hide_process_window()
    ui.close_process_window()
    process_window_meta = ui.process_window_meta(
        size=(1400, 820),
        fixed_size=True,
        target_channel_filter=[1, 3],
        sub_area=True,
        sub_area_default_enabled=False,
        sub_area_size=(800, 600),
        sub_area_label="Preview Area",
        controls_panel_width=520,
        preview_hq_default=True,
        preview_autostretch=True,
        preview_autostretch_default=True,
        preview_area=False,
        header_progress=False,
        tool_configuration={
            "settings_key": "executable_path",
            "button_label": "Configure",
            "dialog_size": [720, 500],
            "required_executables": ["cosmic_tool"],
        },
    )

    class CommitImage:
        def __init__(self):
            self.metadata = {"FILTER": "L"}

        def to_numpy(self):
            import numpy as np
            return np.arange(6, dtype=np.float32).reshape(2, 3)

    commit_result = ui.commit_image(
        CommitImage(),
        history_step_name="Manual Apply",
        history_step_description="Committed from custom dialog",
        allow_resized_output=True,
    )
    open_result = ui.open_image(CommitImage(), title="Generated Background")

    results = {
        "local_width": local_results["width"],
        "local_height": local_results["height"],
        "local_channels": local_results["channels"],
        "local_pixel_type": local_results["pixel_type"],
        "host_width": host_view.width,
        "host_height": host_view.height,
        "host_channels": host_view.channels,
        "host_pixel_type": host_view.pixel_type,
        "tokens": tokens,
        "calls": list(bridge.calls),
        "confirm_result": confirm_result,
        "commit_result": commit_result,
        "open_result": open_result,
        "processwindow_is_native_base": issubclass(ui.ProcessWindow, afternight.ProcessExtension),
        "rtpreview_is_native_base": issubclass(ui.RTPreviewProcess, afternight.RTPreviewExtension),
        "processwindow_has_build_params_ui": hasattr(ui.ProcessWindow, "build_params_ui"),
        "rtpreview_has_build_params_ui": hasattr(ui.RTPreviewProcess, "build_params_ui"),
        "has_param_panel": hasattr(ui, "ParamPanel"),
        "has_card_widget": hasattr(ui, "AnCardWidget"),
        "has_collapsible_section": hasattr(ui, "AnCollapsibleSection"),
        "has_info_banner": hasattr(ui, "AnInfoBanner"),
        "process_window_meta": process_window_meta,
        "image_view_alias_is_viewref": ui.ImageView is afternight.ViewRef,
    }
finally:
    protocol._host_runtime = saved_host
)PY",
             py::globals(), locals);

    const py::dict results = locals["results"].cast<py::dict>();
    EXPECT_EQ(results["local_width"].cast<int>(), 640);
    EXPECT_EQ(results["local_height"].cast<int>(), 480);
    EXPECT_EQ(results["local_channels"].cast<int>(), 1);
    EXPECT_EQ(results["local_pixel_type"].cast<std::string>(), "uint16");
    EXPECT_EQ(results["host_width"].cast<int>(), 321);
    EXPECT_EQ(results["host_height"].cast<int>(), 123);
    EXPECT_EQ(results["host_channels"].cast<int>(), 3);
    EXPECT_EQ(results["host_pixel_type"].cast<std::string>(), "float32");
    EXPECT_TRUE(results["confirm_result"].cast<bool>());
    EXPECT_TRUE(results["commit_result"].cast<bool>());
    EXPECT_TRUE(results["open_result"].cast<bool>());
    EXPECT_TRUE(results["processwindow_is_native_base"].cast<bool>());
    EXPECT_TRUE(results["rtpreview_is_native_base"].cast<bool>());
    EXPECT_FALSE(results["processwindow_has_build_params_ui"].cast<bool>());
    EXPECT_FALSE(results["rtpreview_has_build_params_ui"].cast<bool>());
    EXPECT_FALSE(results["has_param_panel"].cast<bool>());
    EXPECT_FALSE(results["has_card_widget"].cast<bool>());
    EXPECT_FALSE(results["has_collapsible_section"].cast<bool>());
    EXPECT_FALSE(results["has_info_banner"].cast<bool>());
    EXPECT_TRUE(results["image_view_alias_is_viewref"].cast<bool>());

    const py::dict process_window_meta = results["process_window_meta"].cast<py::dict>();
    EXPECT_EQ(process_window_meta["id"].cast<std::string>(), "window_meta");
    EXPECT_EQ(process_window_meta["type"].cast<std::string>(), "meta");
    EXPECT_TRUE(process_window_meta["fixed_size"].cast<bool>());
    EXPECT_TRUE(process_window_meta["target_selector"].cast<bool>());
    const py::list window_size = process_window_meta["window_size"].cast<py::list>();
    ASSERT_EQ(py::len(window_size), 2);
    EXPECT_EQ(window_size[0].cast<int>(), 1400);
    EXPECT_EQ(window_size[1].cast<int>(), 820);
    const py::list sub_area_size = process_window_meta["sub_area_size"].cast<py::list>();
    ASSERT_EQ(py::len(sub_area_size), 2);
    EXPECT_EQ(sub_area_size[0].cast<int>(), 800);
    EXPECT_EQ(sub_area_size[1].cast<int>(), 600);
    EXPECT_FALSE(process_window_meta["sub_area_default_enabled"].cast<bool>());
    EXPECT_EQ(process_window_meta["controls_panel_width"].cast<int>(), 520);
    EXPECT_TRUE(process_window_meta["preview_hq_default"].cast<bool>());
    EXPECT_TRUE(process_window_meta["preview_autostretch"].cast<bool>());
    EXPECT_TRUE(process_window_meta["preview_autostretch_default"].cast<bool>());
    EXPECT_FALSE(process_window_meta["preview_area"].cast<bool>());
    EXPECT_FALSE(process_window_meta["header_progress"].cast<bool>());
    const py::dict tool_configuration = process_window_meta["tool_configuration"].cast<py::dict>();
    EXPECT_EQ(tool_configuration["settings_key"].cast<std::string>(), "executable_path");
    EXPECT_EQ(tool_configuration["button_label"].cast<std::string>(), "Configure");
    const py::list dialog_size = tool_configuration["dialog_size"].cast<py::list>();
    ASSERT_EQ(py::len(dialog_size), 2);
    EXPECT_EQ(dialog_size[0].cast<int>(), 720);
    EXPECT_EQ(dialog_size[1].cast<int>(), 500);
    const py::list required_executables = tool_configuration["required_executables"].cast<py::list>();
    ASSERT_EQ(py::len(required_executables), 1);
    EXPECT_EQ(required_executables[0].cast<std::string>(), "cosmic_tool");

    const py::dict tokens = results["tokens"].cast<py::dict>();
    const py::dict colors = tokens["colors"].cast<py::dict>();
    EXPECT_EQ(colors["accent"].cast<std::string>(), "#123456");
    EXPECT_TRUE(tokens["is_dark"].cast<bool>());

    const py::list calls = results["calls"].cast<py::list>();
    ASSERT_EQ(py::len(calls), 8);
    EXPECT_EQ(calls[0].cast<py::tuple>()[0].cast<std::string>(), "error");
    EXPECT_EQ(calls[1].cast<py::tuple>()[0].cast<std::string>(), "warning");
    EXPECT_EQ(calls[2].cast<py::tuple>()[0].cast<std::string>(), "info");
    const py::tuple confirm_call = calls[3].cast<py::tuple>();
    EXPECT_EQ(confirm_call[0].cast<std::string>(), "confirm");
    EXPECT_TRUE(confirm_call[3].cast<bool>());
    EXPECT_EQ(calls[4].cast<py::tuple>()[0].cast<std::string>(), "hide");
    EXPECT_EQ(calls[5].cast<py::tuple>()[0].cast<std::string>(), "close");
    const py::tuple commit_call = calls[6].cast<py::tuple>();
    const py::tuple commit_shape = commit_call[1].cast<py::tuple>();
    const py::dict commit_metadata = commit_call[2].cast<py::dict>();
    EXPECT_EQ(commit_call[0].cast<std::string>(), "commit");
    EXPECT_EQ(commit_shape[0].cast<int>(), 2);
    EXPECT_EQ(commit_shape[1].cast<int>(), 3);
    EXPECT_EQ(commit_metadata[py::str("FILTER")].cast<std::string>(), "L");
    EXPECT_EQ(commit_call[3].cast<std::string>(), "Manual Apply");
    EXPECT_EQ(commit_call[4].cast<std::string>(), "Committed from custom dialog");
    EXPECT_TRUE(commit_call[5].cast<bool>());
    const py::tuple open_call = calls[7].cast<py::tuple>();
    const py::tuple open_shape = open_call[1].cast<py::tuple>();
    const py::dict open_metadata = open_call[2].cast<py::dict>();
    EXPECT_EQ(open_call[0].cast<std::string>(), "open_image");
    EXPECT_EQ(open_shape[0].cast<int>(), 2);
    EXPECT_EQ(open_shape[1].cast<int>(), 3);
    EXPECT_EQ(open_metadata[py::str("FILTER")].cast<std::string>(), "L");
    EXPECT_EQ(open_call[3].cast<std::string>(), "Generated Background");
}

TEST(ExtensionRuntimeBackendModuleTest, ProgressFacadeForwardsHostUpdatesWhenAvailable)
{
    ASSERT_SHARED_RUNTIME();
    py::gil_scoped_acquire gil;
    py::module_ afternight = py::module_::import("afternight");
    py::module_ protocol = py::module_::import("afternight.protocol");

    py::dict locals;
    locals["afternight"] = afternight;
    locals["protocol"] = protocol;
    py::exec(R"PY(
class HostBridge:
    def __init__(self):
        self.calls = []

    def progress_set_text(self, text):
        self.calls.append(("text", text))

    def progress_set_value(self, value):
        self.calls.append(("value", float(value)))

saved_host = protocol._host_runtime
try:
    protocol._host_runtime = None
    local_progress = afternight.Progress()
    local_progress.set_text("Preparing")
    local_progress.set_value(12.5)

    bridge = HostBridge()
    protocol._host_runtime = bridge
    host_progress = afternight.Progress()
    host_progress.set_text("Downloading model")
    host_progress.set_value(42.0)

    results = {
        "local_text": local_progress.text,
        "local_value": local_progress.value,
        "host_text": host_progress.text,
        "host_value": host_progress.value,
        "host_calls": list(bridge.calls),
    }
finally:
    protocol._host_runtime = saved_host
)PY",
             py::globals(), locals);

    const py::dict results = locals["results"].cast<py::dict>();
    EXPECT_EQ(results["local_text"].cast<std::string>(), "Preparing");
    EXPECT_DOUBLE_EQ(results["local_value"].cast<double>(), 12.5);
    EXPECT_EQ(results["host_text"].cast<std::string>(), "Downloading model");
    EXPECT_DOUBLE_EQ(results["host_value"].cast<double>(), 42.0);

    const py::list calls = results["host_calls"].cast<py::list>();
    ASSERT_EQ(py::len(calls), 2);
    EXPECT_EQ(calls[0].cast<py::tuple>()[0].cast<std::string>(), "text");
    EXPECT_EQ(calls[0].cast<py::tuple>()[1].cast<std::string>(), "Downloading model");
    EXPECT_EQ(calls[1].cast<py::tuple>()[0].cast<std::string>(), "value");
    EXPECT_DOUBLE_EQ(calls[1].cast<py::tuple>()[1].cast<double>(), 42.0);
}
