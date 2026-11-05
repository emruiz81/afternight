// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A command-line one-shot run, written against the engine's public headers alone.
//
// This is a probe, not a tool. It is shaped after the command-line program the product ships beside
// its application: start the engine, open one image, measure it, stretch it into a second image, write
// that to a file whose name decides the format, and stop the engine. Such a program is easy to write
// with a fixed path in each direction, one exception handler around the middle, an exit code of zero
// whatever happened, and no way to interrupt it. This probe does the same work the way a consumer of
// the engine has to do it, and is asked the questions a user of a command-line tool asks: did it do
// what it said, did a failure say which step failed, did pressing Ctrl+C leave a half-written file,
// and did the process let go of everything before it exited.
//
// It includes no consumer header, compiles no consumer source, and links no consumer target.
//
// **What a run does, in engine terms.**
//
//   1. builds the codec registry and an engine context from the invocation's thread and memory
//      settings;
//   2. decodes the input, whatever representation it holds, and widens it into a Float32 working
//      image in the unit interval;
//   3. measures each channel's median and median absolute deviation;
//   4. derives one linked automatic screen transfer from them and applies it into a second image;
//   5. converts the result to the representation the output format is written in, and publishes it
//      through the output transaction; and
//   6. releases every image and shuts the engine context down, which fails if anything is still held.

#pragma once

#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace probe::cli
{

/// What the process exits with. Each failing step has its own code, because a script calling a tool
/// branches on the code and a person reads the message.
enum class ExitCode : int
{
    /// The output was written.
    Success = 0,

    /// The command line did not describe a run: a missing path, an unknown option, an output name
    /// no installed codec writes.
    Usage = 2,

    /// The input could not be read.
    InputFailed = 3,

    /// Reading worked and processing did not.
    ProcessingFailed = 4,

    /// Processing worked and the output could not be written.
    OutputFailed = 5,

    /// The engine could not be started or stopped.
    EngineFailed = 6,

    /// The run was interrupted. The shell convention for a process stopped by an interrupt.
    Interrupted = 130
};

/// The options one run was given, after parsing.
struct Invocation
{
    /// The image to read.
    std::string inputPath{};

    /// The file to write. Its extension decides the container and the representation.
    std::string outputPath{};

    /// The thread budget, or zero for the host's.
    std::uint32_t threads = 0u;

    /// The memory budget in bytes, or zero for no budget beyond the engine's default.
    std::uint64_t memoryBudgetBytes = 0u;

    /// The automatic stretch's shadow clipping, in robust sigmas below the median.
    double shadowsClipping = -2.8;

    /// The automatic stretch's target background.
    double targetBackground = 0.25;
};

/// Parses a command line: `[--threads N] [--memory-mib N] input output`.
///
/// @param arguments the arguments after the program name.
/// @return the invocation, or `ErrorCode::InvalidArgument` naming what was wrong with the line.
[[nodiscard]] an::Result<Invocation> parseArguments(std::span<const std::string_view> arguments);

/// What one run did.
struct RunReport
{
    /// What the process would exit with.
    ExitCode exitCode = ExitCode::Success;

    /// What the process would print on its error stream; empty on success.
    std::string diagnostic{};

    /// The engine's code for the failure that decided the exit code, or `ErrorCode::Ok`.
    an::ErrorCode engineCode = an::ErrorCode::Ok;

    /// The container the output was written as, or `CodecFormat::Unknown` when nothing was written.
    an::CodecFormat outputFormat = an::CodecFormat::Unknown;

    /// The representation the output was written in.
    an::SampleType outputSampleType = an::SampleType::Float32;

    /// The screen transfer the automatic stretch derived.
    an::MidtonesTransferParameters stretch{};

    /// The lineage the written image carried after publication, oldest entry first.
    std::vector<std::string> outputLineage{};

    /// How many progress reports the engine made across the whole run.
    std::uint64_t progressReports = 0u;

    /// Whether the engine context shut down at the end, which it refuses while anything is held.
    bool engineShutDown = false;
};

/// Runs one invocation from start to exit.
///
/// @param invocation what to do.
/// @param stopToken the token the process's interrupt handler stops; every operation is started with
///        it, so an interrupt stops whichever step is running.
/// @param progress where the engine's progress goes as well as being counted, or null.
/// @return what the run did. Never throws: a command-line tool that dies with an exception tells a
///         script nothing.
[[nodiscard]] RunReport runOneShot(const Invocation& invocation, std::stop_token stopToken,
                                   an::ProgressSink* progress = nullptr) noexcept;

} // namespace probe::cli
