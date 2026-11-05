// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFmpeg container boundary for writing: a muxer writing through the engine's
// own file handle into a payload a sequence output reserved.
//
// FFmpeg opens nothing here either. The payload is reopened by the engine's file boundary, which refuses a
// name that no longer holds a regular file with one link, and FFmpeg's writer calls back into it with
// positioned writes. A muxer that asks to open a second resource is refused, and every write observes the
// bound operation's cancellation, so a stop during an append or a trailer is noticed at the next write.

#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "io/sequences/SequenceFile.h"
#include "io/sequences/ffmpeg/FfmpegLibrary.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace an::ffmpeg_detail
{

/// The engine's file seen through FFmpeg's writer interface. Owns the handle, the writer and its buffer.
/// Heap-allocated so the writer's pointer back to it stays valid; not thread-safe, like the session it
/// belongs to.
class OutputFile
{
public:
    /// Releases the writer and closes the file, ignoring a close failure; `close` is how to observe one.
    ~OutputFile();

    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;

    /// Reopens a reserved payload for writing through FFmpeg, discarding what it holds.
    ///
    /// A name that no longer holds a regular file with one link, or that the platform refuses, is
    /// `ErrorCode::ExternalFailure` with `SequenceFailure::IoFailure`; the writer's buffer failing to
    /// allocate is `ErrorCode::AllocationFailed`.
    [[nodiscard]] static Result<std::unique_ptr<OutputFile>> open(std::string_view path);

    /// The writer a muxer is given.
    [[nodiscard]] AVIOContext* writer() const noexcept
    {
        return m_writer;
    }

    /// The operation whose cancellation writes observe and whose trace receives their byte counts.
    void bind(const ExecutionContext* execution) noexcept
    {
        m_execution = execution;
        m_interrupted = false;
        m_platformError = 0;
    }

    /// The bound operation, or null.
    [[nodiscard]] const ExecutionContext* bound() const noexcept
    {
        return m_execution;
    }

    /// Whether a write or an interrupt check refused because the bound operation was stopped.
    [[nodiscard]] bool interrupted() const noexcept
    {
        return m_interrupted;
    }

    /// Records that FFmpeg's interrupt check saw a stop.
    void markInterrupted() noexcept
    {
        m_interrupted = true;
    }

    /// The operating system's error from the last failed write, or zero.
    [[nodiscard]] int platformError() const noexcept
    {
        return m_platformError;
    }

    /// The bytes the file holds: one past the furthest byte written.
    [[nodiscard]] std::uint64_t extent() const noexcept
    {
        return m_extent;
    }

    /// Hands FFmpeg's buffered bytes to the file.
    ///
    /// @return zero, or FFmpeg's error for the writer.
    [[nodiscard]] int flush() noexcept;

    /// Releases the writer and closes the file; a second call reports success.
    ///
    /// @return zero, or the operating system's error.
    [[nodiscard]] int close() noexcept;

    /// Releases the writer, discards the file's contents and closes it, reporting only a platform code.
    ///
    /// @return zero, or the operating system's error from truncating or, failing that, closing.
    int discard() noexcept;

    /// The path the payload was opened with.
    [[nodiscard]] const std::string& path() const noexcept
    {
        return m_path;
    }

private:
    OutputFile() = default;
    void releaseWriter() noexcept;

#if LIBAVFORMAT_VERSION_MAJOR >= 61
    static int write(void* opaque, const std::uint8_t* buffer, int size) noexcept;
#else
    static int write(void* opaque, std::uint8_t* buffer, int size) noexcept;
#endif
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) noexcept;

    SequenceFile m_file;
    AVIOContext* m_writer = nullptr;
    const ExecutionContext* m_execution = nullptr;
    std::uint64_t m_position = 0u;
    std::uint64_t m_extent = 0u;
    int m_platformError = 0;
    bool m_interrupted = false;
    std::string m_path;
};

/// Binds an operation to an output for the life of a scope.
class BoundOutput
{
public:
    /// Binds `execution` to `output` until this object is destroyed; both must outlive it.
    BoundOutput(OutputFile& output, const ExecutionContext& execution) noexcept : m_output(output)
    {
        m_output.bind(&execution);
    }
    ~BoundOutput()
    {
        m_output.bind(nullptr);
    }
    BoundOutput(const BoundOutput&) = delete;
    BoundOutput& operator=(const BoundOutput&) = delete;

private:
    OutputFile& m_output;
};

/// Gives a muxer the output's writer, refuses every resource it might open by name, observes the output's
/// bound operation in FFmpeg's interrupt check and asks for bitexact output.
///
/// @return success, or `ErrorCode::AllocationFailed` when the empty protocol whitelist cannot be held.
[[nodiscard]] Status configureMuxer(AVFormatContext& muxer, OutputFile& output);

/// What a failure an encoder or a muxer reported during a bound operation means, preferring the operation's
/// own cancellation and the operating system's error to FFmpeg's code: a stopped operation answers with
/// its own cancellation, a failed write with `ioFailure`, and anything else with `encoderFailure`.
[[nodiscard]] Status outputFailure(const OutputFile& output, const ExecutionContext& execution, int code,
                                   const char* message, std::string_view parameter);

} // namespace an::ffmpeg_detail
