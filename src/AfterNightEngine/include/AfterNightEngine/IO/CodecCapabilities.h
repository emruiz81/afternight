// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a codec adapter says it can do, as a small record the registry checks once.
///
/// An adapter's `CodecCapabilities` is read when the adapter is registered, validated with
/// `validCodecCapabilities` and copied; the codec facade then refuses a request the record does not
/// cover before the adapter is called. Sample representations and storage arrangements are bit sets
/// built from `codecSampleBit` and `codecLayoutBit`.
///
/// The sets state which representations are possible, not which combinations of an adapter's own
/// options work, so an adapter still refuses an unsupported combination - of compression, scaling or
/// channels - itself, with `ErrorCode::Unsupported`, before it writes anything.

#pragma once

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <cstdint>

namespace an
{
/// The bit a raw sample representation occupies in a capability record's sample sets.
///
/// Every one of the twenty class and width pairs `RawSampleFormat` can state has a bit, including the
/// ones that are not resident sample types. Allocation-free and thread-safe.
///
/// @param format the raw representation.
/// @return one bit, or zero when `format` is not a valid class and width.
[[nodiscard]] constexpr std::uint32_t codecSampleBit(RawSampleFormat format) noexcept
{
    if (!isRawSampleFormat(format))
        return 0u;
    std::uint32_t width = 0u;
    for (auto size = format.sizeBytes; size > 1u; size >>= 1u)
        ++width;
    return 1u << (static_cast<std::uint32_t>(format.sampleClass) * 5u + width);
}

/// The bit a storage arrangement occupies in a capability record's layout sets.
///
/// Only the arrangement is a capability; strides and padding are described by the frame's
/// `ImageLayout`. Allocation-free and thread-safe.
///
/// @param arrangement the planar, interleaved or semi-planar arrangement.
/// @return one bit, or zero for a value outside the enumeration.
[[nodiscard]] constexpr std::uint8_t codecLayoutBit(StorageArrangement arrangement) noexcept
{
    return isStorageArrangement(arrangement) ? static_cast<std::uint8_t>(1u << static_cast<unsigned>(arrangement)) : 0u;
}

/// One adapter's structural capabilities, returned by `ImageCodec::capabilities` and copied by the
/// registry at registration.
///
/// A nonzero sample set enables its operation; every registered adapter can probe, including one that
/// advertises neither a decoder nor an encoder.
struct CodecCapabilities
{
    CodecFormat format = CodecFormat::Unknown; ///< The format served; `Unknown` is refused at registration.
    /// The representations decoded, as an OR of `codecSampleBit` values; zero means no decoder, whole
    /// or region.
    std::uint32_t decodeSamples = 0u;
    std::uint32_t encodeSamples = 0u; ///< The representations encoded, likewise; zero means no encoder.
    /// The arrangements a decoded frame may have, as an OR of `codecLayoutBit` values; all three by
    /// default.
    std::uint8_t decodeLayouts = 7u;
    std::uint8_t encodeLayouts = 7u; ///< The arrangements the encoder accepts in the frame it is given.
    /// The most channels a probed, decoded or encoded frame may have; positive. The engine context's
    /// limits may be lower.
    std::uint32_t maxChannels = 4u;
    /// Whether the adapter decodes a region natively; requires a decoder. A missing region reader is
    /// never replaced by a whole-frame decode and crop.
    bool regionDecode = false;
};

/// Whether a capability record is coherent enough to register.
///
/// Allocation-free and thread-safe.
///
/// @param capabilities the proposed record.
/// @return false when the format is `Unknown` or outside the enumeration, a sample set holds a bit no
///         representation owns, a layout set is empty or holds a bit no arrangement owns,
///         `maxChannels` is zero, or `regionDecode` is set without a decoder; true otherwise.
[[nodiscard]] AFTERNIGHTENGINE_API bool validCodecCapabilities(const CodecCapabilities& capabilities) noexcept;
} // namespace an
