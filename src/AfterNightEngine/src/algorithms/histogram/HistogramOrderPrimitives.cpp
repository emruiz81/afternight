// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - bounded-scratch histogram, selection, quantile, median and MAD primitives.
//
// The memory these operations hold is the point of them. A median absolute deviation that allocated
// one `std::vector` per call - inside a rejection loop, once per output pixel - would not appear as a
// hot function; it would appear as an allocator the whole process contends on. Nothing here holds
// one working element per input sample. A histogram holds its bins once per chunk and bounds the
// chunk count so that the product stays under a stated ceiling; a selection narrows a rank to a
// bracket by counting the samples into
// bins of an interval that holds it - read from a deterministic sample of the plane when every sample
// is selected, the samples' whole range otherwise - and gathers actual samples only once that bracket
// holds a bounded few, falling back to fixing an order-preserving key one eight-bit digit at a time
// when endpoint counts cannot decide a rank. An eight- or sixteen-bit integer plane holding at least
// as many samples as its representation has codes is counted into a table of those codes instead,
// which answers every rank and the deviation from one read, and a histogram of it by classifying
// each code once rather than each sample; verified normalized integer alphabets
// use an exact frequency table for their ranks and weighted deviations; a median with its deviation
// over a large Float32 plane is counted and gathered in two reads together - its bracket and the two
// its deviation's rank lies in counted in one read, the bins and the deviation's candidates gathered in
// another, the deviation's rank placed by the counts and checked against the exact median; a window
// filter holds one window per chunk of rows rather than one per pixel.
//
// Every entry point reads the eight resident representations directly and widens once to Float64 on
// the way in, so the ordering is defined on one processing domain rather than on eight and a
// boundary representation is converted at the read rather than compared in its own type.
//
// A rule that keeps one closed interval of widened values and reads no mask - a range, or finite-only
// over a floating representation - is restated as a value window before any scan, so it reads as the
// all-valid selection that interval narrows and takes the same row scans and sampled bracket. The
// samples selected are the same, and so is every rank.

#include <AfterNightEngine/Algorithms/Kernel/HistogramOrderKernel.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "kernels/reference/OrderCountKernels.h"
#include "kernels/reference/PlaneSourceAccess.h"
#include "kernels/reference/ReferenceKernelInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kHistogramOperation = "an::accumulatePlaneHistogram";
constexpr const char* kSelectionOperation = "an::selectPlaneOrderStatistics";
constexpr const char* kHistogramOverOperation = "an::accumulatePlaneHistogramOver";
constexpr const char* kSelectionOverOperation = "an::selectPlaneOrderStatisticsOver";
constexpr const char* kCompactionOperation = "an::compactSelectedPlaneSamples";
constexpr const char* kWindowOperation = "an::selectWindowOrderStatistic";
constexpr const char* kFootprintOperation = "an::selectFootprintOrderStatistic";

/// How many bits of the ordered key one descent step fixes.
///
/// Eight rather than sixteen because the counters are held once per chunk: a two-hundred-and-fifty-
/// six-entry table is two kibibytes per chunk and leaves the partition free to follow the work,
/// where a sixteen-bit digit would be half a mebibyte per chunk and would cap the parallel width to
/// keep its own ceiling. The narrower digit costs at most a further pass and usually costs none:
/// after one digit, a plane of a million samples holds few enough candidates to gather.
constexpr std::uint32_t kOrderDigitBits = 8u;

/// How many counters one descent step needs.
constexpr std::uint64_t kOrderDigitBuckets = std::uint64_t{1} << kOrderDigitBits;

/// How many descent steps fix a whole Float64 key.
constexpr std::uint32_t kOrderDigits = 64u / kOrderDigitBits;

/// How many chunks a narrowing pass divides the plane into.
///
/// A narrowing pass trades chunks for bins inside the counter budget the descent already owns, so
/// it costs no further memory: eight chunks of that budget is a table wide enough to put a rank
/// inside a gatherable bracket in one read, and still leaves two chunks per worker on the
/// four-thread validation floor.
constexpr std::uint64_t kMaxNarrowingChunks = 8u;

/// Narrowing chunks used by a tight all-valid span, whose scan has no per-sample selection branch.
constexpr std::uint64_t kMaxContiguousNarrowingChunks = 8u;

/// How many fixed chunks an order selection keeps counters for.
///
/// Sixteen chunks per worker on the four-thread validation floor preserve balancing, while a wider
/// fixed partition only clears and merges more tables. The narrowing pass spends the saved counter
/// budget as fewer bins; at this cap a million-sample rank still lands inside the bounded gather in
/// one scan.
constexpr std::uint64_t kMaxSelectionChunks = 64u;

/// The extra per-chunk counters a histogram pass keeps beside its bins.
///
/// Selected, below, above and not-a-number, in that order, so one allocation carries the bins and
/// the counters and one merge covers both.
constexpr std::uint64_t kHistogramCounters = 4u;

/// One failure from this file.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status orderFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// The order-preserving unsigned key of a Float64 value.
///
/// Comparing keys as unsigned integers gives exactly the ascending order of the values they came
/// from, negative values included, which is what lets a selection bucket samples by a digit instead
/// of comparing them. Not-a-number has no place in that order and is refused before it reaches here.
///
/// @param value a finite value or an infinity.
/// @return its key.
[[nodiscard]] std::uint64_t orderKey(double value) noexcept
{
    constexpr std::uint64_t kSign = std::uint64_t{1} << 63u;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    return (bits & kSign) != 0u ? ~bits : (bits | kSign);
}

/// The Float64 value one key came from.
///
/// @param key a key produced by `orderKey`.
/// @return the value.
[[nodiscard]] double keyValue(std::uint64_t key) noexcept
{
    constexpr std::uint64_t kSign = std::uint64_t{1} << 63u;
    return std::bit_cast<double>((key & kSign) != 0u ? (key & ~kSign) : ~key);
}

/// The mask selecting the digits a descent has already fixed.
///
/// @param shift the bit position of the digit being resolved now.
/// @return the mask of every digit above it, which is zero for the first step.
[[nodiscard]] std::uint64_t fixedDigitMask(std::uint32_t shift) noexcept
{
    const std::uint32_t above = shift + kOrderDigitBits;
    return above >= 64u ? 0u : (~std::uint64_t{0} << above);
}

/// The closed interval one histogram request resolves to for one representation.
///
/// An integer representation contributes its own extremes; a floating one contributes the
/// normalized linear domain, which is the meaning integer full scale maps onto.
///
/// @tparam SampleT the resident type.
/// @param request the stated or derived domain.
/// @param lowest where the lower endpoint goes.
/// @param highest where the upper endpoint goes.
template <ResidentSample SampleT>
void resolveInterval(const PlaneHistogramRequest& request, double& lowest, double& highest) noexcept
{
    if (request.domain == HistogramDomain::Stated)
    {
        lowest = request.lowest;
        highest = request.highest;
        return;
    }
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        lowest = 0.0;
        highest = 1.0;
    }
    else
    {
        lowest = kSampleMinimumOf<SampleT>;
        highest = kSampleMaximumOf<SampleT>;
    }
}

/// How many chunks a per-chunk table of this width may be divided into.
///
/// The private tables are what a partition has to pay for, so the ceiling on them decides the
/// partition rather than the other way round. One chunk is always allowed however wide the table
/// is, because refusing to run is not an answer a bin count should be able to produce.
///
/// @param naturalChunks what the work alone would ask for.
/// @param bytesPerChunk what one chunk's private table costs.
/// @return the chunk count, at least one.
[[nodiscard]] std::uint64_t chunksWithinScratch(std::uint64_t naturalChunks, std::uint64_t bytesPerChunk) noexcept
{
    if (naturalChunks == 0u)
    {
        return 1u;
    }
    const std::uint64_t affordable = bytesPerChunk == 0u ? naturalChunks : kMaxHistogramPartialBytes / bytesPerChunk;
    const std::uint64_t bounded = (std::min)(naturalChunks, affordable);
    return bounded == 0u ? 1u : bounded;
}

/// One chunk's view of a run of pixels inside one row.
struct RowRun
{
    /// The row the run lies in.
    std::uint32_t y = 0u;

    /// The first column of the run.
    std::uint32_t x = 0u;

    /// How many pixels the run covers.
    std::uint64_t length = 0u;
};

/// The next run of one row inside a half-open range of row-major pixel indices.
///
/// A chunk covers a range of pixels rather than a range of rows, so it usually begins and ends in
/// the middle of a row; walking it run by run is what lets a body read a row through one pointer.
///
/// @param index the current pixel index, advanced past the run.
/// @param end one past the last pixel of the range.
/// @param width how many pixels one row holds.
/// @return the run.
[[nodiscard]] RowRun nextRun(std::uint64_t& index, std::uint64_t end, std::uint32_t width) noexcept
{
    const std::uint32_t y = static_cast<std::uint32_t>(index / width);
    const std::uint32_t x = static_cast<std::uint32_t>(index % width);
    const std::uint64_t length = (std::min)(end - index, static_cast<std::uint64_t>(width - x));
    index += length;
    return RowRun{.y = y, .x = x, .length = length};
}

/// What one chunk of a counting pass saw.
struct SelectionExtremes
{
    /// How many samples the validity rule selected in this chunk.
    std::uint64_t count = 0u;

    /// The smallest of them.
    double minimum = std::numeric_limits<double>::infinity();

    /// The largest of them.
    double maximum = -std::numeric_limits<double>::infinity();

    /// Multiplicity of the minimum when a refinement counts repeated endpoints.
    std::uint64_t minimumCount = 0u;

    /// Multiplicity of the maximum when a refinement counts repeated endpoints.
    std::uint64_t maximumCount = 0u;

    /// Samples below a bracket's first value, when a bracket count fills this record.
    std::uint64_t belowBracket = 0u;

    /// Samples above a bracket's last value, when a bracket count fills this record.
    std::uint64_t aboveBracket = 0u;

    /// The smallest value above a gathered bin's last value, when a gather fills this record.
    double nextAbove = std::numeric_limits<double>::infinity();
};

/// Adds one refinement candidate, keeping both endpoint multiplicities exact.
void countRefinedValue(SelectionExtremes& seen, double value) noexcept
{
    ++seen.count;
    if (value < seen.minimum)
    {
        seen.minimum = value;
        seen.minimumCount = 1u;
    }
    else if (value == seen.minimum)
        ++seen.minimumCount;
    if (value > seen.maximum)
    {
        seen.maximum = value;
        seen.maximumCount = 1u;
    }
    else if (value == seen.maximum)
        ++seen.maximumCount;
}

/// The chunk cost of one pass over a plane of this representation.
///
/// @tparam SampleT the resident type.
/// @param itemCount how many pixels the pass covers.
/// @param chunkCount the fixed partition.
/// @return the cost of one chunk, for the scheduler's grain policy.
template <ResidentSample SampleT>
[[nodiscard]] TaskCost chunkCostOf(std::uint64_t itemCount, std::uint64_t chunkCount) noexcept
{
    const std::uint64_t largestChunk = itemCount / chunkCount + (itemCount % chunkCount != 0u ? 1u : 0u);
    return TaskCost{.bytesPerItem = largestChunk * sizeof(SampleT), .operationsPerItem = largestChunk * 8u};
}

/// One when a floating sample is an infinity or a not-a-number, read from its exponent bits so the
/// test adds no branch to a counting loop; always zero for an integer sample.
///
/// @tparam SampleT the resident type.
/// @param value the stored sample.
/// @return one or zero, so that a loop can accumulate it with a bitwise or.
template <ResidentSample SampleT> [[nodiscard]] std::uint64_t nonFiniteFlag(SampleT value) noexcept
{
    if constexpr (std::is_same_v<SampleT, float>)
    {
        return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) == 0x7f800000u ? 1u : 0u;
    }
    else if constexpr (std::is_same_v<SampleT, double>)
    {
        return (std::bit_cast<std::uint64_t>(value) & 0x7ff0000000000000ull) == 0x7ff0000000000000ull ? 1u : 0u;
    }
    else
    {
        return 0u;
    }
}

[[nodiscard]] std::uint64_t valueBin(double ordered, double lowest, double highest, std::uint64_t bins) noexcept;

/// The value interval and the hold a block scan reads its samples through: the whole real line for each when the
/// reader states none, so that a scan without them reads every finite sample as it is stored.
struct ScanInterval
{
    /// The interval's inclusive first value.
    double lowest = -std::numeric_limits<double>::infinity();

    /// Its inclusive last value.
    double highest = std::numeric_limits<double>::infinity();
};

/// The value window and the hold one reader states, for the block scans.
///
/// @tparam SampleT the resident type.
/// @param reader the checked component.
/// @return the window and the hold, each the whole real line where the reader states none.
template <ResidentSample SampleT>
[[nodiscard]] std::pair<ScanInterval, ScanInterval>
scanIntervalsOf(const plane_detail::PlaneSampleReader<SampleT>& reader) noexcept
{
    return {ScanInterval{.lowest = reader.valueWindowLowest(), .highest = reader.valueWindowHighest()},
            ScanInterval{.lowest = reader.holdLowest(), .highest = reader.holdHighest()}};
}

/// Counts the selected samples of one representation and records their extremes.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param chunkCount the fixed partition.
/// @param extremes one record per chunk.
/// @param operation the name carried by any failure.
/// @return success, or the first failure in chunk order.
template <ResidentSample SampleT>
[[nodiscard]] Status countSelected(const ExecutionContext& execution,
                                   const plane_detail::PlaneSampleReader<SampleT>& reader, std::uint64_t chunkCount,
                                   SelectionExtremes* extremes, const char* operation)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    // A compiled count where the execution's table has one, for a selection a value window or a hold reads - the
    // loops below keep a plain selection in lanes on their own, and only those two take them out of lanes: every
    // stored sample is selected but for its window, a row's samples are consecutive, and the run admits the finite
    // samples where no window narrows them, so a chunk that counts fewer than it holds has a selected sample that is
    // not finite.
    if constexpr (std::is_same_v<SampleT, float>)
    {
        const OrderExtremesFloat32RunFn compiled =
            reader.hasValueWindow() || reader.hasHold() ? orderExtremesFloat32RunFor(execution) : nullptr;
        if (compiled != nullptr && reader.unmaskedValues() && reader.columnStride() == 1u)
        {
            const double largest = static_cast<double>((std::numeric_limits<float>::max)());
            const bool windowed = reader.hasValueWindow();
            const OrderRead read{.windowLowest = windowed ? reader.valueWindowLowest() : -largest,
                                 .windowHighest = windowed ? reader.valueWindowHighest() : largest,
                                 .holdLowest = reader.holdLowest(),
                                 .holdHighest = reader.holdHighest()};
            const bool packed = reader.contiguousUnmaskedValues();
            return parallelFor(execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount),
                               [&](TaskRange chunkIndices) -> Status {
                                   for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                                   {
                                       SelectionExtremes seen;
                                       const TaskRange range =
                                           plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                                       std::uint64_t counted = 0u;
                                       if (packed)
                                       {
                                           compiled(reader.contiguousSamples() + range.begin, range.end - range.begin,
                                                    read, counted, seen.minimum, seen.maximum);
                                       }
                                       else
                                       {
                                           std::uint64_t index = range.begin;
                                           while (index < range.end)
                                           {
                                               const RowRun run = nextRun(index, range.end, width);
                                               compiled(reader.rowStart(run.y) + run.x, run.length, read, counted,
                                                        seen.minimum, seen.maximum);
                                           }
                                       }
                                       if (!windowed && counted != range.end - range.begin)
                                       {
                                           return orderFailure(
                                               ErrorCode::InvalidData, operation,
                                               "a selected sample is not finite; use a validity rule that excludes it");
                                       }
                                       seen.count = counted;
                                       extremes[chunk] = seen;
                                   }
                                   return Status::success();
                               });
        }
    }
    if (reader.contiguousUnmaskedValues())
    {
        const SampleT* const samples = reader.contiguousSamples();
        const auto [window, hold] = scanIntervalsOf(reader);
        const auto scan = [&]<bool windowed, bool holds>() -> Status {
            return parallelFor(execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount),
                               [&](TaskRange chunkIndices) -> Status {
                                   for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                                   {
                                       // Publish once: adjacent chunk records may share a cache line.
                                       SelectionExtremes seen;
                                       const TaskRange range =
                                           plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                                       std::uint64_t nonFinite = 0u;
                                       for (std::uint64_t index = range.begin; index < range.end; ++index)
                                       {
                                           const SampleT stored = samples[index];
                                           double value = static_cast<double>(stored);
                                           if constexpr (windowed)
                                           {
                                               if (!((value >= window.lowest) & (value <= window.highest)))
                                                   continue;
                                               ++seen.count;
                                           }
                                           nonFinite |= nonFiniteFlag(stored);
                                           if constexpr (holds)
                                               value = (std::min)((std::max)(value, hold.lowest), hold.highest);
                                           seen.minimum = (std::min)(seen.minimum, value);
                                           seen.maximum = (std::max)(seen.maximum, value);
                                       }
                                       if (nonFinite != 0u)
                                       {
                                           return orderFailure(
                                               ErrorCode::InvalidData, operation,
                                               "a selected sample is not finite; use a validity rule that excludes it");
                                       }
                                       if constexpr (!windowed)
                                           seen.count = range.end - range.begin;
                                       extremes[chunk] = seen;
                                   }
                                   return Status::success();
                               });
        };
        if (reader.hasHold())
        {
            return reader.contiguousAllValid() ? scan.template operator()<false, true>()
                                               : scan.template operator()<true, true>();
        }
        return reader.contiguousAllValid() ? scan.template operator()<false, false>()
                                           : scan.template operator()<true, false>();
    }
    const std::uint32_t columnStride = reader.columnStride();
    return parallelFor(
        execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                SelectionExtremes seen;
                const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const RowRun run = nextRun(index, range.end, width);
                    const SampleT* const row = reader.rowStart(run.y);
                    plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                    const bool finite = validityBits.forEachAllowed(run.length, [&](std::uint64_t offset) {
                        const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                        double value = 0.0;
                        if (!reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                        {
                            return true;
                        }
                        if (!std::isfinite(value))
                        {
                            return false;
                        }
                        ++seen.count;
                        seen.minimum = (std::min)(seen.minimum, value);
                        seen.maximum = (std::max)(seen.maximum, value);
                        return true;
                    });
                    if (!finite)
                    {
                        return orderFailure(ErrorCode::InvalidData, operation,
                                            "a selected sample is not finite; use a validity rule that excludes it");
                    }
                }
                extremes[chunk] = seen;
            }
            return Status::success();
        });
}

/// Accumulates one representation's samples into per-chunk bins and counters.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param bins how many bins the caller supplied.
/// @param lowest the interval's inclusive lower endpoint.
/// @param highest the interval's inclusive upper endpoint.
/// @param chunkCount the fixed partition.
/// @param partials `chunkCount * (bins + kHistogramCounters)` counters, cleared by their own chunk.
/// @return success, or the first failure in chunk order.
template <ResidentSample SampleT>
[[nodiscard]] Status accumulateBins(const ExecutionContext& execution,
                                    const plane_detail::PlaneSampleReader<SampleT>& reader, std::uint64_t bins,
                                    double lowest, double highest, std::uint64_t chunkCount, std::uint64_t* partials)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint64_t stride = bins + kHistogramCounters;
    const std::uint32_t columnStride = reader.columnStride();
    return parallelFor(
        execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                std::uint64_t* const chunkBins = partials + chunk * stride;
                std::uint64_t* const counters = chunkBins + bins;
                // A chunk clears its own table, so the worker that is about to fill it is the one
                // that touches it first and the clearing costs the width the counting does.
                std::fill_n(chunkBins, static_cast<std::size_t>(stride), std::uint64_t{0});
                const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const RowRun run = nextRun(index, range.end, width);
                    const SampleT* const row = reader.rowStart(run.y);
                    plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                    for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                    {
                        const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                        double value = 0.0;
                        if (!validityBits.allowsAndAdvance() ||
                            !reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                        {
                            continue;
                        }
                        ++counters[0];
                        if (std::isnan(value))
                        {
                            ++counters[3];
                            continue;
                        }
                        if (value < lowest)
                        {
                            ++counters[1];
                            continue;
                        }
                        if (value > highest)
                        {
                            ++counters[2];
                            continue;
                        }
                        ++chunkBins[valueBin(value, lowest, highest, bins)];
                    }
                }
            }
            return Status::success();
        });
}

/// Counts an all-valid UInt16 component with unit column stride into 65,536 full-scale bins.
///
/// The fast path of the histogram a sixteen-bit display asks for most. Every code is its own bin, so
/// a sample indexes its bin with no interval arithmetic and no validity, range or not-a-number test,
/// and the per-chunk tables hold 32-bit counters, half the memory of the general path's - which is
/// why the caller admits this path only when no chunk holds more samples than such a counter can
/// count. The merge sums the tables per range of bins and writes every one of the caller's bins.
///
/// @param execution the operation.
/// @param reader the checked component, all valid and with unit column stride.
/// @param bins the caller's 65,536 counters.
/// @param chunkCount the fixed partition.
/// @param lowest the resolved lower endpoint, reported back.
/// @param highest the resolved upper endpoint, reported back.
/// @param operation the entry point the caller is inside.
/// @return the counts, or the first failure.
[[nodiscard]] Result<PlaneHistogramResult>
fullScaleUInt16Histogram(const ExecutionContext& execution,
                         const plane_detail::PlaneSampleReader<std::uint16_t>& reader, std::span<std::uint64_t> bins,
                         std::uint64_t chunkCount, double lowest, double highest, const char* operation)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint64_t partialBytes = chunkCount * static_cast<std::uint64_t>(bins.size()) * sizeof(std::uint32_t);
    Result<plane_detail::ScopedContextMemory> partials =
        plane_detail::ScopedContextMemory::take(execution, partialBytes, alignof(std::uint32_t), operation);
    if (!partials.hasValue())
    {
        return partials.status();
    }
    auto* const counters = static_cast<std::uint32_t*>(partials->data());
    const std::uint64_t stride = static_cast<std::uint64_t>(bins.size());
    const Status counted =
        parallelFor(execution, chunkCount, chunkCostOf<std::uint16_t>(itemCount, chunkCount),
                    [&](TaskRange chunkIndices) -> Status {
                        for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                        {
                            std::uint32_t* const chunkBins = counters + chunk * stride;
                            std::fill_n(chunkBins, bins.size(), std::uint32_t{0});
                            const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                            std::uint64_t index = range.begin;
                            while (index < range.end)
                            {
                                const RowRun run = nextRun(index, range.end, width);
                                const std::uint16_t* const row = reader.rowStart(run.y);
                                for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                                {
                                    ++chunkBins[row[run.x + static_cast<std::uint32_t>(offset)]];
                                }
                            }
                        }
                        return Status::success();
                    });
    if (counted.failed())
    {
        return counted;
    }

    const std::uint64_t binCount = static_cast<std::uint64_t>(bins.size());
    const std::uint64_t mergeChunks =
        (std::min)(binCount,
                   deterministicChunkCount(binCount, TaskCost{.bytesPerItem = chunkCount * sizeof(std::uint32_t),
                                                              .operationsPerItem = chunkCount}));
    const Status merged =
        parallelFor(execution, mergeChunks,
                    TaskCost{.bytesPerItem = chunkCount * sizeof(std::uint32_t), .operationsPerItem = chunkCount},
                    [&](TaskRange mergeIndices) -> Status {
                        for (std::uint64_t index = mergeIndices.begin; index < mergeIndices.end; ++index)
                        {
                            const TaskRange range = plane_detail::deterministicRange(index, binCount, mergeChunks);
                            for (std::uint64_t bin = range.begin; bin < range.end; ++bin)
                            {
                                std::uint64_t total = 0u;
                                for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
                                {
                                    total += counters[chunk * stride + bin];
                                }
                                bins[static_cast<std::size_t>(bin)] = total;
                            }
                        }
                        return Status::success();
                    });
    if (merged.failed())
    {
        return merged;
    }

    PlaneHistogramResult result;
    result.run.selection = ValidityCount{.valid = itemCount, .total = itemCount};
    result.run.outputsWritten = itemCount;
    result.deterministicChunks = chunkCount;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    result.partialBytes = partialBytes;
    result.lowest = lowest;
    result.highest = highest;
    return result;
}

/// How a narrowing pass divides the plane and interval inside the selection scratch budget.
struct NarrowingPlan
{
    /// Chunks the narrowing pass partitions the plane into.
    std::uint64_t chunks = 0u;

    /// Bins it divides the interval into.
    std::uint64_t bins = 0u;
};

/// Precomputed arithmetic for the repeated value-to-bin mapping of one exact selection descent.
///
/// The bins only partition candidates before the selected bin is gathered and sorted; they do not
/// approximate the returned rank. Multiplying by the reciprocal here avoids one precise division
/// per sample on both the median and deviation passes while keeping the map monotone and identical
/// between counting and gathering.
struct SelectionBinPlan
{
    /// The interval's inclusive lower endpoint.
    double lowest = 0.0;

    /// The interval's inclusive upper endpoint.
    double highest = 0.0;

    /// Bins per unit of value, or zero when the span overflows and the scaled fallback is needed.
    double binScale = 0.0;

    /// How many bins divide the interval, at least one.
    std::uint64_t bins = 0u;
};

