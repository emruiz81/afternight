// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The command line of AfterNight's command-line tool: what one run is asked to do, the options that ask it, the
/// codes the process exits with, and the text `--help` and `--version` print.
///
/// The tool does one thing - read an image, stretch it for display, write the result - and every decision a run
/// takes is either on its command line or a default stated in `kOptions`. Nothing is read from a settings file, so
/// one command line does the same thing on every machine. The option table is the one place an option is declared:
/// the parser reads it, `usageText` prints it, and the behavior register's rows for the tool are generated from it.
#pragma once

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace an::cli
{

/// The tool's name as a person types it, which every message and the usage text spell.
inline constexpr std::string_view kProgramName = "AfterNightCLI";

/// What the process exits with. Each failing step has its own code, because a script calling the tool branches on
/// the code and a person reads the message.
enum class ExitCode : int
{
    /// The output was written.
    Success = 0,

    /// The command line did not describe a run: a missing path, an unknown option, a value an option does not take,
    /// or an output no installed codec writes in the representation asked for.
    Usage = 2,

    /// The input could not be read.
    InputFailed = 3,

    /// The input was read and could not be stretched or converted.
    ProcessingFailed = 4,

    /// The result could not be written. Nothing is left where the output would have been.
    OutputFailed = 5,

    /// The engine could not be started or stopped, or this tool was built from other headers than the libraries
    /// beside it.
    EngineFailed = 6,

    /// The run was interrupted - Ctrl+C, Ctrl+Break, the console closing or a termination request - and stopped
    /// cleanly. Nothing is left where the output would have been. The shell's code for an interrupted process.
    Interrupted = 130
};

/// One option the tool reads.
struct OptionSpec
{
    /// How it is spelled, with its two dashes.
    std::string_view name;

    /// What its value is called in the usage text, or empty for an option that takes none.
    std::string_view value;

    /// What a run uses when the option is not given, or empty for an option that is not a setting.
    std::string_view defaultValue;

    /// What it does, for the usage text.
    std::string_view help;
};

/// Every option the tool reads, in the order `usageText` prints them.
inline constexpr std::array<OptionSpec, 5u> kOptions{{
    {"--sample-type", "TYPE", "source",
     "the representation the output is written in: uint8, int8, uint16, int16, uint32, int32, float32 or float64; "
     "source writes the input's, or the nearest one the output container holds"},
    {"--threads", "N", "0", "the most threads the run uses; 0 uses every thread the host has"},
    {"--memory-mib", "N", "0", "the engine's memory budget in mebibytes; 0 keeps the engine's default"},
    {"--help", "", "", "prints this text and exits"},
    {"--version", "", "", "prints the versions of the engine and the runtime this tool runs on and exits"},
}};

/// One run, as its command line describes it.
struct Invocation
{
    /// The image to read, as UTF-8.
    std::string inputPath{};

    /// The file to write, as UTF-8. Its extension decides the container.
    std::string outputPath{};

    /// The representation to write, or empty for the input's, or the nearest one the container holds when it holds
    /// not that.
    std::optional<an::SampleType> sampleType{};

    /// The most threads the run uses, or zero for every thread the host has.
    std::uint32_t threads = 0u;

    /// The engine's memory budget in mebibytes, or zero for the engine's default.
    std::uint64_t memoryBudgetMib = 0u;
};

/// What a command line asks for.
enum class CommandKind : std::uint8_t
{
    /// One run.
    Run,

    /// The usage text.
    Help,

    /// The versions of what the tool runs on.
    Version
};

/// A parsed command line.
struct Command
{
    /// What it asks for.
    CommandKind kind = CommandKind::Run;

    /// The run, for `CommandKind::Run`.
    Invocation invocation{};
};

/// The largest memory budget `--memory-mib` accepts: the most mebibytes that are still a byte count.
inline constexpr std::uint64_t kMaxMemoryBudgetMib = UINT64_MAX / (1024u * 1024u);

/// Reads a command line.
///
/// Options and paths may come in any order; `--name value` and `--name=value` are the same option, and everything
/// after `--` is a path. `--help` and `--version` win over everything else on the line.
///
/// @param arguments the arguments after the program's name, as UTF-8.
/// @return the command; or `ErrorCode::InvalidArgument` naming what is wrong with the line - an unknown option, a
///         missing or unreadable value, an option given twice, or other than two paths.
[[nodiscard]] an::Result<Command> parseCommandLine(std::span<const std::string_view> arguments);

/// The representation a `--sample-type` value names.
///
/// @param name the value: one of the eight representations' names, in any letter case.
/// @return the representation, or empty for any other text.
[[nodiscard]] std::optional<an::SampleType> sampleTypeFromName(std::string_view name) noexcept;

/// How the tool spells a representation: the engine's name in lower case.
///
/// @param sampleType the representation.
/// @return the name, such as `uint16`; `unknown` for a value outside the eight.
[[nodiscard]] std::string sampleTypeArgument(an::SampleType sampleType);

/// The line that says how the tool is called, for a usage failure.
///
/// @return the synopsis, without a line ending.
[[nodiscard]] std::string usageSynopsis();

/// What `--help` prints: what a run does, every option with its default, the containers and the exit codes.
///
/// @return the text, each line ending in a line feed.
[[nodiscard]] std::string usageText();

/// What `--version` prints: the engine library's version and binary-interface generation and the consumer runtime's
/// interface version, as the loaded libraries report them.
///
/// @return the text, each line ending in a line feed.
[[nodiscard]] std::string versionText();

} // namespace an::cli
