// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - measurement records and run manifests.
//
// The schema below is everything a publishable measurement must record: what was measured, and
// what machine and build produced it. The manifest travels inside the result file rather than
// alongside it as a separate note, so two result sets can be held to the same conditions before
// any of their numbers are read together.

#pragma once

#include "Descriptors.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace an::bench
{

/// Repeated-trial timings for one scenario on one backend at one thread count.
///
/// The raw samples are kept, not just a summary. Deciding whether a change is real needs the
/// distribution; best-of-N reports the one trial that hides the variance the decision rests on.
struct TimingSamples
{
    std::vector<double> wallMs;
    std::vector<double> cpuMs;

    [[nodiscard]] bool empty() const
    {
        return wallMs.empty();
    }
    [[nodiscard]] double medianWallMs() const;
    [[nodiscard]] double minWallMs() const;
    [[nodiscard]] double maxWallMs() const;
    /// Dispersion as a fraction of the median. The run protocol reads this before it trusts a
    /// ratio, because an unstable machine invents differences that look like regressions.
    [[nodiscard]] double coefficientOfVariation() const;
};

/// Memory and allocation behaviour for one measured scenario.
///
/// The four allocation counters are optional because only an engine can answer them. An engine
/// that cannot report a counter leaves it absent; a zero here means a measured zero. The
/// distinction matters more than it looks: five counters that read zero because nobody wrote to
/// them are indistinguishable from a run that allocated nothing.
struct MemoryCounters
{
    std::optional<std::uint64_t> allocationCount;
    std::optional<std::uint64_t> allocatedBytes;
    std::optional<std::uint64_t> scratchHighWaterBytes;
    std::optional<std::uint64_t> retainedCacheBytes;

    /// Largest resident set observed while the measured body ran, in bytes.
    ///
    /// Sampled rather than accumulated - see `ResidentMemory.h` - so it is a lower bound on the
    /// true peak, and `RunManifest::residentSampleIntervalMs` is the resolution it was taken at.
    /// Zero means the platform declined to answer or nothing sampled it.
    std::uint64_t peakResidentBytes = 0;
};

/// One scenario's outcome from one runner.
struct ResultRecord
{
    std::string scenarioId;
    std::uint32_t operationVersion = 1;

    /// Set when the scenario did not run. A skipped result is a reportable outcome, never an
    /// absent one: a silently dropped scenario leaves a gap in the report that reads as coverage.
    bool skipped = false;
    std::string skipReason;

    BackendId requestedBackend = BackendId::Unspecified;
    BackendId actualBackend = BackendId::Unspecified; ///< Must equal the request or be skipped.

    /// The parallelism the measurement was taken under, as the engine's scheduler reports it.
    ///
    /// It describes the configuration two runs must share to be read together rather than
    /// promising that every kernel saturated it. An engine that caps an individual operation below
    /// this budget still reports the budget; what must never happen is a constant, which would let
    /// two differently-parallel runs read as though they matched.
    std::uint32_t threadCount = 1;

    TimingSamples timings;
    MemoryCounters memory;

    /// Digest over the complete output, not a sample of pixels drawn from it.
    std::string outputDigest;
    std::string fixtureDigest; ///< Proves two runs of the scenario were fed identical input.
    std::string scenarioDigest; ///< Proves parameters and the input contract were identical.

    CorrectnessClass correctnessClass = CorrectnessClass::BitExact;
    double tolerance = 0.0;
    /// Error against the scenario's oracle; required for a class that states a tolerance.
    std::optional<double> correctnessMetric;

    std::uint64_t bytesProcessed = 0;
    std::uint64_t elementsProcessed = 0;
    std::map<std::string, double> counters; ///< Optional cycles/cache/vector-width counters.

    /// Throughput in bytes per second from the median wall time, or 0 when unavailable.
    [[nodiscard]] double bytesPerSecond() const;

    [[nodiscard]] std::vector<std::string> validate() const;
};

/// Compiler, flags, and dependency state that produced a runner.
struct BuildManifest
{
    std::string commit;
    bool dirty = false;
    std::string compiler; ///< e.g. "MSVC 19.51.36252".
    std::string standardLibrary;
    std::string linker;
    std::string configuration; ///< "Release" or "Debug".
    std::string flags;
    bool lto = false;
    bool pgo = false;
    bool fastMath = false; ///< Must stay false; global fast math invalidates the numeric contract.
    std::map<std::string, std::string> dependencyVersions;
};

/// A quantity sampled repeatedly across a run, kept as its shape rather than as every sample.
///
/// Enough to see a clock droop under sustained load - which the run protocol calls the most common
/// source of invented results on a laptop - without carrying thousands of readings into every
/// manifest. `samples` of zero means nothing was sampled, which is a different fact from a measured
/// zero and has to stay distinguishable from one.
struct SampledRange
{
    double minimum = 0.0;
    double mean = 0.0;
    double maximum = 0.0;
    std::uint32_t samples = 0;
};

/// The machine a run happened on.
///
/// Absolute figures are never compared across machines of different class and vendor. `hostId` is what
/// makes that rule enforceable rather than advisory: a reader can see that two result sets came from
/// different machines.
struct EnvironmentManifest
{
    std::string hostId;
    std::string cpuModel;
    std::string microcodeVersion; ///< Firmware revision reported by the operating system.
    std::uint32_t physicalCores = 0;
    std::uint32_t logicalCores = 0;
    std::uint32_t numaNodes = 0; ///< NUMA nodes visible to the process.
    std::uint64_t memoryBytes = 0;
    std::string osName;
    std::string gpuModel;
    std::string driverVersion;
    std::vector<std::string> isaCapabilities; ///< Measured at run time, not assumed from build flags.
    std::string powerProfile; ///< Required on the laptop host. What the operator says they set.
    bool acPower = true;
    bool otherwiseIdle = false; ///< Asserted by the operator; recorded either way.

    /// The power policy the operating system reports, which is not the same claim as
    /// `powerProfile`: that one is the operator's label, this one is the machine. A run whose two
    /// disagree is the case worth catching, and folding them into one field would lose it.
    std::string powerPlan;

    /// Logical processors this process was permitted to run on, and which ones.
    ///
    /// A run confined to half the machine is not comparable with one that had all of it, and
    /// nothing else in this manifest would say so - the thread count a scenario reports is what the
    /// arena chose, not what the process was allowed.
    std::uint32_t affinityCores = 0;
    std::string affinityMask;

    /// The processor clock across the run, sampled rather than read once.
    SampledRange cpuFrequencyMhz;

    /// Share of the machine's processor capacity consumed by everything except this process, over
    /// the run. `otherwiseIdle` is the operator's assertion that nothing else was running; this is
    /// the measurement of it.
    double backgroundLoad = 0.0;

    /// Granularity of the process CPU clock, in milliseconds, measured on this host.
    ///
    /// The run protocol makes process CPU time mandatory for a publishable run, and the
    /// Windows process clock advances one scheduler tick at a time - about 15.6 ms. A
    /// scenario shorter than one tick therefore reports 0 or a whole tick, and two trials of
    /// the same work can differ several-fold from quantization alone. Recording the
    /// resolution is what lets a reader tell a measured CPU time from a quantized one; a
    /// scenario whose wall time does not comfortably exceed it has no usable CPU number.
    double cpuClockResolutionMs = 0.0;
    std::string notes;
};

/// Everything one runner produced in one invocation.
struct RunManifest
{
    /// Bumped when the on-disk shape changes incompatibly. A reader refuses a version it does not
    /// know rather than guessing at a field's meaning.
    static constexpr int kSchemaVersion = 3;

    int schemaVersion = kSchemaVersion;
    std::string runnerVersion;
    std::string timestamp; ///< ISO-8601, supplied by the runner; the library never reads a clock.

    BuildManifest runnerBuild; ///< Provenance of the engine-neutral runner code.
    BuildManifest build; ///< Provenance of the linked engine; may differ from the runner checkout.
    std::string dependencyLockDigest; ///< SHA-256 of the complete dependency lock used to build.
    EnvironmentManifest environment;

    std::uint32_t warmupIterations = 0;
    std::uint32_t repetitions = 0;
    bool randomInterleaving = false;

    /// Interval the per-scenario resident peak was sampled at, in milliseconds.
    ///
    /// Travels with the run for the same reason the CPU clock's resolution does: a peak
    /// sampled every few milliseconds can miss a shorter spike, so the figure is a lower
    /// bound whose tightness this number describes.
    std::uint32_t residentSampleIntervalMs = 0;
    std::string corpusRoot;

    std::vector<ResultRecord> results;

    [[nodiscard]] std::vector<std::string> validate() const;
    [[nodiscard]] const ResultRecord* find(const std::string& scenarioId) const;
};

} // namespace an::bench