/// The mapping one narrowing pass uses for an interval and a bin count.
///
/// @param lowest the smallest ordered value any selected sample can take.
/// @param highest the largest, above `lowest`.
/// @param bins how many bins divide the interval, at least one.
/// @return the plan, whose scale is zero when the span is not finite.
[[nodiscard]] SelectionBinPlan selectionBinPlan(double lowest, double highest, std::uint64_t bins) noexcept
{
    const double span = highest - lowest;
    return SelectionBinPlan{.lowest = lowest,
                            .highest = highest,
                            .binScale = std::isfinite(span) ? static_cast<double>(bins) / span : 0.0,
                            .bins = bins};
}

/// Which bin of a selection descent's interval one ordered value falls in.
///
/// Counting and gathering must agree on this exactly, so both call this one function: a value
/// counted into one bin and gathered into another would write past the buffer the count sized. It
/// is monotone, which is what makes the bin holding a rank the bracket that rank is in. A span too
/// wide to represent falls back to scaling the endpoints first, as `valueBin` does.
///
/// @param ordered the value or deviation being placed.
/// @param plan the interval and its precomputed scale.
/// @return the bin, always below `plan.bins`.
[[nodiscard]] std::uint64_t selectionValueBin(double ordered, const SelectionBinPlan& plan) noexcept
{
    if (plan.binScale != 0.0)
    {
        const double scaled = (ordered - plan.lowest) * plan.binScale;
        if (!(scaled > 0.0))
        {
            return 0u;
        }
        if (!(scaled < static_cast<double>(plan.bins)))
        {
            return plan.bins - 1u;
        }
        return static_cast<std::uint64_t>(scaled);
    }
    if (ordered <= plan.lowest)
    {
        return 0u;
    }
    if (ordered >= plan.highest)
    {
        return plan.bins - 1u;
    }
    double scaled = 0.0;
    if (std::isfinite(plan.lowest) && std::isfinite(plan.highest))
    {
        const double magnitude = (std::max)(std::abs(plan.lowest), std::abs(plan.highest));
        const double normalizedLow = plan.lowest / magnitude;
        const double fraction = (ordered / magnitude - normalizedLow) / (plan.highest / magnitude - normalizedLow);
        scaled = fraction * static_cast<double>(plan.bins);
    }
    if (!(scaled > 0.0))
    {
        return 0u;
    }
    if (!(scaled < static_cast<double>(plan.bins)))
    {
        return plan.bins - 1u;
    }
    return static_cast<std::uint64_t>(scaled);
}

/// The narrowing pass one partition can afford without holding a further counter.
///
/// The descent already owns `chunkCount * kOrderDigitBuckets` counters. Spending them as fewer,
/// wider tables is what turns a digit of the key into a division of the values, which is the step
/// that actually separates samples: the top digits of an ordered key are the sign and the exponent,
/// and every sample of one image shares them.
///
/// @param chunkCount the descent's partition.
/// @param maximumChunks the scan-width ceiling chosen for this source shape.
/// @return the narrowing partition and its bin count, never costing more counters than the descent.
[[nodiscard]] NarrowingPlan narrowingPlan(std::uint64_t chunkCount, std::uint64_t maximumChunks) noexcept
{
    const std::uint64_t counters = chunkCount * kOrderDigitBuckets;
    const std::uint64_t chunks = (std::max)(std::uint64_t{1}, (std::min)(chunkCount, maximumChunks));
    // At least one bin, because `selectionValueBin` addresses `bins - 1` and a partition of no bins
    // would send it past the table. The division cannot reach zero, because the narrowing never uses
    // more chunks than the descent has and each of those owns a whole digit's counters; the clamp is
    // here so that the guarantee belongs to this function rather than to the constants that feed it.
    return NarrowingPlan{.chunks = chunks, .bins = (std::max)(std::uint64_t{1}, counters / chunks)};
}

/// Chooses wider tables for a large unmasked plane without exceeding the total scratch ceiling.
///
/// Small or masked planes reuse the digit tables. Large all-valid planes, packed or with padded
/// rows, spend the remaining bounded workspace on sixteen wider tables, avoiding another full read
/// when the middle bin would otherwise exceed the gather buffer. Table widths are rounded down to
/// whole kibicounters; the extrema, offsets and gather storage are reserved before any counter is
/// budgeted.
///
/// @param itemCount the number of samples in the plane.
/// @param chunkCount the fixed digit partition and offset capacity.
/// @param unmasked whether no value rule or mask selects among the samples.
/// @return the narrowing partition and table width.
[[nodiscard]] NarrowingPlan selectionNarrowingPlan(std::uint64_t itemCount, std::uint64_t chunkCount,
                                                   bool unmasked) noexcept
{
    if (!unmasked || itemCount < 2u * 1024u * 1024u)
        return narrowingPlan(chunkCount, unmasked ? kMaxContiguousNarrowingChunks : kMaxNarrowingChunks);
    const std::uint64_t chunks = (std::min)(chunkCount, std::uint64_t{16});
    const std::uint64_t reserved =
        chunkCount * (sizeof(SelectionExtremes) + sizeof(std::uint64_t)) + kMaxOrderGatherSamples * sizeof(double);
    const std::uint64_t counters = (kMaxHistogramPartialBytes - reserved) / sizeof(std::uint64_t);
    return NarrowingPlan{.chunks = chunks, .bins = counters / chunks / 1024u * 1024u};
}

/// Which bin of a closed histogram interval one value falls in.
///
/// The histogram's own mapping, `floor((value - lowest) / (highest - lowest) * bins)`: the lower
/// endpoint lands in the first bin and the upper endpoint in the last, so the last bin alone is
/// closed at the top. The order selection narrows with `selectionValueBin` instead, which multiplies
/// by a precomputed reciprocal; the two need not agree with each other, only each with itself.
///
/// @param ordered the value being placed.
/// @param lowest the interval's inclusive lower endpoint.
/// @param highest the interval's inclusive upper endpoint.
/// @param bins how many bins divide the interval. At least one, because the histogram validates a
///        nonempty span.
/// @return the bin, always below `bins`.
[[nodiscard]] std::uint64_t valueBin(double ordered, double lowest, double highest, std::uint64_t bins) noexcept
{
    if (ordered <= lowest)
    {
        return 0u;
    }
    if (ordered >= highest)
    {
        return bins - 1u;
    }
    double fraction = 0.0;
    const double span = highest - lowest;
    if (std::isfinite(span))
    {
        fraction = (ordered - lowest) / span;
    }
    else if (std::isfinite(lowest) && std::isfinite(highest))
    {
        // Opposite-sign finite extremes can have an infinite difference. Scaling all three values
        // first keeps the same ratio in [-1, 1] and makes both subtractions finite.
        const double magnitude = (std::max)(std::abs(lowest), std::abs(highest));
        const double normalizedLow = lowest / magnitude;
        fraction = (ordered / magnitude - normalizedLow) / (highest / magnitude - normalizedLow);
    }
    const double scaled = fraction * static_cast<double>(bins);
    if (!(scaled > 0.0))
    {
        return 0u;
    }
    if (!(scaled < static_cast<double>(bins)))
    {
        return bins - 1u;
    }
    return static_cast<std::uint64_t>(scaled);
}

/// Counts the selected samples of one representation into bins of a stated interval.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param deviation whether to order absolute deviations from `center` rather than the samples.
/// @param center the value deviations are taken around.
/// @param binPlan the interval and precomputed mapping into its bins.
/// @param plan the narrowing partition and bin count.
/// @param partials `plan.chunks * plan.bins` counters, already zeroed.
/// @param collectEndpoints whether a narrowed interval concentrates samples in its end bins.
/// @param refinedExtremes optional per-chunk extrema of values inside the refined closed interval.
/// @return success, or the first failure in chunk order.
template <ResidentSample SampleT>
[[nodiscard]] Status countValueBins(const ExecutionContext& execution,
                                    const plane_detail::PlaneSampleReader<SampleT>& reader, bool deviation,
                                    double center, const SelectionBinPlan& binPlan, NarrowingPlan plan,
                                    std::uint64_t* partials, bool collectEndpoints, SelectionExtremes* refinedExtremes)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    // These passes narrow a rank the sampled bracket missed, so every one reads a sample as held through the hold's
    // whole real line when none is stated, which reads a finite value as it is stored.
    const double holdLowest = reader.holdLowest();
    const double holdHighest = reader.holdHighest();
    if (reader.contiguousUnmaskedValues())
    {
        const SampleT* const samples = reader.contiguousSamples();
        const auto scan = [&]<bool windowed>() -> Status {
            if (collectEndpoints)
            {
                // Most samples of a refined interval land in its two end bins. Count those in
                // registers instead of forming a dependent histogram increment for every sample.
                return parallelFor(execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
                                   [&](TaskRange chunkIndices) -> Status {
                                       for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                                       {
                                           std::uint64_t* const bins = partials + chunk * plan.bins;
                                           const TaskRange range =
                                               plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                                           std::uint64_t below = 0u;
                                           std::uint64_t above = 0u;
                                           SelectionExtremes seen;
                                           for (std::uint64_t index = range.begin; index < range.end; ++index)
                                           {
                                               double value = static_cast<double>(samples[index]);
                                               if constexpr (windowed)
                                               {
                                                   if (!reader.valueWindowAllows(value))
                                                       continue;
                                               }
                                               value = (std::min)((std::max)(value, holdLowest), holdHighest);
                                               const double ordered = deviation ? std::abs(value - center) : value;
                                               const bool low = ordered <= binPlan.lowest;
                                               const bool high = ordered >= binPlan.highest;
                                               if ((ordered >= binPlan.lowest) & (ordered <= binPlan.highest))
                                               {
                                                   countRefinedValue(seen, ordered);
                                               }
                                               below += static_cast<std::uint64_t>(low);
                                               above += static_cast<std::uint64_t>(high);
                                               if (!(low | high))
                                                   ++bins[selectionValueBin(ordered, binPlan)];
                                           }
                                           bins[0] += below;
                                           bins[plan.bins - 1u] += above;
                                           refinedExtremes[chunk] = seen;
                                       }
                                       return Status::success();
                                   });
            }
            return parallelFor(
                execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
                [&](TaskRange chunkIndices) -> Status {
                    for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                    {
                        std::uint64_t* const bins = partials + chunk * plan.bins;
                        const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                        if (binPlan.binScale > 0.0 && std::isfinite(binPlan.binScale) &&
                            std::isfinite(binPlan.highest - binPlan.lowest))
                        {
                            // The extrema pass proves that every value is in this interval.
                            // Its finite scale therefore maps into [0, bins], with the upper
                            // endpoint clamped to the final bin. No unsigned-64 conversion or
                            // per-sample lower-bound branch is needed. Refinement has its own
                            // endpoint path above because it sees values outside its interval.
                            const auto last = static_cast<std::uint32_t>(binPlan.bins - 1u);
                            if (deviation)
                            {
                                for (std::uint64_t index = range.begin; index < range.end; ++index)
                                {
                                    double value = static_cast<double>(samples[index]);
                                    if constexpr (windowed)
                                    {
                                        if (!reader.valueWindowAllows(value))
                                            continue;
                                    }
                                    value = (std::min)((std::max)(value, holdLowest), holdHighest);
                                    const double ordered = std::abs(value - center);
                                    const auto bin =
                                        static_cast<std::uint32_t>((ordered - binPlan.lowest) * binPlan.binScale);
                                    ++bins[(std::min)(bin, last)];
                                }
                            }
                            else
                            {
                                for (std::uint64_t index = range.begin; index < range.end; ++index)
                                {
                                    double value = static_cast<double>(samples[index]);
                                    if constexpr (windowed)
                                    {
                                        if (!reader.valueWindowAllows(value))
                                            continue;
                                    }
                                    value = (std::min)((std::max)(value, holdLowest), holdHighest);
                                    const auto bin =
                                        static_cast<std::uint32_t>((value - binPlan.lowest) * binPlan.binScale);
                                    ++bins[(std::min)(bin, last)];
                                }
                            }
                        }
                        else
                        {
                            if (deviation)
                            {
                                for (std::uint64_t index = range.begin; index < range.end; ++index)
                                {
                                    double value = static_cast<double>(samples[index]);
                                    if constexpr (windowed)
                                    {
                                        if (!reader.valueWindowAllows(value))
                                            continue;
                                    }
                                    value = (std::min)((std::max)(value, holdLowest), holdHighest);
                                    const double ordered = std::abs(value - center);
                                    ++bins[selectionValueBin(ordered, binPlan)];
                                }
                            }
                            else
                            {
                                for (std::uint64_t index = range.begin; index < range.end; ++index)
                                {
                                    double value = static_cast<double>(samples[index]);
                                    if constexpr (windowed)
                                    {
                                        if (!reader.valueWindowAllows(value))
                                            continue;
                                    }
                                    value = (std::min)((std::max)(value, holdLowest), holdHighest);
                                    ++bins[selectionValueBin(value, binPlan)];
                                }
                            }
                        }
                    }
                    return Status::success();
                });
        };
        return reader.contiguousAllValid() ? scan.template operator()<false>() : scan.template operator()<true>();
    }
    // A first pass counts inside the interval the extrema pass proved, which the contiguous scans above map without
    // bounds. A masked or strided selection holds the same values, so it maps them the same way; a refinement sees
    // values outside its interval and keeps the general mapping.
    const bool proven = !collectEndpoints && binPlan.binScale > 0.0 && std::isfinite(binPlan.binScale) &&
                        std::isfinite(binPlan.highest - binPlan.lowest);
    const auto last = static_cast<std::uint32_t>(binPlan.bins - 1u);
    const std::uint32_t columnStride = reader.columnStride();
    const auto walk = [&]<bool mapped>() -> Status {
        return parallelFor(
            execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
            [&](TaskRange chunkIndices) -> Status {
                for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                {
                    std::uint64_t* const bins = partials + chunk * plan.bins;
                    SelectionExtremes seen;
                    const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                    std::uint64_t index = range.begin;
                    while (index < range.end)
                    {
                        const RowRun run = nextRun(index, range.end, width);
                        const SampleT* const row = reader.rowStart(run.y);
                        plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                        const bool walked = validityBits.forEachAllowed(run.length, [&](std::uint64_t offset) {
                            const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                            double value = 0.0;
                            if (!reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                            {
                                return true;
                            }
                            const double ordered = deviation ? std::abs(value - center) : value;
                            if constexpr (mapped)
                            {
                                const auto bin =
                                    static_cast<std::uint32_t>((ordered - binPlan.lowest) * binPlan.binScale);
                                ++bins[(std::min)(bin, last)];
                            }
                            else
                            {
                                if (refinedExtremes != nullptr &&
                                    ((ordered >= binPlan.lowest) & (ordered <= binPlan.highest)))
                                {
                                    countRefinedValue(seen, ordered);
                                }
                                ++bins[selectionValueBin(ordered, binPlan)];
                            }
                            return true;
                        });
                        static_cast<void>(walked);
                    }
                    if (refinedExtremes != nullptr)
                        refinedExtremes[chunk] = seen;
                }
                return Status::success();
            });
    };
    return proven ? walk.template operator()<true>() : walk.template operator()<false>();
}

/// An exact closed value interval for a bin, or an unrestricted fallback.
struct SelectionBinBounds
{
    /// First value mapping into the bin.
    double first = -std::numeric_limits<double>::infinity();
    /// Last value mapping into the bin.
    double last = std::numeric_limits<double>::infinity();
    /// Whether the interval alone decides membership.
    bool exact = false;
};

/// Finds a bin's boundaries in the monotone mapping's actual floating-point arithmetic.
///
/// Inverting the mathematical scale can round a boundary past a value the histogram counted.
/// Searching ordered Float64 keys instead finds the exact first and last representable values,
/// so a gather can compare two bounds without recomputing a bin for every input sample.
/// End bins include values beyond the interval because the counting map clamps them there.
///
/// @param plan the counting pass's mapping.
/// @param bin the bin whose samples will be gathered.
/// @return exact bounds for a finite scale and interval, or an unrestricted fallback.
[[nodiscard]] SelectionBinBounds selectionBinBounds(const SelectionBinPlan& plan, std::uint64_t bin) noexcept
{
    if (!(plan.binScale > 0.0) || !std::isfinite(plan.binScale) || !std::isfinite(plan.highest))
        return {};
    const auto firstKey = [&](std::uint64_t target) noexcept {
        std::uint64_t low = orderKey(plan.lowest);
        std::uint64_t high = orderKey(plan.highest);
        while (low < high)
        {
            const std::uint64_t middle = low + (high - low) / 2u;
            if (selectionValueBin(keyValue(middle), plan) < target)
                low = middle + 1u;
            else
                high = middle;
        }
        return low;
    };
    return SelectionBinBounds{.first = bin == 0u ? -std::numeric_limits<double>::infinity() : keyValue(firstKey(bin)),
                              .last = bin + 1u == plan.bins ? std::numeric_limits<double>::infinity()
                                                            : keyValue(firstKey(bin + 1u) - 1u),
                              .exact = true};
}

/// A bin's bounds held the way this compiler tests a value against them fastest, as one branch.
///
/// Both ways admit exactly the same values. Two floating comparisons joined by `&` are split by Clang
/// into two branches, and the first - below the bin or not - is taken for about half the samples of a
/// central bin, in no order a predictor can follow; that cost a gather most of its time on bench-linux.
/// Under Clang the bounds are therefore ordered keys: a key compares as its value does once a negative
/// zero is taken as the positive zero it equals, which adding zero does, and a not-a-number keys beyond
/// both infinities, so one unsigned comparison admits exactly the values the two comparisons admit.
/// Microsoft's compiler keeps the two comparisons as one branch. There the keys gained the contiguous
/// walks nothing and cost the masked and strided walk about a sixth more on bench-windows, so it keeps
/// the bounds as values. A bin whose last bound lies below its first holds nothing, and the gather
/// skips it.
struct SelectionBinMembership
{
#if defined(_MSC_VER) && !defined(__clang__)
    /// The bin's first value.
    double first = 0.0;

    /// The bin's last value.
    double last = 0.0;
#else
    /// The key of the bin's first value.
    std::uint64_t first = 0u;

    /// How far the key of its last value lies beyond that.
    std::uint64_t span = 0u;
#endif

    /// Whether a value lies in the bin's bounds.
    ///
    /// @param value the value.
    /// @return true when it lies between the bounds, both included.
    [[nodiscard]] bool contains(double value) const noexcept
    {
#if defined(_MSC_VER) && !defined(__clang__)
        return (value >= first) & (value <= last);
#else
        return orderKey(value + 0.0) - first <= span;
#endif
    }
};

/// The membership test of a bin's bounds.
///
/// @param bounds the bounds, the first at or below the last.
/// @return their membership test.
[[nodiscard]] SelectionBinMembership binMembershipOf(const SelectionBinBounds& bounds) noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    return SelectionBinMembership{.first = bounds.first, .last = bounds.last};
#else
    const std::uint64_t first = orderKey(bounds.first + 0.0);
    return SelectionBinMembership{.first = first, .span = orderKey(bounds.last + 0.0) - first};
#endif
}

/// How many consecutive samples a scan tests together before acting on any of them.
///
/// Testing a block - which samples lie in an interval, the counts kept beside it and the smallest
/// value above it - takes no branch and stores nothing, so the compiler turns it into vector
/// instructions: sixteen Float64 values are four AVX2 registers. The few samples a block has to act
/// on are then taken one at a time, which is what a bin a gather wants or a bracket's values are in
/// a plane of millions.
constexpr std::size_t kScanBlock = 16u;
static_assert(kScanBlock == kOrderCountBlock, "the compiled bracket count discards into the same counters");

/// Widens one stored sample and orders it as a selection does.
///
/// @tparam deviation whether the order is over absolute deviations from `center`.
/// @tparam holds whether the sample is read as held to `hold` first.
/// @tparam SampleT the resident type.
/// @param stored the stored sample.
/// @param center the value deviations are taken around.
/// @param hold the interval an admitted sample is read as held to, read only when `holds`.
/// @return the value or its deviation.
template <bool deviation, bool holds, ResidentSample SampleT>
[[nodiscard]] double orderedSample(SampleT stored, double center, const ScanInterval& hold) noexcept
{
    double value = static_cast<double>(stored);
    if constexpr (holds)
        value = (std::min)((std::max)(value, hold.lowest), hold.highest);
    if constexpr (deviation)
        return std::abs(value - center);
    else
        return value;
}

/// Gathers one bin's members from consecutive samples, and the smallest value above the bin.
///
/// Membership is the closed interval `[first, last]`, which is the bin when its bounds are exact.
/// The smallest value above it is kept per position in a block, so that each update is an element of
/// a vector minimum rather than a step of a scalar chain; `nextAbove` holds those minima. Under a value
/// window a sample whose stored value lies outside it - where a value that is not a number always lies - is
/// neither a member nor a candidate, decided in the same lanes as the membership.
///
/// @tparam deviation whether the order is over absolute deviations from `center`.
/// @tparam tracked whether the smallest value above the bin is wanted.
/// @tparam windowed whether a value window admits the stored values.
/// @tparam holds whether an admitted sample is read as held to `hold`.
/// @tparam SampleT the resident type.
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param center the value deviations are taken around.
/// @param first the bin's first value.
/// @param last its last value.
/// @param gathered where members go.
/// @param end the end of the bin's place in `gathered`; no member is written at or past it.
/// @param write the next position in `gathered`, advanced past every member, written or not.
/// @param nextAbove the running minima above `last`, one per position in a block.
/// @param window the value window, read only when `windowed`.
/// @param hold the hold, read only when `holds`.
template <bool deviation, bool tracked, bool windowed, bool holds, ResidentSample SampleT>
void gatherConsecutive(const SampleT* samples, std::uint64_t length, double center, double first, double last,
                       double* gathered, std::uint64_t end, std::uint64_t& write,
                       std::array<double, kScanBlock>& nextAbove, const ScanInterval& window,
                       const ScanInterval& hold) noexcept
{
    constexpr double kNone = std::numeric_limits<double>::infinity();
    const auto admitted = [&](SampleT stored) noexcept -> bool {
        if constexpr (windowed)
        {
            const double value = static_cast<double>(stored);
            return (value >= window.lowest) & (value <= window.highest);
        }
        else
        {
            static_cast<void>(stored);
            return true;
        }
    };
    std::uint64_t index = 0u;
    for (; index + kScanBlock <= length; index += kScanBlock)
    {
        bool any = false;
        for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
        {
            const bool inside = admitted(samples[index + lane]);
            const double ordered = orderedSample<deviation, holds>(samples[index + lane], center, hold);
            any |= inside & (ordered >= first) & (ordered <= last);
            if constexpr (tracked)
            {
                const double candidate = (inside & (ordered > last)) ? ordered : kNone;
                nextAbove[lane] = candidate < nextAbove[lane] ? candidate : nextAbove[lane];
            }
        }
        if (any)
        {
            for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
            {
                const bool inside = admitted(samples[index + lane]);
                const double ordered = orderedSample<deviation, holds>(samples[index + lane], center, hold);
                if (inside & (ordered >= first) & (ordered <= last))
                {
                    if (write < end)
                        gathered[write] = ordered;
                    ++write;
                }
            }
        }
    }
    for (; index < length; ++index)
    {
        if (!admitted(samples[index]))
            continue;
        const double ordered = orderedSample<deviation, holds>(samples[index], center, hold);
        if ((ordered >= first) & (ordered <= last))
        {
            if (write < end)
                gathered[write] = ordered;
            ++write;
        }
        else if (tracked && ordered > last && ordered < nextAbove[0])
            nextAbove[0] = ordered;
    }
}

/// Counts consecutive samples below, inside and above a bracket, the inside ones into its bins.
///
/// Every sample of a block is given a counter to increment: its bin, from its value held to the
/// bracket so the conversion stays in range, or, outside the bracket, one of `kScanBlock` counters
/// past the bracket's bins that nothing reads. Neither step branches on whether a sample is inside,
/// which about one sample in ten is, in no order a predictor can follow, and no two samples of a
/// block that lie outside the bracket increment the same counter one after the other.
///
/// @tparam deviation whether the order is over absolute deviations from `center`.
/// @tparam holds whether a sample is read as held to `hold`.
/// @tparam SampleT the resident type.
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param center the value deviations are taken around.
/// @param binPlan the bracket and its mapping, whose scale is finite and positive.
/// @param bins the chunk's counters: `binPlan.bins` bins, then `kScanBlock` discarded counters.
/// @param below the running count below the bracket.
/// @param above the running count above it.
/// @param hold the hold, read only when `holds`.
template <bool deviation, bool holds, ResidentSample SampleT>
void countConsecutive(const SampleT* samples, std::uint64_t length, double center, const SelectionBinPlan& binPlan,
                      std::uint64_t* bins, std::uint64_t& below, std::uint64_t& above,
                      const ScanInterval& hold) noexcept
{
    const double lowest = binPlan.lowest;
    const double highest = binPlan.highest;
    const double scale = binPlan.binScale;
    const auto last = static_cast<std::int32_t>(binPlan.bins - 1u);
    const auto discarded = static_cast<std::int32_t>(binPlan.bins);
    std::uint64_t lowCount = 0u;
    std::uint64_t highCount = 0u;
    std::uint64_t index = 0u;
    for (; index + kScanBlock <= length; index += kScanBlock)
    {
        std::array<std::int32_t, kScanBlock> target{};
        for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
        {
            const double ordered = orderedSample<deviation, holds>(samples[index + lane], center, hold);
            const bool low = ordered < lowest;
            const bool high = ordered > highest;
            lowCount += static_cast<std::uint64_t>(low);
            highCount += static_cast<std::uint64_t>(high);
            const double bracketed = (std::min)((std::max)(ordered, lowest), highest);
            const auto bin = static_cast<std::int32_t>((bracketed - lowest) * scale);
            target[lane] = (low | high) ? discarded + static_cast<std::int32_t>(lane) : (bin < last ? bin : last);
        }
        for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
            ++bins[target[lane]];
    }
    for (; index < length; ++index)
    {
        const double ordered = orderedSample<deviation, holds>(samples[index], center, hold);
        lowCount += static_cast<std::uint64_t>(ordered < lowest);
        highCount += static_cast<std::uint64_t>(ordered > highest);
        if ((ordered >= lowest) & (ordered <= highest))
        {
            const auto bin = static_cast<std::int32_t>((ordered - lowest) * scale);
            ++bins[bin < last ? bin : last];
        }
    }
    below += lowCount;
    above += highCount;
}

