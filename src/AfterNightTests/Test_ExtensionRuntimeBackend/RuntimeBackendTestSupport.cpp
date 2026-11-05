// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime backend suite's shared support: loading the module, the isolated interpreter, the shared runtime and
/// the scenarios' processes.

#include "RuntimeBackendTestSupport.h"

#include "fixtures/harness/TestHarness.h"

#include <pybind11/eval.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace an::tests::runtime_backend
{

namespace
{

#ifdef _WIN32
constexpr const char* kModuleFilename = "an_extension_runtime_backend.dll";
constexpr const char* kModuleFilenameDebug = "an_extension_runtime_backend_d.dll";
#else
constexpr const char* kModuleFilename = "liban_extension_runtime_backend.so";
constexpr const char* kModuleFilenameDebug = "liban_extension_runtime_backend_d.so";
#endif

[[nodiscard]] std::filesystem::path executableDirectory()
{
    return an::tests::harness::executablePath().parent_path();
}

[[nodiscard]] std::filesystem::path bundledPythonExecutable()
{
#if defined(_WIN32)
    return repositoryFile(std::filesystem::path("python") / "python" / "tools_msvc" / "python.exe");
#else
    return repositoryFile(std::filesystem::path("python") / "python" / "tools_linux" / "bin" / "python3.14");
#endif
}

[[nodiscard]] std::filesystem::path repositoryPythonModules()
{
    const std::filesystem::path package =
        repositoryFile(std::filesystem::path("python") / "modules" / "afternight" / "__init__.py");
    return package.empty() ? package : package.parent_path().parent_path();
}

[[nodiscard]] bool setConfigString(PyConfig* config, wchar_t** target, const std::wstring& value, std::string* error)
{
    const PyStatus status = PyConfig_SetString(config, target, value.c_str());
    if (PyStatus_Exception(status))
    {
        if (error != nullptr)
        {
            *error = status.err_msg != nullptr ? status.err_msg : "a Python configuration string was refused";
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool appendSearchPath(PyConfig* config, const std::filesystem::path& path, std::string* error)
{
    if (path.empty())
    {
        return true;
    }
    const PyStatus status = PyWideStringList_Append(&config->module_search_paths, path.wstring().c_str());
    if (PyStatus_Exception(status))
    {
        if (error != nullptr)
        {
            *error = status.err_msg != nullptr ? status.err_msg : "a Python search path was refused";
        }
        return false;
    }
    return true;
}

bool gInterpreterStarted = false;

} // namespace

std::string BackendExports::lastErrorText() const
{
    if (lastError == nullptr)
    {
        return {};
    }
    const char* text = lastError();
    return text != nullptr ? std::string(text) : std::string();
}

std::filesystem::path backendModulePath()
{
#ifdef NDEBUG
    return executableDirectory() / kModuleFilename;
#else
    return executableDirectory() / kModuleFilenameDebug;
#endif
}

const BackendExports* loadBackend(std::string* error)
{
    static BackendExports exports;
    static bool loaded = false;
    if (loaded)
    {
        return exports.complete() ? &exports : nullptr;
    }
    loaded = true;
    const std::filesystem::path path = backendModulePath();
#ifdef _WIN32
    HMODULE handle = ::LoadLibraryW(path.wstring().c_str());
    if (handle == nullptr)
    {
        if (error != nullptr)
        {
            *error = "LoadLibrary refused " + path.string() + " with error " + std::to_string(::GetLastError());
        }
        return nullptr;
    }
    const auto resolve = [handle](const char* symbol) { return ::GetProcAddress(handle, symbol); };
#else
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        if (error != nullptr)
        {
            const char* reason = ::dlerror();
            *error = reason != nullptr ? reason : "dlopen refused " + path.string();
        }
        return nullptr;
    }
    const auto resolve = [handle](const char* symbol) { return ::dlsym(handle, symbol); };
#endif
    exports.abiVersion = reinterpret_cast<std::uint32_t (*)()>(resolve("an_runtime_backend_abi_version"));
    exports.probe = reinterpret_cast<int (*)()>(resolve("an_runtime_backend_probe"));
    exports.initialize = reinterpret_cast<int (*)()>(resolve("an_runtime_backend_initialize"));
    exports.shutdown = reinterpret_cast<void (*)()>(resolve("an_runtime_backend_shutdown"));
    exports.lastError = reinterpret_cast<const char* (*)()>(resolve("an_runtime_backend_last_error"));
    if (!exports.complete())
    {
        if (error != nullptr)
        {
            *error = "the runtime backend module is missing one of its five exports";
        }
        return nullptr;
    }
    return &exports;
}

std::filesystem::path pythonRuntimeRoot()
{
    return executableDirectory() / "PythonLib";
}

std::filesystem::path repositoryFile(const std::filesystem::path& relative)
{
    std::error_code error;
    std::filesystem::path root = std::filesystem::current_path(error);
    while (!error && !root.empty())
    {
        if (std::filesystem::exists(root / relative))
        {
            return (root / relative).lexically_normal();
        }
        const std::filesystem::path parent = root.parent_path();
        if (parent == root)
        {
            break;
        }
        root = parent;
    }
    return {};
}

std::string environmentText(const char* name)
{
    return an::tests::harness::environmentValue(name).value_or(std::string());
}

bool startInterpreter(std::string* error)
{
    if (gInterpreterStarted)
    {
        return true;
    }
    const std::filesystem::path root = pythonRuntimeRoot();
    const std::filesystem::path executable = bundledPythonExecutable();
    const std::filesystem::path modules = repositoryPythonModules();
    if (!std::filesystem::exists(root) || executable.empty() || modules.empty())
    {
        if (error != nullptr)
        {
            *error = "the interpreter runtime " + root.string() +
                     ", the bundled interpreter or the SDK package "
                     "is missing";
        }
        return false;
    }
#if defined(_WIN32)
    const std::filesystem::path standardLibrary = root / "Lib";
    const std::filesystem::path zipped = root.parent_path() / "python314.zip";
    const std::filesystem::path dynamic = root / "DLLs";
    const std::filesystem::path sitePackages = root / "Lib" / "site-packages";
#else
    const std::filesystem::path standardLibrary = root / "lib" / "python3.14";
    const std::filesystem::path zipped;
    const std::filesystem::path dynamic = root / "lib" / "python3.14" / "lib-dynload";
    const std::filesystem::path sitePackages = root / "lib" / "python3.14" / "site-packages";
#endif
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.parse_argv = 0;
    config.install_signal_handlers = 0;
    config.site_import = 0;
    config.user_site_directory = 0;
    config.safe_path = 1;
    config.module_search_paths_set = 1;
    const bool configured =
        setConfigString(&config, &config.home, root.wstring(), error) &&
        setConfigString(&config, &config.program_name, executable.wstring(), error) &&
        setConfigString(&config, &config.executable, executable.wstring(), error) &&
        setConfigString(&config, &config.base_executable, executable.wstring(), error) &&
        setConfigString(&config, &config.prefix, root.wstring(), error) &&
        setConfigString(&config, &config.exec_prefix, root.wstring(), error) &&
        setConfigString(&config, &config.base_prefix, root.wstring(), error) &&
        setConfigString(&config, &config.base_exec_prefix, root.wstring(), error) &&
        appendSearchPath(&config, sitePackages, error) && appendSearchPath(&config, zipped, error) &&
        appendSearchPath(&config, standardLibrary, error) && appendSearchPath(&config, dynamic, error);
    if (!configured)
    {
        PyConfig_Clear(&config);
        return false;
    }
    try
    {
        py::initialize_interpreter(&config, 0, nullptr, false);
    }
    catch (const std::exception& failure)
    {
        PyConfig_Clear(&config);
        if (error != nullptr)
        {
            *error = failure.what();
        }
        return false;
    }
    PyConfig_Clear(&config);
    gInterpreterStarted = true;
    py::gil_scoped_acquire gil;
    py::module_::import("sys").attr("path").attr("insert")(0, modules.string());
    return true;
}

void finalizeInterpreter()
{
    if (gInterpreterStarted && Py_IsInitialized())
    {
        py::finalize_interpreter();
    }
    gInterpreterStarted = false;
}

std::string requireSharedRuntime()
{
    static std::string failure;
    static bool attempted = false;
    if (attempted)
    {
        return failure;
    }
    attempted = true;
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    if (exports == nullptr)
    {
        failure = "the runtime backend did not load: " + error;
        return failure;
    }
    if (exports->initialize() != 1)
    {
        failure = "the runtime backend did not initialize: " + exports->lastErrorText();
        return failure;
    }
    if (!startInterpreter(&error))
    {
        failure = "the interpreter did not start: " + error;
    }
    return failure;
}

ScenarioRun runScenario(const std::string& name)
{
    ScenarioRun run;
    const std::filesystem::path executable = an::tests::harness::executablePath();
    const std::filesystem::path root(environmentText(an::tests::harness::kStorageRootVariable));
    const std::filesystem::path outputPath =
        (root.empty() ? std::filesystem::temp_directory_path() : root) / ("scenario-" + name + ".log");
    std::error_code ignored;
    std::filesystem::remove(outputPath, ignored);
#ifdef _WIN32
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = ::CreateFileW(outputPath.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritable,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        run.output = "the scenario's output file could not be created";
        return run;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
    const std::wstring wideName(name.begin(), name.end());
    ::SetEnvironmentVariableW(L"AFTERNIGHT_RUNTIME_BACKEND_SCENARIO", wideName.c_str());
    const BOOL started =
        ::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    ::SetEnvironmentVariableW(L"AFTERNIGHT_RUNTIME_BACKEND_SCENARIO", nullptr);
    ::CloseHandle(output);
    if (!started)
    {
        run.output = "the scenario's process could not be started";
        return run;
    }
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(process.hProcess, &code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    run.exitCode = static_cast<int>(code);
#else
    std::vector<std::string> environment;
    for (char** entry = environ; *entry != nullptr; ++entry)
    {
        if (std::string(*entry).rfind(std::string(kScenarioVariable) + "=", 0) != 0)
        {
            environment.emplace_back(*entry);
        }
    }
    environment.push_back(std::string(kScenarioVariable) + "=" + name);
    std::vector<char*> environmentPointers;
    for (std::string& entry : environment)
    {
        environmentPointers.push_back(entry.data());
    }
    environmentPointers.push_back(nullptr);
    std::string program = executable.string();
    std::vector<char*> arguments{program.data(), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t child = 0;
    const int spawned =
        posix_spawn(&child, program.c_str(), &actions, nullptr, arguments.data(), environmentPointers.data());
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0)
    {
        run.output = "the scenario's process could not be started";
        return run;
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0)
    {
    }
    run.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    std::ifstream stream(outputPath, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    run.output = text.str();
    return run;
}

void runPython(const char* source, py::dict& scope)
{
    // One dictionary is both the globals and the locals, so a function the source defines sees the names it binds.
    if (!scope.contains("__builtins__"))
    {
        scope["__builtins__"] = py::module_::import("builtins");
    }
    py::exec(source, scope, scope);
}

} // namespace an::tests::runtime_backend
