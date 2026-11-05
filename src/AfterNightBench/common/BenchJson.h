// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - JSON serialization and validation.
//
// The JSON form is the only thing that leaves the runner process and reaches the tools that read
// its results, so it is the actual interface of this system - the C++ types are just a convenient
// in-memory shape for it.
//
// Two rules make an archived result set readable years later, which matters because a recorded
// run is meant to outlive the branch that produced it:
//
//   - every enum is written as its name, never its ordinal; and
//   - parsing is total. A malformed or truncated file produces a list of messages, never a throw
//     escaping this API and never a partially populated object presented as complete.

#pragma once

#include "Results.h"
#include "Scenario.h"

#include <string>
#include <vector>

namespace an::bench::json
{

/// Outcome of a parse. `ok` is false when `errors` is non-empty; the output object is then
/// unspecified and must not be used.
struct ParseResult
{
    bool ok = false;
    std::vector<std::string> errors;

    explicit operator bool() const
    {
        return ok;
    }
};

// --- serialization -------------------------------------------------------------------------

[[nodiscard]] std::string toJson(const ScenarioSpec& scenario, bool pretty = true);
[[nodiscard]] std::string toJson(const std::vector<ScenarioSpec>& scenarios, bool pretty = true);
[[nodiscard]] std::string toJson(const RunManifest& run, bool pretty = true);
[[nodiscard]] std::string toJson(const CorpusManifest& corpus, bool pretty = true);

// --- parsing -------------------------------------------------------------------------------

[[nodiscard]] ParseResult parseScenario(const std::string& text, ScenarioSpec& out);
[[nodiscard]] ParseResult parseScenarios(const std::string& text, std::vector<ScenarioSpec>& out);
[[nodiscard]] ParseResult parseRun(const std::string& text, RunManifest& out);
[[nodiscard]] ParseResult parseCorpus(const std::string& text, CorpusManifest& out);

// --- files ---------------------------------------------------------------------------------

/// Read `path` into `out`. A missing or unreadable file is reported through the result, not by
/// throwing, so a runner can report it as a skipped scenario.
[[nodiscard]] ParseResult readRunFile(const std::string& path, RunManifest& out);
[[nodiscard]] ParseResult readCorpusFile(const std::string& path, CorpusManifest& out);
[[nodiscard]] ParseResult readScenariosFile(const std::string& path, std::vector<ScenarioSpec>& out);

/// Write `run` to `path`.
///
/// The write goes to a pending file beside `path` and is renamed into place, so an interrupted run
/// cannot leave a half-written result file that later reads as a valid but truncated run. This is
/// the same atomicity rule the engine's generated artifacts follow.
[[nodiscard]] bool writeRunFile(const std::string& path, const RunManifest& run, std::string& error);

/// Write `scenarios` to `path`, with the same atomicity as `writeRunFile`.
///
/// This is how a run's catalogue is handed to later runs: a dumped catalogue, filtered or batched,
/// is what `--scenarios` reads back, so every run of it executes a contract that came from one
/// place.
[[nodiscard]] bool writeScenariosFile(const std::string& path, const std::vector<ScenarioSpec>& scenarios,
                                      std::string& error);

} // namespace an::bench::json
