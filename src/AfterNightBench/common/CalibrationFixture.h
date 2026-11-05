// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - shared image-calibration master fixtures.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an::bench
{

/// The fixed multiplier the calibration rows use.
inline constexpr double kCalibrationFixtureDarkScale = 0.875;

/// The scale that carries a captured frame's sixteen-bit codes into the unit interval.
///
/// A power of two, so every code becomes one exactly representable Float32 value and the engine's
/// conversion rules take no part in the light it calibrates.
inline constexpr float kCalibrationFixtureCodeScale = 1.0f / 65536.0f;

/// A captured frame's sixteen-bit codes as the light a real-frame calibration row calibrates.
///
/// @param codes the frame's samples as little-endian sixteen-bit codes, two bytes a sample.
/// @return one unit-interval sample for every code, in the same order.
[[nodiscard]] inline std::vector<float> makeCalibrationFixtureLight(std::span<const std::uint8_t> codes)
{
    std::vector<float> light(codes.size() / 2u);
    for (std::size_t index = 0u; index < light.size(); ++index)
    {
        const auto code = static_cast<std::uint16_t>(codes[2u * index] | (codes[2u * index + 1u] << 8u));
        light[index] = static_cast<float>(code) * kCalibrationFixtureCodeScale;
    }
    return light;
}

/// Deterministic master samples for one calibration scenario.
struct CalibrationFixtureMasters
{
    /// The master bias, one sample for every light sample.
    std::vector<float> bias;

    /// The master dark, one sample for every light sample.
    std::vector<float> dark;

    /// The unnormalized master flat, one sample for every light sample.
    std::vector<float> flat;
};

/// Builds binary-exact, spatially varying masters for the calibration rows.
///
/// Every constant is a power-of-two fraction, so constructing the masters cannot introduce a
/// decimal parsing difference between runners. The flat stays strictly positive.
///
/// @param sampleCount the number of light samples each master must cover.
/// @return three equally sized master buffers.
[[nodiscard]] inline CalibrationFixtureMasters makeCalibrationFixtureMasters(std::size_t sampleCount)
{
    CalibrationFixtureMasters masters{
        .bias = std::vector<float>(sampleCount),
        .dark = std::vector<float>(sampleCount),
        .flat = std::vector<float>(sampleCount),
    };
    for (std::size_t index = 0u; index < sampleCount; ++index)
    {
        masters.bias[index] = 0.0078125f + static_cast<float>((index * 17u) % 64u) * 0.0001220703125f;
        masters.dark[index] = 0.015625f + static_cast<float>((index * 29u) % 128u) * 0.0001220703125f;
        masters.flat[index] = 0.75f + static_cast<float>((index * 43u) % 128u) * 0.001953125f;
    }
    return masters;
}

} // namespace an::bench
