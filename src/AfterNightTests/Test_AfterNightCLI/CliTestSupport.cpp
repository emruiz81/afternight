// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CliTestSupport.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/harness/TestHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace an::cli::tests
{

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path fromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// ---- Directories.

ScratchDirectory::ScratchDirectory()
{
    static std::atomic<std::uint64_t> counter{0u};
    std::error_code error;
    const std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return;
    }
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 16 && !m_valid; ++attempt)
    {
        const std::filesystem::path candidate =
            base / ("cli-" + std::to_string(counter.fetch_add(1u)) + "-" + std::to_string(seed % 1000003));
        if (std::filesystem::create_directory(candidate, error) && !error)
        {
            m_root = candidate;
            m_valid = true;
        }
    }
}

ScratchDirectory::~ScratchDirectory()
{
    if (m_valid)
    {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }
}

bool ScratchDirectory::valid() const noexcept
{
    return m_valid;
}

std::filesystem::path ScratchDirectory::path(std::string_view relative) const
{
    return m_root / fromUtf8(relative);
}

std::string ScratchDirectory::file(std::string_view relative) const
{
    return utf8(path(relative));
}

std::filesystem::path ScratchDirectory::makeDirectory(std::string_view relative) const
{
    std::error_code error;
    const std::filesystem::path made = path(relative);
    std::filesystem::create_directories(made, error);
    return made;
}

std::vector<std::string> ScratchDirectory::fileNames(std::string_view relative) const
{
    std::vector<std::string> names;
    std::error_code error;
    const std::filesystem::path directory = relative.empty() ? m_root : path(relative);
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
    {
        names.push_back(utf8(entry.path().filename()));
    }
    std::sort(names.begin(), names.end());
    return names;
}

// ---- Inputs and records.

std::optional<MaterializedFixture> materialize(const fixtures::FixtureImage& image)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
    if (!samples.has_value())
    {
        ADD_FAILURE() << image.name << ": " << error;
        return std::nullopt;
    }
    return MaterializedFixture{image, std::move(*samples)};
}

std::optional<MaterializedFixture> materialize(std::string_view catalogueName)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(catalogueName);
    if (image == nullptr)
    {
        ADD_FAILURE() << catalogueName << " is not a fixture of the catalogue";
        return std::nullopt;
    }
    return materialize(*image);
}

bool writeInput(const MaterializedFixture& fixture, const std::filesystem::path& path)
{
    std::string error;
    if (!fixtures::writeFixtureFits(fixture.image, fixture.samples, path, error))
    {
        ADD_FAILURE() << "writing " << utf8(path) << ": " << error;
        return false;
    }
    return true;
}

fixtures::FixtureImage largeNoisyFrame(std::uint32_t width, std::uint32_t height)
{
    fixtures::FixtureImage image;
    image.name = "cli-large-noisy-rgb-uint16";
    image.width = width;
    image.height = height;
    image.channels = fixtures::FixtureChannels::Rgb;
    image.sampleType = fixtures::FixtureSampleType::UInt16;
    image.domain = fixtures::FixtureDomain::SensorCode;
    image.pattern = fixtures::FixturePattern::Noise;
    image.seed = 41u;
    image.levels = {{.base = 20000.0, .amplitude = 18000.0},
                    {.base = 26000.0, .amplitude = 18000.0},
                    {.base = 32000.0, .amplitude = 18000.0}};
    return image;
}

std::optional<fixtures::FixtureFitsFile> readFitsOutput(const std::filesystem::path& path)
{
    std::string error;
    std::optional<fixtures::FixtureFitsFile> file = fixtures::readFixtureFits(path, error);
    if (!file.has_value())
    {
        ADD_FAILURE() << "reading " << utf8(path) << ": " << error;
    }
    return file;
}

std::optional<fixtures::Expectation> expectationRecord(std::string_view file, std::string_view id)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    if (!directory.has_value())
    {
        ADD_FAILURE() << error;
        return std::nullopt;
    }
    const std::optional<std::vector<fixtures::Expectation>> records =
        fixtures::readExpectationFile(*directory / fromUtf8(file), error);
    if (!records.has_value())
    {
        ADD_FAILURE() << error;
        return std::nullopt;
    }
    for (const fixtures::Expectation& record : *records)
    {
        if (record.id == id)
        {
            return record;
        }
    }
    ADD_FAILURE() << file << " holds no record " << id;
    return std::nullopt;
}

// ---- The tool's binary.

std::filesystem::path toolPath()
{
    const std::filesystem::path suite = an::tests::harness::executablePath();
#if defined(_DEBUG)
    const char* name = "AfterNightCLI_d";
#else
    const char* name = "AfterNightCLI";
#endif
#ifdef _WIN32
    return suite.parent_path() / (std::string(name) + ".exe");
#else
    return suite.parent_path() / name;
#endif
}

namespace
{

std::string readWhole(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

#ifdef _WIN32
/// One argument quoted so the C runtime's parser gives it back unchanged.
std::wstring quoteArgument(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        return argument;
    }
    std::wstring quoted = L"\"";
    for (auto it = argument.begin();; ++it)
    {
        std::size_t backslashes = 0u;
        while (it != argument.end() && *it == L'\\')
        {
            ++it;
            ++backslashes;
        }
        if (it == argument.end())
        {
            quoted.append(backslashes * 2u, L'\\');
            break;
        }
        if (*it == L'"')
        {
            quoted.append(backslashes * 2u + 1u, L'\\');
        }
        else
        {
            quoted.append(backslashes, L'\\');
        }
        quoted.push_back(*it);
    }
    quoted.push_back(L'"');
    return quoted;
}

bool hasConsole()
{
    return GetConsoleCP() != 0u;
}
#endif

} // namespace

