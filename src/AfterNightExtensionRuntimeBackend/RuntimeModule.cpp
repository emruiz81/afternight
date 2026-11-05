// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The embedded `_afternight_runtime` module: the runtime SDK's native half, registered with the interpreter when the
// backend module is loaded, before the host starts its interpreter.
//
// Every binding keeps the keyword names and defaults the SDK declares - the behaviour matrix reads them from this
// definition - and is defined beside the engine area it reaches (RuntimeBindings.h). Two members are the
// backend's own and not the SDK's: `_session_capsule()`, through which the full host borrows the backend's engine
// session (RuntimeSessionCapsule.h), and `_RuntimeImage._export_state()`, which reports the writable-export protocol's
// state to the backend's suite.

#include "RuntimePython.h"

#include "RuntimeBindings.h"
#include "RuntimeImage.h"

#include <memory>
#include <string>

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(_afternight_runtime, m)
{
    using an::sdk::RuntimeImage;

    py::class_<RuntimeImage, std::shared_ptr<RuntimeImage>>(m, "_RuntimeImage")
        .def("to_numpy", &RuntimeImage::toNumpy)
        .def("from_numpy", &RuntimeImage::fromNumpy)
        .def("metadata", &RuntimeImage::metadata)
        .def("properties", &RuntimeImage::properties)
        .def("set_metadata", &RuntimeImage::setMetadata)
        .def("clone", &RuntimeImage::clone)
        .def("_export_state", &RuntimeImage::exportState);

    m.def("_session_capsule", &an::sdk::sessionCapsuleCall);
    m.def("available_pixel_types", &an::sdk::availablePixelTypes);
    m.def("image_from_numpy", &an::sdk::imageFromNumpyCall, py::arg("array"), py::arg("metadata") = py::none());
    m.def("load_image", &an::sdk::loadImageCall, py::arg("path"), py::arg("load_metadata") = true);
    m.def("save_image", &an::sdk::saveImageCall, py::arg("image"), py::arg("path"), py::arg("options") = py::none());
    m.def("is_supported_path", [](const std::string& path) { return an::sdk::isSupportedImagePath(path); });
    m.def("convert_pixel_type", &an::sdk::convertPixelTypeCall, py::arg("image"), py::arg("pixel_type"));
    m.def("apply_linear_transform", &an::sdk::applyLinearTransformCall, py::arg("source"), py::arg("destination"),
          py::arg("scale") = 1.0, py::arg("offset") = 0.0);
    m.def("extract_channel", &an::sdk::extractChannelCall, py::arg("image"), py::arg("channel"));
    m.def("extract_channels", &an::sdk::extractChannelsCall, py::arg("image"));
    m.def("compose_rgb", &an::sdk::composeRgbCall, py::arg("red"), py::arg("green"), py::arg("blue"));
    m.def("detect_fwhm", &an::sdk::detectFwhmCall, py::arg("image"), py::arg("channel") = -1);
    m.def("find_stars", &an::sdk::findStarsCall, py::arg("image"), py::arg("channel") = -1, py::arg("max_stars") = 512,
          py::arg("params") = py::none());
    m.def("create_calibration_masters_paths", &an::sdk::createCalibrationMastersPathsCall, py::arg("role_paths"),
          py::arg("output_dir"), py::arg("filter_name") = "L", py::arg("method") = "average",
          py::arg("params") = py::none(), py::arg("save_options") = py::none());
    m.def("calibrate_sequence_paths", &an::sdk::calibrateSequencePathsCall, py::arg("paths"), py::arg("masters"),
          py::arg("output_dir"), py::arg("params") = py::none(), py::arg("output_options") = py::none());
    m.def("register_sequence_paths", &an::sdk::registerSequencePathsCall, py::arg("paths"), py::arg("output_dir"),
          py::arg("reference_index") = 0, py::arg("max_stars") = 512, py::arg("min_stars") = 6,
          py::arg("params") = py::none(), py::arg("transform_type") = "similarity", py::arg("polynomial_order") = 0,
          py::arg("auto_select_polynomial_order") = false, py::arg("interpolation") = "lanczos4");
    m.def("apply_registration_paths", &an::sdk::applyRegistrationPathsCall, py::arg("paths"), py::arg("registrations"),
          py::arg("output_dir"), py::arg("reference_index") = 0, py::arg("interpolation") = "lanczos4",
          py::arg("alignment_mode") = "standard", py::arg("output_options") = py::none());
    m.def("stack_sequence_paths", &an::sdk::stackSequencePathsCall, py::arg("paths"), py::arg("output_path"),
          py::arg("method") = "average", py::arg("params") = py::none(), py::arg("metadata") = py::none(),
          py::arg("save_options") = py::none());
    m.def("drizzle_stack_sequence_paths", &an::sdk::drizzleStackSequencePathsCall, py::arg("paths"),
          py::arg("registrations"), py::arg("output_path"), py::arg("method") = "average",
          py::arg("params") = py::none(), py::arg("metadata") = py::none(), py::arg("save_options") = py::none(),
          py::arg("reference_index") = 0, py::arg("interpolation") = "lanczos4",
          py::arg("alignment_mode") = "standard");
    m.def("richardson_lucy_deconvolution", &an::sdk::richardsonLucyDeconvolutionCall, py::arg("image"),
          py::arg("fwhm_x"), py::arg("fwhm_y") = 0.0, py::arg("angle_degrees") = 0.0, py::arg("iterations") = 20,
          py::arg("tv_regularization") = 0.0, py::arg("damping") = 0.0, py::arg("stopping_delta") = 0.0,
          py::arg("psf_size") = 0, py::arg("output_pixel_type") = "float32", py::arg("preserve_flux") = true,
          py::arg("clamp_to_unit_range") = true, py::arg("profile") = "gaussian", py::arg("beta") = 4.5,
          py::arg("relative_stopping_delta") = 0.0, py::arg("edge_taper_radius") = 0.0,
          py::arg("convolution_backend") = "auto", py::arg("color_mode") = "auto", py::arg("edge_taper_mode") = "auto",
          py::arg("edge_taper_iterations") = 3, py::arg("regularization_mode") = "total_variation",
          py::arg("update_method") = "multiplicative", py::arg("gradient_descent_step") = 0.0003);
    m.def("wiener_deconvolution", &an::sdk::wienerDeconvolutionCall, py::arg("image"), py::arg("fwhm_x"),
          py::arg("fwhm_y") = 0.0, py::arg("angle_degrees") = 0.0, py::arg("regularization") = 0.01,
          py::arg("psf_size") = 0, py::arg("output_pixel_type") = "float32", py::arg("preserve_flux") = true,
          py::arg("clamp_to_unit_range") = true, py::arg("profile") = "gaussian", py::arg("beta") = 4.5,
          py::arg("edge_taper_radius") = 0.0, py::arg("color_mode") = "auto", py::arg("edge_taper_mode") = "auto",
          py::arg("edge_taper_iterations") = 3);
    m.def("richardson_lucy_star_deconvolution", &an::sdk::richardsonLucyStarDeconvolutionCall, py::arg("image"),
          py::arg("channel") = -1, py::arg("max_stars") = 512, py::arg("star_params") = py::none(),
          py::arg("iterations") = 20, py::arg("tv_regularization") = 0.0, py::arg("damping") = 0.0,
          py::arg("stopping_delta") = 0.0, py::arg("psf_size") = 0, py::arg("output_pixel_type") = "float32",
          py::arg("preserve_flux") = true, py::arg("clamp_to_unit_range") = true, py::arg("profile") = "auto",
          py::arg("minimum_star_count") = 3, py::arg("ignore_saturated") = true, py::arg("circularize") = false,
          py::arg("relative_stopping_delta") = 0.0, py::arg("edge_taper_radius") = 0.0,
          py::arg("convolution_backend") = "auto", py::arg("color_mode") = "auto", py::arg("edge_taper_mode") = "auto",
          py::arg("edge_taper_iterations") = 3, py::arg("regularization_mode") = "total_variation",
          py::arg("update_method") = "multiplicative", py::arg("gradient_descent_step") = 0.0003);
}
