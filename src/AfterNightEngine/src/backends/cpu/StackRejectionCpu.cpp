// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the rejection rows, compiled once per instruction set and frame count.
//
// The compiled rejection rows are isolated from the shared SIMD table so their per-instruction-set
// object cost remains attributable. They run one row at a time, eight adjacent columns to a block
// and one column to a lane, so every step of the one-column estimator and of the single-pass
// combination is one operation over the block. The steps are theirs in the same order and the
// same widths - each sample read as its double plus zero, sums accumulated in double in frame or
// rank order, every other statistic in the width that estimator takes it in - with an empty slot
// adding a zero, which no running sum of finite samples can tell from skipping it, and sorting as
// +inf behind every survivor. The medians and the rankings use the same odd-even merge network,
// each exchange selects on one comparison, so every lane is ordered exactly as that estimator
// orders its column. A lane whose estimator has returned stops moving; the rest iterate on. The
// differential tests hold every lane to the one-column path bit for bit over every family, frame
// count, validity rule and tail.
//
// There is one row per frame count from two to eight, because the network and every per-frame
// array are sized by the count at compile time. Any other count, and a rung this build does not
// compile, answers null, and the operation keeps its one-column path.

#include "backends/cpu/StackRejectionCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY
// The shared vcpkg headers otherwise mark even inline debug assertions as DLL imports.
// This translation unit uses only header definitions, including those assertions.
#define HWY_DLLEXPORT

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/StackRejectionCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace stack_rejection_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

/// The columns of one block, one to a lane.
constexpr std::uint32_t kLanes = 8u;

using FloatTag = hn::FixedTag<float, kLanes>;
using IntTag = hn::RebindToSigned<FloatTag>;
using UnsignedTag = hn::RebindToUnsigned<FloatTag>;
using DoubleTag = hn::Repartition<double, FloatTag>;
using FloatVector = hn::Vec<FloatTag>;
using IntVector = hn::Vec<IntTag>;
using DoubleVector = hn::Vec<DoubleTag>;
using FloatMask = hn::Mask<FloatTag>;
using IntMask = hn::Mask<IntTag>;

/// A lane's disposition as bits: whether its sample survives, and the side of its first
/// rejection. A missing sample carries none, a kept one only survives, a removed one carries its
/// side, and a replaced one survives with its side.
constexpr std::int32_t kSurvives = 1;
constexpr std::int32_t kFirstLow = 2;
constexpr std::int32_t kFirstHigh = 4;
static_assert(kSurvives == 1 << 0 && kFirstLow == 1 << 1 && kFirstHigh == 1 << 2,
              "a disposition's bits are read and composed by these shifts");

/// The odd-even merge network the one-column estimator sorts a short history with.
struct Network
{
    std::array<std::array<std::uint32_t, 2u>, 19u> exchanges{}; ///< Each exchange's two positions, lower first.
    std::uint32_t count = 0u; ///< How many exchanges the network holds.
};

/// The network for `Count` samples, exchange for exchange as the one-column estimator builds it.
template <std::uint32_t Count> constexpr Network networkOf()
{
    Network network;
    for (std::uint32_t p = 1u; p < Count; p *= 2u)
        for (std::uint32_t k = p; k >= 1u; k /= 2u)
            for (std::uint32_t j = k % p; j + k < Count; j += 2u * k)
                for (std::uint32_t i = 0u; i < k && i + j + k < Count; ++i)
                    if ((i + j) / (2u * p) == (i + j + k) / (2u * p))
                        network.exchanges[network.count++] = {i + j, i + j + k};
    return network;
}

/// One exchange of the network over every lane: the pair in order, the smaller first, as two
/// selects on one comparison, which is the one-column estimator's exchange exactly.
inline void exchangeLanes(FloatVector& left, FloatVector& right) noexcept
{
    const FloatMask exchange = hn::Gt(left, right);
    const FloatVector first = hn::IfThenElse(exchange, right, left);
    const FloatVector second = hn::IfThenElse(exchange, left, right);
    left = first;
    right = second;
}

/// Sorts every lane's `N` values with the network for `N`, unrolled at compile time.
template <std::uint32_t N, std::size_t... Exchanges>
void sortLanes(std::array<FloatVector, N>& values, std::index_sequence<Exchanges...>) noexcept
{
    constexpr Network kNetwork = networkOf<N>();
    (exchangeLanes(values[kNetwork.exchanges[Exchanges][0]], values[kNetwork.exchanges[Exchanges][1]]), ...);
}

/// One exchange of the network over every lane, ordering the pair by value and, between equal
/// values, by frame, which is the order the one-column fit ranks its survivors in. A frame travels
/// as its own bit, which orders as its index does.
inline void exchangeRanked(FloatVector& leftValue, IntVector& leftFrame, FloatVector& rightValue,
                           IntVector& rightFrame) noexcept
{
    const FloatTag floatTag;
    const FloatMask exchange =
        hn::Or(hn::Gt(leftValue, rightValue),
               hn::And(hn::Eq(leftValue, rightValue), hn::RebindMask(floatTag, hn::Gt(leftFrame, rightFrame))));
    const IntMask exchangeFrames = hn::RebindMask(IntTag(), exchange);
    const FloatVector firstValue = hn::IfThenElse(exchange, rightValue, leftValue);
    const FloatVector secondValue = hn::IfThenElse(exchange, leftValue, rightValue);
    const IntVector firstFrame = hn::IfThenElse(exchangeFrames, rightFrame, leftFrame);
    const IntVector secondFrame = hn::IfThenElse(exchangeFrames, leftFrame, rightFrame);
    leftValue = firstValue;
    rightValue = secondValue;
    leftFrame = firstFrame;
    rightFrame = secondFrame;
}

/// Ranks every lane's `N` values, and the frames they came from, with the network for `N`.
template <std::uint32_t N, std::size_t... Exchanges>
void sortRanked(std::array<FloatVector, N>& values, std::array<IntVector, N>& frames,
                std::index_sequence<Exchanges...>) noexcept
{
    constexpr Network kNetwork = networkOf<N>();
    (exchangeRanked(values[kNetwork.exchanges[Exchanges][0]], frames[kNetwork.exchanges[Exchanges][0]],
                    values[kNetwork.exchanges[Exchanges][1]], frames[kNetwork.exchanges[Exchanges][1]]),
     ...);
}

/// Adds every lane of `values`, widened to double, to its lane of the pair of sums.
inline void accumulate(DoubleVector& low, DoubleVector& high, FloatVector values) noexcept
{
    const DoubleTag doubleTag;
    low = hn::Add(low, hn::PromoteLowerTo(doubleTag, values));
    high = hn::Add(high, hn::PromoteUpperTo(doubleTag, values));
}

/// Every lane's Float32 quotient of a double sum by a double divisor, rounded as the one-column
/// path rounds `static_cast<float>(sum / divisor)`.
[[nodiscard]] inline FloatVector quotientOf(DoubleVector lowSum, DoubleVector highSum, DoubleVector lowDivisor,
                                            DoubleVector highDivisor) noexcept
{
    const FloatTag floatTag;
    const hn::Half<FloatTag> halfTag;
    return hn::Combine(floatTag, hn::DemoteTo(halfTag, hn::Div(highSum, highDivisor)),
                       hn::DemoteTo(halfTag, hn::Div(lowSum, lowDivisor)));
}

