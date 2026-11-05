// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the engine's own encoders' way of writing into a handle the output
// transaction already holds.
//
// Through the public interface an encoder opens the pending path itself, and the transaction then
// reopens the file to inspect, flush and publish it. An encoder that implements
// `StreamingImageCodec` writes through a `CodecOutput` the transaction reserved instead, so the file
// is created, written, size-checked, read back and flushed through one handle before it is closed and
// renamed. The extension is private, so a public adapter keeps the path-based entry point.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace an
{
template <typename T> class Result;
class EngineContext;
class ExecutionContext;
class RawFrame;
class ImageMetadata;
struct CodecEncodeRequest;
struct CodecEncodeReport;
enum class CodecFormat : std::uint8_t;

/// A transaction's exclusively reserved output. Writes complete synchronously and never throw.
/// The encoder borrows it; the transaction inspects the completed bytes and checks close before publication.
class CodecOutput
{
public:
    virtual ~CodecOutput() = default;
    /// Optional known-size allocation hint. A refusal keeps ordinary incremental writes available.
    virtual void reserve(std::uint64_t) noexcept {}
    /// Writes the complete span, or returns false after a short or failed write.
    virtual bool write(std::span<const std::byte> bytes) noexcept = 0;
};

/// Optional internal encoder extension that reuses a transaction's reservation handle.
/// Ordinary public ImageCodec implementations keep their existing path-based entry point.
class StreamingImageCodec
{
public:
    virtual ~StreamingImageCodec() = default;
    /// Writes a complete payload to an empty output, without retaining or closing the borrow.
    virtual Result<CodecEncodeReport> encodeTo(const EngineContext& engine, const ExecutionContext& execution,
                                               const CodecEncodeRequest& request, const RawFrame& frame,
                                               const ImageMetadata& metadata, CodecOutput& output) const = 0;
};

/// Whether this context's registered adapter supports the internal output extension.
[[nodiscard]] bool supportsPreparedCodecWrite(const EngineContext& engine, CodecFormat format) noexcept;

/// The normal encode facade, with the same validation, scheduling and exception boundaries.
/// A non-null output must be empty and reserved exclusively by the calling transaction.
Result<CodecEncodeReport> encodePreparedImage(const EngineContext& engine, const ExecutionContext& execution,
                                              const CodecEncodeRequest& request, const RawFrame& frame,
                                              const ImageMetadata& metadata, CodecOutput* output);
} // namespace an
