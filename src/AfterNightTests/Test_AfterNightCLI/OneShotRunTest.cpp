// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// One run of the command-line tool, in this process: what it writes, what the written file says about what went in,
// which representation each container gets, which step each failure is reported against, and that an interrupt
// leaves nothing behind.
//
// The stretch is held to recorded expectations for the same inputs, checked in as neutral records, and read back from
// the tool's FITS output by the neutral reader - so the engine never marks its own work.

#include "CliTestSupport.h"
#include "CommandLine.h"
#include "OneShotRun.h"
#include "WrittenImageProbe.h"

#include "fixtures/common/PresentationCases.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace cli = an::cli;
namespace support = an::cli::tests;
namespace fixtures = an::tests::fixtures;

/// A run's invocation from its words; the case fails when the line is refused.
cli::Invocation invocationOf(std::initializer_list<std::string_view> words)
{
    const std::vector<std::string_view> arguments(words);
    const an::Result<cli::Command> parsed = cli::parseCommandLine(arguments);
    EXPECT_TRUE(parsed.hasValue()) << (parsed.hasValue() ? "" : parsed.status().message());
    return parsed.hasValue() ? parsed->invocation : cli::Invocation{};
}

/// Runs an invocation with a token nothing stops.
cli::RunReport run(const cli::Invocation& invocation, an::ProgressSink* progress = nullptr)
{
    std::stop_source never;
    return cli::runOneShot(invocation, never.get_token(), progress);
}

/// A progress sink that stops a run the first time a named stage reports.
class InterruptAt final : public an::ProgressSink
{
public:
    InterruptAt(std::stop_source& source, std::string stage) : m_source(source), m_stage(std::move(stage)) {}

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        if (report.stage != nullptr && m_stage == report.stage)
        {
            m_source.request_stop();
        }
    }

private:
    std::stop_source& m_source;
    std::string m_stage;
};

/// Whether a card's value, read as a number, is this number.
bool numericCard(const fixtures::FixtureFitsFile& file, std::string_view keyword, double expected)
{
    const std::optional<std::string> value = file.card(keyword);
    if (!value.has_value())
    {
        return false;
    }
    const std::optional<double> parsed = fixtures::parseDouble(*value);
    return parsed.has_value() && std::abs(*parsed - expected) <= 1e-9 * (std::max)(1.0, std::abs(expected));
}

/// A light frame with what a capture program and a plate solver write into its header.
fixtures::FixtureImage solvedLightFrame()
{
    fixtures::FixtureImage image;
    image.name = "cli-solved-light-uint16";
    image.width = 48u;
    image.height = 32u;
    image.channels = fixtures::FixtureChannels::Mono;
    image.sampleType = fixtures::FixtureSampleType::UInt16;
    image.domain = fixtures::FixtureDomain::SensorCode;
    image.pattern = fixtures::FixturePattern::GradientNoise;
    image.seed = 7u;
    image.levels = {{.base = 1200.0, .perColumn = 6.0, .perRow = 4.0, .amplitude = 90.0}};
    using Kind = fixtures::FixtureCardKind;
    image.cards = {
        {"OBJECT", "M 83", "target", Kind::Text},
        {"INSTRUME", "ZWO ASI2600MM Pro", "camera", Kind::Text},
        {"TELESCOP", "Esprit 100", "telescope", Kind::Text},
        {"FILTER", "L", "filter", Kind::Text},
        {"EXPTIME", "120", "exposure in seconds", Kind::Real},
        {"DATE-OBS", "2026-04-11T03:12:45.250", "exposure start, UTC", Kind::Text},
        {"IMAGETYP", "Light Frame", "frame kind", Kind::Text},
        {"FOCALLEN", "550", "focal length, mm", Kind::Real},
        {"CTYPE1", "RA---TAN", "", Kind::Text},
        {"CTYPE2", "DEC--TAN", "", Kind::Text},
        {"CRVAL1", "204.2538", "", Kind::Real},
        {"CRVAL2", "-29.8658", "", Kind::Real},
        {"CRPIX1", "24.5", "", Kind::Real},
        {"CRPIX2", "16.5", "", Kind::Real},
        {"CD1_1", "-0.000384", "", Kind::Real},
        {"CD1_2", "0.0", "", Kind::Real},
        {"CD2_1", "0.0", "", Kind::Real},
        {"CD2_2", "0.000384", "", Kind::Real},
        {"RADESYS", "ICRS", "", Kind::Text},
    };
    return image;
}

class OneShotRunTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scratch.valid());
    }

    /// Writes a catalogue fixture as `<name>.fits` and returns the path.
    std::string input(std::string_view catalogueName)
    {
        const std::optional<support::MaterializedFixture> fixture = support::materialize(catalogueName);
        const std::string path = m_scratch.file(std::string(catalogueName) + ".fits");
        if (fixture.has_value())
        {
            EXPECT_TRUE(support::writeInput(*fixture, support::fromUtf8(path)));
        }
        return path;
    }

    support::ScratchDirectory m_scratch;
};

} // namespace

TEST_F(OneShotRunTest, ItsStretchIsTheRecordForEveryRecordedInput)
{
    std::size_t compared = 0u;
    for (const fixtures::PresentationCase& item : fixtures::presentationCases())
    {
        if (item.path != fixtures::PresentationPath::CommandLine)
        {
            continue;
        }
        SCOPED_TRACE(item.id());
        const std::optional<support::MaterializedFixture> fixture = support::materialize(item.input);
        ASSERT_TRUE(fixture.has_value());
        const std::string in = m_scratch.file(item.input + ".fits");
        const std::string out = m_scratch.file(item.input + ".stretched.fits");
        ASSERT_TRUE(support::writeInput(*fixture, support::fromUtf8(in)));

        const cli::RunReport report = run(invocationOf({in, out}));
        ASSERT_EQ(report.exitCode, cli::ExitCode::Success) << report.diagnostic;
        EXPECT_TRUE(report.sessionShutDown);

        // The stretch is written in the input's representation by default.
        const std::optional<fixtures::FixtureFitsFile> written = support::readFitsOutput(support::fromUtf8(out));
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->samples.sampleType(), fixture->image.sampleType);

        const std::optional<fixtures::Expectation> record =
            support::expectationRecord(fixtures::kPresentationExpectationFile, item.id());
        ASSERT_TRUE(record.has_value());
        ASSERT_TRUE(fixtures::expectationDescribes(*record, fixture->image, fixture->samples))
            << "the record was captured from another input";
        const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(*record, written->samples);
        EXPECT_TRUE(comparison.matched) << comparison.reason << "; " << comparison.differingSamples
                                        << " samples differ, the largest by " << comparison.largestDifference;
        ++compared;
    }
    EXPECT_EQ(compared, 2u) << "every recorded command-line input is compared";
}

TEST_F(OneShotRunTest, AFloatingInputIsReadAsAFractionOfFullScaleAndTheReportSaysSo)
{
    const cli::RunReport floating = run(invocationOf({input("scene-mono-float32"), m_scratch.file("float.fits")}));
    ASSERT_EQ(floating.exitCode, cli::ExitCode::Success) << floating.diagnostic;
    EXPECT_TRUE(floating.inputDomainRestated);
    EXPECT_EQ(floating.inputSampleType, an::SampleType::Float32);

    const cli::RunReport integer = run(invocationOf({input("scene-rgb-uint16"), m_scratch.file("integer.fits")}));
    ASSERT_EQ(integer.exitCode, cli::ExitCode::Success) << integer.diagnostic;
    EXPECT_FALSE(integer.inputDomainRestated);
    EXPECT_EQ(integer.colorChannels, 3u);
    EXPECT_EQ(integer.channels, 3u);
    EXPECT_EQ(integer.width, 12u);
    EXPECT_EQ(integer.height, 8u);

    // Each colour channel took its own transfer: the stretch is never linked.
    EXPECT_NE(integer.transfer[0].midtones, integer.transfer[2].midtones);
}

