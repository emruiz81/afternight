// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the stack-accumulation row, compiled once per instruction set.
//
// The stack-accumulation row is isolated from the shared SIMD table so its per-instruction-set
// object cost remains attributable. One vector of lanes reads one vector of samples, decides which
// of them are admitted and which of those contribute, and updates every kept term with a lane-wise
// select, so a lane that does not contribute stores exactly what it loaded. Each arithmetic
// operation is the scalar row's, on the same operands in the same order, and no contraction is
// permitted, so the compiled row stores the scalar row's bits; the differential tests prove it over
// every rule, every update, every term and every tail rather than assume it.

#include "backends/cpu/StackAccumulatorCpu.h"

#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS (~(HWY_AVX2 | HWY_AVX3))
#endif

#define HWY_HEADER_ONLY

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "backends/cpu/StackAccumulatorCpu.cpp"
#include <hwy/foreach_target.h> // IWYU pragma: keep

#include <hwy/highway.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

HWY_BEFORE_NAMESPACE();

namespace an
{
namespace stack_accumulator_simd
{
namespace HWY_NAMESPACE
{

namespace hn = hwy::HWY_NAMESPACE;

// The scalar authority, compiled here for the samples a vector of lanes does not cover.
#include "kernels/reference/StackAccumulatorRow.inl"

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

/// Accumulates one contiguous run of samples a vector of lanes at a time, then finishes the tail
/// with the scalar row.
///
/// @tparam SampleT the resident floating representation of the samples.
/// @param samples the first sample of the run.
/// @param count how many samples the run holds.
/// @param parameters the rule, the weights and the update.
/// @param targets the terms, already offset to the run's first element.
/// @return what the run did.
template <typename SampleT>
[[nodiscard]] StackAccumulateRunOutcome stackAccumulateRunOf(const SampleT* samples, std::uint64_t count,
                                                             const StackAccumulateRunParameters& parameters,
                                                             const StackAccumulatorRunTargets& targets) noexcept
{
    const hn::ScalableTag<double> lanesTag;
    const hn::Rebind<float, decltype(lanesTag)> floatTag;
    const hn::Rebind<std::int32_t, decltype(lanesTag)> signedTag;
    const hn::Rebind<std::uint32_t, decltype(lanesTag)> countTag;
    static_assert(hn::MaxLanes(hn::ScalableTag<double>{}) <= 8u, "the packed-mask gather reads at most two bytes");
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(lanesTag));

    const auto zero = hn::Zero(lanesTag);
    const auto one = hn::Set(lanesTag, 1.0);
    const auto lowest = hn::Set(lanesTag, parameters.lowestValid);
    const auto highest = hn::Set(lanesTag, parameters.highestValid);
    const auto frameWeight = hn::Set(lanesTag, parameters.frameWeight);
    const auto everyLane = hn::FirstN(lanesTag, static_cast<std::size_t>(lanes));

