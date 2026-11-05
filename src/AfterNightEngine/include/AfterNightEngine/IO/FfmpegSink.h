// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Video encoded through FFmpeg, one frame at a time, into a private output.
///
/// **The encoder is named, and a device encodes only when asked.** H.264 is libx264, H.265 is libx265 and VP9 is
/// libvpx-vp9; a build without one refuses it by name. Their settings at each of five quality levels are immutable
/// data inside the adapter. A request may prefer a device instead. The device candidates are then tried in a fixed
/// order, and one that refuses to open is passed over for the next rather than failing the export, ending with the
/// software encoder; the sink states which encoder opened and which refused.
///
/// **The conversion is stated, and the stream says what it was.** A colour frame becomes YCbCr through a
/// stated matrix and range, and the stream carries both. A grey frame needs no matrix, so it states none
/// and states its range.
///
/// **An encoder's own threads are counted.** These libraries start threads of their own that no task arena
/// reaches. By default each is configured to run its work one thread at a time; a request for more takes
/// exactly that many of the engine context's threads for as long as the encoder is open, so the library's
/// threads and the context's operations together never exceed the context's maximum.
///
/// **FFmpeg opens nothing.** The container is written through the engine's own file boundary into the
/// pending payload an `IO/SequenceOutput.h` output reserved, and the caller publishes it once the sink has
/// finished.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/FrameSink.h>

#include <cstdint>
#include <string>
#include <vector>

namespace an
{

class SequenceOutput;

/// Which video coding a sink produces, and so which encoder it uses.
enum class FfmpegVideoCodec : std::uint8_t
{
    /// H.264, through libx264.
    H264,

    /// H.265, through libx265.
    H265,

    /// VP9, through libvpx-vp9.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Vp9
};

/// Which container a sink writes the coded stream into.
enum class FfmpegVideoContainer : std::uint8_t
{
    /// MP4, which holds H.264, H.265 and VP9. Descriptors name `SequenceFormat::Mp4`.
    Mp4,

    /// Matroska, which holds all three. Descriptors name `SequenceFormat::Video`.
    Matroska,

    /// WebM, which holds VP9 alone. Descriptors name `SequenceFormat::Video`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    WebM
};

/// How much a sink spends on each picture, as a constant rate factor per encoder. The factors are
/// immutable data in the adapter:
///
/// | Level | libx264 | libx265 | libvpx-vp9 |
/// |---|---:|---:|---:|
/// | `Draft` | 28 | 32 | 35 |
/// | `Standard` | 26 | 29 | 32 |
/// | `Good` | 24 | 26 | 28 |
/// | `High` | 22 | 23 | 24 |
/// | `Best` | 20 | 20 | 20 |
///
/// libx264 and libx265 run their `medium` preset with the `grain` tuning. libvpx-vp9 runs at speed one
/// with frame-parallel decoding and one frame of lag, and its factor is lowered for pictures larger than
/// 1920x1080 and raised for smaller ones by five times the natural logarithm of the area ratio, by at
/// most four, computed in single precision and truncated toward zero.
/// Every stream is 8-bit 4:2:0 with a keyframe at least every twelve frames.
enum class FfmpegEncodeQuality : std::uint8_t
{
    Draft, ///< The smallest files.
    Standard, ///< The default.
    Good, ///< Between the default and `High`.
    High, ///< Close to transparent for most astronomical video.
    Best ///< The largest files. Deliberately the last enumerator, so a membership test needs no table.
};

/// The matrix a colour frame is converted to YCbCr through, which the stream then states.
enum class FfmpegEncodeMatrix : std::uint8_t
{
    /// ITU-R BT.709, stated as H.273 matrix coefficients 1. The default.
    Bt709,

    /// ITU-R BT.601, stated as H.273 matrix coefficients 6.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Bt601
};

/// The code range a frame's samples are mapped into, which the stream then states.
enum class FfmpegEncodeRange : std::uint8_t
{
    /// Luma from 16 to 235 and chroma from 16 to 240. The default.
    Limited,

    /// Every eight-bit code.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Full
};

/// What a sink does with frames whose description states a colour filter array, which no video stream
/// can state.
enum class FfmpegMosaicPolicy : std::uint8_t
{
    /// The description is refused with `SequenceFailure::UnsupportedEncoding` naming `cfa`. The default.
    Refuse,

    /// The samples are encoded as grey and the pattern is not stored.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    WriteSamplesAsMono
};

/// Which encoders a sink tries, in order.
enum class FfmpegEncoderPreference : std::uint8_t
{
    /// The coding's software encoder alone. The default.
    Processor,

