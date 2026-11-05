// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what a decoded FFmpeg frame becomes: the decision, made once per stream when
// a source opens, of how its samples are described, rearranged or converted, and the per-frame work
// that carries the decision out.

#pragma once

#include <AfterNightEngine/IO/FfmpegSequence.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/sequences/ffmpeg/FfmpegLibrary.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace an::ffmpeg_detail
{

/// How every frame of one stream is delivered.
struct DeliveryPlan
{
    AVPixelFormat stored = AV_PIX_FMT_NONE; ///< The pixel format the decoder produces.
    std::uint32_t width = 0u; ///< The stream's frame width in pixels, as its parameters state it.
    std::uint32_t height = 0u; ///< The stream's frame height in pixels, as its parameters state it.
    FfmpegConversion conversion = FfmpegConversion::None; ///< Which of the delivery paths every frame takes.
    std::optional<RawFrameDescriptor> frame; ///< Every delivered frame's description.
    std::optional<ImageLayout> layout; ///< Where a delivered frame's samples sit in engine memory.
    std::optional<ImageLayout> allocation; ///< A converted frame's allocation: the layout plus a spare row.
    std::array<std::uint32_t, 4> sourcePlane{0u, 1u, 2u, 3u}; ///< The stored plane each delivered plane copies.
    bool exchangeRedAndBlue = false; ///< Packed samples are stored blue first.
    bool shareable = false; ///< The decoder's buffer can be described in place.
    std::uint32_t storedBits = 8u; ///< The widest stored component.
    int scaleTarget = -1; ///< The scaler's output pixel format for a conversion, or -1 when nothing is scaled.
    int scaleMatrix = -1; ///< The scaler's YCbCr matrix, or -1 for none.
    int sourceFullRange = 0; ///< Whether the scaler reads full-range YCbCr.
    std::uint8_t conversionMatrix = 2u; ///< H.273 code of the matrix a conversion applies.
    FfmpegColorRange conversionRange = FfmpegColorRange::Unspecified; ///< Range a conversion reads.
    SourceEncoding source{}; ///< The stored sample width and byte order.
};

/// Decides how a stream's frames are delivered, refusing what the options do not admit, in a fixed
/// order: the pixel format, then the matrix, then the range, then the limits, then the stated meaning.
///
/// A stream that states no pixel format or frame size, or whose frames have no consistent description,
/// is `ErrorCode::InvalidData` with `SequenceFailure::MalformedContainer`. A pixel format, matrix or
/// range the options do not deliver is `ErrorCode::Unsupported` with
/// `SequenceFailure::UnsupportedEncoding`. Frames beyond the engine's limits are
/// `ErrorCode::LimitExceeded`, and a stated colour space or numeric domain that cannot describe the
/// stream's samples is `ErrorCode::IncompatibleArguments`, both with
/// `SequenceFailure::DescriptorMismatch`.
///
/// @param parameters the stream's codec parameters.
/// @param options the source's options, which decide what is converted and what is assumed.
/// @param limits the engine's limits on a frame's extent and layout.
/// @param pixelAspectRatio the container's pixel aspect ratio, stated on every delivered frame.
/// @param path the video's path, carried by a refusal.
/// @return the delivery plan, or the refusal.
[[nodiscard]] Result<DeliveryPlan> planDelivery(const AVCodecParameters& parameters, const FfmpegSourceOptions& options,
                                                const EngineLimits& limits, double pixelAspectRatio,
                                                std::string_view path);

/// A stream's code range in the adapter's vocabulary.
[[nodiscard]] FfmpegColorRange colorRangeOf(int range) noexcept;

/// The stable text of a conversion, as a card carries it.
[[nodiscard]] const char* conversionName(FfmpegConversion conversion) noexcept;

/// The stable text of a range, as a card carries it.
[[nodiscard]] const char* colorRangeName(FfmpegColorRange range) noexcept;

/// Carries a plan out frame by frame. Owns the scaler a conversion needs; not thread-safe.
class FrameDelivery
{
public:
    /// Takes a plan `planDelivery` produced. The scaler is created at the first conversion.
    explicit FrameDelivery(DeliveryPlan plan) noexcept;

    /// The plan every frame is delivered by.
    [[nodiscard]] const DeliveryPlan& plan() const noexcept
    {
        return m_plan;
    }

    /// Delivers one decoded frame: shared in place, copied, looked up or converted as planned.
    ///
    /// A frame whose pixel format or size differs from the stream's is `ErrorCode::InvalidData` with
    /// `SequenceFailure::MalformedContainer`. A frame the delivery plan would share but that cannot be described
    /// in place is copied instead.
    [[nodiscard]] Result<RawFrame> deliver(const EngineContext& engine, const ExecutionContext& execution,
                                           const AVFrame& frame, const FfmpegSourceOptions& options,
                                           std::string_view path);

private:
    /// Describes the decoder's buffer in place, holding a reference to the frame for as long as the
    /// samples live; answers an empty frame when the buffer cannot be described, so the caller copies.
    [[nodiscard]] Result<RawFrame> share(const EngineContext& engine, const ExecutionContext& execution,
                                         const AVFrame& frame) const;
    /// Copies the samples into engine memory, taking each plane from `DeliveryPlan::sourcePlane` and
    /// exchanging red and blue when the delivery plan says so.
    [[nodiscard]] Result<RawFrame> copy(const EngineContext& engine, const ExecutionContext& execution,
                                        const AVFrame& frame, std::string_view path) const;
    /// Looks palette indices up into eight-bit RGB, substituting a grey ramp for an empty palette when
    /// the options allow it.
    [[nodiscard]] Result<RawFrame> expandPalette(const EngineContext& engine, const ExecutionContext& execution,
                                                 const AVFrame& frame, const FfmpegSourceOptions& options,
                                                 std::string_view path) const;
    /// Converts through the scaler into eight- or sixteen-bit RGB, creating the scaler on first use.
    [[nodiscard]] Result<RawFrame> convert(const EngineContext& engine, const ExecutionContext& execution,
                                           const AVFrame& frame, std::string_view path);

    DeliveryPlan m_plan;
    ScaleContextPtr m_scale;
};

} // namespace an::ffmpeg_detail
