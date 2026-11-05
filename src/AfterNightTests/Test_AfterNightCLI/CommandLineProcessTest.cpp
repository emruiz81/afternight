// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command-line tool's own binary, run the way a script runs it: what it prints, what it exits with, where it
// logs, a path no code page spells, and Ctrl+Break or SIGINT in the middle of a write.
//
// These cases launch the binary beside this suite, so they prove what ships rather than what this suite compiles:
// the entry point's argument conversion, its version check, its logger, its interrupt handler and its exit codes.
// The child inherits this suite's storage root, so it logs there and never into the user's own data directory.

#include "CliTestSupport.h"
#include "CommandLine.h"
#include "OneShotRun.h"

#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{

namespace cli = an::cli;
namespace support = an::cli::tests;

constexpr int kSuccess = static_cast<int>(cli::ExitCode::Success);
constexpr int kUsage = static_cast<int>(cli::ExitCode::Usage);
constexpr int kInputFailed = static_cast<int>(cli::ExitCode::InputFailed);
constexpr int kOutputFailed = static_cast<int>(cli::ExitCode::OutputFailed);
constexpr int kInterrupted = static_cast<int>(cli::ExitCode::Interrupted);

class CommandLineProcessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scratch.valid());
        ASSERT_TRUE(std::filesystem::exists(support::toolPath()))
            << support::utf8(support::toolPath()) << " is not built beside this suite";
    }

    /// Writes a catalogue fixture under a name and returns its path.
    std::string input(std::string_view catalogueName, std::string_view fileName)
    {
        const std::optional<support::MaterializedFixture> fixture = support::materialize(catalogueName);
        const std::string path = m_scratch.file(fileName);
        if (fixture.has_value())
        {
            EXPECT_TRUE(support::writeInput(*fixture, support::fromUtf8(path)));
        }
        return path;
    }

    support::ScratchDirectory m_scratch;
};

#ifdef _WIN32
/// Whether Windows lets a process that declares itself long-path aware use a path past its old 260-character limit;
/// where it does not, no process can.
[[nodiscard]] bool longPathsEnabled()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value == 1u;
}
#endif

/// The path as this process reaches it past the old limit: through the extended-length prefix on Windows, where this
/// suite does not declare itself long-path aware, and as it is elsewhere.
[[nodiscard]] std::filesystem::path reachable(const std::filesystem::path& path)
{
#ifdef _WIN32
    return std::filesystem::path(L"\\\\?\\" + std::filesystem::absolute(path).wstring());
#else
    return path;
#endif
}

} // namespace

TEST_F(CommandLineProcessTest, HelpPrintsTheUsageTextAndExitsZero)
{
    for (const char* spelling : {"--help", "-h"})
    {
        const support::ToolRun run = support::runTool({spelling});
        ASSERT_TRUE(run.finished);
        EXPECT_EQ(run.exitCode, kSuccess) << run.standardError;
        EXPECT_EQ(run.standardOutput, cli::usageText());
        EXPECT_TRUE(run.standardError.empty()) << run.standardError;
    }
}

TEST_F(CommandLineProcessTest, VersionNamesTheEngineAndTheRuntimeItLoaded)
{
    const support::ToolRun run = support::runTool({"--version"});
    ASSERT_TRUE(run.finished);
    EXPECT_EQ(run.exitCode, kSuccess) << run.standardError;
    // The child loaded the libraries beside it, which are the ones this suite loaded.
    EXPECT_EQ(run.standardOutput, cli::versionText());
}

#if defined(__linux__)
// A deployed tool is run where it lies, with no library path naming the directory its libraries are in, so it finds
// them beside itself through its own run path, as every other executable of the product does.
TEST_F(CommandLineProcessTest, ItFindsTheLibrariesBesideItWithNoLibraryPathNamingThem)
{
    const char* const inherited = std::getenv("LD_LIBRARY_PATH");
    const std::optional<std::string> saved =
        inherited != nullptr ? std::optional<std::string>(inherited) : std::nullopt;
    unsetenv("LD_LIBRARY_PATH");
    const support::ToolRun run = support::runTool({"--version"});
    if (saved.has_value())
    {
        setenv("LD_LIBRARY_PATH", saved->c_str(), 1);
    }
    ASSERT_TRUE(run.finished);
    EXPECT_EQ(run.exitCode, kSuccess) << run.standardError;
    EXPECT_EQ(run.standardOutput, cli::versionText());
}
#endif

