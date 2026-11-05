// AfterNightEngine - the median and auto-adaptive column-combination rows, compiled once per
// instruction set.
//
// The column-combination row is isolated from the shared SIMD table so its per-instruction-set
// object cost remains attributable. One vector of lanes is one pixel per lane: every frame's row is
// read a vector at a time, the frames' rules decide lane by lane which samples are admitted, and
// the admitted samples of each lane's column are laid side by side in the worker's workspace, one
// vector per frame, with an excluded sample replaced by positive infinity so that it sorts past
// every admitted one. The median is then an odd-even merge network over those vectors - a fixed
// sequence of lane-wise minimum and maximum selections whose result is the exactly sorted column
// of every lane at once - followed by a lane-wise pick of the middle sample or the two middle ones.
// The auto-adaptive mean runs the adopted algorithm's arithmetic on every lane at once, adding a
// zero for each excluded sample, which stores the scalar row's bits because no partial sum is ever
// a negative zero. Each pixel that has fewer than the minimum samples, or that escapes on its first
// pass, keeps its plain mean through a lane-wise select. The differential tests hold the compiled
// row to the scalar row bit for bit over every rule, every shape and every tail rather than to a
// tolerance.
//
// The auto-adaptive arithmetic is adapted from DeepSkyStacker's `AutoAdaptiveWeightedAverage`
// (https://github.com/deepskystacker/DSS), whose licence states `Copyright (c) 2006-2019, LucCoiffier`
// and `Copyright (c) 2018-2023, David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali
// Pelenjow, Michal Schulz, Martin Toeltsch` and the three-clause BSD terms (BSD-3-Clause); this file is
// part of that derived work and is distributed under the GNU General Public License, version 3 or any
// later version, with that notice kept, in full in `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`. The
// provenance note is `docs/dev/DEEPSKYSTACKER_PROVENANCE.md`.
//
// The Float32 row has a narrower path for the commonest stack: eight frames combined by the
// median, under the all-valid, finite-only or zero-is-no-data rule, with no variance map. It sorts
// the eight samples of each lane in Float32 with a fixed eight-input network and averages the
// middle pair in Float64; a vector holding a sample that rule excludes, or any non-finite sample,
// and the tail of the row, go through the general row instead.

// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "backends/cpu/StackMethodCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/StackMethodCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace stack_method_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

// The scalar authority, compiled here for the pixels a vector of lanes does not cover.
#include "kernels/reference/StackMethodRow.inl"

/// The admission mask of one vector of lanes under an explicit packed mask.
///
/// The lanes' bits begin at an arbitrary bit of the row, so up to two bytes are gathered and
/// shifted down before the library reads them lane by lane. The second byte is read only when the
/// lanes reach into it, which keeps every read inside the row.
///
/// @tparam TagT the Float64 lane description.
/// @param tag the lane description.
/// @param bits the row's packed bits.
/// @param firstBit the bit of the first lane's sample.
/// @param lanes how many lanes the vector holds, at most eight.
/// @return the mask, one lane per sample.
template <class TagT>
[[nodiscard]] hn::Mask<TagT> maskLanesAt(TagT tag, const std::uint8_t* bits, std::uint64_t firstBit,
                                         std::uint64_t lanes) noexcept
{
    const std::uint64_t byte = firstBit / 8u;
    const unsigned shift = static_cast<unsigned>(firstBit % 8u);
    std::uint32_t word = bits[byte];
    if (shift + lanes > 8u)
    {
        word |= static_cast<std::uint32_t>(bits[byte + 1u]) << 8u;
    }
    const std::uint8_t laneBits = static_cast<std::uint8_t>(word >> shift);
    return hn::LoadMaskBits(tag, &laneBits);
}