/// The sum of a tally's lanes, each of which counts at most a bounded number of blocks.
[[nodiscard]] inline std::uint64_t totalOf(IntVector tally) noexcept
{
    return static_cast<std::uint64_t>(hn::ReduceSum(IntTag(), tally));
}

/// Stores the first `lanes` lanes of `vector`, in one whole store when the block is whole.
template <class TagT>
void storeLanes(hn::Vec<TagT> vector, TagT tag, hn::TFromD<TagT>* destination, std::size_t lanes) noexcept
{
    if (lanes == hn::MaxLanes(tag))
        hn::StoreU(vector, tag, destination);
    else
        hn::StoreN(vector, tag, destination, lanes);
}

/// The median of each lane's `count` survivors, sorted ahead of +inf, as the one-column estimator
/// takes it: the middle survivor of an odd count, and of an even one the Float32 sum of the middle
/// two halved - which that estimator halves in double and rounds back, and halving is exact in
/// either width, so the Float32 product by one half rounds to the same value.
template <std::uint32_t N>
[[nodiscard]] FloatVector medianOfSorted(const std::array<FloatVector, N>& sorted, IntVector count) noexcept
{
    const FloatTag floatTag;
    const IntTag intTag;
    const IntVector one = hn::Set(intTag, 1);
    const IntVector middle = hn::ShiftRight<1>(count);
    FloatVector upper = hn::Zero(floatTag);
    FloatVector lower = hn::Zero(floatTag);
    for (std::uint32_t position = 0u; position < N; ++position)
    {
        const IntVector here = hn::Set(intTag, static_cast<std::int32_t>(position));
        upper = hn::IfThenElse(hn::RebindMask(floatTag, hn::Eq(here, middle)), sorted[position], upper);
        lower = hn::IfThenElse(hn::RebindMask(floatTag, hn::Eq(hn::Add(here, one), middle)), sorted[position], lower);
    }
    const FloatVector even = hn::Mul(hn::Add(upper, lower), hn::Set(floatTag, 0.5f));
    return hn::IfThenElse(hn::RebindMask(floatTag, hn::Eq(hn::And(count, one), one)), upper, even);
}

/// The lower and the upper four lanes of a mask, as masks of the double lanes they cover.
[[nodiscard]] inline std::pair<hn::Mask<DoubleTag>, hn::Mask<DoubleTag>> doubleMasksOf(IntMask mask) noexcept
{
    const hn::Half<IntTag> halfTag;
    const DoubleTag doubleTag;
    return {hn::PromoteMaskTo(doubleTag, halfTag, hn::LowerHalfOfMask(halfTag, mask)),
            hn::PromoteMaskTo(doubleTag, halfTag, hn::UpperHalfOfMask(halfTag, mask))};
}

/// Each lane's entry at that lane's own position, or zero where the position holds none.
template <std::uint32_t N, class VectorT>
[[nodiscard]] VectorT atPosition(const std::array<VectorT, N>& entries, IntVector position) noexcept
{
    const hn::DFromV<VectorT> tag;
    const IntTag intTag;
    VectorT result = hn::Zero(tag);
    for (std::uint32_t index = 0u; index < N; ++index)
        result =
            hn::IfThenElse(hn::RebindMask(tag, hn::Eq(position, hn::Set(intTag, static_cast<std::int32_t>(index)))),
                           entries[index], result);
    return result;
}

/// The median of each lane's `count` samples, sorted ahead of +inf, as the general combination
/// takes it in double: the middle sample of an odd count, and of an even one the double sum of
/// the middle two halved.
template <std::uint32_t N>
void medianInDouble(const std::array<FloatVector, N>& sorted, IntVector count, DoubleVector& low,
                    DoubleVector& high) noexcept
{
    const DoubleTag doubleTag;
    const IntTag intTag;
    const IntVector one = hn::Set(intTag, 1);
    const IntVector middle = hn::ShiftRight<1>(count);
    const FloatVector upper = atPosition<N>(sorted, middle);
    const FloatVector lower = atPosition<N>(sorted, hn::Sub(middle, one));
    const DoubleVector upperLow = hn::PromoteLowerTo(doubleTag, upper);
    const DoubleVector upperHigh = hn::PromoteUpperTo(doubleTag, upper);
    const DoubleVector half = hn::Set(doubleTag, 0.5);
    const DoubleVector evenLow = hn::Mul(hn::Add(hn::PromoteLowerTo(doubleTag, lower), upperLow), half);
    const DoubleVector evenHigh = hn::Mul(hn::Add(hn::PromoteUpperTo(doubleTag, lower), upperHigh), half);
    const auto [oddLow, oddHigh] = doubleMasksOf(hn::Eq(hn::And(count, one), one));
    low = hn::IfThenElse(oddLow, upperLow, evenLow);
    high = hn::IfThenElse(oddHigh, upperHigh, evenHigh);
}