    StackAccumulateRunOutcome outcome{};
    bool finite = true;
    std::uint64_t index = 0u;
    for (; index + lanes <= count; index += lanes)
    {
        auto value = zero;
        if constexpr (std::is_same_v<SampleT, float>)
        {
            value = hn::PromoteTo(lanesTag, hn::LoadU(floatTag, samples + index));
        }
        else
        {
            value = hn::LoadU(lanesTag, samples + index);
        }

        auto admitted = everyLane;
        switch (parameters.rule)
        {
        case ValidityRule::AllValid:
            break;
        case ValidityRule::FiniteOnly:
            admitted = hn::IsFinite(value);
            break;
        case ValidityRule::ZeroIsNoData:
            // The scalar row's `value != 0.0` is true for a NaN, which the ordered `hn::Ne` is not,
            // so the rule is the negated equality: a NaN is admitted, and the run refuses it.
            admitted = hn::Not(hn::Eq(value, zero));
            break;
        case ValidityRule::ExplicitMask:
            admitted = maskLanesAt(lanesTag, parameters.maskBits, parameters.maskBitOffset + index, lanes);
            break;
        case ValidityRule::DomainRange:
            admitted = hn::And(hn::Ge(value, lowest), hn::Le(value, highest));
            break;
        }

        auto weight = one;
        auto contributes = admitted;
        if (parameters.weighted)
        {
            weight = frameWeight;
            if (parameters.localWeights != nullptr)
            {
                weight = hn::Mul(weight, hn::PromoteTo(lanesTag, hn::LoadU(floatTag, parameters.localWeights + index)));
            }
            contributes = hn::And(admitted, hn::And(hn::Gt(weight, zero), hn::IsFinite(weight)));
        }
        const std::uint64_t admittedCount = static_cast<std::uint64_t>(hn::CountTrue(lanesTag, admitted));
        const std::uint64_t contributingCount = static_cast<std::uint64_t>(hn::CountTrue(lanesTag, contributes));
        outcome.excludedByValidity += lanes - admittedCount;
        outcome.excludedByWeight += admittedCount - contributingCount;
        outcome.accumulated += contributingCount;

        const auto countsBefore = hn::BitCast(signedTag, hn::LoadU(countTag, targets.counts + index));
        const auto increment = hn::DemoteTo(signedTag, hn::IfThenElseZero(contributes, one));
        const auto countsAfter = hn::Add(countsBefore, increment);
        hn::StoreU(hn::BitCast(countTag, countsAfter), countTag, targets.counts + index);
        const auto after = hn::PromoteTo(lanesTag, countsAfter);
        const auto first = hn::Eq(after, one);

        const auto old = hn::LoadU(lanesTag, targets.sums + index);
        auto term = old;
        switch (parameters.update)
        {
        case StackAccumulateRunUpdate::WeightedSum:
            term = hn::Add(old, hn::Mul(value, weight));
            break;
        case StackAccumulateRunUpdate::Minimum:
            term = hn::IfThenElse(first, value, hn::IfThenElse(hn::Lt(value, old), value, old));
            break;
        case StackAccumulateRunUpdate::Maximum:
            term = hn::IfThenElse(first, value, hn::IfThenElse(hn::Lt(old, value), value, old));
            break;
        }
        const auto stored = hn::IfThenElse(contributes, term, old);
        hn::StoreU(stored, lanesTag, targets.sums + index);
        const auto idle = hn::Not(contributes);
        auto finiteLanes = hn::Or(idle, hn::And(hn::IsFinite(value), hn::IsFinite(stored)));

        if (targets.weightSums != nullptr)
        {
            const auto previous = hn::LoadU(lanesTag, targets.weightSums + index);
            const auto sum = hn::IfThenElse(contributes, hn::Add(previous, weight), previous);
            hn::StoreU(sum, lanesTag, targets.weightSums + index);
            finiteLanes = hn::And(finiteLanes, hn::Or(idle, hn::IsFinite(sum)));
        }
        if (targets.weightSquareSums != nullptr)
        {
            const auto previous = hn::LoadU(lanesTag, targets.weightSquareSums + index);
            const auto sum = hn::IfThenElse(contributes, hn::Add(previous, hn::Mul(weight, weight)), previous);
            hn::StoreU(sum, lanesTag, targets.weightSquareSums + index);
            finiteLanes = hn::And(finiteLanes, hn::Or(idle, hn::IsFinite(sum)));
        }
        if (targets.varianceMoments != nullptr)
        {
            auto mean = zero;
            auto moment = zero;
            hn::LoadInterleaved2(lanesTag, targets.varianceMoments + 2u * index, mean, moment);
            const auto delta = hn::Sub(value, mean);
            const auto meanAfter = hn::Add(mean, hn::Div(delta, after));
            const auto momentAfter = hn::Add(moment, hn::Mul(delta, hn::Sub(value, meanAfter)));
            const auto meanStored = hn::IfThenElse(contributes, meanAfter, mean);
            const auto momentStored = hn::IfThenElse(contributes, momentAfter, moment);
            hn::StoreInterleaved2(meanStored, momentStored, lanesTag, targets.varianceMoments + 2u * index);
            finiteLanes = hn::And(finiteLanes, hn::Or(idle, hn::IsFinite(momentStored)));
        }
        finite = finite && hn::AllTrue(lanesTag, finiteLanes);
    }