/// Orders eight vectors lane by lane without changing any sample value.
/// @tparam VectorT a vector of finite samples or positive infinity.
/// @param a the first vector, smallest on return.
/// @param b the second vector.
/// @param c the third vector.
/// @param d the fourth vector.
/// @param e the fifth vector.
/// @param f the sixth vector.
/// @param g the seventh vector.
/// @param h the eighth vector, largest on return.
template <class VectorT>
HWY_INLINE void sortEight(VectorT& a, VectorT& b, VectorT& c, VectorT& d, VectorT& e, VectorT& f, VectorT& g,
                          VectorT& h) noexcept
{
    const auto exchange = [](auto& low, auto& high) {
        const auto first = low;
        low = hn::Min(first, high);
        high = hn::Max(first, high);
    };
    exchange(a, b);
    exchange(c, d);
    exchange(e, f);
    exchange(g, h);
    exchange(a, c);
    exchange(b, d);
    exchange(e, g);
    exchange(f, h);
    exchange(b, c);
    exchange(f, g);
    exchange(a, e);
    exchange(d, h);
    exchange(b, f);
    exchange(c, g);
    exchange(b, e);
    exchange(d, g);
    exchange(c, e);
    exchange(d, f);
    exchange(d, e);
}

/// Sorts every lane's column at once with an odd-even merge network over the workspace vectors.
///
/// The network is the same fixed sequence of compare-exchanges for every column of a given
/// length, so it has no data-dependent branch and every lane is sorted exactly; a lane-wise
/// minimum and maximum are exact selections over finite samples and infinities.
///
/// @tparam TagT the Float64 lane description.
/// @param tag the lane description.
/// @param workspace the columns, one vector of `lanes` doubles per frame.
/// @param count how many vectors, which is the frame count.
/// @param lanes how many doubles one vector holds.
template <class TagT>
void sortColumnLanes(TagT tag, double* workspace, std::uint32_t count, std::uint64_t lanes) noexcept
{
    for (std::uint32_t p = 1u; p < count; p *= 2u)
    {
        for (std::uint32_t k = p; k >= 1u; k /= 2u)
        {
            for (std::uint32_t j = k % p; j + k < count; j += 2u * k)
            {
                for (std::uint32_t i = 0u; i < k; ++i)
                {
                    const std::uint32_t low = i + j;
                    const std::uint32_t high = i + j + k;
                    if (high >= count)
                    {
                        break;
                    }
                    if (low / (2u * p) != high / (2u * p))
                    {
                        continue;
                    }
                    double* const lowVector = workspace + static_cast<std::uint64_t>(low) * lanes;
                    double* const highVector = workspace + static_cast<std::uint64_t>(high) * lanes;
                    const auto a = hn::LoadU(tag, lowVector);
                    const auto b = hn::LoadU(tag, highVector);
                    hn::StoreU(hn::Min(a, b), tag, lowVector);
                    hn::StoreU(hn::Max(a, b), tag, highVector);
                }
            }
        }
    }
}

