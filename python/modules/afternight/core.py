# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Runtime-backed core image operations for the public AfterNight SDK."""

from . import (
    _card_metadata,
    _coerce_to_runtime_image,
    _host_available,
    _host_call,
    _require_runtime_module,
    _wrap_runtime_image,
)


def available_pixel_types():
    return list(_require_runtime_module().available_pixel_types())


def from_numpy(array, metadata=None):
    runtime_module = _require_runtime_module()
    # A lent image's metadata carries its sidecar's structured records beside its cards; only the cards are kept.
    return _wrap_runtime_image(runtime_module.image_from_numpy(array, _card_metadata(metadata)))


def apply_linear_transform(source, destination, scale=1.0, offset=0.0):
    if _host_available() and source._token is not None and destination._token is not None:
        _host_call(
            "image_apply_linear_transform",
            source._token,
            destination._token,
            float(scale),
            float(offset),
        )
        return destination

    runtime_module = _require_runtime_module()
    runtime_module.apply_linear_transform(
        _coerce_to_runtime_image(source),
        _coerce_to_runtime_image(destination),
        float(scale),
        float(offset),
    )
    return destination


def convert_pixel_type(image, pixel_type):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(
        runtime_module.convert_pixel_type(_coerce_to_runtime_image(image), str(pixel_type))
    )


def extract_channel(image, channel):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(
        runtime_module.extract_channel(_coerce_to_runtime_image(image), int(channel))
    )


def extract_channels(image):
    runtime_module = _require_runtime_module()
    return tuple(
        _wrap_runtime_image(channel)
        for channel in runtime_module.extract_channels(_coerce_to_runtime_image(image))
    )


def compose_rgb(red, green, blue):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(
        runtime_module.compose_rgb(
            _coerce_to_runtime_image(red),
            _coerce_to_runtime_image(green),
            _coerce_to_runtime_image(blue),
        )
    )


def detect_fwhm(image, channel=-1):
    runtime_module = _require_runtime_module()
    return float(
        runtime_module.detect_fwhm(
            _coerce_to_runtime_image(image),
            int(channel),
        )
    )


def richardson_lucy_deconvolution(
    image,
    *,
    fwhm,
    fwhm_y=None,
    angle_degrees=0.0,
    iterations=20,
    tv_regularization=0.0,
    regularization_mode="total_variation",
    update_method="multiplicative",
    gradient_descent_step=0.0003,
    damping=0.0,
    stopping_delta=0.0,
    relative_stopping_delta=0.0,
    edge_taper_radius=0.0,
    edge_taper_mode="auto",
    edge_taper_iterations=3,
    convolution_backend="auto",
    color_mode="auto",
    psf_size=0,
    output_pixel_type="float32",
    preserve_flux=True,
    clamp_to_unit_range=True,
    profile="gaussian",
    beta=4.5,
):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(
        runtime_module.richardson_lucy_deconvolution(
            _coerce_to_runtime_image(image),
            float(fwhm),
            float(fwhm if fwhm_y is None else fwhm_y),
            float(angle_degrees),
            int(iterations),
            float(tv_regularization),
            float(damping),
            float(stopping_delta),
            int(psf_size),
            str(output_pixel_type),
            bool(preserve_flux),
            bool(clamp_to_unit_range),
            str(profile),
            float(beta),
            float(relative_stopping_delta),
            float(edge_taper_radius),
            str(convolution_backend),
            str(color_mode),
            str(edge_taper_mode),
            int(edge_taper_iterations),
            str(regularization_mode),
            str(update_method),
            float(gradient_descent_step),
        )
    )


def richardson_lucy_star_deconvolution(
    image,
    *,
    channel=-1,
    max_stars=512,
    star_params=None,
    iterations=20,
    tv_regularization=0.0,
    regularization_mode="total_variation",
    update_method="multiplicative",
    gradient_descent_step=0.0003,
    damping=0.0,
    stopping_delta=0.0,
    relative_stopping_delta=0.0,
    edge_taper_radius=0.0,
    edge_taper_mode="auto",
    edge_taper_iterations=3,
    convolution_backend="auto",
    color_mode="auto",
    psf_size=0,
    output_pixel_type="float32",
    preserve_flux=True,
    clamp_to_unit_range=True,
    profile="auto",
    minimum_star_count=3,
    ignore_saturated=True,
    circularize=False,
):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(
        runtime_module.richardson_lucy_star_deconvolution(
            _coerce_to_runtime_image(image),
            int(channel),
            int(max_stars),
            star_params,
            int(iterations),
            float(tv_regularization),
            float(damping),
            float(stopping_delta),
            int(psf_size),
            str(output_pixel_type),
            bool(preserve_flux),
            bool(clamp_to_unit_range),
            str(profile),
            int(minimum_star_count),
            bool(ignore_saturated),
            bool(circularize),
            float(relative_stopping_delta),
            float(edge_taper_radius),
            str(convolution_backend),
            str(color_mode),
            str(edge_taper_mode),
            int(edge_taper_iterations),
            str(regularization_mode),
            str(update_method),
            float(gradient_descent_step),
        )
    )


def wiener_deconvolution(
    image,
    *,
    fwhm,
    fwhm_y=None,
    angle_degrees=0.0,
    regularization=0.01,
    edge_taper_radius=0.0,
    edge_taper_mode="auto",
    edge_taper_iterations=3,
    color_mode="auto",
    psf_size=0,
    output_pixel_type="float32",
    preserve_flux=True,
    clamp_to_unit_range=True,
    profile="gaussian",
    beta=4.5,
):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(
        runtime_module.wiener_deconvolution(
            _coerce_to_runtime_image(image),
            float(fwhm),
            float(fwhm if fwhm_y is None else fwhm_y),
            float(angle_degrees),
            float(regularization),
            int(psf_size),
            str(output_pixel_type),
            bool(preserve_flux),
            bool(clamp_to_unit_range),
            str(profile),
            float(beta),
            float(edge_taper_radius),
            str(color_mode),
            str(edge_taper_mode),
            int(edge_taper_iterations),
        )
    )


__all__ = [
    "apply_linear_transform",
    "available_pixel_types",
    "compose_rgb",
    "convert_pixel_type",
    "detect_fwhm",
    "extract_channel",
    "extract_channels",
    "from_numpy",
    "richardson_lucy_deconvolution",
    "richardson_lucy_star_deconvolution",
    "wiener_deconvolution",
]
