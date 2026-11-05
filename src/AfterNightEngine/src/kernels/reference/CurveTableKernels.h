// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - private shapes of the compiled runs that build a tone curve's table.
//
// A curve's plan samples the prepared curve into a table and measures how far the table, read as the pointwise step
// reads it, stands from the curve. Each is one pass over many entries or intervals, so a CPU table compiles both per
// instruction set, and this header is their vocabulary: the prepared curve as a run reads it, the inputs an entry and
// a probe sample, the region of the curve an input lies in, the two run types and the accessors that read them out of
// a CPU table. A run writes the bits of the scalar loops in `algorithms/pointwise/ToneCurve.cpp`, which evaluate in
// the same order, so no plan depends on which table built it.

#pragma once

#include <algorithm>
#include <cstdint>

namespace an
{

class ExecutionContext;

/// How many parts a sampled curve table's interval is split into where its distance from the curve is measured: the
/// three points inside it are the quarter points.
inline constexpr std::uint32_t kCurveTableProbesPerInterval = 4u;

/// A prepared curve and the table it is sampled into, as the compiled table runs read them.
///
/// A sampled table spans the points' range, its first entry at the first point's input and its last at the last's;
/// an exact table holds one entry per code of an integer representation, at the code over the largest code, which
/// may lie outside the points' range.
struct CurveTableShape
{
    /// The control points' inputs, strictly increasing.
    const double* inputs = nullptr;

    /// The control points' outputs.
    const double* outputs = nullptr;

    /// Each segment's derivatives at its left and its right end, two a segment, or null where the segments are
    /// straight.
    const double* segmentTangents = nullptr;

    /// How many control points there are; at least two.
    std::uint32_t pointCount = 0u;

    /// How many entries the table holds; at least two.
    std::uint32_t entryCount = 0u;

    /// Whether entry `i` is the curve at `i` over the largest index - an exact table's code - rather than at
    /// `curveTableInput`'s input across the points' range.
    bool codeInputs = false;

    /// Whether an input outside the points' range extends the nearer end segment's secant rather than taking the
    /// nearer end point's output. A table whose inputs can leave the range under a policy that refuses them is not
    /// handed to a compiled run.
    bool extrapolate = false;
};

/// The input entry `index` of a table of `entryCount` entries over `[low, high]` samples the curve at: the end
/// itself for the last entry, otherwise `low` plus the interval's width times the index over the last index, in
/// that order. Inside `[low, high]` for every index.
///
/// @return the input.
[[nodiscard]] inline double curveTableInput(double low, double high, std::uint32_t index,
                                            std::uint32_t entryCount) noexcept
{
    return index + 1u == entryCount
               ? high
               : low + (high - low) * static_cast<double>(index) / static_cast<double>(entryCount - 1u);
}

/// The input of probe `probe`, one below `kCurveTableProbesPerInterval` at most, inside interval `index` of a table
/// of `entryCount` entries over `[low, high]`: the index plus the probe's share of an interval, over the last
/// index, scaling the interval's width from `low`. Inside `[low, high]` for every interval, by a margin of a
/// quarter of an interval.
///
/// @return the input.
[[nodiscard]] inline double curveTableProbeInput(double low, double high, std::uint32_t index, std::uint32_t probe,
                                                 std::uint32_t entryCount) noexcept
{
    const double position = (static_cast<double>(index) + static_cast<double>(probe) / kCurveTableProbesPerInterval) /
                            static_cast<double>(entryCount - 1u);
    return low + (high - low) * position;
}

/// The input entry `index` of a table samples its curve at: an exact table's code over the largest code, or
/// `curveTableInput`'s across the points' range.
///
/// @return the input.
[[nodiscard]] inline double curveTableEntryInput(const CurveTableShape& shape, std::uint32_t index) noexcept
{
    return shape.codeInputs
               ? static_cast<double>(index) / static_cast<double>(shape.entryCount - 1u)
               : curveTableInput(shape.inputs[0], shape.inputs[shape.pointCount - 1u], index, shape.entryCount);
}

/// The segment a curve is evaluated on at an input inside its points' range: the first at or below the first
/// point, the last at or above the last point, and otherwise the one whose left point is the last at or below the
/// input.
///
/// @return the segment, below `pointCount - 1`.
[[nodiscard]] inline std::uint32_t curveTableSegment(const double* inputs, std::uint32_t pointCount,
                                                     double input) noexcept
{
    if (input <= inputs[0])
    {
        return 0u;
    }
    if (input >= inputs[pointCount - 1u])
    {
        return pointCount - 2u;
    }
    const double* const upper = std::upper_bound(inputs, inputs + pointCount, input);
    return static_cast<std::uint32_t>(upper - inputs) - 1u;
}

/// Where on the curve an input lies: zero below the first point, the segment's index plus one inside the points'
/// range as `curveTableSegment` finds it, and the point count above the last point. It rises with the input.
///
/// @return the region.
[[nodiscard]] inline std::uint32_t curveTableRegion(const CurveTableShape& shape, double input) noexcept
{
    if (input < shape.inputs[0])
    {
        return 0u;
    }
    if (input > shape.inputs[shape.pointCount - 1u])
    {
        return shape.pointCount;
    }
    return curveTableSegment(shape.inputs, shape.pointCount, input) + 1u;
}

/// Fills entries `begin` to `end` of a curve table.
///
/// Each is the curve at `curveTableEntryInput`, evaluated as the tone curve's evaluator evaluates it - outside the
/// points' range the nearer end's output or the end segment's secant from it, inside on `curveTableSegment`'s
/// segment the straight interpolation or the cubic Hermite basis with the segment's two derivatives - with every
/// operation in its order, then held to [0, 1].
///
/// @return the largest distance an entry was held by.
using CurveTableEntriesRunFn = double (*)(const CurveTableShape& shape, std::uint32_t begin, std::uint32_t end,
                                          double* entries) noexcept;

/// The largest distance of a table, sampled linearly, from its curve at the probes of intervals `begin` to `end`.
///
/// Each probe's input is `curveTableProbeInput`'s; the curve there is evaluated as `CurveTableEntriesRunFn`
/// evaluates it, and the table read as a linearly sampled curve reads it - the input's table position held to the
/// table, the entry below it and the next, and the fraction between them - with every operation in its order.
///
/// @return the largest distance.
using CurveTableProbesRunFn = double (*)(const CurveTableShape& shape, const double* entries, std::uint32_t begin,
                                         std::uint32_t end) noexcept;

/// A table's two curve-table runs; both null where it has none and the scalar loops run instead.
struct CurveTableRuns
{
    /// The entries run.
    CurveTableEntriesRunFn entries = nullptr;

    /// The probes run.
    CurveTableProbesRunFn probes = nullptr;
};

/// The sampled curve table's runs selected by one execution's immutable CPU table.
[[nodiscard]] CurveTableRuns curveTableRunsFor(const ExecutionContext& execution) noexcept;

/// The sampled curve table's runs of the best table this host runs and this build holds. A plan builder given no
/// execution asks for these; they write the scalar loops' bits, so no plan depends on which instruction set built
/// it.
[[nodiscard]] CurveTableRuns curveTableRunsForHost() noexcept;

} // namespace an