    /// The device encoders this build contains, in this order, and then the software encoder: for H.264 and
    /// H.265, NVIDIA's NVENC, Intel's Quick Sync, AMD's AMF, VA-API and VideoToolbox; for VP9, VA-API. The
    /// first that opens encodes the stream.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    DeviceFirst
};

/// Which encoder a sink opened, and which it passed over.
struct FfmpegEncoderReport
{
    /// FFmpeg's name for the encoder that opened, such as `h264_vaapi` or `libx264`.
    std::string encoderName{};

    /// Whether that encoder runs on a device rather than on the processor.
    bool device = false;

    /// FFmpeg's names for the candidates this build contains that refused to open before it, in the order they
    /// were tried.
    std::vector<std::string> refused{};
};

/// What a sink encodes and how. Borrowed by `createFfmpegSink` and copied.
struct FfmpegSinkRequest
{
    /// The coding, and so the encoder.
    FfmpegVideoCodec codec = FfmpegVideoCodec::H264;

    /// The container, which must hold the coding.
    FfmpegVideoContainer container = FfmpegVideoContainer::Mp4;

    /// How much each picture may cost.
    FfmpegEncodeQuality quality = FfmpegEncodeQuality::Standard;

    /// The matrix colour frames are converted through. Grey frames need none and ignore it.
    FfmpegEncodeMatrix matrix = FfmpegEncodeMatrix::Bt709;

    /// The code range every frame is mapped into.
    FfmpegEncodeRange range = FfmpegEncodeRange::Limited;

    /// What happens to frames whose description states a filter array.
    FfmpegMosaicPolicy mosaicFrames = FfmpegMosaicPolicy::Refuse;

    /// How many threads the encoding library may run its work on, from one to the engine context's
    /// maximum. One - the default - configures libx264 and libvpx-vp9 to start no thread and libx265 to
    /// run without a thread pool and with one frame thread, which works while the appending thread
    /// waits. More takes a lease on exactly that many of the engine context's threads from the sink's
    /// creation until it finishes or closes, and gives the count to the library as libx264's and
    /// libvpx-vp9's thread count and libx265's pool size. libvpx-vp9 writes the same stream at every
    /// count and libx264 does not, so bytes repeat for a repeated request, its count included. A device encoder
    /// is given no thread count.
    std::uint32_t encoderThreads = 1u;