/// Judges one block of `N` frames with a sigma family's estimator: every lane's dispositions move
/// and, for the replacing family, its samples, and `lowEvents` and `highEvents` gain each lane's
/// rejections per side.
///
/// @return false when a judged lane's statistics leave the finite range or its winsorized
///         deviation does not settle, as the one-column estimator refuses its column.
template <std::uint32_t N>
[[nodiscard]] bool judgeSigmaFamily(const StackRejectionRowParameters& parameters, std::array<FloatVector, N>& values,
                                    std::array<IntVector, N>& states, IntVector& lowEvents,
                                    IntVector& highEvents) noexcept
{
    const FloatTag floatTag;
    const IntTag intTag;
    const DoubleTag doubleTag;

    const bool replace = parameters.family == StackRejectionRowFamily::MedianSigmaReplacement;
    const bool winsorized = parameters.family == StackRejectionRowFamily::WinsorizedSigmaClipping;
    const IntVector one = hn::Set(intTag, 1);
    const IntVector kept = hn::Set(intTag, kSurvives);
    const IntVector firstLow = hn::Set(intTag, replace ? kSurvives | kFirstLow : kFirstLow);
    const IntVector firstHigh = hn::Set(intTag, replace ? kSurvives | kFirstHigh : kFirstHigh);
    const FloatVector infinity = hn::Set(floatTag, std::numeric_limits<float>::infinity());
    const FloatVector lowFactor = hn::Set(floatTag, parameters.lowFactor);
    const FloatVector highFactor = hn::Set(floatTag, parameters.highFactor);
    const DoubleVector oneDouble = hn::Set(doubleTag, 1.0);
    const IntMask none = hn::FirstN(intTag, 0u);

    // A lane with fewer than two survivors has nothing to judge.
    IntVector count = hn::Zero(intTag);
    for (std::uint32_t frame = 0u; frame < N; ++frame)
        count = hn::Add(count, hn::And(states[frame], kept));
    IntMask active = hn::Gt(count, one);
    IntVector cumulative = hn::Zero(intTag);
    while (!hn::AllFalse(intTag, active))
    {
        std::array<FloatMask, N> survives;
        count = hn::Zero(intTag);
        for (std::uint32_t frame = 0u; frame < N; ++frame)
        {
            const IntVector survivor = hn::And(states[frame], kept);
            survives[frame] = hn::RebindMask(floatTag, hn::Eq(survivor, kept));
            count = hn::Add(count, survivor);
        }
        const FloatVector countFloat = hn::ConvertTo(floatTag, count);
        const DoubleVector countLow = hn::PromoteLowerTo(doubleTag, countFloat);
        const DoubleVector countHigh = hn::PromoteUpperTo(doubleTag, countFloat);
        const DoubleVector denominatorLow = hn::Sub(countLow, oneDouble);
        const DoubleVector denominatorHigh = hn::Sub(countHigh, oneDouble);

        // The deviation of the survivors: their mean, then their squared distances from it.
        DoubleVector sumLow = hn::Zero(doubleTag);
        DoubleVector sumHigh = hn::Zero(doubleTag);
        for (std::uint32_t frame = 0u; frame < N; ++frame)
            accumulate(sumLow, sumHigh, hn::IfThenElseZero(survives[frame], values[frame]));
        const FloatVector mean = quotientOf(sumLow, sumHigh, countLow, countHigh);
        DoubleVector squaresLow = hn::Zero(doubleTag);
        DoubleVector squaresHigh = hn::Zero(doubleTag);
        for (std::uint32_t frame = 0u; frame < N; ++frame)
        {
            const FloatVector difference = hn::Sub(values[frame], mean);
            accumulate(squaresLow, squaresHigh, hn::IfThenElseZero(survives[frame], hn::Mul(difference, difference)));
        }
        FloatVector sigma = hn::Sqrt(quotientOf(squaresLow, squaresHigh, denominatorLow, denominatorHigh));

        // The center: the survivors' median, the survivors sorted ahead of +inf.
        std::array<FloatVector, N> sorted;
        for (std::uint32_t frame = 0u; frame < N; ++frame)
            sorted[frame] = hn::IfThenElse(survives[frame], values[frame], infinity);
        sortLanes<N>(sorted, std::make_index_sequence<networkOf<N>().count>{});
        const FloatVector center = medianOfSorted<N>(sorted, count);

        if (winsorized)
        {
            // Each lane winsorizes its survivors until its own deviation settles, and then stops.
            IntMask iterating = active;
            std::array<FloatVector, N> clamped = values;
            for (std::uint32_t iteration = 0u; !hn::AllFalse(intTag, iterating); ++iteration)
            {
                const FloatVector low = hn::Sub(center, hn::Mul(hn::Set(floatTag, 1.5f), sigma));
                const FloatVector high = hn::Add(center, hn::Mul(hn::Set(floatTag, 1.5f), sigma));
                const FloatMask moving = hn::RebindMask(floatTag, iterating);
                DoubleVector clampedLow = hn::Zero(doubleTag);
                DoubleVector clampedHigh = hn::Zero(doubleTag);
                for (std::uint32_t frame = 0u; frame < N; ++frame)
                {
                    const FloatVector bounded =
                        hn::IfThenElse(hn::Lt(clamped[frame], low), low,
                                       hn::IfThenElse(hn::Lt(high, clamped[frame]), high, clamped[frame]));
                    clamped[frame] = hn::IfThenElse(moving, bounded, clamped[frame]);
                    accumulate(clampedLow, clampedHigh, hn::IfThenElseZero(survives[frame], clamped[frame]));
                }
                const FloatVector clampedMean = quotientOf(clampedLow, clampedHigh, countLow, countHigh);
                DoubleVector spreadLow = hn::Zero(doubleTag);
                DoubleVector spreadHigh = hn::Zero(doubleTag);
                for (std::uint32_t frame = 0u; frame < N; ++frame)
                {
                    const FloatVector difference = hn::Sub(clamped[frame], clampedMean);
                    accumulate(spreadLow, spreadHigh,
                               hn::IfThenElseZero(survives[frame], hn::Mul(difference, difference)));
                }
                const FloatVector next =
                    hn::Mul(hn::Set(floatTag, 1.134f),
                            hn::Sqrt(quotientOf(spreadLow, spreadHigh, denominatorLow, denominatorHigh)));
                // The convergence test compares in double, as the one-column estimator's does, so
                // it is taken lane by lane.
                std::array<float, kLanes> nextLanes;
                std::array<float, kLanes> sigmaLanes;
                std::array<float, kLanes> lowLanes;
                std::array<float, kLanes> highLanes;
                std::array<std::int32_t, kLanes> movingLanes;
                hn::StoreU(next, floatTag, nextLanes.data());
                hn::StoreU(sigma, floatTag, sigmaLanes.data());
                hn::StoreU(low, floatTag, lowLanes.data());
                hn::StoreU(high, floatTag, highLanes.data());
                hn::StoreU(hn::VecFromMask(intTag, iterating), intTag, movingLanes.data());
                for (std::uint32_t lane = 0u; lane < kLanes; ++lane)
                {
                    if (movingLanes[lane] == 0)
                        continue;
                    const float previous = sigmaLanes[lane];
                    sigmaLanes[lane] = nextLanes[lane];
                    if (!std::isfinite(sigmaLanes[lane]) || !std::isfinite(lowLanes[lane]) ||
                        !std::isfinite(highLanes[lane]))
                        return false;
                    if (!(std::fabs(sigmaLanes[lane] - previous) > previous * 0.0005))
                        movingLanes[lane] = 0;
                    // Refuse failure to converge instead of hanging on an adversarial column.
                    else if (iteration == 8191u)
                        return false;
                }
                sigma = hn::LoadU(floatTag, sigmaLanes.data());
                iterating = hn::MaskFromVec(hn::LoadU(intTag, movingLanes.data()));
            }
        }
        const IntMask finite = hn::RebindMask(intTag, hn::And(hn::IsFinite(center), hn::IsFinite(sigma)));
        if (!hn::AllFalse(intTag, hn::AndNot(finite, active)))
            return false;

        // The decisions, frame by frame as the one-column loop takes them: a lane that is not
        // replacing stops at the first survivor that would leave four or fewer.
        const FloatVector lowLimit = hn::Mul(sigma, lowFactor);
        const FloatVector highLimit = hn::Mul(sigma, highFactor);
        const IntVector four = hn::Set(intTag, 4);
        IntMask changed = none;
        IntMask stopped = none;
        for (std::uint32_t frame = 0u; frame < N; ++frame)
        {
            const IntMask eligible = hn::AndNot(stopped, hn::And(active, hn::RebindMask(intTag, survives[frame])));
            IntMask judged = eligible;
            if (!replace)
            {
                const IntMask halt = hn::Or(hn::Le(count, cumulative), hn::Le(hn::Sub(count, cumulative), four));
                stopped = hn::Or(stopped, hn::And(eligible, halt));
                judged = hn::AndNot(halt, eligible);
            }
            const FloatVector value = values[frame];
            const FloatMask isLow = hn::Gt(hn::Sub(center, value), lowLimit);
            const FloatMask isHigh = hn::AndNot(isLow, hn::Gt(hn::Sub(value, center), highLimit));
            const IntMask rejectLow = hn::And(judged, hn::RebindMask(intTag, isLow));
            const IntMask rejectHigh = hn::And(judged, hn::RebindMask(intTag, isHigh));
            const IntMask reject = hn::Or(rejectLow, rejectHigh);
            lowEvents = hn::Add(lowEvents, hn::IfThenElseZero(rejectLow, one));
            highEvents = hn::Add(highEvents, hn::IfThenElseZero(rejectHigh, one));
            const IntMask relabel = replace ? hn::And(reject, hn::Eq(states[frame], kept)) : reject;
            states[frame] = hn::IfThenElse(relabel, hn::IfThenElse(rejectLow, firstLow, firstHigh), states[frame]);
            if (replace)
                values[frame] = hn::IfThenElse(hn::RebindMask(floatTag, reject), center, value);
            else
                cumulative = hn::Add(cumulative, hn::IfThenElseZero(reject, one));
            changed = hn::Or(changed, reject);
        }
        active = hn::And(active, changed);
    }
    return true;
}

