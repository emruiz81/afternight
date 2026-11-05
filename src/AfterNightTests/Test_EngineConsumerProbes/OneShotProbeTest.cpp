// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the command-line one-shot probe is asked, and why each question is one a user of a command-line
// tool asks rather than one a test of an operation asks.
//
// Four questions: did the file that came out say what was done to what went in; did each failure name
// the step that met it, in an exit code a script can branch on; did an interrupt leave a half-written
// file where the output should be; and did the process let go of the engine before it exited.

#include "OneShotProbe.h"
#include "ProbeSupport.h"

#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// A command line from its words.
///
/// @param words the arguments after the program name.
/// @return the parsed invocation; the test fails when parsing refused.
[[nodiscard]] probe::cli::Invocation invocationOf(std::initializer_list<std::string_view> words)
{
    const std::vector<std::string_view> arguments(words);
    const an::Result<probe::cli::Invocation> parsed = probe::cli::parseArguments(arguments);
    EXPECT_TRUE(parsed.hasValue()) << parsed.status().message();
    return parsed.hasValue() ? *parsed : probe::cli::Invocation{};
}

/// Writes the light frame every case reads.
///
/// @param scratch where to write it.
/// @param width the frame's width.
/// @param height the frame's height.
/// @return the path.
[[nodiscard]] std::string writeInput(const probe::ScratchDirectory& scratch, std::uint32_t width = 160u,
                                     std::uint32_t height = 120u)
{
    const std::string path = scratch.file("light.fits");
    an::Result<an::EngineContext> engine = probe::createProbeEngine(2u);
    EXPECT_TRUE(engine.hasValue()) << engine.status().message();
    if (engine.hasValue())
    {
        const an::Status written = probe::writeSensorFrame(
            *engine, path, probe::SensorFrameRequest{.width = width, .height = height, .seed = 11u});
        EXPECT_TRUE(written.ok()) << probe::describeFailure(written);
    }
    return path;
}

/// What a written file holds, read back through a context of its own.
struct ReadBack
{
    an::SampleType sampleType = an::SampleType::Float32;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    double median = 0.0;
    std::vector<std::string> cards{};
};

/// Opens a written file the way a second program would.
///
/// @param path the file.
/// @return what it holds; the test fails when it cannot be read.
[[nodiscard]] ReadBack readBack(const std::string& path)
{
    ReadBack result;
    an::Result<an::EngineContext> engine = probe::createProbeEngine(2u);
    if (!engine.hasValue())
    {
        ADD_FAILURE() << engine.status().message();
        return result;
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, an::ExecutionOptions{});
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(*engine, *execution, an::CodecReadRequest{.path = path});
    if (!decoded.hasValue())
    {
        ADD_FAILURE() << probe::describeFailure(decoded.status());
        return result;
    }
    const std::optional<an::RawFrameDescriptor> raw = decoded->frame.descriptor();
    const std::optional<an::ImageLayout> layout = decoded->frame.layout();
    result.sampleType = raw->residentSampleType().value_or(an::SampleType::Float32);
    result.width = raw->extent().width();
    result.height = raw->extent().height();
    const an::Result<an::RobustStatistics> statistics =
        an::computeImageRobustStatistics(*execution, result.sampleType, decoded->frame.bytes().data(), *layout, 0u,
                                         an::StatisticsSelection{}, an::RobustStatisticsRequest{});
    if (statistics.hasValue())
    {
        result.median = statistics->median;
    }
    for (const an::CodecCard& card : decoded->metadata.codec().cards())
    {
        result.cards.push_back(card.keyword() + "=" + card.value());
    }
    return result;
}

/// Whether one recorded text starts with a prefix.
///
/// @param texts what was recorded.
/// @param prefix what to look for.
/// @return true when an entry starts with it.
[[nodiscard]] bool anyStartsWith(const std::vector<std::string>& texts, std::string_view prefix)
{
    for (const std::string& text : texts)
    {
        if (text.rfind(prefix, 0u) == 0u)
        {
            return true;
        }
    }
    return false;
}

/// A progress sink that interrupts the run the first time a named stage reports.
class InterruptAt final : public an::ProgressSink
{
public:
    /// Builds a sink that stops a run.
    ///
    /// @param source the run's stop source.
    /// @param stage the stage whose first report triggers the interrupt.
    InterruptAt(std::stop_source& source, std::string stage) : m_source(source), m_stage(std::move(stage)) {}

    /// Requests the stop when the named stage reports.
    ///
    /// @param report what the operation said.
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        if (report.stage != nullptr && m_stage == report.stage)
        {
            m_source.request_stop();
        }
    }

private:
    /// The run's stop source.
    std::stop_source& m_source;

    /// The stage that triggers the stop.
    std::string m_stage;
};

} // namespace