/// Copies every selected sample of one bin into one bounded buffer.
///
/// With exact bounds the same read also finds the smallest value above the bin, which is the next
/// rank whenever the rank being gathered is the bin's last: every value at or below the bin's last
/// value is in the bin or below it, so the first value past it is the next one in the order.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param deviation whether to order absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param binPlan the interval and precomputed mapping into its bins.
/// @param plan the narrowing partition and bin count.
/// @param bin which bin to gather.
/// @param bounds the values the bin holds: `selectionBinBounds` of it, or narrower where a bracket
///        excludes what its end bins would otherwise take.
/// @param offsets one write offset per narrowing chunk.
/// @param matching how many members the count found in the bin over every chunk, which is where the last chunk's
///        members end.
/// @param gathered where the values go.
/// @param successors optional, one record per narrowing chunk, whose `nextAbove` receives the smallest
///        value above `bounds.last`; filled only when the bounds are exact.
/// @param operation the entry point a failure raised here belongs to.
/// @return success, or the first failure in chunk order; `ErrorCode::Internal` when a chunk gathered other than the
///         members its count counted. No chunk writes past the slot its count sized: a member past it is counted and
///         not written.
template <ResidentSample SampleT>
[[nodiscard]] Status
gatherValueBin(const ExecutionContext& execution, const plane_detail::PlaneSampleReader<SampleT>& reader,
               bool deviation, double center, const SelectionBinPlan& binPlan, NarrowingPlan plan, std::uint64_t bin,
               const SelectionBinBounds& bounds, const std::uint64_t* offsets, std::uint64_t matching, double* gathered,
               SelectionExtremes* successors, const char* operation)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    if (!(bounds.first <= bounds.last))
    {
        return Status::success();
    }
    const SelectionBinMembership inBounds = binMembershipOf(bounds);
    const bool follows = successors != nullptr && bounds.exact;
    const double lastBound = bounds.last;
    const std::uint32_t columnStride = reader.columnStride();
    if (bounds.exact && reader.unmaskedValues() && columnStride == 1u)
    {
        // Every sample is selected, but for a value window the block scan decides in its lanes, and a row's samples
        // are consecutive, so a chunk is read as runs of consecutive samples, the whole chunk at once when the rows
        // are packed - by the block loop, which keeps a plain selection in lanes on its own, and by the compiled
        // gather where the execution's table has one for a selection a window or a hold takes out of them.
        const bool packed = reader.contiguousUnmaskedValues();
        const auto [window, hold] = scanIntervalsOf(reader);
        OrderGatherFloat32RunFn compiled = nullptr;
        if constexpr (std::is_same_v<SampleT, float>)
        {
            compiled = reader.hasValueWindow() || reader.hasHold() ? orderGatherFloat32RunFor(execution) : nullptr;
        }
        const OrderRead read{.windowLowest = window.lowest,
                             .windowHighest = window.highest,
                             .holdLowest = hold.lowest,
                             .holdHighest = hold.highest};
        const auto scan = [&]<bool ofDeviation, bool tracked, bool windowed, bool holds>() -> Status {
            return parallelFor(
                execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
                [&](TaskRange chunkIndices) -> Status {
                    for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                    {
                        std::uint64_t write = offsets[chunk];
                        const std::uint64_t slotEnd = chunk + 1u < plan.chunks ? offsets[chunk + 1u] : matching;
                        std::array<double, kScanBlock> nextAbove{};
                        nextAbove.fill(std::numeric_limits<double>::infinity());
                        const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                        const auto run = [&](const SampleT* samples, std::uint64_t length) {
                            if constexpr (std::is_same_v<SampleT, float>)
                            {
                                if (compiled != nullptr)
                                {
                                    compiled(samples, length, ofDeviation, center, read, bounds.first, lastBound,
                                             gathered, slotEnd, write, tracked ? nextAbove.data() : nullptr);
                                    return;
                                }
                            }
                            gatherConsecutive<ofDeviation, tracked, windowed, holds>(
                                samples, length, center, bounds.first, lastBound, gathered, slotEnd, write, nextAbove,
                                window, hold);
                        };
                        if (packed)
                        {
                            run(reader.contiguousSamples() + range.begin, range.end - range.begin);
                        }
                        else
                        {
                            std::uint64_t index = range.begin;
                            while (index < range.end)
                            {
                                const RowRun rowRun = nextRun(index, range.end, width);
                                run(reader.rowStart(rowRun.y) + rowRun.x, rowRun.length);
                            }
                        }
                        // The count sized each chunk's slot; a gather that disagrees with it counted past the slot,
                        // though it wrote nothing there, and its members cannot be ranked.
                        if (write != slotEnd)
                        {
                            return orderFailure(ErrorCode::Internal, operation,
                                                "a bin's gather wrote other than the members its count counted");
                        }
                        if constexpr (tracked)
                        {
                            double next = std::numeric_limits<double>::infinity();
                            for (const double candidate : nextAbove)
                                next = candidate < next ? candidate : next;
                            successors[chunk].nextAbove = next;
                        }
                    }
                    return Status::success();
                });
        };
        const auto scanThrough = [&]<bool windowed, bool holds>() -> Status {
            if (deviation)
            {
                return follows ? scan.template operator()<true, true, windowed, holds>()
                               : scan.template operator()<true, false, windowed, holds>();
            }
            return follows ? scan.template operator()<false, true, windowed, holds>()
                           : scan.template operator()<false, false, windowed, holds>();
        };
        if (reader.hasHold())
        {
            return reader.hasValueWindow() ? scanThrough.template operator()<true, true>()
                                           : scanThrough.template operator()<false, true>();
        }
        return reader.hasValueWindow() ? scanThrough.template operator()<true, false>()
                                       : scanThrough.template operator()<false, false>();
    }
    if (reader.contiguousAllValid())
    {
        const SampleT* const samples = reader.contiguousSamples();
        const double holdLowest = reader.holdLowest();
        const double holdHighest = reader.holdHighest();
        return parallelFor(
            execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
            [&](TaskRange chunkIndices) -> Status {
                for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                {
                    std::uint64_t write = offsets[chunk];
                    const std::uint64_t slotEnd = chunk + 1u < plan.chunks ? offsets[chunk + 1u] : matching;
                    const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                    for (std::uint64_t index = range.begin; index < range.end; ++index)
                    {
                        const double value =
                            (std::min)((std::max)(static_cast<double>(samples[index]), holdLowest), holdHighest);
                        const double ordered = deviation ? std::abs(value - center) : value;
                        if (inBounds.contains(ordered) && (bounds.exact || selectionValueBin(ordered, binPlan) == bin))
                        {
                            if (write < slotEnd)
                                gathered[write] = ordered;
                            ++write;
                        }
                    }
                    if (write != slotEnd)
                    {
                        return orderFailure(ErrorCode::Internal, operation,
                                            "a bin's gather wrote other than the members its count counted");
                    }
                }
                return Status::success();
            });
    }
    // The smallest value above the bin is kept as an ordered key, so that following it is an integer select and
    // minimum rather than a branch: about half the samples a central bin's gather reads lie above the bin, in no order
    // a predictor can follow, and a branch on that cost the masked gather most of its time.
    const std::uint64_t lastKey = orderKey(lastBound + 0.0);
    const auto walk = [&]<bool tracked>() -> Status {
        return parallelFor(
            execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
            [&](TaskRange chunkIndices) -> Status {
                for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                {
                    std::uint64_t write = offsets[chunk];
                    const std::uint64_t slotEnd = chunk + 1u < plan.chunks ? offsets[chunk + 1u] : matching;
                    std::uint64_t nextKey = ~std::uint64_t{0};
                    const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                    std::uint64_t index = range.begin;
                    while (index < range.end)
                    {
                        const RowRun run = nextRun(index, range.end, width);
                        const SampleT* const row = reader.rowStart(run.y);
                        plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                        const bool walked = validityBits.forEachAllowed(run.length, [&](std::uint64_t offset) {
                            const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                            double value = 0.0;
                            if (!reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                            {
                                return true;
                            }
                            const double ordered = deviation ? std::abs(value - center) : value;
                            if (inBounds.contains(ordered) &&
                                (bounds.exact || selectionValueBin(ordered, binPlan) == bin))
                            {
                                if (write < slotEnd)
                                    gathered[write] = ordered;
                                ++write;
                            }
                            if constexpr (tracked)
                            {
                                const std::uint64_t key = orderKey(ordered + 0.0);
                                const std::uint64_t candidate = key > lastKey ? key : ~std::uint64_t{0};
                                nextKey = candidate < nextKey ? candidate : nextKey;
                            }
                            return true;
                        });
                        static_cast<void>(walked);
                    }
                    if (write != slotEnd)
                    {
                        return orderFailure(ErrorCode::Internal, operation,
                                            "a bin's gather wrote other than the members its count counted");
                    }
                    if constexpr (tracked)
                        successors[chunk].nextAbove =
                            nextKey == ~std::uint64_t{0} ? std::numeric_limits<double>::infinity() : keyValue(nextKey);
                }
                return Status::success();
            });
    };
    return follows ? walk.template operator()<true>() : walk.template operator()<false>();
}

/// The smallest value above a gathered bin, from the records its gather filled.
///
/// @param successors one record per narrowing chunk.
/// @param chunks how many chunks the gather ran.
/// @return the smallest of their `nextAbove`, an infinity when no value lies above the bin.
[[nodiscard]] double nextAboveGatheredBin(const SelectionExtremes* successors, std::uint64_t chunks) noexcept
{
    double next = std::numeric_limits<double>::infinity();
    for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
        next = successors[chunk].nextAbove < next ? successors[chunk].nextAbove : next;
    return next;
}

/// Counts the samples below, inside and above a bracket of values, the inside ones into its bins.
///
/// The bracket is read from a sample of the plane and holds the rank being sought, so its bins
/// divide the values near that rank finely enough to gather one in a single further read. Values
/// outside it are only counted, in registers, which is what keeps a concentrated sky from landing
/// one increment after another on the same counter of a table spread over the plane's whole range.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component, every sample of which is selected but for its value interval.
/// @param deviation whether to order absolute deviations from `center` rather than the samples.
/// @param center the value deviations are taken around.
/// @param binPlan the bracket and its mapping, whose scale is finite and positive, into at most
///        `plan.bins - kScanBlock` bins.
/// @param plan the narrowing partition and table width.
/// @param partials `plan.chunks * plan.bins` counters, already zeroed: each table's bracket bins, then
///        the counters `countConsecutive` discards into.
/// @param tallies one record per narrowing chunk, receiving the counts below and above the bracket.
/// @return success, or the first failure in chunk order.
template <ResidentSample SampleT>
[[nodiscard]] Status countBracketBins(const ExecutionContext& execution,
                                      const plane_detail::PlaneSampleReader<SampleT>& reader, bool deviation,
                                      double center, const SelectionBinPlan& binPlan, NarrowingPlan plan,
                                      std::uint64_t* partials, SelectionExtremes* tallies)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint32_t columnStride = reader.columnStride();
    const bool packed = reader.contiguousUnmaskedValues();
    const auto last = static_cast<std::uint32_t>(binPlan.bins - 1u);
    const double lowest = binPlan.lowest;
    const double highest = binPlan.highest;
    const double scale = binPlan.binScale;
    // A compiled count where the execution's table has one: the same counters as the block loop below.
    OrderCountFloat32RunFn compiled = nullptr;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        compiled = orderCountFloat32RunFor(execution);
    }
    const OrderBracket bracket{.lowest = lowest,
                               .highest = highest,
                               .scale = scale,
                               .bins = binPlan.bins,
                               .windowLowest = reader.valueWindowLowest(),
                               .windowHighest = reader.valueWindowHighest(),
                               .holdLowest = reader.holdLowest(),
                               .holdHighest = reader.holdHighest()};
    const ScanInterval hold{.lowest = bracket.holdLowest, .highest = bracket.holdHighest};
    const bool holds = reader.hasHold();
    const auto pass = [&]<bool windowed>() -> Status {
        return parallelFor(
            execution, plan.chunks, chunkCostOf<SampleT>(itemCount, plan.chunks),
            [&](TaskRange chunkIndices) -> Status {
                for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
                {
                    std::uint64_t* const bins = partials + chunk * plan.bins;
                    std::uint64_t below = 0u;
                    std::uint64_t above = 0u;
                    // An inside value maps as `selectionValueBin` maps it: its scaled offset lies in
                    // [0, bins], and only the upper end needs clamping into the last bin.
                    const auto place = [&](SampleT stored) {
                        double value = static_cast<double>(stored);
                        if constexpr (windowed)
                        {
                            if (!reader.valueWindowAllows(value))
                                return;
                        }
                        value = (std::min)((std::max)(value, hold.lowest), hold.highest);
                        const double ordered = deviation ? std::abs(value - center) : value;
                        below += static_cast<std::uint64_t>(ordered < lowest);
                        above += static_cast<std::uint64_t>(ordered > highest);
                        if ((ordered >= lowest) & (ordered <= highest))
                        {
                            const auto bin = static_cast<std::uint32_t>((ordered - lowest) * scale);
                            ++bins[(std::min)(bin, last)];
                        }
                    };
                    const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, plan.chunks);
                    // A row of consecutive samples is counted a block at a time: outside any value window by the block
                    // loop, and inside one too by the compiled count, which takes the window.
                    const bool blocks = !windowed || compiled != nullptr;
                    const auto consecutive = [&](const SampleT* samples, std::uint64_t length) {
                        if constexpr (std::is_same_v<SampleT, float>)
                        {
                            if (compiled != nullptr)
                            {
                                compiled(samples, length, deviation, center, bracket, bins, below, above);
                                return;
                            }
                        }
                        if (deviation)
                        {
                            if (holds)
                                countConsecutive<true, true>(samples, length, center, binPlan, bins, below, above,
                                                             hold);
                            else
                                countConsecutive<true, false>(samples, length, center, binPlan, bins, below, above,
                                                              hold);
                        }
                        else
                        {
                            if (holds)
                                countConsecutive<false, true>(samples, length, center, binPlan, bins, below, above,
                                                              hold);
                            else
                                countConsecutive<false, false>(samples, length, center, binPlan, bins, below, above,
                                                               hold);
                        }
                    };
                    if (packed && blocks)
                    {
                        consecutive(reader.contiguousSamples() + range.begin, range.end - range.begin);
                    }
                    else
                    {
                        std::uint64_t index = range.begin;
                        while (index < range.end)
                        {
                            const RowRun run = nextRun(index, range.end, width);
                            const SampleT* const row =
                                reader.rowStart(run.y) + static_cast<std::size_t>(run.x) * columnStride;
                            if (blocks && columnStride == 1u)
                            {
                                consecutive(row, run.length);
                                continue;
                            }
                            for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                                place(row[static_cast<std::size_t>(offset) * columnStride]);
                        }
                    }
                    tallies[chunk].belowBracket = below;
                    tallies[chunk].aboveBracket = above;
                }
                return Status::success();
            });
    };
    return reader.hasValueWindow() ? pass.template operator()<true>() : pass.template operator()<false>();
}

/// Counts one digit of the ordered key over the samples the fixed digits still admit.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param deviation whether to order absolute deviations from `center` rather than the samples.
/// @param center the value deviations are taken around.
/// @param prefix the digits already fixed.
/// @param shift the bit position of the digit being counted.
/// @param chunkCount the fixed partition.
/// @param partials `chunkCount * kOrderDigitBuckets` counters, already zeroed.
/// @return success, or the first failure in chunk order.
template <ResidentSample SampleT>
[[nodiscard]] Status
countDigit(const ExecutionContext& execution, const plane_detail::PlaneSampleReader<SampleT>& reader, bool deviation,
           double center, std::uint64_t prefix, std::uint32_t shift, std::uint64_t chunkCount, std::uint64_t* partials)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint64_t mask = fixedDigitMask(shift);
    const std::uint64_t fixed = prefix & mask;
    const std::uint32_t columnStride = reader.columnStride();
    return parallelFor(
        execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                std::uint64_t* const buckets = partials + chunk * kOrderDigitBuckets;
                const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const RowRun run = nextRun(index, range.end, width);
                    const SampleT* const row = reader.rowStart(run.y);
                    plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                    for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                    {
                        const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                        double value = 0.0;
                        if (!validityBits.allowsAndAdvance() ||
                            !reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                        {
                            continue;
                        }
                        const std::uint64_t key = orderKey(deviation ? std::abs(value - center) : value);
                        if ((key & mask) != fixed)
                        {
                            continue;
                        }
                        ++buckets[(key >> shift) & (kOrderDigitBuckets - 1u)];
                    }
                }
            }
            return Status::success();
        });
}

/// Copies every sample whose key matches the fixed digits into one bounded buffer.
///
/// Each chunk writes at the offset the per-chunk counts already fixed, so the buffer is filled in
/// chunk order however the chunks were scheduled; sorting afterwards makes the answer independent
/// of the order within a chunk as well.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param deviation whether to order absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param prefix every digit fixed so far, the one being gathered included.
/// @param shift the bit position of the last digit fixed.
/// @param chunkCount the fixed partition.
/// @param offsets one write offset per chunk.
/// @param matching how many samples the counts found matching over every chunk, which is where the last chunk's
///        samples end.
/// @param gathered where the values go.
/// @param operation the entry point a failure raised here belongs to.
/// @return success, or the first failure in chunk order; `ErrorCode::Internal` when a chunk gathered other than the
///         samples its count counted. No chunk writes past the slot its count sized: a sample past it is counted and
///         not written.
template <ResidentSample SampleT>
[[nodiscard]] Status
gatherMatching(const ExecutionContext& execution, const plane_detail::PlaneSampleReader<SampleT>& reader,
               bool deviation, double center, std::uint64_t prefix, std::uint32_t shift, std::uint64_t chunkCount,
               const std::uint64_t* offsets, std::uint64_t matching, double* gathered, const char* operation)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint64_t mask = shift == 0u ? ~std::uint64_t{0} : (~std::uint64_t{0} << shift);
    const std::uint64_t fixed = prefix & mask;
    const std::uint32_t columnStride = reader.columnStride();
    return parallelFor(
        execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                std::uint64_t write = offsets[chunk];
                const std::uint64_t slotEnd = chunk + 1u < chunkCount ? offsets[chunk + 1u] : matching;
                const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const RowRun run = nextRun(index, range.end, width);
                    const SampleT* const row = reader.rowStart(run.y);
                    plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                    for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                    {
                        const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                        double value = 0.0;
                        if (!validityBits.allowsAndAdvance() ||
                            !reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                        {
                            continue;
                        }
                        const double ordered = deviation ? std::abs(value - center) : value;
                        if ((orderKey(ordered) & mask) == fixed)
                        {
                            if (write < slotEnd)
                                gathered[write] = ordered;
                            ++write;
                        }
                    }
                }
                if (write != slotEnd)
                {
                    return orderFailure(ErrorCode::Internal, operation,
                                        "a bucket's gather wrote other than the samples its count counted");
                }
            }
            return Status::success();
        });
}

/// Everything one descent shares: the plane, the partition, and the scratch it descends with.
///
/// @tparam SampleT the resident type.
template <ResidentSample SampleT> struct SelectionState
{
    /// The operation.
    const ExecutionContext& execution;

    /// The checked component and its validity rule.
    const plane_detail::PlaneSampleReader<SampleT>& reader;

    /// The fixed partition, which every step of the selection reuses.
    std::uint64_t chunkCount = 0u;

    /// The histogram partition selected for this plane.
    NarrowingPlan narrowing;

    /// Reused extrema storage, one record per deterministic chunk.
    SelectionExtremes* extremes = nullptr;

    /// Counters shared by narrowing and digit steps.
    std::uint64_t* partials = nullptr;

    /// One write offset per chunk, reused by every gather.
    std::uint64_t* offsets = nullptr;

    /// Up to `kMaxOrderGatherSamples` values, reused by every gather.
    double* gathered = nullptr;

    /// How many times the plane has been read.
    std::uint64_t scans = 0u;

    /// The entry point a failure raised below here belongs to. Never null.
    const char* operation = nullptr;
};

/// Resolves one zero-based rank exactly, holding nothing proportional to the sample count.
///
/// @tparam SampleT the resident type.
/// @param state the plane, partition and scratch.
/// @param deviation whether the order is over absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param rank the zero-based rank to resolve, below the selected count.
/// @param value where the selected value goes.
/// @return success, or an execution or cancellation failure.
template <ResidentSample SampleT>
[[nodiscard]] Status selectRank(SelectionState<SampleT>& state, bool deviation, double center, std::uint64_t rank,
                                double& value)
{
    std::uint64_t prefix = 0u;
    std::uint64_t below = 0u;
    for (std::uint32_t digit = 0u; digit < kOrderDigits; ++digit)
    {
        const std::uint32_t shift = 64u - kOrderDigitBits * (digit + 1u);
        std::fill_n(state.partials, static_cast<std::size_t>(state.chunkCount * kOrderDigitBuckets), std::uint64_t{0});
        Status status = countDigit(state.execution, state.reader, deviation, center, prefix, shift, state.chunkCount,
                                   state.partials);
        if (status.failed())
        {
            return status;
        }
        ++state.scans;

        std::uint64_t bucket = 0u;
        std::uint64_t cumulative = 0u;
        for (; bucket < kOrderDigitBuckets; ++bucket)
        {
            std::uint64_t total = 0u;
            for (std::uint64_t chunk = 0u; chunk < state.chunkCount; ++chunk)
            {
                total += state.partials[chunk * kOrderDigitBuckets + bucket];
            }
            if (below + cumulative + total > rank)
            {
                break;
            }
            cumulative += total;
        }
        if (bucket == kOrderDigitBuckets)
        {
            return orderFailure(ErrorCode::Internal, state.operation,
                                "the order descent left no bucket holding the requested rank");
        }
        below += cumulative;
        prefix |= bucket << shift;

        // The chosen bucket's per-chunk counts are its gather offsets, so they are read out of the
        // per-chunk tables rather than from a merged total.
        std::uint64_t matching = 0u;
        for (std::uint64_t chunk = 0u; chunk < state.chunkCount; ++chunk)
        {
            state.offsets[chunk] = matching;
            matching += state.partials[chunk * kOrderDigitBuckets + bucket];
        }

        if (matching <= kMaxOrderGatherSamples)
        {
            status = gatherMatching(state.execution, state.reader, deviation, center, prefix, shift, state.chunkCount,
                                    state.offsets, matching, state.gathered, state.operation);
            if (status.failed())
            {
                return status;
            }
            ++state.scans;
            std::sort(state.gathered, state.gathered + static_cast<std::size_t>(matching));
            value = state.gathered[static_cast<std::size_t>(rank - below)];
            return Status::success();
        }
        if (shift == 0u)
        {
            // Every bit of the key is fixed, so every remaining candidate is the same value.
            value = keyValue(prefix);
            return Status::success();
        }
    }
    return orderFailure(ErrorCode::Internal, state.operation, "the order descent did not resolve a rank");
}

/// How many standard errors of a sampled rank a bracket reaches either side of it.
///
/// Where a rank falls among a sample of the plane estimates where it falls in the plane, with a
/// standard error of the square root of `n q (1 - q)` sample positions. Eight of them either side
/// keeps the bracket to about a tenth of the samples of a four-thousand-sample draw around a median.
/// A miss costs one read and never an answer: the selection then narrows over the samples' whole
/// range.
constexpr double kBracketStandardErrors = 8.0;

/// The fewest participating samples a bracket is drawn from.
constexpr std::uint64_t kMinimumBracketSamples = 256u;

/// A value interval expected to hold a rank, and whether one was found.
struct SampledBracket
{
    /// The interval's first value.
    double lowest = 0.0;

    /// Its last value, above the first.
    double highest = 0.0;

    /// Whether the sample gave an interval at all.
    bool found = false;
};