TEST_F(OneShotRunTest, TheOutputCarriesTheInputsAcquisitionRecordAndSolutionAndNoneOfItsOtherCards)
{
    const std::optional<support::MaterializedFixture> light = support::materialize(solvedLightFrame());
    ASSERT_TRUE(light.has_value());
    const std::string in = m_scratch.file("light.fits");
    const std::string out = m_scratch.file("light.stretched.fits");
    ASSERT_TRUE(support::writeInput(*light, support::fromUtf8(in)));

    const cli::RunReport report = run(invocationOf({in, out}));
    ASSERT_EQ(report.exitCode, cli::ExitCode::Success) << report.diagnostic;
    const std::optional<fixtures::FixtureFitsFile> written = support::readFitsOutput(support::fromUtf8(out));
    ASSERT_TRUE(written.has_value());

    // The acquisition record survives the stretch: what was observed, with what, when and for how long.
    EXPECT_EQ(written->card("OBJECT").value_or(""), "M 83");
    EXPECT_EQ(written->card("INSTRUME").value_or(""), "ZWO ASI2600MM Pro");
    EXPECT_EQ(written->card("TELESCOP").value_or(""), "Esprit 100");
    EXPECT_EQ(written->card("FILTER").value_or(""), "L");
    EXPECT_TRUE(numericCard(*written, "EXPOSURE", 120.0) || numericCard(*written, "EXPTIME", 120.0));
    EXPECT_EQ(written->card("DATE-OBS").value_or("").rfind("2026-04-11T03:12:45", 0u), 0u);

    // The pixel grid is the input's, so the plate solution still describes it, and the tool writes it.
    EXPECT_TRUE(report.worldCoordinatesWritten);
    EXPECT_EQ(written->card("CTYPE1").value_or(""), "RA---TAN");
    EXPECT_TRUE(numericCard(*written, "CRVAL1", 204.2538));
    EXPECT_TRUE(numericCard(*written, "CRVAL2", -29.8658));
    EXPECT_TRUE(numericCard(*written, "CRPIX1", 24.5));

    // A card the engine does not read into a typed record does not describe a display stretch, so it is not
    // carried.
    EXPECT_FALSE(written->card("IMAGETYP").has_value());
    EXPECT_FALSE(written->card("FOCALLEN").has_value());

    // What was done is in the written image's lineage, the stretch first and the publication last.
    ASSERT_GE(report.outputLineage.size(), 2u);
    EXPECT_EQ(report.outputLineage.front().rfind("an::presentImage", 0u), 0u) << report.outputLineage.front();
    EXPECT_NE(report.outputLineage.back().find("light.stretched.fits"), std::string::npos)
        << report.outputLineage.back();
    EXPECT_GT(report.bytesWritten, 0u);

    // A container that holds no FITS keywords is written without them.
    const cli::RunReport png = run(invocationOf({in, m_scratch.file("light.png")}));
    ASSERT_EQ(png.exitCode, cli::ExitCode::Success) << png.diagnostic;
    EXPECT_FALSE(png.worldCoordinatesWritten);

    // An input with no solution is written with none, and that is no failure.
    const cli::RunReport unsolved = run(invocationOf({input("scene-rgb-uint16"), m_scratch.file("unsolved.fits")}));
    ASSERT_EQ(unsolved.exitCode, cli::ExitCode::Success) << unsolved.diagnostic;
    EXPECT_FALSE(unsolved.worldCoordinatesWritten);
}

