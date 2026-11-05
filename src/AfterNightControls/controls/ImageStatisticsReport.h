// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The figures the statistics window shows for each channel of an image, computed through the engine's basic and
/// advanced statistics over the samples the image's validity admits.
///
/// Every figure is read through the representation's display range (`widgets/DisplayNormalization.h`), as the window
/// always showed it: a location as a fraction of the range, a spread as a share of it, and a variance as a share of its
/// square. A scale multiplies them back: a location and a spread by the scale's full scale and a variance by its
/// square.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace an::ui
{

/// One channel's figures, each a fraction of the representation's display range.
struct ImageStatisticsChannel
{
    /// How many samples the figures are over: those the image's validity admits, less the clipped ones when they were
    /// left out.
    std::uint64_t count = 0u;

    /// How many pixels the image has.
    std::uint64_t pixels = 0u;

    /// The arithmetic mean.
    double mean = 0.0;

    /// The median: the mean of the two middle samples of an even count.
    double median = 0.0;

    /// The smallest sample.
    double minimum = 0.0;

    /// The largest sample.
    double maximum = 0.0;

    /// The population standard deviation, which the window shows as both its sigma and its standard deviation.
    double standardDeviation = 0.0;

    /// The mean absolute distance from the mean.
    double averageDeviation = 0.0;

    /// The biweight midvariance about the median.
    double biweightMidvariance = 0.0;

    /// The population variance.
    double variance = 0.0;

    /// The median absolute deviation from the median, unscaled.
    double medianAbsoluteDeviation = 0.0;

    /// The background noise taken from each row's first differences.
    double backgroundNoise = 0.0;
};

/// Every channel's figures, in the image's channel order.
struct ImageStatisticsReport
{
    /// Each channel's figures.
    std::vector<ImageStatisticsChannel> channels;
};

/// The scale the window shows the figures at.
enum class StatisticsScale : std::uint8_t
{
    /// Fractions of the range.
    Normalized,

    /// Eight-bit codes.
    Bits8,

    /// Sixteen-bit codes.
    Bits16,

    /// Thirty-two-bit codes.
    Bits32
};

/// How many rows of figures the window shows after the two counts.
inline constexpr std::size_t kStatisticsFigureRows = 11u;

/// One channel's figures at a scale, in the window's row order: mean, sigma, median, minimum, maximum, standard
/// deviation, average deviation, biweight midvariance, variance, median absolute deviation and background noise.
///
/// @param channel the channel's figures.
/// @param scale the scale.
/// @return the figures as the window shows them.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::array<double, kStatisticsFigureRows>
scaledStatisticsFigures(const ImageStatisticsChannel& channel, StatisticsScale scale) noexcept;

/// Computes the figures of an image read under its lease.
///
/// @param scope the admitted operation the statistics run in.
/// @param image the image.
/// @param excludeClipped whether the samples at either end of the range - at or below zero and at or above one - are
///        left out as well as those the image's validity leaves out.
/// @return the figures; a channel whose selection admits nothing has a count of zero and zero figures;
///         `ErrorCode::NotInitialized` for an image that holds nothing; or what the engine refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<ImageStatisticsReport>
computeImageStatisticsReport(const an::consumer::OperationScope& scope, const an::consumer::AssetReadLease& image,
                             bool excludeClipped);

} // namespace an::ui