    /// Which encoders are tried. A device encoder is given the same eight-bit 4:2:0 picture the software encoder
    /// is, rearranged into NV12 order and, for VA-API, uploaded to one of its surfaces, with a keyframe at least
    /// every twelve frames. Its settings are immutable data in the adapter: NVENC's `p4` preset at a constant
    /// quantizer, Quick Sync's `medium` preset at a global quality, AMF and VA-API at a constant quantizer - each
    /// the quality level's factor for the coding in the table above - and VideoToolbox at a fixed quality scale.
    /// The stream a device writes is that device's and repeats only on it.
    FfmpegEncoderPreference encoderPreference = FfmpegEncoderPreference::Processor;
};

/// Whether this build's FFmpeg contains the encoder a coding needs.
///
/// Thread-safe and allocation-free; answerable before any engine object exists.
///
/// @param codec the coding to ask about.
/// @return true when the encoder is present; false when it is not, and for a value outside the
///         enumeration.
[[nodiscard]] AFTERNIGHTENGINE_API bool isFfmpegEncoderAvailable(FfmpegVideoCodec codec) noexcept;

/// Opens a video encoder over an output's pending payload and writes the container's header.
///
/// The descriptor must name `SequenceFormat::Mp4` for an MP4 container and `SequenceFormat::Video` for
/// Matroska and WebM, a fixed frame and a nominal frame rate whose numerator and denominator are at most
/// `INT32_MAX`, and advertise no seek. Frame `i` is presented at `i` periods of that rate and lasts one
/// period, so a descriptor that requires timestamps is refused and a frame's own presentation ticks are
/// not stored. Matroska and WebM store millisecond timestamps, so a rate above 1000 frames per second is
/// refused there as `ErrorCode::Unsupported` naming `nominalFrameRate` before the payload is claimed; the
/// time base the muxer actually chose is checked again after the header is written, and must still
/// tell adjacent frames apart. What a stream stores:
///
/// | Frame property | Stored | Refused |
/// |---|---|---|
/// | Samples | one unsigned byte | every other representation: `sampleFormat` |
/// | Channels | mono as grey; RGB through the request's matrix | every other schema: `channelSchema` |
/// | Filter array | none; any pattern under `FfmpegMosaicPolicy::WriteSamplesAsMono` | any pattern by default: `cfa` |
/// | Geometry | top-left or bottom-left origin, square pixels, even width and height | `pixelAspectRatio`, `extent` |
/// | Arrangement | one plane, or three RGB planes, with or without row padding | others, at append: `layout` |
///
/// FFmpeg's colour converter addresses a row stride as an `int`, so a colour frame whose row stride does
/// not fit one is refused at append as `ErrorCode::Unsupported` naming `rowStrideBytes`, before any sample
/// reaches the converter. Grey rows are addressed directly and have no such limit.
///
/// Colour basis and numeric domain are not stored beyond the matrix and range of the conversion. A colour
/// frame's conversion computes bilinear chroma from full-resolution chroma with accurate rounding and
/// bitexact arithmetic. A grey frame's luma codes are its samples mapped exactly into the range -
/// `round(16 + 219v/255)` in limited range, `v` in full range - with neutral chroma, the codes that
/// conversion would write. The muxer writes bitexact containers, with no library version or random
/// identifier, and every packet carries its duration.
///
/// Nothing is written until the request, the descriptor, the engine's limits and the encoder's
/// presence have been accepted and the thread lease has been taken, so a refused request leaves the
/// payload as it was. The sink then claims the payload through `SequenceOutput::claimWriter`, reopens
/// it by name as `createSerSink` does, opens the encoder and writes the header. Appends convert and
/// encode one frame and mux every packet the encoder returns; FFmpeg's writes observe the appending
/// operation's cancellation. Finish drains the encoder, writes the trailer, closes the payload,
/// releases the lease and completes the claim, after which the caller commits the output. Closing an
/// unfinished sink discards what it wrote and returns the payload to the output unpublished.
///
/// With `FfmpegEncoderPreference::DeviceFirst`, each candidate is opened in turn over the same payload
/// before any header is written, and a candidate that refuses to open, or refuses a setting, is passed
/// over. Only the software encoder's refusal, or the last candidate's when this build has no software
/// encoder, is reported.
///
/// @param engine the context the output was reserved for; lends the encoder's threads.
/// @param execution an operation of that context; its cancellation is checked before and after the
///        encoder opens.
/// @param output a valid, uncommitted output of the same context with no writer; claimed until the sink
///        finishes or closes.
/// @param descriptor the container's description; copied.
/// @param request what to encode and how; copied.
/// @return an open sink; `ErrorCode::InvalidArgument` naming an option outside its enumeration, a zero
///         `encoderThreads`, or a descriptor whose format does not match the container, states no fixed
///         frame, advertises a seek or states no nominal frame rate; `ErrorCode::InvalidState` for an
///         output that holds nothing or already has a writer; `ErrorCode::IncompatibleArguments` for an
///         output or execution of another context; `ErrorCode::Unsupported` with
///         `SequenceFailure::UnsupportedEncoding` naming `codec` for a coding the container cannot hold
///         or this build has no encoder for, or what the stream cannot store, required timestamps
///         included, or naming `nominalFrameRate` for a rate the container's timestamps cannot tell
///         apart; `ErrorCode::LimitExceeded` naming `encoderThreads` above the context's maximum,
///         `nominalFrameRate` beyond 32-bit signed terms, or with `SequenceFailure::DescriptorMismatch`
///         for frames beyond the engine's limits; `ErrorCode::Busy` naming `encoderThreads` when the
///         context cannot lend them all; `ErrorCode::ExternalFailure` with
///         `SequenceFailure::EncoderFailure` carrying FFmpeg's code when the encoder refuses to open or
///         the muxer to write, or with `SequenceFailure::IoFailure` carrying the operating system's code;
///         or the execution's cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSink>
createFfmpegSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
                 const SequenceDescriptor& descriptor, const FfmpegSinkRequest& request);

/// Opens a video encoder over an output's pending payload as `createFfmpegSink` does, and states which encoder
/// opened.
///
/// @param engine the context the output was reserved for; lends the encoder's threads.
/// @param execution an operation of that context.
/// @param output a valid, uncommitted output of the same context with no writer.
/// @param descriptor the container's description; copied.
/// @param request what to encode and how; copied.
/// @param report receives the encoder that opened and the candidates that refused before it; left unchanged
///        when the sink is refused.
/// @return what `createFfmpegSink` returns.
[[nodiscard]] AFTERNIGHTENGINE_API Result<FrameSink>
createFfmpegSink(const EngineContext& engine, const ExecutionContext& execution, SequenceOutput& output,
                 const SequenceDescriptor& descriptor, const FfmpegSinkRequest& request, FfmpegEncoderReport& report);

} // namespace an