TEST_F(CommandLineProcessTest, ALineThatDescribesNoRunExitsTwoAndSaysWhyOnTheErrorStream)
{
    for (const std::vector<std::string>& line : {std::vector<std::string>{}, std::vector<std::string>{"only-one.fits"},
                                                 std::vector<std::string>{"--fast", "a.fits", "b.fits"}})
    {
        const support::ToolRun run = support::runTool(line);
        ASSERT_TRUE(run.finished);
        EXPECT_EQ(run.exitCode, kUsage);
        EXPECT_TRUE(run.standardOutput.empty()) << run.standardOutput;
        EXPECT_EQ(run.standardError.rfind("AfterNightCLI: ", 0u), 0u) << run.standardError;
        EXPECT_NE(run.standardError.find(cli::usageSynopsis()), std::string::npos) << run.standardError;
    }
}

TEST_F(CommandLineProcessTest, ARunWritesTheFileSaysWhatItWroteAndExitsZero)
{
    const std::string in = input("scene-rgb-uint16", "light.fits");
    const std::filesystem::path outputs = m_scratch.makeDirectory("out");
    const std::string out = support::utf8(outputs / "light.tif");

    const support::ToolRun run = support::runTool({in, out});
    ASSERT_TRUE(run.finished);
    ASSERT_EQ(run.exitCode, kSuccess) << run.standardError;
    EXPECT_TRUE(run.standardError.empty()) << run.standardError;
    EXPECT_EQ(m_scratch.fileNames("out"), (std::vector<std::string>{"light.tif"}));

    // What it printed is the line a run in this process describes, for the same input and output.
    cli::Invocation invocation;
    invocation.inputPath = in;
    invocation.outputPath = support::utf8(m_scratch.path("again.tif"));
    std::stop_source never;
    const cli::RunReport here = cli::runOneShot(invocation, never.get_token());
    ASSERT_EQ(here.exitCode, cli::ExitCode::Success) << here.diagnostic;
    invocation.outputPath = out;
    EXPECT_EQ(run.standardOutput, cli::describeRun(invocation, here) + "\n");
}

TEST_F(CommandLineProcessTest, EachStepsFailureIsItsExitCodeAndALineOnTheErrorStream)
{
    const std::string in = input("scene-rgb-uint16", "light.fits");

    const support::ToolRun missing = support::runTool({m_scratch.file("absent.fits"), m_scratch.file("out.fits")});
    ASSERT_TRUE(missing.finished);
    EXPECT_EQ(missing.exitCode, kInputFailed) << missing.standardError;
    EXPECT_NE(missing.standardError.find("absent.fits"), std::string::npos) << missing.standardError;

    const support::ToolRun nowhere = support::runTool({in, m_scratch.file("no/such/out.fits")});
    ASSERT_TRUE(nowhere.finished);
    EXPECT_EQ(nowhere.exitCode, kOutputFailed) << nowhere.standardError;

    const support::ToolRun bitmap = support::runTool({in, m_scratch.file("out.bmp")});
    ASSERT_TRUE(bitmap.finished);
    EXPECT_EQ(bitmap.exitCode, kUsage) << bitmap.standardError;

    for (const support::ToolRun* run : {&missing, &nowhere, &bitmap})
    {
        EXPECT_TRUE(run->standardOutput.empty()) << run->standardOutput;
        EXPECT_EQ(run->standardError.rfind("AfterNightCLI: ", 0u), 0u) << run->standardError;
    }
    EXPECT_EQ(m_scratch.fileNames(), (std::vector<std::string>{"light.fits"}));
}

TEST_F(CommandLineProcessTest, APathNoCodePageSpellsIsReadAndWritten)
{
    // Greek, Cyrillic and an emoji: no single Windows code page spells all three.
    const std::string in = input("scene-mono-float32", "\xCE\xB1\xCF\x83\xCF\x84\xCE\xAD\xCF\x81\xCE\xB9 "
                                                       "\xD0\xB7\xD0\xB2\xD0\xB5\xD0\xB7\xD0\xB4\xD0\xB0.fits");
    const std::string out = m_scratch.file("\xF0\x9F\x94\xAD result.tif");
    const support::ToolRun run = support::runTool({in, out});
    ASSERT_TRUE(run.finished);
    ASSERT_EQ(run.exitCode, kSuccess) << run.standardError;
    EXPECT_TRUE(std::filesystem::exists(support::fromUtf8(out)));
    // Written to a file, the output is the UTF-8 bytes it names.
    EXPECT_NE(run.standardOutput.find("\xF0\x9F\x94\xAD result.tif"), std::string::npos) << run.standardOutput;
}