#ifdef _WIN32
struct RunningTool::Process
{
    PROCESS_INFORMATION info{};
    bool running = false;
};
#else
struct RunningTool::Process
{
    pid_t pid = -1;
    bool running = false;
    bool reaped = false;
    int status = 0;
};
#endif

RunningTool::RunningTool(const std::vector<std::string>& arguments, const std::filesystem::path& captureDirectory)
    : m_process(std::make_unique<Process>()), m_outputPath(captureDirectory / "stdout.txt"),
      m_errorPath(captureDirectory / "stderr.txt")
{
    const std::filesystem::path program = toolPath();
    if (!std::filesystem::exists(program))
    {
        m_failure = utf8(program) + " does not exist; build the command-line tool first";
        return;
    }
#ifdef _WIN32
    std::wstring commandLine = quoteArgument(program.wstring());
    for (const std::string& argument : arguments)
    {
        commandLine += L" " + quoteArgument(fromUtf8(argument).wstring());
    }
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(m_outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE error = CreateFileW(m_errorPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || error == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE)
    {
        m_failure = "the capture files could not be opened";
    }
    else
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = output;
        startup.hStdError = error;
        // A process group of its own, so an interrupt sent to the tool reaches nothing else; and no window of its own
        // when this suite has no console for it to share.
        const DWORD flags = CREATE_NEW_PROCESS_GROUP | (hasConsole() ? 0u : static_cast<DWORD>(CREATE_NO_WINDOW));
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &startup,
                           &m_process->info))
        {
            m_process->running = true;
        }
        else
        {
            m_failure = "CreateProcessW failed with " + std::to_string(GetLastError());
        }
    }
    for (HANDLE handle : {output, error, input})
    {
        if (handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }
#else
    const std::string programText = utf8(program);
    std::vector<std::string> words{programText};
    words.insert(words.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& word : words)
    {
        argv.push_back(word.data());
    }
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, m_outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addopen(&actions, 2, m_errorPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const int spawned = posix_spawn(&m_process->pid, programText.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned == 0)
    {
        m_process->running = true;
    }
    else
    {
        m_failure = "posix_spawn failed with " + std::to_string(spawned);
    }
#endif
}

RunningTool::~RunningTool()
{
    if (m_process->running)
    {
        (void)wait(std::chrono::milliseconds(0));
    }
#ifdef _WIN32
    if (m_process->info.hProcess != nullptr)
    {
        CloseHandle(m_process->info.hProcess);
        CloseHandle(m_process->info.hThread);
    }
#endif
}

bool RunningTool::started() const noexcept
{
    return m_process->running;
}

const std::string& RunningTool::failure() const noexcept
{
    return m_failure;
}

bool RunningTool::interrupt()
{
    if (!m_process->running)
    {
        m_failure = "the tool is not running";
        return false;
    }
#ifdef _WIN32
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, m_process->info.dwProcessId))
    {
        m_failure = "GenerateConsoleCtrlEvent failed with " + std::to_string(GetLastError());
        return false;
    }
#else
    if (kill(m_process->pid, SIGINT) != 0)
    {
        m_failure = "kill failed";
        return false;
    }
#endif
    return true;
}

bool RunningTool::hasExited()
{
    if (!m_process->running)
    {
        return true;
    }
#ifdef _WIN32
    return WaitForSingleObject(m_process->info.hProcess, 0u) == WAIT_OBJECT_0;
#else
    if (!m_process->reaped && waitpid(m_process->pid, &m_process->status, WNOHANG) == m_process->pid)
    {
        m_process->reaped = true;
    }
    return m_process->reaped;
#endif
}

ToolRun RunningTool::wait(std::chrono::milliseconds bound)
{
    ToolRun run;
    if (!m_process->running)
    {
        return run;
    }
#ifdef _WIN32
    const DWORD waited = WaitForSingleObject(m_process->info.hProcess, static_cast<DWORD>(bound.count()));
    run.finished = waited == WAIT_OBJECT_0;
    if (!run.finished)
    {
        TerminateProcess(m_process->info.hProcess, 1u);
        WaitForSingleObject(m_process->info.hProcess, INFINITE);
    }
    DWORD code = 0u;
    GetExitCodeProcess(m_process->info.hProcess, &code);
    run.exitCode = static_cast<int>(code);
#else
    int status = m_process->status;
    const auto deadline = std::chrono::steady_clock::now() + bound;
    bool reaped = m_process->reaped;
    while (!reaped && std::chrono::steady_clock::now() < deadline)
    {
        reaped = waitpid(m_process->pid, &status, WNOHANG) == m_process->pid;
        if (!reaped)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    if (!reaped)
    {
        reaped = waitpid(m_process->pid, &status, WNOHANG) == m_process->pid;
    }
    run.finished = reaped;
    if (!run.finished)
    {
        kill(m_process->pid, SIGKILL);
        waitpid(m_process->pid, &status, 0);
    }
    run.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    m_process->running = false;
    run.standardOutput = readWhole(m_outputPath);
    run.standardError = readWhole(m_errorPath);
    return run;
}

ToolRun runTool(const std::vector<std::string>& arguments, std::chrono::milliseconds bound)
{
    ScratchDirectory capture;
    if (!capture.valid())
    {
        ADD_FAILURE() << "no capture directory";
        return {};
    }
    RunningTool tool(arguments, capture.path(""));
    if (!tool.started())
    {
        ADD_FAILURE() << tool.failure();
        return {};
    }
    return tool.wait(bound);
}

bool canInterruptChildren()
{
#ifdef _WIN32
    return hasConsole();
#else
    return true;
#endif
}

} // namespace an::cli::tests
