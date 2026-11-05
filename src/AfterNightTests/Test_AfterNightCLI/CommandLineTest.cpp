// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the command-line tool's parser accepts, what it refuses, and what it prints for --help and --version.
//
// A line that does not describe a run is refused before any engine exists, with a message that names what was wrong;
// a line that does describes every decision of the run, with the defaults the option table states.

#include "CommandLine.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// A command line from its words.
an::Result<an::cli::Command> parse(std::initializer_list<std::string_view> words)
{
    const std::vector<std::string_view> arguments(words);
    return an::cli::parseCommandLine(arguments);
}

/// A run's invocation; the case fails when the line is refused or asks for something else.
an::cli::Invocation runOf(std::initializer_list<std::string_view> words)
{
    const an::Result<an::cli::Command> parsed = parse(words);
    EXPECT_TRUE(parsed.hasValue()) << (parsed.hasValue() ? "" : parsed.status().message());
    if (!parsed.hasValue())
    {
        return {};
    }
    EXPECT_EQ(parsed->kind, an::cli::CommandKind::Run);
    return parsed->invocation;
}

/// The message a refused line carries; the case fails when the line was accepted.
std::string refusalOf(std::initializer_list<std::string_view> words)
{
    const an::Result<an::cli::Command> parsed = parse(words);
    EXPECT_FALSE(parsed.hasValue());
    if (parsed.hasValue())
    {
        return {};
    }
    EXPECT_EQ(parsed.status().code(), an::ErrorCode::InvalidArgument);
    return parsed.status().message();
}

} // namespace

TEST(CommandLineTest, TwoPathsAreARunWithEveryDefaultTheOptionTableStates)
{
    const an::cli::Invocation run = runOf({"in.fits", "out.tif"});
    EXPECT_EQ(run.inputPath, "in.fits");
    EXPECT_EQ(run.outputPath, "out.tif");
    EXPECT_FALSE(run.sampleType.has_value());
    EXPECT_EQ(run.threads, 0u);
    EXPECT_EQ(run.memoryBudgetMib, 0u);

    // The table states those defaults, which is what the usage text prints and the behavior register reads.
    for (const an::cli::OptionSpec& option : an::cli::kOptions)
    {
        if (option.name == "--threads" || option.name == "--memory-mib")
        {
            EXPECT_EQ(option.defaultValue, "0") << option.name;
        }
        if (option.name == "--sample-type")
        {
            EXPECT_EQ(option.defaultValue, "source");
        }
    }
}

TEST(CommandLineTest, EveryOptionIsReadInEitherSpellingAndInAnyPosition)
{
    const an::cli::Invocation spaced =
        runOf({"--threads", "3", "in.png", "--memory-mib", "512", "out.fits", "--sample-type", "UInt16"});
    EXPECT_EQ(spaced.threads, 3u);
    EXPECT_EQ(spaced.memoryBudgetMib, 512u);
    EXPECT_EQ(spaced.sampleType, an::SampleType::UInt16);
    EXPECT_EQ(spaced.inputPath, "in.png");
    EXPECT_EQ(spaced.outputPath, "out.fits");

    const an::cli::Invocation joined = runOf({"--threads=8", "--sample-type=float64", "a.xisf", "b.xisf"});
    EXPECT_EQ(joined.threads, 8u);
    EXPECT_EQ(joined.sampleType, an::SampleType::Float64);

    // Everything after `--` is a path, so a file whose name starts with dashes can be named.
    const an::cli::Invocation dashed = runOf({"--", "--odd.fits", "-out.tif"});
    EXPECT_EQ(dashed.inputPath, "--odd.fits");
    EXPECT_EQ(dashed.outputPath, "-out.tif");
}

TEST(CommandLineTest, EveryRepresentationIsNamedAsTheEngineNamesIt)
{
    for (const an::SampleType type :
         {an::SampleType::UInt8, an::SampleType::Int8, an::SampleType::UInt16, an::SampleType::Int16,
          an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64})
    {
        const std::string spelled = an::cli::sampleTypeArgument(type);
        EXPECT_EQ(an::cli::sampleTypeFromName(spelled), type) << spelled;
        EXPECT_EQ(an::cli::sampleTypeFromName(an::sampleTypeName(type)), type) << an::sampleTypeName(type);
        for (const char letter : spelled)
        {
            EXPECT_FALSE(letter >= 'A' && letter <= 'Z') << spelled << " is not spelled in lower case";
        }
    }
    EXPECT_EQ(an::cli::sampleTypeArgument(an::SampleType::UInt16), "uint16");
    EXPECT_EQ(an::cli::sampleTypeArgument(an::SampleType::Float32), "float32");
    EXPECT_FALSE(an::cli::sampleTypeFromName("uint64").has_value());
    EXPECT_FALSE(an::cli::sampleTypeFromName("").has_value());
    EXPECT_FALSE(an::cli::sampleTypeFromName("source").has_value());

    // `source` is the default spelled out, and asks for nothing.
    EXPECT_FALSE(runOf({"--sample-type", "source", "a.fits", "b.fits"}).sampleType.has_value());
}

