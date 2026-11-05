// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "kernels/reference/LayoutKernels.h"

#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

// The authoritative layout bodies, and the one property that makes them an oracle.
//
// Every one of them moves whole samples with `std::memcpy` and never reads a sample through a type.
// That is not caution about aliasing: it is the whole numeric contract of this family. A run of
// `Float32` samples that holds a signalling not-a-number is moved here byte for byte, and any
// implementation that loaded it into a floating register and stored it back would be free to
// return a different bit pattern for the same call. The vector kernels next door keep the property
// by using integer lanes; this file keeps it by never having a lane at all.
//
// They are also deliberately slow. A copy that walked a sample at a time through `memcpy` is the
// clearest statement of what the operation is, and it is the thing every compiled backend in this
// engine is compared against rather than the thing any of them is expected to match in time.

namespace an
{
namespace
{

/// Copies one run one sample at a time.
///
/// @tparam WidthV how many bytes one sample holds.
/// @param source where the samples to copy start.
/// @param destination where the copies go.
/// @param count how many samples.
template <std::size_t WidthV>
void copyRunReferenceOf(const void* source, void* destination, std::uint64_t count) noexcept
{
    const std::uint8_t* const in = static_cast<const std::uint8_t*>(source);
    std::uint8_t* const out = static_cast<std::uint8_t*>(destination);
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        std::memcpy(out + index * WidthV, in + index * WidthV, WidthV);
    }
}

/// Writes one sample value over a whole run, one sample at a time.
///
/// @tparam WidthV how many bytes one sample holds.
/// @param value the sample to repeat.
/// @param destination where the run starts.
/// @param count how many samples.
template <std::size_t WidthV>
void fillRunReferenceOf(const void* value, void* destination, std::uint64_t count) noexcept
{
    const std::uint8_t* const pattern = static_cast<const std::uint8_t*>(value);
    std::uint8_t* const out = static_cast<std::uint8_t*>(destination);
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        std::memcpy(out + index * WidthV, pattern, WidthV);
    }
}

/// Splits one interleaved run into its channels, in pixel order.
///
/// @tparam WidthV how many bytes one sample holds.
/// @param source where the interleaved samples start.
/// @param destinations one contiguous run per channel.
/// @param channelCount how many samples one pixel holds.
/// @param pixelCount how many pixels.
template <std::size_t WidthV>
void deinterleaveRunReferenceOf(const void* source, void* const* destinations, std::uint32_t channelCount,
                                std::uint64_t pixelCount) noexcept
{
    const std::uint8_t* const in = static_cast<const std::uint8_t*>(source);
    for (std::uint64_t pixel = 0u; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            std::uint8_t* const out = static_cast<std::uint8_t*>(destinations[channel]);
            std::memcpy(out + pixel * WidthV, in + (pixel * channelCount + channel) * WidthV, WidthV);
        }
    }
}

/// Joins one contiguous run per channel into one interleaved run, in pixel order.
///
/// @tparam WidthV how many bytes one sample holds.
/// @param sources one contiguous run per channel.
/// @param destination where the interleaved samples go.
/// @param channelCount how many samples one pixel holds.
/// @param pixelCount how many pixels.
template <std::size_t WidthV>
void interleaveRunReferenceOf(const void* const* sources, void* destination, std::uint32_t channelCount,
                              std::uint64_t pixelCount) noexcept
{
    std::uint8_t* const out = static_cast<std::uint8_t*>(destination);
    for (std::uint64_t pixel = 0u; pixel < pixelCount; ++pixel)
    {
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            const std::uint8_t* const in = static_cast<const std::uint8_t*>(sources[channel]);
            std::memcpy(out + (pixel * channelCount + channel) * WidthV, in + pixel * WidthV, WidthV);
        }
    }
}

/// One entry per representation, built from the width that representation has.
///
/// The eight representations collapse onto four bodies here rather than at every call site, which
/// is what keeps a layout kernel from being instantiated twice for two names of the same width.
///
/// @tparam MakeT what to build for one width.
/// @tparam SampleI the representation indices, which are always every one of them.
/// @param make the builder, called once per representation.
/// @return the table.
template <typename MakeT, std::size_t... SampleI>
[[nodiscard]] constexpr auto perWidthTableOf(MakeT make, std::index_sequence<SampleI...>) noexcept
{
    return std::array{make(std::integral_constant<std::size_t, SampleI>{})...};
}

/// The byte width of one representation, as a constant expression.
///
/// @param sampleType the representation.
/// @return how many bytes one of its samples holds.
[[nodiscard]] constexpr std::size_t widthOf(SampleType sampleType) noexcept
{
    return static_cast<std::size_t>(sampleTypeSizeBytes(sampleType));
}

/// Every representation, as a reference copy kernel.
constexpr auto kCopyTable = perWidthTableOf(
    [](auto index) -> CopyRunFn {
        return &copyRunReferenceOf<widthOf(static_cast<SampleType>(decltype(index)::value))>;
    },
    std::make_index_sequence<kSampleTypeCount>{});

/// Every representation, as a reference fill kernel.
constexpr auto kFillTable = perWidthTableOf(
    [](auto index) -> FillRunFn {
        return &fillRunReferenceOf<widthOf(static_cast<SampleType>(decltype(index)::value))>;
    },
    std::make_index_sequence<kSampleTypeCount>{});

/// Every representation, as a reference channel-splitting kernel.
constexpr auto kDeinterleaveTable = perWidthTableOf(
    [](auto index) -> DeinterleaveRunFn {
        return &deinterleaveRunReferenceOf<widthOf(static_cast<SampleType>(decltype(index)::value))>;
    },
    std::make_index_sequence<kSampleTypeCount>{});

/// Every representation, as a reference channel-joining kernel.
constexpr auto kInterleaveTable = perWidthTableOf(
    [](auto index) -> InterleaveRunFn {
        return &interleaveRunReferenceOf<widthOf(static_cast<SampleType>(decltype(index)::value))>;
    },
    std::make_index_sequence<kSampleTypeCount>{});

} // namespace

CopyRunFn referenceCopyRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kCopyTable[static_cast<std::size_t>(sampleType)];
}

FillRunFn referenceFillRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kFillTable[static_cast<std::size_t>(sampleType)];
}

DeinterleaveRunFn referenceDeinterleaveRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kDeinterleaveTable[static_cast<std::size_t>(sampleType)];
}

InterleaveRunFn referenceInterleaveRun(SampleType sampleType) noexcept
{
    if (!isSampleType(sampleType))
    {
        return nullptr;
    }
    return kInterleaveTable[static_cast<std::size_t>(sampleType)];
}

} // namespace an
