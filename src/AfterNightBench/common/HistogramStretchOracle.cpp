// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent normalized rational primitive and streaming full-image error checks.
#include "HistogramStretchOracle.h"

#include "StretchOutputCheck.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace an::bench
{
HistogramStretchOracle::HistogramStretchOracle(const StretchSettings& settings)
    : m_k(2.0L * settings.amount), m_symmetry(settings.symmetry), m_low(settings.lowProtection),
      m_high(settings.highProtection), m_origin(primitive(0.0L)), m_extent(primitive(1.0L) - m_origin),
      m_lowPrimitive(primitive(m_low)), m_highPrimitive(primitive(m_high)), m_lowSlope(slope(m_low)),
      m_highSlope(slope(m_high))
{
}

long double HistogramStretchOracle::slope(long double value) const
{
    const auto denominator = 1.0L + m_k * std::abs(value - m_symmetry);
    return 1.0L / (denominator * denominator);
}

long double HistogramStretchOracle::primitive(long double value) const
{
    const auto protectedValue = std::clamp(value, m_low, m_high);
    const auto distance = protectedValue - m_symmetry;
    return distance / (1.0L + m_k * std::abs(distance)) + (value - protectedValue) * slope(protectedValue);
}

long double HistogramStretchOracle::forward(long double value) const
{
    return std::clamp((primitive(std::clamp(value, 0.0L, 1.0L)) - m_origin) / m_extent, 0.0L, 1.0L);
}

long double HistogramStretchOracle::inverse(long double value) const
{
    const auto target = m_origin + std::clamp(value, 0.0L, 1.0L) * m_extent;
    long double result;
    if (target < m_lowPrimitive)
        result = m_low + (target - m_lowPrimitive) / m_lowSlope;
    else if (target > m_highPrimitive)
        result = m_high + (target - m_highPrimitive) / m_highSlope;
    else
        result = m_symmetry + target / (1.0L - m_k * std::abs(target));
    return std::clamp(result, 0.0L, 1.0L);
}

long double HistogramStretchOracle::maximumSlope() const
{
    return 1.0L / m_extent;
}

bool requiresHistogramStretchOracle(const ScenarioSpec& scenario)
{
    const auto function = scenario.parameters.find("function");
    return scenario.group == "stretch" && function != scenario.parameters.end() &&
           (function->second == "5" || function->second == "6");
}

void checkHistogramStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                 std::span<const float> output, ResultRecord& result)
{
    const HistogramStretchOracle oracle(settings);
    const StretchCheckTotals totals = checkStretchSamples(oracle, settings.function == 6u, input, output);
    result.correctnessMetric = static_cast<double>(totals.maximum);
    auto& counters = result.counters;
    counters["histogramOracleVersion"] = 1.0;
    counters["histogramOracleSamples"] = static_cast<double>(totals.count);
    counters["histogramOracleMaxAbsoluteError"] = static_cast<double>(totals.maximum);
    counters["histogramOracleRmsError"] =
        totals.count ? static_cast<double>(std::sqrt(totals.squares / totals.count)) : 1.0;
    counters["histogramRangeViolations"] = totals.invalid;
    counters["histogramRangeExcursion"] = totals.rangeExcursion;
    counters["histogramEndpointViolations"] = totals.endpoints;
    counters["histogramOrderingViolations"] = totals.ordering;
    counters["histogramRoundtripError"] = static_cast<double>(totals.roundtrip);
}

bool histogramStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result)
{
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    if (!settings || !requiresHistogramStretchOracle(scenario))
        return false;
    const auto get = [&](const char* name) {
        const auto found = result.counters.find(name);
        return found == result.counters.end() ? -1.0 : found->second;
    };
    const auto bounded = [&](const char* name, double bound) {
        const double value = get(name);
        return std::isfinite(value) && value >= 0.0 && value <= bound;
    };
    // The numerical tier belongs to the histogram contract, not to an editable tolerance.
    const HistogramStretchOracle oracle(*settings);
    return get("histogramOracleVersion") == 1.0 &&
           get("histogramOracleSamples") == static_cast<double>(scenario.input.width) * scenario.input.height &&
           bounded("histogramOracleMaxAbsoluteError", 1.0e-6) && bounded("histogramOracleRmsError", 1.0e-6) &&
           bounded("histogramRangeViolations", 0.0) && bounded("histogramEndpointViolations", 0.0) &&
           // The output is clamped to the interval, so no sample may fall outside it.
           bounded("histogramRangeExcursion", 0.0) && bounded("histogramOrderingViolations", 0.0) &&
           bounded("histogramRoundtripError", static_cast<double>(oracle.maximumSlope()) * 1.0e-6 + 1.0e-14);
}
} // namespace an::bench
