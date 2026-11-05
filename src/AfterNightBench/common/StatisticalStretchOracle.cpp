// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent statistical stretch analysis and curve, and streaming full-image error checks.
#include "StatisticalStretchOracle.h"

#include "StretchOutputCheck.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace an::bench
{
namespace
{
constexpr double kLowerHalfMadToSigma = 1.4826;
constexpr std::size_t kKeyHalfBuckets = 65536u;

/// An unsigned key whose order is the order of finite Float32 values; both zeros share one key.
std::uint32_t orderKey(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value == 0.0f ? 0.0f : value);
    return (bits & 0x80000000u) != 0u ? ~bits : bits | 0x80000000u;
}

float valueOfKey(std::uint32_t key)
{
    return std::bit_cast<float>((key & 0x80000000u) != 0u ? key & 0x7fffffffu : ~key);
}

/// The bucket holding zero-based rank `rank` of the counted order, and the rank inside it.
std::pair<std::uint32_t, std::uint64_t> locate(const std::vector<std::uint64_t>& counts, std::uint64_t rank)
{
    for (std::size_t bucket = 0u; bucket < counts.size(); ++bucket)
    {
        if (rank < counts[bucket])
            return {static_cast<std::uint32_t>(bucket), rank};
        rank -= counts[bucket];
    }
    return {static_cast<std::uint32_t>(counts.size() - 1u), 0u};
}

/// Two ranks of the finite samples' ascending order, found by counting the high and then the low
/// half of each order key: two passes over the image and three fixed tables, whatever its size.
std::pair<float, float> selectRanks(std::span<const float> input, std::uint64_t lower, std::uint64_t upper)
{
    std::vector<std::uint64_t> high(kKeyHalfBuckets);
    for (const float value : input)
        if (std::isfinite(value))
            ++high[orderKey(value) >> 16u];
    const auto [lowerBucket, lowerRank] = locate(high, lower);
    const auto [upperBucket, upperRank] = locate(high, upper);
    std::vector<std::uint64_t> lowerCounts(kKeyHalfBuckets);
    std::vector<std::uint64_t> upperCounts(kKeyHalfBuckets);
    for (const float value : input)
    {
        if (!std::isfinite(value))
            continue;
        const std::uint32_t key = orderKey(value);
        if ((key >> 16u) == lowerBucket)
            ++lowerCounts[key & 0xffffu];
        if ((key >> 16u) == upperBucket)
            ++upperCounts[key & 0xffffu];
    }
    return {valueOfKey(lowerBucket << 16u | locate(lowerCounts, lowerRank).first),
            valueOfKey(upperBucket << 16u | locate(upperCounts, upperRank).first)};
}

/// The lower middle value of an ascending span plus half the distance to the upper one.
double medianOfSorted(std::span<const double> sorted)
{
    const double lower = sorted[(sorted.size() - 1u) / 2u];
    const double upper = sorted[sorted.size() / 2u];
    return lower + 0.5 * (upper - lower);
}

double counter(const ResultRecord& result, const char* name)
{
    const auto found = result.counters.find(name);
    return found == result.counters.end() ? std::numeric_limits<double>::quiet_NaN() : found->second;
}
} // namespace