TEST(CommandLineTest, ALineThatDescribesNoRunIsRefusedNamingWhatIsWrong)
{
    EXPECT_NE(refusalOf({}).find("input"), std::string::npos);
    EXPECT_NE(refusalOf({"in.fits"}).find("input"), std::string::npos);
    EXPECT_NE(refusalOf({"a.fits", "b.fits", "c.fits"}).find("input"), std::string::npos);
    EXPECT_NE(refusalOf({"--fast", "in.fits", "out.fits"}).find("--fast"), std::string::npos);
    EXPECT_NE(refusalOf({"-v", "in.fits", "out.fits"}).find("-v"), std::string::npos);
    EXPECT_NE(refusalOf({"in.fits", "out.fits", "--threads"}).find("--threads"), std::string::npos);
    EXPECT_NE(refusalOf({"--threads", "many", "in.fits", "out.fits"}).find("many"), std::string::npos);
    EXPECT_NE(refusalOf({"--threads", "-1", "in.fits", "out.fits"}).find("-1"), std::string::npos);
    EXPECT_NE(refusalOf({"--threads", "4294967296", "in.fits", "out.fits"}).find("--threads"), std::string::npos);
    EXPECT_NE(refusalOf({"--memory-mib", "1.5", "in.fits", "out.fits"}).find("1.5"), std::string::npos);
    EXPECT_NE(refusalOf({"--memory-mib", "17592186044416", "in.fits", "out.fits"}).find("--memory-mib"),
              std::string::npos);
    EXPECT_NE(refusalOf({"--sample-type", "uint64", "in.fits", "out.fits"}).find("uint64"), std::string::npos);
    EXPECT_NE(refusalOf({"--threads=", "in.fits", "out.fits"}).find("--threads"), std::string::npos);
    EXPECT_NE(refusalOf({"--threads", "2", "--threads", "3", "in.fits", "out.fits"}).find("twice"), std::string::npos);
    EXPECT_NE(refusalOf({"--help=yes"}).find("--help"), std::string::npos);
    EXPECT_NE(refusalOf({"", "out.fits"}).find("empty"), std::string::npos);

    // The largest budget that is still a byte count is accepted.
    EXPECT_EQ(runOf({"--memory-mib", std::to_string(an::cli::kMaxMemoryBudgetMib), "a.fits", "b.fits"}).memoryBudgetMib,
              an::cli::kMaxMemoryBudgetMib);
}

TEST(CommandLineTest, HelpAndVersionWinOverEverythingElseOnTheLine)
{
    for (const std::initializer_list<std::string_view> words :
         {std::initializer_list<std::string_view>{"--help"}, {"in.fits", "--help"}, {"--bogus", "--help"}, {"-h"}})
    {
        const an::Result<an::cli::Command> parsed = parse(words);
        ASSERT_TRUE(parsed.hasValue());
        EXPECT_EQ(parsed->kind, an::cli::CommandKind::Help);
    }
    const an::Result<an::cli::Command> version = parse({"--version", "in.fits"});
    ASSERT_TRUE(version.hasValue());
    EXPECT_EQ(version->kind, an::cli::CommandKind::Version);

    // After `--` they are paths.
    const an::cli::Invocation paths = runOf({"--", "--help", "--version"});
    EXPECT_EQ(paths.inputPath, "--help");
    EXPECT_EQ(paths.outputPath, "--version");
}

TEST(CommandLineTest, TheUsageTextStatesEveryOptionItsDefaultTheContainersAndEveryExitCode)
{
    const std::string text = an::cli::usageText();
    EXPECT_EQ(text.rfind("Usage: AfterNightCLI", 0u), 0u) << text;
    EXPECT_NE(text.find(an::cli::usageSynopsis()), std::string::npos);
    for (const an::cli::OptionSpec& option : an::cli::kOptions)
    {
        EXPECT_NE(text.find(option.name), std::string::npos) << option.name;
        if (!option.defaultValue.empty())
        {
            EXPECT_NE(text.find("default: " + std::string(option.defaultValue)), std::string::npos) << option.name;
        }
    }
    for (const char* container : {".fits", ".xisf", ".tif", ".png", ".jpg"})
    {
        EXPECT_NE(text.find(container), std::string::npos) << container;
    }
    for (const char* code : {"  0 ", "  2 ", "  3 ", "  4 ", "  5 ", "  6 ", "130 "})
    {
        EXPECT_NE(text.find(code), std::string::npos) << "exit code" << code;
    }
    // The stretch the tool applies is stated, with its constants.
    EXPECT_NE(text.find("-2.8"), std::string::npos);
    EXPECT_NE(text.find("0.25"), std::string::npos);
    EXPECT_EQ(text.back(), '\n');
}

TEST(CommandLineTest, TheVersionTextNamesTheLoadedEngineAndRuntime)
{
    const std::string text = an::cli::versionText();
    EXPECT_NE(text.find(an::engineVersionString()), std::string::npos) << text;
    EXPECT_NE(text.find("generation " + std::to_string(an::engineAbiGeneration())), std::string::npos) << text;
    EXPECT_NE(text.find("interface " + std::to_string(an::consumer::consumerRuntimeInterfaceVersion())),
              std::string::npos)
        << text;
    EXPECT_EQ(text.back(), '\n');
}