TEST_F(CommandLineProcessTest, APathPastWindowsOldLimitIsWrittenAndReadBack)
{
#ifdef _WIN32
    if (!longPathsEnabled())
    {
        GTEST_SKIP() << "this Windows has long paths turned off, so no process can use one";
    }
#endif
    const std::string in = input("scene-mono-float32", "light.fits");
    const std::filesystem::path top = m_scratch.path("long");
    std::filesystem::path deep = top;
    for (char letter = 'a'; letter < 'e'; ++letter)
    {
        deep /= std::string(60u, letter);
    }
    ASSERT_TRUE(std::filesystem::create_directories(reachable(deep)));
    const std::string out = support::utf8(deep / "light written past the old limit.fits");
    ASSERT_GT(out.size(), 260u);

    const support::ToolRun written = support::runTool({in, out});
    ASSERT_TRUE(written.finished);
    EXPECT_EQ(written.exitCode, kSuccess) << written.standardError;
    EXPECT_TRUE(std::filesystem::exists(reachable(support::fromUtf8(out))));

    const support::ToolRun read = support::runTool({out, m_scratch.file("read back.tif")});
    ASSERT_TRUE(read.finished);
    EXPECT_EQ(read.exitCode, kSuccess) << read.standardError;

    std::error_code error;
    std::filesystem::remove_all(reachable(top), error);
    EXPECT_FALSE(error) << error.message();
}

TEST_F(CommandLineProcessTest, ItLogsIntoTheStorageRootItIsGivenAndNowhereElse)
{
    const std::optional<std::string> root =
        an::tests::harness::environmentValue(an::tests::harness::kStorageRootVariable);
    ASSERT_TRUE(root.has_value()) << "the harness gives every suite a storage root";
    const std::filesystem::path log = support::fromUtf8(*root) / "logs" / "AfterNight.log";

    const std::string in = input("scene-mono-float32", "logged.fits");
    const support::ToolRun run = support::runTool({in, m_scratch.file("logged.png")});
    ASSERT_TRUE(run.finished);
    ASSERT_EQ(run.exitCode, kSuccess) << run.standardError;

    std::ifstream stream(log, std::ios::binary);
    ASSERT_TRUE(stream.good()) << support::utf8(log) << " was not written";
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("logged.png"), std::string::npos) << "the run did not log what it wrote";
}

TEST_F(CommandLineProcessTest, AnInterruptDuringTheWriteLeavesNothingWhereTheOutputWouldHaveBeen)
{
    if (!support::canInterruptChildren())
    {
        GTEST_SKIP() << "this process has no console its children share, so a console interrupt cannot reach the tool; "
                        "run the suite from a console";
    }
    // Twelve megapixels of colour noise: deflating them as sixteen-bit PNG takes seconds, which is the window the
    // interrupt lands in.
    const std::optional<support::MaterializedFixture> frame =
        support::materialize(support::largeNoisyFrame(4096u, 3072u));
    ASSERT_TRUE(frame.has_value());
    const std::string in = m_scratch.file("noise.fits");
    ASSERT_TRUE(support::writeInput(*frame, support::fromUtf8(in)));
    const std::filesystem::path outputs = m_scratch.makeDirectory("out");
    const std::filesystem::path captures = m_scratch.makeDirectory("captures");

    support::RunningTool tool({in, support::utf8(outputs / "noise.png")}, captures);
    ASSERT_TRUE(tool.started()) << tool.failure();

    // The write has begun once its pending file exists beside the destination.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    while (m_scratch.fileNames("out").empty() && !tool.hasExited() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (m_scratch.fileNames("out").empty())
    {
        const support::ToolRun early = tool.wait(std::chrono::seconds(10));
        FAIL() << "the write never began; the tool exited with " << early.exitCode << ": " << early.standardError;
    }
    ASSERT_TRUE(tool.interrupt()) << tool.failure();

    const support::ToolRun run = tool.wait(std::chrono::minutes(1));
    ASSERT_TRUE(run.finished) << "the tool did not stop";
    EXPECT_EQ(run.exitCode, kInterrupted) << "exit " << run.exitCode << ": " << run.standardError;
    EXPECT_NE(run.standardError.find("interrupted"), std::string::npos) << run.standardError;
    EXPECT_TRUE(run.standardOutput.empty()) << run.standardOutput;
    EXPECT_TRUE(m_scratch.fileNames("out").empty()) << "the interrupted write left a file behind";
}
