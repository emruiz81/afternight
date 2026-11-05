// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the runtime backend's suite shares: the backend module loaded by file name as the full host loads it, its five
/// C exports, the isolated interpreter the host configures, and lifecycle scenarios that each run in a process of
/// their own.
///
/// The suite is the backend's driver and nothing else's: it links the engine the backend links, its consumer runtime
/// and the platform library - to read the session capsule as the full host does - and no Qt and no product library,
/// so a module the backend brings into a process is the backend's. CPython is not restartable once NumPy has been
/// imported, so every case that starts or ends the backend's session or the interpreter runs as a scenario: this
/// executable started again with `AFTERNIGHT_RUNTIME_BACKEND_SCENARIO` naming it, judged by its exit code and output.
/// The binding cases share one process-wide runtime instead: the module loaded, its session open and the interpreter
/// started, all left in place until the process ends.
#pragma once

#include "RuntimePython.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace an::tests::runtime_backend
{

namespace py = pybind11;

/// The environment variable that makes this executable run one scenario instead of the suite.
inline constexpr const char* kScenarioVariable = "AFTERNIGHT_RUNTIME_BACKEND_SCENARIO";

/// The five C exports, resolved by name.
struct BackendExports
{
    std::uint32_t (*abiVersion)() = nullptr;
    int (*probe)() = nullptr;
    int (*initialize)() = nullptr;
    void (*shutdown)() = nullptr;
    const char* (*lastError)() = nullptr;

    /// Whether every export resolved.
    [[nodiscard]] bool complete() const noexcept
    {
        return abiVersion != nullptr && probe != nullptr && initialize != nullptr && shutdown != nullptr &&
               lastError != nullptr;
    }

    /// The last error as a string, empty when the export is missing or answers null.
    [[nodiscard]] std::string lastErrorText() const;
};

/// The backend module's path: beside this executable, under the configuration's name.
[[nodiscard]] std::filesystem::path backendModulePath();

/// Loads the backend module once for the process, as the full host loads it, and never unloads it.
///
/// @param error receives the loader's reason when the module cannot be loaded.
/// @return the exports, or null when the module or an export is missing.
[[nodiscard]] const BackendExports* loadBackend(std::string* error = nullptr);

/// The interpreter runtime deployed beside this executable.
[[nodiscard]] std::filesystem::path pythonRuntimeRoot();

/// A file of the repository, found by walking up from the working directory; empty when there is none.
[[nodiscard]] std::filesystem::path repositoryFile(const std::filesystem::path& relative);

/// An environment variable's value, or an empty string.
[[nodiscard]] std::string environmentText(const char* name);

/// Starts the isolated interpreter the full host configures - no site, no user directory, no signal handlers - with
/// the repository's SDK package first on its path. Starting it twice starts it once.
///
/// @param error receives the reason when it cannot start.
/// @return whether it runs.
[[nodiscard]] bool startInterpreter(std::string* error = nullptr);

/// Finalizes the interpreter `startInterpreter` started; for a scenario, which ends its process after.
void finalizeInterpreter();

/// The runtime the binding cases share: the backend loaded, its session open, the interpreter started.
///
/// @return an empty string when it is ready, the reason otherwise.
[[nodiscard]] std::string requireSharedRuntime();

/// What a scenario's process did.
struct ScenarioRun
{
    /// Its exit code; 0 when every check held.
    int exitCode = -1;

    /// What it wrote to its standard output and error.
    std::string output;
};

/// Runs a scenario in a new process of this executable.
///
/// @param name the scenario.
/// @return what it did.
[[nodiscard]] ScenarioRun runScenario(const std::string& name);

/// Runs the scenario a process was started for; defined beside the scenarios.
///
/// @param name the scenario.
/// @return the process's exit code: 0 when every check held, 2 for an unknown scenario, 1 otherwise.
[[nodiscard]] int runScenarioInThisProcess(const std::string& name);

/// Runs Python source in the shared interpreter with one dictionary as its global and local names, so a function it
/// defines sees the names it binds; the dictionary gains the builtins when it has none.
void runPython(const char* source, py::dict& scope);

} // namespace an::tests::runtime_backend
