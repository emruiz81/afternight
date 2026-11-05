// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the fast conversion and strided-copy bodies, and the one rule that keeps
// them honest.
//
// Every one of them produces exactly what the reference produces, sample for sample and counter for
// counter, and the way that is achieved is not care: in the general body the arithmetic is the same
// `convertSample` call. What these kernels remove is everything around it - the policy read from
// memory, the switch on a rounding mode that never changes, the per-sample branch that increments a
// counter - and none of that is arithmetic. A body that reimplemented the conversion to go faster
// would be a second set of rules to keep in agreement, which is the thing a reference exists to
// avoid.
//
// Four bodies do state their own arithmetic, because the call would cost them the speed they exist
// for, and each is written to land on the reference's bits. UInt16 to Float32 under the defaults
// multiplies by the reciprocal of the full scale where the reference divides by it; the quotient of
// a sixteen-bit code by 65535 never lies close enough to a Float32 rounding boundary for the two to
// narrow differently. Float32 to UInt16 under the truncating policy repeats the reference's scale,
// clamp and truncation step for step, with the same non-finite mapping and the same counts. Float32
// to an unsigned eight- or sixteen-bit code under the defaults repeats the reference's exact product,
// clamp and rounding half to even as selections rather than branches, which is what a pointwise
// result narrowed back into a camera frame spends its time on. Every other floating sample narrowed
// to an integer under the defaults, or as a code value, takes the same selections: the reference's
// product in double where the domain maps full scales, its clamp, and its rounding half to even.

#include "backends/cpu/ConversionKernelsCpu.h"

#include "kernels/reference/ConversionKernels.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace an
{
namespace
{

/// The five decisions almost every conversion in this engine makes.
///
/// Named as a constant so that a body specialised for it has the decisions as compile-time values
/// rather than as fields, which is what lets the scaling collapse to one multiply and the rounding
/// to the two or three instructions `NearestEven` actually needs.
constexpr ConversionPolicy kApprovedDefaults{};

/// The truncating policy the conversion surface exposes.
constexpr ConversionPolicy kTowardZeroDefaults{.rounding = RoundingMode::TowardZero};

/// The approved decisions with each number standing for itself: how a pointwise result, and a signed integer's or a
/// 32-bit integer's working copy, are narrowed back to codes.
constexpr ConversionPolicy kCodeValueDefaults{.domain = DomainMapping::CodeValue};

/// Converts one run under the approved defaults, with the decisions folded in.
///
/// The counters are accumulated in locals and added once at the end: incrementing a member of a
/// referenced object per sample is a load and a store the loop does not otherwise need, and the
/// two flags are booleans a compiler turns into arithmetic when nothing branches on them.
///
/// @tparam SourceV what the samples are.
/// @tparam DestV what they are to become.
/// @param source where the samples to convert start.
/// @param destination where the converted samples go.
/// @param count how many samples.
/// @param counters what the run met, added to whatever they already hold.
template <SampleType SourceV, SampleType DestV>
void convertRunDefaults(const void* source, void* destination, std::uint64_t count,
                        ConversionCounters& counters) noexcept
{
    using Source = SampleOf<SourceV>;
    using Dest = SampleOf<DestV>;
    const Source* const in = static_cast<const Source*>(source);
    Dest* const out = static_cast<Dest*>(destination);

    std::uint64_t nonFinite = 0u;
    std::uint64_t saturated = 0u;
    if constexpr (SourceV == SampleType::UInt16 && DestV == SampleType::Float32)
    {
        constexpr double kInverseFullScale = 1.0 / 65535.0;
        for (std::uint64_t index = 0u; index < count; ++index)
        {
            out[index] = static_cast<float>(static_cast<double>(in[index]) * kInverseFullScale);
        }
        counters.samplesConverted += count;
        return;
    }
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const ConvertedSample<Dest> converted = convertSample<Source, Dest>(in[index], kApprovedDefaults);
        nonFinite += converted.sourceWasNonFinite ? 1u : 0u;
        saturated += converted.saturated ? 1u : 0u;
        out[index] = converted.value;
    }
    counters.samplesConverted += count;
    counters.nonFiniteInputs += nonFinite;
    counters.saturatedSamples += saturated;
}

/// Converts Float32 samples to an unsigned eight- or sixteen-bit code under the approved defaults.
///
/// The reference's arithmetic without its branches. The product by the full scale is exact in double, because a
/// Float32 significand times a full scale of sixteen bits needs at most forty; the clamp is a pair of selections, and
/// the rounding half to even is the reference's own comparison of the fraction left by truncation, written as
/// arithmetic, so a run of samples about half a code costs no misprediction. A sample that is not finite takes the
/// reference's mapping and is counted, and a clamped one is counted.
///
/// @tparam DestT the unsigned eight- or sixteen-bit representation.
/// @param source where the samples start.
/// @param destination where the converted samples go.
/// @param count how many samples.
/// @param counters what the run met, added to whatever they already hold.
template <typename DestT>
void convertFloat32ToUnsignedNearestEven(const void* source, void* destination, std::uint64_t count,
                                         ConversionCounters& counters) noexcept
{
    const auto* const in = static_cast<const float*>(source);
    auto* const out = static_cast<DestT*>(destination);
    constexpr double kFullScale = static_cast<double>((std::numeric_limits<DestT>::max)());
    constexpr float kLargest = (std::numeric_limits<float>::max)();
    std::uint64_t nonFinite = 0u;
    std::uint64_t saturated = 0u;
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const float value = in[index];
        const bool finite = value >= -kLargest && value <= kLargest;
        const double real = finite ? static_cast<double>(value) * kFullScale : 0.0;
        const bool clamped = real < 0.0 || real > kFullScale;
        const double held = real < 0.0 ? 0.0 : (real > kFullScale ? kFullScale : real);
        const std::int32_t whole = static_cast<std::int32_t>(held);
        const double fraction = held - static_cast<double>(whole);
        const std::int32_t up =
            static_cast<std::int32_t>(fraction > 0.5) | (static_cast<std::int32_t>(fraction == 0.5) & (whole & 1));
        const DestT mapped = value > 0.0f ? (std::numeric_limits<DestT>::max)() : DestT{0u};
        out[index] = finite ? static_cast<DestT>(whole + up) : mapped;
        nonFinite += finite ? 0u : 1u;
        saturated += finite && clamped ? 1u : 0u;
    }
    counters.samplesConverted += count;
    counters.nonFiniteInputs += nonFinite;
    counters.saturatedSamples += saturated;
}

