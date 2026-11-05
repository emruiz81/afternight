// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The curve a curves widget edits: its control points in the unit square and the rule that joins them, evaluated by
/// the engine's `evaluateCurve`.
///
/// The engine evaluates a curve and keeps no node list, so the list is the widget's. Editing keeps the points in
/// strictly increasing input order with every output in the unit interval, which is what the engine requires of a
/// curve, and the editing rules and the fallbacks for too few points are the product's.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <cstddef>
#include <vector>

namespace an::ui
{

/// An editable tone curve.
class AFTERNIGHTCONTROLS_API ToneCurveModel
{
public:
    /// The identity: a straight line through (0, 0) and (1, 1).
    ToneCurveModel();

    /// A curve through stated points.
    ///
    /// @param points the control points, in increasing input order.
    /// @param interpolation the rule that joins them.
    ToneCurveModel(std::vector<an::CurveControlPoint> points, an::CurveInterpolation interpolation);

    /// The control points, in increasing input order.
    [[nodiscard]] const std::vector<an::CurveControlPoint>& points() const noexcept;

    /// How many control points there are.
    [[nodiscard]] std::size_t size() const noexcept;

    /// Whether the curve has no control point.
    [[nodiscard]] bool isEmpty() const noexcept;

    /// The rule the user chose.
    [[nodiscard]] an::CurveInterpolation interpolation() const noexcept;

    /// Chooses the rule that joins the points.
    ///
    /// @param interpolation the rule.
    void setInterpolation(an::CurveInterpolation interpolation) noexcept;

    /// The rule the curve is evaluated with: a straight line through two points, and the natural cubic through fewer
    /// than five where Akima or Steffen was chosen.
    [[nodiscard]] an::CurveInterpolation effectiveInterpolation() const noexcept;

    /// Inserts a point before the first one whose input is not below it, with its output held in the unit interval.
    ///
    /// @param input where the point is.
    /// @param output what the curve is there.
    /// @return the index the point has; a point at an existing point's input moves that one instead and returns its
    ///         index, since two points cannot share an input.
    std::size_t insertPoint(double input, double output);

    /// Moves a point. An end point keeps its input at the end of the interval only by being dragged there; an inner
    /// point stays strictly between its neighbours by 0.0001; the output is held in the unit interval.
    ///
    /// @param index which point; one past the last is ignored.
    /// @param input where it moves to.
    /// @param output what the curve is there.
    void updatePoint(std::size_t index, double input, double output);

    /// Removes an inner point; an end point stays.
    ///
    /// @param index which point.
    void removePoint(std::size_t index);

    /// Whether the curve is the straight line through (0, 0) and (1, 1) alone.
    [[nodiscard]] bool isIdentity() const noexcept;

    /// The curve at one input, held at the end points outside them.
    ///
    /// @param input where to evaluate it.
    /// @return the curve there; zero for a curve of fewer than two points or one the engine refuses.
    [[nodiscard]] double evaluate(double input) const;

    /// Curves compare by their points and the rule the user chose.
    friend AFTERNIGHTCONTROLS_API bool operator==(const ToneCurveModel& left, const ToneCurveModel& right) noexcept;

private:
    std::vector<an::CurveControlPoint> m_points;
    an::CurveInterpolation m_interpolation = an::CurveInterpolation::Linear;
};

} // namespace an::ui