/// Judges one block of `N` frames with the linear fit: each lane's survivors ranked by value, a
/// line fitted over their ranks, and the first of them farther from it than the thresholds
/// removed while more than four would remain, until nothing moves. The fit never changes a
/// sample, so the survivors of a later iteration keep the ranking of the first, and every lane is
/// judged in that ranking; only at the end is each disposition returned to its frame.
///
/// @return false when a fitted lane's line or deviation leaves the finite range, as the
///         one-column estimator refuses its column.
template <std::uint32_t N>
[[nodiscard]] bool judgeLinearFit(const StackRejectionRowParameters& parameters,
                                  const std::array<FloatVector, N>& values, std::array<IntVector, N>& states,
                                  IntVector& lowEvents, IntVector& highEvents) noexcept
{
    static_assert(kStackRejectionRowFrames == kLanes, "one lane of the reciprocal table per count");
    const FloatTag floatTag;
    const IntTag intTag;
    const IntVector zero = hn::Zero(intTag);
    const IntVector one = hn::Set(intTag, 1);
    const IntVector four = hn::Set(intTag, 4);
    const IntVector lastLane = hn::Set(intTag, static_cast<std::int32_t>(kLanes - 1u));
    const FloatVector infinity = hn::Set(floatTag, std::numeric_limits<float>::infinity());
    const FloatVector lowFactor = hn::Set(floatTag, parameters.lowFactor);
    const FloatVector highFactor = hn::Set(floatTag, parameters.highFactor);
    const FloatVector meanRank = hn::Set(floatTag, parameters.meanRank);
    const FloatVector inverseRankVariance = hn::Set(floatTag, parameters.inverseRankVariance);
    const FloatVector reciprocals = hn::LoadU(floatTag, parameters.reciprocals.data());

    // The survivors ranked by value and, between equal values, by frame; what is not a survivor
    // ranks behind them as +inf.
    std::array<FloatVector, N> ranked;
    std::array<IntVector, N> frameBits;
    IntVector count = zero;
    for (std::uint32_t frame = 0u; frame < N; ++frame)
    {
        const IntVector survivor = hn::And(states[frame], one);
        ranked[frame] = hn::IfThenElse(hn::RebindMask(floatTag, hn::Eq(survivor, one)), values[frame], infinity);
        frameBits[frame] = hn::Set(intTag, std::int32_t{1} << frame);
        count = hn::Add(count, survivor);
    }
    sortRanked<N>(ranked, frameBits, std::make_index_sequence<networkOf<N>().count>{});
    std::array<IntMask, N> alive;
    for (std::uint32_t position = 0u; position < N; ++position)
        alive[position] = hn::Gt(count, hn::Set(intTag, static_cast<std::int32_t>(position)));

    // A lane with fewer than two survivors has nothing to fit.
    IntMask active = hn::Gt(count, one);
    IntVector cumulative = zero;
    IntVector lowBits = zero;
    IntVector highBits = zero;
    while (!hn::AllFalse(intTag, active))
    {
        // Each survivor's rank among the survivors, and the reciprocal its running means weigh by.
        std::array<FloatVector, N> rank;
        std::array<FloatVector, N> reciprocal;
        count = zero;
        for (std::uint32_t position = 0u; position < N; ++position)
        {
            rank[position] = hn::ConvertTo(floatTag, count);
            reciprocal[position] =
                hn::TableLookupLanes(reciprocals, hn::IndicesFromVec(floatTag, hn::Min(count, lastLane)));
            count = hn::Add(count, hn::IfThenElseZero(alive[position], one));
        }
        active = hn::And(active, hn::Gt(count, one));

        // The line: the running mean of the survivors in rank order - which from zero takes the
        // first survivor exactly - their running covariance with the rank, and the fit.
        FloatVector mean = hn::Zero(floatTag);
        for (std::uint32_t position = 0u; position < N; ++position)
            mean = hn::IfThenElse(hn::RebindMask(floatTag, alive[position]),
                                  hn::Add(mean, hn::Mul(hn::Sub(ranked[position], mean), reciprocal[position])), mean);
        FloatVector covariance = hn::Zero(floatTag);
        for (std::uint32_t position = 0u; position < N; ++position)
        {
            const FloatVector dx = hn::Sub(rank[position], meanRank);
            const FloatVector dy = hn::Sub(ranked[position], mean);
            covariance = hn::IfThenElse(
                hn::RebindMask(floatTag, alive[position]),
                hn::Add(covariance, hn::Mul(hn::Sub(hn::Mul(dx, dy), covariance), reciprocal[position])), covariance);
        }
        const FloatVector slope = hn::Mul(covariance, inverseRankVariance);
        const FloatVector intercept = hn::Sub(mean, hn::Mul(meanRank, slope));
        FloatVector sigma = hn::Zero(floatTag);
        for (std::uint32_t position = 0u; position < N; ++position)
        {
            const FloatVector fitted = hn::Add(hn::Mul(slope, rank[position]), intercept);
            sigma = hn::IfThenElse(hn::RebindMask(floatTag, alive[position]),
                                   hn::Add(sigma, hn::Abs(hn::Sub(ranked[position], fitted))), sigma);
        }
        sigma = hn::Div(sigma, hn::ConvertTo(floatTag, count));
        const IntMask finite =
            hn::RebindMask(intTag, hn::And(hn::And(hn::IsFinite(sigma), hn::IsFinite(slope)), hn::IsFinite(intercept)));
        if (!hn::AllFalse(intTag, hn::AndNot(finite, active)))
            return false;

        // The decisions in rank order: a lane stops at the first survivor that would leave four or
        // fewer, counting every removal it has made.
        const FloatVector lowLimit = hn::Mul(sigma, lowFactor);
        const FloatVector highLimit = hn::Mul(sigma, highFactor);
        IntVector removed = zero;
        for (std::uint32_t position = 0u; position < N; ++position)
        {
            const FloatVector product = hn::Mul(slope, rank[position]);
            const FloatVector lowDistance = hn::Sub(hn::Add(product, intercept), ranked[position]);
            const FloatVector highDistance = hn::Sub(hn::Sub(ranked[position], product), intercept);
            const FloatMask isLow = hn::Gt(lowDistance, lowLimit);
            const FloatMask isHigh = hn::AndNot(isLow, hn::Gt(highDistance, highLimit));
            const IntMask open = hn::Gt(hn::Sub(count, hn::Add(cumulative, removed)), four);
            const IntMask judged = hn::And(hn::And(active, alive[position]), open);
            const IntMask rejectLow = hn::And(judged, hn::RebindMask(intTag, isLow));
            const IntMask rejectHigh = hn::And(judged, hn::RebindMask(intTag, isHigh));
            const IntMask reject = hn::Or(rejectLow, rejectHigh);
            lowEvents = hn::Add(lowEvents, hn::IfThenElseZero(rejectLow, one));
            highEvents = hn::Add(highEvents, hn::IfThenElseZero(rejectHigh, one));
            lowBits = hn::Or(lowBits, hn::IfThenElseZero(rejectLow, frameBits[position]));
            highBits = hn::Or(highBits, hn::IfThenElseZero(rejectHigh, frameBits[position]));
            removed = hn::Add(removed, hn::IfThenElseZero(reject, one));
            alive[position] = hn::AndNot(reject, alive[position]);
        }
        cumulative = hn::Add(cumulative, removed);
        active = hn::And(active, hn::Gt(removed, zero));
    }

    // Every frame's disposition, read from its bit in the ranking's survivors and removals.
    IntVector survivorBits = zero;
    for (std::uint32_t position = 0u; position < N; ++position)
        survivorBits = hn::Or(survivorBits, hn::IfThenElseZero(alive[position], frameBits[position]));
    for (std::uint32_t frame = 0u; frame < N; ++frame)
    {
        const auto shift = static_cast<int>(frame);
        const IntVector survived = hn::And(hn::ShiftRightSame(survivorBits, shift), one);
        const IntVector low = hn::And(hn::ShiftRightSame(lowBits, shift), one);
        const IntVector high = hn::And(hn::ShiftRightSame(highBits, shift), one);
        states[frame] = hn::Or(survived, hn::Or(hn::ShiftLeft<1>(low), hn::ShiftLeft<2>(high)));
    }
    return true;
}