/// The values around one rank and its successor, read from a deterministic sample of the plane.
///
/// The sample takes up to `kMaxOrderGatherSamples` pixels, into the gather buffer, which no gather
/// is using yet. Their positions step through the plane by the golden ratio's fraction of it, a
/// sequence that spreads evenly over any length and falls into no column pattern: positions equally
/// spaced through a plane whose width divides the spacing would sample the same few columns of every
/// row, and a gradient across the frame would then move the whole sample. A bracket end that would
/// reach past the sample's own extremes is the selection's known extreme instead, so a rank near the
/// ends of the order is never missed for want of a sample there.
///
/// @tparam SampleT the resident type.
/// @param state the plane, partition and scratch.
/// @param deviation whether the order is over absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param lowest the smallest ordered value any selected sample takes.
/// @param highest the largest.
/// @param count how many samples participate.
/// @param rank the first rank the bracket must hold.
/// @param lastRank the last, `rank` or the one above it.
/// @return the bracket, or one that was not found when the reader selects by a rule or a mask, or the
///         sample holds too few selected values or no spread.
template <ResidentSample SampleT>
[[nodiscard]] SampledBracket sampleBracket(SelectionState<SampleT>& state, bool deviation, double center, double lowest,
                                           double highest, std::uint64_t count, std::uint64_t rank,
                                           std::uint64_t lastRank)
{
    const auto& reader = state.reader;
    if (!reader.unmaskedValues())
        return {};
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint64_t probes = (std::min)(std::uint64_t{kMaxOrderGatherSamples}, itemCount);
    const std::uint32_t columnStride = reader.columnStride();
    std::uint64_t taken = 0u;
    // The fraction of the golden ratio: successive multiples of it leave the largest gaps between
    // positions already taken.
    constexpr double kGoldenFraction = 0.6180339887498949;
    for (std::uint64_t probe = 0u; probe < probes; ++probe)
    {
        const double step = 0.5 + static_cast<double>(probe) * kGoldenFraction;
        const auto index =
            (std::min)(static_cast<std::uint64_t>((step - std::floor(step)) * static_cast<double>(itemCount)),
                       itemCount - 1u);
        const auto y = static_cast<std::uint32_t>(index / width);
        const auto x = static_cast<std::uint32_t>(index % width);
        const double stored = static_cast<double>(reader.rowStart(y)[static_cast<std::size_t>(x) * columnStride]);
        if (!reader.valueWindowAllows(stored))
            continue;
        const double value = reader.held(stored);
        state.gathered[taken++] = deviation ? std::abs(value - center) : value;
    }
    if (taken < kMinimumBracketSamples)
        return {};
    std::sort(state.gathered, state.gathered + static_cast<std::size_t>(taken));
    const double drawn = static_cast<double>(taken);
    const auto position = [&](std::uint64_t target, double side) {
        const double fraction = (static_cast<double>(target) + 0.5) / static_cast<double>(count);
        const double spread = kBracketStandardErrors * std::sqrt(drawn * fraction * (1.0 - fraction)) + 2.0;
        return fraction * drawn + side * spread;
    };
    const double first = std::floor(position(rank, -1.0));
    const double last = std::ceil(position(lastRank, 1.0));
    SampledBracket bracket{.lowest = first <= 0.0 ? lowest : state.gathered[static_cast<std::size_t>(first)],
                           .highest = last >= drawn - 1.0 ? highest : state.gathered[static_cast<std::size_t>(last)],
                           .found = true};
    bracket.found = bracket.highest > bracket.lowest;
    return bracket;
}

/// Resolves one rank, and optionally the rank above it, in as few reads of the plane as possible.
///
/// A descent that fixes eight bits of the key per read spends its first reads on the sign and the
/// exponent, which every sample of one image shares, so it pays a read per digit and separates
/// almost nothing until the last few. One pass over the values around the rank divides them where
/// they actually differ, and the bin holding the rank is usually already small enough to gather and
/// sort, which answers the rank and its successor together - the successor from the same read when
/// it lies above the bin. On a plane whose every sample is selected those values are a bracket read
/// from a sample of the plane; otherwise, or when the bracket misses, they are the samples' whole
/// range. A dense bin gets one further subdivision using the same tables. That pass counts
/// repeated endpoints to resolve large plateaus without gathering them. A bounded digit descent
/// remains for ranks that neither the gathered values nor those exact endpoint counts can decide.
///
/// @tparam SampleT the resident type.
/// @param state the plane, partition and scratch.
/// @param deviation whether the order is over absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param lowest the smallest ordered value any selected sample can take.
/// @param highest the largest ordered value any selected sample can take.
/// @param count how many samples participate.
/// @param rank the zero-based rank to resolve, below the selected count.
/// @param wantSuccessor whether the rank above it is wanted too.
/// @param first where the value at `rank` goes.
/// @param second where the value at `rank + 1` goes, set to `first` when it is not wanted.
/// @return success, or an execution or cancellation failure.
template <ResidentSample SampleT>
[[nodiscard]] Status selectRanks(SelectionState<SampleT>& state, bool deviation, double center, double lowest,
                                 double highest, std::uint64_t count, std::uint64_t rank, bool wantSuccessor,
                                 double& first, double& second)
{
    if (!(highest > lowest))
    {
        // Every selected sample holds the same ordered value, so every rank is that value and no
        // read can say anything further.
        first = lowest;
        second = lowest;
        return Status::success();
    }

    const NarrowingPlan plan = state.narrowing;
    // The gather of one bin answers the rank, and its successor when that lies in the same bin or
    // is the first value above it.
    const auto gatherRanks = [&](const SelectionBinPlan& binPlan, std::uint64_t bin, const SelectionBinBounds& bounds,
                                 std::uint64_t below, std::uint64_t matching, bool& resolved) -> Status {
        // The chosen bin's per-chunk counts are its gather offsets, so they are read out of the
        // per-chunk tables rather than from a merged total.
        std::uint64_t running = 0u;
        for (std::uint64_t chunk = 0u; chunk < plan.chunks; ++chunk)
        {
            state.offsets[chunk] = running;
            running += state.partials[chunk * plan.bins + bin];
        }
        // Only the bin's last rank has its successor outside the bin, so only then does the gather follow it.
        const bool follows = wantSuccessor && rank + 1u == below + matching;
        const Status gathered =
            gatherValueBin(state.execution, state.reader, deviation, center, binPlan, plan, bin, bounds, state.offsets,
                           matching, state.gathered, follows ? state.extremes : nullptr, state.operation);
        if (gathered.failed())
            return gathered;
        ++state.scans;
        std::sort(state.gathered, state.gathered + static_cast<std::size_t>(matching));
        first = state.gathered[static_cast<std::size_t>(rank - below)];
        second = first;
        resolved = true;
        if (!wantSuccessor)
            return Status::success();
        if (rank + 1u < below + matching)
        {
            second = state.gathered[static_cast<std::size_t>(rank + 1u - below)];
            return Status::success();
        }
        const double next =
            bounds.exact ? nextAboveGatheredBin(state.extremes, plan.chunks) : std::numeric_limits<double>::infinity();
        if (next <= highest)
        {
            second = next;
            return Status::success();
        }
        return selectRank(state, deviation, center, rank + 1u, second);
    };
    // The bin holding a rank, and how many samples lie below it and in it.
    const auto findBin = [&](std::uint64_t bins, std::uint64_t start, std::uint64_t& bin, std::uint64_t& below,
                             std::uint64_t& matching) {
        below = start;
        for (bin = 0u; bin < bins; ++bin)
        {
            std::uint64_t total = 0u;
            for (std::uint64_t chunk = 0u; chunk < plan.chunks; ++chunk)
            {
                total += state.partials[chunk * plan.bins + bin];
            }
            if (below + total > rank)
            {
                matching = total;
                return true;
            }
            below += total;
        }
        return false;
    };

    SelectionBinPlan binPlan = selectionBinPlan(lowest, highest, plan.bins);
    std::uint64_t refinedBelow = 0u;
    std::uint64_t refinedCount = 0u;
    unsigned firstAttempt = 0u;
    Status status;
    const SampledBracket bracket =
        sampleBracket(state, deviation, center, lowest, highest, count, rank, wantSuccessor ? rank + 1u : rank);
    // The bracket leaves the last `kScanBlock` counters of each table to the counts it discards.
    const SelectionBinPlan bracketPlan = selectionBinPlan(bracket.lowest, bracket.highest, plan.bins - kScanBlock);
    if (bracket.found && bracketPlan.binScale > 0.0 && std::isfinite(bracketPlan.binScale))
    {
        std::fill_n(state.partials, static_cast<std::size_t>(plan.chunks * plan.bins), std::uint64_t{0});
        status = countBracketBins(state.execution, state.reader, deviation, center, bracketPlan, plan, state.partials,
                                  state.extremes);
        if (status.failed())
            return status;
        ++state.scans;
        std::uint64_t outside = 0u;
        for (std::uint64_t chunk = 0u; chunk < plan.chunks; ++chunk)
            outside += state.extremes[chunk].belowBracket;
        std::uint64_t bin = 0u;
        std::uint64_t below = 0u;
        std::uint64_t matching = 0u;
        if (rank >= outside && findBin(bracketPlan.bins, outside, bin, below, matching))
        {
            // The bracket's end bins hold only values inside it: the counting pass tallied the rest.
            SelectionBinBounds bounds = selectionBinBounds(bracketPlan, bin);
            bounds.first = (std::max)(bounds.first, bracket.lowest);
            bounds.last = (std::min)(bounds.last, bracket.highest);
            if (matching <= kMaxOrderGatherSamples)
            {
                bool resolved = false;
                status = gatherRanks(bracketPlan, bin, bounds, below, matching, resolved);
                if (status.failed() || resolved)
                    return status;
            }
            else if (bounds.exact && bounds.last > bounds.first)
            {
                refinedBelow = below;
                refinedCount = matching;
                binPlan = selectionBinPlan(bounds.first, bounds.last, plan.bins);
                firstAttempt = 1u;
            }
            else if (bounds.exact)
            {
                // A bin whose bounds are one value holds that value alone.
                first = bounds.first;
                second = first;
                if (!wantSuccessor || rank + 1u < below + matching)
                    return Status::success();
                return selectRank(state, deviation, center, rank + 1u, second);
            }
        }
    }

    for (unsigned attempt = firstAttempt; attempt < 2u; ++attempt)
    {
        std::fill_n(state.partials, static_cast<std::size_t>(plan.chunks * plan.bins), std::uint64_t{0});
        status = countValueBins(state.execution, state.reader, deviation, center, binPlan, plan, state.partials,
                                attempt != 0u, attempt == 0u ? nullptr : state.extremes);
        if (status.failed())
        {
            return status;
        }
        ++state.scans;

        if (attempt != 0u)
        {
            SelectionExtremes seen;
            for (std::uint64_t chunk = 0u; chunk < plan.chunks; ++chunk)
            {
                seen.count += state.extremes[chunk].count;
                const auto& partial = state.extremes[chunk];
                if (partial.minimum < seen.minimum)
                {
                    seen.minimum = partial.minimum;
                    seen.minimumCount = partial.minimumCount;
                }
                else if (partial.minimum == seen.minimum)
                    seen.minimumCount += partial.minimumCount;
                if (partial.maximum > seen.maximum)
                {
                    seen.maximum = partial.maximum;
                    seen.maximumCount = partial.maximumCount;
                }
                else if (partial.maximum == seen.maximum)
                    seen.maximumCount += partial.maximumCount;
            }
            // A repeated-value population can exceed the gather capacity indefinitely. The
            // refinement already read every member, so its endpoint counts can prove both
            // adjacent ranks even when rounding gave two nearly equal deviation values.
            const auto endpoint = [&](std::uint64_t target, double& value) {
                if (target - refinedBelow < seen.minimumCount)
                    value = seen.minimum;
                else if (target < refinedBelow + seen.count && target >= refinedBelow + seen.count - seen.maximumCount)
                    value = seen.maximum;
                else
                    return false;
                return true;
            };
            if (seen.count == refinedCount && endpoint(rank, first))
            {
                second = first;
                if (!wantSuccessor || endpoint(rank + 1u, second))
                    return Status::success();
                return selectRank(state, deviation, center, rank + 1u, second);
            }
        }

        std::uint64_t bin = 0u;
        std::uint64_t below = 0u;
        std::uint64_t matching = 0u;
        if (!findBin(plan.bins, 0u, bin, below, matching))
        {
            return orderFailure(ErrorCode::Internal, state.operation,
                                "the narrowing pass left no bin holding the requested rank");
        }

        const SelectionBinBounds bounds = selectionBinBounds(binPlan, bin);
        if (matching <= kMaxOrderGatherSamples)
        {
            bool resolved = false;
            return gatherRanks(binPlan, bin, bounds, below, matching, resolved);
        }

        // One further subdivision handles a large, dense bin without restarting a digit
        // descent for both middle ranks. Values outside the narrower interval remain in its
        // end bins, so counts still describe the whole plane and the rank never changes.
        // Exact endpoint multiplicities can finish a plateau during this same read.
        const double refinedLow = (std::max)(lowest, bounds.first);
        const double refinedHigh = (std::min)(highest, bounds.last);
        if (attempt != 0u || !bounds.exact || !std::isfinite(refinedLow) || !std::isfinite(refinedHigh) ||
            !(refinedHigh > refinedLow))
            break;
        refinedBelow = below;
        refinedCount = matching;
        binPlan = selectionBinPlan(refinedLow, refinedHigh, plan.bins);
    }

    status = selectRank(state, deviation, center, rank, first);
    if (status.failed() || !wantSuccessor)
    {
        second = first;
        return status;
    }
    return selectRank(state, deviation, center, rank + 1u, second);
}

/// The value of one quantile under the requested interpolation.
///
/// @tparam SampleT the resident type.
/// @param state the plane, partition and scratch.
/// @param deviation whether the order is over absolute deviations from `center`.
/// @param center the value deviations are taken around.
/// @param lowest the smallest ordered value any selected sample can take.
/// @param highest the largest ordered value any selected sample can take.
/// @param count how many samples participate.
/// @param quantile the fraction, within `[0, 1]`.
/// @param interpolation which rule resolves a position between two ranks.
/// @param value where the answer goes.
/// @return success, or an execution or cancellation failure.
template <ResidentSample SampleT>
[[nodiscard]] Status selectQuantile(SelectionState<SampleT>& state, bool deviation, double center, double lowest,
                                    double highest, std::uint64_t count, double quantile,
                                    QuantileInterpolation interpolation, double& value)
{
    const double position = quantile * static_cast<double>(count - 1u);
    const double floored = std::floor(position);
    const std::uint64_t lower = static_cast<std::uint64_t>(floored);
    const double fraction = position - floored;
    if (interpolation == QuantileInterpolation::NearestRank)
    {
        double ignored = 0.0;
        return selectRanks(state, deviation, center, lowest, highest, count,
                           static_cast<std::uint64_t>(std::floor(position + 0.5)), false, value, ignored);
    }
    if (interpolation == QuantileInterpolation::LowerRank || fraction == 0.0)
    {
        double ignored = 0.0;
        return selectRanks(state, deviation, center, lowest, highest, count, lower, false, value, ignored);
    }
    // Linear interpolation needs the rank and the one above it. They are adjacent, so one narrowing
    // almost always brackets both and one gather answers them together.
    double first = 0.0;
    double second = 0.0;
    const Status status = selectRanks(state, deviation, center, lowest, highest, count, lower, true, first, second);
    if (status.failed())
    {
        return status;
    }
    value = first + fraction * (second - first);
    return Status::success();
}

/// One exactly represented value and its frequency in a finite alphabet.
struct WeightedOrderValue
{
    double value = 0.0;
    std::uint64_t count = 0u;
};

/// Tries an exact histogram of a normalized integer alphabet, validating every participating sample.
///
/// A few probes only choose whether the extra validation pass is worth attempting. They never
/// establish correctness: one off-grid sample discards the histogram and selects through the
/// general exact path. The reused tables also hold the weighted deviations after their merge. A hold
/// whose ends are Float32 values is applied to each stored sample as the hold applies it, so a held
/// end on the grid keeps a plane of codes on this path; a hold with another end leaves it to the
/// general path, which reads the held value in Float64.
[[nodiscard]] Result<bool> selectNormalizedIntegerGrid(SelectionState<float>& state, std::span<const double> quantiles,
                                                       std::span<double> values, const PlaneOrderRequest& request,
                                                       std::uint64_t count, PlaneOrderStatistics& result)
{
    const std::uint64_t itemCount = static_cast<std::uint64_t>(state.reader.width()) * state.reader.height();
    if (itemCount < 2u * 1024u * 1024u || !state.reader.contiguousUnmaskedValues() || result.minimum < 0.0 ||
        result.maximum > 1.0 || !(result.maximum > result.minimum))
        return false;
    const bool holds = state.reader.hasHold();
    const auto holdLowest = static_cast<float>(state.reader.holdLowest());
    const auto holdHighest = static_cast<float>(state.reader.holdHighest());
    if (holds && (static_cast<double>(holdLowest) != state.reader.holdLowest() ||
                  static_cast<double>(holdHighest) != state.reader.holdHighest()))
        return false;
    const auto held = [&](float stored) noexcept {
        return holds ? (std::min)((std::max)(stored, holdLowest), holdHighest) : stored;
    };
    const float* const samples = state.reader.contiguousSamples();
    std::uint32_t bits = 0u;
    for (std::uint64_t probe = 0u; probe < 64u; ++probe)
    {
        const float stored = samples[probe * (itemCount - 1u) / 63u];
        if (!state.reader.valueWindowAllows(stored))
            continue;
        const float value = held(stored);
        if (!(value >= 0.0f && value <= 1.0f))
            return false;
        const auto code = static_cast<std::uint32_t>(static_cast<double>(value) * 65535.0 + 0.5);
        if (std::bit_cast<std::uint32_t>(static_cast<float>(code) / 65535.0f) != std::bit_cast<std::uint32_t>(value))
            return false;
        bits |= code;
    }
    const auto shift = (std::min)(static_cast<unsigned>(std::countr_zero(bits)), 16u);
    if (shift < 2u)
        return false;
    const std::uint64_t bins = 65536u >> shift;
    const std::uint64_t capacity =
        (std::max)(state.chunkCount * kOrderDigitBuckets, state.narrowing.chunks * state.narrowing.bins);
    const std::uint64_t chunks = (std::min)({state.chunkCount, std::uint64_t{16}, capacity / bins});
    if (chunks < 3u)
        return false;
    std::fill_n(state.partials, static_cast<std::size_t>(chunks * bins), std::uint64_t{0});
    std::array<bool, kMaxSelectionChunks> invalid{};
    const std::uint32_t lowMask = (std::uint32_t{1} << shift) - 1u;
    const Status counted =
        parallelFor(state.execution, chunks, chunkCostOf<float>(itemCount, chunks), [&](TaskRange range) -> Status {
            for (std::uint64_t chunk = range.begin; chunk < range.end; ++chunk)
            {
                const auto items = plane_detail::deterministicRange(chunk, itemCount, chunks);
                auto* const histogram = state.partials + chunk * bins;
                bool offGrid = false;
                for (std::uint64_t index = items.begin; index < items.end; ++index)
                {
                    const float stored = samples[index];
                    if (!state.reader.valueWindowAllows(stored))
                        continue;
                    const float value = held(stored);
                    // Counted extrema already proved the selected values finite and in [0,1].
                    const auto code = static_cast<std::uint32_t>(static_cast<double>(value) * 65535.0 + 0.5);
                    if ((code & lowMask) != 0u || std::bit_cast<std::uint32_t>(static_cast<float>(code) / 65535.0f) !=
                                                      std::bit_cast<std::uint32_t>(value))
                    {
                        offGrid = true;
                        continue;
                    }
                    ++histogram[code >> shift];
                }
                invalid[chunk] = offGrid;
            }
            return Status::success();
        });
    ++state.scans;
    if (counted.failed())
        return counted;
    for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
        if (invalid[chunk])
            return false;
    for (std::uint64_t chunk = 1u; chunk < chunks; ++chunk)
        for (std::uint64_t bin = 0u; bin < bins; ++bin)
            state.partials[bin] += state.partials[chunk * bins + bin];
    auto* const weighted = reinterpret_cast<WeightedOrderValue*>(state.partials + bins);
    std::size_t used = 0u;
    std::uint64_t total = 0u;
    for (std::uint64_t bin = 0u; bin < bins; ++bin)
    {
        const std::uint64_t frequency = state.partials[bin];
        if (frequency == 0u)
            continue;
        total += frequency;
        const double value = static_cast<float>(bin << shift) / 65535.0f;
        std::construct_at(weighted + used++, WeightedOrderValue{value, frequency});
    }
    if (total != count)
        return orderFailure(ErrorCode::Internal, state.operation,
                            "the exact grid histogram changed the selection count");
    const auto rankValue = [&](std::uint64_t rank) {
        for (std::size_t index = 0u; index < used; ++index)
        {
            if (rank < weighted[index].count)
                return weighted[index].value;
            rank -= weighted[index].count;
        }
        return weighted[used - 1u].value;
    };
    const auto quantile = [&](double fraction) {
        const double position = fraction * static_cast<double>(count - 1u);
        if (request.interpolation == QuantileInterpolation::NearestRank)
            return rankValue(static_cast<std::uint64_t>(std::floor(position + 0.5)));
        const double lowerPosition = std::floor(position);
        const auto lowerRank = static_cast<std::uint64_t>(lowerPosition);
        const double lower = rankValue(lowerRank);
        if (request.interpolation == QuantileInterpolation::LowerRank || position == lowerPosition)
            return lower;
        return lower + (position - lowerPosition) * (rankValue(lowerRank + 1u) - lower);
    };
    result.median = quantile(0.5);
    for (std::size_t index = 0u; index < quantiles.size(); ++index)
        values[index] = quantile(quantiles[index]);
    result.medianAbsoluteDeviation = std::numeric_limits<double>::quiet_NaN();
    if (request.medianAbsoluteDeviation)
    {
        for (std::size_t index = 0u; index < used; ++index)
            weighted[index].value = std::abs(weighted[index].value - result.median);
        std::sort(weighted, weighted + used,
                  [](const auto& left, const auto& right) { return left.value < right.value; });
        result.medianAbsoluteDeviation = quantile(0.5);
    }
    return true;
}

/// Whether a representation holds few enough distinct codes to count its samples in a table of them, for a rank or
/// a histogram: the four eight- and sixteen-bit integer representations.
///
/// @tparam SampleT the resident type.
template <ResidentSample SampleT>
inline constexpr bool kCountsFromCodeTable =
    std::is_same_v<SampleT, std::uint8_t> || std::is_same_v<SampleT, std::int8_t> ||
    std::is_same_v<SampleT, std::uint16_t> || std::is_same_v<SampleT, std::int16_t>;

/// How often each code of an eight- or sixteen-bit plane occurs, and the scratch that holds the counts.
struct CodeCounts
{
    /// The block the merged counts sit at the start of, followed by the chunks' own tables.
    plane_detail::ScopedContextMemory scratch;

    /// How many samples hold each code, indexed from the representation's lowest code; only the samples a packed mask
    /// admits are counted, and no rule, window or hold has been applied.
    std::uint64_t* counts = nullptr;

    /// How many chunks the plane was counted in.
    std::uint64_t chunks = 0u;

    /// The size of the block, in bytes.
    std::uint64_t bytes = 0u;
};

/// Counts how often each code of an eight- or sixteen-bit plane occurs, in one read of it.
///
/// Such a plane holds at most 256 or 65,536 distinct values, so whatever the rule, the value window
/// and the hold decide about a sample they decide about its code, once, after the count; only a
/// packed mask is read pixel by pixel. Each chunk counts into a table of 32-bit counters of its own,
/// within the scratch ceiling the merged counts leave, and the tables are summed by ranges of codes.
/// The partition follows the plane alone and every count is exact, so the counts are the same at
/// every width.
///
/// @tparam SampleT an eight- or sixteen-bit integer representation.
/// @param execution the operation.
/// @param reader the checked component, its validity rule and its window.
/// @param operation the entry point the caller is inside.
/// @param table the counts, filled when the plane was counted.
/// @return true when the plane was counted; false when it holds fewer samples than the table has
///         codes, because clearing and walking the table would then cost more than reading the
///         samples, or when a chunk could count more samples than its counters hold; or an
///         allocation, cancellation or execution failure.
template <ResidentSample SampleT>
[[nodiscard]] Result<bool> countCodes(const ExecutionContext& execution,
                                      const plane_detail::PlaneSampleReader<SampleT>& reader, const char* operation,
                                      CodeCounts& table)
{
    constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
    constexpr std::int64_t kLowestCode = static_cast<std::int64_t>((std::numeric_limits<SampleT>::min)());
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    if (itemCount < kCodes)
    {
        return false;
    }
    const std::uint64_t mergedBytes = kCodes * sizeof(std::uint64_t);
    const std::uint64_t tableBytes = kCodes * sizeof(std::uint32_t);
    const std::uint64_t affordable = (kMaxHistogramPartialBytes - mergedBytes) / tableBytes;
    const std::uint64_t naturalChunks =
        deterministicChunkCount(itemCount, TaskCost{.bytesPerItem = sizeof(SampleT), .operationsPerItem = 2u});
    const std::uint64_t chunks =
        (std::max)(std::uint64_t{1}, (std::min)({naturalChunks, affordable, kMaxSelectionChunks}));
    if (itemCount / chunks + 1u > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    const std::uint64_t scratchBytes = mergedBytes + chunks * tableBytes;
    Result<plane_detail::ScopedContextMemory> scratch =
        plane_detail::ScopedContextMemory::take(execution, scratchBytes, alignof(std::uint64_t), operation);
    if (!scratch.hasValue())
    {
        return scratch.status();
    }
    auto* const counts = static_cast<std::uint64_t*>(scratch->data());
    auto* const tables = reinterpret_cast<std::uint32_t*>(counts + kCodes);
    const std::uint32_t columnStride = reader.columnStride();
    const auto codeOf = [](SampleT stored) noexcept {
        return static_cast<std::size_t>(static_cast<std::int64_t>(stored) - kLowestCode);
    };

    const Status counted =
        parallelFor(execution, chunks, chunkCostOf<SampleT>(itemCount, chunks), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                std::uint32_t* const table = tables + chunk * kCodes;
                // A chunk clears its own table, so the worker about to fill it is the one that touches it first.
                std::fill_n(table, static_cast<std::size_t>(kCodes), std::uint32_t{0});
                const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunks);
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const RowRun run = nextRun(index, range.end, width);
                    const SampleT* const row = reader.rowStart(run.y) + static_cast<std::size_t>(run.x) * columnStride;
                    if (reader.hasMask())
                    {
                        plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                        for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                        {
                            if (validityBits.allowsAndAdvance())
                            {
                                ++table[codeOf(row[static_cast<std::size_t>(offset) * columnStride])];
                            }
                        }
                    }
                    else if (columnStride == 1u)
                    {
                        for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                        {
                            ++table[codeOf(row[offset])];
                        }
                    }
                    else
                    {
                        for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                        {
                            ++table[codeOf(row[static_cast<std::size_t>(offset) * columnStride])];
                        }
                    }
                }
            }
            return Status::success();
        });
    if (counted.failed())
    {
        return counted;
    }

    const TaskCost mergeCost{.bytesPerItem = chunks * sizeof(std::uint32_t), .operationsPerItem = chunks};
    const std::uint64_t mergeChunks = (std::min)(kCodes, deterministicChunkCount(kCodes, mergeCost));
    const Status merged = parallelFor(execution, mergeChunks, mergeCost, [&](TaskRange mergeIndices) -> Status {
        for (std::uint64_t index = mergeIndices.begin; index < mergeIndices.end; ++index)
        {
            const TaskRange codes = plane_detail::deterministicRange(index, kCodes, mergeChunks);
            for (std::uint64_t code = codes.begin; code < codes.end; ++code)
            {
                std::uint64_t total = 0u;
                for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
                {
                    total += tables[chunk * kCodes + code];
                }
                counts[code] = total;
            }
        }
        return Status::success();
    });
    if (merged.failed())
    {
        return merged;
    }
    table.scratch = std::move(*scratch);
    table.counts = counts;
    table.chunks = chunks;
    table.bytes = scratchBytes;
    return true;
}

