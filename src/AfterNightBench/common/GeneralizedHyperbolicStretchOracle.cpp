// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Independent generalized-hyperbolic curves per bias regime and streaming full-image error checks.
#include "GeneralizedHyperbolicStretchOracle.h"

#include "StretchOutputCheck.h"

#include <algorithm>
#include <cmath>

namespace an::bench
{
GeneralizedHyperbolicStretchOracle::GeneralizedHyperbolicStretchOracle(const StretchSettings& settings)
    : m_amount(settings.amount), m_bias(settings.bias), m_symmetry(settings.symmetry), m_low(settings.lowProtection),
      m_high(settings.highProtection), m_lowCurve(curve(m_symmetry - m_low)), m_highCurve(curve(m_high - m_symmetry)),
      m_lowSlope(slope(m_symmetry - m_low)), m_highSlope(slope(m_high - m_symmetry)),
      m_lowTail(tail(m_symmetry - m_low)), m_highTail(tail(m_high - m_symmetry)),
      m_lowRise(m_low * m_lowSlope + m_lowCurve), m_highRise((1.0L - m_high) * m_highSlope + m_highCurve),
      m_extent(m_lowRise + m_highRise)
{
}

long double GeneralizedHyperbolicStretchOracle::curve(long double u) const
{
    const long double d = m_amount;
    const long double b = m_bias;
    if (d == 0.0L || u == 0.0L)
        return u;
    if (b == -1.0L)
        return std::log1p(d * u) / d;
    if (b < 0.0L)
        return -std::expm1((1.0L + b) / b * std::log1p(-b * d * u)) / ((1.0L + b) * d);
    if (b == 0.0L)
        return -std::expm1(-d * u) / d;
    return -std::expm1(-std::log1p(b * d * u) / b) / d;
}

long double GeneralizedHyperbolicStretchOracle::slope(long double u) const
{
    const long double d = m_amount;
    const long double b = m_bias;
    if (d == 0.0L)
        return 1.0L;
    if (b == -1.0L)
        return 1.0L / (1.0L + d * u);
    if (b < 0.0L)
        return std::exp(std::log1p(-b * d * u) / b);
    if (b == 0.0L)
        return std::exp(-d * u);
    return std::exp(-(1.0L + b) / b * std::log1p(b * d * u));
}

long double GeneralizedHyperbolicStretchOracle::tail(long double u) const
{
    const long double d = m_amount;
    const long double b = m_bias;
    if (b == 0.0L)
        return std::exp(-d * u);
    if (b > 0.0L)
        return std::exp(-std::log1p(b * d * u) / b);
    return std::exp((1.0L + b) / b * std::log1p(-b * d * u));
}

long double GeneralizedHyperbolicStretchOracle::distance(long double drop, long double limit, long double limitCurve,
                                                         long double limitTail) const
{
    const long double d = m_amount;
    const long double b = m_bias;
    const long double r = (std::max)(drop, 0.0L);
    // Near its supremum the curve is flat, and the value it reaches is the supremum less a rise
    // too small to survive a subtraction; the remaining rise itself is exact to its own precision.
    // Inverting it is the better conditioned form exactly when it is below one half.
    const long double t = limitTail + (b < 0.0L ? 1.0L + b : 1.0L) * d * r;
    const long double s = (std::max)(limitCurve - r, 0.0L);
    long double u;
    if (d == 0.0L || s == 0.0L)
        u = s;
    else if (t < 0.5L)
        u = b == 0.0L  ? -std::log(t) / d
            : b > 0.0L ? std::expm1(-b * std::log(t)) / (b * d)
                       : std::expm1(b / (1.0L + b) * std::log(t)) / (-b * d);
    else if (b == -1.0L)
        u = std::expm1(d * s) / d;
    // The curve's supremum can round onto the value, which is then reached at the limit.
    else if (b < 0.0L)
        u = std::expm1(b / (1.0L + b) * std::log1p(-(std::min)((1.0L + b) * d * s, 1.0L))) / (-b * d);
    else if (b == 0.0L)
        u = -std::log1p(-(std::min)(d * s, 1.0L)) / d;
    else
        u = std::expm1(-b * std::log1p(-(std::min)(d * s, 1.0L))) / (b * d);
    return std::isnan(u) ? limit : (std::min)(u, limit);
}

long double GeneralizedHyperbolicStretchOracle::forward(long double value) const
{
    const auto x = std::clamp(value, 0.0L, 1.0L);
    if (m_amount == 0.0L)
        return x;
    long double result;
    // Each piece is written from its own end of the unit interval, so it meets its neighbour at a
    // protection point exactly and never cancels a small rise against the whole extent.
    if (x < m_low)
        result = x * m_lowSlope / m_extent;
    else if (x < m_symmetry)
        result = (m_low * m_lowSlope + (m_lowCurve - curve(m_symmetry - x))) / m_extent;
    else if (x <= m_high)
        result = 1.0L - ((1.0L - m_high) * m_highSlope + (m_highCurve - curve(x - m_symmetry))) / m_extent;
    else
        result = 1.0L - (1.0L - x) * m_highSlope / m_extent;
    return std::clamp(result, 0.0L, 1.0L);
}

long double GeneralizedHyperbolicStretchOracle::inverse(long double value) const
{
    if (value <= 0.0L)
        return 0.0L;
    if (value >= 1.0L)
        return 1.0L;
    if (m_amount == 0.0L)
        return value;
    const auto below = value * m_extent;
    const auto above = (1.0L - value) * m_extent;
    long double result;
    if (below < m_low * m_lowSlope)
        result = below / m_lowSlope;
    else if (above < (1.0L - m_high) * m_highSlope)
        result = 1.0L - above / m_highSlope;
    else if (below < m_lowRise)
        result = m_symmetry - distance(below - m_low * m_lowSlope, m_symmetry - m_low, m_lowCurve, m_lowTail);
    else
        result =
            m_symmetry + distance(above - (1.0L - m_high) * m_highSlope, m_high - m_symmetry, m_highCurve, m_highTail);
    return std::clamp(result, 0.0L, 1.0L);
}

long double GeneralizedHyperbolicStretchOracle::maximumSlope() const
{
    return 1.0L / m_extent;
}

bool requiresGeneralizedHyperbolicStretchOracle(const ScenarioSpec& scenario)
{
    const auto function = scenario.parameters.find("function");
    return scenario.group == "stretch" && function != scenario.parameters.end() &&
           (function->second == "0" || function->second == "1");
}

void checkGeneralizedHyperbolicStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                             std::span<const float> output, ResultRecord& result)
{
    const GeneralizedHyperbolicStretchOracle oracle(settings);
    const StretchCheckTotals totals = checkStretchSamples(oracle, settings.function == 1u, input, output);
    result.correctnessMetric = static_cast<double>(totals.maximum);
    auto& counters = result.counters;
    counters["ghsOracleVersion"] = 1.0;
    counters["ghsOracleSamples"] = static_cast<double>(totals.count);
    counters["ghsOracleMaxAbsoluteError"] = static_cast<double>(totals.maximum);
    counters["ghsOracleRmsError"] = totals.count ? static_cast<double>(std::sqrt(totals.squares / totals.count)) : 1.0;
    counters["ghsRangeViolations"] = totals.invalid;
    counters["ghsRangeExcursion"] = totals.rangeExcursion;
    counters["ghsEndpointViolations"] = totals.endpoints;
    counters["ghsOrderingViolations"] = totals.ordering;
    counters["ghsRoundtripError"] = static_cast<double>(totals.roundtrip);
}

bool generalizedHyperbolicStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result)
{
    std::string error;
    const auto settings = parseStretchSettings(scenario, error);
    if (!settings || !requiresGeneralizedHyperbolicStretchOracle(scenario))
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
    const GeneralizedHyperbolicStretchOracle oracle(*settings);
    return get("ghsOracleVersion") == 1.0 &&
           get("ghsOracleSamples") == static_cast<double>(scenario.input.width) * scenario.input.height &&
           bounded("ghsOracleMaxAbsoluteError", 1.0e-6) && bounded("ghsOracleRmsError", 1.0e-6) &&
           bounded("ghsRangeViolations", 0.0) && bounded("ghsEndpointViolations", 0.0) &&
           // The output is clamped to the interval, so no sample may fall outside it.
           bounded("ghsRangeExcursion", 0.0) && bounded("ghsOrderingViolations", 0.0) &&
           bounded("ghsRoundtripError", static_cast<double>(oracle.maximumSlope()) * 1.0e-6 + 1.0e-14);
}
} // namespace an::bench