/// Judges one block of `N` frames with percentile clipping: each lane's median of its survivors,
/// and every survivor farther below or above it than the stated fraction of it removed, in one
/// pass compared in double as the one-column estimator compares.
///
/// @return false when a judged lane's median leaves the finite range, as the one-column estimator
///         refuses its column.
template <std::uint32_t N>
[[nodiscard]] bool judgePercentile(const StackRejectionRowParameters& parameters,
                                   const std::array<FloatVector, N>& values, std::array<IntVector, N>& states,
                                   IntVector& lowEvents, IntVector& highEvents) noexcept
{
    const FloatTag floatTag;
    const IntTag intTag;
    const DoubleTag doubleTag;
    const IntVector one = hn::Set(intTag, 1);
    const IntVector firstLow = hn::Set(intTag, kFirstLow);
    const IntVector firstHigh = hn::Set(intTag, kFirstHigh);
    const FloatVector infinity = hn::Set(floatTag, std::numeric_limits<float>::infinity());

    std::array<FloatVector, N> sorted;
    IntVector count = hn::Zero(intTag);
    for (std::uint32_t frame = 0u; frame < N; ++frame)
    {
        const IntVector survivor = hn::And(states[frame], one);
        sorted[frame] = hn::IfThenElse(hn::RebindMask(floatTag, hn::Eq(survivor, one)), values[frame], infinity);
        count = hn::Add(count, survivor);
    }
    // A lane with fewer than two survivors has nothing to judge.
    IntMask active = hn::Gt(count, one);
    if (hn::AllFalse(intTag, active))
        return true;
    sortLanes<N>(sorted, std::make_index_sequence<networkOf<N>().count>{});
    const FloatVector center = medianOfSorted<N>(sorted, count);
    if (!hn::AllFalse(intTag, hn::AndNot(hn::RebindMask(intTag, hn::IsFinite(center)), active)))
        return false;
    // Every fraction of a zero median is zero, and the estimator judges nothing about one.
    active = hn::AndNot(hn::RebindMask(intTag, hn::Eq(center, hn::Zero(floatTag))), active);
    const DoubleVector centerLow = hn::PromoteLowerTo(doubleTag, center);
    const DoubleVector centerHigh = hn::PromoteUpperTo(doubleTag, center);
    const DoubleVector lowFraction = hn::Set(doubleTag, parameters.lowFraction);
    const DoubleVector highFraction = hn::Set(doubleTag, parameters.highFraction);
    const DoubleVector lowLimitLow = hn::Mul(centerLow, lowFraction);
    const DoubleVector lowLimitHigh = hn::Mul(centerHigh, lowFraction);
    const DoubleVector highLimitLow = hn::Mul(centerLow, highFraction);
    const DoubleVector highLimitHigh = hn::Mul(centerHigh, highFraction);
    for (std::uint32_t frame = 0u; frame < N; ++frame)
    {
        const DoubleVector valueLow = hn::PromoteLowerTo(doubleTag, values[frame]);
        const DoubleVector valueHigh = hn::PromoteUpperTo(doubleTag, values[frame]);
        const IntMask isLow =
            hn::OrderedDemote2MasksTo(intTag, doubleTag, hn::Gt(hn::Sub(centerLow, valueLow), lowLimitLow),
                                      hn::Gt(hn::Sub(centerHigh, valueHigh), lowLimitHigh));
        const IntMask isHigh = hn::AndNot(
            isLow, hn::OrderedDemote2MasksTo(intTag, doubleTag, hn::Gt(hn::Sub(valueLow, centerLow), highLimitLow),
                                             hn::Gt(hn::Sub(valueHigh, centerHigh), highLimitHigh)));
        const IntMask judged = hn::And(active, hn::Eq(hn::And(states[frame], one), one));
        const IntMask rejectLow = hn::And(judged, isLow);
        const IntMask rejectHigh = hn::And(judged, isHigh);
        lowEvents = hn::Add(lowEvents, hn::IfThenElseZero(rejectLow, one));
        highEvents = hn::Add(highEvents, hn::IfThenElseZero(rejectHigh, one));
        states[frame] = hn::IfThenElse(rejectLow, firstLow, hn::IfThenElse(rejectHigh, firstHigh, states[frame]));
    }
    return true;
}