/// Selects exactly from a table of how often each code of an eight- or sixteen-bit plane occurs.
///
/// Counting how often each code occurs answers every rank exactly - the value of a rank is the code
/// whose running count first passes it - from one read of the plane, where narrowing reads it at
/// least twice for a median and twice more for its deviation. The rule, the value window and the
/// hold are applied once per code. The deviations from the median are ordered by walking the table
/// outwards from the median: the held values below it fall as the walk moves down and those above
/// it rise as it moves up, so the nearer of the two fronts is always the next deviation. Every rank
/// is the same sample and every interpolation the same arithmetic as the general selection, so the
/// answers are the ones it gives, at every width.
///
/// @tparam SampleT an eight- or sixteen-bit integer representation.
/// @param execution the operation.
/// @param reader the checked component, its validity rule and its window.
/// @param quantiles the fractions to select.
/// @param values where their values go, written only once every rank is known.
/// @param request the interpolation rule and whether the deviation is wanted.
/// @param operation the entry point the caller is inside.
/// @param result the statistics, filled when the table answered.
/// @return true when the table answered; false when `countCodes` did not count the plane;
///         `ErrorCode::EmptySelection` when nothing participates; or an allocation, cancellation or
///         execution failure.
template <ResidentSample SampleT>
[[nodiscard]] Result<bool>
selectFromCodeTable(const ExecutionContext& execution, const plane_detail::PlaneSampleReader<SampleT>& reader,
                    std::span<const double> quantiles, std::span<double> values, const PlaneOrderRequest& request,
                    const char* operation, PlaneOrderStatistics& result)
{
    constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
    constexpr std::int64_t kLowestCode = static_cast<std::int64_t>((std::numeric_limits<SampleT>::min)());
    CodeCounts table;
    const Result<bool> counted = countCodes(execution, reader, operation, table);
    if (!counted.hasValue())
    {
        return counted.status();
    }
    if (!*counted)
    {
        return false;
    }
    std::uint64_t* const counts = table.counts;
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();

    // Each code is admitted by the rule and the window once, and `counts` becomes the running count of the admitted
    // samples up to and including each code, so the code holding a rank is the first whose running count passes it.
    std::uint64_t count = 0u;
    for (std::uint64_t code = 0u; code < kCodes; ++code)
    {
        double ignored = 0.0;
        if (reader.selectedAfterMask(static_cast<SampleT>(static_cast<std::int64_t>(code) + kLowestCode), ignored))
        {
            count += counts[code];
        }
        counts[code] = count;
    }
    if (count == 0u)
    {
        return orderFailure(ErrorCode::EmptySelection, operation, "the selection rules selected no samples");
    }
    const auto heldValueOf = [&](std::uint64_t code) {
        return reader.held(static_cast<double>(static_cast<std::int64_t>(code) + kLowestCode));
    };
    const auto valueAtRank = [&](std::uint64_t rank) {
        return heldValueOf(static_cast<std::uint64_t>(std::upper_bound(counts, counts + kCodes, rank) - counts));
    };
    const auto countOf = [&](std::uint64_t code) { return counts[code] - (code == 0u ? 0u : counts[code - 1u]); };
    // The same positions, the same rules and the same interpolation as `selectQuantile`.
    const auto quantileOf = [&](double quantile, const auto& rankValue) {
        const double position = quantile * static_cast<double>(count - 1u);
        const double floored = std::floor(position);
        const auto lower = static_cast<std::uint64_t>(floored);
        const double fraction = position - floored;
        if (request.interpolation == QuantileInterpolation::NearestRank)
        {
            return rankValue(static_cast<std::uint64_t>(std::floor(position + 0.5)));
        }
        const double first = rankValue(lower);
        if (request.interpolation == QuantileInterpolation::LowerRank || fraction == 0.0)
        {
            return first;
        }
        return first + fraction * (rankValue(lower + 1u) - first);
    };

    const double median = quantileOf(0.5, valueAtRank);
    double deviation = std::numeric_limits<double>::quiet_NaN();
    if (request.medianAbsoluteDeviation)
    {
        // A code at or below the median holds a value at or below it, and one above it a value at or above it, so a
        // walk from the median outwards meets each side's deviations in ascending order.
        const double firstAbove = std::floor(median) + 1.0 - static_cast<double>(kLowestCode);
        std::uint64_t upward =
            static_cast<std::uint64_t>((std::min)((std::max)(firstAbove, 0.0), static_cast<double>(kCodes)));
        std::uint64_t downward = upward;
        std::uint64_t passed = 0u;
        double front = 0.0;
        const auto deviationAtRank = [&](std::uint64_t rank) {
            while (passed <= rank)
            {
                const double below =
                    downward > 0u ? median - heldValueOf(downward - 1u) : std::numeric_limits<double>::infinity();
                const double above =
                    upward < kCodes ? heldValueOf(upward) - median : std::numeric_limits<double>::infinity();
                if (below <= above)
                {
                    --downward;
                    passed += countOf(downward);
                    front = below;
                }
                else
                {
                    passed += countOf(upward);
                    ++upward;
                    front = above;
                }
            }
            return front;
        };
        deviation = quantileOf(0.5, deviationAtRank);
    }

    for (std::size_t index = 0u; index < quantiles.size(); ++index)
    {
        values[index] = quantileOf(quantiles[index], valueAtRank);
    }
    result.minimum = valueAtRank(0u);
    result.maximum = valueAtRank(count - 1u);
    result.median = median;
    result.medianAbsoluteDeviation = deviation;
    result.run.selection = ValidityCount{.valid = count, .total = itemCount};
    result.run.outputsWritten =
        static_cast<std::uint64_t>(quantiles.size()) + (request.medianAbsoluteDeviation ? 4u : 3u);
    result.deterministicChunks = table.chunks;
    result.plannedWorkers = static_cast<std::uint32_t>(
        (std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), table.chunks));
    result.planeScans = 1u;
    result.peakScratchBytes = table.bytes;
    return true;
}

/// Counts an eight- or sixteen-bit plane's histogram from a table of how often each code occurs.
///
/// Where a sample falls - its bin, below the interval, above it, or outside the selection - depends
/// on its value alone, and such a plane holds at most 65,536 values, so each code is classified once,
/// by the rule and window the general count applies and by its comparisons and `valueBin`, and its
/// count added where it falls. One read of the plane at the cost of one counter per sample, where the
/// general count converts, tests and bins every sample, and the same counts, because every sample is
/// classified as the general count classifies it.
///
/// @tparam SampleT an eight- or sixteen-bit integer representation.
/// @param execution the operation.
/// @param reader the checked component, its validity rule and its window.
/// @param bins the caller's counters, every one of them written.
/// @param lowest the interval's inclusive lower endpoint.
/// @param highest the interval's inclusive upper endpoint.
/// @param operation the entry point the caller is inside.
/// @param result the counts, filled when the table answered.
/// @return true when the table answered; false when `countCodes` did not count the plane; or an
///         allocation, cancellation or execution failure.
template <ResidentSample SampleT>
[[nodiscard]] Result<bool> histogramFromCodeTable(const ExecutionContext& execution,
                                                  const plane_detail::PlaneSampleReader<SampleT>& reader,
                                                  std::span<std::uint64_t> bins, double lowest, double highest,
                                                  const char* operation, PlaneHistogramResult& result)
{
    constexpr std::uint64_t kCodes = std::uint64_t{1} << (8u * sizeof(SampleT));
    constexpr std::int64_t kLowestCode = static_cast<std::int64_t>((std::numeric_limits<SampleT>::min)());
    CodeCounts table;
    const Result<bool> counted = countCodes(execution, reader, operation, table);
    if (!counted.hasValue())
    {
        return counted.status();
    }
    if (!*counted)
    {
        return false;
    }
    const std::uint64_t binCount = static_cast<std::uint64_t>(bins.size());
    std::fill(bins.begin(), bins.end(), std::uint64_t{0});
    std::uint64_t selected = 0u;
    std::uint64_t below = 0u;
    std::uint64_t above = 0u;
    std::uint64_t notANumber = 0u;
    for (std::uint64_t code = 0u; code < kCodes; ++code)
    {
        const std::uint64_t held = table.counts[code];
        double value = 0.0;
        if (held == 0u ||
            !reader.selectedAfterMask(static_cast<SampleT>(static_cast<std::int64_t>(code) + kLowestCode), value))
        {
            continue;
        }
        selected += held;
        if (std::isnan(value))
        {
            notANumber += held;
            continue;
        }
        if (value < lowest)
        {
            below += held;
            continue;
        }
        if (value > highest)
        {
            above += held;
            continue;
        }
        bins[static_cast<std::size_t>(valueBin(value, lowest, highest, binCount))] += held;
    }

    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    result.run.selection = ValidityCount{.valid = selected, .total = itemCount};
    result.run.outputsWritten = selected - below - above - notANumber;
    result.deterministicChunks = table.chunks;
    result.plannedWorkers = static_cast<std::uint32_t>(
        (std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), table.chunks));
    result.partialBytes = table.bytes;
    result.lowest = lowest;
    result.highest = highest;
    result.belowRange = below;
    result.aboveRange = above;
    result.notANumber = notANumber;
    return true;
}

/// The fewest samples a plane holds before its median and their absolute deviation are counted and gathered in two
/// reads together.
///
/// Below it the narrowing reads a plane in a few milliseconds whichever way it goes, and the sample of the plane the
/// brackets are read from would be a fair part of the plane.
constexpr std::uint64_t kTwoReadMinimumSamples = std::uint64_t{1} << 20u;

/// The most chunks two reads divide a plane into; the work decides fewer, never the machine.
constexpr std::uint64_t kTwoReadChunks = 16u;

/// How many bins each of the three brackets two reads count holds.
constexpr std::uint32_t kTwoReadBracketBins = 8192u;

/// The most samples the second of two reads gathers: the median's bins and every sample that could be the deviation's
/// rank. A plane that needs more is handed to the narrowing.
constexpr std::uint64_t kTwoReadGatherSamples = 32768u;

/// How many consecutive samples the first read takes at a time - their extremes, then their brackets - so that the
/// second look at them reads the nearest cache rather than memory.
constexpr std::uint64_t kTwoReadRun = 4096u;

/// The least share of distinct values, in eighths, among the sampled values inside the median's bracket for two reads
/// to be tried. Values repeated that often are codes rather than measurements. A code's bin holds every sample of that
/// code - for a sky's codes over a frame of tens of megapixels, more than a gather does - and a gather that overflows
/// costs a read and answers nothing, so codes go to the narrowing, which resolves them by their plateau counts, and
/// codes of fourteen bits or fewer in the unit interval from a table, in two reads.
constexpr std::uint64_t kTwoReadDistinctEighths = 6u;

/// What one chunk of the first of two reads saw: its selected samples and their extremes, as the narrowing's counting
/// pass records them, and what it tallied between and beyond the brackets.
struct TwoReadChunk
{
    /// The count, the extremes and the tallies.
    OrderBracketTallies tallies{};
};

static_assert(kTwoReadChunks * (kOrderBracketSetSize * kTwoReadBracketBins + kOrderCountBlock) * sizeof(std::uint32_t) +
                      kOrderBracketSetSize * (kTwoReadBracketBins + 1u) * sizeof(std::uint64_t) +
                      kTwoReadGatherSamples * sizeof(double) +
                      kTwoReadChunks * (sizeof(TwoReadChunk) + kOrderBracketSetSize * sizeof(std::uint64_t)) <=
                  kMaxHistogramPartialBytes,
              "two reads must keep their whole scratch within the selection's ceiling");
static_assert(2u * kMaxOrderGatherSamples <= kTwoReadGatherSamples,
              "the gather buffer holds the sample of the plane and its deviations while the brackets are read");

/// The brackets two reads count: where the deviation's rank lies below the median, the median's own, and where the
/// deviation's rank lies above the median - ascending, disjoint, and each mapped into `kTwoReadBracketBins` bins.
struct TwoReadBrackets
{
    /// The three brackets and their mappings, in that order.
    std::array<SelectionBinPlan, kOrderBracketSetSize> plans{};

    /// Whether the sample of the plane gave brackets two reads can count.
    bool found = false;
};

/// The three brackets of two reads, read from a deterministic sample of the plane.
///
/// The sample is the narrowing's own - up to `kMaxOrderGatherSamples` pixels at positions the golden ratio's fraction
/// steps through the plane by - sorted. The median's bracket reaches `kBracketStandardErrors` standard errors of the
/// sampled middle rank either side of it. The deviation's ranks lie where the sampled deviations from the sample's
/// median put them, as far again either side: a median absolute deviation moves no further than the centre it is taken
/// around, and the plane's median lies anywhere in its bracket, so the deviation's brackets reach that far further,
/// below the median's bracket and above it. Every bracket is only a guess at where a rank lies: the reads count the
/// samples below and above each, and a rank a bracket misses hands the plane to the narrowing.
///
/// @tparam SampleT the resident type.
/// @param reader the checked component, every sample of which is selected but for its value window.
/// @param scratch `2 * kMaxOrderGatherSamples` values, overwritten.
/// @return the brackets, or none when the sample holds too few selected values, holds a selected value that is not
///         finite - which the narrowing refuses - repeats its values, or leaves the deviation's brackets no room beside
///         the median's.
template <ResidentSample SampleT>
[[nodiscard]] TwoReadBrackets sampleTwoReadBrackets(const plane_detail::PlaneSampleReader<SampleT>& reader,
                                                    double* scratch) noexcept
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint64_t probes = (std::min)(std::uint64_t{kMaxOrderGatherSamples}, itemCount);
    const std::uint32_t columnStride = reader.columnStride();
    double* const values = scratch;
    double* const deviations = scratch + kMaxOrderGatherSamples;
    std::uint64_t taken = 0u;
    // The fraction of the golden ratio, as the narrowing's sample steps by it.
    constexpr double kGoldenFraction = 0.6180339887498949;
    for (std::uint64_t probe = 0u; probe < probes; ++probe)
    {
        const double step = 0.5 + static_cast<double>(probe) * kGoldenFraction;
        const auto index =
            (std::min)(static_cast<std::uint64_t>((step - std::floor(step)) * static_cast<double>(itemCount)),
                       itemCount - 1u);
        const auto y = static_cast<std::uint32_t>(index / width);
        const auto x = static_cast<std::uint32_t>(index % width);
        const double stored = static_cast<double>(reader.rowStart(y)[static_cast<std::size_t>(x) * columnStride]);
        if (!reader.valueWindowAllows(stored))
            continue;
        if (!std::isfinite(stored))
            return {};
        values[taken++] = reader.held(stored);
    }
    if (taken < kMinimumBracketSamples)
        return {};
    std::sort(values, values + static_cast<std::size_t>(taken));
    const double drawn = static_cast<double>(taken);
    const double spread = kBracketStandardErrors * std::sqrt(drawn * 0.25) + 2.0;
    const double firstPosition = std::floor(0.5 * drawn - spread);
    const double lastPosition = std::ceil(0.5 * drawn + spread);
    if (!(firstPosition > 0.0) || !(lastPosition < drawn - 1.0))
        return {};
    const auto first = static_cast<std::size_t>(firstPosition);
    const auto last = static_cast<std::size_t>(lastPosition);
    const double medianLowest = values[first];
    const double medianHighest = values[last];
    if (!(medianHighest > medianLowest))
        return {};
    std::uint64_t distinct = 1u;
    for (std::size_t index = first + 1u; index <= last; ++index)
        distinct += values[index] != values[index - 1u] ? 1u : 0u;
    if (distinct * 8u < static_cast<std::uint64_t>(last - first + 1u) * kTwoReadDistinctEighths)
        return {};
    const double centre = values[static_cast<std::size_t>(taken / 2u)];
    for (std::uint64_t index = 0u; index < taken; ++index)
        deviations[index] = std::abs(values[index] - centre);
    std::sort(deviations, deviations + static_cast<std::size_t>(taken));
    const double reach = (std::max)(medianHighest - centre, centre - medianLowest);
    const double nearest = deviations[first] - reach;
    const double farthest = deviations[last] + reach;
    // A deviation's bracket that would reach into the median's leaves no bin holding one statistic's samples alone.
    if (!(nearest > medianHighest - medianLowest))
        return {};
    TwoReadBrackets brackets;
    brackets.plans[0] = selectionBinPlan(medianLowest - farthest, medianHighest - nearest, kTwoReadBracketBins);
    brackets.plans[1] = selectionBinPlan(medianLowest, medianHighest, kTwoReadBracketBins);
    brackets.plans[2] = selectionBinPlan(medianLowest + nearest, medianHighest + farthest, kTwoReadBracketBins);
    for (const SelectionBinPlan& plan : brackets.plans)
    {
        if (!(plan.binScale > 0.0) || !std::isfinite(plan.binScale) || !std::isfinite(plan.lowest) ||
            !std::isfinite(plan.highest) || !(plan.highest > plan.lowest))
            return {};
    }
    if (!(brackets.plans[0].highest < brackets.plans[1].lowest) ||
        !(brackets.plans[1].highest < brackets.plans[2].lowest))
        return {};
    brackets.found = true;
    return brackets;
}

/// Counts the selected samples among consecutive samples and records their extremes, as the narrowing's counting pass
/// does: an extreme met earlier stays when a later value equals it, and a selected sample that is not finite is
/// flagged.
///
/// @tparam windowed whether a value window admits the stored values.
/// @tparam holds whether an admitted sample is read as held to `hold`.
/// @tparam SampleT the resident type.
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param window the value window, read only when `windowed`.
/// @param hold the hold, read only when `holds`.
/// @param seen the running count and extremes, in its `counted`, `minimum` and `maximum`.
/// @param nonFinite set to one when a selected sample is not finite.
template <bool windowed, bool holds, ResidentSample SampleT>
void extremesConsecutive(const SampleT* samples, std::uint64_t length, const ScanInterval& window,
                         const ScanInterval& hold, OrderBracketTallies& seen, std::uint64_t& nonFinite) noexcept
{
    for (std::uint64_t index = 0u; index < length; ++index)
    {
        const SampleT stored = samples[index];
        double value = static_cast<double>(stored);
        if constexpr (windowed)
        {
            if (!((value >= window.lowest) & (value <= window.highest)))
                continue;
        }
        ++seen.counted;
        nonFinite |= nonFiniteFlag(stored);
        if constexpr (holds)
            value = (std::min)((std::max)(value, hold.lowest), hold.highest);
        seen.minimum = (std::min)(seen.minimum, value);
        seen.maximum = (std::max)(seen.maximum, value);
    }
}

/// Counts consecutive samples into a set of brackets at once: the scalar authority of the compiled run, which counts
/// what this counts - see `OrderCountBracketSetFloat32RunFn`.
///
/// @tparam windowed whether the set's window admits the stored values; a plane without one is counted whole.
/// @tparam holds whether a counted sample is read as held to the set's hold.
/// @tparam SampleT the resident type.
/// @param samples the first sample.
/// @param length how many consecutive samples to count.
/// @param brackets the brackets, their mapping, the window and the hold.
/// @param bins the counters: the brackets' bins, then `kScanBlock` discarded counters.
/// @param tallies the running tallies.
template <bool windowed, bool holds, ResidentSample SampleT>
void countBracketSetConsecutive(const SampleT* samples, std::uint64_t length, const OrderBracketSet& brackets,
                                std::uint32_t* bins, OrderBracketTallies& tallies) noexcept
{
    const auto last = static_cast<std::int32_t>(brackets.bins - 1u);
    const auto perBracket = static_cast<std::int32_t>(brackets.bins);
    const std::int32_t discarded = static_cast<std::int32_t>(kOrderBracketSetSize) * perBracket;
    std::uint64_t belowFirst = 0u;
    std::uint64_t belowSecond = 0u;
    std::uint64_t aboveSecond = 0u;
    std::uint64_t aboveThird = 0u;
    // The counter one sample increments, or `uncounted` when it lies in no bracket or outside the window.
    const auto counterOf = [&](SampleT stored, std::int32_t uncounted) noexcept {
        const double value = static_cast<double>(stored);
        if constexpr (windowed)
        {
            if (!((value >= brackets.windowLowest) & (value <= brackets.windowHighest)))
                return uncounted;
        }
        double read = value;
        if constexpr (holds)
            read = (std::min)((std::max)(value, brackets.holdLowest), brackets.holdHighest);
        belowFirst += static_cast<std::uint64_t>(read < brackets.lowest[0]);
        belowSecond += static_cast<std::uint64_t>(read < brackets.lowest[1]);
        aboveSecond += static_cast<std::uint64_t>(read > brackets.highest[1]);
        aboveThird += static_cast<std::uint64_t>(read > brackets.highest[2]);
        for (std::size_t bracket = 0u; bracket < kOrderBracketSetSize; ++bracket)
        {
            if ((read >= brackets.lowest[bracket]) & (read <= brackets.highest[bracket]))
            {
                const auto bin = static_cast<std::int32_t>((read - brackets.lowest[bracket]) * brackets.scale[bracket]);
                return static_cast<std::int32_t>(bracket) * perBracket + (bin < last ? bin : last);
            }
        }
        return uncounted;
    };
    std::uint64_t index = 0u;
    for (; index + kScanBlock <= length; index += kScanBlock)
    {
        std::array<std::int32_t, kScanBlock> target{};
        for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
            target[lane] = counterOf(samples[index + lane], discarded + static_cast<std::int32_t>(lane));
        for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
            ++bins[target[lane]];
    }
    for (; index < length; ++index)
    {
        const std::int32_t counter = counterOf(samples[index], -1);
        if (counter >= 0)
            ++bins[counter];
    }
    tallies.belowFirst += belowFirst;
    tallies.belowSecond += belowSecond;
    tallies.aboveSecond += aboveSecond;
    tallies.aboveThird += aboveThird;
}

/// Gathers the members of a set of intervals from consecutive samples: the scalar authority of the compiled run - see
/// `OrderGatherIntervalSetFloat32RunFn`. A block's membership is tested without a branch, and only a block holding a
/// member is walked.
///
/// @tparam windowed whether a value window admits the stored values.
/// @tparam holds whether an admitted sample is read as held to the read's hold.
/// @tparam SampleT the resident type.
/// @param samples the first sample.
/// @param length how many consecutive samples to read.
/// @param read the window and the hold.
/// @param intervals the intervals.
/// @param gathered where members go.
/// @param end each interval's end of place in `gathered`; no member is written at or past it.
/// @param write each interval's next position in `gathered`, advanced past every member, written or not.
template <bool windowed, bool holds, ResidentSample SampleT>
void gatherIntervalSetConsecutive(const SampleT* samples, std::uint64_t length, const OrderRead& read,
                                  const OrderIntervalSet& intervals, double* gathered,
                                  const std::array<std::uint64_t, kOrderBracketSetSize>& end,
                                  std::array<std::uint64_t, kOrderBracketSetSize>& write) noexcept
{
    const auto heldOf = [&](SampleT stored, bool& admitted) noexcept {
        const double value = static_cast<double>(stored);
        admitted = true;
        if constexpr (windowed)
            admitted = (value >= read.windowLowest) & (value <= read.windowHighest);
        if constexpr (holds)
            return (std::min)((std::max)(value, read.holdLowest), read.holdHighest);
        else
            return value;
    };
    const auto memberOf = [&](double held) noexcept {
        for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
        {
            if ((held >= intervals.first[interval]) & (held <= intervals.last[interval]))
                return static_cast<int>(interval);
        }
        return -1;
    };
    const auto take = [&](SampleT stored) noexcept {
        bool admitted = false;
        const double held = heldOf(stored, admitted);
        if (!admitted)
            return;
        const int interval = memberOf(held);
        if (interval >= 0)
        {
            std::uint64_t& at = write[static_cast<std::size_t>(interval)];
            if (at < end[static_cast<std::size_t>(interval)])
                gathered[at] = held;
            ++at;
        }
    };
    std::uint64_t index = 0u;
    for (; index + kScanBlock <= length; index += kScanBlock)
    {
        bool any = false;
        for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
        {
            bool admitted = false;
            const double held = heldOf(samples[index + lane], admitted);
            bool inside = false;
            for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
                inside |= (held >= intervals.first[interval]) & (held <= intervals.last[interval]);
            any |= admitted & inside;
        }
        if (any)
        {
            for (std::size_t lane = 0u; lane < kScanBlock; ++lane)
                take(samples[index + lane]);
        }
    }
    for (; index < length; ++index)
        take(samples[index]);
}

