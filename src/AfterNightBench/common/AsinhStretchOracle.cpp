// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent normalized hyperbolic primitive and streaming full-image error checks.
#include "AsinhStretchOracle.h"

#include "StretchOutputCheck.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace an::bench
{
AsinhStretchOracle::AsinhStretchOracle(const StretchSettings& settings)
    : m_amount(settings.amount), m_symmetry(settings.symmetry), m_low(settings.lowProtection),
      m_high(settings.highProtection), m_origin(primitive(0.0L)), m_extent(primitive(1.0L) - m_origin),
      m_lowPrimitive(primitive(m_low)), m_highPrimitive(primitive(m_high)), m_lowSlope(slope(m_low)),
      m_highSlope(slope(m_high))
{
}

long double AsinhStretchOracle::slope(long double value) const
{
    return 1.0L / std::hypot(1.0L, m_amount * (value - m_symmetry));
}

long double AsinhStretchOracle::primitive(long double value) const
{
    const auto protectedValue = std::clamp(value, m_low, m_high);
    const auto distance = protectedValue - m_symmetry;
    // Dividing out amount makes the zero-amount limit nonsingular and avoids
    // cancellation between tiny normalized endpoint values.
    const auto curved = m_amount == 0.0L ? distance : std::asinh(m_amount * distance) / m_amount;
    return curved + (value - protectedValue) * slope(protectedValue);
}

long double AsinhStretchOracle::forward(long double value) const
{
    return std::clamp((primitive(std::clamp(value, 0.0L, 1.0L)) - m_origin) / m_extent, 0.0L, 1.0L);
}

long double AsinhStretchOracle::inverse(long double value) const
{
    if (value <= 0.0L)
        return 0.0L;
    if (value >= 1.0L)
        return 1.0L;
    const auto target = m_origin + std::clamp(value, 0.0L, 1.0L) * m_extent;
    long double result;
    if (target < m_lowPrimitive)
        result = m_low + (target - m_lowPrimitive) / m_lowSlope;
    else if (target > m_highPrimitive)
        result = m_high + (target - m_highPrimitive) / m_highSlope;
    else
        result = m_symmetry + (m_amount == 0.0L ? target : std::sinh(m_amount * target) / m_amount);
    return std::clamp(result, 0.0L, 1.0L);
}

long double AsinhStretchOracle::maximumSlope() const
{
    return 1.0L / m_extent;
}

bool requiresAsinhStretchOracle(const ScenarioSpec& scenario)
{
    const auto function = scenario.parameters.find("function");
    return scenario.group == "stretch" && function != scenario.parameters.end() &&
           (function->second == "2" || function->second == "3");
}

void checkAsinhStretchOutput(const StretchSettings& settings, std::span<const float> input,
                             std::span<const float> output, ResultRecord& result)
{
    const AsinhStretchOracle oracle(settings);
    const StretchCheckTotals totals = checkStretchSamples(oracle, settings.function == 3u, input, output);
    result.correctnessMetric = static_cast<double>(totals.maximum);
    auto& counters = result.counters;
    counters["asinhOracleVersion"] = 1.0;
    counters["asinhOracleSamples"] = static_cast<double>(totals.count);
    counters["asinhOracleMaxAbsoluteError"] = static_cast<double>(totals.maximum);
    counters["asinhOracleRmsError"] =
        totals.count ? static_cast<double>(std::sqrt(totals.squares / totals.count)) : 1.0;
    counters["asinhRangeViolations"] = totals.invalid;
    counters["asinhRangeExcursion"] = totals.rangeExcursion;
    counters["asinhEndpointViolations"] = totals.endpoints;
    counters["asinhOrderingViolations"] = totals.ordering;
    counters["asinhRoundtripError"] = static_cast<double>(totals.roundtrip);
}

bool asinhStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result)
{
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    if (!settings || !requiresAsinhStretchOracle(scenario))
        return false;
    const auto get = [&](const char* name) {
        const auto found = result.counters.find(name);
        return found == result.counters.end() ? -1.0 : found->second;
    };
    const auto bounded = [&](const char* name, double bound) {
        const double value = get(name);
        return std::isfinite(value) && value >= 0.0 && value <= bound;
    };
    // The numerical tier belongs to the asinh contract, not to an editable tolerance.
    const AsinhStretchOracle oracle(*settings);
    return get("asinhOracleVersion") == 1.0 &&
           get("asinhOracleSamples") == static_cast<double>(scenario.input.width) * scenario.input.height &&
           bounded("asinhOracleMaxAbsoluteError", 1.0e-6) && bounded("asinhOracleRmsError", 1.0e-6) &&
           bounded("asinhRangeViolations", 0.0) && bounded("asinhEndpointViolations", 0.0) &&
           // The output is clamped to the interval, so no sample may fall outside it.
           bounded("asinhRangeExcursion", 0.0) && bounded("asinhOrderingViolations", 0.0) &&
           bounded("asinhRoundtripError", static_cast<double>(oracle.maximumSlope()) * 1.0e-6 + 1.0e-14);
}
} // namespace an::bench