/// Converts floating samples to an integer representation under the approved rounding and range decisions, at full
/// scale or as code values.
///
/// The reference's arithmetic without its branches, as the unsigned body above does it: the sample in double -
/// times the destination's full scale when the full scales correspond, the product the reference forms - held to
/// what the destination holds, and rounded half to even by the reference's own comparison of the fraction left by
/// truncation, in either direction. The truncation is into the narrowest integer that holds every code of the
/// destination, so it stays a vector instruction wherever one exists. A sample that is not finite takes the
/// reference's mapping and is counted, and a clamped one is counted.
///
/// @tparam SourceT Float32 or Float64.
/// @tparam DestT one of the six integer representations.
/// @tparam FullScaleV whether the full scales correspond, rather than each number standing for itself.
/// @param source where the samples start.
/// @param destination where the converted samples go.
/// @param count how many samples.
/// @param counters what the run met, added to whatever they already hold.
template <typename SourceT, typename DestT, bool FullScaleV>
void convertFloatingToIntegerNearestEven(const void* source, void* destination, std::uint64_t count,
                                         ConversionCounters& counters) noexcept
{
    using WholeT = std::conditional_t<std::is_same_v<DestT, std::uint32_t>, std::int64_t, std::int32_t>;
    const auto* const in = static_cast<const SourceT*>(source);
    auto* const out = static_cast<DestT*>(destination);
    constexpr double kLow = kSampleMinimumOf<DestT>;
    constexpr double kHigh = kSampleMaximumOf<DestT>;
    constexpr SourceT kLargest = (std::numeric_limits<SourceT>::max)();
    std::uint64_t nonFinite = 0u;
    std::uint64_t saturated = 0u;
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const SourceT value = in[index];
        const bool finite = value >= -kLargest && value <= kLargest;
        double real = finite ? static_cast<double>(value) : 0.0;
        if constexpr (FullScaleV)
        {
            real *= kHigh;
        }
        const bool clamped = real < kLow || real > kHigh;
        const double held = real < kLow ? kLow : (real > kHigh ? kHigh : real);
        const WholeT whole = static_cast<WholeT>(held);
        const double fraction = held - static_cast<double>(whole);
        const WholeT odd = whole & WholeT{1};
        const WholeT up = static_cast<WholeT>(fraction > 0.5) | (static_cast<WholeT>(fraction == 0.5) & odd);
        const WholeT down = static_cast<WholeT>(fraction < -0.5) | (static_cast<WholeT>(fraction == -0.5) & odd);
        const DestT mapped = value > SourceT{0}   ? (std::numeric_limits<DestT>::max)()
                             : value < SourceT{0} ? (std::numeric_limits<DestT>::lowest)()
                                                  : DestT{0};
        out[index] = finite ? static_cast<DestT>(whole + up - down) : mapped;
        nonFinite += finite ? 0u : 1u;
        saturated += finite && clamped ? 1u : 0u;
    }
    counters.samplesConverted += count;
    counters.nonFiniteInputs += nonFinite;
    counters.saturatedSamples += saturated;
}