    if (index < count)
    {
        StackAccumulateRunParameters tailParameters = parameters;
        tailParameters.maskBitOffset = parameters.maskBitOffset + index;
        tailParameters.localWeights = parameters.localWeights == nullptr ? nullptr : parameters.localWeights + index;
        const StackAccumulatorRunTargets tailTargets{
            .sums = targets.sums + index,
            .counts = targets.counts + index,
            .weightSums = targets.weightSums == nullptr ? nullptr : targets.weightSums + index,
            .weightSquareSums = targets.weightSquareSums == nullptr ? nullptr : targets.weightSquareSums + index,
            .varianceMoments = targets.varianceMoments == nullptr ? nullptr : targets.varianceMoments + 2u * index};
        const StackAccumulateRunOutcome tail =
            stackAccumulateRowOf<SampleT>(samples + index, 1u, count - index, tailParameters, tailTargets);
        outcome.accumulated += tail.accumulated;
        outcome.excludedByValidity += tail.excludedByValidity;
        outcome.excludedByWeight += tail.excludedByWeight;
        finite = finite && tail.finite;
    }
    outcome.finite = finite;
    return outcome;
}

/// One vector of lanes of a band's samples, widened to Float64 exactly.
///
/// @tparam SampleT the band's representation.
/// @param tag the Float64 lane description.
/// @param samples the first sample.
/// @return the samples as Float64 lanes.
template <typename SampleT, class TagT>
[[nodiscard]] HWY_INLINE hn::Vec<TagT> widenedLanes(TagT tag, const SampleT* samples) noexcept
{
    if constexpr (std::is_same_v<SampleT, double>)
    {
        return hn::LoadU(tag, samples);
    }
    else if constexpr (std::is_same_v<SampleT, float>)
    {
        return hn::PromoteTo(tag, hn::LoadU(hn::Rebind<float, TagT>(), samples));
    }
    else
    {
        const hn::Rebind<std::int32_t, TagT> wordTag;
        return hn::PromoteTo(tag, hn::PromoteTo(wordTag, hn::LoadU(hn::Rebind<SampleT, TagT>(), samples)));
    }
}

/// Accumulates one row of every band into the row's terms, a block of lanes at a time with every
/// band in turn inside the block, then finishes the tail with the scalar authority.
///
/// The block's sums and counts stay in registers while every band passes over them, which is what a
/// run per band cannot do; each element still takes its samples in band order, through the same
/// select and the same addition as the single-band row, so its terms are that row's bits.
///
/// @tparam SampleT the bands' representation.
/// @param rows one planar row per band, in band order.
/// @param bands how many bands.
/// @param count how many samples each row holds.
/// @param parameters the rule, the weights and the update, the same for every band.
/// @param targets the terms, already offset to the row's first element.
/// @param admittedPerBand receives, added to what it holds, each band's samples taken in.
/// @return what the run did, over every band.
template <typename SampleT>
[[nodiscard]] StackAccumulateRunOutcome
stackAccumulateBandsRunOf(const SampleT* const* rows, std::uint32_t bands, std::uint64_t count,
                          const StackAccumulateRunParameters& parameters, const StackAccumulatorRunTargets& targets,
                          std::uint64_t* admittedPerBand) noexcept
{
    const bool plain = parameters.update == StackAccumulateRunUpdate::WeightedSum && !parameters.weighted &&
                       parameters.rule != ValidityRule::ExplicitMask && targets.weightSums == nullptr &&
                       targets.weightSquareSums == nullptr && targets.varianceMoments == nullptr;
    if (!plain)
    {
        return stackAccumulateBandsRowOf<SampleT>(rows, bands, count, parameters, targets, admittedPerBand);
    }
    constexpr bool kFloating = std::is_floating_point_v<SampleT>;
    constexpr std::uint64_t kVectors = 4u;
    const hn::ScalableTag<double> lanesTag;
    const hn::Rebind<std::int32_t, decltype(lanesTag)> signedTag;
    const hn::Rebind<std::uint32_t, decltype(lanesTag)> countTag;
    const std::uint64_t lanes = static_cast<std::uint64_t>(hn::Lanes(lanesTag));
    const std::uint64_t block = lanes * kVectors;
    const auto zero = hn::Zero(lanesTag);
    const auto one = hn::Set(lanesTag, 1.0);
    const auto lowest = hn::Set(lanesTag, parameters.lowestValid);
    const auto highest = hn::Set(lanesTag, parameters.highestValid);
    const auto everyLane = hn::FirstN(lanesTag, static_cast<std::size_t>(lanes));
    const ValidityRule rule = parameters.rule;

    StackAccumulateRunOutcome outcome{};
    bool finite = true;
    std::uint64_t index = 0u;
    for (; index + block <= count; index += block)
    {
        hn::Vec<decltype(lanesTag)> sums[kVectors];
        hn::Vec<decltype(lanesTag)> added[kVectors];
        for (std::uint64_t vector = 0u; vector < kVectors; ++vector)
        {
            sums[vector] = hn::LoadU(lanesTag, targets.sums + index + vector * lanes);
            added[vector] = zero;
        }
        auto finiteLanes = everyLane;
        for (std::uint32_t band = 0u; band < bands; ++band)
        {
            const SampleT* const row = rows[band] + index;
            std::uint64_t taken = 0u;
            for (std::uint64_t vector = 0u; vector < kVectors; ++vector)
            {
                const auto value = widenedLanes(lanesTag, row + vector * lanes);
                auto admitted = everyLane;
                switch (rule)
                {
                case ValidityRule::AllValid:
                case ValidityRule::ExplicitMask:
                    break;
                case ValidityRule::FiniteOnly:
                    admitted = hn::IsFinite(value);
                    break;
                case ValidityRule::ZeroIsNoData:
                    // Unordered as the scalar row's `value != 0.0` is, so a NaN is admitted.
                    admitted = hn::Not(hn::Eq(value, zero));
                    break;
                case ValidityRule::DomainRange:
                    admitted = hn::And(hn::Ge(value, lowest), hn::Le(value, highest));
                    break;
                }
                sums[vector] = hn::IfThenElse(admitted, hn::Add(sums[vector], hn::Mul(value, one)), sums[vector]);
                added[vector] = hn::Add(added[vector], hn::IfThenElseZero(admitted, one));
                taken += static_cast<std::uint64_t>(hn::CountTrue(lanesTag, admitted));
                if constexpr (kFloating)
                {
                    finiteLanes =
                        hn::And(finiteLanes,
                                hn::Or(hn::Not(admitted), hn::And(hn::IsFinite(value), hn::IsFinite(sums[vector]))));
                }
            }
            admittedPerBand[band] += taken;
            outcome.accumulated += taken;
        }
        for (std::uint64_t vector = 0u; vector < kVectors; ++vector)
        {
            const std::uint64_t at = index + vector * lanes;
            hn::StoreU(sums[vector], lanesTag, targets.sums + at);
            const auto before = hn::BitCast(signedTag, hn::LoadU(countTag, targets.counts + at));
            const auto after = hn::Add(before, hn::DemoteTo(signedTag, added[vector]));
            hn::StoreU(hn::BitCast(countTag, after), countTag, targets.counts + at);
        }
        finite = finite && hn::AllTrue(lanesTag, finiteLanes);
    }
    outcome.excludedByValidity = static_cast<std::uint64_t>(bands) * index - outcome.accumulated;
    if (index < count)
    {
        const StackAccumulatorRunTargets tailTargets{.sums = targets.sums + index,
                                                     .counts = targets.counts + index,
                                                     .weightSums = nullptr,
                                                     .weightSquareSums = nullptr,
                                                     .varianceMoments = nullptr};
        for (std::uint32_t band = 0u; band < bands; ++band)
        {
            const StackAccumulateRunOutcome tail =
                stackAccumulateRowOf<SampleT>(rows[band] + index, 1u, count - index, parameters, tailTargets);
            admittedPerBand[band] += tail.accumulated;
            outcome.accumulated += tail.accumulated;
            outcome.excludedByValidity += tail.excludedByValidity;
            finite = finite && tail.finite;
        }
    }
    outcome.finite = finite;
    return outcome;
}

} // namespace HWY_NAMESPACE
} // namespace stack_accumulator_simd
} // namespace an

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace an
{
namespace stack_accumulator_simd
{
namespace
{

constexpr StackAccumulatorCpuRuns kAvx2Runs{
    .float32 = HWY_CHOOSE_AVX2(stackAccumulateRunOf<float>),
    .float64 = HWY_CHOOSE_AVX2(stackAccumulateRunOf<double>),
    .bands = {.float32 = HWY_CHOOSE_AVX2(stackAccumulateBandsRunOf<float>),
              .float64 = HWY_CHOOSE_AVX2(stackAccumulateBandsRunOf<double>),
              .uint16 = HWY_CHOOSE_AVX2(stackAccumulateBandsRunOf<std::uint16_t>),
              .int16 = HWY_CHOOSE_AVX2(stackAccumulateBandsRunOf<std::int16_t>),
              .uint8 = HWY_CHOOSE_AVX2(stackAccumulateBandsRunOf<std::uint8_t>),
              .int8 = HWY_CHOOSE_AVX2(stackAccumulateBandsRunOf<std::int8_t>)}};

#if (HWY_TARGETS & HWY_AVX3) != 0
constexpr StackAccumulatorCpuRuns kAvx3Runs{
    .float32 = HWY_CHOOSE_AVX3(stackAccumulateRunOf<float>),
    .float64 = HWY_CHOOSE_AVX3(stackAccumulateRunOf<double>),
    .bands = {.float32 = HWY_CHOOSE_AVX3(stackAccumulateBandsRunOf<float>),
              .float64 = HWY_CHOOSE_AVX3(stackAccumulateBandsRunOf<double>),
              .uint16 = HWY_CHOOSE_AVX3(stackAccumulateBandsRunOf<std::uint16_t>),
              .int16 = HWY_CHOOSE_AVX3(stackAccumulateBandsRunOf<std::int16_t>),
              .uint8 = HWY_CHOOSE_AVX3(stackAccumulateBandsRunOf<std::uint8_t>),
              .int8 = HWY_CHOOSE_AVX3(stackAccumulateBandsRunOf<std::int8_t>)}};
#endif

} // namespace
} // namespace stack_accumulator_simd

const StackAccumulatorCpuRuns* simdStackAccumulatorRuns(const InstructionSet instructionSet) noexcept
{
    switch (instructionSet)
    {
    case InstructionSet::Avx2:
        return &stack_accumulator_simd::kAvx2Runs;
    case InstructionSet::Avx512Skylake:
#if (HWY_TARGETS & HWY_AVX3) != 0
        return &stack_accumulator_simd::kAvx3Runs;
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