/// Calls `run` with each run of consecutive samples one chunk of a plane covers, in order: the whole chunk at once
/// when the rows are packed, a row's part of it at a time otherwise.
///
/// @tparam SampleT the resident type.
/// @param reader the checked component, with unit column stride.
/// @param range the chunk's pixels.
/// @param run what to call with a run's first sample and its length.
template <ResidentSample SampleT, typename RunT>
void forEachRun(const plane_detail::PlaneSampleReader<SampleT>& reader, TaskRange range, RunT&& run)
{
    if (reader.contiguousUnmaskedValues())
    {
        run(reader.contiguousSamples() + range.begin, range.end - range.begin);
        return;
    }
    std::uint64_t index = range.begin;
    while (index < range.end)
    {
        const RowRun rowRun = nextRun(index, range.end, reader.width());
        run(reader.rowStart(rowRun.y) + rowRun.x, rowRun.length);
    }
}

/// The window a set of brackets is counted through: the reader's own, or for a floating plane without one every finite
/// value, so that a sample that is not finite is counted nowhere, as the compiled count of the extremes leaves it.
///
/// @tparam SampleT the resident type.
/// @param reader the checked component.
/// @return the window.
template <ResidentSample SampleT>
[[nodiscard]] ScanInterval twoReadWindowOf(const plane_detail::PlaneSampleReader<SampleT>& reader) noexcept
{
    if (reader.hasValueWindow())
        return ScanInterval{.lowest = reader.valueWindowLowest(), .highest = reader.valueWindowHighest()};
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        const double largest = static_cast<double>((std::numeric_limits<SampleT>::max)());
        return ScanInterval{.lowest = -largest, .highest = largest};
    }
    return ScanInterval{};
}

/// The first of two reads: every chunk's selected samples, their extremes, and the three brackets' bins and tallies.
///
/// The compiled count takes a run whole, counting its extremes as it bins it. The block loops take a run a part at a
/// time - its extremes, then its brackets - so that the second look at a part reads the nearest cache.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component, with unit column stride, every sample selected but for its window.
/// @param brackets the brackets, the window and the hold.
/// @param chunks the fixed partition.
/// @param tableCounters the counters one chunk's table holds.
/// @param tables `chunks * tableCounters` counters, each chunk's cleared by that chunk.
/// @param records one record per chunk.
/// @param operation the entry point a failure belongs to.
/// @return success, or `ErrorCode::InvalidData` for a selected sample that is not finite, or the first failure.
template <ResidentSample SampleT>
[[nodiscard]] Status
countTwoReadBrackets(const ExecutionContext& execution, const plane_detail::PlaneSampleReader<SampleT>& reader,
                     const OrderBracketSet& brackets, std::uint64_t chunks, std::uint64_t tableCounters,
                     std::uint32_t* tables, TwoReadChunk* records, const char* operation)
{
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    const bool windowed = reader.hasValueWindow();
    const bool holds = reader.hasHold();
    const auto [window, hold] = scanIntervalsOf(reader);
    OrderCountBracketSetFloat32RunFn compiled = nullptr;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        compiled = orderCountBracketSetFloat32RunFor(execution);
    }
    const bool countWindowed = windowed || std::is_floating_point_v<SampleT>;
    return parallelFor(
        execution, chunks, chunkCostOf<SampleT>(itemCount, chunks), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                std::uint32_t* const table = tables + chunk * tableCounters;
                std::fill_n(table, static_cast<std::size_t>(tableCounters), std::uint32_t{0});
                TwoReadChunk record;
                std::uint64_t nonFinite = 0u;
                const auto countRun = [&](const SampleT* samples, std::uint64_t length) {
                    if constexpr (std::is_same_v<SampleT, float>)
                    {
                        if (compiled != nullptr)
                        {
                            // Without a window of its own the run counts every finite sample, so one it leaves
                            // uncounted is a selected sample that is not finite.
                            const std::uint64_t before = record.tallies.counted;
                            compiled(samples, length, brackets, table, record.tallies);
                            if (!windowed && record.tallies.counted - before != length)
                                nonFinite = 1u;
                            return;
                        }
                    }
                    for (std::uint64_t offset = 0u; offset < length; offset += kTwoReadRun)
                    {
                        const SampleT* const run = samples + offset;
                        const std::uint64_t runLength = (std::min)(kTwoReadRun, length - offset);
                        if (windowed)
                        {
                            if (holds)
                                extremesConsecutive<true, true>(run, runLength, window, hold, record.tallies,
                                                                nonFinite);
                            else
                                extremesConsecutive<true, false>(run, runLength, window, hold, record.tallies,
                                                                 nonFinite);
                        }
                        else if (holds)
                        {
                            extremesConsecutive<false, true>(run, runLength, window, hold, record.tallies, nonFinite);
                        }
                        else
                        {
                            extremesConsecutive<false, false>(run, runLength, window, hold, record.tallies, nonFinite);
                        }
                        if (countWindowed)
                        {
                            if (holds)
                                countBracketSetConsecutive<true, true>(run, runLength, brackets, table, record.tallies);
                            else
                                countBracketSetConsecutive<true, false>(run, runLength, brackets, table,
                                                                        record.tallies);
                        }
                        else if (holds)
                        {
                            countBracketSetConsecutive<false, true>(run, runLength, brackets, table, record.tallies);
                        }
                        else
                        {
                            countBracketSetConsecutive<false, false>(run, runLength, brackets, table, record.tallies);
                        }
                    }
                };
                forEachRun(reader, plane_detail::deterministicRange(chunk, itemCount, chunks), countRun);
                if (nonFinite != 0u)
                {
                    return orderFailure(ErrorCode::InvalidData, operation,
                                        "a selected sample is not finite; use a validity rule that excludes it");
                }
                records[chunk] = record;
            }
            return Status::success();
        });
}

/// The second of two reads: every member of the three intervals, each interval's at its own place in one buffer and
/// each chunk's at the offset the first read's counts give it.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component, with unit column stride, every sample selected but for its window.
/// @param intervals the intervals, the median's first.
/// @param chunks the first read's partition.
/// @param offsets each chunk's first position for each interval, `chunks * kOrderBracketSetSize` of them.
/// @param ends where each interval's members end in the buffer.
/// @param gathered where members go.
/// @param operation the entry point a failure belongs to.
/// @return success, or `ErrorCode::Internal` when a chunk gathered other than its counts said, or the first failure.
template <ResidentSample SampleT>
[[nodiscard]] Status
gatherTwoReadIntervals(const ExecutionContext& execution, const plane_detail::PlaneSampleReader<SampleT>& reader,
                       const OrderIntervalSet& intervals, std::uint64_t chunks, const std::uint64_t* offsets,
                       const std::array<std::uint64_t, kOrderBracketSetSize>& ends, double* gathered,
                       const char* operation)
{
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    const bool windowed = reader.hasValueWindow();
    const bool holds = reader.hasHold();
    const ScanInterval window = twoReadWindowOf(reader);
    const OrderRead read{.windowLowest = window.lowest,
                         .windowHighest = window.highest,
                         .holdLowest = reader.holdLowest(),
                         .holdHighest = reader.holdHighest()};
    OrderGatherIntervalSetFloat32RunFn compiled = nullptr;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        compiled = orderGatherIntervalSetFloat32RunFor(execution);
    }
    const bool gatherWindowed = windowed || std::is_floating_point_v<SampleT>;
    return parallelFor(
        execution, chunks, chunkCostOf<SampleT>(itemCount, chunks), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                // The first read's counts sized each chunk's place for each interval: it starts where the chunk's
                // offset says and ends where the next chunk's starts, and the gather writes nothing past that end.
                std::array<std::uint64_t, kOrderBracketSetSize> write{};
                std::array<std::uint64_t, kOrderBracketSetSize> end{};
                for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
                {
                    write[interval] = offsets[chunk * kOrderBracketSetSize + interval];
                    end[interval] =
                        chunk + 1u < chunks ? offsets[(chunk + 1u) * kOrderBracketSetSize + interval] : ends[interval];
                }
                const auto gatherRun = [&](const SampleT* samples, std::uint64_t length) {
                    if constexpr (std::is_same_v<SampleT, float>)
                    {
                        if (compiled != nullptr)
                        {
                            compiled(samples, length, read, intervals, gathered, end, write);
                            return;
                        }
                    }
                    if (gatherWindowed)
                    {
                        if (holds)
                            gatherIntervalSetConsecutive<true, true>(samples, length, read, intervals, gathered, end,
                                                                     write);
                        else
                            gatherIntervalSetConsecutive<true, false>(samples, length, read, intervals, gathered, end,
                                                                      write);
                    }
                    else if (holds)
                    {
                        gatherIntervalSetConsecutive<false, true>(samples, length, read, intervals, gathered, end,
                                                                  write);
                    }
                    else
                    {
                        gatherIntervalSetConsecutive<false, false>(samples, length, read, intervals, gathered, end,
                                                                   write);
                    }
                };
                forEachRun(reader, plane_detail::deterministicRange(chunk, itemCount, chunks), gatherRun);
                // A gather that disagrees with the counts counted past its place, though it wrote nothing there.
                for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
                {
                    if (write[interval] != end[interval])
                    {
                        return orderFailure(ErrorCode::Internal, operation,
                                            "an interval's gather wrote other than the members its count counted");
                    }
                }
            }
            return Status::success();
        });
}

/// Selects a median and its absolute deviation exactly, in two reads of the plane, where the plane allows it.
///
/// The narrowing reads a plane five times for these two statistics: once for its extremes and twice for each, a count
/// of a bracket and a gather of the bin holding the rank. A deviation's rank is not known before the median is, but
/// where it lies is: at the median's distance from its deviation below the median and above it. So the brackets of
/// both - the median's, and one each side of it for the deviation - are read from a sample of the plane and counted in
/// one read, with the extremes, and the bins that hold the median and every sample whose deviation could be the
/// deviation's rank are gathered in a second. The median is then the gathered bin's sample at its rank. The deviation's
/// rank is placed by the counts: the samples between the two gathered runs of bins all lie nearer the median than any
/// gathered one could need to, and those beyond them all farther, which the gathered values' deviations from the exact
/// median are checked against before the rank is read among them. Every answer is the narrowing's, bit for bit.
///
/// A plane whose sample repeats its values, whose median and deviation are too close for their brackets to stand
/// apart, whose ranks a bracket misses, or whose gather would outgrow `kTwoReadGatherSamples` is handed back - before
/// the first read where the sample shows it, after it otherwise - and the reads taken are reported for the narrowing to
/// count with its own.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param request the interpolation rule; the deviation is asked for.
/// @param operation the entry point a failure belongs to.
/// @param result the statistics, filled when the reads answered.
/// @param scans how many reads were taken, whether or not they answered.
/// @param scratchBytes the most scratch held at once.
/// @return true when the reads answered; false when the plane is handed to the narrowing; or a failure, which is the
///         narrowing's for the same plane.
template <ResidentSample SampleT>
[[nodiscard]] Result<bool> selectMedianAndDeviationInTwoReads(const ExecutionContext& execution,
                                                              const plane_detail::PlaneSampleReader<SampleT>& reader,
                                                              const PlaneOrderRequest& request, const char* operation,
                                                              PlaneOrderStatistics& result, std::uint64_t& scans,
                                                              std::uint64_t& scratchBytes)
{
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    if (itemCount < kTwoReadMinimumSamples || !reader.unmaskedValues() || reader.columnStride() != 1u)
        return false;
    const std::uint64_t naturalChunks =
        deterministicChunkCount(itemCount, TaskCost{.bytesPerItem = sizeof(SampleT), .operationsPerItem = 8u});
    const std::uint64_t chunks = (std::max)(std::uint64_t{1}, (std::min)(kTwoReadChunks, naturalChunks));
    if (itemCount / chunks + 1u > (std::numeric_limits<std::uint32_t>::max)())
        return false;

    // The gather's buffer holds the sample of the plane while the brackets are read from it.
    Result<plane_detail::ScopedContextMemory> gatherMemory = plane_detail::ScopedContextMemory::take(
        execution, kTwoReadGatherSamples * sizeof(double), alignof(double), operation);
    if (!gatherMemory.hasValue())
        return gatherMemory.status();
    double* const gathered = static_cast<double*>(gatherMemory->data());
    const TwoReadBrackets brackets = sampleTwoReadBrackets(reader, gathered);
    if (!brackets.found)
        return false;

    constexpr std::uint64_t kBins = kTwoReadBracketBins;
    const std::uint64_t tableCounters = kOrderBracketSetSize * kBins + kOrderCountBlock;
    Result<plane_detail::ScopedContextMemory> tableMemory = plane_detail::ScopedContextMemory::take(
        execution, chunks * tableCounters * sizeof(std::uint32_t), alignof(std::uint32_t), operation);
    if (!tableMemory.hasValue())
        return tableMemory.status();
    Result<plane_detail::ScopedContextMemory> runningMemory = plane_detail::ScopedContextMemory::take(
        execution, kOrderBracketSetSize * (kBins + 1u) * sizeof(std::uint64_t), alignof(std::uint64_t), operation);
    if (!runningMemory.hasValue())
        return runningMemory.status();
    Result<plane_detail::ScopedContextMemory> recordMemory = plane_detail::ScopedContextMemory::take(
        execution, chunks * (sizeof(TwoReadChunk) + kOrderBracketSetSize * sizeof(std::uint64_t)),
        alignof(TwoReadChunk), operation);
    if (!recordMemory.hasValue())
        return recordMemory.status();
    scratchBytes = gatherMemory->bytes() + tableMemory->bytes() + runningMemory->bytes() + recordMemory->bytes();
    auto* const tables = static_cast<std::uint32_t*>(tableMemory->data());
    auto* const running = static_cast<std::uint64_t*>(runningMemory->data());
    auto* const records = static_cast<TwoReadChunk*>(recordMemory->data());
    auto* const offsets = reinterpret_cast<std::uint64_t*>(records + chunks);
    std::fill_n(records, static_cast<std::size_t>(chunks), TwoReadChunk{});

    const ScanInterval window = twoReadWindowOf(reader);
    OrderBracketSet set{.bins = kTwoReadBracketBins,
                        .windowLowest = window.lowest,
                        .windowHighest = window.highest,
                        .holdLowest = reader.holdLowest(),
                        .holdHighest = reader.holdHighest()};
    for (std::size_t bracket = 0u; bracket < kOrderBracketSetSize; ++bracket)
    {
        set.lowest[bracket] = brackets.plans[bracket].lowest;
        set.highest[bracket] = brackets.plans[bracket].highest;
        set.scale[bracket] = brackets.plans[bracket].binScale;
    }
    const Status counted =
        countTwoReadBrackets(execution, reader, set, chunks, tableCounters, tables, records, operation);
    if (counted.failed())
        return counted;
    ++scans;

    // The chunks' records merged in chunk order, as the narrowing merges its counting pass's, and each bracket's bins
    // summed over the chunks into running counts: `running[b][i]` samples of bracket `b` lie in its bins before `i`.
    std::uint64_t count = 0u;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    OrderBracketTallies tallies;
    for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
    {
        count += records[chunk].tallies.counted;
        minimum = (std::min)(minimum, records[chunk].tallies.minimum);
        maximum = (std::max)(maximum, records[chunk].tallies.maximum);
        tallies.belowFirst += records[chunk].tallies.belowFirst;
        tallies.belowSecond += records[chunk].tallies.belowSecond;
        tallies.aboveSecond += records[chunk].tallies.aboveSecond;
        tallies.aboveThird += records[chunk].tallies.aboveThird;
    }
    if (count == 0u)
        return false;
    for (std::size_t bracket = 0u; bracket < kOrderBracketSetSize; ++bracket)
    {
        std::uint64_t* const sums = running + bracket * (kBins + 1u);
        sums[0] = 0u;
        for (std::uint64_t bin = 0u; bin < kBins; ++bin)
        {
            std::uint64_t total = 0u;
            for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
                total += tables[chunk * tableCounters + bracket * kBins + bin];
            sums[bin + 1u] = sums[bin] + total;
        }
    }
    const std::uint64_t* const lowerRunning = running;
    const std::uint64_t* const medianRunning = running + (kBins + 1u);
    const std::uint64_t* const upperRunning = running + 2u * (kBins + 1u);
    const std::uint64_t lowerSum = lowerRunning[kBins];
    const std::uint64_t medianSum = medianRunning[kBins];
    const std::uint64_t upperSum = upperRunning[kBins];
    if (tallies.belowSecond + medianSum + tallies.aboveSecond != count ||
        tallies.belowFirst + lowerSum > tallies.belowSecond || tallies.aboveThird + upperSum > tallies.aboveSecond)
    {
        return orderFailure(ErrorCode::Internal, operation, "a bracket count's tallies disagree with its bins");
    }

    // The ranks the median takes, and the deviation with it: the same positions and rules as `selectQuantile`.
    const double position = 0.5 * static_cast<double>(count - 1u);
    const double floored = std::floor(position);
    const auto lower = static_cast<std::uint64_t>(floored);
    const double fraction = position - floored;
    std::uint64_t firstRank = lower;
    std::uint64_t lastRank = lower;
    if (request.interpolation == QuantileInterpolation::NearestRank)
    {
        firstRank = static_cast<std::uint64_t>(std::floor(position + 0.5));
        lastRank = firstRank;
    }
    else if (request.interpolation == QuantileInterpolation::Linear && fraction != 0.0)
    {
        lastRank = lower + 1u;
    }
    const auto interpolated = [&](double first, double last) {
        return request.interpolation == QuantileInterpolation::Linear && fraction != 0.0
                   ? first + fraction * (last - first)
                   : first;
    };

    // The median's bins, which its ranks must lie in for the reads to answer.
    if (firstRank < tallies.belowSecond || lastRank >= tallies.belowSecond + medianSum)
        return false;
    const auto binHolding = [&](const std::uint64_t* sums, std::uint64_t rank) {
        return static_cast<std::uint64_t>(std::upper_bound(sums + 1, sums + kBins + 1u, rank) - (sums + 1));
    };
    const std::uint64_t medianFirstBin = binHolding(medianRunning, firstRank - tallies.belowSecond);
    const std::uint64_t medianLastBin = binHolding(medianRunning, lastRank - tallies.belowSecond);
    // The values a run of a bracket's bins holds, exactly: a bracket's end bins hold only values inside it.
    const auto runOf = [&](std::size_t bracket, std::uint64_t firstBin, std::uint64_t lastBin) {
        const SelectionBinPlan& plan = brackets.plans[bracket];
        return std::pair<double, double>{(std::max)(selectionBinBounds(plan, firstBin).first, plan.lowest),
                                         (std::min)(selectionBinBounds(plan, lastBin).last, plan.highest)};
    };
    const auto [medianLowest, medianHighest] = runOf(1u, medianFirstBin, medianLastBin);

    // How many samples lie within a distance of the median, at least and at most, wherever in its bins it lies: both
    // ends of that span inside the deviation's brackets, every sample between them counted whole, and each bracket's
    // bins counted whole when they could reach the span or only when they lie inside it.
    const SelectionBinPlan& lowerPlan = brackets.plans[0];
    const SelectionBinPlan& upperPlan = brackets.plans[2];
    const std::uint64_t between = count - tallies.belowFirst - lowerSum - upperSum - tallies.aboveThird;
    const auto lowerBin = [&](double value) { return selectionValueBin(value, lowerPlan); };
    const auto upperBin = [&](double value) { return selectionValueBin(value, upperPlan); };
    const auto atMost = [&](double distance) {
        return lowerSum - lowerRunning[lowerBin(medianLowest - distance)] + between +
               upperRunning[upperBin(medianHighest + distance) + 1u];
    };
    const auto atLeast = [&](double distance) {
        return lowerSum - lowerRunning[(std::min)(kBins, lowerBin(medianHighest - distance) + 1u)] + between +
               upperRunning[upperBin(medianLowest + distance)];
    };
    const double nearest = (std::max)(medianHighest - lowerPlan.highest, upperPlan.lowest - medianLowest);
    const double farthest = (std::min)(medianLowest - lowerPlan.lowest, upperPlan.highest - medianHighest);
    if (!(nearest < farthest))
        return false;
    // The distances at which a count first reaches a target: the last below it and the first at it.
    const auto reaching = [&](const auto& bound, std::uint64_t target) -> std::optional<std::pair<double, double>> {
        if (bound(nearest) >= target || bound(farthest) < target)
            return std::nullopt;
        double below = nearest;
        double at = farthest;
        for (int step = 0; step < 128; ++step)
        {
            const double middle = below + (at - below) * 0.5;
            if (!(middle > below) || !(middle < at))
                break;
            if (bound(middle) >= target)
                at = middle;
            else
                below = middle;
        }
        return std::pair<double, double>{below, at};
    };
    const std::optional<std::pair<double, double>> nearestDeviation = reaching(atMost, firstRank + 1u);
    const std::optional<std::pair<double, double>> farthestDeviation = reaching(atLeast, lastRank + 1u);
    if (!nearestDeviation.has_value() || !farthestDeviation.has_value())
        return false;
    const double deviationLowest = nearestDeviation->first;
    const double deviationHighest = farthestDeviation->second;
    // The runs of bins whose samples could be the deviation's ranks, a bin wider each way.
    const std::uint64_t lowerFirstBin = lowerBin(medianLowest - deviationHighest);
    const std::uint64_t lowerLastBin = lowerBin(medianHighest - deviationLowest);
    const std::uint64_t upperFirstBin = upperBin(medianLowest + deviationLowest);
    const std::uint64_t upperLastBin = upperBin(medianHighest + deviationHighest);
    const std::array<std::uint64_t, kOrderBracketSetSize> firstBins{
        medianFirstBin, lowerFirstBin == 0u ? 0u : lowerFirstBin - 1u, upperFirstBin == 0u ? 0u : upperFirstBin - 1u};
    const std::array<std::uint64_t, kOrderBracketSetSize> lastBins{
        medianLastBin, (std::min)(kBins - 1u, lowerLastBin + 1u), (std::min)(kBins - 1u, upperLastBin + 1u)};
    // The intervals in gather order - the median's, the deviation's below it and above it - and their brackets.
    constexpr std::array<std::size_t, kOrderBracketSetSize> kBracketOf{1u, 0u, 2u};
    const std::array<const std::uint64_t*, kOrderBracketSetSize> runningOf{medianRunning, lowerRunning, upperRunning};
    OrderIntervalSet intervals;
    std::array<std::uint64_t, kOrderBracketSetSize> members{};
    for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
    {
        const auto [first, last] = runOf(kBracketOf[interval], firstBins[interval], lastBins[interval]);
        intervals.first[interval] = first;
        intervals.last[interval] = last;
        members[interval] = runningOf[interval][lastBins[interval] + 1u] - runningOf[interval][firstBins[interval]];
    }
    if (members[0] + members[1] + members[2] > kTwoReadGatherSamples)
        return false;
    // Each chunk's place for each interval's members, in chunk order within the interval's run of the buffer.
    std::array<std::uint64_t, kOrderBracketSetSize> ends{members[0], members[0] + members[1],
                                                         members[0] + members[1] + members[2]};
    for (std::size_t interval = 0u; interval < kOrderBracketSetSize; ++interval)
    {
        std::uint64_t place = interval == 0u ? 0u : ends[interval - 1u];
        const std::uint64_t offset = kBracketOf[interval] * kBins;
        for (std::uint64_t chunk = 0u; chunk < chunks; ++chunk)
        {
            offsets[chunk * kOrderBracketSetSize + interval] = place;
            for (std::uint64_t bin = firstBins[interval]; bin <= lastBins[interval]; ++bin)
                place += tables[chunk * tableCounters + offset + bin];
        }
    }
    const Status gatheredStatus =
        gatherTwoReadIntervals(execution, reader, intervals, chunks, offsets, ends, gathered, operation);
    if (gatheredStatus.failed())
        return gatheredStatus;
    ++scans;

    // The median: its ranks among the gathered bins.
    const std::uint64_t belowMedian = tallies.belowSecond + medianRunning[medianFirstBin];
    std::sort(gathered, gathered + static_cast<std::size_t>(members[0]));
    const double median = interpolated(gathered[static_cast<std::size_t>(firstRank - belowMedian)],
                                       gathered[static_cast<std::size_t>(lastRank - belowMedian)]);

    // The deviation: every sample between the gathered runs is nearer the median than the farthest of them can be, and
    // every sample beyond them farther than the nearest, so the deviation's ranks lie among the gathered deviations,
    // past those between - which the gathered deviations are held to before a rank is read.
    const std::uint64_t outsideBelow = tallies.belowFirst + lowerRunning[firstBins[1]];
    const std::uint64_t outsideAbove = tallies.aboveThird + (upperSum - upperRunning[lastBins[2] + 1u]);
    const std::uint64_t inside = count - outsideBelow - members[1] - members[2] - outsideAbove;
    const std::uint64_t candidates = members[1] + members[2];
    if (firstRank < inside || lastRank - inside >= candidates)
        return false;
    double* const deviations = gathered + members[0];
    for (std::uint64_t index = 0u; index < candidates; ++index)
        deviations[index] = std::abs(deviations[index] - median);
    std::sort(deviations, deviations + static_cast<std::size_t>(candidates));
    const double firstDeviation = deviations[static_cast<std::size_t>(firstRank - inside)];
    const double lastDeviation = deviations[static_cast<std::size_t>(lastRank - inside)];
    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    const double insideReach = (std::max)(std::abs(std::nextafter(intervals.last[1], kInfinity) - median),
                                          std::abs(std::nextafter(intervals.first[2], -kInfinity) - median));
    const double outsideReach = (std::min)(std::abs(std::nextafter(intervals.first[1], -kInfinity) - median),
                                           std::abs(std::nextafter(intervals.last[2], kInfinity) - median));
    if (!(insideReach <= firstDeviation) || !(lastDeviation <= outsideReach))
        return false;

    result.minimum = minimum;
    result.maximum = maximum;
    result.median = median;
    result.medianAbsoluteDeviation = interpolated(firstDeviation, lastDeviation);
    result.run.selection = ValidityCount{.valid = count, .total = itemCount};
    result.run.outputsWritten = 4u;
    result.deterministicChunks = chunks;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunks));
    result.planeScans = scans;
    result.peakScratchBytes = scratchBytes;
    return true;
}

