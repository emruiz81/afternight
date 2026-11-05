// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The interface a format adapter implements to be registered in a `CodecRegistry`.
///
/// An adapter keeps its library's headers, handles and exceptions inside its own translation units;
/// nothing of the library appears in a published header. The registry holds a shared const adapter,
/// so every call must be reentrant across engine contexts and executions: decoder state lives in the
/// call, and whatever configuration the adapter keeps is immutable. Samples and scratch come from the
/// engine context passed in, and memory the library owns travels with a `SampleBuffer` lifetime token.
///
/// The facade in `IO/CodecIO.h` is the only caller. It checks each request against the adapter's
/// capability record, runs the call inside the execution's admitted task arena - so nested parallel
/// work inherits the operation's thread ceiling - and turns an exception from any callback,
/// `capabilities` included, into a returned failure. An adapter doing long work checks the
/// execution's cancellation and reports through its sinks.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecRequests.h>

namespace an
{
class EngineContext;
class ExecutionContext;

/// A format adapter: a probe, and optionally a decoder, a region decoder and an encoder, each of which
/// refuses by default.
class AFTERNIGHTENGINE_INTERFACE ImageCodec
{
public:
    /// Releases the adapter's configuration once its last owner lets it go; must not throw.
    virtual ~ImageCodec() = default;

    /// What the adapter can do, read once at registration and without touching a file.
    /// @return the capability record; the registry validates and copies it.
    [[nodiscard]] virtual CodecCapabilities capabilities() const = 0;

    /// Reads and checks the selected image's header without decoding its pixels.
    ///
    /// The disposition returned states the validation actually performed, and a request whose
    /// `validatePayload` is false must not validate the payload. A header that cannot be read is
    /// `CodecFailure::MetadataUnreadable` and a payload that fails validation is
    /// `CodecFailure::PayloadUnreadable`, so a caller can tell the stages apart.
    ///
    /// @param engine the engine context that owns the operation's resources.
    /// @param execution an execution of that context: its cancellation, budget, concurrency and sinks.
    /// @param request the source selection, borrowed for the call.
    /// @return the description, layout, image count and metadata; or a typed failure.
    [[nodiscard]] virtual Result<CodecImageInfo> probe(const EngineContext& engine, const ExecutionContext& execution,
                                                       const CodecReadRequest& request) const = 0;

    /// Decodes the whole selected image as the container stores it, with no conversion and no guessed
    /// layout; the facade checks the frame against the probe.
    /// @param engine the engine context that owns the decoded samples.
    /// @param execution an execution of that context.
    /// @param request the source selection.
    /// @return the frame and its source metadata, or a typed failure. The default implementation
    ///         returns `ErrorCode::Unsupported`.
    [[nodiscard]] virtual AFTERNIGHTENGINE_API Result<CodecDecodeResult>
    decode(const EngineContext& engine, const ExecutionContext& execution, const CodecReadRequest& request) const;

    /// Decodes exactly one rectangle, keeping the source's meaning and shifting a filter array's phase
    /// by the region's offset.
    ///
    /// Must read the region natively - decoding the whole image and cropping it is what the facade
    /// refuses to hide. The returned layout may carry padding.
    ///
    /// @param engine the engine context that owns the decoded samples.
    /// @param execution an execution of that context.
    /// @param request the source selection.
    /// @param region the rectangle, already checked against the image, in the source's row order.
    /// @return the region's frame and source metadata, or a typed failure. The default implementation
    ///         returns `ErrorCode::Unsupported`.
    [[nodiscard]] virtual AFTERNIGHTENGINE_API Result<CodecDecodeResult> decodeRegion(const EngineContext& engine,
                                                                                      const ExecutionContext& execution,
                                                                                      const CodecReadRequest& request,
                                                                                      const CodecRegion& region) const;

    /// Writes already-prepared samples to the request's private pending path, and never publishes.
    ///
    /// An option the adapter cannot honour is refused before the pending file is opened or truncated.
    ///
    /// @param engine the engine context that owns the encoder's workspaces.
    /// @param execution an execution of that context.
    /// @param request the pending path and the output intent, borrowed for the call.
    /// @param frame the complete samples, borrowed and not modified.
    /// @param metadata the source facts; the output intent is `request.intent` alone.
    /// @return the number of bytes written, or a typed failure. The default implementation returns
    ///         `ErrorCode::Unsupported`.
    [[nodiscard]] virtual AFTERNIGHTENGINE_API Result<CodecEncodeReport>
    encode(const EngineContext& engine, const ExecutionContext& execution, const CodecEncodeRequest& request,
           const RawFrame& frame, const ImageMetadata& metadata) const;
};
} // namespace an
