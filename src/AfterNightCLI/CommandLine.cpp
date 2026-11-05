// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The command-line tool's parser, usage text and version text.
//
// The parser reads the option table and nothing else, so an option exists exactly when the table states it. It
// refuses rather than guesses: an unknown option, a value an option does not take, an option given twice or other
// than two paths is a usage failure naming what was wrong, decided before any engine exists.

#include "CommandLine.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Core/Version.h>

#include <charconv>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace an::cli
{
namespace
{

/// The eight representations, in the order the engine enumerates them.
constexpr std::array<an::SampleType, 8u> kSampleTypes{
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

/// The value `--sample-type` takes for the input's own representation.
constexpr std::string_view kSourceSampleType = "source";

/// A usage failure.
///
/// @param message what was wrong with the command line.
/// @return `ErrorCode::InvalidArgument` carrying it.
[[nodiscard]] an::Status usage(std::string message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, std::move(message));
}

/// An ASCII letter in lower case; every other byte as it is.
[[nodiscard]] char lowered(char letter) noexcept
{
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

/// The option the table states under this spelling, or null.
[[nodiscard]] const OptionSpec* optionNamed(std::string_view name) noexcept
{
    for (const OptionSpec& option : kOptions)
    {
        if (option.name == name)
        {
            return &option;
        }
    }
    return nullptr;
}

/// Reads a whole number with nothing before or after it.
///
/// @param text the value.
/// @return the number, or empty for anything else - a sign, a fraction, a space, a number too large.
[[nodiscard]] std::optional<std::uint64_t> wholeNumber(std::string_view text) noexcept
{
    std::uint64_t value = 0u;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

/// Applies one option's value to the invocation.
///
/// @param name the option.
/// @param value its value.
/// @param invocation what is being built.
/// @return success, or the usage failure naming the value.
[[nodiscard]] an::Status applyOption(std::string_view name, std::string_view value, Invocation& invocation)
{
    if (name == "--sample-type")
    {
        if (value == kSourceSampleType)
        {
            invocation.sampleType.reset();
            return an::Status::success();
        }
        const std::optional<an::SampleType> type = sampleTypeFromName(value);
        if (!type.has_value())
        {
            return usage("--sample-type takes source, uint8, int8, uint16, int16, uint32, int32, float32 or float64, "
                         "not '" +
                         std::string(value) + "'");
        }
        invocation.sampleType = type;
        return an::Status::success();
    }
    const std::optional<std::uint64_t> number = wholeNumber(value);
    if (!number.has_value())
    {
        return usage(std::string(name) + " takes a whole number, not '" + std::string(value) + "'");
    }
    if (name == "--threads")
    {
        if (*number > (std::numeric_limits<std::uint32_t>::max)())
        {
            return usage("--threads takes at most " + std::to_string((std::numeric_limits<std::uint32_t>::max)()) +
                         ", not " + std::string(value));
        }
        invocation.threads = static_cast<std::uint32_t>(*number);
        return an::Status::success();
    }
    if (*number > kMaxMemoryBudgetMib)
    {
        return usage("--memory-mib takes at most " + std::to_string(kMaxMemoryBudgetMib) + ", not " +
                     std::string(value));
    }
    invocation.memoryBudgetMib = *number;
    return an::Status::success();
}

/// How wide the usage text is, in columns.
constexpr std::size_t kUsageWidth = 100u;

/// Text broken into lines of at most `kUsageWidth` columns at its spaces, each line indented.
///
/// @param text one paragraph.
/// @param indent how many spaces start each line.
/// @return the lines, each ending in a line feed.
[[nodiscard]] std::string wrapped(std::string_view text, std::size_t indent)
{
    std::string lines;
    std::string line(indent, ' ');
    std::size_t start = 0u;
    while (start < text.size())
    {
        std::size_t end = text.find(' ', start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        const std::string_view word = text.substr(start, end - start);
        if (line.size() > indent && line.size() + 1u + word.size() > kUsageWidth)
        {
            lines += line + "\n";
            line.assign(indent, ' ');
        }
        if (line.size() > indent)
        {
            line += ' ';
        }
        line += word;
        start = end + 1u;
    }
    if (line.size() > indent)
    {
        lines += line + "\n";
    }
    return lines;
}

} // namespace

an::Result<Command> parseCommandLine(std::span<const std::string_view> arguments)
{
    // `--help` and `--version` win over everything else before `--`, so a person who asks for help with a mistyped
    // line still gets it.
    for (const std::string_view argument : arguments)
    {
        if (argument == "--")
        {
            break;
        }
        if (argument == "--help" || argument == "-h")
        {
            return Command{.kind = CommandKind::Help};
        }
    }
    for (const std::string_view argument : arguments)
    {
        if (argument == "--")
        {
            break;
        }
        if (argument == "--version")
        {
            return Command{.kind = CommandKind::Version};
        }
    }

    Command command;
    std::vector<std::string_view> paths;
    std::vector<std::string_view> given;
    bool optionsEnded = false;
    for (std::size_t index = 0u; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        if (optionsEnded || argument.empty() || argument.front() != '-' || argument == "-")
        {
            paths.push_back(argument);
            continue;
        }
        if (argument == "--")
        {
            optionsEnded = true;
            continue;
        }
        const std::size_t equals = argument.find('=');
        const std::string_view name = argument.substr(0u, equals);
        const OptionSpec* option = optionNamed(name);
        if (option == nullptr)
        {
            return usage("unknown option " + std::string(name));
        }
        if (option->value.empty())
        {
            return usage(std::string(name) + " takes no value");
        }
        for (const std::string_view earlier : given)
        {
            if (earlier == name)
            {
                return usage(std::string(name) + " is given twice");
            }
        }
        given.push_back(name);
        std::string_view value;
        if (equals != std::string_view::npos)
        {
            value = argument.substr(equals + 1u);
        }
        else if (index + 1u < arguments.size())
        {
            value = arguments[++index];
        }
        else
        {
            return usage(std::string(name) + " needs a value");
        }
        if (value.empty())
        {
            return usage(std::string(name) + " needs a value");
        }
        const an::Status applied = applyOption(name, value, command.invocation);
        if (applied.failed())
        {
            return applied;
        }
    }
    if (paths.size() != 2u)
    {
        return usage("expected an input path and an output path, and " + std::to_string(paths.size()) +
                     (paths.size() == 1u ? " path was given" : " paths were given"));
    }
    if (paths[0].empty() || paths[1].empty())
    {
        return usage(paths[0].empty() ? "the input path is empty" : "the output path is empty");
    }
    command.invocation.inputPath = std::string(paths[0]);
    command.invocation.outputPath = std::string(paths[1]);
    return command;
}

std::optional<an::SampleType> sampleTypeFromName(std::string_view name) noexcept
{
    for (const an::SampleType type : kSampleTypes)
    {
        const std::string_view spelled = an::sampleTypeName(type);
        if (spelled.size() != name.size())
        {
            continue;
        }
        bool same = true;
        for (std::size_t index = 0u; index < name.size() && same; ++index)
        {
            same = lowered(spelled[index]) == lowered(name[index]);
        }
        if (same)
        {
            return type;
        }
    }
    return std::nullopt;
}

std::string sampleTypeArgument(an::SampleType sampleType)
{
    for (const an::SampleType type : kSampleTypes)
    {
        if (type == sampleType)
        {
            std::string spelled = an::sampleTypeName(type);
            for (char& letter : spelled)
            {
                letter = lowered(letter);
            }
            return spelled;
        }
    }
    return "unknown";
}

std::string usageSynopsis()
{
    return "Usage: " + std::string(kProgramName) + " [options] <input> <output>";
}

std::string usageText()
{
    std::ostringstream text;
    text << usageSynopsis() << "\n"
         << "\n"
         << "Reads one image, stretches it for display and writes the result. Each channel is stretched on its own by\n"
         << "the automatic screen transfer: its shadows clipped at " << an::kDisplayShadowsClipping
         << " normalized median absolute deviations from\n"
         << "its median, and its median moved to " << an::kDisplayTargetBackground << ".\n"
         << "\n"
         << "The input may be FITS, XISF, TIFF, PNG, JPEG or a camera RAW file; a floating input is read as a\n"
         << "fraction of full scale, and a RAW frame from a colour filter array is stretched as the mosaic it\n"
         << "recorded, which only FITS, XISF and TIFF can hold. The output's extension chooses its container:\n"
         << ".fits, .fit or .fts, .xisf, .tif or .tiff, .png, or .jpg or .jpeg. A file already at the output\n"
         << "path is replaced, and an output is either written whole or not at all.\n"
         << "\n"
         << "Options:\n";
    for (const OptionSpec& option : kOptions)
    {
        std::string spelled = "  " + std::string(option.name);
        if (!option.value.empty())
        {
            spelled += " " + std::string(option.value);
        }
        std::string help(option.help);
        if (!option.defaultValue.empty())
        {
            help += " (default: " + std::string(option.defaultValue) + ")";
        }
        text << spelled << "\n" << wrapped(help, 6u);
    }
    text << "\n"
         << "Exit codes:\n"
         << "    0 the output was written\n"
         << "    2 the command line did not describe a run, or the output container cannot hold what was asked\n"
         << "    3 the input could not be read\n"
         << "    4 the input could not be stretched or converted\n"
         << "    5 the output could not be written\n"
         << "    6 the engine could not be started or stopped\n"
         << "  130 the run was interrupted; nothing was left where the output would have been\n";
    return text.str();
}

std::string versionText()
{
    std::ostringstream text;
    text << kProgramName << "\n"
         << "engine " << an::engineVersionString() << ", binary-interface generation " << an::engineAbiGeneration()
         << "\n"
         << "consumer runtime interface " << an::consumer::consumerRuntimeInterfaceVersion() << "\n";
    return text.str();
}

} // namespace an::cli
