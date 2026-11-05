// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent midtones transfer curve and streaming full-image error checks.
#include "MidtonesTransferStretchOracle.h"

#include "StretchOutputCheck.h"

#include <cmath>

namespace an::bench
{
MidtonesTransferStretchOracle::MidtonesTransferStretchOracle(const StretchSettings& settings)
    : m_midtones(settings.midtones), m_shadows(settings.shadows), m_highlights(settings.highlights)
{
}

long double MidtonesTransferStretchOracle::forward(long double value) const
{
    if (value <= m_shadows)
        return 0.0L;
    if (value >= m_highlights)
        return 1.0L;
    const long double above = value - m_shadows;
    const long double below = m_highlights - value;
    const long double rise = (1.0L - m_midtones) * above;
    return rise / (m_midtones * below + rise);
}

long double MidtonesTransferStretchOracle::inverse(long double value) const
{
    if (value <= 0.0L || m_highlights <= m_shadows)
        return m_shadows;
    if (value >= 1.0L)
        return m_highlights;
    // The inverse of a midtones transfer is the transfer with the complementary balance.
    const long double rise = m_midtones * value;
    return m_shadows + (m_highlights - m_shadows) * rise / (rise + (1.0L - m_midtones) * (1.0L - value));
}

bool requiresMidtonesTransferStretchOracle(const ScenarioSpec& scenario)
{
    const auto function = scenario.parameters.find("function");
    return scenario.group == "stretch" && function != scenario.parameters.end() && function->second == "8";
}

void checkMidtonesTransferStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                        std::span<const float> output, ResultRecord& result)
{
    const MidtonesTransferStretchOracle oracle(settings);
    const StretchCheckTotals totals = checkStretchSamples(oracle, false, input, output);
    result.correctnessMetric = static_cast<double>(totals.maximum);
    auto& counters = result.counters;
    counters["mtfOracleVersion"] = 1.0;
    counters["mtfOracleSamples"] = static_cast<double>(totals.count);
    counters["mtfOracleMaxAbsoluteError"] = static_cast<double>(totals.maximum);
    counters["mtfOracleRmsError"] = totals.count ? static_cast<double>(std::sqrt(totals.squares / totals.count)) : 1.0;
    counters["mtfRangeViolations"] = totals.invalid;
    counters["mtfRangeExcursion"] = totals.rangeExcursion;
    counters["mtfEndpointViolations"] = totals.endpoints;
    counters["mtfOrderingViolations"] = totals.ordering;
}

bool midtonesTransferStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result)
{
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    if (!settings || !requiresMidtonesTransferStretchOracle(scenario))
        return false;
    const auto get = [&](const char* name) {
        const auto found = result.counters.find(name);
        return found == result.counters.end() ? -1.0 : found->second;
    };
    const auto bounded = [&](const char* name, double bound) {
        const double value = get(name);
        return std::isfinite(value) && value >= 0.0 && value <= bound;
    };
    // The numerical tier belongs to the family's contract, not to an editable tolerance.
    return get("mtfOracleVersion") == 1.0 &&
           get("mtfOracleSamples") == static_cast<double>(scenario.input.width) * scenario.input.height &&
           bounded("mtfOracleMaxAbsoluteError", 1.0e-6) && bounded("mtfOracleRmsError", 1.0e-6) &&
           bounded("mtfRangeViolations", 0.0) && bounded("mtfEndpointViolations", 0.0) &&
           // The output is clamped to the interval, so no sample may fall outside it.
           bounded("mtfRangeExcursion", 0.0) && bounded("mtfOrderingViolations", 0.0);
}
} // namespace an::bench
