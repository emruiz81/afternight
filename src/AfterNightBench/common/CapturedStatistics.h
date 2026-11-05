// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral captured frames and references for the statistics rows.

#pragma once

#include "BasicStatisticsOracle.h"
#include "OrderStatisticsOracle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// Carries a little-endian sixteen-bit capture into the unit interval the way the captured statistics rows do.
///
/// Each code becomes `code / 65535`, rounded once to Float32: the normalization a sixteen-bit frame
/// conventionally receives when it is read as floating point, so the rows measure the samples a caller
/// would hand the engine. The quotient is inexact, which is why the runner calls this rather than
/// leaving the value to the engine's conversion rules. A byte count that is not a whole number of codes
/// yields nothing.
[[nodiscard]] std::vector<float> makeUnitIntervalCapture(std::span<const std::uint8_t> codes);

/// Computes the exact median and median absolute deviation of `makeUnitIntervalCapture(codes)` by counting codes.
///
/// The conversion is strictly increasing, so ranking the codes ranks the samples; the answer is the one
/// `computeOrderStatisticsOracle` gives over the converted samples, to the bit, without sorting tens of
/// millions of them in every process a qualification launches. The deviations are ranked the same way, over
/// the distinct values and their counts.
///
/// Returns no value for an empty capture or one whose byte count is not a whole number of codes.
[[nodiscard]] std::optional<OrderStatisticsOracle>
computeCapturedOrderStatisticsOracle(std::span<const std::uint8_t> codes);

/// The fourteen estimators the complete statistics of one channel report.
///
/// The count, extrema, mean and variance are scored against `computeBasicStatisticsOracle`; the median
/// and deviation are scored against the exact order reference. The remaining seven are the engine's own
/// definitions, so they are carried in the output's digest, where a change shows, and not scored against
/// a reference.
struct CompleteStatistics
{
    std::size_t count = 0u;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double populationVariance = 0.0;
    double median = 0.0;
    double medianAbsoluteDeviation = 0.0;
    double averageAbsoluteDeviation = 0.0;
    double sumOfSquares = 0.0;
    double meanOfSquares = 0.0;
    double biweightMidvariance = 0.0;
    double backgroundNoise = 0.0;
    double location = 0.0;
    double scale = 0.0;
};

/// The larger of `basicStatisticsError` and `orderStatisticsError` over the fields each reference defines.
[[nodiscard]] double completeStatisticsError(const CompleteStatistics& actual, const BasicStatisticsOracle& basic,
                                             const OrderStatisticsOracle& order) noexcept;

/// A canonical full-output representation of all fourteen estimators, shared by both runner adapters.
[[nodiscard]] std::string completeStatisticsText(const CompleteStatistics& statistics);

} // namespace an::bench
