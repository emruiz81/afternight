// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the uncompressed AVI container's private vocabulary: where every header field
// sits, how many frames a RIFF section holds, the bytes of the headers and indexes a sink writes, and its
// typed failures.
//
// A section is at most one gibibyte, so every offset inside one fits 32 bits. Each section's standard
// index is based at its own `movi` list, the one AVI 1.0 `idx1` index covers the first section alone, and the
// master index at the front names every standard index - the OpenDML extension's layout, with its
// `odml` list inside `hdrl` after the stream list.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/AviSequence.h>
#include <AfterNightEngine/IO/SequenceError.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

/// The uncompressed AVI sink's layout planning, byte encoders and failure builders.
namespace an::avi_detail
{

/// The most bytes transferred between two cancellation checkpoints. Each transfer is one system call.
inline constexpr std::uint64_t kTransferBytes = 16u * 1024u * 1024u;

/// The bytes a frame chunk's header adds to its samples.
inline constexpr std::uint64_t kChunkHeaderBytes = 8u;

/// The bytes of a standard index before its entries, its chunk header included.
inline constexpr std::uint64_t kStandardIndexHeaderBytes = 32u;

/// The bytes each frame adds to its section's standard index.
inline constexpr std::uint64_t kStandardIndexEntryBytes = 8u;

/// The bytes of the `idx1` index chunk header.
inline constexpr std::uint64_t kIdx1HeaderBytes = 8u;

/// The bytes each frame of the first section adds to the `idx1` index.
inline constexpr std::uint64_t kIdx1EntryBytes = 16u;

/// The bytes that open every section after the first: `RIFF`, its size, `AVIX`, `LIST`, its size, `movi`.
inline constexpr std::uint64_t kExtensionSectionHeaderBytes = 24u;

/// What a request and a descriptor make of a container, decided before a byte is written.
struct Layout
{
    std::uint32_t width = 0u; ///< The frame's width in pixels, at most `kAviMaximumFrameDimension`.
    std::uint32_t height = 0u; ///< The frame's height in pixels, at most `kAviMaximumFrameDimension`.
    std::uint32_t bytesPerPixel = 0u; ///< One for grey through the palette, three for blue, green and red.
    std::uint32_t storedRowBytes = 0u; ///< One row's samples, padded to a multiple of four.
    std::uint64_t frameBytes = 0u; ///< Every stored row of one frame.
    bool rowsArriveBottomUp = false; ///< The frame's origin is its bottom-left corner.
    std::uint32_t rateNumerator = 1u; ///< Frames per second, as the container's rate.
    std::uint32_t rateDenominator = 1u; ///< The container's scale.
    std::uint64_t riffBytes = 0u; ///< The most bytes one section holds.
    std::uint32_t riffCapacity = 0u; ///< Sections the master index has room for.
    std::uint64_t headerBytes = 0u; ///< Every byte before the first frame chunk.
    std::uint64_t firstSectionFrames = 0u; ///< Frames the first section holds.
    std::uint64_t laterSectionFrames = 0u; ///< Frames each later section holds.
    std::uint64_t maximumFrames = 0u; ///< Frames the container holds, at most `UINT32_MAX`.
};

/// What the header states once frames have been written.
struct HeaderCounts
{
    std::uint64_t totalFrames = 0u; ///< Every frame of every section.
    std::uint64_t firstSectionFrames = 0u; ///< Frames of the first section.
    std::uint64_t firstSectionBytes = 0u; ///< The whole first section, its `idx1` index included.
    std::uint32_t sectionsInUse = 0u; ///< Master index entries that name a standard index.
};

/// Judges a request and a descriptor in a fixed order - the request's options, the descriptor's shape,
/// the timing, the container's frame count, the frame's samples, channels, filter array, pixel shape
/// and extent, whether one frame fits a section, and the stated frame count against the master index's
/// room - and decides the layout.
///
/// A request that leaves the master index's room at zero has it derived: from the stated frame count,
/// or the default room when no count is stated. A request option out of range is
/// `ErrorCode::InvalidArgument`. A descriptor that is not a fixed-frame, unseekable AVI description with
/// a nominal rate is `ErrorCode::InvalidArgument` with `SequenceFailure::InvalidDescriptor`, beside
/// whatever `validateSequenceDescriptor` refuses. Per-frame timestamps and a frame the container cannot
/// store are `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding`, and a frame count or
/// frame size beyond what the sections and the master index hold is `ErrorCode::LimitExceeded`.
[[nodiscard]] Result<Layout> planLayout(const SequenceDescriptor& descriptor, const AviSinkRequest& request);

/// The absolute offset of a section's first frame chunk: the end of the header for the first section,
/// and the end of the opening bytes of a later section that starts at `sectionStart`.
[[nodiscard]] constexpr std::uint64_t firstChunkOffset(const Layout& layout, bool firstSection,
                                                       std::uint64_t sectionStart) noexcept
{
    return firstSection ? layout.headerBytes : sectionStart + kExtensionSectionHeaderBytes;
}

/// The bytes one frame occupies in its section, its chunk header included.
[[nodiscard]] constexpr std::uint64_t chunkBytes(const Layout& layout) noexcept
{
    return kChunkHeaderBytes + layout.frameBytes;
}

/// Encodes the header - everything through the first section's `movi` list header - with the counts and
/// the sizes that follow from them. The master index entries are left as they are.
void encodeHeader(std::span<std::byte> header, const Layout& layout, const HeaderCounts& counts) noexcept;

/// Encodes one master index entry inside the header.
void encodeMasterIndexEntry(std::span<std::byte> header, const Layout& layout, std::uint32_t section,
                            std::uint64_t indexOffset, std::uint32_t indexBytes, std::uint32_t frames) noexcept;

/// Encodes the opening bytes of a later section holding `frames` frames and, when it holds any, their
/// standard index. An open section is encoded with zero frames, and its sizes then say so.
void encodeExtensionSectionHeader(std::span<std::byte> target, const Layout& layout, std::uint64_t frames) noexcept;

/// Encodes a frame chunk's header.
void encodeChunkHeader(std::span<std::byte> target, const Layout& layout) noexcept;

/// Encodes a standard index's header for a section holding `frames` frames whose `movi` list is based at
/// `moviOffset`.
void encodeStandardIndexHeader(std::span<std::byte> target, std::uint64_t frames, std::uint64_t moviOffset) noexcept;

/// Encodes standard index entries for a section's frames from its `first` frame on, one entry per eight
/// bytes of `target`; each entry's offset is relative to the section's `movi` list.
void encodeStandardIndexEntries(std::span<std::byte> target, const Layout& layout, std::uint64_t first) noexcept;

/// Encodes the `idx1` index chunk header for `frames` frames.
void encodeIdx1Header(std::span<std::byte> target, std::uint64_t frames) noexcept;

/// Encodes `idx1` index entries for the first section's frames from its `first` frame on, one entry per
/// sixteen bytes of `target`; each entry's offset is relative to the first section's `movi` list.
void encodeIdx1Entries(std::span<std::byte> target, const Layout& layout, std::uint64_t first) noexcept;

/// A refusal carrying its sequence reason and whichever of the parameter, the path and further fields apply.
[[nodiscard]] Status aviFailure(ErrorCode code, SequenceFailure reason, const char* message, std::string_view parameter,
                                std::string_view path, std::vector<StatusContextField> extra = {});

/// An `ErrorCode::InvalidArgument` refusal naming the parameter, with no sequence reason.
[[nodiscard]] Status argumentFailure(const char* message, const char* parameter);

/// An operating-system refusal: `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure`, carrying
/// the platform's error code and the path.
[[nodiscard]] Status ioFailure(int platformError, const char* message, std::string_view path);

} // namespace an::avi_detail