/// Judges one block of `N` frames with the generalized extreme Studentized deviate test: each
/// lane's survivors ranked by value and, between equal values, by frame; the farther extreme of
/// what remains taken as a candidate while the candidates stay within the column's most outliers
/// and more than two samples remain; and every candidate up to the last whose statistic exceeds
/// its critical value removed, on the side of the median it lies. The statistics are rescanned
/// in rank order for every candidate, which is how the reference backend takes them, and the
/// moment shortcut of the one-column path is admitted only where it is proven to decide the same.
///
/// @return false when a tested lane's statistics leave the finite range, as the one-column
///         estimator refuses its column.
template <std::uint32_t N>
[[nodiscard]] bool judgeGesd(const StackRejectionRowParameters& parameters, const std::array<FloatVector, N>& values,
                             std::array<IntVector, N>& states, IntVector& lowEvents, IntVector& highEvents) noexcept
{
    static_assert(kStackRejectionRowFrames == kLanes, "one lane of each table per count");
    const FloatTag floatTag;
    const IntTag intTag;
    const DoubleTag doubleTag;
    const IntVector zero = hn::Zero(intTag);
    const IntVector one = hn::Set(intTag, 1);
    const IntVector two = hn::Set(intTag, 2);
    const IntVector lastLane = hn::Set(intTag, static_cast<std::int32_t>(kLanes - 1u));
    const FloatVector infinity = hn::Set(floatTag, std::numeric_limits<float>::infinity());
    const FloatVector criticalTable = hn::LoadU(floatTag, parameters.critical.data());
    const IntVector maximumTable = hn::LoadU(intTag, parameters.maximumOutliers.data());

    // The survivors ranked by value and, between equal values, by frame; what is not a survivor
    // ranks behind them as +inf.
    std::array<FloatVector, N> ranked;
    std::array<IntVector, N> frameBits;
    IntVector count = zero;
    for (std::uint32_t frame = 0u; frame < N; ++frame)
    {
        const IntVector survivor = hn::And(states[frame], one);
        ranked[frame] = hn::IfThenElse(hn::RebindMask(floatTag, hn::Eq(survivor, one)), values[frame], infinity);
        frameBits[frame] = hn::Set(intTag, std::int32_t{1} << frame);
        count = hn::Add(count, survivor);
    }
    // A lane with fewer than three survivors has nothing to test.
    IntMask active = hn::Gt(count, two);
    if (hn::AllFalse(intTag, active))
        return true;
    sortRanked<N>(ranked, frameBits, std::make_index_sequence<networkOf<N>().count>{});
    const FloatVector median = medianOfSorted<N>(ranked, count);
    if (!hn::AllFalse(intTag, hn::AndNot(hn::RebindMask(intTag, hn::IsFinite(median)), active)))
        return false;
    const IntVector maximum = hn::TableLookupLanes(
        maximumTable, hn::IndicesFromVec(intTag, hn::Min(hn::Max(hn::Sub(count, one), zero), lastLane)));

    // What remains is the ranks from `low` up to `high`. Candidates are pending until a later
    // significant one confirms them with itself, so the confirmed are always a prefix.
    IntVector low = zero;
    IntVector high = count;
    IntVector pendingLow = zero;
    IntVector pendingHigh = zero;
    IntVector pendingLowCount = zero;
    IntVector pendingHighCount = zero;
    IntVector confirmedLow = zero;
    IntVector confirmedHigh = zero;
    IntVector confirmedLowCount = zero;
    IntVector confirmedHighCount = zero;
    for (std::int32_t iteration = 0;; ++iteration)
    {
        const IntVector remaining = hn::Sub(high, low);
        active = hn::And(active, hn::And(hn::Gt(maximum, hn::Set(intTag, iteration)), hn::Gt(remaining, two)));
        if (hn::AllFalse(intTag, active))
            break;
        std::array<FloatMask, N> inside;
        DoubleVector sumLow = hn::Zero(doubleTag);
        DoubleVector sumHigh = hn::Zero(doubleTag);
        for (std::uint32_t position = 0u; position < N; ++position)
        {
            const IntVector here = hn::Set(intTag, static_cast<std::int32_t>(position));
            inside[position] = hn::RebindMask(floatTag, hn::And(hn::Ge(here, low), hn::Lt(here, high)));
            accumulate(sumLow, sumHigh, hn::IfThenElseZero(inside[position], ranked[position]));
        }
        const FloatVector remainingFloat = hn::ConvertTo(floatTag, remaining);
        const FloatVector degrees = hn::ConvertTo(floatTag, hn::Sub(remaining, one));
        const FloatVector mean = quotientOf(sumLow, sumHigh, hn::PromoteLowerTo(doubleTag, remainingFloat),
                                            hn::PromoteUpperTo(doubleTag, remainingFloat));
        DoubleVector squaresLow = hn::Zero(doubleTag);
        DoubleVector squaresHigh = hn::Zero(doubleTag);
        for (std::uint32_t position = 0u; position < N; ++position)
        {
            // The difference is taken in Float32 and squared in double, as the estimator's is.
            const FloatVector difference = hn::IfThenElseZero(inside[position], hn::Sub(ranked[position], mean));
            const DoubleVector differenceLow = hn::PromoteLowerTo(doubleTag, difference);
            const DoubleVector differenceHigh = hn::PromoteUpperTo(doubleTag, difference);
            squaresLow = hn::Add(squaresLow, hn::Mul(differenceLow, differenceLow));
            squaresHigh = hn::Add(squaresHigh, hn::Mul(differenceHigh, differenceHigh));
        }
        const FloatVector sigma = hn::Sqrt(quotientOf(squaresLow, squaresHigh, hn::PromoteLowerTo(doubleTag, degrees),
                                                      hn::PromoteUpperTo(doubleTag, degrees)));
        const IntMask finite = hn::RebindMask(intTag, hn::And(hn::IsFinite(mean), hn::IsFinite(sigma)));
        if (!hn::AllFalse(intTag, hn::AndNot(finite, active)))
            return false;
        // A lane whose remaining samples do not spread stops testing.
        active = hn::AndNot(hn::RebindMask(intTag, hn::Le(sigma, hn::Zero(floatTag))), active);
        if (hn::AllFalse(intTag, active))
            break;

        // The candidate is the farther extreme, the upper one only when strictly farther.
        const IntVector top = hn::Sub(high, one);
        const FloatVector lowValue = atPosition<N>(ranked, low);
        const FloatVector highValue = atPosition<N>(ranked, top);
        const FloatVector lowDistance = hn::Sub(mean, lowValue);
        const FloatVector highDistance = hn::Sub(highValue, mean);
        const FloatMask removeHigh = hn::Gt(highDistance, lowDistance);
        const IntMask removeHighLanes = hn::RebindMask(intTag, removeHigh);
        const FloatVector statistic = hn::Div(hn::IfThenElse(removeHigh, highDistance, lowDistance), sigma);
        const FloatVector critical = hn::TableLookupLanes(
            criticalTable, hn::IndicesFromVec(floatTag, hn::And(hn::Sub(remaining, one), lastLane)));
        const IntMask significant = hn::And(active, hn::RebindMask(intTag, hn::Gt(statistic, critical)));
        const IntVector candidate = atPosition<N>(frameBits, hn::IfThenElse(removeHighLanes, top, low));
        const IntMask above =
            hn::And(active, hn::RebindMask(intTag, hn::Ge(hn::IfThenElse(removeHigh, highValue, lowValue), median)));
        const IntMask below = hn::AndNot(above, active);
        pendingHigh = hn::Or(pendingHigh, hn::IfThenElseZero(above, candidate));
        pendingLow = hn::Or(pendingLow, hn::IfThenElseZero(below, candidate));
        pendingHighCount = hn::Add(pendingHighCount, hn::IfThenElseZero(above, one));
        pendingLowCount = hn::Add(pendingLowCount, hn::IfThenElseZero(below, one));
        confirmedHigh = hn::IfThenElse(significant, pendingHigh, confirmedHigh);
        confirmedLow = hn::IfThenElse(significant, pendingLow, confirmedLow);
        confirmedHighCount = hn::IfThenElse(significant, pendingHighCount, confirmedHighCount);
        confirmedLowCount = hn::IfThenElse(significant, pendingLowCount, confirmedLowCount);
        high = hn::IfThenElse(hn::And(active, removeHighLanes), top, high);
        low = hn::IfThenElse(hn::AndNot(removeHighLanes, active), hn::Add(low, one), low);
    }
    lowEvents = hn::Add(lowEvents, confirmedLowCount);
    highEvents = hn::Add(highEvents, confirmedHighCount);
    const IntVector firstLow = hn::Set(intTag, kFirstLow);
    const IntVector firstHigh = hn::Set(intTag, kFirstHigh);
    for (std::uint32_t frame = 0u; frame < N; ++frame)
    {
        const IntVector bit = hn::Set(intTag, std::int32_t{1} << frame);
        const IntMask removedLow = hn::Eq(hn::And(confirmedLow, bit), bit);
        const IntMask removedHigh = hn::Eq(hn::And(confirmedHigh, bit), bit);
        states[frame] = hn::IfThenElse(removedLow, firstLow, hn::IfThenElse(removedHigh, firstHigh, states[frame]));
    }
    return true;
}

