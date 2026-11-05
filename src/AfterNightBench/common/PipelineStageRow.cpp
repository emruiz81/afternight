// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "PipelineStageRow.h"

#include "FileCache.h"

#include <chrono>

namespace an::bench
{

StageRowMeasurement::StageRowMeasurement(const ScenarioSpec& scenario, double resultScale,
                                         std::optional<FitsAverageResultRange> range)
    : m_condition(stageCacheCondition(scenario)), m_resultScale(resultScale), m_range(range)
{
    m_hooks.beforeClock = [this](StageFramePaths paths, std::string& error) {
        if (!m_condition)
        {
            error = "the stage row declares no storage state this runner knows";
            return false;
        }
        const auto started = std::chrono::steady_clock::now();
        const bool ready = *m_condition == StageCacheCondition::Cold ? dropFilesFromCache(paths, error)
                                                                     : readFilesThroughCache(paths, error);
        m_prepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        m_frames = paths.size();
        if (ready)
        {
            m_sampler.start();
        }
        return ready;
    };
    m_hooks.afterClock = [this](StageFramePaths paths, std::span<const float> plane) {
        m_sampler.stop();
        m_check = checkFitsAverage(paths, plane, m_resultScale, m_range);
        m_checked = true;
    };
}

void StageRowMeasurement::record(const PipelineStageTiming& timing, const std::string& digest, const char* counterName,
                                 ResultRecord& out) const
{
    out.timings.wallMs.push_back(timing.wallMs);
    out.timings.cpuMs.push_back(timing.cpuMs);
    out.memory.peakResidentBytes = m_sampler.peakBytes();
    out.outputDigest = digest;
    out.counters[std::string("stage.") + counterName + "Ms"] = timing.wallMs;
    out.counters[std::string("stage.") + counterName + "CpuMs"] = timing.cpuMs;
    out.counters["stageCacheCondition"] = m_condition == StageCacheCondition::Hot ? 1.0 : 0.0;
    out.counters["stageCachePrepareMs"] = m_prepareMs;
    out.counters["stageFrames"] = static_cast<double>(m_frames);
    FitsAverageCheck check = m_check;
    if (!m_checked)
    {
        check = FitsAverageCheck{};
        check.problem = "the stage handed over no plane";
    }
    recordFitsAverageCheck(check, out);
}

} // namespace an::bench
