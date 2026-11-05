// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "Results.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace an::bench
{
namespace
{

bool isLowerHexDigest(const std::string& value)
{
    return value.size() == 64u && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
           });
}

void validateBuild(const BuildManifest& build, const char* label, std::vector<std::string>& problems)
{
    const auto require = [&](const std::string& value, const char* field) {
        if (value.empty())
        {
            problems.push_back(std::string("run manifest has no ") + label + ' ' + field);
        }
    };
    require(build.commit, "commit");
    require(build.compiler, "compiler");
    require(build.standardLibrary, "standard library");
    require(build.linker, "linker");
    require(build.configuration, "configuration");
    require(build.flags, "compiler flags");
    if (build.dependencyVersions.empty())
    {
        problems.push_back(std::string("run manifest has no ") + label + " dependency versions");
    }
    if (build.fastMath)
    {
        problems.push_back(std::string("run manifest reports fast math in the ") + label +
                           "; that invalidates the numeric contract");
    }
}

} // namespace

double TimingSamples::medianWallMs() const
{
    if (wallMs.empty())
    {
        return 0.0;
    }
    std::vector<double> sorted = wallMs;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t middle = sorted.size() / 2;
    return sorted.size() % 2 == 1 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) * 0.5;
}

double TimingSamples::minWallMs() const
{
    return wallMs.empty() ? 0.0 : *std::min_element(wallMs.begin(), wallMs.end());
}

double TimingSamples::maxWallMs() const
{
    return wallMs.empty() ? 0.0 : *std::max_element(wallMs.begin(), wallMs.end());
}

double TimingSamples::coefficientOfVariation() const
{
    if (wallMs.size() < 2)
    {
        return 0.0;
    }

    const double mean = std::accumulate(wallMs.begin(), wallMs.end(), 0.0) / static_cast<double>(wallMs.size());
    if (mean <= 0.0)
    {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (double sample : wallMs)
    {
        const double delta = sample - mean;
        sumSquares += delta * delta;
    }
    const double variance = sumSquares / static_cast<double>(wallMs.size() - 1);
    return std::sqrt(variance) / mean;
}

double ResultRecord::bytesPerSecond() const
{
    const double median = timings.medianWallMs();
    if (median <= 0.0 || bytesProcessed == 0)
    {
        return 0.0;
    }
    return static_cast<double>(bytesProcessed) / (median / 1000.0);
}

std::vector<std::string> ResultRecord::validate() const
{
    std::vector<std::string> problems;

    if (scenarioId.empty())
    {
        problems.push_back("result has an empty scenarioId");
    }

    if (skipped)
    {
        if (skipReason.empty())
        {
            problems.push_back("result '" + scenarioId + "' is skipped without a reason");
        }
        // A skipped result carries no timings or digest; that is the whole point of skipping.
        return problems;
    }

    if (timings.empty())
    {
        problems.push_back("result '" + scenarioId + "' has no timing samples and is not skipped");
    }
    if (outputDigest.empty())
    {
        problems.push_back("result '" + scenarioId + "' has no output digest");
    }
    if (fixtureDigest.empty())
    {
        problems.push_back("result '" + scenarioId + "' has no fixture digest");
    }
    if (scenarioDigest.empty())
    {
        problems.push_back("result '" + scenarioId + "' has no scenario digest");
    }
    if (threadCount == 0)
    {
        problems.push_back("result '" + scenarioId + "' has threadCount 0");
    }

    if (timings.cpuMs.size() != timings.wallMs.size())
    {
        problems.push_back("result '" + scenarioId + "' has different wall and CPU sample counts");
    }
    const auto invalidTiming = [](double value) { return !std::isfinite(value) || value < 0.0; };
    if (std::any_of(timings.wallMs.begin(), timings.wallMs.end(), invalidTiming) ||
        std::any_of(timings.cpuMs.begin(), timings.cpuMs.end(), invalidTiming))
    {
        problems.push_back("result '" + scenarioId + "' has a non-finite or negative timing sample");
    }
    if (correctnessMetric.has_value() && (!std::isfinite(*correctnessMetric) || *correctnessMetric < 0.0))
    {
        problems.push_back("result '" + scenarioId + "' has an invalid correctness metric");
    }
    if (actualBackend == BackendId::Unspecified)
    {
        problems.push_back("result '" + scenarioId + "' does not identify the backend that ran");
    }

    // An unsupported forced backend must skip with a reason. Running something else while keeping
    // the requested label is how a measurement quietly stops measuring what it claims to measure.
    if (requestedBackend != BackendId::Unspecified && actualBackend != requestedBackend)
    {
        problems.push_back("result '" + scenarioId + "' requested backend " + std::string(toString(requestedBackend)) +
                           " but ran " + std::string(toString(actualBackend)) +
                           "; an unavailable backend must be skipped, not substituted");
    }

    return problems;
}

std::vector<std::string> RunManifest::validate() const
{
    std::vector<std::string> problems;

    if (schemaVersion != kSchemaVersion)
    {
        problems.push_back("run schemaVersion " + std::to_string(schemaVersion) + " is not the supported version " +
                           std::to_string(kSchemaVersion));
    }
    if (runnerVersion.empty())
    {
        problems.push_back("run manifest has no runner version");
    }
    if (timestamp.empty())
    {
        problems.push_back("run manifest has no timestamp");
    }
    validateBuild(runnerBuild, "runner build", problems);
    validateBuild(build, "engine build", problems);
    if (!isLowerHexDigest(dependencyLockDigest))
    {
        problems.push_back("run manifest has no valid dependency-lock SHA-256 digest");
    }
    if (environment.hostId.empty())
    {
        problems.push_back("run manifest has no hostId");
    }
    if (environment.hostId == "unknown-host")
    {
        problems.push_back("run manifest has unknown host identity");
    }
    if (environment.cpuModel.empty())
    {
        problems.push_back("run manifest has no CPU model");
    }
    if (environment.microcodeVersion.empty())
    {
        problems.push_back("run manifest has no CPU microcode version");
    }
    if (environment.physicalCores == 0 || environment.logicalCores == 0)
    {
        problems.push_back("run manifest has incomplete CPU topology");
    }
    if (environment.numaNodes == 0)
    {
        problems.push_back("run manifest has no NUMA topology");
    }
    if (environment.memoryBytes == 0)
    {
        problems.push_back("run manifest has no physical-memory size");
    }
    if (environment.osName.empty())
    {
        problems.push_back("run manifest has no operating-system identity");
    }
    if (environment.gpuModel.empty() || environment.driverVersion.empty())
    {
        problems.push_back("run manifest does not state GPU and driver applicability");
    }

    std::set<std::string> scenarioIds;
    for (const ResultRecord& record : results)
    {
        auto recordProblems = record.validate();
        problems.insert(problems.end(), recordProblems.begin(), recordProblems.end());
        if (!record.scenarioId.empty() && !scenarioIds.insert(record.scenarioId).second)
        {
            problems.push_back("run manifest has duplicate scenarioId '" + record.scenarioId + "'");
        }
        if (!record.skipped && repetitions != 0 && record.timings.wallMs.size() != repetitions)
        {
            problems.push_back("result '" + record.scenarioId + "' has " +
                               std::to_string(record.timings.wallMs.size()) + " timing samples, expected " +
                               std::to_string(repetitions));
        }
    }

    return problems;
}

const ResultRecord* RunManifest::find(const std::string& scenarioId) const
{
    const auto at =
        std::find_if(results.begin(), results.end(), [&](const ResultRecord& r) { return r.scenarioId == scenarioId; });
    return at == results.end() ? nullptr : &*at;
}

} // namespace an::bench