TEST_F(OneShotRunTest, EachContainerIsWrittenInTheRepresentationTheRunDecided)
{
    struct Case
    {
        std::string input;
        std::string output;
        std::vector<std::string_view> options;
        an::CodecFormat format;
        an::SampleType sampleType;
    };
    const std::string colour = input("scene-rgb-uint16");
    const std::string mono = input("scene-mono-float32");
    const std::vector<Case> cases{
        {colour, "colour.tif", {}, an::CodecFormat::Tiff, an::SampleType::UInt16},
        {colour, "colour.png", {}, an::CodecFormat::Png, an::SampleType::UInt16},
        {colour, "colour.jpg", {}, an::CodecFormat::Jpeg, an::SampleType::UInt8},
        {colour, "colour.xisf", {}, an::CodecFormat::Xisf, an::SampleType::UInt16},
        {colour, "colour-float.tif", {"--sample-type", "float32"}, an::CodecFormat::Tiff, an::SampleType::Float32},
        {colour, "colour-bytes.png", {"--sample-type", "uint8"}, an::CodecFormat::Png, an::SampleType::UInt8},
        {mono, "mono.png", {}, an::CodecFormat::Png, an::SampleType::UInt16},
        {mono, "mono.xisf", {}, an::CodecFormat::Xisf, an::SampleType::Float32},
        {mono, "mono-signed.fits", {"--sample-type", "int16"}, an::CodecFormat::Fits, an::SampleType::Int16},
    };
    for (const Case& item : cases)
    {
        SCOPED_TRACE(item.output);
        cli::Invocation invocation;
        invocation.inputPath = item.input;
        invocation.outputPath = m_scratch.file(item.output);
        if (!item.options.empty())
        {
            invocation.sampleType = cli::sampleTypeFromName(item.options[1]);
        }
        const cli::RunReport report = run(invocation);
        ASSERT_EQ(report.exitCode, cli::ExitCode::Success) << report.diagnostic;
        EXPECT_EQ(report.outputFormat, item.format);
        EXPECT_EQ(report.outputSampleType, item.sampleType);
        const std::optional<support::WrittenImage> written = support::probeWritten(m_scratch.path(item.output));
        ASSERT_TRUE(written.has_value());
        EXPECT_EQ(written->sampleType, item.sampleType);
        EXPECT_EQ(written->width, 12u);
        EXPECT_EQ(written->height, 8u);
        EXPECT_EQ(written->channels, report.channels);
    }

    // A representation change is in the lineage between the stretch and the publication.
    const cli::RunReport converted = run(invocationOf({colour, m_scratch.file("again.tif")}));
    ASSERT_EQ(converted.exitCode, cli::ExitCode::Success) << converted.diagnostic;
    ASSERT_EQ(converted.outputLineage.size(), 3u);
    EXPECT_NE(converted.outputLineage[1].find("UInt16"), std::string::npos) << converted.outputLineage[1];
}

TEST_F(OneShotRunTest, ARepresentationTheContainerCannotHoldIsRefusedBeforeTheInputIsRead)
{
    const std::string in = input("scene-rgb-uint16");
    const cli::RunReport report = run(invocationOf({"--sample-type", "float32", in, m_scratch.file("out.png")}));
    EXPECT_EQ(report.exitCode, cli::ExitCode::Usage) << report.diagnostic;
    EXPECT_NE(report.diagnostic.find("uint8 and uint16"), std::string::npos) << report.diagnostic;
    EXPECT_EQ(report.progressReports, 0u) << "the input was decoded before the refusal";
    EXPECT_TRUE(report.sessionShutDown);
    EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"scene-rgb-uint16.fits"}));
}

TEST_F(OneShotRunTest, AMosaicIsStretchedAsTheMosaicItIsAndWrittenOnlyWhereItsFilterArrayCanBeStated)
{
    const std::string mosaic = input("mosaic-rggb-uint16");

    // A container that can state the filter array holds the stretched mosaic, which still names its pattern.
    const cli::RunReport kept = run(invocationOf({mosaic, m_scratch.file("mosaic.fits")}));
    ASSERT_EQ(kept.exitCode, cli::ExitCode::Success) << kept.diagnostic;
    EXPECT_EQ(kept.channels, 1u);
    EXPECT_EQ(kept.colorChannels, 1u);
    const std::optional<fixtures::FixtureFitsFile> written = support::readFitsOutput(m_scratch.path("mosaic.fits"));
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->cfa, fixtures::FixtureCfa::Rggb);

    // One that cannot is refused before the mosaic is decoded, naming the containers that can.
    for (const char* output : {"mosaic.png", "mosaic.jpg"})
    {
        const cli::RunReport refused = run(invocationOf({mosaic, m_scratch.file(output)}));
        EXPECT_EQ(refused.exitCode, cli::ExitCode::Usage) << refused.diagnostic;
        EXPECT_NE(refused.diagnostic.find("filter array"), std::string::npos) << refused.diagnostic;
        EXPECT_EQ(refused.progressReports, 0u) << "the mosaic was decoded before the refusal";
        EXPECT_TRUE(refused.sessionShutDown);
    }
    EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"mosaic-rggb-uint16.fits", "mosaic.fits"}));
}