StatisticalStretchReference measureStatisticalStretch(const StretchSettings& settings, std::span<const float> input)
{
    StatisticalStretchReference result;
    result.minimum = std::numeric_limits<double>::infinity();
    result.maximum = -std::numeric_limits<double>::infinity();
    for (const float value : input)
    {
        if (!std::isfinite(value))
            continue;
        ++result.samples;
        result.minimum = (std::min)(result.minimum, static_cast<double>(value));
        result.maximum = (std::max)(result.maximum, static_cast<double>(value));
    }
    if (result.samples == 0u)
    {
        result.minimum = result.maximum = result.median = result.lowerHalfSigma = result.blackPoint =
            std::numeric_limits<double>::quiet_NaN();
        return result;
    }
    const auto [lower, upper] = selectRanks(input, (result.samples - 1u) / 2u, result.samples / 2u);
    result.median = static_cast<double>(lower) + 0.5 * (static_cast<double>(upper) - static_cast<double>(lower));

    const std::size_t stride =
        (std::max)(std::size_t{1}, static_cast<std::size_t>((input.size() + kStatisticalStretchSampleLimit - 1u) /
                                                            kStatisticalStretchSampleLimit));
    std::vector<double> sample;
    sample.reserve(input.size() / stride + 1u);
    for (std::size_t index = 0u; index < input.size(); index += stride)
        if (std::isfinite(input[index]))
            sample.push_back(input[index]);
    std::sort(sample.begin(), sample.end());
    result.sampledValues = sample.size();
    const double sampleMedian = medianOfSorted(sample);
    const std::span<double> lowerHalf(
        sample.data(),
        static_cast<std::size_t>(std::upper_bound(sample.begin(), sample.end(), sampleMedian) - sample.begin()));
    const double lowerMedian = medianOfSorted(lowerHalf);
    for (double& value : lowerHalf)
        value = std::abs(value - lowerMedian);
    std::sort(lowerHalf.begin(), lowerHalf.end());
    result.lowerHalfSigma = medianOfSorted(lowerHalf) * kLowerHalfMadToSigma;
    const double sigmas = settings.blackpointSigma;
    result.blackPoint =
        settings.noBlackClip
            ? result.minimum
            : (std::min)((std::max)(result.minimum, result.median - sigmas * result.lowerHalfSigma), 0.99);
    return result;
}

StatisticalStretchOracle::StatisticalStretchOracle(const StretchSettings& settings, double median, double blackPoint)
    : m_blackPoint(blackPoint), m_scale((std::max)(1.0L - static_cast<long double>(blackPoint), 1.0e-12L)),
      m_median((static_cast<long double>(median) - blackPoint) / m_scale), m_target(settings.targetMedian),
      m_applyCurves(settings.applyCurves), m_normalize(settings.normalize), m_curveX{}, m_curveY{}
{
    const long double t = m_target;
    const long double exponent = 1.0L - static_cast<long double>(settings.curvesBoost);
    const long double x[6] = {0.0L, 0.5L * t, t, 0.25L * (1.0L - t) + t, 0.75L * (1.0L - t) + t, 1.0L};
    const long double y[6] = {0.0L, 0.5L * t, t, std::pow(x[3], exponent), std::pow(std::pow(x[4], exponent), exponent),
                              1.0L};
    std::copy(x, x + 6, m_curveX);
    std::copy(y, y + 6, m_curveY);
}

long double StatisticalStretchOracle::transfer(long double value) const
{
    const long double r = std::clamp((value - m_blackPoint) / m_scale, 0.0L, 1.0L);
    const long double m = m_median;
    const long double t = m_target;
    const long double rise = (1.0L - m) * t * r;
    const long double denominator = m * (1.0L - t) + r * (t - m);
    long double result = std::abs(denominator) < 1.0e-12L ? -rise / 1.0e-12L : rise / denominator;
    if (!m_applyCurves)
        return result;
    if (result <= m_curveX[0])
        return m_curveY[0];
    for (std::size_t index = 0u; index + 1u < 6u; ++index)
        if (result < m_curveX[index + 1u])
            return m_curveY[index] + (result - m_curveX[index]) / (m_curveX[index + 1u] - m_curveX[index]) *
                                         (m_curveY[index + 1u] - m_curveY[index]);
    return m_curveY[5];
}

