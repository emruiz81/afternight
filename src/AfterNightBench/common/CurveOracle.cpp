// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CurveOracle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace an::bench
{
namespace
{

/// The secant of segment `i`.
[[nodiscard]] long double secantOf(const std::vector<long double>& inputs, const std::vector<long double>& outputs,
                                   std::size_t i) noexcept
{
    return (outputs[i + 1u] - outputs[i]) / (inputs[i + 1u] - inputs[i]);
}

/// One with the sign of `value`, zero counting as positive, as Steffen's paper writes the sign function.
[[nodiscard]] long double signOf(long double value) noexcept
{
    return value < 0.0L ? -1.0L : 1.0L;
}

} // namespace

bool curveRuleNamed(const std::string& name, CurveRule& rule) noexcept
{
    if (name == "linear")
    {
        rule = CurveRule::Linear;
    }
    else if (name == "natural-cubic")
    {
        rule = CurveRule::NaturalCubic;
    }
    else if (name == "akima")
    {
        rule = CurveRule::Akima;
    }
    else if (name == "steffen")
    {
        rule = CurveRule::Steffen;
    }
    else
    {
        return false;
    }
    return true;
}

CurveOracle::CurveOracle(std::span<const CurvePoint> points, CurveRule rule)
{
    const std::size_t count = points.size();
    if (count < (rule == CurveRule::Linear ? 2u : 3u))
    {
        return;
    }
    for (std::size_t i = 0u; i < count; ++i)
    {
        if (!std::isfinite(points[i].input) || !std::isfinite(points[i].output) ||
            (i != 0u && !(points[i].input > points[i - 1u].input)))
        {
            return;
        }
        m_inputs.push_back(static_cast<long double>(points[i].input));
        m_outputs.push_back(static_cast<long double>(points[i].output));
    }
    const std::size_t segments = count - 1u;
    m_first.assign(segments, 0.0L);
    m_second.assign(segments, 0.0L);
    m_third.assign(segments, 0.0L);

    switch (rule)
    {
    case CurveRule::Linear:
        for (std::size_t i = 0u; i < segments; ++i)
        {
            m_first[i] = secantOf(m_inputs, m_outputs, i);
        }
        break;

    case CurveRule::NaturalCubic: {
        // The second derivatives over two, zero at both ends, from the symmetric tridiagonal system, solved by
        // elimination.
        std::vector<long double> halfSecond(count, 0.0L);
        const std::size_t unknowns = count - 2u;
        std::vector<long double> diagonal(unknowns, 0.0L);
        std::vector<long double> right(unknowns, 0.0L);
        for (std::size_t k = 0u; k < unknowns; ++k)
        {
            const long double before = m_inputs[k + 1u] - m_inputs[k];
            const long double after = m_inputs[k + 2u] - m_inputs[k + 1u];
            diagonal[k] = 2.0L * (before + after);
            right[k] = 3.0L * (secantOf(m_inputs, m_outputs, k + 1u) - secantOf(m_inputs, m_outputs, k));
        }
        for (std::size_t k = 1u; k < unknowns; ++k)
        {
            const long double coupling = m_inputs[k + 1u] - m_inputs[k];
            const long double factor = coupling / diagonal[k - 1u];
            diagonal[k] -= factor * coupling;
            right[k] -= factor * right[k - 1u];
        }
        for (std::size_t k = unknowns; k-- > 0u;)
        {
            const long double coupling = k + 1u < unknowns ? m_inputs[k + 2u] - m_inputs[k + 1u] : 0.0L;
            const long double next = k + 1u < unknowns ? halfSecond[k + 2u] : 0.0L;
            halfSecond[k + 1u] = (right[k] - coupling * next) / diagonal[k];
        }
        for (std::size_t i = 0u; i < segments; ++i)
        {
            const long double width = m_inputs[i + 1u] - m_inputs[i];
            m_first[i] = secantOf(m_inputs, m_outputs, i) - width * (halfSecond[i + 1u] + 2.0L * halfSecond[i]) / 3.0L;
            m_second[i] = halfSecond[i];
            m_third[i] = (halfSecond[i + 1u] - halfSecond[i]) / (3.0L * width);
        }
        break;
    }

    case CurveRule::Akima: {
        // The secants, two more at each end extended linearly from the first and last two: `slopes[i + 2]` is the
        // secant of segment `i`.
        std::vector<long double> slopes(segments + 4u, 0.0L);
        for (std::size_t i = 0u; i < segments; ++i)
        {
            slopes[i + 2u] = secantOf(m_inputs, m_outputs, i);
        }
        slopes[1u] = 2.0L * slopes[2u] - slopes[3u];
        slopes[0u] = 3.0L * slopes[2u] - 2.0L * slopes[3u];
        slopes[segments + 2u] = 2.0L * slopes[segments + 1u] - slopes[segments];
        slopes[segments + 3u] = 3.0L * slopes[segments + 1u] - 2.0L * slopes[segments];
        const auto m = [&slopes](std::size_t i, int offset) {
            return slopes[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) + 2 + offset)];
        };
        for (std::size_t i = 0u; i < segments; ++i)
        {
            const long double weights = std::fabs(m(i, 1) - m(i, 0)) + std::fabs(m(i, -1) - m(i, -2));
            if (weights == 0.0L)
            {
                m_first[i] = m(i, 0);
                continue;
            }
            const long double width = m_inputs[i + 1u] - m_inputs[i];
            const long double nextWeights = std::fabs(m(i, 2) - m(i, 1)) + std::fabs(m(i, 0) - m(i, -1));
            const long double alpha = std::fabs(m(i, -1) - m(i, -2)) / weights;
            long double endSlope = m(i, 0);
            if (nextWeights != 0.0L)
            {
                const long double nextAlpha = std::fabs(m(i, 0) - m(i, -1)) / nextWeights;
                endSlope = (1.0L - nextAlpha) * m(i, 0) + nextAlpha * m(i, 1);
            }
            const long double startSlope = (1.0L - alpha) * m(i, -1) + alpha * m(i, 0);
            m_first[i] = startSlope;
            m_second[i] = (3.0L * m(i, 0) - 2.0L * startSlope - endSlope) / width;
            m_third[i] = (startSlope + endSlope - 2.0L * m(i, 0)) / (width * width);
        }
        break;
    }

    case CurveRule::Steffen: {
        std::vector<long double> slopes(count, 0.0L);
        slopes.front() = secantOf(m_inputs, m_outputs, 0u);
        slopes.back() = secantOf(m_inputs, m_outputs, segments - 1u);
        for (std::size_t i = 1u; i + 1u < count; ++i)
        {
            const long double before = m_inputs[i] - m_inputs[i - 1u];
            const long double after = m_inputs[i + 1u] - m_inputs[i];
            const long double secantBefore = secantOf(m_inputs, m_outputs, i - 1u);
            const long double secantAfter = secantOf(m_inputs, m_outputs, i);
            const long double parabola = (secantBefore * after + secantAfter * before) / (before + after);
            slopes[i] =
                (signOf(secantBefore) + signOf(secantAfter)) *
                (std::min)(std::fabs(secantBefore), (std::min)(std::fabs(secantAfter), 0.5L * std::fabs(parabola)));
        }
        for (std::size_t i = 0u; i < segments; ++i)
        {
            const long double width = m_inputs[i + 1u] - m_inputs[i];
            const long double secant = secantOf(m_inputs, m_outputs, i);
            m_first[i] = slopes[i];
            m_second[i] = (3.0L * secant - 2.0L * slopes[i] - slopes[i + 1u]) / width;
            m_third[i] = (slopes[i] + slopes[i + 1u] - 2.0L * secant) / width / width;
        }
        break;
    }
    }
    m_valid = true;
}

