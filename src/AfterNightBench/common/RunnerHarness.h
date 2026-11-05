// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the runner harness.
//
// A runner differs from this harness in exactly one thing: how it maps a scenario onto the engine it
// links. Everything else - argument parsing, the build and environment manifests, the run loop, the
// skipped-scenario rule, and writing the result file - lives here, where the suite of this library
// tests it without an engine.
//
// This header stays engine-neutral like the rest of this library. It names no engine type; the
// adapter interface below is the entire surface an engine-linked runner implements.

#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace an::bench
{

/// Everything one runner invocation was told to do.
///
/// The fields that identify the run - host, commit, timestamp - may be supplied on the command line
/// or left for `resolveRunOptions` to detect. They are recorded either way: a result file that
/// cannot say which machine and which commit produced it is not publishable, so the runner refuses
/// to write one rather than filling in a plausible value.
struct RunOptions
{
    std::string outputPath; ///< Where the run manifest is written. Required.
    std::string scenarioPath; ///< Scenario file to run; empty means the adapter's own set.

    /// Where to write the scenario catalogue this run executed; empty means do not write it.
    ///
    /// The catalogue is the contract later runs are given, and deriving it from the runner rather
    /// than committing a copy is what stops it drifting from the adapter that defines it.
    std::string scenarioDumpPath;
    std::string corpusRoot; ///< Root for corpus-backed fixtures; empty when none was given.

    /// Corpus manifest to load; empty when the run uses synthetic fixtures only.
    ///
    /// Separate from `corpusRoot` because the two answer different questions: the manifest says
    /// which datasets exist and what they must hash to, the root says where this machine keeps
    /// them. The manifest is committed with the scenarios that reference it; the root is not,
    /// because the corpus is host-local.
    std::string corpusManifest;

    /// Walk the corpus manifest, report what resolves, and exit without measuring.
    ///
    /// The check to run before a long measurement and after building a manifest: finding a
    /// missing frame costs seconds here and forty minutes inside a benchmark.
    bool verifyCorpusOnly = false;

    std::string hostId; ///< Machine identity; detected when empty.
    std::string timestamp; ///< ISO-8601 run time; read from the clock when empty.
    std::string commit; ///< Repository commit; detected from git when empty.
    bool dirty = false; ///< Whether the checkout carried uncommitted changes.
    bool dirtyProvided = false; ///< True when `dirty` came from the command line, not detection.
    std::string engineCommit; ///< Linked engine commit; defaults to `commit` when empty.
    bool engineDirty = false; ///< Whether the linked engine checkout carried changes.
    bool engineDirtyProvided = false; ///< True when engine dirtiness was supplied explicitly.
    std::optional<bool> engineLto; ///< Linked-engine LTO from its build recipe; absent retains adapter provenance.
    std::string dependencyLockDigest; ///< SHA-256 of the dependency lock; detected when empty.

    std::string powerProfile; ///< Required on a laptop host, where it decides sustained clocks.
    bool otherwiseIdle = false; ///< Asserted by the operator; recorded either way.
    std::string notes;

    /// Where a pipeline row writes its calibrated and aligned intermediates and leaves them; empty
    /// writes them under the temporary directory and removes them with the run.
    ///
    /// It is how a later stage's input is prepared from exactly the frames the pipeline produced.
    /// The directory must be new or empty, and nothing the run writes there is removed.
    std::string keptPipelineScratch;

    std::uint32_t warmupIterations = 1;
    std::uint32_t repetitions = 5;

    /// Maximum threads the engine is asked to admit; zero uses the host default.
    ///
    /// A nonzero value is what a thread-scaling sweep sets. It is applied at the engine boundary,
    /// not merely written into the manifest, and each result still records what its operation was
    /// actually admitted.
    std::uint32_t maxThreads = 0u;

    /// Backend the engine must actually use; absent lets its dispatch table choose.
    ///
    /// Only concrete backends are accepted. A request is made with fallback forbidden, and a
    /// scenario that cannot use it is reported as skipped rather than timed under another label.
    std::optional<BackendId> forcedBackend;

    /// Where in each operation a stop is requested, as a fraction of the operation's own median
    /// time; absent when the run does not probe cancellation.
    ///
    /// After an operation has been timed and digested as usual, it runs once more under a stop token
    /// requested this far into its median time, and its result records how long it took to return
    /// after the request and what it returned. The timings and the digest are those of the ordinary
    /// run, taken before the probe.
    std::optional<double> cancelAtFraction;

    /// Whether an external controller actually interleaved this run with another one.
    ///
    /// A standalone runner cannot make this claim: it owns its repetitions and knows nothing of
    /// what ran before or after it. Only a controller that launches the separate runner
    /// invocations and controls their order may set it in an assembled manifest.
    bool randomInterleaving = false;

    /// A dry run proves the protocol without measuring: scenarios are parsed and validated, every
    /// one is reported as skipped, and no timing is published. It is what continuous integration
    /// runs, because a shared CI machine cannot produce a trustworthy number and a number nobody
    /// can trust is worse than no number.
    bool dryRun = false;
};

/// Usage text for `executable`, listing every option `parseRunOptions` accepts.
[[nodiscard]] std::string runnerUsage(const std::string& executable);

/// Parse a runner command line into `out`.
///
/// Returns false and sets `error` for an unknown option, a missing value, an unparsable number, or
/// a missing required option. Never throws and never writes to a stream.
[[nodiscard]] bool parseRunOptions(int argc, char** argv, RunOptions& out, std::string& error);

/// Fill in whatever `options` did not state: corpus root, host identity, timestamp, and commit.
///
/// Detection touches the environment and may spawn `git`, so it is separated from parsing: tests
/// and callers that already know these values set them and never reach the detection path.
void resolveRunOptions(RunOptions& options);

/// This machine's identity, from the platform's host-name environment variable.
///
/// Returns "unknown-host" when the environment names none. That value is deliberately not a
/// plausible host name: two runs recorded under it are not evidence they ran on the same machine,
/// and a reader can see that at a glance.
[[nodiscard]] std::string detectHostId();

/// The current time as an ISO-8601 UTC string.
[[nodiscard]] std::string currentTimestamp();

/// The checked-out commit, from `git rev-parse HEAD`, and whether the tree carried changes.
///
/// Returns an empty string when git is unavailable or the working directory is not a repository.
/// The caller then has no commit, and refusing to write the run is the correct outcome.
[[nodiscard]] std::string detectCommit(bool& dirty);

/// The engine-specific half of a benchmark run: the only thing a runner implements.
class EngineAdapter
{
public:
    virtual ~EngineAdapter() = default;

    /// A short version string for the runner binary itself.
    [[nodiscard]] virtual std::string runnerVersion() const = 0;

    /// Fill in the build-manifest fields only the linked engine can answer, such as the compiler
    /// that produced it and the configuration it was built in.
    virtual void describeEngine(BuildManifest& manifest) const = 0;

    /// Fill in the environment fields only the linked engine can answer.
    ///
    /// Instruction-set capability is the case that matters: it must be measured by the code that
    /// will dispatch on it, not assumed from the flags the runner was built with. The default does
    /// nothing, for an engine that cannot yet report any.
    virtual void describeEnvironment(EnvironmentManifest& manifest) const
    {
        (void)manifest;
    }

    /// Configure the adapter from the resolved run options, before any scenario runs.
    ///
    /// This is where an adapter picks up anything the command line told the run rather than
    /// the engine - the corpus manifest above all, since fixtures are the adapter's to build.
    /// Returns false with `error` set to fail the run: a manifest that was named and cannot be
    /// loaded is a configuration mistake, and continuing would silently skip every scenario
    /// that needed it while reporting a clean run.
    ///
    /// The default accepts any options and does nothing.
    virtual bool prepare(const RunOptions& options, std::string& error)
    {
        (void)options;
        (void)error;
        return true;
    }

    /// The scenarios this runner executes when the command line names no scenario file.
    [[nodiscard]] virtual std::vector<ScenarioSpec> scenarios() const = 0;

    /// Execute one scenario and fill `out`; `out.scenarioId` and `out.operationVersion` are already
    /// set when this is called.
    ///
    /// Return false with `skipReason` set when this engine cannot run the scenario - it does not
    /// implement the operation, a forced backend is unavailable, or a corpus dataset is absent. A
    /// skipped scenario is a reported outcome, never a failed run, and never a quiet substitution
    /// of something the engine can do instead.
    virtual bool run(const ScenarioSpec& scenario, const RunOptions& options, ResultRecord& out,
                     std::string& skipReason) = 0;
};

/// Assemble the run manifest for `scenarios` without touching the filesystem or the clock.
///
/// `options` must already be resolved: whatever it says about host, commit, and timestamp is what
/// the manifest records. In a dry run every scenario is reported as skipped and the adapter's
/// `run` is not called.
[[nodiscard]] RunManifest buildRun(const RunOptions& options, const std::vector<ScenarioSpec>& scenarios,
                                   EngineAdapter& adapter);

/// The whole runner: parse, resolve, load scenarios, run, validate, and write.
///
/// Returns the process exit code, which the runner returns unchanged:
///
///   0  the run completed and the manifest was written
///   1  the manifest failed validation, or it could not be written; nothing was published
///   3  bad arguments, or the scenario file could not be read
[[nodiscard]] int runBenchmarkMain(int argc, char** argv, EngineAdapter& adapter);

} // namespace an::bench