/// Runs the whole selection for one representation.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param source the checked component.
/// @param quantiles the fractions to select.
/// @param values where their values go.
/// @param request the interpolation rule and whether the deviation is wanted.
/// @param validity the value rule and optional packed mask.
/// @param window where the component sits inside that mask, and the interval narrowing it.
/// @param operation the entry point the caller is inside.
/// @return the statistics, or the first failure.
template <ResidentSample SampleT>
[[nodiscard]] Result<PlaneOrderStatistics>
selectAs(const ExecutionContext& execution, const KernelPlaneSource& source, std::span<const double> quantiles,
         std::span<double> values, const PlaneOrderRequest& request, const KernelValidityView& validity,
         const plane_detail::PlaneWindow& window, const char* operation)
{
    const plane_detail::PlaneSampleReader<SampleT> reader(source, validity, window);
    if constexpr (kCountsFromCodeTable<SampleT>)
    {
        PlaneOrderStatistics fromTable;
        const Result<bool> answered =
            selectFromCodeTable(execution, reader, quantiles, values, request, operation, fromTable);
        if (!answered.hasValue())
        {
            return answered.status();
        }
        if (*answered)
        {
            return fromTable;
        }
    }
    // A median with its deviation over a large Float32 plane every sample of which is selected but for its window is
    // counted and gathered in two reads where the plane allows it - the representation the compiled runs count, which
    // every other representation's display is measured in. What two reads hand back is narrowed below, and the reads
    // they took are counted with the narrowing's own.
    std::uint64_t twoReadScans = 0u;
    std::uint64_t twoReadScratch = 0u;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        if (request.medianAbsoluteDeviation && quantiles.empty())
        {
            PlaneOrderStatistics twoRead;
            const Result<bool> answered = selectMedianAndDeviationInTwoReads(execution, reader, request, operation,
                                                                             twoRead, twoReadScans, twoReadScratch);
            if (!answered.hasValue())
            {
                return answered.status();
            }
            if (*answered)
            {
                return twoRead;
            }
        }
    }
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    const TaskCost sampleCost{.bytesPerItem = sizeof(SampleT), .operationsPerItem = 8u};
    const std::uint64_t chunkCount =
        (std::min)(kMaxSelectionChunks, chunksWithinScratch(deterministicChunkCount(itemCount, sampleCost),
                                                            kOrderDigitBuckets * sizeof(std::uint64_t)));

    const std::uint64_t extremeBytes = chunkCount * sizeof(SelectionExtremes);
    Result<plane_detail::ScopedContextMemory> extremes =
        plane_detail::ScopedContextMemory::take(execution, extremeBytes, alignof(SelectionExtremes), operation);
    if (!extremes.hasValue())
    {
        return extremes.status();
    }
    const NarrowingPlan narrowing = selectionNarrowingPlan(itemCount, chunkCount, reader.unmaskedValues());
    const std::uint64_t partialBytes =
        (std::max)(chunkCount * kOrderDigitBuckets, narrowing.chunks * narrowing.bins) * sizeof(std::uint64_t);
    Result<plane_detail::ScopedContextMemory> partials =
        plane_detail::ScopedContextMemory::take(execution, partialBytes, alignof(std::uint64_t), operation);
    if (!partials.hasValue())
    {
        return partials.status();
    }
    const std::uint64_t offsetBytes = chunkCount * sizeof(std::uint64_t);
    Result<plane_detail::ScopedContextMemory> offsets =
        plane_detail::ScopedContextMemory::take(execution, offsetBytes, alignof(std::uint64_t), operation);
    if (!offsets.hasValue())
    {
        return offsets.status();
    }
    const std::uint64_t gatherBytes = static_cast<std::uint64_t>(kMaxOrderGatherSamples) * sizeof(double);
    Result<plane_detail::ScopedContextMemory> gathered =
        plane_detail::ScopedContextMemory::take(execution, gatherBytes, alignof(double), operation);
    if (!gathered.hasValue())
    {
        return gathered.status();
    }

    SelectionExtremes* const seen = static_cast<SelectionExtremes*>(extremes->data());
    std::fill_n(seen, static_cast<std::size_t>(chunkCount), SelectionExtremes{});
    Status status = countSelected(execution, reader, chunkCount, seen, operation);
    if (status.failed())
    {
        return status;
    }
    PlaneOrderStatistics result;
    result.planeScans = 1u;
    std::uint64_t count = 0u;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        count += seen[chunk].count;
        minimum = (std::min)(minimum, seen[chunk].minimum);
        maximum = (std::max)(maximum, seen[chunk].maximum);
    }
    if (count == 0u)
    {
        return orderFailure(ErrorCode::EmptySelection, operation, "the selection rules selected no samples");
    }
    result.minimum = minimum;
    result.maximum = maximum;

    SelectionState<SampleT> state{.execution = execution,
                                  .reader = reader,
                                  .chunkCount = chunkCount,
                                  .narrowing = narrowing,
                                  .extremes = seen,
                                  .partials = static_cast<std::uint64_t*>(partials->data()),
                                  .offsets = static_cast<std::uint64_t*>(offsets->data()),
                                  .gathered = static_cast<double*>(gathered->data()),
                                  .scans = 0u,
                                  .operation = operation};

    bool selectedFromGrid = false;
    if constexpr (std::is_same_v<SampleT, float>)
    {
        const Result<bool> grid = selectNormalizedIntegerGrid(state, quantiles, values, request, count, result);
        if (!grid.hasValue())
            return grid.status();
        selectedFromGrid = *grid;
    }
    if (!selectedFromGrid)
    {
        status = selectQuantile(state, false, 0.0, minimum, maximum, count, 0.5, request.interpolation, result.median);
        if (status.failed())
        {
            return status;
        }
        for (std::size_t index = 0u; index < quantiles.size(); ++index)
        {
            // The two endpoints are already exact from the counting pass, so asking for them costs no
            // descent at all; every rule agrees on them because the position is an integer there.
            if (quantiles[index] == 0.0)
            {
                values[index] = minimum;
                continue;
            }
            if (quantiles[index] == 1.0)
            {
                values[index] = maximum;
                continue;
            }
            if (quantiles[index] == 0.5)
            {
                // One half is always resolved, because the deviation is defined around it. A caller
                // that also names it is asking for the same number under the same rule.
                values[index] = result.median;
                continue;
            }
            status = selectQuantile(state, false, 0.0, minimum, maximum, count, quantiles[index], request.interpolation,
                                    values[index]);
            if (status.failed())
            {
                return status;
            }
        }
        result.medianAbsoluteDeviation = std::numeric_limits<double>::quiet_NaN();
        if (request.medianAbsoluteDeviation)
        {
            // A deviation from the median is at least zero and at most the further of the two extremes,
            // so the interval a narrowing needs is already known and costs no read to find.
            const double furthest = (std::max)(result.median - minimum, maximum - result.median);
            status = selectQuantile(state, true, result.median, 0.0, furthest, count, 0.5, request.interpolation,
                                    result.medianAbsoluteDeviation);
            if (status.failed())
            {
                return status;
            }
        }
    }

    result.run.selection = ValidityCount{.valid = count, .total = itemCount};
    result.run.outputsWritten =
        static_cast<std::uint64_t>(quantiles.size()) + (request.medianAbsoluteDeviation ? 4u : 3u);
    result.deterministicChunks = chunkCount;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    result.planeScans += state.scans + twoReadScans;
    result.peakScratchBytes = (std::max)(extremeBytes + partialBytes + offsetBytes + gatherBytes, twoReadScratch);
    return result;
}

/// Runs the whole histogram for one representation.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param source the checked component.
/// @param bins the caller's counters.
/// @param request the bin and domain contract.
/// @param validity the value rule and optional packed mask.
/// @param window where the component sits inside that mask, and the interval narrowing it.
/// @param operation the entry point the caller is inside.
/// @return the counts, or the first failure.
template <ResidentSample SampleT>
[[nodiscard]] Result<PlaneHistogramResult>
histogramAs(const ExecutionContext& execution, const KernelPlaneSource& source, std::span<std::uint64_t> bins,
            const PlaneHistogramRequest& request, const KernelValidityView& validity,
            const plane_detail::PlaneWindow& window, const char* operation)
{
    double lowest = 0.0;
    double highest = 0.0;
    resolveInterval<SampleT>(request, lowest, highest);
    if (!std::isfinite(lowest) || !std::isfinite(highest) || !(highest > lowest))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation,
                            "the histogram interval must be finite and increasing");
    }

    const plane_detail::PlaneSampleReader<SampleT> reader(source, validity, window);
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    const std::uint64_t binCount = static_cast<std::uint64_t>(bins.size());
    const std::uint64_t stride = binCount + kHistogramCounters;
    const TaskCost sampleCost{.bytesPerItem = sizeof(SampleT), .operationsPerItem = 8u};
    const std::uint64_t chunkCount =
        chunksWithinScratch(deterministicChunkCount(itemCount, sampleCost), stride * sizeof(std::uint64_t));
    const std::uint64_t partialBytes = chunkCount * stride * sizeof(std::uint64_t);

    if constexpr (std::is_same_v<SampleT, std::uint16_t>)
    {
        const std::uint64_t largestChunk = itemCount / chunkCount + (itemCount % chunkCount == 0u ? 0u : 1u);
        if (request.domain == HistogramDomain::RepresentationFullScale && binCount == 65536u &&
            validity.descriptor.rule() == ValidityRule::AllValid && validity.mask == nullptr &&
            !window.hasValueWindow && reader.columnStride() == 1u &&
            largestChunk <= (std::numeric_limits<std::uint32_t>::max)())
        {
            return fullScaleUInt16Histogram(execution, reader, bins, chunkCount, lowest, highest, operation);
        }
    }
    if constexpr (kCountsFromCodeTable<SampleT>)
    {
        PlaneHistogramResult fromTable;
        const Result<bool> answered =
            histogramFromCodeTable(execution, reader, bins, lowest, highest, operation, fromTable);
        if (!answered.hasValue())
        {
            return answered.status();
        }
        if (*answered)
        {
            return fromTable;
        }
    }

    Result<plane_detail::ScopedContextMemory> partials =
        plane_detail::ScopedContextMemory::take(execution, partialBytes, alignof(std::uint64_t), operation);
    if (!partials.hasValue())
    {
        return partials.status();
    }
    std::uint64_t* const counters = static_cast<std::uint64_t*>(partials->data());
    const Status status = accumulateBins(execution, reader, binCount, lowest, highest, chunkCount, counters);
    if (status.failed())
    {
        return status;
    }

    // Merging is a column sum over the per-chunk tables, and for a wide histogram it reads more
    // memory than the counting pass did. Counts are exact integers, so the sum is associative and
    // dividing it over bin ranges changes no bin; each range is walked chunk by chunk so that every
    // read is contiguous within one table.
    const std::uint64_t mergeChunks =
        (std::min)(binCount,
                   deterministicChunkCount(binCount, TaskCost{.bytesPerItem = chunkCount * sizeof(std::uint64_t),
                                                              .operationsPerItem = chunkCount}));
    const Status merged =
        parallelFor(execution, mergeChunks,
                    TaskCost{.bytesPerItem = chunkCount * sizeof(std::uint64_t), .operationsPerItem = chunkCount},
                    [&](TaskRange mergeIndices) -> Status {
                        for (std::uint64_t index = mergeIndices.begin; index < mergeIndices.end; ++index)
                        {
                            const TaskRange range = plane_detail::deterministicRange(index, binCount, mergeChunks);
                            for (std::uint64_t bin = range.begin; bin < range.end; ++bin)
                            {
                                bins[static_cast<std::size_t>(bin)] = 0u;
                            }
                            for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
                            {
                                const std::uint64_t* const chunkBins = counters + chunk * stride;
                                for (std::uint64_t bin = range.begin; bin < range.end; ++bin)
                                {
                                    bins[static_cast<std::size_t>(bin)] += chunkBins[bin];
                                }
                            }
                        }
                        return Status::success();
                    });
    if (merged.failed())
    {
        return merged;
    }
    std::uint64_t selected = 0u;
    std::uint64_t below = 0u;
    std::uint64_t above = 0u;
    std::uint64_t notANumber = 0u;
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        const std::uint64_t* const chunkCounters = counters + chunk * stride + binCount;
        selected += chunkCounters[0];
        below += chunkCounters[1];
        above += chunkCounters[2];
        notANumber += chunkCounters[3];
    }

    PlaneHistogramResult result;
    result.run.selection = ValidityCount{.valid = selected, .total = itemCount};
    result.run.outputsWritten = selected - below - above - notANumber;
    result.deterministicChunks = chunkCount;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    result.partialBytes = partialBytes;
    result.lowest = lowest;
    result.highest = highest;
    result.belowRange = below;
    result.aboveRange = above;
    result.notANumber = notANumber;
    return result;
}

/// The window sample at one coordinate, or nothing when the validity rule excludes it.
///
/// @param source the input plane.
/// @param accepts the validity rule.
/// @param request the window and border contract.
/// @param x the column, which may be outside the plane.
/// @param y the row, which may be outside the plane.
/// @param value where the sample goes.
/// @return true when the coordinate contributes a sample.
[[nodiscard]] bool windowSampleAt(PlaneView<const float> source, const reference_detail::ValidityAccessor& accepts,
                                  const OrderWindowRequest& request, std::int64_t x, std::int64_t y,
                                  float& value) noexcept
{
    const std::int64_t width = static_cast<std::int64_t>(source.width());
    const std::int64_t height = static_cast<std::int64_t>(source.height());
    const std::int64_t sourceX = kernelBorderIndex(x, width, request.border);
    const std::int64_t sourceY = kernelBorderIndex(y, height, request.border);
    if (sourceX == width || sourceY == height)
    {
        if (request.border == KernelBorderMode::NotApplicable)
        {
            return false;
        }
        value = request.constantBorder;
        return true;
    }
    const std::uint32_t column = static_cast<std::uint32_t>(sourceX);
    const std::uint32_t row = static_cast<std::uint32_t>(sourceY);
    const float sample = (*source.row(row))[column];
    if (!accepts.accepts(sample, column, row))
    {
        return false;
    }
    value = sample;
    return true;
}

/// Refuses selected non-finite samples before any window output is changed.
///
/// @param execution the operation policy.
/// @param source the input plane.
/// @param accepts the validity rule.
/// @param operation the entry point the refusal names.
/// @return success, or `InvalidData` when a selected sample is not finite.
[[nodiscard]] Status checkWindowSamples(const ExecutionContext& execution, PlaneView<const float> source,
                                        const reference_detail::ValidityAccessor& accepts, const char* operation)
{
    const std::uint32_t width = source.width();
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(width) * sizeof(float),
                           .operationsPerItem = width};
    return parallelFor(execution, source.height(), rowCost, [&](TaskRange rows) -> Status {
        for (std::uint64_t row = rows.begin; row < rows.end; ++row)
        {
            const std::uint32_t y = static_cast<std::uint32_t>(row);
            const std::span<const float> samples = *source.row(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                if (accepts.accepts(samples[x], x, y) && !std::isfinite(samples[x]))
                {
                    return orderFailure(ErrorCode::InvalidData, operation,
                                        "a selected window sample is not finite; use a validity rule that excludes "
                                        "it");
                }
            }
        }
        return Status::success();
    });
}

/// The quantile of one already sorted window.
///
/// @param window the ascending samples.
/// @param held how many of them there are. A window of one or none answers with the first entry,
///        which for none is whatever the buffer held.
/// @param quantile the fraction, within `[0, 1]`.
/// @param interpolation which rule resolves a position between two ranks.
/// @return the selected value.
[[nodiscard]] double windowQuantile(const float* window, std::uint32_t held, double quantile,
                                    QuantileInterpolation interpolation) noexcept
{
    if (held <= 1u)
    {
        // Every rule agrees on a window of one, and a window of none has no rank to resolve: the
        // centre sample always contributes, so the caller never asks, and `held - 1u` on an unsigned
        // count would otherwise wrap into an index far past the buffer rather than answering badly.
        return static_cast<double>(window[0]);
    }
    const double position = quantile * static_cast<double>(held - 1u);
    const double floored = std::floor(position);
    const std::uint32_t lower = static_cast<std::uint32_t>(floored);
    const double fraction = position - floored;
    if (interpolation == QuantileInterpolation::NearestRank)
    {
        return static_cast<double>(window[static_cast<std::uint32_t>(std::floor(position + 0.5))]);
    }
    const double first = static_cast<double>(window[lower]);
    if (interpolation == QuantileInterpolation::LowerRank || fraction == 0.0)
    {
        return first;
    }
    return first + fraction * (static_cast<double>(window[lower + 1u]) - first);
}

/// Copies every selected sample into one caller-sized buffer, in row-major source order.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param reader the checked component and its validity rule.
/// @param chunkCount the fixed partition.
/// @param offsets one write offset per chunk, already the prefix sum of the per-chunk counts.
/// @param count how many samples the counts selected over every chunk, which is where the last chunk's samples end.
/// @param gathered where the values go.
/// @return success, or the first failure in chunk order; `ErrorCode::Internal` when a chunk gathered other than the
///         samples its count counted. No chunk writes past the slot its count sized: a sample past it is counted and
///         not written, so a caller's buffer of `count` values is never written past.
template <ResidentSample SampleT>
[[nodiscard]] Status gatherSelected(const ExecutionContext& execution,
                                    const plane_detail::PlaneSampleReader<SampleT>& reader, std::uint64_t chunkCount,
                                    const std::uint64_t* offsets, std::uint64_t count, double* gathered)
{
    const std::uint32_t width = reader.width();
    const std::uint64_t itemCount = static_cast<std::uint64_t>(width) * reader.height();
    const std::uint32_t columnStride = reader.columnStride();
    return parallelFor(
        execution, chunkCount, chunkCostOf<SampleT>(itemCount, chunkCount), [&](TaskRange chunkIndices) -> Status {
            for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
            {
                std::uint64_t write = offsets[chunk];
                const std::uint64_t slotEnd = chunk + 1u < chunkCount ? offsets[chunk + 1u] : count;
                const TaskRange range = plane_detail::deterministicRange(chunk, itemCount, chunkCount);
                std::uint64_t index = range.begin;
                while (index < range.end)
                {
                    const RowRun run = nextRun(index, range.end, width);
                    const SampleT* const row = reader.rowStart(run.y);
                    plane_detail::PlaneMaskCursor validityBits = reader.maskCursor(run.y, run.x);
                    for (std::uint64_t offset = 0u; offset < run.length; ++offset)
                    {
                        const std::uint32_t x = run.x + static_cast<std::uint32_t>(offset);
                        double value = 0.0;
                        if (!validityBits.allowsAndAdvance() ||
                            !reader.selectedAfterMask(row[static_cast<std::size_t>(x) * columnStride], value))
                        {
                            continue;
                        }
                        if (write < slotEnd)
                            gathered[write] = value;
                        ++write;
                    }
                }
                if (write != slotEnd)
                {
                    return orderFailure(ErrorCode::Internal, kCompactionOperation,
                                        "a chunk's gather wrote other than the samples its count counted");
                }
            }
            return Status::success();
        });
}

/// Counts, and optionally gathers, one representation's selected samples.
///
/// @tparam SampleT the resident type.
/// @param execution the operation.
/// @param source the checked component, already reduced to the selected rectangle.
/// @param validity the value rule and optional packed mask.
/// @param window where that rectangle sits inside the mask, and the interval narrowing it.
/// @param destination where the samples go, or empty to count without writing.
/// @return the counts and extremes, or the first failure.
template <ResidentSample SampleT>
[[nodiscard]] Result<PlaneCompactionResult>
compactAs(const ExecutionContext& execution, const KernelPlaneSource& source, const KernelValidityView& validity,
          const plane_detail::PlaneWindow& window, std::span<double> destination)
{
    const plane_detail::PlaneSampleReader<SampleT> reader(source, validity, window);
    const std::uint64_t itemCount = static_cast<std::uint64_t>(reader.width()) * reader.height();
    const TaskCost sampleCost{.bytesPerItem = sizeof(SampleT), .operationsPerItem = 8u};
    const std::uint64_t chunkCount = chunksWithinScratch(deterministicChunkCount(itemCount, sampleCost),
                                                         sizeof(SelectionExtremes) + sizeof(std::uint64_t));

    Result<plane_detail::ScopedContextMemory> extremes = plane_detail::ScopedContextMemory::take(
        execution, chunkCount * sizeof(SelectionExtremes), alignof(SelectionExtremes), kCompactionOperation);
    if (!extremes.hasValue())
    {
        return extremes.status();
    }
    Result<plane_detail::ScopedContextMemory> offsets = plane_detail::ScopedContextMemory::take(
        execution, chunkCount * sizeof(std::uint64_t), alignof(std::uint64_t), kCompactionOperation);
    if (!offsets.hasValue())
    {
        return offsets.status();
    }

    SelectionExtremes* const seen = static_cast<SelectionExtremes*>(extremes->data());
    std::fill_n(seen, static_cast<std::size_t>(chunkCount), SelectionExtremes{});
    const Status counted = countSelected(execution, reader, chunkCount, seen, kCompactionOperation);
    if (counted.failed())
    {
        return counted;
    }

    PlaneCompactionResult result;
    result.deterministicChunks = chunkCount;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    std::uint64_t count = 0u;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        count += seen[chunk].count;
        minimum = (std::min)(minimum, seen[chunk].minimum);
        maximum = (std::max)(maximum, seen[chunk].maximum);
    }
    if (count == 0u)
    {
        return orderFailure(ErrorCode::EmptySelection, kCompactionOperation, "the selection rules selected no samples");
    }
    result.run.selection = ValidityCount{.valid = count, .total = itemCount};
    result.minimum = minimum;
    result.maximum = maximum;
    if (destination.empty())
    {
        return result;
    }
    if (static_cast<std::uint64_t>(destination.size()) < count)
    {
        return Status::failure(
            ErrorCode::IncompatibleArguments,
            StatusDetail{.message = "the compaction destination is shorter than the selected sample count",
                         .operation = kCompactionOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, count),
                                     StatusContextField::ofCount(ContextKey::Available,
                                                                 static_cast<std::uint64_t>(destination.size()))}});
    }

    std::uint64_t* const writeOffsets = static_cast<std::uint64_t*>(offsets->data());
    std::uint64_t running = 0u;
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        writeOffsets[chunk] = running;
        running += seen[chunk].count;
    }
    const Status gathered = gatherSelected(execution, reader, chunkCount, writeOffsets, count, destination.data());
    if (gathered.failed())
    {
        return gathered;
    }
    result.run.outputsWritten = count;
    return result;
}

/// Checks the arguments both histogram entry points share.
///
/// @param source the complete component the caller named.
/// @param bins the caller-owned counters.
/// @param request the bin and domain contract.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason the call cannot proceed.
[[nodiscard]] Status validateHistogramArguments(const KernelPlaneSource& source, std::span<std::uint64_t> bins,
                                                const PlaneHistogramRequest& request, const char* operation)
{
    if (bins.empty())
    {
        return orderFailure(ErrorCode::InvalidArgument, operation, "a histogram needs at least one bin");
    }
    if (bins.size() > kMaxHistogramBins)
    {
        return orderFailure(ErrorCode::OutOfRange, operation,
                            "the histogram asks for more bins than the stated ceiling");
    }
    if (request.domain != HistogramDomain::Stated && request.domain != HistogramDomain::RepresentationFullScale)
    {
        return orderFailure(ErrorCode::InvalidArgument, operation, "the histogram request names no known domain");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(),
                       reinterpret_cast<const std::uint8_t*>(bins.data()),
                       static_cast<std::uint64_t>(bins.size_bytes())))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation, "the histogram bins overlap the source samples");
    }
    return Status::success();
}

/// A selection to read, with any interval rule restated as its value window.
struct IntervalFold
{
    /// The value rule and optional packed mask to read with.
    KernelValidityView validity;

    /// Where the component sits inside the mask, and the interval narrowing it.
    plane_detail::PlaneWindow window;
};

