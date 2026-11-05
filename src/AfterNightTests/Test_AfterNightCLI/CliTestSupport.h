// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// What the command-line tool's suite shares that names no engine type: a directory of each case's own, inputs the tool
// reads, a reader for what it wrote as FITS, the records it is held to, and the tool's own binary run the way a script
// runs it.
//
// The inputs are neutral fixtures written by the neutral FITS writer, and a FITS output is read back by the neutral
// reader, so what the tool is held to comes from no engine. The one helper that reads an output through the
// engine - a TIFF, PNG, JPEG or XISF file has no neutral reader - is `WrittenImageProbe.h`, kept apart so that no file
// both describes a neutral fixture and names an engine type.

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/common/FixtureImage.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::cli::tests
{

namespace fixtures = an::tests::fixtures;

/// A path as the UTF-8 text the tool and the engine take.
[[nodiscard]] std::string utf8(const std::filesystem::path& path);

/// A path from UTF-8 text.
[[nodiscard]] std::filesystem::path fromUtf8(std::string_view text);

/// A directory of one case's own under the temporary directory the suite's harness owns, removed with everything in
/// it when this goes away.
class ScratchDirectory
{
public:
    ScratchDirectory();
    ~ScratchDirectory();
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    /// Whether the directory exists.
    [[nodiscard]] bool valid() const noexcept;

    /// A path inside it; `relative` may name a subdirectory, which is not created.
    [[nodiscard]] std::filesystem::path path(std::string_view relative) const;

    /// The same path as UTF-8.
    [[nodiscard]] std::string file(std::string_view relative) const;

    /// Creates a subdirectory and returns its path.
    [[nodiscard]] std::filesystem::path makeDirectory(std::string_view relative) const;

    /// Every file under a subdirectory, or the root, by name and sorted.
    [[nodiscard]] std::vector<std::string> fileNames(std::string_view relative = {}) const;

private:
    std::filesystem::path m_root;
    bool m_valid = false;
};

/// A catalogue fixture's description and samples, materialized; the case fails when the name is unknown.
struct MaterializedFixture
{
    fixtures::FixtureImage image;
    fixtures::FixtureSamples samples;
};
[[nodiscard]] std::optional<MaterializedFixture> materialize(const fixtures::FixtureImage& image);
[[nodiscard]] std::optional<MaterializedFixture> materialize(std::string_view catalogueName);

/// Writes a fixture as a neutral FITS file; false, with a failure added to the case, when it cannot.
[[nodiscard]] bool writeInput(const MaterializedFixture& fixture, const std::filesystem::path& path);

/// A noisy colour light frame of sixteen-bit codes, large enough that writing it as PNG takes seconds.
[[nodiscard]] fixtures::FixtureImage largeNoisyFrame(std::uint32_t width, std::uint32_t height);

/// Reads a FITS output with the neutral reader; empty, with a failure added, when it cannot.
[[nodiscard]] std::optional<fixtures::FixtureFitsFile> readFitsOutput(const std::filesystem::path& path);

/// The record of this id in a checked-in expectation file; empty, with a failure added, when there is none.
[[nodiscard]] std::optional<fixtures::Expectation> expectationRecord(std::string_view file, std::string_view id);

/// The tool's binary beside this suite's, in this suite's configuration.
[[nodiscard]] std::filesystem::path toolPath();

/// What one run of the tool's binary did.
struct ToolRun
{
    /// Whether it exited within its bound; one that did not was killed.
    bool finished = false;

    /// Its exit code: the code it returned, or 128 plus the signal that ended it.
    int exitCode = -1;

    /// Everything it wrote to its standard output and error, as bytes.
    std::string standardOutput;
    std::string standardError;
};

/// The tool's binary running, which a case may interrupt.
class RunningTool
{
public:
    /// Starts the tool with these arguments, its output and error captured into files in `captureDirectory` and its
    /// input empty. On Windows it runs in a process group of its own, so an interrupt reaches it and nothing else.
    RunningTool(const std::vector<std::string>& arguments, const std::filesystem::path& captureDirectory);

    /// Kills the tool if it is still running.
    ~RunningTool();

    RunningTool(const RunningTool&) = delete;
    RunningTool& operator=(const RunningTool&) = delete;

    /// Whether it started; the reason when it did not.
    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] const std::string& failure() const noexcept;

    /// Sends it the console's interrupt: Ctrl+Break to its process group on Windows, SIGINT on Linux.
    ///
    /// @return false, with the reason in `failure`, when the interrupt could not be sent.
    [[nodiscard]] bool interrupt();

    /// Whether it has exited already. Never waits.
    [[nodiscard]] bool hasExited();

    /// Waits for it to exit, killing it after `bound`.
    [[nodiscard]] ToolRun wait(std::chrono::milliseconds bound);

    struct Process;

private:
    std::unique_ptr<Process> m_process;
    std::filesystem::path m_outputPath;
    std::filesystem::path m_errorPath;
    std::string m_failure;
};

/// Runs the tool to completion, killing it after `bound`.
[[nodiscard]] ToolRun runTool(const std::vector<std::string>& arguments,
                              std::chrono::milliseconds bound = std::chrono::minutes(2));

/// Whether the suite can send its children a console interrupt: always on Linux; on Windows only when this process has
/// a console its children share.
[[nodiscard]] bool canInterruptChildren();

} // namespace an::cli::tests
