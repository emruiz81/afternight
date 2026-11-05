// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent histogram stretch definition and complete-output qualification.
#pragma once

#include "Results.h"
#include "StretchScenarios.h"

#include <span>

namespace an::bench
{
/// The normalized primitive t/(1 + 2*amount*abs(t)), with tangent extensions at
/// the protection points. No engine coefficients or evaluator are used.
class HistogramStretchOracle
{
public:
    /// Borrow no state; Float32 settings are widened before any arithmetic.
    explicit HistogramStretchOracle(const StretchSettings& settings);
    /// Evaluate the normalized, clamped forward curve in long double.
    [[nodiscard]] long double forward(long double value) const;
    /// Invert the primitive or its tangent, then clamp to the unit interval.
    [[nodiscard]] long double inverse(long double value) const;
    /// Global Lipschitz bound of the normalized forward curve.
    [[nodiscard]] long double maximumSlope() const;

private:
    /// Unnormalized primitive, extended by its boundary tangents.
    [[nodiscard]] long double primitive(long double value) const;
    /// Slope of the unextended primitive at a protection point.
    [[nodiscard]] long double slope(long double value) const;
    long double m_k;
    long double m_symmetry;
    long double m_low;
    long double m_high;
    long double m_origin;
    long double m_extent;
    /// The extended primitive and its tangent slope at each protection point, which every
    /// inverse compares against; evaluated once rather than per sample.
    long double m_lowPrimitive;
    long double m_highPrimitive;
    long double m_lowSlope;
    long double m_highSlope;
};

/// True for either direction of the histogram family, independent of its row name.
[[nodiscard]] bool requiresHistogramStretchOracle(const ScenarioSpec& scenario);
/// Visit every sample outside timing, using constant auxiliary storage. Reports numerical
/// error plus range, endpoint, adjacent ordering and inverse-roundtrip invariants separately.
void checkHistogramStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                 std::span<const float> output, ResultRecord& result);
/// Require complete evidence: every counter the check writes, inside the family's numerical tier.
[[nodiscard]] bool histogramStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result);
} // namespace an::bench
