// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight's command-line tool: one image read, stretched for display and written.
//
// The entry point does what only a process can do and leaves the run to `runOneShot`: it reads its arguments as UTF-8,
// refuses to run beside an engine or a runtime built from other headers, answers --help and --version, watches the
// console for an interrupt before any thread starts, starts the platform logger from the logging configuration the
// application uses, and prints what the run did. A usage failure is answered before the logger or any engine starts.

#include "CommandLine.h"
#include "ConsoleText.h"
#include "InterruptWatch.h"
#include "OneShotRun.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/LoggingConfig.h>

#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// Starts the platform logger from the logging configuration in the application's data directory, as the application
/// does. A tool that cannot log still runs, and says so once.
void startLogging() noexcept
{
    try
    {
        an::platform::LoggingConfig config;
        an::platform::loadLoggingConfig(config);
        an::platform::initializeLoggingFromConfig(config);
    }
    catch (const std::exception& error)
    {
        an::cli::writeStandardError(std::string(an::cli::kProgramName) +
                                    ": warning: no log is written: " + error.what() + "\n");
    }
}

/// The tool, from its arguments to its exit code.
///
/// @param arguments the arguments after the program's name, as UTF-8.
/// @return the exit code.
int runTool(const std::vector<std::string>& arguments) noexcept
{
    const std::string prefix = std::string(an::cli::kProgramName) + ": ";

    // Nothing the engine or the runtime says can be trusted if either was built from other headers than this tool, so
    // that is asked before anything else is.
    if (!an::engineMatchesHeaders())
    {
        an::cli::writeStandardError(prefix + "the engine library beside this tool was built from other headers; "
                                             "install the tool and its libraries together\n");
        return static_cast<int>(an::cli::ExitCode::EngineFailed);
    }
    if (an::consumer::consumerRuntimeInterfaceVersion() != an::consumer::kConsumerRuntimeInterfaceVersion)
    {
        an::cli::writeStandardError(prefix + "the consumer runtime beside this tool was built from other headers; "
                                             "install the tool and its libraries together\n");
        return static_cast<int>(an::cli::ExitCode::EngineFailed);
    }

    try
    {
        const std::vector<std::string_view> views(arguments.begin(), arguments.end());
        const an::Result<an::cli::Command> command = an::cli::parseCommandLine(views);
        if (!command.hasValue())
        {
            an::cli::writeStandardError(prefix + command.status().message() + "\n" + an::cli::usageSynopsis() +
                                        "\nRun " + std::string(an::cli::kProgramName) +
                                        " --help for what a run does and every option.\n");
            return static_cast<int>(an::cli::ExitCode::Usage);
        }
        if (command->kind == an::cli::CommandKind::Help)
        {
            an::cli::writeStandardOutput(an::cli::usageText());
            return static_cast<int>(an::cli::ExitCode::Success);
        }
        if (command->kind == an::cli::CommandKind::Version)
        {
            an::cli::writeStandardOutput(an::cli::versionText());
            return static_cast<int>(an::cli::ExitCode::Success);
        }

        // Before any thread starts: the logger and the engine both start threads, and an interrupt that arrives while
        // they do is still a stop request rather than the end of the process.
        an::cli::InterruptWatch interrupt;
        startLogging();
        LOG_INFO("{} {} -> {}", an::cli::kProgramName, command->invocation.inputPath, command->invocation.outputPath);
        if (!interrupt.installed())
        {
            LOG_WARNING("{}: no interrupt handler could be installed; an interrupt ends the process where it stands",
                        an::cli::kProgramName);
        }
        const an::cli::RunReport report = an::cli::runOneShot(command->invocation, interrupt.token());
        interrupt.finished();
        if (report.exitCode == an::cli::ExitCode::Success)
        {
            an::cli::writeStandardOutput(an::cli::describeRun(command->invocation, report) + "\n");
        }
        else
        {
            an::cli::writeStandardError(prefix + report.diagnostic + "\n");
        }
        LOG_INFO("{} exits with {}", an::cli::kProgramName, static_cast<int>(report.exitCode));
        an::platform::shutdownLogging();
        return static_cast<int>(report.exitCode);
    }
    catch (...)
    {
        an::cli::writeStandardError(prefix + "the tool ended with an exception\n");
        return static_cast<int>(an::cli::ExitCode::EngineFailed);
    }
}

} // namespace

#ifdef _WIN32
int wmain(int argumentCount, wchar_t** arguments)
{
    std::vector<std::string> converted;
    try
    {
        converted = an::cli::utf8Arguments(argumentCount, arguments);
    }
    catch (...)
    {
        an::cli::writeStandardError(std::string(an::cli::kProgramName) + ": the arguments could not be read\n");
        return static_cast<int>(an::cli::ExitCode::Usage);
    }
    return runTool(converted);
}
#else
int main(int argumentCount, char** arguments)
{
    std::vector<std::string> converted;
    try
    {
        converted = an::cli::utf8Arguments(argumentCount, arguments);
    }
    catch (...)
    {
        an::cli::writeStandardError(std::string(an::cli::kProgramName) + ": the arguments could not be read\n");
        return static_cast<int>(an::cli::ExitCode::Usage);
    }
    return runTool(converted);
}
#endif