/// Judges one block of `N` frames with the row's estimator.
template <std::uint32_t N>
[[nodiscard]] bool judgeBlock(const StackRejectionRowParameters& parameters, std::array<FloatVector, N>& values,
                              std::array<IntVector, N>& states, IntVector& lowEvents, IntVector& highEvents) noexcept
{
    switch (parameters.family)
    {
    case StackRejectionRowFamily::SigmaClipping:
    case StackRejectionRowFamily::MedianSigmaReplacement:
    case StackRejectionRowFamily::WinsorizedSigmaClipping:
        return judgeSigmaFamily<N>(parameters, values, states, lowEvents, highEvents);
    case StackRejectionRowFamily::LinearFitClipping:
        return judgeLinearFit<N>(parameters, values, states, lowEvents, highEvents);
    case StackRejectionRowFamily::PercentileClipping:
        return judgePercentile<N>(parameters, values, states, lowEvents, highEvents);
    case StackRejectionRowFamily::GeneralizedEsdTest:
        return judgeGesd<N>(parameters, values, states, lowEvents, highEvents);
    }
    return false;
}

/// Judges and combines one row with `N` frames, a block of `kLanes` columns at a time.
template <std::uint32_t N>
[[nodiscard]] StackRejectionRowOutcome judgeRow(const StackRejectionRowParameters& parameters,
                                                const StackRejectionRowSource& source,
                                                const StackRejectionRowTargets& targets) noexcept
{
    const FloatTag floatTag;
    const IntTag intTag;
    const UnsignedTag unsignedTag;
    const DoubleTag doubleTag;
    const hn::Half<FloatTag> halfTag;
    const IntVector zero = hn::Zero(intTag);
    const IntVector one = hn::Set(intTag, 1);
    const FloatVector zeroFloat = hn::Zero(floatTag);
    const bool finiteOnly = source.rule == ValidityRule::FiniteOnly;
    const bool zeroIsNoData = source.rule == ValidityRule::ZeroIsNoData;
    // Tallies are kept per lane for at most this many blocks before they are added into the
    // outcome, so no lane's count can overflow however wide the row.
    constexpr std::uint64_t kBlocksPerTally = std::uint64_t{1u} << 20u;

    StackRejectionRowOutcome outcome;
    std::uint64_t first = 0u;
    while (first < source.width)
    {
        std::array<IntVector, N> excluded;
        std::array<IntVector, N> rejected;
        excluded.fill(zero);
        rejected.fill(zero);
        IntVector combined = zero;
        IntVector withSamples = zero;
        IntVector lowEvents = zero;
        IntVector highEvents = zero;
        IntVector mappedLow = zero;
        IntVector mappedHigh = zero;
        for (std::uint64_t block = 0u; block < kBlocksPerTally && first < source.width; ++block, first += kLanes)
        {
            const std::size_t lanes = static_cast<std::size_t>((std::min)(std::uint64_t{kLanes}, source.width - first));
            const bool whole = lanes == kLanes;
            const IntMask present = hn::FirstN(intTag, lanes);

            // Every frame's samples, admitted by the rule as one column admits them.
            std::array<FloatVector, N> values;
            std::array<IntVector, N> states;
            IntVector admittedCount = zero;
            FloatMask nonFinite = hn::RebindMask(floatTag, hn::FirstN(intTag, 0u));
            for (std::uint32_t frame = 0u; frame < N; ++frame)
            {
                const float* const row = source.rows[frame] + first;
                // Read as the one-column path reads a sample, its double plus zero, so a negative
                // zero is admitted as a positive one.
                const FloatVector sample =
                    hn::Add(whole ? hn::LoadU(floatTag, row) : hn::LoadN(floatTag, row, lanes), zeroFloat);
                const FloatMask finite = hn::IsFinite(sample);
                FloatMask valid = hn::RebindMask(floatTag, present);
                // The zero rule is the one-column path's unordered `sample != 0.0`, true for a NaN,
                // which the ordered `hn::Ne` is not: the negated equality admits a NaN, and the row
                // then refuses it as an admitted sample that is not finite.
                if (finiteOnly)
                    valid = hn::And(valid, finite);
                else if (zeroIsNoData)
                    valid = hn::AndNot(hn::Eq(sample, zeroFloat), valid);
                nonFinite = hn::Or(nonFinite, hn::AndNot(finite, valid));
                const IntMask admitted = hn::RebindMask(intTag, valid);
                excluded[frame] = hn::Add(excluded[frame], hn::IfThenElseZero(hn::AndNot(admitted, present), one));
                states[frame] = hn::IfThenElseZero(admitted, hn::Set(intTag, kSurvives));
                values[frame] = hn::IfThenElseZero(valid, sample);
                admittedCount = hn::Add(admittedCount, hn::IfThenElseZero(admitted, one));
            }
            if (!hn::AllFalse(floatTag, nonFinite))
            {
                outcome.failure = StackRejectionRowFailure::NonFiniteSample;
                return outcome;
            }
            if (!judgeBlock<N>(parameters, values, states, lowEvents, highEvents))
            {
                outcome.failure = StackRejectionRowFailure::NonFiniteStatistics;
                return outcome;
            }

            // The single pass over every lane: survivors counted and summed in frame order, first
            // rejections counted per side.
            IntVector count = zero;
            IntVector low = zero;
            IntVector high = zero;
            DoubleVector sumLow = hn::Zero(doubleTag);
            DoubleVector sumHigh = hn::Zero(doubleTag);
            for (std::uint32_t frame = 0u; frame < N; ++frame)
            {
                const IntVector state = states[frame];
                const IntVector survivor = hn::And(state, one);
                const IntVector lowSide = hn::And(hn::ShiftRight<1>(state), one);
                const IntVector highSide = hn::ShiftRight<2>(state);
                count = hn::Add(count, survivor);
                accumulate(sumLow, sumHigh,
                           hn::IfThenElseZero(hn::RebindMask(floatTag, hn::Eq(survivor, one)), values[frame]));
                low = hn::Add(low, lowSide);
                high = hn::Add(high, highSide);
                rejected[frame] = hn::Add(rejected[frame], hn::Or(lowSide, highSide));
            }
            // A column left with no survivor either admitted no sample - its sum is zero, and
            // dividing it by one writes the zero the general pass writes for it, with the zero
            // coverage, gain and weight sum that pass gives it - or had every admitted sample
            // removed, which only the percentile's single pass can do: the iterating removers never
            // judge a column down past four survivors, the deviate test keeps two, and the
            // replacing family removes nothing. The general pass keeps every admitted sample of
            // such a column instead and writes their median, which is repeated here.
            const FloatVector divisor = hn::ConvertTo(floatTag, hn::Max(count, one));
            DoubleVector valueLow = hn::Div(sumLow, hn::PromoteLowerTo(doubleTag, divisor));
            DoubleVector valueHigh = hn::Div(sumHigh, hn::PromoteUpperTo(doubleTag, divisor));
            const IntMask emptied = hn::And(hn::Eq(count, zero), hn::Gt(admittedCount, zero));
            if (!hn::AllFalse(intTag, emptied))
            {
                std::array<FloatVector, N> sorted;
                for (std::uint32_t frame = 0u; frame < N; ++frame)
                    sorted[frame] = hn::IfThenElse(hn::RebindMask(floatTag, hn::Ne(states[frame], zero)), values[frame],
                                                   hn::Set(floatTag, std::numeric_limits<float>::infinity()));
                sortLanes<N>(sorted, std::make_index_sequence<networkOf<N>().count>{});
                DoubleVector medianLow = hn::Zero(doubleTag);
                DoubleVector medianHigh = hn::Zero(doubleTag);
                medianInDouble<N>(sorted, admittedCount, medianLow, medianHigh);
                const auto [emptiedLow, emptiedHigh] = doubleMasksOf(emptied);
                valueLow = hn::IfThenElse(emptiedLow, medianLow, valueLow);
                valueHigh = hn::IfThenElse(emptiedHigh, medianHigh, valueHigh);
                count = hn::IfThenElse(emptied, admittedCount, count);
            }
            const FloatVector countFloat = hn::ConvertTo(floatTag, count);
            const FloatVector narrowed =
                hn::Combine(floatTag, hn::DemoteTo(halfTag, valueHigh), hn::DemoteTo(halfTag, valueLow));
            const bool finite = targets.outputFloat64
                                    ? hn::AllTrue(doubleTag, hn::And(hn::IsFinite(valueLow), hn::IsFinite(valueHigh)))
                                    : hn::AllTrue(floatTag, hn::IsFinite(narrowed));
            if (!finite)
            {
                outcome.failure = StackRejectionRowFailure::NonFiniteCombination;
                return outcome;
            }

            const IntMask covered = hn::Gt(count, zero);
            if (targets.outputFloat64)
            {
                constexpr std::size_t kHalf = kLanes / 2u;
                auto* const output = reinterpret_cast<double*>(targets.output) + first;
                storeLanes(valueLow, doubleTag, output, (std::min)(lanes, kHalf));
                if (lanes > kHalf)
                    storeLanes(valueHigh, doubleTag, output + kHalf, lanes - kHalf);
            }
            else
            {
                storeLanes(narrowed, floatTag, reinterpret_cast<float*>(targets.output) + first, lanes);
            }
            if (targets.maskRow != nullptr)
            {
                // A block starts on a byte of the mask row and covers it.
                std::uint8_t bits = 0u;
                hn::StoreMaskBits(intTag, covered, &bits);
                targets.maskRow[first / kLanes] |= bits;
            }
            if (targets.coverage != nullptr)
                storeLanes(hn::BitCast(unsignedTag, count), unsignedTag, targets.coverage + first, lanes);
            if (targets.gain != nullptr)
                storeLanes(countFloat, floatTag, targets.gain + first, lanes);
            if (targets.weightSum != nullptr)
                storeLanes(countFloat, floatTag, targets.weightSum + first, lanes);
            if (targets.low != nullptr)
                storeLanes(hn::BitCast(unsignedTag, low), unsignedTag, targets.low + first, lanes);
            if (targets.high != nullptr)
                storeLanes(hn::BitCast(unsignedTag, high), unsignedTag, targets.high + first, lanes);
            if (targets.merged != nullptr)
                storeLanes(hn::BitCast(unsignedTag, hn::Add(low, high)), unsignedTag, targets.merged + first, lanes);
            combined = hn::Add(combined, count);
            withSamples = hn::Sub(withSamples, hn::VecFromMask(intTag, covered));
            mappedLow = hn::Add(mappedLow, low);
            mappedHigh = hn::Add(mappedHigh, high);
        }
        for (std::uint32_t frame = 0u; frame < N; ++frame)
        {
            const std::uint64_t excludedHere = totalOf(excluded[frame]);
            outcome.excludedByValidity += excludedHere;
            if (targets.frameCounters != nullptr)
            {
                targets.frameCounters[static_cast<std::size_t>(frame) * 2u] -= excludedHere;
                targets.frameCounters[static_cast<std::size_t>(frame) * 2u + 1u] += totalOf(rejected[frame]);
            }
        }
        outcome.withSamples += totalOf(withSamples);
        outcome.samplesCombined += totalOf(combined);
        outcome.rejectedLow += totalOf(lowEvents);
        outcome.rejectedHigh += totalOf(highEvents);
        outcome.mappedLow += totalOf(mappedLow);
        outcome.mappedHigh += totalOf(mappedHigh);
    }
    return outcome;
}

} // namespace HWY_NAMESPACE
} // namespace stack_rejection_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace stack_rejection_simd
{
namespace
{

/// One instruction set's rows, indexed by frame count; a count with no row holds null.
using RowTable = std::array<StackRejectionFloat32RowFn, kStackRejectionRowFrames + 1u>;

constexpr RowTable kAvx2Rows{nullptr,
                             nullptr,
                             HWY_CHOOSE_AVX2(judgeRow<2u>),
                             HWY_CHOOSE_AVX2(judgeRow<3u>),
                             HWY_CHOOSE_AVX2(judgeRow<4u>),
                             HWY_CHOOSE_AVX2(judgeRow<5u>),
                             HWY_CHOOSE_AVX2(judgeRow<6u>),
                             HWY_CHOOSE_AVX2(judgeRow<7u>),
                             HWY_CHOOSE_AVX2(judgeRow<8u>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr RowTable kAvx3Rows{nullptr,
                             nullptr,
                             HWY_CHOOSE_AVX3(judgeRow<2u>),
                             HWY_CHOOSE_AVX3(judgeRow<3u>),
                             HWY_CHOOSE_AVX3(judgeRow<4u>),
                             HWY_CHOOSE_AVX3(judgeRow<5u>),
                             HWY_CHOOSE_AVX3(judgeRow<6u>),
                             HWY_CHOOSE_AVX3(judgeRow<7u>),
                             HWY_CHOOSE_AVX3(judgeRow<8u>)};
#endif

/// The row for a frame count, or null for a count the table does not reach.
[[nodiscard]] StackRejectionFloat32RowFn rowOf(const RowTable& rows, const std::uint32_t frames) noexcept
{
    return frames < rows.size() ? rows[frames] : nullptr;
}

} // namespace
} // namespace stack_rejection_simd

StackRejectionFloat32RowFn simdStackRejectionFloat32Row(const InstructionSet instructionSet,
                                                        const std::uint32_t frames) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return stack_rejection_simd::rowOf(stack_rejection_simd::kAvx2Rows, frames);
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return stack_rejection_simd::rowOf(stack_rejection_simd::kAvx3Rows, frames);
#else
        return nullptr;
#endif
    case InstructionSet::Automatic:
    case InstructionSet::Scalar:
    case InstructionSet::Sse2:
    case InstructionSet::Sse42:
    case InstructionSet::Avx:
    case InstructionSet::Neon:
        return nullptr;
    }
    return nullptr;
}

} // namespace an

#endif // HWY_ONCE
