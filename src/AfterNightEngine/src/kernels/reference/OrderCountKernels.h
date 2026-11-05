// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - private shape of the compiled run that counts an exact selection's bracket.
//
// An exact selection narrows a rank by counting the selected samples below, inside and above a bracket of values,
// the inside ones into the bracket's bins. `algorithms/histogram/HistogramOrderPrimitives.cpp` counts consecutive
// samples a block at a time so that a compiler can keep a block's arithmetic in vector lanes; not every compiler
// does, so a CPU table compiles the same count for Float32 samples per instruction set, and this header is its
// vocabulary: the bracket as the run reads it, the block, the run's type and the accessor that reads it out of a CPU
// table. A run counts exactly what the scalar block loop counts, so no rank depends on which table counted it. A median
// and its absolute deviation are counted in one read through a set of three brackets and gathered in another through a
// set of intervals, and the runs of those sets are here too.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace an
{

class ExecutionContext;

/// How many consecutive samples a count maps before it increments their counters, and how many discarded counters
/// follow a bracket's bins: an outside sample of a block increments the counter its position in the block names.
inline constexpr std::size_t kOrderCountBlock = 16u;

/// A selection bracket as the compiled count reads it.
struct OrderBracket
{
    /// The bracket's first value.
    double lowest = 0.0;

    /// Its last value.
    double highest = 0.0;

    /// Bins per unit of value: an inside value's bin is its distance above `lowest` times this, truncated and held to
    /// the last bin.
    double scale = 0.0;

    /// How many bins the bracket holds; at least one.
    std::uint64_t bins = 1u;

    /// The first value of the selection's value window: a sample whose widened value lies outside the window is not
    /// counted at all. Minus infinity where no window narrows the selection.
    double windowLowest = -std::numeric_limits<double>::infinity();

    /// The window's last value, inclusive; infinity where no window narrows the selection.
    double windowHighest = std::numeric_limits<double>::infinity();

    /// The first value a counted sample is read as: a sample below it is counted as this value. Minus infinity where
    /// no hold reads the selection.
    double holdLowest = -std::numeric_limits<double>::infinity();

    /// The last value a counted sample is read as, inclusive; infinity where no hold reads the selection.
    double holdHighest = std::numeric_limits<double>::infinity();
};

/// Counts consecutive Float32 samples, or their absolute deviations from a centre, below, inside and above a bracket,
/// the inside ones into its bins.
///
/// A sample is widened to Float64. One outside the bracket's window - where a value that is not a number always lies -
/// is not counted at all, and every sample inside it is finite. A counted sample is read as held to the bracket's hold,
/// the lower end first and then the upper, and is, for a deviation, replaced by its absolute difference from `center`;
/// a value below `lowest` counts below and one above `highest` counts above, and any
/// other increments `bins[min(trunc((value - lowest) * scale), bins - 1)]`. In a whole block of `kOrderCountBlock`
/// samples, a sample that increments no bin increments the discarded counter `bins[bracket.bins + position]` instead,
/// which nothing reads; a sample after the last whole block increments a bin or nothing. Every count is added to what
/// its counter already holds.
///
/// @param samples the first sample.
/// @param length how many consecutive samples to count.
/// @param deviation whether to count absolute deviations from `center` rather than the samples.
/// @param center the value deviations are taken around.
/// @param bracket the bracket and its mapping, whose scale is finite and positive.
/// @param bins the counters: `bracket.bins` bins, then `kOrderCountBlock` discarded counters.
/// @param below the running count below the bracket.
/// @param above the running count above it.
using OrderCountFloat32RunFn = void (*)(const float* samples, std::uint64_t length, bool deviation, double center,
                                        const OrderBracket& bracket, std::uint64_t* bins, std::uint64_t& below,
                                        std::uint64_t& above) noexcept;

/// The compiled bracket count selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table always. A null
///         answer means the selection counts with its own block loop.
[[nodiscard]] OrderCountFloat32RunFn orderCountFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The window and the hold a run reads consecutive samples through.
struct OrderRead
{
    /// The first value of the selection's value window: a sample whose widened value lies outside the window is not
    /// read at all. A run that admits every finite sample states the largest Float32 magnitudes.
    double windowLowest = -std::numeric_limits<double>::infinity();

    /// The window's last value, inclusive.
    double windowHighest = std::numeric_limits<double>::infinity();

    /// The first value a read sample is held to; minus infinity where no hold reads the selection.
    double holdLowest = -std::numeric_limits<double>::infinity();

    /// The last value a read sample is held to; infinity where no hold reads the selection.
    double holdHighest = std::numeric_limits<double>::infinity();
};

/// Counts consecutive Float32 samples inside a window and records the extremes of their held values.
///
/// A sample is widened to Float64; one outside the window - where a value that is not a number always lies - is not
/// counted. A counted sample is held, the lower end first and then the upper, and the extremes are of the held values
/// under the rule the scalar count keeps: an extreme met earlier stays when a later value equals it, so a smallest or
/// largest held value of zero is the first zero counted, with that zero's sign. `count` is added to, and `minimum`
/// and `maximum` are replaced only by a smaller or a larger value, so the runs over one chunk's rows compose in their
/// order as one scalar pass over the chunk would.
///
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param read the window and the hold.
/// @param count the running count of samples inside the window.
/// @param minimum the running smallest held value.
/// @param maximum the running largest held value.
using OrderExtremesFloat32RunFn = void (*)(const float* samples, std::uint64_t length, const OrderRead& read,
                                           std::uint64_t& count, double& minimum, double& maximum) noexcept;

/// The compiled count and extremes selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table always.
[[nodiscard]] OrderExtremesFloat32RunFn orderExtremesFloat32RunFor(const ExecutionContext& execution) noexcept;

/// Gathers one bin's members from consecutive Float32 samples, and the smallest ordered value above the bin.
///
/// A sample outside the window is neither a member nor a candidate. A sample inside it is held, and ordered as its
/// absolute deviation from `center` for a deviation; one whose ordered value lies in the closed interval
/// `[first, last]` is appended at `write`, in the samples' order - written only while `write` lies below `end`, and
/// counted past it all the same, so a gather that disagrees with the count that sized its place writes nothing beyond
/// the place and leaves its caller the disagreement to see. Where `nextAbove` is given, it holds `kOrderCountBlock`
/// running minima, one per position in a block: the ordered value of a sample above `last` replaces its position's
/// minimum only when it is smaller, and a sample after the last whole block competes for the first position, as the
/// scalar block loop keeps them.
///
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param deviation whether to order absolute deviations from `center` rather than the held samples.
/// @param center the value deviations are taken around.
/// @param read the window and the hold.
/// @param first the bin's first value.
/// @param last its last value.
/// @param gathered where members go.
/// @param end the end of the place in `gathered`; no member is written at or past it.
/// @param write the next position in `gathered`, advanced past every member, written or not.
/// @param nextAbove the running minima above `last`, `kOrderCountBlock` of them, or null when none is wanted.
using OrderGatherFloat32RunFn = void (*)(const float* samples, std::uint64_t length, bool deviation, double center,
                                         const OrderRead& read, double first, double last, double* gathered,
                                         std::uint64_t end, std::uint64_t& write, double* nextAbove) noexcept;

/// The compiled bin gather selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table always.
[[nodiscard]] OrderGatherFloat32RunFn orderGatherFloat32RunFor(const ExecutionContext& execution) noexcept;

/// How many brackets one count of a set of them bins, and how many intervals one gather of a set of them takes members
/// of: a median's and the two its absolute deviation's rank lies at, one below the median and one above it.
inline constexpr std::size_t kOrderBracketSetSize = 3u;

/// The brackets one count bins at once.
///
/// The brackets are disjoint and ascending - each one's last value below the next one's first - and hold as many bins
/// each, laid one bracket after another in one table: bracket `b`'s bins are the table's `[b * bins, (b + 1) * bins)`,
/// and `kOrderCountBlock` discarded counters follow the last of them.
struct OrderBracketSet
{
    /// Each bracket's first value.
    std::array<double, kOrderBracketSetSize> lowest{};

    /// Each bracket's last value.
    std::array<double, kOrderBracketSetSize> highest{};

    /// Each bracket's bins per unit of value: an inside value's bin is its distance above its bracket's first value
    /// times this, truncated and held to the bracket's last bin.
    std::array<double, kOrderBracketSetSize> scale{};

    /// How many bins each bracket holds; at least one.
    std::uint32_t bins = 1u;

    /// The first value of the selection's value window, as `OrderBracket` states it.
    double windowLowest = -std::numeric_limits<double>::infinity();

    /// The window's last value, inclusive.
    double windowHighest = std::numeric_limits<double>::infinity();

    /// The first value a counted sample is read as, as `OrderBracket` states it.
    double holdLowest = -std::numeric_limits<double>::infinity();

    /// The last value a counted sample is read as, inclusive.
    double holdHighest = std::numeric_limits<double>::infinity();
};

/// What a count of a set of brackets tallies beside their bins: the samples it counted and their extremes, kept as
/// `OrderExtremesFloat32RunFn` keeps them, and how many lie below and above the brackets.
struct OrderBracketTallies
{
    /// Samples inside the window.
    std::uint64_t counted = 0u;

    /// The smallest of their held values: replaced only by a smaller one, so that a smallest value of zero is the first
    /// zero counted, with its sign.
    double minimum = std::numeric_limits<double>::infinity();

    /// The largest of their held values, kept by the same rule.
    double maximum = -std::numeric_limits<double>::infinity();

    /// Counted samples below the first bracket's first value.
    std::uint64_t belowFirst = 0u;

    /// Counted samples below the second bracket's first value.
    std::uint64_t belowSecond = 0u;

    /// Counted samples above the second bracket's last value.
    std::uint64_t aboveSecond = 0u;

    /// Counted samples above the third bracket's last value.
    std::uint64_t aboveThird = 0u;
};

/// Counts consecutive Float32 samples into a set of brackets at once, and tallies what lies between and beyond them.
///
/// A sample is widened to Float64; one outside the window - where a value that is not a number always lies - is not
/// counted at all. A counted sample is held, the lower end first and then the upper, is counted with its extremes as
/// `OrderExtremesFloat32RunFn` counts it, adds to every tally whose side it lies on, and increments the bin of the
/// bracket it lies in: its distance above that bracket's first value times its scale, truncated and held to the
/// bracket's last bin. In a whole block of `kOrderCountBlock` samples, a sample that increments no bin increments the
/// discarded counter its position in the block names instead, which nothing reads; a sample after the last whole block
/// increments a bin or nothing. Every count is added to what its counter holds, and the extremes are replaced only by
/// a smaller or a larger value, so the runs over one chunk's rows compose in their order as one pass over it would.
///
/// @param samples the first sample.
/// @param length how many consecutive samples to count.
/// @param brackets the brackets, their mapping, the window and the hold.
/// @param bins the counters: `kOrderBracketSetSize * brackets.bins` bins, then `kOrderCountBlock` discarded counters.
/// @param tallies the running tallies.
using OrderCountBracketSetFloat32RunFn = void (*)(const float* samples, std::uint64_t length,
                                                  const OrderBracketSet& brackets, std::uint32_t* bins,
                                                  OrderBracketTallies& tallies) noexcept;

/// The compiled count of a set of brackets selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table always.
[[nodiscard]] OrderCountBracketSetFloat32RunFn
orderCountBracketSetFloat32RunFor(const ExecutionContext& execution) noexcept;

/// The closed intervals one gather takes members of, disjoint, so that a sample is a member of one at most.
struct OrderIntervalSet
{
    /// Each interval's first value.
    std::array<double, kOrderBracketSetSize> first{};

    /// Each interval's last value; an interval whose last value lies below its first takes nothing.
    std::array<double, kOrderBracketSetSize> last{};
};

/// Gathers the members of a set of intervals from consecutive Float32 samples, each interval's at its own position.
///
/// A sample outside the window is no member. A sample inside it is held, and appended at `write[i]` when its held value
/// lies in interval `i`, in the samples' order. A member is written only while `write[i]` lies below `end[i]` and is
/// counted past it all the same, so a gather that disagrees with the count that sized the interval's place writes
/// nothing beyond the place and leaves its caller the disagreement to see.
///
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param read the window and the hold.
/// @param intervals the intervals.
/// @param gathered where members go.
/// @param end each interval's end of place in `gathered`; no member is written at or past it.
/// @param write each interval's next position in `gathered`, advanced past every member, written or not.
using OrderGatherIntervalSetFloat32RunFn = void (*)(const float* samples, std::uint64_t length, const OrderRead& read,
                                                    const OrderIntervalSet& intervals, double* gathered,
                                                    const std::array<std::uint64_t, kOrderBracketSetSize>& end,
                                                    std::array<std::uint64_t, kOrderBracketSetSize>& write) noexcept;

/// The compiled gather of a set of intervals selected by one execution's immutable CPU table.
///
/// @param execution the operation whose backend and instruction set select the table.
/// @return the compiled run, or null when the selected table has none - which is the scalar table always.
[[nodiscard]] OrderGatherIntervalSetFloat32RunFn
orderGatherIntervalSetFloat32RunFor(const ExecutionContext& execution) noexcept;

} // namespace an
