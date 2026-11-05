// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the engine's own adapters' way of keeping one open source from the header
// check to the decode.
//
// Through the public interface a decode opens its file twice: once to probe, so the facade can check
// the header before allocating, and once to decode. An adapter that implements
// `PreparingImageCodec` opens it once: the facade checks the prepared description and then decodes
// from the same handle. The extension is private, so a public adapter keeps the ordinary probe and
// decode path and the published interface does not change.

#pragma once

#include <AfterNightEngine/IO/ImageCodec.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace an
{
/// Operation-local input state that keeps header validation and decoding on the same open source.
/// The facade owns it until decoding finishes, including cancellation and every failure return.
class PreparedCodecRead
{
public:
    virtual ~PreparedCodecRead() = default;
    /// Geometry and source meaning available before pixel allocation; borrowed until decode starts.
    virtual const CodecImageInfo& info() const noexcept = 0;
    /// Consumes this preparation once, after the facade has checked geometry and representation.
    /// @param engine the engine context that owns the decoded samples.
    /// @param execution the operation the read belongs to.
    /// @param request the same source selection the preparation was made from.
    /// @param region the rectangle to decode, or null for the whole image.
    /// @return the frame and its source metadata, or a typed failure.
    virtual Result<CodecDecodeResult> decode(const EngineContext& engine, const ExecutionContext& execution,
                                             const CodecReadRequest& request, const CodecRegion* region) = 0;
    /// Whether this preparation decodes whole rows into caller memory, as often as it is asked.
    /// @return false unless the adapter implements `decodeRowsInto`.
    virtual bool decodesRowsInto() const noexcept
    {
        return false;
    }
    /// How many rows the smallest piece of the prepared image that `decodeRowsInto` decodes whole spans.
    /// @return at least one, for a payload read as it is stored; the image's height when every band decodes all of it.
    virtual std::uint32_t decodeGranularityRows() const noexcept
    {
        return 1u;
    }
    /// Decodes whole rows of the prepared image into caller memory and stays ready for more rows.
    ///
    /// Called only when `decodesRowsInto` is true, after the facade has checked the rows against the
    /// image and `destination` against `layout`: planar or interleaved as the adapter decodes, every
    /// channel, rows of exactly the width. Never after `decode`, which consumes the preparation.
    /// @param engine the engine context the image was prepared in.
    /// @param execution the operation the read belongs to.
    /// @param request the same source selection the preparation was made from.
    /// @param firstRow the first row, inside the image.
    /// @param rows how many rows, at least one and inside the image.
    /// @param destination the caller's memory, at least `layout.totalBytes()` long.
    /// @param layout where each decoded sample goes.
    /// @return success, or a typed failure after which `destination` holds nothing to rely on.
    virtual Status decodeRowsInto(const EngineContext& engine, const ExecutionContext& execution,
                                  const CodecReadRequest& request, std::uint32_t firstRow, std::uint32_t rows,
                                  std::span<std::byte> destination, const ImageLayout& layout)
    {
        static_cast<void>(engine);
        static_cast<void>(execution);
        static_cast<void>(request);
        static_cast<void>(firstRow);
        static_cast<void>(rows);
        static_cast<void>(destination);
        static_cast<void>(layout);
        return Status::failure(ErrorCode::Unsupported);
    }
};

/// Optional internal extension for adapters whose input state can span preflight and decode.
/// Public ImageCodec implementations retain the ordinary probe/decode path and ABI.
class PreparingImageCodec : public ImageCodec
{
public:
    /// Opens one input and describes it without allocating pixels or validating the full payload.
    /// A null preparation is refused by the facade as `CodecFailure::DescriptorMismatch`.
    virtual Result<std::unique_ptr<PreparedCodecRead>> prepareRead(const EngineContext& engine,
                                                                   const ExecutionContext& execution,
                                                                   const CodecReadRequest& request) const = 0;
};
} // namespace an