/// Combines one row of one channel a vector of pixels at a time, then finishes the tail with the
/// scalar row.
///
/// @tparam SampleT the resident floating representation of the bands' samples.
/// @param firstPixel the first pixel of the row the run covers.
/// @param count how many pixels the run covers.
/// @param parameters the frames, the row and the combination. Every frame must be planar.
/// @param scratch the worker's column and workspace.
/// @param targets the rows of the output, the mask and the maps.
/// @return what the run did.
template <typename SampleT>
[[nodiscard]] StackColumnRunOutcome
stackColumnRunOf(std::uint64_t firstPixel, std::uint64_t count, const StackColumnRunParameters& parameters,
                 const StackColumnRunScratch& scratch, const StackColumnRunTargets& targets) noexcept
{
    const hn::ScalableTag<double> tag;
    const hn::Rebind<float, decltype(tag)> floatTag;
    const hn::Rebind<std::int32_t, decltype(tag)> signedTag;
    const hn::Rebind<std::uint32_t, decltype(tag)> countTag;
    static_assert(hn::MaxLanes(hn::ScalableTag<double>{}) <= kStackColumnRunLanes,
                  "the column workspace holds fewer columns than a vector has lanes");
    static_assert(hn::MaxLanes(hn::ScalableTag<double>{}) <= 8u, "the packed-mask gather reads at most two bytes");
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(tag));

    const auto zero = hn::Zero(tag);
    const auto one = hn::Set(tag, 1.0);
    const auto half = hn::Set(tag, 0.5);
    const auto infinity = hn::Set(tag, std::numeric_limits<double>::infinity());
    const auto scale = hn::Set(tag, parameters.sampleScale);
    const auto smallSigma = hn::Set(tag, parameters.smallSigma);
    const auto minimumSamples = hn::Set(tag, static_cast<double>(parameters.minimumSamples));
    const auto everyLane = hn::FirstN(tag, static_cast<std::size_t>(lanes));
    const std::uint32_t frameCount = parameters.frameCount;
    double* const values = scratch.workspace;
    double* const weights = scratch.workspace + static_cast<std::uint64_t>(frameCount) * lanes;
    const auto fullCount = hn::Set(tag, static_cast<double>(frameCount));

    StackColumnRunOutcome outcome{};
    bool finite = true;
    const std::uint64_t end = firstPixel + count;
    std::uint64_t x = firstPixel;
    for (; x + lanes <= end; x += lanes)
    {
        auto n = zero;
        auto sum = zero;
        auto finiteLanes = everyLane;
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            const StackColumnFrame& frame = parameters.frames[index];
            const std::byte* const row = stackColumnFrameRow(frame, parameters.channel, parameters.y);
            auto value = zero;
            if constexpr (std::is_same_v<SampleT, float>)
            {
                value = hn::PromoteTo(tag, hn::LoadU(floatTag, reinterpret_cast<const float*>(row) + x));
            }
            else
            {
                value = hn::LoadU(tag, reinterpret_cast<const double*>(row) + x);
            }
            value = hn::Add(value, zero);

            auto admitted = everyLane;
            switch (frame.rule)
            {
            case ValidityRule::AllValid:
                break;
            case ValidityRule::FiniteOnly:
                admitted = hn::IsFinite(value);
                break;
            case ValidityRule::ZeroIsNoData:
                // The scalar row's `value != 0.0` is true for a NaN, which the ordered `hn::Ne` is
                // not, so the rule is the negated equality: a NaN is admitted, and the run refuses it.
                admitted = hn::Not(hn::Eq(value, zero));
                break;
            case ValidityRule::ExplicitMask:
                admitted = maskLanesAt(tag, stackColumnFrameMaskRow(frame, parameters.channel, parameters.y), x, lanes);
                break;
            case ValidityRule::DomainRange:
                admitted = hn::And(hn::Ge(value, hn::Set(tag, frame.lowestValid)),
                                   hn::Le(value, hn::Set(tag, frame.highestValid)));
                break;
            }
            finiteLanes = hn::And(finiteLanes, hn::Or(hn::Not(admitted), hn::IsFinite(value)));
            n = hn::Add(n, hn::IfThenElseZero(admitted, one));
            sum = hn::Add(sum, hn::IfThenElseZero(admitted, value));
            hn::StoreU(hn::IfThenElse(admitted, value, infinity), tag,
                       values + static_cast<std::uint64_t>(index) * lanes);
        }
        finite = finite && hn::AllTrue(tag, finiteLanes);
        const auto hasSamples = hn::Gt(n, zero);
        const std::uint64_t admittedHere = static_cast<std::uint64_t>(hn::ReduceSum(tag, n));
        outcome.admitted += admittedHere;
        outcome.excludedByValidity += lanes * frameCount - admittedHere;
        outcome.withSamples += static_cast<std::uint64_t>(hn::CountTrue(tag, hasSamples));

        auto variance = zero;
        if (targets.variance != nullptr)
        {
            const auto mean = hn::Div(sum, n);
            auto squares = zero;
            for (std::uint32_t index = 0u; index < frameCount; ++index)
            {
                const auto value = hn::LoadU(tag, values + static_cast<std::uint64_t>(index) * lanes);
                const auto deviation = hn::Sub(value, mean);
                squares = hn::Add(squares, hn::IfThenElseZero(hn::Ne(value, infinity), hn::Mul(deviation, deviation)));
            }
            variance = hn::IfThenElseZero(hn::Gt(n, one), hn::Div(squares, hn::Sub(n, one)));
        }

        auto result = zero;
        switch (parameters.combination)
        {
        case StackColumnRunCombination::Median: {
            sortColumnLanes(tag, values, frameCount, lanes);
            if (hn::AllTrue(tag, hn::Eq(n, fullCount)))
            {
                // Every lane has the same count, so the two middle vectors are known. Selecting
                // them by comparing every column index with every lane repeats work per frame.
                const auto upper = hn::LoadU(tag, values + (frameCount / 2u) * lanes);
                result = upper;
                if (frameCount % 2u == 0u)
                {
                    const auto lower = hn::LoadU(tag, values + (frameCount / 2u - 1u) * lanes);
                    result = hn::Mul(hn::Add(lower, upper), half);
                }
                break;
            }
            const auto halfCount = hn::Mul(n, half);
            const auto upperIndex = hn::Floor(halfCount);
            const auto odd = hn::Ne(halfCount, upperIndex);
            const auto lowerIndex = hn::Sub(upperIndex, one);
            auto upper = zero;
            auto lower = zero;
            for (std::uint32_t index = 0u; index < frameCount; ++index)
            {
                const auto position = hn::Set(tag, static_cast<double>(index));
                const auto value = hn::LoadU(tag, values + static_cast<std::uint64_t>(index) * lanes);
                upper = hn::IfThenElse(hn::Eq(position, upperIndex), value, upper);
                lower = hn::IfThenElse(hn::Eq(position, lowerIndex), value, lower);
            }
            const auto even = hn::Mul(hn::Add(lower, upper), half);
            result = hn::IfThenElseZero(hasSamples, hn::IfThenElse(odd, upper, even));
            break;
        }
        case StackColumnRunCombination::AutoAdaptiveWeightedAverage: {
            auto scaledSum = zero;
            for (std::uint32_t index = 0u; index < frameCount; ++index)
            {
                const auto value = hn::LoadU(tag, values + static_cast<std::uint64_t>(index) * lanes);
                scaledSum = hn::Add(scaledSum, hn::IfThenElseZero(hn::Ne(value, infinity), hn::Mul(value, scale)));
            }
            auto average = hn::Div(scaledSum, n);
            const auto plain = hn::Div(average, scale);
            auto settled = hn::Lt(n, minimumSamples);
            for (std::uint32_t iteration = 0u; iteration < parameters.iterations; ++iteration)
            {
                auto squares = zero;
                for (std::uint32_t index = 0u; index < frameCount; ++index)
                {
                    const auto value = hn::LoadU(tag, values + static_cast<std::uint64_t>(index) * lanes);
                    const auto deviation = hn::Sub(hn::Mul(value, scale), average);
                    squares =
                        hn::Add(squares, hn::IfThenElseZero(hn::Ne(value, infinity), hn::Mul(deviation, deviation)));
                }
                const auto sigma = hn::Sqrt(hn::Div(squares, n));
                if (iteration == 0u)
                {
                    settled = hn::Or(settled, hn::Le(sigma, smallSigma));
                }
                auto total = zero;
                for (std::uint32_t index = 0u; index < frameCount; ++index)
                {
                    const auto value = hn::LoadU(tag, values + static_cast<std::uint64_t>(index) * lanes);
                    const auto distance = hn::Abs(hn::Sub(hn::Mul(value, scale), average));
                    const auto ratio = hn::Div(distance, sigma);
                    const auto weight = hn::Div(one, hn::Add(one, hn::Mul(ratio, ratio)));
                    hn::StoreU(weight, tag, weights + static_cast<std::uint64_t>(index) * lanes);
                    total = hn::Add(total, hn::IfThenElseZero(hn::Ne(value, infinity), weight));
                }
                auto next = zero;
                for (std::uint32_t index = 0u; index < frameCount; ++index)
                {
                    const auto value = hn::LoadU(tag, values + static_cast<std::uint64_t>(index) * lanes);
                    const auto weight = hn::LoadU(tag, weights + static_cast<std::uint64_t>(index) * lanes);
                    const auto term = hn::Div(hn::Mul(hn::Mul(value, scale), weight), total);
                    next = hn::Add(next, hn::IfThenElseZero(hn::Ne(value, infinity), term));
                }
                average = next;
            }
            result = hn::IfThenElseZero(hasSamples, hn::IfThenElse(settled, plain, hn::Div(average, scale)));
            break;
        }
        }
        finite = finite && hn::AllTrue(tag, hn::Or(hn::Not(hasSamples), hn::IsFinite(result)));

        if (parameters.outputFloat64)
        {
            hn::StoreU(result, tag, reinterpret_cast<double*>(targets.output) + x);
        }
        else
        {
            const auto narrowed = hn::DemoteTo(floatTag, result);
            finite = finite && hn::AllTrue(floatTag, hn::IsFinite(narrowed));
            hn::StoreU(narrowed, floatTag, reinterpret_cast<float*>(targets.output) + x);
        }
        if (targets.maskRow != nullptr)
        {
            std::uint8_t bits[8] = {};
            hn::StoreMaskBits(tag, hasSamples, bits);
            const std::uint32_t word = static_cast<std::uint32_t>(bits[0]) << (x % 8u);
            targets.maskRow[x / 8u] |= static_cast<std::uint8_t>(word & 0xFFu);
            if (x % 8u + lanes > 8u)
            {
                targets.maskRow[x / 8u + 1u] |= static_cast<std::uint8_t>(word >> 8u);
            }
        }
        if (targets.coverage != nullptr)
        {
            hn::StoreU(hn::BitCast(countTag, hn::DemoteTo(signedTag, n)), countTag, targets.coverage + x);
        }
        if (targets.variance != nullptr)
        {
            hn::StoreU(hn::DemoteTo(floatTag, variance), floatTag, targets.variance + x);
        }
        if (targets.gain != nullptr)
        {
            hn::StoreU(hn::DemoteTo(floatTag, n), floatTag, targets.gain + x);
        }
        if (targets.weightSum != nullptr)
        {
            hn::StoreU(hn::DemoteTo(floatTag, n), floatTag, targets.weightSum + x);
        }
    }

    if (x < end)
    {
        const StackColumnRunOutcome tail = stackColumnRowOf<SampleT>(x, end - x, parameters, scratch, targets);
        outcome.admitted += tail.admitted;
        outcome.excludedByValidity += tail.excludedByValidity;
        outcome.withSamples += tail.withSamples;
        finite = finite && tail.finite;
    }
    outcome.finite = finite;
    return outcome;
}

