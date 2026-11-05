// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral oracle for the tone curve rows.

#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace an::bench
{

/// One control point of a row's curve.
struct CurvePoint
{
    double input = 0.0; ///< Where the point lies, in the unit interval.
    double output = 0.0; ///< The curve's value there.
};

/// A curve's rule, as the rows name it.
enum class CurveRule : std::uint8_t
{
    Linear, ///< Straight segments between the points.
    NaturalCubic, ///< The cubic spline with zero second derivative at both ends.
    Akima, ///< Akima's 1970 rule, the end slopes extended from the first and last two.
    Steffen ///< Steffen's 1990 monotone rule, the end slopes the end secants.
};

/// The rule a row's `interpolation` parameter names, or nothing for a name no rule has.
///
/// @param name `linear`, `natural-cubic`, `akima` or `steffen`.
/// @param rule written with the rule.
/// @return whether the name is one of the four.
[[nodiscard]] bool curveRuleNamed(const std::string& name, CurveRule& rule) noexcept;

/// A curve through its points under one rule, evaluated in `long double` from each rule's own published definition.
///
/// The coefficients are those of each method's paper, as a numerical library lays them out:
/// the natural cubic spline's second derivatives from its tridiagonal system, Akima's slopes from his weighted
/// secants with the two missing secants at each end extended linearly, and Steffen's from the smallest of his three
/// bounds. The engine is not called, so a row's answer is measured against one it did not compute.
class CurveOracle
{
public:
    /// The curve through `points` under `rule`; `valid()` says whether the points describe one.
    ///
    /// @param points at least two points with strictly increasing inputs, three for the cubic rules.
    /// @param rule the rule.
    CurveOracle(std::span<const CurvePoint> points, CurveRule rule);

    /// Whether the points and the rule describe a curve.
    [[nodiscard]] bool valid() const noexcept;

    /// The curve at an input clamped to the points' range: the end values outside it.
    ///
    /// @param input any input; a not-a-number is returned as it is.
    /// @return the curve's value.
    [[nodiscard]] long double curveValue(long double input) const noexcept;

    /// `curveValue` held to the unit interval, as a curves process applies a curve.
    ///
    /// @param input any input; a not-a-number is returned as it is.
    /// @return the held value.
    [[nodiscard]] long double heldValue(long double input) const noexcept;

    /// Whether an input lies outside the points' range, where the end values answer it.
    ///
    /// @param input any input.
    /// @return true below the first point's input or above the last's.
    [[nodiscard]] bool outsidePoints(long double input) const noexcept;

private:
    std::vector<long double> m_inputs;
    std::vector<long double> m_outputs;
    /// Per segment, the cubic's first, second and third coefficients about the segment's start.
    std::vector<long double> m_first;
    std::vector<long double> m_second;
    std::vector<long double> m_third;
    bool m_valid = false;
};

/// What the complete comparison of a row's output with the oracle found.
struct CurveOutputCheck
{
    std::uint64_t samples = 0u; ///< The samples compared, every one of the output.
    double largest = 0.0; ///< The largest absolute difference from the held curve.
    std::uint64_t largestIndex = 0u; ///< The sample it was found at.
    std::uint64_t clampedInputs = 0u; ///< Inputs outside the points' range, answered by the end values.
    std::uint64_t heldOutputs = 0u; ///< Outputs the unit interval held, the curve leaving it.
};

/// Compares every sample of a curve row's output with the oracle at the same sample of its input.
///
/// A row's output is a curve sampled at Float32 inputs and narrowed to Float32, so the comparison is complete rather
/// than probed: the samples next to a control point, an extremum the cubic rules overshoot to and the clamped ends are
/// all among them.
///
/// @param oracle the curve.
/// @param input the samples the row curved.
/// @param output the row's answer, one sample per input sample.
/// @param check written with what the comparison found.
/// @return false when the two spans differ in size or the oracle is not valid.
[[nodiscard]] bool checkCurveOutput(const CurveOracle& oracle, std::span<const float> input,
                                    std::span<const float> output, CurveOutputCheck& check);

/// Compares every sample of a sixteen-bit curve row's output with the oracle at the same sample of its input, each
/// code read as a fraction of the largest code.
///
/// @param oracle the curve.
/// @param input the codes the row curved.
/// @param output the row's answer, one code per input code.
/// @param check written with what the comparison found.
/// @return false when the two spans differ in size or the oracle is not valid.
[[nodiscard]] bool checkCurveOutputCodes(const CurveOracle& oracle, std::span<const std::uint16_t> input,
                                         std::span<const std::uint16_t> output, CurveOutputCheck& check);

/// Writes a check's counters into a result record's counter map, under the names a run reports.
///
/// @param check what the comparison found.
/// @param counters the record's counters.
void reportCurveOutputCheck(const CurveOutputCheck& check, std::map<std::string, double>& counters);

} // namespace an::bench