TEST_F(OneShotRunTest, EveryFailureIsReportedAgainstTheStepThatMetIt)
{
    const std::string in = input("scene-rgb-uint16");

    // A container no installed codec writes is a usage failure, decided before the input is read.
    const cli::RunReport bitmap = run(invocationOf({in, m_scratch.file("out.bmp")}));
    EXPECT_EQ(bitmap.exitCode, cli::ExitCode::Usage) << bitmap.diagnostic;
    EXPECT_NE(bitmap.diagnostic.find("out.bmp"), std::string::npos) << bitmap.diagnostic;
    EXPECT_EQ(bitmap.progressReports, 0u);
    EXPECT_TRUE(bitmap.sessionShutDown);

    // So is camera RAW, which the tool reads and does not write.
    const cli::RunReport raw = run(invocationOf({in, m_scratch.file("out.cr2")}));
    EXPECT_EQ(raw.exitCode, cli::ExitCode::Usage) << raw.diagnostic;

    // An input that is not there, or not an image, fails the read and names it.
    const cli::RunReport missing = run(invocationOf({m_scratch.file("absent.fits"), m_scratch.file("out.fits")}));
    EXPECT_EQ(missing.exitCode, cli::ExitCode::InputFailed) << missing.diagnostic;
    EXPECT_NE(missing.diagnostic.find("absent.fits"), std::string::npos) << missing.diagnostic;
    EXPECT_TRUE(missing.sessionShutDown);
    {
        std::ofstream text(m_scratch.path("notes.fits"), std::ios::binary);
        text << "these are not the bytes of a FITS file\n";
    }
    const cli::RunReport garbage = run(invocationOf({m_scratch.file("notes.fits"), m_scratch.file("out.fits")}));
    EXPECT_EQ(garbage.exitCode, cli::ExitCode::InputFailed) << garbage.diagnostic;

    // A destination directory that does not exist fails the write, after the stretch succeeded.
    const cli::RunReport nowhere = run(invocationOf({in, m_scratch.file("no/such/directory/out.fits")}));
    EXPECT_EQ(nowhere.exitCode, cli::ExitCode::OutputFailed) << nowhere.diagnostic;
    EXPECT_EQ(nowhere.colorChannels, 3u) << "the stretch ran before the write failed";
    EXPECT_TRUE(nowhere.sessionShutDown);

    // A thread ceiling no engine context may have is a usage failure.
    const cli::RunReport threads = run(invocationOf({"--threads", "1000000", in, m_scratch.file("out.fits")}));
    EXPECT_EQ(threads.exitCode, cli::ExitCode::Usage) << threads.diagnostic;
    EXPECT_NE(threads.diagnostic.find("engine"), std::string::npos) << threads.diagnostic;

    // Nothing failed beside the inputs.
    EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"notes.fits", "scene-rgb-uint16.fits"}));
}

TEST_F(OneShotRunTest, AMemoryBudgetIsARefusalWithACodeAtTheFirstStepThatNeededTheMemory)
{
    // A megapixel of sixteen-bit codes: decoding needs two mebibytes, the Float32 stretch four more.
    fixtures::FixtureImage frame;
    frame.name = "cli-megapixel-uint16";
    frame.width = 1024u;
    frame.height = 1024u;
    frame.sampleType = fixtures::FixtureSampleType::UInt16;
    frame.domain = fixtures::FixtureDomain::SensorCode;
    frame.pattern = fixtures::FixturePattern::Noise;
    frame.seed = 3u;
    frame.levels = {{.base = 12000.0, .amplitude = 6000.0}};
    const std::optional<support::MaterializedFixture> fixture = support::materialize(frame);
    ASSERT_TRUE(fixture.has_value());
    const std::string in = m_scratch.file("megapixel.fits");
    ASSERT_TRUE(support::writeInput(*fixture, support::fromUtf8(in)));

    const cli::RunReport starved = run(invocationOf({"--memory-mib", "1", in, m_scratch.file("a.fits")}));
    EXPECT_EQ(starved.exitCode, cli::ExitCode::InputFailed) << starved.diagnostic;
    EXPECT_EQ(starved.engineCode, an::ErrorCode::MemoryBudgetExceeded) << starved.diagnostic;
    EXPECT_TRUE(starved.sessionShutDown);

    const cli::RunReport stretched = run(invocationOf({"--memory-mib", "3", in, m_scratch.file("b.fits")}));
    EXPECT_EQ(stretched.exitCode, cli::ExitCode::ProcessingFailed) << stretched.diagnostic;
    EXPECT_EQ(stretched.engineCode, an::ErrorCode::MemoryBudgetExceeded) << stretched.diagnostic;
    EXPECT_TRUE(stretched.sessionShutDown);

    EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"megapixel.fits"}));
}