/// Restates a rule that keeps one closed interval of widened values as that interval.
///
/// A range rule, and a finite-only rule over a floating representation, select exactly the samples
/// whose widened value lies in one closed interval - a value that is not a number lies in none - and
/// a value window narrows the rule to the intersection of the two. Read as all-valid samples inside
/// that intersection, the selection takes the scans that read a row of samples at a time and the
/// sampled bracket, which a rule turns away. Finite-only over an integer representation admits every
/// value and is read as all-valid. A rule beside a mask, a range with an infinite end - which admits
/// an infinite sample no finite window can - and an empty intersection are left as they are.
///
/// @param sampleType the representation the rule is applied to.
/// @param validity the value rule and optional packed mask.
/// @param window where the component sits inside the mask, and the interval narrowing it.
/// @return the selection to read, which selects the same samples.
[[nodiscard]] IntervalFold foldIntervalRule(SampleType sampleType, const KernelValidityView& validity,
                                            const plane_detail::PlaneWindow& window) noexcept
{
    IntervalFold folded{.validity = validity, .window = window};
    if (validity.mask != nullptr)
    {
        return folded;
    }
    double lowest = 0.0;
    double highest = 0.0;
    switch (validity.descriptor.rule())
    {
    case ValidityRule::DomainRange: {
        const std::optional<double> low = validity.descriptor.lowestValid();
        const std::optional<double> high = validity.descriptor.highestValid();
        if (!low.has_value() || !high.has_value() || !std::isfinite(*low) || !std::isfinite(*high))
        {
            return folded;
        }
        lowest = *low;
        highest = *high;
        break;
    }
    case ValidityRule::FiniteOnly:
        if (sampleType == SampleType::Float32)
        {
            lowest = -static_cast<double>((std::numeric_limits<float>::max)());
            highest = static_cast<double>((std::numeric_limits<float>::max)());
            break;
        }
        if (sampleType == SampleType::Float64)
        {
            lowest = -(std::numeric_limits<double>::max)();
            highest = (std::numeric_limits<double>::max)();
            break;
        }
        folded.validity.descriptor = ValidityDescriptor{};
        return folded;
    default:
        return folded;
    }
    if (window.hasValueWindow)
    {
        lowest = (std::max)(lowest, window.lowestValue);
        highest = (std::min)(highest, window.highestValue);
    }
    if (!(lowest <= highest))
    {
        return folded;
    }
    folded.validity.descriptor = ValidityDescriptor{};
    folded.window.hasValueWindow = true;
    folded.window.lowestValue = lowest;
    folded.window.highestValue = highest;
    return folded;
}

/// Runs the histogram on the representation the checked component holds.
///
/// @param execution the operation.
/// @param source the checked component, already reduced to the selected rectangle.
/// @param bins the caller-owned counters.
/// @param request the bin and domain contract.
/// @param validity the value rule and optional packed mask.
/// @param window where the component sits inside that mask, and the interval narrowing it.
/// @param operation the entry point the caller is inside.
/// @return the counts, or the first failure.
[[nodiscard]] Result<PlaneHistogramResult>
dispatchHistogram(const ExecutionContext& execution, const KernelPlaneSource& source, std::span<std::uint64_t> bins,
                  const PlaneHistogramRequest& request, const KernelValidityView& validity,
                  const plane_detail::PlaneWindow& window, const char* operation)
{
    const IntervalFold folded = foldIntervalRule(source.sampleType, validity, window);
    switch (source.sampleType)
    {
    case SampleType::UInt8:
        return histogramAs<std::uint8_t>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::Int8:
        return histogramAs<std::int8_t>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::UInt16:
        return histogramAs<std::uint16_t>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::Int16:
        return histogramAs<std::int16_t>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::UInt32:
        return histogramAs<std::uint32_t>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::Int32:
        return histogramAs<std::int32_t>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::Float32:
        return histogramAs<float>(execution, source, bins, request, folded.validity, folded.window, operation);
    case SampleType::Float64:
        return histogramAs<double>(execution, source, bins, request, folded.validity, folded.window, operation);
    }
    return orderFailure(ErrorCode::InvalidArgument, operation, "the histogram source names no resident sample type");
}

/// Checks the arguments both selection entry points share.
///
/// @param source the complete component the caller named.
/// @param quantiles the requested fractions.
/// @param values where they are to be written.
/// @param request the interpolation rule and deviation flag.
/// @param operation the entry point the caller is inside.
/// @return success, or the first reason the call cannot proceed.
[[nodiscard]] Status validateSelectionArguments(const KernelPlaneSource& source, std::span<const double> quantiles,
                                                std::span<double> values, const PlaneOrderRequest& request,
                                                const char* operation)
{
    if (quantiles.size() != values.size())
    {
        return orderFailure(ErrorCode::IncompatibleArguments, operation,
                            "the selection output span is not as long as its quantile span");
    }
    if (quantiles.size() > kMaxOrderStatisticRanks)
    {
        return orderFailure(ErrorCode::OutOfRange, operation,
                            "the selection asks for more quantiles than the stated ceiling");
    }
    if (!isQuantileInterpolation(request.interpolation))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation,
                            "the selection request names no known interpolation rule");
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(),
                       reinterpret_cast<const std::uint8_t*>(values.data()),
                       static_cast<std::uint64_t>(values.size_bytes())))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation, "the selection output overlaps the source samples");
    }
    if (regionsOverlap(
            reinterpret_cast<const std::uint8_t*>(quantiles.data()), static_cast<std::uint64_t>(quantiles.size_bytes()),
            reinterpret_cast<const std::uint8_t*>(values.data()), static_cast<std::uint64_t>(values.size_bytes())))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation, "the selection output overlaps its quantile inputs");
    }
    for (const double quantile : quantiles)
    {
        if (!(quantile >= 0.0) || !(quantile <= 1.0))
        {
            return orderFailure(ErrorCode::OutOfRange, operation,
                                "a requested quantile is outside the closed unit interval");
        }
    }
    if (request.hold.has_value() && (!std::isfinite(request.hold->lowest) || !std::isfinite(request.hold->highest) ||
                                     request.hold->lowest > request.hold->highest))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation,
                            "the selection's hold is not a finite ordered closed interval");
    }
    return Status::success();
}

/// A selection's window with the request's hold stated on it.
///
/// @param window where the component sits inside the mask, and the interval narrowing it.
/// @param request the selection request, whose hold is carried onto the window.
/// @return the window, reading every admitted sample as held when the request asks.
[[nodiscard]] plane_detail::PlaneWindow withHold(plane_detail::PlaneWindow window,
                                                 const PlaneOrderRequest& request) noexcept
{
    window.hasHold = request.hold.has_value();
    window.holdLowest = request.hold.has_value() ? request.hold->lowest : 0.0;
    window.holdHighest = request.hold.has_value() ? request.hold->highest : 0.0;
    return window;
}

/// Runs the selection on the representation the checked component holds.
///
/// @param execution the operation.
/// @param source the checked component, already reduced to the selected rectangle.
/// @param quantiles the requested fractions.
/// @param values where they are written.
/// @param request the interpolation rule and deviation flag.
/// @param validity the value rule and optional packed mask.
/// @param window where the component sits inside that mask, and the interval narrowing it.
/// @param operation the entry point the caller is inside.
/// @return the statistics, or the first failure.
[[nodiscard]] Result<PlaneOrderStatistics>
dispatchSelection(const ExecutionContext& execution, const KernelPlaneSource& source, std::span<const double> quantiles,
                  std::span<double> values, const PlaneOrderRequest& request, const KernelValidityView& validity,
                  const plane_detail::PlaneWindow& window, const char* operation)
{
    // The rule and the window decide on the stored value, and an admitted sample is then read as held.
    IntervalFold folded = foldIntervalRule(source.sampleType, validity, window);
    folded.window = withHold(folded.window, request);
    switch (source.sampleType)
    {
    case SampleType::UInt8:
        return selectAs<std::uint8_t>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                      operation);
    case SampleType::Int8:
        return selectAs<std::int8_t>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                     operation);
    case SampleType::UInt16:
        return selectAs<std::uint16_t>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                       operation);
    case SampleType::Int16:
        return selectAs<std::int16_t>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                      operation);
    case SampleType::UInt32:
        return selectAs<std::uint32_t>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                       operation);
    case SampleType::Int32:
        return selectAs<std::int32_t>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                      operation);
    case SampleType::Float32:
        return selectAs<float>(execution, source, quantiles, values, request, folded.validity, folded.window,
                               operation);
    case SampleType::Float64:
        return selectAs<double>(execution, source, quantiles, values, request, folded.validity, folded.window,
                                operation);
    }
    return orderFailure(ErrorCode::InvalidArgument, operation, "the selection source names no resident sample type");
}

/// Runs the compaction on the representation the checked component holds.
///
/// @param execution the operation.
/// @param source the checked component, already reduced to the selected rectangle.
/// @param validity the value rule and optional packed mask.
/// @param window where the component sits inside that mask, and the interval narrowing it.
/// @param destination where the samples go, or empty to count without writing.
/// @return the counts and extremes, or the first failure.
[[nodiscard]] Result<PlaneCompactionResult> dispatchCompaction(const ExecutionContext& execution,
                                                               const KernelPlaneSource& source,
                                                               const KernelValidityView& validity,
                                                               const plane_detail::PlaneWindow& window,
                                                               std::span<double> destination)
{
    const IntervalFold folded = foldIntervalRule(source.sampleType, validity, window);
    switch (source.sampleType)
    {
    case SampleType::UInt8:
        return compactAs<std::uint8_t>(execution, source, folded.validity, folded.window, destination);
    case SampleType::Int8:
        return compactAs<std::int8_t>(execution, source, folded.validity, folded.window, destination);
    case SampleType::UInt16:
        return compactAs<std::uint16_t>(execution, source, folded.validity, folded.window, destination);
    case SampleType::Int16:
        return compactAs<std::int16_t>(execution, source, folded.validity, folded.window, destination);
    case SampleType::UInt32:
        return compactAs<std::uint32_t>(execution, source, folded.validity, folded.window, destination);
    case SampleType::Int32:
        return compactAs<std::int32_t>(execution, source, folded.validity, folded.window, destination);
    case SampleType::Float32:
        return compactAs<float>(execution, source, folded.validity, folded.window, destination);
    case SampleType::Float64:
        return compactAs<double>(execution, source, folded.validity, folded.window, destination);
    }
    return orderFailure(ErrorCode::InvalidArgument, kCompactionOperation,
                        "the compaction source names no resident sample type");
}

} // namespace

Result<PlaneHistogramResult> accumulatePlaneHistogram(const ExecutionContext& execution,
                                                      const KernelPlaneSource& source, std::span<std::uint64_t> bins,
                                                      const PlaneHistogramRequest& request,
                                                      const KernelValidityView& validity)
{
    const Status ready = plane_detail::checkPlaneSource(execution, source, validity, kHistogramOperation);
    if (ready.failed())
    {
        return ready;
    }
    const Status arguments = validateHistogramArguments(source, bins, request, kHistogramOperation);
    if (arguments.failed())
    {
        return arguments;
    }
    return dispatchHistogram(execution, source, bins, request, validity, plane_detail::PlaneWindow{},
                             kHistogramOperation);
}

Result<PlaneHistogramResult>
accumulatePlaneHistogramOver(const ExecutionContext& execution, const KernelPlaneSource& source,
                             std::span<std::uint64_t> bins, const PlaneHistogramRequest& request,
                             const KernelValidityView& validity, const KernelPlaneSelection& selection)
{
    const Status ready = plane_detail::checkPlaneSource(execution, source, validity, kHistogramOverOperation);
    if (ready.failed())
    {
        return ready;
    }
    const Status arguments = validateHistogramArguments(source, bins, request, kHistogramOverOperation);
    if (arguments.failed())
    {
        return arguments;
    }
    if (selection.width == 0u || selection.height == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = 0u}, kHistogramOverOperation);
    }
    const Result<plane_detail::PlaneRectangle> rectangle =
        plane_detail::describePlaneRectangle(execution, source, selection, kHistogramOverOperation);
    if (!rectangle.hasValue())
    {
        return rectangle.status();
    }
    const KernelPlaneSource rectangleSource{.sampleType = source.sampleType,
                                            .samples = rectangle->samples,
                                            .layout = &rectangle->layout,
                                            .plane = source.plane,
                                            .sampleInPixel = source.sampleInPixel};
    return dispatchHistogram(execution, rectangleSource, bins, request, validity, rectangle->window,
                             kHistogramOverOperation);
}

Result<PlaneOrderStatistics> selectPlaneOrderStatistics(const ExecutionContext& execution,
                                                        const KernelPlaneSource& source,
                                                        std::span<const double> quantiles, std::span<double> values,
                                                        const PlaneOrderRequest& request,
                                                        const KernelValidityView& validity)
{
    const Status ready = plane_detail::checkPlaneSource(execution, source, validity, kSelectionOperation);
    if (ready.failed())
    {
        return ready;
    }
    const Status arguments = validateSelectionArguments(source, quantiles, values, request, kSelectionOperation);
    if (arguments.failed())
    {
        return arguments;
    }
    return dispatchSelection(execution, source, quantiles, values, request, validity, plane_detail::PlaneWindow{},
                             kSelectionOperation);
}

Result<PlaneOrderStatistics> selectPlaneOrderStatisticsOver(const ExecutionContext& execution,
                                                            const KernelPlaneSource& source,
                                                            std::span<const double> quantiles, std::span<double> values,
                                                            const PlaneOrderRequest& request,
                                                            const KernelValidityView& validity,
                                                            const KernelPlaneSelection& selection)
{
    const Status ready = plane_detail::checkPlaneSource(execution, source, validity, kSelectionOverOperation);
    if (ready.failed())
    {
        return ready;
    }
    const Status arguments = validateSelectionArguments(source, quantiles, values, request, kSelectionOverOperation);
    if (arguments.failed())
    {
        return arguments;
    }
    if (selection.width == 0u || selection.height == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = 0u}, kSelectionOverOperation);
    }
    const Result<plane_detail::PlaneRectangle> rectangle =
        plane_detail::describePlaneRectangle(execution, source, selection, kSelectionOverOperation);
    if (!rectangle.hasValue())
    {
        return rectangle.status();
    }
    const KernelPlaneSource rectangleSource{.sampleType = source.sampleType,
                                            .samples = rectangle->samples,
                                            .layout = &rectangle->layout,
                                            .plane = source.plane,
                                            .sampleInPixel = source.sampleInPixel};
    return dispatchSelection(execution, rectangleSource, quantiles, values, request, validity, rectangle->window,
                             kSelectionOverOperation);
}

Result<PlaneCompactionResult> compactSelectedPlaneSamples(const ExecutionContext& execution,
                                                          const KernelPlaneSource& source,
                                                          const KernelValidityView& validity,
                                                          const KernelPlaneSelection& selection,
                                                          std::span<double> destination)
{
    const Status ready = plane_detail::checkPlaneSource(execution, source, validity, kCompactionOperation);
    if (ready.failed())
    {
        return ready;
    }
    if (regionsOverlap(static_cast<const std::uint8_t*>(source.samples), source.layout->totalBytes(),
                       reinterpret_cast<const std::uint8_t*>(destination.data()),
                       static_cast<std::uint64_t>(destination.size_bytes())))
    {
        return orderFailure(ErrorCode::InvalidArgument, kCompactionOperation,
                            "the compaction destination overlaps the source samples");
    }
    if (selection.width == 0u || selection.height == 0u)
    {
        return requireSelection(ValidityCount{.valid = 0u, .total = 0u}, kCompactionOperation);
    }
    const Result<plane_detail::PlaneRectangle> rectangle =
        plane_detail::describePlaneRectangle(execution, source, selection, kCompactionOperation);
    if (!rectangle.hasValue())
    {
        return rectangle.status();
    }
    const KernelPlaneSource rectangleSource{.sampleType = source.sampleType,
                                            .samples = rectangle->samples,
                                            .layout = &rectangle->layout,
                                            .plane = source.plane,
                                            .sampleInPixel = source.sampleInPixel};
    return dispatchCompaction(execution, rectangleSource, validity, rectangle->window, destination);
}

namespace
{

/// The checks both window entry points make before a sample is read: the execution, the two planes,
/// the validity view, the border, the quantile, the interpolation and the window's extent.
///
/// @param execution the operation policy.
/// @param source the input plane.
/// @param destination the output plane.
/// @param request the window contract.
/// @param validity the input rule.
/// @param operation the entry point the refusals name.
/// @return success, or the refusal the entry points document.
[[nodiscard]] Status checkWindowRequest(const ExecutionContext& execution, PlaneView<const float> source,
                                        PlaneView<float> destination, const OrderWindowRequest& request,
                                        const KernelValidityView& validity, const char* operation)
{
    Status status = reference_detail::checkExecution(execution, operation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkPair(source, destination, operation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkValidity(validity, source.width(), source.height(), operation);
    if (status.failed())
    {
        return status;
    }
    status = reference_detail::checkOverlap(source, destination, false, operation);
    if (status.failed())
    {
        return status;
    }
    if (!isKernelBorderMode(request.border) || !std::isfinite(request.constantBorder))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation,
                            "the window border must be a known mode and its constant must be finite");
    }
    if (!(request.quantile >= 0.0) || !(request.quantile <= 1.0))
    {
        return orderFailure(ErrorCode::OutOfRange, operation,
                            "the window quantile must lie in the closed unit interval");
    }
    if (!isQuantileInterpolation(request.interpolation))
    {
        return orderFailure(ErrorCode::InvalidArgument, operation,
                            "the window request names no known interpolation rule");
    }
    const std::uint64_t across = 2u * static_cast<std::uint64_t>(request.radiusX) + 1u;
    const std::uint64_t down = 2u * static_cast<std::uint64_t>(request.radiusY) + 1u;
    if (across > kMaxOrderWindowSamples || down > kMaxOrderWindowSamples || across * down > kMaxOrderWindowSamples)
    {
        return orderFailure(ErrorCode::OutOfRange, operation, "the window covers more samples than the stated ceiling");
    }
    return Status::success();
}

/// The window loop both entry points share: every accepted center's window is gathered from the cells
/// `member` admits, in row-major order, then sorted and ranked.
///
/// @tparam MemberT a callable taking a cell's row-major index within the window and answering whether
///         it is a member; the rectangle's admits every cell.
/// @param execution the operation policy.
/// @param source the checked input plane.
/// @param destination the checked output plane.
/// @param request the checked window contract.
/// @param windowSamples how many cells `member` admits, which sizes each chunk's window buffer.
/// @param validity the input rule.
/// @param operation the entry point the refusals name.
/// @param member which cells of the window are members.
/// @return the counts and the window plan, or a memory, finiteness or cancellation failure.
template <typename MemberT>
[[nodiscard]] Result<OrderWindowResult>
rankWindowMembers(const ExecutionContext& execution, PlaneView<const float> source, PlaneView<float> destination,
                  const OrderWindowRequest& request, std::uint32_t windowSamples, const KernelValidityView& validity,
                  const char* operation, MemberT member)
{
    const reference_detail::ValidityAccessor accepts(validity);
    Status status = checkWindowSamples(execution, source, accepts, operation);
    if (status.failed())
    {
        return status;
    }

    const std::uint32_t width = source.width();
    const std::uint32_t height = source.height();
    const TaskCost rowCost{.bytesPerItem = static_cast<std::uint64_t>(width) * windowSamples * sizeof(float),
                           .operationsPerItem = static_cast<std::uint64_t>(width) * windowSamples * 4u};
    const std::uint64_t chunkCount =
        chunksWithinScratch(deterministicChunkCount(height, rowCost),
                            static_cast<std::uint64_t>(windowSamples) * sizeof(float) + sizeof(std::uint64_t));
    const std::uint64_t windowBytes = chunkCount * windowSamples * sizeof(float);
    Result<plane_detail::ScopedContextMemory> windows =
        plane_detail::ScopedContextMemory::take(execution, windowBytes, alignof(float), operation);
    if (!windows.hasValue())
    {
        return windows.status();
    }
    const std::uint64_t writtenBytes = chunkCount * sizeof(std::uint64_t);
    Result<plane_detail::ScopedContextMemory> written =
        plane_detail::ScopedContextMemory::take(execution, writtenBytes, alignof(std::uint64_t), operation);
    if (!written.hasValue())
    {
        return written.status();
    }
    float* const buffers = static_cast<float*>(windows->data());
    std::uint64_t* const writtenPerChunk = static_cast<std::uint64_t*>(written->data());
    std::fill_n(writtenPerChunk, static_cast<std::size_t>(chunkCount), std::uint64_t{0});

    const auto reachX = static_cast<std::int64_t>(request.radiusX);
    const auto reachY = static_cast<std::int64_t>(request.radiusY);
    const std::int64_t across = 2 * reachX + 1;
    status = parallelFor(execution, chunkCount, rowCost, [&](TaskRange chunkIndices) -> Status {
        for (std::uint64_t chunk = chunkIndices.begin; chunk < chunkIndices.end; ++chunk)
        {
            float* const window = buffers + chunk * windowSamples;
            const TaskRange rows = plane_detail::deterministicRange(chunk, height, chunkCount);
            std::uint64_t produced = 0u;
            for (std::uint64_t row = rows.begin; row < rows.end; ++row)
            {
                const std::uint32_t y = static_cast<std::uint32_t>(row);
                const std::span<const float> input = *source.row(y);
                const std::span<float> output = *destination.row(y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    if (!accepts.accepts(input[x], x, y))
                    {
                        continue;
                    }
                    std::uint32_t held = 0u;
                    for (std::int64_t offsetY = -reachY; offsetY <= reachY; ++offsetY)
                    {
                        const std::int64_t cellRow = (offsetY + reachY) * across + reachX;
                        for (std::int64_t offsetX = -reachX; offsetX <= reachX; ++offsetX)
                        {
                            if (!member(static_cast<std::size_t>(cellRow + offsetX)))
                            {
                                continue;
                            }
                            float sample = 0.0f;
                            if (windowSampleAt(source, accepts, request, static_cast<std::int64_t>(x) + offsetX,
                                               static_cast<std::int64_t>(y) + offsetY, sample))
                            {
                                window[held] = sample;
                                ++held;
                            }
                        }
                    }
                    if (held == 0u)
                    {
                        // Only a footprint that leaves its center out can reach here: nothing in the
                        // window has a rank, so the center is left as an invalid one is.
                        continue;
                    }
                    std::sort(window, window + held);
                    output[x] =
                        static_cast<float>(windowQuantile(window, held, request.quantile, request.interpolation));
                    ++produced;
                }
            }
            writtenPerChunk[chunk] = produced;
        }
        return Status::success();
    });
    if (status.failed())
    {
        return status;
    }

    std::uint64_t producedTotal = 0u;
    for (std::uint64_t chunk = 0u; chunk < chunkCount; ++chunk)
    {
        producedTotal += writtenPerChunk[chunk];
    }

    OrderWindowResult result;
    result.run.selection = ValidityCount{.valid = producedTotal, .total = static_cast<std::uint64_t>(width) * height};
    result.run.outputsWritten = producedTotal;
    result.windowSamples = windowSamples;
    result.deterministicChunks = chunkCount;
    result.plannedWorkers =
        static_cast<std::uint32_t>((std::min)(static_cast<std::uint64_t>(execution.admittedConcurrency()), chunkCount));
    result.scratchBytes = windowBytes + writtenBytes;
    return result;
}

} // namespace

Result<OrderWindowResult> selectWindowOrderStatistic(const ExecutionContext& execution, PlaneView<const float> source,
                                                     PlaneView<float> destination, const OrderWindowRequest& request,
                                                     const KernelValidityView& validity)
{
    const Status ready = checkWindowRequest(execution, source, destination, request, validity, kWindowOperation);
    if (ready.failed())
    {
        return ready;
    }
    const auto windowSamples = static_cast<std::uint32_t>((2u * static_cast<std::uint64_t>(request.radiusX) + 1u) *
                                                          (2u * static_cast<std::uint64_t>(request.radiusY) + 1u));
    return rankWindowMembers(execution, source, destination, request, windowSamples, validity, kWindowOperation,
                             [](std::size_t) { return true; });
}

Result<OrderWindowResult> selectFootprintOrderStatistic(const ExecutionContext& execution,
                                                        PlaneView<const float> source, PlaneView<float> destination,
                                                        const OrderWindowRequest& request,
                                                        std::span<const std::uint8_t> footprint,
                                                        const KernelValidityView& validity)
{
    const Status ready = checkWindowRequest(execution, source, destination, request, validity, kFootprintOperation);
    if (ready.failed())
    {
        return ready;
    }
    const std::uint64_t cells = (2u * static_cast<std::uint64_t>(request.radiusX) + 1u) *
                                (2u * static_cast<std::uint64_t>(request.radiusY) + 1u);
    if (footprint.size() != cells)
    {
        return orderFailure(ErrorCode::IncompatibleArguments, kFootprintOperation,
                            "a structuring element holds one byte for every cell of the window it reaches across");
    }
    const auto members = static_cast<std::uint32_t>(
        std::count_if(footprint.begin(), footprint.end(), [](std::uint8_t cell) { return cell != 0u; }));
    if (members == 0u)
    {
        return orderFailure(ErrorCode::InvalidArgument, kFootprintOperation,
                            "a structuring element that marks no cell has no window to rank");
    }
    return rankWindowMembers(execution, source, destination, request, members, validity, kFootprintOperation,
                             [footprint](std::size_t cell) { return footprint[cell] != 0u; });
}

} // namespace an
