// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Engine-independent midtones transfer definition and complete-output qualification.
#pragma once

#include "Results.h"
#include "StretchScenarios.h"

#include <span>

namespace an::bench
{
/// The midtones transfer function, written from its definition. No engine coefficients or
/// evaluator are used.
///
/// An input at or below the shadow point `s` maps to zero and one at or above the highlight point
/// `h` to one, the shadow rule first, so equal endpoints describe a step. Between them the input is
/// rescaled to `x = (v - s) / (h - s)` and mapped to `(m - 1) x / ((2m - 1) x - m)` for the midtones
/// balance `m`: the identity at one half, a step at zero and zero below the highlight point at one.
///
/// It is evaluated as `(1 - m)(v - s) / (m (h - v) + (1 - m)(v - s))`, the same fraction multiplied
/// through by `h - s`. Every term is nonnegative, so nothing cancels, and the numerator is part of
/// the denominator, so the value cannot leave the unit interval.
class MidtonesTransferStretchOracle
{
public:
    /// Borrow no state; Float32 settings are widened before any arithmetic.
    explicit MidtonesTransferStretchOracle(const StretchSettings& settings);
    /// Evaluate the curve in long double.
    [[nodiscard]] long double forward(long double value) const;
    /// The least input the curve maps to `value`: the shadow point for zero and below and the
    /// highlight point for one and above.
    [[nodiscard]] long double inverse(long double value) const;

private:
    long double m_midtones;
    long double m_shadows;
    long double m_highlights;
};

/// True for the screen-transfer family, independent of its row name.
[[nodiscard]] bool requiresMidtonesTransferStretchOracle(const ScenarioSpec& scenario);
/// Visit every sample outside timing, using constant auxiliary storage. Reports numerical error
/// plus range, endpoint and adjacent ordering invariants separately.
void checkMidtonesTransferStretchOutput(const StretchSettings& settings, std::span<const float> input,
                                        std::span<const float> output, ResultRecord& result);
/// Require complete evidence: every counter the check writes, inside the family's numerical tier.
[[nodiscard]] bool midtonesTransferStretchEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& result);
} // namespace an::bench
