// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One run of the command-line tool, from opening its engine session to shutting it down.
///
/// A run opens a consumer engine session with the invocation's thread and memory ceilings and the codecs the tool
/// installs, then:
///
/// 1. checks that an installed codec writes the output's container, before anything is read;
/// 2. probes the input for its representation, decides the output's representation from it, and refuses a container
///    that cannot state the colour filter array of an input that is a sensor's mosaic;
/// 3. decodes the input into an image asset;
/// 4. presents it through the engine's display presentation - each channel's exact median and median absolute
///    deviation, the automatic screen transfer with each channel its own, the result a Float32 working image - and
///    records the presentation's declared effect on the result's metadata;
/// 5. converts that into the output representation when it is not Float32, at full scale and to the nearest value;
/// 6. publishes it through the output transaction with an explicit output intent: the representation, the
///    container's compression, and for FITS and XISF the input's world coordinates, which the stretch keeps because
///    its pixel grid is the input's; and
/// 7. releases every asset and shuts the session down, which fails while anything is still held.
///
/// Every step is admitted as its own operation of the session and ends before the next starts, and every operation
/// carries the run's stop token, so an interrupt stops whichever step is running and the output transaction leaves
/// nothing behind. A failure is reported against the step that met it, as the exit code that step owns.
///
/// **A floating input is read as a fraction of full scale.** The engine decodes a floating FITS or XISF file as
/// physically scaled, because the container has no card for what its samples mean, and a display presentation has no
/// full scale to present a physical quantity against. This tool reads every floating sample of such an input as a
/// fraction of full scale, by restating the input's numeric domain, on purpose, and says so in its report.
#pragma once

#include "CommandLine.h"

#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace an
{
class CodecRegistry;
class ProgressSink;
} // namespace an

namespace an::cli
{

/// The codecs the tool installs: FITS, XISF, TIFF, PNG and JPEG, which it reads and writes, and camera RAW, which it
/// reads. A RAW frame from a colour filter array is read as the mosaic of camera codes it recorded, with no white
/// balance and no demosaicing, and is stretched as that mosaic.
///
/// @return the registry, or whatever building an adapter or the registry reported.
[[nodiscard]] an::Result<std::shared_ptr<const an::CodecRegistry>> createToolCodecRegistry();

/// What one run did.
struct RunReport
{
    /// What the process exits with.
    ExitCode exitCode = ExitCode::Success;

    /// What the process prints on its error stream: the step that failed and why. Empty on success.
    std::string diagnostic{};

    /// The engine's code for the failure that decided the exit code, or `ErrorCode::Ok`.
    an::ErrorCode engineCode = an::ErrorCode::Ok;

    /// The input's representation, once it was probed.
    an::SampleType inputSampleType = an::SampleType::Float32;

    /// The input's width in pixels, once it was probed.
    std::uint32_t width = 0u;

    /// The input's height in pixels, once it was probed.
    std::uint32_t height = 0u;

    /// The input's channels, once it was probed.
    std::uint32_t channels = 0u;

    /// Whether a floating input's physical or sensor-code domain was read as a fraction of full scale.
    bool inputDomainRestated = false;

    /// How many colour channels the presentation stretched: one for grey, three for colour; zero before it ran.
    std::uint32_t colorChannels = 0u;

    /// The screen transfer each colour channel took.
    std::array<an::MidtonesTransferParameters, 3u> transfer{};

    /// The container the output was written as, or `CodecFormat::Unknown` when nothing was written.
    an::CodecFormat outputFormat = an::CodecFormat::Unknown;

    /// The representation the output was written in.
    an::SampleType outputSampleType = an::SampleType::Float32;

    /// Whether the input's world coordinates were written with the output.
    bool worldCoordinatesWritten = false;

    /// The size of the file written, in bytes.
    std::uint64_t bytesWritten = 0u;

    /// The lineage the written image carried after publication, oldest entry first, as `operation: detail`.
    std::vector<std::string> outputLineage{};

    /// How many progress reports the engine made across the run.
    std::uint64_t progressReports = 0u;

    /// Whether the session shut down at the end, which it refuses while anything taken from it is still held.
    bool sessionShutDown = false;
};

/// Runs one invocation from opening the session to shutting it down.
///
/// @param invocation what to do.
/// @param stopToken the token the process's interrupt handler stops; every operation is admitted with it.
/// @param progress where the engine's progress goes as well as being counted, or null.
/// @return what the run did. Never throws: a tool that dies with an exception tells a script nothing.
[[nodiscard]] RunReport runOneShot(const Invocation& invocation, std::stop_token stopToken,
                                   an::ProgressSink* progress = nullptr) noexcept;

/// The line the tool prints when a run succeeded: what it wrote and how.
///
/// @param invocation the run.
/// @param report what it did.
/// @return the line, without a line ending.
[[nodiscard]] std::string describeRun(const Invocation& invocation, const RunReport& report);

} // namespace an::cli