TEST_F(OneShotRunTest, AnInterruptStopsTheStepThatIsRunningAndLeavesNothingWhereTheOutputWouldHaveBeen)
{
    const std::string in = input("scene-rgb-uint16");
    for (const char* stage : {"decode FITS", "encode TIFF"})
    {
        SCOPED_TRACE(stage);
        std::stop_source interrupt;
        InterruptAt sink(interrupt, stage);
        const cli::RunReport report =
            cli::runOneShot(invocationOf({in, m_scratch.file("interrupted.tif")}), interrupt.get_token(), &sink);
        EXPECT_EQ(report.exitCode, cli::ExitCode::Interrupted) << report.diagnostic;
        EXPECT_EQ(report.engineCode, an::ErrorCode::Cancelled);
        EXPECT_NE(report.diagnostic.find("interrupted"), std::string::npos) << report.diagnostic;
        EXPECT_TRUE(report.sessionShutDown);
        EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"scene-rgb-uint16.fits"}));
    }

    // An interrupt before the run starts stops it before anything is read.
    std::stop_source early;
    early.request_stop();
    const cli::RunReport before = cli::runOneShot(invocationOf({in, m_scratch.file("never.tif")}), early.get_token());
    EXPECT_EQ(before.exitCode, cli::ExitCode::Interrupted) << before.diagnostic;
    EXPECT_EQ(before.progressReports, 0u);
    EXPECT_TRUE(before.sessionShutDown);
    EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"scene-rgb-uint16.fits"}));
}

TEST_F(OneShotRunTest, TheLineASuccessfulRunPrintsSaysWhatWasWrittenAndHow)
{
    const cli::Invocation invocation = invocationOf({input("scene-rgb-uint16"), m_scratch.file("said.png")});
    const cli::RunReport report = run(invocation);
    ASSERT_EQ(report.exitCode, cli::ExitCode::Success) << report.diagnostic;
    const std::string line = cli::describeRun(invocation, report);
    EXPECT_NE(line.find("said.png"), std::string::npos) << line;
    EXPECT_NE(line.find("PNG"), std::string::npos) << line;
    EXPECT_NE(line.find("uint16"), std::string::npos) << line;
    EXPECT_NE(line.find("12 x 8"), std::string::npos) << line;
    EXPECT_NE(line.find("3 channels"), std::string::npos) << line;
    EXPECT_EQ(line.find('\n'), std::string::npos);
    EXPECT_GT(report.progressReports, 0u) << "the decode and the encode report progress";
}

TEST(ToolCodecsTest, TheToolReadsCameraRawAndWritesTheFiveContainersItNames)
{
    const an::Result<std::shared_ptr<const an::CodecRegistry>> registry = cli::createToolCodecRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();
    for (const an::CodecFormat format : {an::CodecFormat::Fits, an::CodecFormat::Xisf, an::CodecFormat::Tiff,
                                         an::CodecFormat::Png, an::CodecFormat::Jpeg})
    {
        const std::optional<an::CodecCapabilities> capabilities = (*registry)->capabilities(format);
        ASSERT_TRUE(capabilities.has_value()) << an::codecFormatName(format);
        EXPECT_NE(capabilities->decodeSamples, 0u) << an::codecFormatName(format);
        EXPECT_NE(capabilities->encodeSamples, 0u) << an::codecFormatName(format);
    }
    const std::optional<an::CodecCapabilities> raw = (*registry)->capabilities(an::CodecFormat::Raw);
    ASSERT_TRUE(raw.has_value());
    EXPECT_NE(raw->decodeSamples, 0u);
    EXPECT_EQ(raw->encodeSamples, 0u);
}
