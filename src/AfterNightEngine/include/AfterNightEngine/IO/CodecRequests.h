// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The values the codec facade and its adapters exchange: what to read, which region, what a probe
/// established, what a decode returns, and what to write where.
///
/// A request borrows its path for one synchronous call and copies nothing. A decoded frame either owns
/// its samples in the calling engine context or carries the lifetime token that keeps an adapter's
/// memory alive; the facade refuses a bare borrow. Source metadata never carries an output intent -
/// only an encode request states one - so a fact about the source cannot silently become a decision
/// about the output.

#pragma once

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/RawDecoderPreference.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace an
{
/// Which image to read, and from where; borrowed for one call.
///
/// A stated format is used as stated: the facade never falls back to the extension, to the content or
/// to another adapter.
struct CodecReadRequest
{
    /// The source path: nonempty UTF-8 with no NUL, at most 131072 bytes, borrowed for the call.
    std::string_view path;
    CodecFormat format = CodecFormat::Unknown; ///< The format to read; `Unknown` resolves the path's extension.
    /// The zero-based image in a container that holds several; one past the last is
    /// `ErrorCode::OutOfRange`.
    std::uint32_t imageIndex = 0u;
    /// Whether a probe or decode reads the source's metadata; false asks for the description only.
    bool readMetadata = true;
    /// Whether a probe also validates the payload without decoding it; false asks for the header and
    /// metadata only. An adapter that cannot validate without decoding reports
    /// `CodecProbeDisposition::DecodeRequired` rather than decoding silently.
    bool validatePayload = true;
    /// Overrides `RawCodecOptions::decoderPreference` for this read; no value keeps the adapter's
    /// default, and formats other than RAW ignore it. A value outside the enumeration is
    /// `ErrorCode::InvalidArgument` before the file is opened. The choice is copied when the read is
    /// prepared, so a caller changes it between reads - `NativeLibRaw` to turn acceleration off,
    /// `Automatic` to turn it back on - without rebuilding the registry or the engine context or
    /// releasing any image, and a read already prepared keeps its choice, including the native retry
    /// `Automatic` may make. Any application-wide setting copied here is the caller's to own and
    /// synchronize.
    std::optional<RawDecoderPreference> rawDecoderPreference;
};

/// A nonempty rectangle of an image, in the source descriptor's row order, keeping every channel.
///
/// Coordinates are zero-based and the rectangle is half-open; nothing flips it onto a display axis. A
/// region that is empty or reaches past the image is `ErrorCode::OutOfRange`.
struct CodecRegion
{
    std::uint32_t x = 0u; ///< The first column.
    std::uint32_t y = 0u; ///< The first row, counted in the source descriptor's row order.
    std::uint32_t width = 0u; ///< The number of columns; positive.
    std::uint32_t height = 0u; ///< The number of rows; positive.
};

/// How much a successful probe established, without allocating any pixel storage.
enum class CodecProbeDisposition : std::uint8_t
{
    /// The header and metadata are readable; only a decode can show that the payload is.
    DecodeRequired,
    PayloadValidatedWithoutDecode ///< The selected image's payload was also validated, without decoding it.
};

/// What a successful probe established about a source, before any pixel storage exists.
///
/// A failed probe tells its two stages apart by reason: `CodecFailure::MetadataUnreadable` when the
/// header could not be read, `CodecFailure::PayloadUnreadable` when the payload failed validation.
/// After a payload failure, a header-only probe can still read the source metadata.
struct CodecImageInfo
{
    RawFrameDescriptor descriptor; ///< The selected image's meaning and representation, as a decode returns it.
    ImageLayout decodedLayout; ///< The whole image's exact decoded layout, padding and byte strides included.
    std::uint64_t imageCount = 1u; ///< The number of selectable images in the container; zero is refused.
    ImageMetadata metadata{}; ///< The source facts; an output intent here is refused.
    /// The validation actually performed.
    CodecProbeDisposition disposition = CodecProbeDisposition::DecodeRequired;
};

/// A complete decoded image; no partial frame crosses the facade on failure or cancellation.
struct CodecDecodeResult
{
    /// The samples, owned by the engine context or held by a mapping's or decoder's lifetime token; a
    /// bare borrow is refused.
    RawFrame frame;
    ImageMetadata metadata{}; ///< The source facts; the output intent is always absent.
};

/// What to encode where: a private pending path the caller owns, never the final destination.
///
/// A failed encode may leave partial bytes at the pending path. Removing them, closing, flushing,
/// renaming and publishing the artifact belong to the output transaction, and an encoder never
/// modifies the samples it is given.
struct CodecEncodeRequest
{
    /// The private pending path: nonempty UTF-8 with no NUL, at most 131072 bytes, borrowed for the call.
    std::string_view pendingPath;
    /// The format to write; state it when the pending name's suffix does not name the format.
    CodecFormat format = CodecFormat::Unknown;
    /// The encoding decisions, borrowed for the call; the source metadata's own intent is never used.
    /// Null asks for the frame's representation to be preserved as it is, integer representations
    /// that are not resident sample types included. An intent must already state the frame's sample
    /// type and byte order: the facade converts nothing and applies no data-dependent normalization.
    const OutputIntent* intent = nullptr;
};

/// What an encode wrote to its private pending path; it says nothing about a committed artifact.
struct CodecEncodeReport
{
    std::uint64_t bytesWritten = 0u; ///< The complete encoded container's size in bytes.
};
} // namespace an
