// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared median and MAD scenario.

#pragma once

#include "BasicStatisticsOracle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace an::bench
{

/// The result fields the engine exposes for the order-statistics benchmark.
struct OrderStatisticsOracle
{
    std::size_t count = 0u;
    double median = 0.0;
    double medianAbsoluteDeviation = 0.0;
};

/// Computes the benchmark's independent exact reference by sorting a copy.
///
/// Deliberately the slow, obvious method: the point of the scenario is that the engine produces a
/// median without one, so the oracle is the definition rather than an implementation the engine
/// might share a mistake with. The quantile at one half uses the position `0.5 * (count - 1)` and
/// interpolates linearly between the two bracketing ranks; the deviation is the same quantile of
/// the absolute deviations from it.
///
/// Returns no value for an empty input or a non-finite sample, because neither is a valid fixture
/// for the scenario that uses this oracle.
[[nodiscard]] std::optional<OrderStatisticsOracle> computeOrderStatisticsOracle(std::span<const float> samples);

/// Computes the same exact reference over a rectangular periodic-mask selection.
///
/// The window is the one `BasicStatisticsOracleWindow` describes, so the masked-region scenarios of
/// both statistics families select exactly the same samples and cannot drift apart.
///
/// Returns no value when the window is invalid, the source span does not exactly cover its stated
/// dimensions, the mask selects nothing, or a selected sample is non-finite.
[[nodiscard]] std::optional<OrderStatisticsOracle>
computeOrderStatisticsOracle(std::span<const float> samples, const BasicStatisticsOracleWindow& window);

/// The result fields the sigma-clipped estimator scenario compares.
struct SigmaClippedOracle
{
    std::size_t retained = 0u;
    double location = 0.0;
    double scale = 0.0;
    double mean = 0.0;
    double standardDeviation = 0.0;
};

/// Computes the benchmark's independent reference for the clipped estimator by sorting a copy.
///
/// It repeats the engine's stated rule rather than an approximation of it: the interval is the
/// current median plus and minus the requested number of scales, a scale is the median absolute
/// deviation times the normal-distribution constant, the interval is clamped to the extremes of the
/// whole selection, and the iteration stops when the retained count stops moving. The reported mean
/// and standard deviation are those of the samples the final interval retained.
///
/// Returns no value for an empty input, a non-finite sample, or an interval that retains nothing.
[[nodiscard]] std::optional<SigmaClippedOracle> computeSigmaClippedOracle(std::span<const float> samples,
                                                                          double lowSigma, double highSigma,
                                                                          std::uint32_t maximumIterations);

/// The largest normalized error in `actual` relative to `expected`.
///
/// Values whose magnitude is below one use absolute error; larger values use relative error. A
/// count mismatch or non-finite field returns the largest finite double so a result remains
/// schema-valid and fails its declared tolerance.
[[nodiscard]] double orderStatisticsError(const OrderStatisticsOracle& actual,
                                          const OrderStatisticsOracle& expected) noexcept;

/// The largest normalized error in one clipped result relative to another. See
/// `orderStatisticsError` for the normalization.
[[nodiscard]] double sigmaClippedError(const SigmaClippedOracle& actual, const SigmaClippedOracle& expected) noexcept;

/// A canonical full-output representation of a clipped result, shared by both runner adapters.
[[nodiscard]] std::string sigmaClippedText(const SigmaClippedOracle& statistics);

/// A canonical full-output representation shared by both runner adapters.
[[nodiscard]] std::string orderStatisticsText(const OrderStatisticsOracle& statistics);

} // namespace an::bench
