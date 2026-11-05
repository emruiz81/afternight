// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the writer FFmpeg's muxers write a video through, over the engine's file handle.
//
// The writer keeps its own position and the file's extent - one past the furthest byte written - so a
// muxer that seeks back to patch a header, or asks for the size, is answered without a platform call.
// Every write checks the bound operation's stop and deadline first, and a refused write records the
// operating system's error so that `outputFailure` can report it rather than FFmpeg's generic code.

#include "io/sequences/ffmpeg/FfmpegOutput.h"

#include <AfterNightEngine/Runtime/TraceSink.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <span>
#include <utility>

namespace an::ffmpeg_detail
{
namespace
{
/// How many bytes FFmpeg's writer buffers between calls into the file boundary. Compressed video packets are
/// small beside it, so a frame's packets usually reach the file in one call.
constexpr int kWriterBufferBytes = 256 * 1024;

int interruptRequested(void* opaque) noexcept
{
    auto* output = static_cast<OutputFile*>(opaque);
    const ExecutionContext* execution = output->bound();
    if (execution != nullptr && (execution->stopRequested() || execution->deadlineExceeded()))
    {
        output->markInterrupted();
        return 1;
    }
    return 0;
}

int refuseForeignOpen(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) noexcept
{
    return errorNotPermitted();
}
} // namespace

OutputFile::~OutputFile()
{
    releaseWriter();
}

Result<std::unique_ptr<OutputFile>> OutputFile::open(std::string_view path)
{
    std::unique_ptr<OutputFile> output(new OutputFile());
    output->m_path = std::string(path);
    const std::filesystem::path native(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    if (const int error = output->m_file.openForWriting(native); error != 0)
        return ioFailure(error, "the pending video payload could not be opened", path);
    auto* buffer = static_cast<unsigned char*>(av_malloc(static_cast<std::size_t>(kWriterBufferBytes)));
    if (buffer == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    output->m_writer =
        avio_alloc_context(buffer, kWriterBufferBytes, 1, output.get(), nullptr, &OutputFile::write, &OutputFile::seek);
    if (output->m_writer == nullptr)
    {
        av_free(buffer);
        return Status::failure(ErrorCode::AllocationFailed);
    }
    return output;
}

void OutputFile::releaseWriter() noexcept
{
    if (m_writer == nullptr)
        return;
    av_freep(&m_writer->buffer);
    avio_context_free(&m_writer);
}

int OutputFile::flush() noexcept
{
    if (m_writer == nullptr)
        return 0;
    avio_flush(m_writer);
    return m_writer->error;
}

int OutputFile::close() noexcept
{
    releaseWriter();
    return m_file.close();
}

int OutputFile::discard() noexcept
{
    releaseWriter();
    if (!m_file.isOpen())
        return 0;
    const int truncated = m_file.truncate();
    const int closed = m_file.close();
    return truncated != 0 ? truncated : closed;
}

#if LIBAVFORMAT_VERSION_MAJOR >= 61
int OutputFile::write(void* opaque, const std::uint8_t* buffer, int size) noexcept
#else
int OutputFile::write(void* opaque, std::uint8_t* buffer, int size) noexcept
#endif
{
    auto* output = static_cast<OutputFile*>(opaque);
    if (size <= 0)
        return 0;
    const ExecutionContext* execution = output->m_execution;
    if (execution != nullptr && (execution->stopRequested() || execution->deadlineExceeded()))
    {
        output->m_interrupted = true;
        return errorExit();
    }
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(buffer), static_cast<std::size_t>(size));
    if (const int error = output->m_file.writeAt(bytes, output->m_position); error != 0)
    {
        output->m_platformError = error;
        return errorInput();
    }
    output->m_position += static_cast<std::uint64_t>(size);
    output->m_extent = (std::max)(output->m_extent, output->m_position);
    if (execution != nullptr)
        execution->reportTrace(TraceEventKind::Counter, "ffmpeg", "output-bytes", static_cast<double>(size), "bytes");
    return size;
}

std::int64_t OutputFile::seek(void* opaque, std::int64_t offset, int whence) noexcept
{
    auto* output = static_cast<OutputFile*>(opaque);
    if (whence == AVSEEK_SIZE)
        return static_cast<std::int64_t>(output->m_extent);
    std::int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE)
    {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = static_cast<std::int64_t>(output->m_position);
        break;
    case SEEK_END:
        base = static_cast<std::int64_t>(output->m_extent);
        break;
    default:
        return errorInvalidArgument();
    }
    if ((offset < 0 && base + offset < 0) || (offset > 0 && base > (std::numeric_limits<std::int64_t>::max)() - offset))
        return errorInvalidArgument();
    output->m_position = static_cast<std::uint64_t>(base + offset);
    return base + offset;
}

Status configureMuxer(AVFormatContext& muxer, OutputFile& output)
{
    muxer.pb = output.writer();
    muxer.flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_BITEXACT;
    muxer.io_open = &refuseForeignOpen;
    muxer.interrupt_callback.callback = &interruptRequested;
    muxer.interrupt_callback.opaque = &output;
    // The container is written through the writer above and needs no protocol, so an empty whitelist
    // refuses every resource a muxer might otherwise open by name.
    muxer.protocol_whitelist = av_strdup("");
    if (muxer.protocol_whitelist == nullptr)
        return Status::failure(ErrorCode::AllocationFailed);
    return Status::success();
}

Status outputFailure(const OutputFile& output, const ExecutionContext& execution, int code, const char* message,
                     std::string_view parameter)
{
    if (output.interrupted() || code == errorExit())
    {
        if (const Status stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        return Status::failure(
            ErrorCode::Cancelled,
            {.message = "the video output was interrupted", .operation = "ffmpeg", .domain = ErrorDomain::Io});
    }
    if (output.platformError() != 0)
        return ioFailure(output.platformError(), message, output.path());
    return encoderFailure(code, message, parameter, output.path());
}

} // namespace an::ffmpeg_detail
