// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent generalized-hyperbolic stretch definition and complete-output qualification.
#pragma once

#include "Results.h"
#include "StretchScenarios.h"

#include <span>

namespace an::bench
{
/// Payne's generalized hyperbolic family, written from its definition for each bias regime and
/// extended by tangent lines at the protection points. No engine coefficients or evaluator are used.
///
/// Either side of the symmetry point the curve is `sign(t) G(|t|)` for `t = x - symmetry`, where
/// `G = T / amount` and `T` is the family member the bias `b` selects, at amount `D`:
///
/// | Bias | T(u) |
/// |---|---|
/// | -1 | ln(1 + D u) |
/// | (-1, 0) | (1 - (1 - b D u)^((1 + b) / b)) / (1 + b) |
/// | 0 | 1 - exp(-D u) |
/// | (0, 1] | 1 - (1 + b D u)^(-1 / b) |
///
/// Every member has `T'(0) = D`, so `G` has unit slope at the symmetry point and the zero-amount
/// limit is the identity. Each is evaluated through `log1p` and `expm1`, so a bias approaching
/// minus one or zero approaches that regime's curve instead of cancelling.
class GeneralizedHyperbolicStretchOracle
{
public:
    /// Borrow no state; Float32 settings are widened before any arithmetic.
    explicit GeneralizedHyperbolicStretchOracle(const StretchSettings& settings);
    /// Evaluate the normalized, clamped forward curve in long double.
    [[nodiscard]] long double forward(long double value) const;
    /// Invert a tangent line or the regime's curve, then clamp to the unit interval.
    ///
    /// Zero and below map to zero and one and above to one; a value the curve reaches over a flat
    /// stretch maps into that stretch.
    [[nodiscard]] long double inverse(long double value) const;
    /// Global Lipschitz bound of the normalized forward curve.
    [[nodiscard]] long double maximumSlope() const;

private:
    /// `G(u)` for a distance `u >= 0` from the symmetry point.
    [[nodiscard]] long double curve(long double distance) const;
    /// `G'(u)`, the slope a tangent line continues at a protection point.
    [[nodiscard]] long double slope(long double distance) const;
    /// `1 - c D G(u)`, the part of the curve's supremum not yet reached, where `c` is `1 + b` for a
    /// negative bias and one otherwise; one for the logarithm, which has no supremum.
    [[nodiscard]] long double tail(long double distance) const;
    /// The distance `u <= limit` at which `G(u)` is `drop` below `limitCurve = G(limit)`, whose
    /// tail is `limitTail`.
    [[nodiscard]] long double distance(long double drop, long double limit, long double limitCurve,
                                       long double limitTail) const;
    long double m_amount;
    long double m_bias;
    long double m_symmetry;
    long double m_low;
    long double m_high;
    /// The curve and its slope at each protection point, evaluated once rather than per sample.
    long double m_lowCurve;
    long double m_highCurve;
    long double m_lowSlope;
    long double m_highSlope;
    long double m_lowTail;
    long double m_highTail;
    /// The primitive's rise from zero to the symmetry point, and from the symmetry point to one.
    long double m_lowRise;
    long double m_highRise;
    long double m_extent;
};

/// True for either direction of the generalized-hyperbolic family, independent of its row name.
[[nodiscard]] bool requiresGeneralizedHyperbolicStretchOracle(const ScenarioSpec& scenario);
/// Visit every sample outside timing, using constant auxiliary storage. Reports numerical
/// error plus range, endpoint, adjacent ordering and inverse-roundtrip invariants separately.
void checkGeneralizedHyperbolicStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                             std::span<const float> output, ResultRecord& result);
/// Require complete evidence: every counter the check writes, inside the family's numerical tier.
[[nodiscard]] bool generalizedHyperbolicStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result);
} // namespace an::bench
