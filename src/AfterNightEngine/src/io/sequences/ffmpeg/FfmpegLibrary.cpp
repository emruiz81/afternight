// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg error codes, object owners and failure translation, and the library's
// entry in the engine's dependency report.
//
// The library's error macros are spelled only in this file, so the rest of the adapter compares codes
// through `errorAgain` and its siblings, and the lint exemptions those spellings need stay in one place.

#include "io/sequences/ffmpeg/FfmpegLibrary.h"

#include "runtime/DependencyVersions.h"

// The two FFmpeg headers the dependency report needs and this adapter's own work does not: the one
// that declares the run-time version accessor, and the generated one that carries the version this
// build was compiled against. The second is guarded because it is generated rather than authored, so
// a build of FFmpeg that did not produce it leaves the compiled version unreported instead of
// failing to compile.
extern "C" {
#include <libavutil/avutil.h>
#if __has_include(<libavutil/ffversion.h>)
#include <libavutil/ffversion.h>
#endif
}

#include <cerrno>
#include <utility>

namespace an::ffmpeg_detail
{

int errorAgain() noexcept
{
    return AVERROR(EAGAIN);
}

int errorEndOfFile() noexcept
{
    return AVERROR_EOF; // NOLINT: the library's own spelling of its code.
}

int errorExit() noexcept
{
    return AVERROR_EXIT; // NOLINT: the library's own spelling of its code.
}

int errorNotPermitted() noexcept
{
    return AVERROR(EPERM);
}

int errorInput() noexcept
{
    return AVERROR(EIO);
}

int errorInvalidArgument() noexcept
{
    return AVERROR(EINVAL);
}

void FormatContextDeleter::operator()(AVFormatContext* context) const noexcept
{
    avformat_close_input(&context);
}

void OutputContextDeleter::operator()(AVFormatContext* context) const noexcept
{
    avformat_free_context(context);
}

void CodecContextDeleter::operator()(AVCodecContext* context) const noexcept
{
    avcodec_free_context(&context);
}

void PacketDeleter::operator()(AVPacket* packet) const noexcept
{
    av_packet_free(&packet);
}

void FrameDeleter::operator()(AVFrame* frame) const noexcept
{
    av_frame_free(&frame);
}

void ScaleContextDeleter::operator()(SwsContext* context) const noexcept
{
    sws_freeContext(context);
}

void BufferDeleter::operator()(AVBufferRef* buffer) const noexcept
{
    av_buffer_unref(&buffer);
}

Status videoFailure(ErrorCode code, SequenceFailure reason, std::string message, std::string_view parameter,
                    std::string_view path, std::vector<StatusContextField> extra)
{
    std::vector<StatusContextField> context;
    context.reserve(3u + extra.size());
    context.push_back(StatusContextField::ofCount(ContextKey::SequenceReason, static_cast<std::uint64_t>(reason)));
    if (!parameter.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Parameter, std::string(parameter)));
    if (!path.empty())
        context.push_back(StatusContextField::ofText(ContextKey::Path, std::string(path)));
    for (auto& field : extra)
        context.push_back(std::move(field));
    return Status::failure(code, {.message = std::move(message),
                                  .operation = "ffmpeg",
                                  .domain = ErrorDomain::Io,
                                  .context = std::move(context)});
}

Status argumentFailure(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           {.message = message,
                            .operation = "ffmpeg",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

Status ioFailure(int platformError, const char* message, std::string_view path)
{
    return videoFailure(ErrorCode::ExternalFailure, SequenceFailure::IoFailure, message, {}, path,
                        {StatusContextField::ofNumber(ContextKey::LibraryCode, platformError)});
}

Status libraryFailure(int code, const char* message, std::string_view path)
{
    std::string described = message;
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(code, text, sizeof(text)) == 0)
    {
        described += ": ";
        described += text;
    }
    std::vector<StatusContextField> extra{StatusContextField::ofText(ContextKey::Library, "ffmpeg"),
                                          StatusContextField::ofNumber(ContextKey::LibraryCode, code)};
    if (code == AVERROR(ENOMEM))
        return videoFailure(ErrorCode::AllocationFailed, SequenceFailure::DecoderFailure, std::move(described), {},
                            path, std::move(extra));
    if (code == AVERROR(EPERM))
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding,
                            "the container refers to another resource, and this adapter opens none", "container", path,
                            std::move(extra));
    if (code == AVERROR_INVALIDDATA || code == AVERROR_EOF || // NOLINT: the library's own spelling of its codes.
        code == errorInvalidArgument())
        return videoFailure(ErrorCode::InvalidData, SequenceFailure::MalformedContainer, std::move(described), {}, path,
                            std::move(extra));
    if (code == AVERROR_DECODER_NOT_FOUND || code == AVERROR_DEMUXER_NOT_FOUND || // NOLINT: as above.
        code == AVERROR_PATCHWELCOME || code == AVERROR(ENOSYS)) // NOLINT: as above.
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding, std::move(described), {},
                            path, std::move(extra));
    return videoFailure(ErrorCode::ExternalFailure, SequenceFailure::DecoderFailure, std::move(described), {}, path,
                        std::move(extra));
}

Status encoderFailure(int code, const char* message, std::string_view parameter, std::string_view path)
{
    std::string described = message;
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(code, text, sizeof(text)) == 0)
    {
        described += ": ";
        described += text;
    }
    std::vector<StatusContextField> extra{StatusContextField::ofText(ContextKey::Library, "ffmpeg"),
                                          StatusContextField::ofNumber(ContextKey::LibraryCode, code)};
    if (code == AVERROR(ENOMEM))
        return videoFailure(ErrorCode::AllocationFailed, SequenceFailure::EncoderFailure, std::move(described),
                            parameter, path, std::move(extra));
    if (code == AVERROR_ENCODER_NOT_FOUND || code == AVERROR_MUXER_NOT_FOUND || // NOLINT: the library's own spelling.
        code == AVERROR_PATCHWELCOME || code == AVERROR(ENOSYS)) // NOLINT: as above.
        return videoFailure(ErrorCode::Unsupported, SequenceFailure::UnsupportedEncoding, std::move(described),
                            parameter, path, std::move(extra));
    return videoFailure(ErrorCode::ExternalFailure, SequenceFailure::EncoderFailure, std::move(described), parameter,
                        path, std::move(extra));
}

} // namespace an::ffmpeg_detail

namespace an::dependency_detail
{

void reportFfmpeg(DependencyVersion& entry) noexcept
{
    entry.name = "ffmpeg";
    entry.linkage = DependencyLinkage::SharedLibrary;

    // The distribution's version rather than any one library's. `libavformat` calls itself 62 and
    // `libavutil` 60, which are interface generations and not what an advisory or a build record
    // means by the version of FFmpeg.
#if defined(FFMPEG_VERSION)
    setVersionTextFromBanner(entry.compiled, FFMPEG_VERSION, nullptr);
#endif
    setVersionTextFromBanner(entry.loaded, av_version_info(), nullptr);
}

} // namespace an::dependency_detail
