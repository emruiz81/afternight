// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg libraries as this adapter uses them: their headers, owners for their
// objects, the few constants whose macros are spelled once here, and how their failures become the
// engine's.
//
// Every FFmpeg header the engine includes is reached through this directory and nowhere else, which
// build/scripts/tools/check_include_layers.py enforces: no file outside io/sequences/ffmpeg sees an
// FFmpeg type, and no published header names one.

#pragma once

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/SequenceError.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/// The FFmpeg video adapter's private machinery: the library boundary, the container input and output,
/// frame delivery and encoding. Nothing outside `io/sequences/ffmpeg` names it.
namespace an::ffmpeg_detail
{

/// The timestamp FFmpeg uses to say there is none, without the cast its macro spells.
inline constexpr std::int64_t kNoTimestamp = (std::numeric_limits<std::int64_t>::min)();

/// FFmpeg's "try again after the other call" code.
[[nodiscard]] int errorAgain() noexcept;

/// FFmpeg's end-of-input code.
[[nodiscard]] int errorEndOfFile() noexcept;

/// FFmpeg's "the interrupt callback asked to stop" code.
[[nodiscard]] int errorExit() noexcept;

/// The code this adapter answers a container's request to open another resource with.
[[nodiscard]] int errorNotPermitted() noexcept;

/// The code this adapter's reader answers an operating-system failure with.
[[nodiscard]] int errorInput() noexcept;

/// The code this adapter's reader answers an invalid seek with.
[[nodiscard]] int errorInvalidArgument() noexcept;

/// Releases a demuxer context and everything it owns except a caller-supplied reader.
struct FormatContextDeleter
{
    /// Closes the demuxer's input and frees the context. A null context is ignored.
    void operator()(AVFormatContext* context) const noexcept;
};

/// Releases a muxer context and everything it owns except a caller-supplied writer.
struct OutputContextDeleter
{
    /// Frees the muxer context and its streams; the writer is the caller's to close. A null context is
    /// ignored.
    void operator()(AVFormatContext* context) const noexcept;
};

/// Releases a decoder context.
struct CodecContextDeleter
{
    /// Frees the codec context and what it owns. A null context is ignored.
    void operator()(AVCodecContext* context) const noexcept;
};

/// Releases a packet and its payload reference.
struct PacketDeleter
{
    /// Unreferences the payload and frees the packet. A null packet is ignored.
    void operator()(AVPacket* packet) const noexcept;
};

/// Releases a frame and its buffer references.
struct FrameDeleter
{
    /// Unreferences the frame's buffers and frees the frame. A null frame is ignored.
    void operator()(AVFrame* frame) const noexcept;
};

/// Releases a scaler.
struct ScaleContextDeleter
{
    /// Frees the scaler. A null scaler is ignored.
    void operator()(SwsContext* context) const noexcept;
};

/// Releases one reference to a reference-counted buffer, such as a device or a frames context.
struct BufferDeleter
{
    /// Drops the reference; the buffer is freed with its last one. A null reference is ignored.
    void operator()(AVBufferRef* buffer) const noexcept;
};

/// An owned demuxer context, closed with its input.
using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;

/// An owned muxer context; its writer is owned apart from it.
using OutputContextPtr = std::unique_ptr<AVFormatContext, OutputContextDeleter>;

/// An owned decoder or encoder context.
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

/// An owned packet.
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

/// An owned frame.
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

/// An owned scaler.
using ScaleContextPtr = std::unique_ptr<SwsContext, ScaleContextDeleter>;

/// One owned reference to a reference-counted buffer.
using BufferPtr = std::unique_ptr<AVBufferRef, BufferDeleter>;

/// A refusal carrying its sequence reason and whichever of the parameter, the path and further fields
/// apply.
[[nodiscard]] Status videoFailure(ErrorCode code, SequenceFailure reason, std::string message,
                                  std::string_view parameter, std::string_view path,
                                  std::vector<StatusContextField> extra = {});

/// An `ErrorCode::InvalidArgument` refusal naming the parameter, with no sequence reason.
[[nodiscard]] Status argumentFailure(const char* message, const char* parameter);

/// An operating-system refusal: `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure`, carrying
/// the platform's error code and the path.
[[nodiscard]] Status ioFailure(int platformError, const char* message, std::string_view path);

/// What a failure a demuxer or a decoder reported means, and each answer carries the library, its code
/// and its own description beside the message.
///
/// An allocation failure is `ErrorCode::AllocationFailed`. A container's attempt to open another
/// resource, which this adapter refuses, is `ErrorCode::Unsupported` naming the container. Invalid or
/// ended data and an invalid argument are `ErrorCode::InvalidData` with
/// `SequenceFailure::MalformedContainer`. A missing decoder or demuxer or an unimplemented feature is
/// `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding`, and anything else is
/// `ErrorCode::ExternalFailure` with `SequenceFailure::DecoderFailure`.
[[nodiscard]] Status libraryFailure(int code, const char* message, std::string_view path);

/// What a failure an encoder or a muxer reported means, and each answer carries the parameter it
/// concerns, the library, its code and its own description beside the message.
///
/// An allocation failure is `ErrorCode::AllocationFailed`, a missing encoder or muxer or an
/// unimplemented feature is `ErrorCode::Unsupported` with `SequenceFailure::UnsupportedEncoding`, and
/// anything else is `ErrorCode::ExternalFailure` with `SequenceFailure::EncoderFailure`.
[[nodiscard]] Status encoderFailure(int code, const char* message, std::string_view parameter, std::string_view path);

} // namespace an::ffmpeg_detail