/// Uses Float32 lanes to select eight finite samples' middle pair, then averages in Float64.
/// Excluded samples, other validity rules, variance requests and tails use the general row.
/// @param firstPixel the row's first pixel.
/// @param count the number of pixels.
/// @param parameters the checked planar frames and combination.
/// @param scratch the general row's workspace.
/// @param targets the output rows and optional maps.
/// @return the same samples and counters as the general row.
[[nodiscard]] StackColumnRunOutcome stackFloat32Run(std::uint64_t firstPixel, std::uint64_t count,
                                                    const StackColumnRunParameters& parameters,
                                                    const StackColumnRunScratch& scratch,
                                                    const StackColumnRunTargets& targets) noexcept
{
    const auto general = [&](std::uint64_t first, std::uint64_t size) {
        return stackColumnRunOf<float>(first, size, parameters, scratch, targets);
    };
    if (parameters.frameCount != 8u || parameters.combination != StackColumnRunCombination::Median ||
        targets.variance != nullptr)
        return general(firstPixel, count);
    std::array<const float*, 8u> rows;
    std::array<bool, 8u> excludesZero;
    for (std::uint32_t frame = 0u; frame < 8u; ++frame)
    {
        const auto& record = parameters.frames[frame];
        const auto rule = record.rule;
        if (rule != ValidityRule::AllValid && rule != ValidityRule::FiniteOnly && rule != ValidityRule::ZeroIsNoData)
            return general(firstPixel, count);
        rows[frame] = reinterpret_cast<const float*>(stackColumnFrameRow(record, parameters.channel, parameters.y));
        excludesZero[frame] = rule == ValidityRule::ZeroIsNoData;
    }
    const hn::ScalableTag<float> tag;
    const hn::Repartition<double, decltype(tag)> doubleTag;
    const hn::Half<decltype(tag)> halfTag;
    const hn::RebindToUnsigned<decltype(tag)> countTag;
    static_assert(hn::MaxLanes(tag) <= 16u, "the packed output mask must fit in a shifted 32-bit word");
    const std::uint64_t lanes = hn::Lanes(tag);
    const auto zero = hn::Zero(tag);
    const auto half = hn::Set(doubleTag, 0.5);
    StackColumnRunOutcome outcome{};
    const auto include = [&](const StackColumnRunOutcome& part) {
        outcome.admitted += part.admitted;
        outcome.excludedByValidity += part.excludedByValidity;
        outcome.withSamples += part.withSamples;
        outcome.finite = outcome.finite && part.finite;
    };
    const std::uint64_t end = firstPixel + count;
    std::uint64_t x = firstPixel;
    for (; x + lanes <= end; x += lanes)
    {
        auto complete = hn::FirstN(tag, lanes);
        const auto load = [&](std::uint32_t frame) {
            const auto raw = hn::LoadU(tag, rows[frame] + x);
            // The scalar reader adds positive zero after widening. State its signed-zero
            // canonicalization explicitly: a compiler may remove Float32 addition by zero.
            const auto value = hn::IfThenElse(hn::Eq(raw, zero), zero, raw);
            complete = hn::And(complete, hn::IsFinite(value));
            // The zero rule's unordered test, as the general row spells it; a NaN has already
            // failed the finite test, so either spelling sends its vector to the general row.
            if (excludesZero[frame])
                complete = hn::AndNot(hn::Eq(value, zero), complete);
            return value;
        };
        auto a = load(0u);
        auto b = load(1u);
        auto c = load(2u);
        auto d = load(3u);
        auto e = load(4u);
        auto f = load(5u);
        auto g = load(6u);
        auto h = load(7u);
        if (!hn::AllTrue(tag, complete))
        {
            include(general(x, lanes));
            continue;
        }
        sortEight(a, b, c, d, e, f, g, h);
        const auto low = hn::Mul(hn::Add(hn::PromoteLowerTo(doubleTag, d), hn::PromoteLowerTo(doubleTag, e)), half);
        const auto high = hn::Mul(hn::Add(hn::PromoteUpperTo(doubleTag, d), hn::PromoteUpperTo(doubleTag, e)), half);
        if (parameters.outputFloat64)
        {
            hn::StoreU(low, doubleTag, reinterpret_cast<double*>(targets.output) + x);
            hn::StoreU(high, doubleTag, reinterpret_cast<double*>(targets.output) + x + lanes / 2u);
        }
        else
            hn::StoreU(hn::Combine(tag, hn::DemoteTo(halfTag, high), hn::DemoteTo(halfTag, low)), tag,
                       reinterpret_cast<float*>(targets.output) + x);
        if (targets.maskRow != nullptr)
        {
            const auto shift = static_cast<unsigned>(x % 8u);
            const std::uint32_t bits = ((std::uint32_t{1} << lanes) - 1u) << shift;
            for (unsigned bit = 0u; bit < shift + lanes; bit += 8u)
                targets.maskRow[x / 8u + bit / 8u] |= static_cast<std::uint8_t>(bits >> bit);
        }
        if (targets.coverage != nullptr)
            hn::StoreU(hn::Set(countTag, 8u), countTag, targets.coverage + x);
        if (targets.gain != nullptr)
            hn::StoreU(hn::Set(tag, 8.0f), tag, targets.gain + x);
        if (targets.weightSum != nullptr)
            hn::StoreU(hn::Set(tag, 8.0f), tag, targets.weightSum + x);
        outcome.admitted += 8u * lanes;
        outcome.withSamples += lanes;
    }
    if (x < end)
        include(general(x, end - x));
    return outcome;
}

} // namespace HWY_NAMESPACE
} // namespace stack_method_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace stack_method_simd
{
namespace
{

constexpr StackMethodCpuRuns kAvx2Runs{.float32 = HWY_CHOOSE_AVX2(stackFloat32Run),
                                       .float64 = HWY_CHOOSE_AVX2(stackColumnRunOf<double>)};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr StackMethodCpuRuns kAvx3Runs{.float32 = HWY_CHOOSE_AVX3(stackFloat32Run),
                                       .float64 = HWY_CHOOSE_AVX3(stackColumnRunOf<double>)};
#endif

} // namespace
} // namespace stack_method_simd

const StackMethodCpuRuns* simdStackMethodRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &stack_method_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &stack_method_simd::kAvx3Runs;
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