/// Converts normalized Float32 samples to UInt16 with truncation toward zero.
///
/// @param source where the samples start.
/// @param destination where the converted samples go.
/// @param count how many samples.
/// @param counters what the run met, added to whatever they already hold.
void convertFloat32ToUInt16TowardZero(const void* source, void* destination, std::uint64_t count,
                                      ConversionCounters& counters) noexcept
{
    const auto* const in = static_cast<const float*>(source);
    auto* const out = static_cast<std::uint16_t*>(destination);
    constexpr double kFullScale = static_cast<double>((std::numeric_limits<std::uint16_t>::max)());
    std::uint64_t nonFinite = 0u;
    std::uint64_t saturated = 0u;
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const float sourceValue = in[index];
        if (!std::isfinite(sourceValue))
        {
            ++nonFinite;
            out[index] = sourceValue == sourceValue && sourceValue > 0.0f ? (std::numeric_limits<std::uint16_t>::max)()
                                                                          : std::uint16_t{0u};
            continue;
        }
        double real = static_cast<double>(sourceValue) * kFullScale;
        if (real < 0.0)
        {
            real = 0.0;
            ++saturated;
        }
        else if (real > kFullScale)
        {
            real = kFullScale;
            ++saturated;
        }
        out[index] = static_cast<std::uint16_t>(real);
    }
    counters.samplesConverted += count;
    counters.nonFiniteInputs += nonFinite;
    counters.saturatedSamples += saturated;
}

/// Converts one run, taking the fast body where it applies and the reference where it does not.
///
/// @tparam SourceV what the samples are.
/// @tparam DestV what they are to become.
/// @param source where the samples to convert start.
/// @param destination where the converted samples go.
/// @param count how many samples.
/// @param policy the five decisions.
/// @param counters what the run met, added to whatever they already hold.
/// @return where the kernel stopped, or a default-constructed outcome when it converted everything.
///         The fast bodies never stop early: neither refusing policy is one of the approved
///         defaults.
template <SampleType SourceV, SampleType DestV>
[[nodiscard]] ConversionRunOutcome convertRunCpuOf(const void* source, void* destination, std::uint64_t count,
                                                   const ConversionPolicy& policy,
                                                   ConversionCounters& counters) noexcept
{
    using Source = SampleOf<SourceV>;
    using Dest = SampleOf<DestV>;

    // Named rather than read in every branch: the copy body has no use for a policy, and a
    // parameter a compiler can see is unused is a warning this build treats as an error.
    (void)policy;

    if constexpr (std::is_same_v<Source, Dest> && !std::is_floating_point_v<Source>)
    {
        // An integer sample cannot be non-finite and cannot leave its own range, so every policy
        // agrees that this run is a copy and no policy has anything to count but the samples.
        if (count > 0u)
        {
            std::memcpy(destination, source, static_cast<std::size_t>(count) * sizeof(Source));
        }
        counters.samplesConverted += count;
        return ConversionRunOutcome{};
    }
    else
    {
        if constexpr (SourceV == SampleType::Float32 && DestV == SampleType::UInt16)
        {
            if (policy == kTowardZeroDefaults)
            {
                convertFloat32ToUInt16TowardZero(source, destination, count, counters);
                return ConversionRunOutcome{};
            }
        }
        if constexpr (SourceV == SampleType::Float32 && (DestV == SampleType::UInt16 || DestV == SampleType::UInt8))
        {
            if (policy == kApprovedDefaults)
            {
                convertFloat32ToUnsignedNearestEven<Dest>(source, destination, count, counters);
                return ConversionRunOutcome{};
            }
        }
        if constexpr (std::is_floating_point_v<Source> && !std::is_floating_point_v<Dest>)
        {
            if (policy == kApprovedDefaults)
            {
                convertFloatingToIntegerNearestEven<Source, Dest, true>(source, destination, count, counters);
                return ConversionRunOutcome{};
            }
            if (policy == kCodeValueDefaults)
            {
                convertFloatingToIntegerNearestEven<Source, Dest, false>(source, destination, count, counters);
                return ConversionRunOutcome{};
            }
        }
        if (policy == kApprovedDefaults)
        {
            convertRunDefaults<SourceV, DestV>(source, destination, count, counters);
            return ConversionRunOutcome{};
        }
        const ConvertRunFn fallback = referenceConvertRun(SourceV, DestV);
        return fallback(source, destination, count, policy, counters);
    }
}