bool CurveOracle::valid() const noexcept
{
    return m_valid;
}

long double CurveOracle::curveValue(long double input) const noexcept
{
    if (!m_valid || std::isnan(input))
    {
        return input;
    }
    if (input <= m_inputs.front())
    {
        return m_outputs.front();
    }
    if (input >= m_inputs.back())
    {
        return m_outputs.back();
    }
    const auto upper = std::upper_bound(m_inputs.begin(), m_inputs.end(), input);
    const std::size_t segment = static_cast<std::size_t>(upper - m_inputs.begin()) - 1u;
    const long double offset = input - m_inputs[segment];
    return m_outputs[segment] + offset * (m_first[segment] + offset * (m_second[segment] + offset * m_third[segment]));
}

long double CurveOracle::heldValue(long double input) const noexcept
{
    const long double value = curveValue(input);
    return std::isnan(value) ? value : (std::min)((std::max)(value, 0.0L), 1.0L);
}

bool CurveOracle::outsidePoints(long double input) const noexcept
{
    return m_valid && (input < m_inputs.front() || input > m_inputs.back());
}

bool checkCurveOutput(const CurveOracle& oracle, std::span<const float> input, std::span<const float> output,
                      CurveOutputCheck& check)
{
    check = CurveOutputCheck{};
    if (!oracle.valid() || input.size() != output.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        const long double sample = static_cast<long double>(input[index]);
        if (oracle.outsidePoints(sample))
        {
            ++check.clampedInputs;
        }
        const long double curve = oracle.curveValue(sample);
        if (curve < 0.0L || curve > 1.0L)
        {
            ++check.heldOutputs;
        }
        const double expected = static_cast<double>(oracle.heldValue(sample));
        const double produced = static_cast<double>(output[index]);
        // A sample the oracle answers and the row does not is as far from it as anything can be.
        const double difference = std::isnan(produced) != std::isnan(expected) ? std::numeric_limits<double>::infinity()
                                  : std::isnan(produced)                       ? 0.0
                                                                               : std::fabs(produced - expected);
        if (difference > check.largest)
        {
            check.largest = difference;
            check.largestIndex = index;
        }
    }
    check.samples = input.size();
    return true;
}

