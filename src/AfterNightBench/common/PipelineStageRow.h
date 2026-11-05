// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - what a stage row does around a runner's stage, written once.
//
// A runner's side of a stage row is only its engine's stage. Putting the frames into the declared
// storage state, sampling resident memory over the clocked region alone, checking the stacked plane
// against the exact average of the frames and writing the record do not depend on the engine, so
// they are written once here and a runner hands its stage the hooks this builds.
#pragma once

#include "FitsAverageOracle.h"
#include "PipelineFixture.h"
#include "PipelineStageFixture.h"
#include "ResidentMemory.h"
#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <optional>
#include <string>

namespace an::bench
{

/// One stage row's measurement around a runner's stage.
class StageRowMeasurement
{
public:
    /// Prepares the hooks for `scenario`.
    ///
    /// @param scenario the stage row.
    /// @param resultScale what one unit of the engine's stacked plane is in the frames' code
    ///        units: one for a plane in codes, the full scale for one divided by it.
    /// @param range the range the engine writes its average inside, or nothing when it writes
    ///        the average as it is.
    StageRowMeasurement(const ScenarioSpec& scenario, double resultScale,
                        std::optional<FitsAverageResultRange> range = std::nullopt);

    StageRowMeasurement(const StageRowMeasurement&) = delete;
    StageRowMeasurement& operator=(const StageRowMeasurement&) = delete;

    /// The storage state the row declares, or nothing when it declares none this build knows, which
    /// the runner reports as a skip.
    [[nodiscard]] std::optional<StageCacheCondition> condition() const noexcept
    {
        return m_condition;
    }

    /// The hooks to hand the stage. `beforeClock` puts the frames into the row's storage state and
    /// starts the resident sampler; `afterClock` stops it and checks the plane.
    [[nodiscard]] const StageClockHooks& hooks() const noexcept
    {
        return m_hooks;
    }

    /// Writes a completed stage into `out`: its clocks, the peak resident size over the clocked
    /// region, the digest, the storage preparation, and the exact-average check.
    ///
    /// @param timing the stage's clocks.
    /// @param digest the digest of the stacked plane.
    /// @param counterName the pipeline's counter name for the stage, such as `masterBias`.
    /// @param out the result.
    void record(const PipelineStageTiming& timing, const std::string& digest, const char* counterName,
                ResultRecord& out) const;

private:
    std::optional<StageCacheCondition> m_condition;
    double m_resultScale = 1.0;
    std::optional<FitsAverageResultRange> m_range;
    double m_prepareMs = 0.0;
    std::uint64_t m_frames = 0u;
    FitsAverageCheck m_check;
    bool m_checked = false;
    ResidentMemorySampler m_sampler;
    StageClockHooks m_hooks;
};

} // namespace an::bench
