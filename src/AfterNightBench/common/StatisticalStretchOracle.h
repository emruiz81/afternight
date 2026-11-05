// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent statistical stretch definition and complete-output qualification.
#pragma once

#include "Results.h"
#include "StretchScenarios.h"

#include <cstdint>
#include <span>

namespace an::bench
{
/// The most values the deterministic sample behind the lower-half spread holds.
inline constexpr std::uint64_t kStatisticalStretchSampleLimit = 400000u;

/// What the statistical stretch of one packed mono image is defined by, measured outside timing.
///
/// The minimum and the median are the image's own, over every finite sample: the curve sends the
/// median to the target and cuts at a black point never below the minimum, so neither is an
/// estimate. The median of an even count is the lower middle value plus half the distance to the
/// upper one. The spread is a noise estimate: 1.4826 times the median absolute deviation of the
/// lower half of a deterministic sample - every sample whose index is a multiple of
/// `ceil(count / 400000)` - about that lower half's own median, where the lower half is every
/// sampled value at or below the sample's median.
struct StatisticalStretchReference
{
    double minimum = 0.0;
    double maximum = 0.0;
    double median = 0.0;
    double lowerHalfSigma = 0.0;
    std::uint64_t samples = 0u;
    std::uint64_t sampledValues = 0u;
    /// The minimum under `noBlackClip`; otherwise `median - blackpointSigma * lowerHalfSigma`,
    /// raised to the minimum and capped at 0.99.
    double blackPoint = 0.0;
};

/// Measures the reference analysis of every finite sample, selecting the median by counting
/// rather than by sorting a copy of the image.
[[nodiscard]] StatisticalStretchReference measureStatisticalStretch(const StretchSettings& settings,
                                                                    std::span<const float> input);

/// The statistical stretch, written from its definition. No engine coefficients or evaluator are used.
///
/// A sample `v` is rescaled to `r = (v - b) / (1 - b)` for the black point `b`, and `r` is held to
/// the unit interval: everything at or below the black point is cut to zero, as the stretch's
/// clipping count says it is, and nothing above one exceeds it. The rescaled median `m` goes to the
/// target `t` through `(m - 1) t r / (m (t + r - 1) - t r)`, whose denominator is replaced by 1e-12
/// when it is closer to zero than that. It is evaluated as `(1 - m) t r / (m (1 - t) + r (t - m))`,
/// the same fraction with both terms negated: for a median below the target every term is
/// nonnegative, so nothing cancels. The optional six-point highlight curve, the optional division
/// by the image-wide maximum of what those produce, and a clamp to the unit interval follow.
class StatisticalStretchOracle
{
public:
    /// The curve through `median` with black point `blackPoint`; Float32 settings are widened.
    StatisticalStretchOracle(const StretchSettings& settings, double median, double blackPoint);
    /// The transfer and the optional highlight curve, before normalization and the final clamp.
    [[nodiscard]] long double transfer(long double value) const;
    /// Measures the normalization divisor over `input`: the largest transfer when normalization is
    /// requested and that is positive, otherwise one.
    void measureDivisor(std::span<const float> input);
    /// The divisor `forward` applies.
    [[nodiscard]] long double divisor() const
    {
        return m_divisor;
    }
    /// The complete stretch of one sample.
    [[nodiscard]] long double forward(long double value) const;
    /// The family has no inverse row; the shared walk never asks for one.
    [[nodiscard]] long double inverse(long double value) const;

private:
    long double m_blackPoint;
    long double m_scale;
    long double m_median;
    long double m_target;
    bool m_applyCurves;
    bool m_normalize;
    long double m_curveX[6];
    long double m_curveY[6];
    long double m_divisor = 1.0L;
};

/// True for the statistical stretch, independent of its row name.
[[nodiscard]] bool requiresStatisticalStretchOracle(const ScenarioSpec& scenario);

/// Visits every sample outside timing, using constant auxiliary storage beyond the bounded sample.
///
/// The output is checked against the curve of the reference analysis, whose statistics are reported
/// beside it. Reports numerical error plus range, endpoint and adjacent ordering invariants.
void checkStatisticalStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                   std::span<const float> output, ResultRecord& result);

/// Requires complete evidence: every counter the check writes, inside the family's numerical tier.
[[nodiscard]] bool statisticalStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result);
} // namespace an::bench