void StatisticalStretchOracle::measureDivisor(std::span<const float> input)
{
    m_divisor = 1.0L;
    if (!m_normalize)
        return;
    const std::size_t chunks = (input.size() + kStretchCheckChunkSamples - 1u) / kStretchCheckChunkSamples;
    std::vector<long double> largest(chunks, -std::numeric_limits<long double>::infinity());
    forEachStretchCheckChunk(input.size(), [&](std::size_t index, std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i)
            if (std::isfinite(input[i]))
                largest[index] = (std::max)(largest[index], transfer(input[i]));
    });
    const long double maximum = largest.empty() ? 0.0L : *std::max_element(largest.begin(), largest.end());
    if (maximum > 0.0L)
        m_divisor = maximum;
}

long double StatisticalStretchOracle::forward(long double value) const
{
    return std::clamp(transfer(value) / m_divisor, 0.0L, 1.0L);
}

long double StatisticalStretchOracle::inverse(long double) const
{
    return std::numeric_limits<long double>::quiet_NaN();
}

bool requiresStatisticalStretchOracle(const ScenarioSpec& scenario)
{
    const auto function = scenario.parameters.find("function");
    return scenario.group == "stretch" && function != scenario.parameters.end() && function->second == "9";
}

void checkStatisticalStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                   std::span<const float> output, ResultRecord& result)
{
    const StatisticalStretchReference reference = measureStatisticalStretch(settings, input);
    StatisticalStretchOracle oracle(settings, reference.median, reference.blackPoint);
    oracle.measureDivisor(input);
    const StretchCheckTotals totals = checkStretchSamples(oracle, false, input, output);
    result.correctnessMetric = static_cast<double>(totals.maximum);
    auto& counters = result.counters;
    counters["statisticalOracleVersion"] = 1.0;
    counters["statisticalOracleSamples"] = static_cast<double>(totals.count);
    counters["statisticalOracleMaxAbsoluteError"] = static_cast<double>(totals.maximum);
    counters["statisticalOracleRmsError"] =
        totals.count ? static_cast<double>(std::sqrt(totals.squares / totals.count)) : 1.0;
    counters["statisticalRangeViolations"] = totals.invalid;
    counters["statisticalRangeExcursion"] = totals.rangeExcursion;
    counters["statisticalEndpointViolations"] = totals.endpoints;
    counters["statisticalOrderingViolations"] = totals.ordering;
    counters["statisticalReferenceMinimum"] = reference.minimum;
    counters["statisticalReferenceMedian"] = reference.median;
    counters["statisticalReferenceLowerHalfSigma"] = reference.lowerHalfSigma;
    counters["statisticalReferenceSampledValues"] = static_cast<double>(reference.sampledValues);
    counters["statisticalReferenceBlackPoint"] = reference.blackPoint;
    counters["statisticalNormalizationDivisor"] = static_cast<double>(oracle.divisor());
}

bool statisticalStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result)
{
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    if (!settings || !requiresStatisticalStretchOracle(scenario))
        return false;
    const auto bounded = [&](const char* name, double bound) {
        const double value = counter(result, name);
        return std::isfinite(value) && value >= 0.0 && value <= bound;
    };
    const auto present = [&](const char* name) { return std::isfinite(counter(result, name)); };
    for (const char* name :
         {"statisticalReferenceMinimum", "statisticalReferenceMedian", "statisticalReferenceLowerHalfSigma",
          "statisticalReferenceSampledValues", "statisticalReferenceBlackPoint", "statisticalNormalizationDivisor"})
        if (!present(name))
            return false;
    // The numerical tier belongs to the family's contract, not to an editable tolerance.
    return counter(result, "statisticalOracleVersion") == 1.0 &&
           counter(result, "statisticalOracleSamples") ==
               static_cast<double>(scenario.input.width) * scenario.input.height &&
           bounded("statisticalOracleMaxAbsoluteError", 1.0e-6) && bounded("statisticalOracleRmsError", 1.0e-6) &&
           bounded("statisticalRangeViolations", 0.0) && bounded("statisticalEndpointViolations", 0.0) &&
           // The output is clamped to the interval, so no sample may fall outside it.
           bounded("statisticalRangeExcursion", 0.0) && bounded("statisticalOrderingViolations", 0.0);
}
} // namespace an::bench
