// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg video encoders as the sink uses them: which encoder and muxer a
// request names, the immutable settings at each quality level, what a request and a descriptor make of a
// stream, and the stated conversion of a frame into the encoder's picture.

#pragma once

#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/FfmpegSink.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include "io/sequences/ffmpeg/FfmpegLibrary.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace an::ffmpeg_detail
{

/// Every stream's keyframe interval, in frames.
inline constexpr int kKeyframeInterval = 12;

/// What a request and a descriptor make of a stream, decided before a byte is written.
struct EncodePlan
{
    FfmpegVideoCodec codec = FfmpegVideoCodec::H264; ///< The coding the stream is encoded with.
    FfmpegVideoContainer container = FfmpegVideoContainer::Mp4; ///< The container the stream is muxed into.
    const char* encoderName = ""; ///< FFmpeg's name for the encoder.
    const char* muxerName = ""; ///< FFmpeg's name for the muxer.
    int width = 0; ///< The picture's width in pixels; even, and within 32-bit signed terms.
    int height = 0; ///< The picture's height in pixels; even, and within 32-bit signed terms.
    bool grey = false; ///< Frames are stored as grey, whether they were mono or a mosaic.
    bool rowsArriveBottomUp = false; ///< The frame's origin is its bottom-left corner.
    AVRational frameRate{1, 1}; ///< Frames per second.
    AVRational timeBase{1, 1}; ///< Seconds per frame, the encoder's time base.
    int constantRateFactor = 0; ///< The quality level's factor for this coding and picture size.
    FfmpegEncodeQuality quality = FfmpegEncodeQuality::Standard; ///< The requested quality level.
    FfmpegEncodeMatrix matrix = FfmpegEncodeMatrix::Bt709; ///< The YCbCr matrix a colour frame is converted by.
    FfmpegEncodeRange range = FfmpegEncodeRange::Limited; ///< The code range the picture's samples use.
    std::uint32_t encoderThreads = 1u; ///< The software encoder's thread count; at least one.
    FfmpegEncoderPreference preference = FfmpegEncoderPreference::Processor; ///< Which encoders are tried.
};

/// The settings and the picture an encoder takes.
enum class EncoderFamily : std::uint8_t
{
    Software, ///< libx264, libx265 or libvpx-vp9: a constant rate factor and planar 4:2:0 pictures.
    Nvenc, ///< NVIDIA's NVENC: a constant quantizer and NV12 pictures.
    QuickSync, ///< Intel's Quick Sync: a global quality and NV12 pictures.
    Amf, ///< AMD's AMF: a constant quantizer for every picture type and NV12 pictures.
    Vaapi, ///< VA-API: a constant quantizer and NV12 pictures uploaded to device surfaces.
    VideoToolbox ///< Apple's VideoToolbox: a quality scale and NV12 pictures.
};

/// One encoder a sink may try.
struct EncoderCandidate
{
    const char* name; ///< FFmpeg's name for the encoder.
    EncoderFamily family; ///< The settings the encoder is opened with.
};

/// An opened encoder, and for VA-API the device and the surfaces its pictures are uploaded to.
struct OpenedEncoder
{
    CodecContextPtr context; ///< The open encoder.
    BufferPtr device; ///< The VA-API device, or null for any other family.
    BufferPtr frames; ///< The VA-API surfaces' frames context, or null for any other family.
};

/// The encoders a coding and a preference try, in order, the software encoder last.
[[nodiscard]] std::span<const EncoderCandidate> encoderCandidatesOf(FfmpegVideoCodec codec,
                                                                    FfmpegEncoderPreference preference) noexcept;

/// The software encoder a coding needs, which a refusal names; null for a value outside the enumeration.
[[nodiscard]] const char* encoderNameOf(FfmpegVideoCodec codec) noexcept;

/// The constant rate factor a coding and a quality level give a picture of this size.
///
/// H.264 and H.265 take their level's factor as it stands. VP9's is lowered for a picture larger than
/// 1920x1080 and raised by at most four for a smaller one, then held within libvpx-vp9's range of 0 to 63.
/// `quality` must be within its enumeration; the caller has checked it.
[[nodiscard]] int constantRateFactorOf(FfmpegVideoCodec codec, FfmpegEncodeQuality quality, std::uint32_t width,
                                       std::uint32_t height) noexcept;

/// Judges a request and a descriptor in a fixed order - the request's options, the descriptor's shape and
/// format, the timing, the coding against the container, the rate, the frame's samples, channels, filter
/// array, pixel shape and extent, and the encoder's presence - and decides the plan.
///
/// An option outside its enumeration or a zero thread count is `ErrorCode::InvalidArgument`. A
/// descriptor whose format does not match the container, that states no fixed frame or no nominal rate,
/// or that asks to seek is `ErrorCode::InvalidArgument` with `SequenceFailure::InvalidDescriptor`, beside
/// whatever `validateSequenceDescriptor` refuses. A rate beyond 32-bit signed terms is
/// `ErrorCode::LimitExceeded`. Everything else a stream cannot store - required timestamps, a coding the
/// container cannot hold, more than 1000 frames per second in Matroska or WebM, a frame of another
/// shape, an odd or oversized extent, no candidate encoder in this build - is `ErrorCode::Unsupported`;
/// both carry `SequenceFailure::UnsupportedEncoding`.
[[nodiscard]] Result<EncodePlan> planEncode(const SequenceDescriptor& descriptor, const FfmpegSinkRequest& request);

/// Opens one candidate with its family's settings, the plan's colour description and, for the software encoder,
/// the plan's thread count. A device candidate takes NV12 pictures; a VA-API candidate is given its device and a
/// frames context of NV12 surfaces the picture's size.
///
/// @param plan what to encode.
/// @param candidate the encoder to open.
/// @param globalHeader whether the muxer wants the stream headers apart from the packets.
/// @param path names a failure.
/// @return the open encoder; `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding` when
///         this build lacks the candidate or a device encoder leaves a setting untaken, so the next
///         candidate is tried; `ErrorCode::AllocationFailed`; what `encoderFailure` makes of the
///         library's refusal to open the device, its surfaces or the encoder; or `ErrorCode::Internal`
///         when the software encoder leaves a setting untaken, which is a defect of this adapter.
[[nodiscard]] Result<OpenedEncoder> openEncoder(const EncodePlan& plan, const EncoderCandidate& candidate,
                                                bool globalHeader, std::string_view path);

/// Rearranges a 4:2:0 picture into an NV12 picture of the same size, sample for sample.
void rearrangeToNv12(const AVFrame& planar, AVFrame& semiPlanar) noexcept;

/// The pixel format a colour frame's arrangement is handed to the converter as.
[[nodiscard]] AVPixelFormat sourceFormatOf(const ImageLayout& layout) noexcept;

/// A converter from one colour arrangement's pixel format into the encoder's pictures, under the plan's matrix
/// and range, with bilinear chroma, accurate rounding, full-resolution chroma input and bitexact arithmetic.
/// The scaler's refusal to exist is `ErrorCode::AllocationFailed`, and its refusal of the matrix and range
/// is `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding`.
[[nodiscard]] Result<ScaleContextPtr> openConverter(const EncodePlan& plan, AVPixelFormat source);

/// Converts one colour frame into a writable picture of the encoder's format, reading a bottom-up frame from
/// its last row with negative strides so no row is copied twice. The scaler's refusal is reported through
/// `encoderFailure`.
[[nodiscard]] Status convertFrame(SwsContext& converter, const EncodePlan& plan, const RawFrame& frame,
                                  const ImageLayout& layout, AVFrame& picture, std::string_view path);

/// Converts one grey frame into a writable picture of the encoder's format without a scaler. Each luma code
/// is its sample mapped exactly into the plan's range - `round(16 + 219v/255)` in limited range and `v` in
/// full range, the codes an accurate conversion writes - and both chroma planes are neutral. Rows are read
/// from the bottom for a bottom-up frame.
void convertGreyFrame(const EncodePlan& plan, const RawFrame& frame, AVFrame& picture) noexcept;

} // namespace an::ffmpeg_detail
