// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the shared histogram scenarios.

#pragma once

#include "BasicStatisticsOracle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// The result fields the engine exposes for a histogram benchmark.
///
/// The bins themselves are compared through `checksum`, which is order-sensitive, rather than
/// carried in the record: a sixty-five-thousand-bin histogram is the interesting case and copying it
/// into every result record would cost more than the operation being measured.
struct HistogramOracle
{
    /// How many samples entered a bin.
    std::size_t counted = 0u;

    /// How many bins the histogram divided its interval into.
    std::size_t binCount = 0u;

    /// An order-sensitive checksum over the bins, so two histograms agreeing here agree everywhere.
    std::uint64_t checksum = 0u;

    /// The count in the fullest bin, which a display reads to scale its vertical axis.
    std::uint64_t largestBin = 0u;

    /// Which bin that was. The first of several equal ones.
    std::size_t largestBinIndex = 0u;
};

/// The checksum a run computes over its own bins.
///
/// A sixty-four-bit FNV-1a over the counts in bin order. Stated once so that two adapters cannot
/// agree on the bins and disagree on the number that stands for them.
[[nodiscard]] std::uint64_t histogramChecksum(std::span<const std::uint64_t> bins) noexcept;

/// Summarises a bin array a run already holds.
///
/// @param bins the counts, at least one.
/// @return the record, or no value for an empty span.
[[nodiscard]] std::optional<HistogramOracle> summariseHistogram(std::span<const std::uint64_t> bins);

/// Counts sixteen-bit codes into one bin each, which is the full-scale histogram the engine builds.
///
/// Deliberately the slow, obvious method: one increment per sample into a bin indexed by the code
/// itself. The engine reaches that mapping through arithmetic of its own, so an oracle that does
/// no arithmetic is the definition rather than a second implementation of the same idea.
///
/// @param samples the sixteen-bit codes.
/// @param bins how many bins, which must be 65,536 for the mapping to be one code per bin.
/// @return the record, or no value for an empty input or a bin count that is not the code space.
[[nodiscard]] std::optional<HistogramOracle> computeFullScaleHistogramOracle(std::span<const std::uint16_t> samples,
                                                                             std::size_t bins);

/// Counts a rectangular periodic-mask selection of a Float32 frame into a stated closed interval.
///
/// The window is the one `BasicStatisticsOracleWindow` describes, so every masked-region scenario on
/// the statistics rows selects exactly the same samples. Equally sized bins cover the closed
/// interval and the upper endpoint belongs to the last bin; a selected sample outside the interval
/// or not a number enters no bin and is not counted.
///
/// @param samples the frame, which must exactly cover the window's stated dimensions.
/// @param window the rectangle and periodic mask.
/// @param lowest the interval's inclusive lower endpoint.
/// @param highest the interval's inclusive upper endpoint, above `lowest`.
/// @param bins how many bins divide the interval, at least one.
/// @param counts where the bins go, resized to `bins`.
/// @return the record, or no value for an invalid window, span, interval or bin count.
[[nodiscard]] std::optional<HistogramOracle>
computeWindowedHistogramOracle(std::span<const float> samples, const BasicStatisticsOracleWindow& window, double lowest,
                               double highest, std::size_t bins, std::vector<std::uint64_t>& counts);

/// The value at one quantile of a counted distribution, under the interpolated rule.
///
/// The same convention the engine publishes: the rank sought is `quantile * total`, the bracketing
/// bin is the first whose running total reaches it, and the value is interpolated across that bin
/// assuming its samples are spread evenly through it. Written out here rather than shared, because
/// an oracle that calls the thing it checks checks nothing.
///
/// @param counts the bins, at least one, holding at least one sample between them.
/// @param lowest the interval's inclusive lower endpoint.
/// @param highest the interval's inclusive upper endpoint.
/// @param quantile the fraction, within `[0, 1]`.
/// @return the value, or the lower endpoint when no sample entered a bin.
[[nodiscard]] double histogramOracleQuantile(std::span<const std::uint64_t> counts, double lowest, double highest,
                                             double quantile) noexcept;

/// The largest normalized error in `actual` relative to `expected`.
///
/// A histogram is counted rather than estimated, so a difference in any of the compared fields is a
/// wrong answer: this returns zero when every field agrees and the largest finite double when one
/// does not.
[[nodiscard]] double histogramError(const HistogramOracle& actual, const HistogramOracle& expected) noexcept;

/// A canonical full-output representation shared by both runner adapters.
[[nodiscard]] std::string histogramText(const HistogramOracle& histogram);

/// A canonical representation of the percentile values read out of a counted distribution.
///
/// Written beside the histogram text rather than inside it, because the full-scale row reports a
/// histogram and no percentiles and the two digests must not become the same shape by accident.
///
/// @param values the values, in the order their quantiles were asked for.
/// @return the text.
[[nodiscard]] std::string histogramPercentileText(std::span<const double> values);

} // namespace an::bench
