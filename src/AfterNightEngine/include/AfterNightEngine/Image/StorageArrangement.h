// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The word for how a layout arranges an image's samples.
///
/// `Core/ImageLayout.h` deliberately refuses to name its own arrangement. It holds how many of a
/// pixel's samples share a plane and how many planes there are, because that is the arithmetic every
/// offset is computed from, and one plane per channel and one plane holding every channel are the
/// same arithmetic with different numbers. Naming them is a question about the image model rather
/// than about addresses, so it is answered here.
///
/// The name is derived, never stored. A descriptor that carried the arrangement as a field beside a
/// layout that implies it would have two answers and no rule about which wins, and the one that
/// disagreed would be the one a kernel read.
///
/// The other direction lives here too, and it is the only place in this engine where a description
/// of an image produces a description of memory. `layoutRequestFor` takes what an image is, plus the
/// one thing the description deliberately does not say - how its samples are to be spread across
/// planes - and states the geometry that follows. That it is the inverse of `storageArrangementOf` -
/// exactly, except that a single-channel image is always named planar - is a property worth being
/// able to test, which is why the two are in one header rather than one each.

#pragma once

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <cstdint>
#include <optional>

namespace an
{

/// How an image's samples are spread across its planes.
enum class StorageArrangement : std::uint8_t
{
    /// One plane per channel: every sample in a plane belongs to the same channel. The arrangement
    /// most of this engine's processing reads, because a channel is then a contiguous run.
    Planar,

    /// One plane holding every channel: a pixel's samples are adjacent. What most display buffers
    /// and several codecs hand over.
    Interleaved,

    /// Neither: more than one plane, each holding more than one of a pixel's samples. A luminance
    /// plane beside an interleaved chroma plane is the arrangement that produces this.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SemiPlanar
};

/// Whether a value is one of the arrangements this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param arrangement the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStorageArrangement(StorageArrangement arrangement) noexcept
{
    return static_cast<std::uint8_t>(arrangement) <= static_cast<std::uint8_t>(StorageArrangement::SemiPlanar);
}

/// What to call the arrangement this layout describes.
///
/// A single-channel image satisfies both the planar and the interleaved definition - one channel in
/// one plane is the same bytes either way - and is reported planar. That is a choice, and the reason
/// it is this one is that every multi-channel image in this engine which is not interleaved is
/// planar, so a caller comparing arrangements either side of a channel-count change sees no
/// difference that is not there.
///
/// Padding does not enter into it. A layout with padded rows or planes arranges its samples exactly
/// as the same layout without padding does; whether the samples are contiguous is a separate
/// question the layout already answers.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param layout the validated layout to name.
/// @return `Planar` when each plane holds one sample of a pixel; `Interleaved` when one plane holds
///         every channel of a multi-channel image; `SemiPlanar` otherwise. The layout's own
///         validation already proved that samples per pixel times plane count is exactly the
///         channel count, so there is no fourth answer.
[[nodiscard]] constexpr StorageArrangement storageArrangementOf(const ImageLayout& layout) noexcept
{
    if (layout.samplesPerPixel() == 1u)
    {
        return StorageArrangement::Planar;
    }
    if (layout.planeCount() == 1u)
    {
        return StorageArrangement::Interleaved;
    }
    return StorageArrangement::SemiPlanar;
}

/// The arrangement's own name, for a log line, a diagnostic field or a test failure message.
///
/// The spelling of the enumerator: `StorageArrangement::SemiPlanar` is `"SemiPlanar"`.
///
/// Thread-safe and allocation-free.
///
/// @param arrangement the arrangement to name. A value outside the enumeration is named
///        `"UnknownStorageArrangement"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* storageArrangementName(StorageArrangement arrangement) noexcept;

/// The memory geometry an image of this description needs, arranged this way.
///
/// The descriptor says what the image is and says nothing about where its samples go, which is the
/// split that lets one image be stored two ways. This is where the missing half is supplied: name
/// the arrangement, and everything else follows from the description - the element size from the
/// sample type, the plane count and the samples per plane from the channel count and the
/// arrangement, the extent unchanged.
///
/// The result is a request rather than a layout, so the strides and the total are still derived and
/// checked by `ImageLayout` against the limits in force. Nothing here decides whether an image that
/// size is admissible; this decides only what shape it would have.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param descriptor what the image is.
/// @param arrangement how its samples are to be spread across planes.
/// @param rowAlignmentBytes the boundary every row and plane is to start on. The default is what a
///        vector kernel needs; 1 asks for no padding at all.
/// @return the request, or an empty optional for an arrangement outside the enumeration and for
///         `StorageArrangement::SemiPlanar`. The semi-planar case is refused rather than guessed:
///         a luminance plane beside an interleaved chroma plane and a three-channel image split
///         one-and-two are both semi-planar, the channel count does not say which, and a
///         descriptor has nowhere to state the split. An image arranged that way is built from a
///         layout stated in full.
[[nodiscard]] constexpr std::optional<ImageLayoutRequest>
layoutRequestFor(const ImageDescriptor& descriptor, StorageArrangement arrangement,
                 std::uint64_t rowAlignmentBytes = kDefaultBufferAlignmentBytes) noexcept
{
    const std::uint32_t channelCount = descriptor.extent().channelCount();
    std::uint32_t samplesPerPixel = 0u;
    std::uint32_t planeCount = 0u;
    switch (arrangement)
    {
    case StorageArrangement::Planar:
        samplesPerPixel = 1u;
        planeCount = channelCount;
        break;
    case StorageArrangement::Interleaved:
        samplesPerPixel = channelCount;
        planeCount = 1u;
        break;
    case StorageArrangement::SemiPlanar:
        return std::nullopt;
    }
    if (planeCount == 0u)
    {
        return std::nullopt;
    }
    return ImageLayoutRequest{.extent = descriptor.extent(),
                              .elementSizeBytes = sampleTypeSizeBytes(descriptor.sampleType()),
                              .samplesPerPixel = samplesPerPixel,
                              .planeCount = planeCount,
                              .rowAlignmentBytes = rowAlignmentBytes};
}

/// Whether a layout and a description are about the same image.
///
/// A layout and a descriptor overlap in exactly two facts, and this is both of them: how many
/// pixels and channels there are, and how many bytes one sample takes. Everything else each of them
/// carries is its own - the strides and the arrangement belong to the layout, the colour basis and
/// the numeric domain to the descriptor - so agreeing on these two is the whole of what it means for
/// the pair to describe one image.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param descriptor what the image is.
/// @param layout where its samples sit.
/// @return true when the extents are equal and the layout's element size is the sample type's.
[[nodiscard]] constexpr bool layoutDescribes(const ImageDescriptor& descriptor, const ImageLayout& layout) noexcept
{
    return layout.extent() == descriptor.extent() &&
           layout.elementSizeBytes() == sampleTypeSizeBytes(descriptor.sampleType());
}

} // namespace an