bool checkCurveOutputCodes(const CurveOracle& oracle, std::span<const std::uint16_t> input,
                           std::span<const std::uint16_t> output, CurveOutputCheck& check)
{
    check = CurveOutputCheck{};
    if (!oracle.valid() || input.size() != output.size())
    {
        return false;
    }
    // A sixteen-bit image holds 65,536 values, so the oracle is asked once for each.
    constexpr long double kLargestCode = 65535.0L;
    std::vector<double> expected(65536u, 0.0);
    std::vector<std::uint8_t> curvesOut(65536u, 0u);
    for (std::size_t code = 0u; code < expected.size(); ++code)
    {
        const long double at = static_cast<long double>(code) / kLargestCode;
        const long double curve = oracle.curveValue(at);
        expected[code] = static_cast<double>(oracle.heldValue(at));
        curvesOut[code] = curve < 0.0L || curve > 1.0L ? 1u : 0u;
    }
    for (std::size_t index = 0u; index < input.size(); ++index)
    {
        check.heldOutputs += curvesOut[input[index]];
        const double produced = static_cast<double>(output[index]) / static_cast<double>(kLargestCode);
        const double difference = std::fabs(produced - expected[input[index]]);
        if (difference > check.largest)
        {
            check.largest = difference;
            check.largestIndex = index;
        }
    }
    check.samples = input.size();
    return true;
}

void reportCurveOutputCheck(const CurveOutputCheck& check, std::map<std::string, double>& counters)
{
    counters["curveOracleSamples"] = static_cast<double>(check.samples);
    counters["curveOracleLargest"] = check.largest;
    counters["curveOracleLargestIndex"] = static_cast<double>(check.largestIndex);
    counters["curveOracleClampedInputs"] = static_cast<double>(check.clampedInputs);
    counters["curveOracleHeldOutputs"] = static_cast<double>(check.heldOutputs);
}

} // namespace an::bench