/// Copies one run between two strides, taking the contiguous cases as runs.
///
/// A repack always has one contiguous side - the planar one - so the two specialised bodies cover
/// every repack this engine performs, and the general body covers a caller that asked for something
/// else.
///
/// @tparam SampleV what the samples are.
/// @param source where the samples to copy start.
/// @param sourceStrideSamples how many samples from one source sample to the next.
/// @param destination where the copies go.
/// @param destinationStrideSamples how many samples from one destination sample to the next.
/// @param count how many samples.
template <SampleType SampleV>
void strideCopyRunCpuOf(const void* source, std::uint64_t sourceStrideSamples, void* destination,
                        std::uint64_t destinationStrideSamples, std::uint64_t count) noexcept
{
    using Sample = SampleOf<SampleV>;
    const Sample* const in = static_cast<const Sample*>(source);
    Sample* const out = static_cast<Sample*>(destination);

    if (sourceStrideSamples == 1u && destinationStrideSamples == 1u)
    {
        if (count > 0u)
        {
            std::memcpy(out, in, static_cast<std::size_t>(count) * sizeof(Sample));
        }
        return;
    }
    if (sourceStrideSamples == 1u)
    {
        for (std::uint64_t index = 0u; index < count; ++index)
        {
            out[index * destinationStrideSamples] = in[index];
        }
        return;
    }
    if (destinationStrideSamples == 1u)
    {
        for (std::uint64_t index = 0u; index < count; ++index)
        {
            out[index] = in[index * sourceStrideSamples];
        }
        return;
    }
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        out[index * destinationStrideSamples] = in[index * sourceStrideSamples];
    }
}

/// One row of the conversion table: one source representation against every destination.
///
/// @tparam SourceI which source representation, as an index into the enumeration.
/// @tparam DestI the destination indices, which are always every one of them.
/// @return the row.
template <std::size_t SourceI, std::size_t... DestI>
[[nodiscard]] constexpr std::array<ConvertRunFn, sizeof...(DestI)> convertRowOf(std::index_sequence<DestI...>) noexcept
{
    return std::array<ConvertRunFn, sizeof...(DestI)>{
        &convertRunCpuOf<static_cast<SampleType>(SourceI), static_cast<SampleType>(DestI)>...};
}

/// The whole conversion table.
///
/// @tparam SourceI the source indices, which are always every one of them.
/// @return every ordered pair, indexed by source and then by destination.
template <std::size_t... SourceI>
[[nodiscard]] constexpr std::array<std::array<ConvertRunFn, kSampleTypeCount>, sizeof...(SourceI)>
convertTableOf(std::index_sequence<SourceI...>) noexcept
{
    return std::array<std::array<ConvertRunFn, kSampleTypeCount>, sizeof...(SourceI)>{
        convertRowOf<SourceI>(std::make_index_sequence<kSampleTypeCount>{})...};
}

/// One entry per representation, for a kernel family that has one.
///
/// @tparam MakeT what to build for each representation.
/// @tparam SampleI the representation indices, which are always every one of them.
/// @param make the builder, called once per representation.
/// @return the table.
template <typename MakeT, std::size_t... SampleI>
[[nodiscard]] constexpr auto perSampleTableOf(MakeT make, std::index_sequence<SampleI...>) noexcept
{
    return std::array{make(std::integral_constant<std::size_t, SampleI>{})...};
}

/// Every ordered pair of representations, as compiled conversion kernels.
constexpr auto kConvertTable = convertTableOf(std::make_index_sequence<kSampleTypeCount>{});

/// Every representation, as a compiled strided-copy kernel.
constexpr auto kStrideCopyTable = perSampleTableOf(
    [](auto index) -> StrideCopyRunFn { return &strideCopyRunCpuOf<static_cast<SampleType>(decltype(index)::value)>; },
    std::make_index_sequence<kSampleTypeCount>{});

} // namespace

ConvertRunFn cpuConvertRun(SampleType source, SampleType destination) noexcept
{
    if (!isSampleType(source) || !isSampleType(destination))
    {
        return nullptr;
    }
    return kConvertTable[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)];
}

StrideCopyRunFn cpuStrideCopyRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kStrideCopyTable[static_cast<std::size_t>(sampleType)];
}

} // namespace an
