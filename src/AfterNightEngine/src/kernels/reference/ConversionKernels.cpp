// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/ConversionKernels.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

// Sixty-four conversion kernels, built from one body and looked up in a table rather than reached
// through a switch.
//
// The table is assembled at compile time from the two enumerations, which is what makes it total:
// there is no pair a reader has to check was written down, because every pair is an index. A switch
// over eight cases nested in a switch over eight is the same code and puts the completeness of the
// matrix in a reader's hands.
//
// The bodies are deliberately the public `convertSample` template and nothing else. A kernel that
// re-derived the arithmetic would be a second implementation of the conversion rules, and the whole
// value of a reference is that there is exactly one.

namespace an
{
namespace
{

/// Converts one run of samples of one ordered pair, by the rules and nothing faster.
///
/// @tparam SourceV what the samples are.
/// @tparam DestV what they are to become.
/// @param source where the samples to convert start.
/// @param destination where the converted samples go.
/// @param count how many samples.
/// @param policy the five decisions.
/// @param counters what the run met, added to whatever they already hold.
/// @return where the kernel stopped, or a default-constructed outcome when it converted everything.
template <SampleType SourceV, SampleType DestV>
[[nodiscard]] ConversionRunOutcome convertRunOf(const void* source, void* destination, std::uint64_t count,
                                                const ConversionPolicy& policy, ConversionCounters& counters) noexcept
{
    using Source = SampleOf<SourceV>;
    using Dest = SampleOf<DestV>;
    const Source* const in = static_cast<const Source*>(source);
    Dest* const out = static_cast<Dest*>(destination);

    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const ConvertedSample<Dest> converted = convertSample<Source, Dest>(in[index], policy);
        if (converted.sourceWasNonFinite)
        {
            ++counters.nonFiniteInputs;
            if (policy.nonFinite == NonFinitePolicy::Reject)
            {
                return ConversionRunOutcome{.rejectedIndex = index, .rejected = true, .rejectedForNonFinite = true};
            }
        }
        if (converted.saturated)
        {
            ++counters.saturatedSamples;
            if (policy.range == RangePolicy::Reject)
            {
                return ConversionRunOutcome{.rejectedIndex = index, .rejected = true, .rejectedForNonFinite = false};
            }
        }
        out[index] = converted.value;
        ++counters.samplesConverted;
    }
    return ConversionRunOutcome{};
}

/// Copies one run of samples of one representation between two strides.
///
/// @tparam SampleV what the samples are.
/// @param source where the samples to copy start.
/// @param sourceStrideSamples how many samples from one source sample to the next.
/// @param destination where the copies go.
/// @param destinationStrideSamples how many samples from one destination sample to the next.
/// @param count how many samples.
template <SampleType SampleV>
void strideCopyRunOf(const void* source, std::uint64_t sourceStrideSamples, void* destination,
                     std::uint64_t destinationStrideSamples, std::uint64_t count) noexcept
{
    using Sample = SampleOf<SampleV>;
    const Sample* const in = static_cast<const Sample*>(source);
    Sample* const out = static_cast<Sample*>(destination);
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        out[index * destinationStrideSamples] = in[index * sourceStrideSamples];
    }
}

/// Reverses the bytes of one run of samples of one width, in place.
///
/// @tparam SampleV what the samples are, which is where the width comes from.
/// @param samples where the run starts.
/// @param count how many samples.
template <SampleType SampleV> void byteSwapRunOf(void* samples, std::uint64_t count) noexcept
{
    constexpr std::size_t kWidth = sizeof(SampleOf<SampleV>);
    std::uint8_t* const bytes = static_cast<std::uint8_t*>(samples);
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        std::uint8_t* const sample = bytes + index * kWidth;
        for (std::size_t low = 0u; low < kWidth / 2u; ++low)
        {
            const std::uint8_t held = sample[low];
            sample[low] = sample[kWidth - 1u - low];
            sample[kWidth - 1u - low] = held;
        }
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
        &convertRunOf<static_cast<SampleType>(SourceI), static_cast<SampleType>(DestI)>...};
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

/// Every ordered pair of representations, as conversion kernels.
constexpr auto kConvertTable = convertTableOf(std::make_index_sequence<kSampleTypeCount>{});

/// Every representation, as a strided-copy kernel.
constexpr auto kStrideCopyTable = perSampleTableOf(
    [](auto index) -> StrideCopyRunFn { return &strideCopyRunOf<static_cast<SampleType>(decltype(index)::value)>; },
    std::make_index_sequence<kSampleTypeCount>{});

/// Every representation, as a byte-swap kernel. The one-byte representations have none, because one
/// byte has one order.
constexpr auto kByteSwapTable = perSampleTableOf(
    [](auto index) -> ByteSwapRunFn {
        constexpr SampleType kRepresentation = static_cast<SampleType>(decltype(index)::value);
        if constexpr (sizeof(SampleOf<kRepresentation>) > 1u)
        {
            return &byteSwapRunOf<kRepresentation>;
        }
        else
        {
            return nullptr;
        }
    },
    std::make_index_sequence<kSampleTypeCount>{});

} // namespace

ConvertRunFn referenceConvertRun(SampleType source, SampleType destination) noexcept
{
    if (!isSampleType(source) || !isSampleType(destination))
    {
        return nullptr;
    }
    return kConvertTable[static_cast<std::size_t>(source)][static_cast<std::size_t>(destination)];
}

StrideCopyRunFn referenceStrideCopyRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kStrideCopyTable[static_cast<std::size_t>(sampleType)];
}

ByteSwapRunFn referenceByteSwapRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kByteSwapTable[static_cast<std::size_t>(sampleType)];
}

} // namespace an