TEST(OneShotProbeTest, TheFileThatComesOutSaysWhatWasDoneToTheFileThatWentIn)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const std::string input = writeInput(scratch);
    const std::string tiff = scratch.file("stretched.tif");
    const std::string fits = scratch.file("stretched.fits");

    std::stop_source interrupt;
    const probe::cli::RunReport toTiff =
        probe::cli::runOneShot(invocationOf({"--threads", "2", input, tiff}), interrupt.get_token());
    ASSERT_EQ(toTiff.exitCode, probe::cli::ExitCode::Success) << toTiff.diagnostic;
    EXPECT_EQ(toTiff.outputFormat, an::CodecFormat::Tiff);
    EXPECT_EQ(toTiff.outputSampleType, an::SampleType::UInt16);
    EXPECT_TRUE(toTiff.engineShutDown);

    // The decode and the encode both report progress; a command-line progress bar has something to
    // draw from for the two steps that touch the disk.
    EXPECT_GT(toTiff.progressReports, 0u);

    // The lineage of what was written names the stretch and the publication, in that order.
    EXPECT_TRUE(anyStartsWith(toTiff.outputLineage, "an::stretchImage"));
    ASSERT_FALSE(toTiff.outputLineage.empty());
    EXPECT_NE(toTiff.outputLineage.back().find("stretched.tif"), std::string::npos) << toTiff.outputLineage.back();

    // The automatic stretch put the background where it was asked to: a quarter of full scale.
    const ReadBack tiffBack = readBack(tiff);
    EXPECT_EQ(tiffBack.sampleType, an::SampleType::UInt16);
    EXPECT_EQ(tiffBack.width, 160u);
    EXPECT_EQ(tiffBack.height, 120u);
    EXPECT_NEAR(tiffBack.median / 65535.0, 0.25, 0.03);

    // A FITS output keeps the capture program's own cards: an output sized from the input without copying the
    // metadata would drop them.
    const probe::cli::RunReport toFits = probe::cli::runOneShot(invocationOf({input, fits}), interrupt.get_token());
    ASSERT_EQ(toFits.exitCode, probe::cli::ExitCode::Success) << toFits.diagnostic;
    EXPECT_EQ(toFits.outputSampleType, an::SampleType::Float32);
    const ReadBack fitsBack = readBack(fits);
    EXPECT_EQ(fitsBack.sampleType, an::SampleType::Float32);
    EXPECT_TRUE(anyStartsWith(fitsBack.cards, "OBJECT=")) << "the target name did not survive the run";
    EXPECT_TRUE(anyStartsWith(fitsBack.cards, "IMAGETYP=")) << "the exposure kind did not survive the run";
    EXPECT_NEAR(fitsBack.median, 0.25, 0.03);
}

TEST(OneShotProbeTest, EveryFailureNamesTheStepThatMetIt)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const std::string input = writeInput(scratch);
    std::stop_source interrupt;

    // A command line that does not describe a run is refused before an engine exists.
    const std::array<std::string_view, 1u> onlyOne{"in.fits"};
    EXPECT_FALSE(probe::cli::parseArguments(onlyOne).hasValue());
    const std::array<std::string_view, 3u> unknown{"--fast", "in.fits", "out.fits"};
    EXPECT_FALSE(probe::cli::parseArguments(unknown).hasValue());

    // An output name no installed codec writes is a usage error, decided before the input is read.
    const probe::cli::RunReport bitmap =
        probe::cli::runOneShot(invocationOf({input, scratch.file("out.bmp")}), interrupt.get_token());
    EXPECT_EQ(bitmap.exitCode, probe::cli::ExitCode::Usage) << bitmap.diagnostic;
    EXPECT_EQ(bitmap.progressReports, 0u);
    EXPECT_TRUE(bitmap.engineShutDown);

    // An input that is not there fails the read, and says so.
    const probe::cli::RunReport missing = probe::cli::runOneShot(
        invocationOf({scratch.file("absent.fits"), scratch.file("out.fits")}), interrupt.get_token());
    EXPECT_EQ(missing.exitCode, probe::cli::ExitCode::InputFailed) << missing.diagnostic;
    EXPECT_FALSE(missing.diagnostic.empty());
    EXPECT_TRUE(missing.engineShutDown);

    // A destination directory that does not exist fails the write, after the processing succeeded.
    const probe::cli::RunReport nowhere = probe::cli::runOneShot(
        invocationOf({input, scratch.file("no/such/directory/out.fits")}), interrupt.get_token());
    EXPECT_EQ(nowhere.exitCode, probe::cli::ExitCode::OutputFailed) << nowhere.diagnostic;
    EXPECT_TRUE(nowhere.engineShutDown);

    // A memory budget smaller than the frame is a refusal with a code, not a crash, and the step it
    // names is the first one that needed the memory.
    const std::string large = scratch.file("large.fits");
    {
        an::Result<an::EngineContext> engine = probe::createProbeEngine(2u);
        ASSERT_TRUE(engine.hasValue());
        ASSERT_TRUE(
            probe::writeSensorFrame(*engine, large, probe::SensorFrameRequest{.width = 1024u, .height = 1024u}).ok());
    }
    const probe::cli::RunReport starved = probe::cli::runOneShot(
        invocationOf({"--memory-mib", "1", large, scratch.file("large_out.fits")}), interrupt.get_token());
    EXPECT_EQ(starved.exitCode, probe::cli::ExitCode::InputFailed) << starved.diagnostic;
    EXPECT_EQ(starved.engineCode, an::ErrorCode::MemoryBudgetExceeded) << starved.diagnostic;
    EXPECT_TRUE(starved.engineShutDown);
}

TEST(OneShotProbeTest, AnInterruptDuringTheWriteLeavesNothingWhereTheOutputWouldHaveBeen)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const std::string input = writeInput(scratch);
    const std::string output = scratch.file("interrupted.tif");

    std::stop_source interrupt;
    InterruptAt sink(interrupt, "encode TIFF");
    const probe::cli::RunReport report =
        probe::cli::runOneShot(invocationOf({input, output}), interrupt.get_token(), &sink);

    EXPECT_EQ(report.exitCode, probe::cli::ExitCode::Interrupted) << report.diagnostic;
    EXPECT_EQ(report.engineCode, an::ErrorCode::Cancelled);
    EXPECT_TRUE(report.engineShutDown);

    // No destination, and no pending sibling beside where it would have been: the only file in the
    // directory is the input.
    EXPECT_FALSE(std::filesystem::exists(
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(output.data()), output.size()))));
    EXPECT_EQ(scratch.fileNames(), (std::vector<std::string>{"light.fits"}));
}
