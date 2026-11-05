// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Shared deterministic frame measurements for the photometric-weighting comparison.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an::bench
{

/// Engine-neutral measurements for one candidate stack frame.
struct PhotometricWeightingFixtureSample
{
    /// Photometric zero point in magnitudes, or no measurement.
    std::optional<double> zeroPointMagnitudes{};

    /// One-sigma uncertainty of the zero point in magnitudes, or no measurement.
    std::optional<double> zeroPointStandardUncertaintyMagnitudes{};

    /// Registration residual root mean square in pixels, or no measurement.
    std::optional<double> astrometricRootMeanSquarePixels{};
};

/// Derives frame-level measurements from a benchmark fixture.
///
/// Using the fixture samples as the entropy source makes its recorded digest prove the identity of
/// the measurements too. The index schedule deliberately includes missing zero points, missing
/// uncertainties and missing registration residuals. At least one measured uncertainty is absent,
/// so this scenario measures the model without uncertainty propagation; the engine's direct suites
/// measure and verify that propagation.
///
/// @param source finite normalized samples, one per candidate frame.
/// @return one deterministic measurement per sample in the same order.
[[nodiscard]] inline std::vector<PhotometricWeightingFixtureSample>
makePhotometricWeightingFixture(std::span<const float> source)
{
    std::vector<PhotometricWeightingFixtureSample> result(source.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (index % 17u == 0u)
        {
            continue;
        }
        const double value = std::abs(static_cast<double>(source[index]));
        PhotometricWeightingFixtureSample& sample = result[index];
        sample.zeroPointMagnitudes = 23.5 + std::fmod(value + static_cast<double>(index % 211u) * 0.001, 0.75);
        if (index % 19u != 0u)
        {
            sample.zeroPointStandardUncertaintyMagnitudes =
                0.008 + std::fmod(value * 0.037 + static_cast<double>(index % 13u) * 0.001, 0.05);
        }
        if (index % 7u != 0u)
        {
            sample.astrometricRootMeanSquarePixels =
                0.6 + std::fmod(value * 1.3 + static_cast<double>(index % 29u) * 0.017, 1.8);
        }
    }
    return result;
}

} // namespace an::bench
