// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the ImageIntegration-shaped probe is asked, and why each question is one a unit test of a
// single operation could not ask.
//
// The four questions are the four properties a multi-stage consumer needs from an engine: that the
// metadata arriving at the last stage describes what actually happened; that a stage's artifact can
// be recognised as still valid without reading a pixel of it; that a stop leaves the finished work
// intact; and that one progress display can be built out of operations that each count their own
// units.

#include "IntegrationOrchestrationProbe.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

/// The engine context every probe run works in.
///
/// @return a context admitting four threads, or an invalid one when the engine refuses.
[[nodiscard]] an::EngineContext makeEngine()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

/// An observer that records the stage boundaries it was told about, and may stop the run at one.
///
/// The stopping half is how the cancellation case reaches the middle of the workflow: a consumer
/// cancelling a stack presses a button between two stages, not inside an inner loop, and a probe
/// that could only cancel before the first operation would prove nothing about the artifacts the
/// earlier stages left behind.
class RecordingObserver final : public probe::WorkflowObserver
{
public:
    /// Builds an observer that never stops the run.
    RecordingObserver() = default;

    /// Builds an observer that stops the run once a number of stages have finished.
    ///
    /// @param source the stop source the run was started with.
    /// @param stopAfterStages how many stage boundaries to allow before requesting the stop.
    RecordingObserver(std::stop_source* source, std::uint32_t stopAfterStages) noexcept
        : m_source(source), m_stopAfter(stopAfterStages)
    {
    }

    void onStageCompleted(std::uint32_t completed, std::uint32_t total, probe::Stage stage) noexcept override
    {
        completedStages.push_back(completed);
        totals.push_back(total);
        names.push_back(probe::stageName(stage));
        if (m_source != nullptr && completed == m_stopAfter)
        {
            m_source->request_stop();
        }
    }

    /// How far the workflow said it had got, at each boundary.
    std::vector<std::uint32_t> completedStages{};

    /// How far it said it had to go, at each boundary.
    std::vector<std::uint32_t> totals{};

    /// Which stage each boundary was.
    std::vector<std::string> names{};

private:
    /// Where the stop is requested, or null for an observer that only records.
    std::stop_source* m_source = nullptr;

    /// How many boundaries to allow first.
    std::uint32_t m_stopAfter = 0u;
};

/// Whether a lineage holds an entry whose text starts with a prefix.
///
/// @param lineage the recorded entries.
/// @param prefix what to look for at the start of one.
/// @return true when an entry begins with it.
[[nodiscard]] bool lineageMentions(const std::vector<std::string>& lineage, const std::string& prefix)
{
    for (const std::string& entry : lineage)
    {
        if (entry.rfind(prefix, 0u) == 0u)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(IntegrationOrchestrationProbeTest, TheIntegratedFrameCarriesTheLineageOfEveryStageThatTouchedIt)
{
    const an::EngineContext engine = makeEngine();
    an::Result<probe::Workflow> workflow = probe::Workflow::create(engine, 3u, 3u);
    ASSERT_TRUE(workflow.hasValue()) << workflow.status().message();

    std::stop_source stop;
    RecordingObserver observer;
    const an::Result<probe::RunReport> report = workflow->run(stop.get_token(), &observer);
    ASSERT_TRUE(report.hasValue()) << report.status().message();

    ASSERT_EQ(report->stages.size(), 6u);
    for (const probe::StageRecord& stage : report->stages)
    {
        EXPECT_TRUE(stage.executed) << probe::stageName(stage.stage);
    }

    // The lineage is why a transition is carried through six stages at all: every entry is something
    // an operation or the workflow declared, in the order it happened, and nothing invented it. It
    // only reaches here because every stage seeds its destination from its input - the engine does
    // not carry metadata across an operation.
    ASSERT_FALSE(report->integratedLineage.empty());
    EXPECT_TRUE(lineageMentions(report->integratedLineage, "probe::Workflow::calibrate"));
    EXPECT_TRUE(lineageMentions(report->integratedLineage, "probe::Workflow::normalize"));
    EXPECT_TRUE(lineageMentions(report->integratedLineage, "an::executePointwiseProgram"));
    EXPECT_EQ(report->integratedLineage.back().rfind("probe::Workflow::stateOutput", 0u), 0u);
    EXPECT_TRUE(report->statedOutputIntent);

    // Two calibration chains, the workflow's calibration statement, one normalization chain and its
    // statement, two accumulations and one divide, then the output decision.
    EXPECT_GE(report->integratedLineage.size(), 8u);

    // The integrated frame's semantic generation is zero, and that is correct rather than a gap.
    // The metadata travelled here because each stage seeded its destination from its input, but a
    // generation is a counter on one image and a new image starts at zero. Nothing the last two
    // chains did changed what their result means, so nothing moved it. A consumer that wanted to
    // know whether the meaning had changed since some earlier point has to key on the inputs it
    // knows about, which is what this workflow's artifact key does.
    EXPECT_EQ(report->integratedSemanticGeneration, 0u);

    // The defect mask covers the frame and every synthetic sample is inside the selected range.
    EXPECT_EQ(report->totalPixels, 24u * 16u);
    EXPECT_EQ(report->validPixels, report->totalPixels);

    // Normalization scales each light onto the reference frame's mean, so the average of the
    // normalized lights is that mean.
    EXPECT_GT(report->integratedMean, 0.0);

    EXPECT_EQ(observer.completedStages, (std::vector<std::uint32_t>{1u, 2u, 3u, 4u, 5u, 6u}));
    EXPECT_EQ(observer.names.front(), "MasterDark");
    EXPECT_EQ(observer.names.back(), "StateOutput");
    EXPECT_EQ(report->workflowProgressReports, 6u);
}

TEST(IntegrationOrchestrationProbeTest, ASecondRunReusesEveryArtifactWithoutReadingAPixel)
{
    const an::EngineContext engine = makeEngine();
    an::Result<probe::Workflow> workflow = probe::Workflow::create(engine, 3u, 3u);
    ASSERT_TRUE(workflow.hasValue()) << workflow.status().message();

    std::stop_source stop;
    const an::Result<probe::RunReport> first = workflow->run(stop.get_token(), nullptr);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    const std::size_t cachedAfterFirst = workflow->cachedArtifactCount();
    EXPECT_GT(cachedAfterFirst, 0u);

    const an::Result<probe::RunReport> second = workflow->run(stop.get_token(), nullptr);
    ASSERT_TRUE(second.hasValue()) << second.status().message();

    // Nothing changed, so every cacheable stage recognised its own artifact. The mask and the output
    // stages are not cached - neither produces an image - so they run again.
    for (const probe::StageRecord& stage : second->stages)
    {
        if (stage.stage == probe::Stage::DefectMask || stage.stage == probe::Stage::StateOutput)
        {
            continue;
        }
        EXPECT_TRUE(stage.reused) << probe::stageName(stage.stage);
        EXPECT_EQ(stage.operations, 0u) << probe::stageName(stage.stage);
    }
    EXPECT_EQ(workflow->cachedArtifactCount(), cachedAfterFirst);
    EXPECT_DOUBLE_EQ(second->integratedMean, first->integratedMean);

    // And the half that makes the key worth having: writing to a frame moves its pixel generation,
    // and every artifact derived from it stops matching. The master dark does not depend on the
    // light, so it is still reused - the discrimination a whole-workflow timestamp would not give.
    ASSERT_TRUE(workflow->touchReferenceLight().ok());
    const an::Result<probe::RunReport> third = workflow->run(stop.get_token(), nullptr);
    ASSERT_TRUE(third.hasValue()) << third.status().message();

    bool masterDarkReused = false;
    bool calibrateExecuted = false;
    bool integrateExecuted = false;
    for (const probe::StageRecord& stage : third->stages)
    {
        masterDarkReused = masterDarkReused || (stage.stage == probe::Stage::MasterDark && stage.reused);
        calibrateExecuted = calibrateExecuted || (stage.stage == probe::Stage::Calibrate && stage.executed);
        integrateExecuted = integrateExecuted || (stage.stage == probe::Stage::Integrate && stage.executed);
    }
    EXPECT_TRUE(masterDarkReused);
    EXPECT_TRUE(calibrateExecuted);
    EXPECT_TRUE(integrateExecuted);
}

TEST(IntegrationOrchestrationProbeTest, AStopBetweenTwoStagesLeavesTheFinishedArtifactsIntact)
{
    const an::EngineContext engine = makeEngine();
    an::Result<probe::Workflow> workflow = probe::Workflow::create(engine, 3u, 3u);
    ASSERT_TRUE(workflow.hasValue()) << workflow.status().message();

    std::stop_source stop;
    RecordingObserver observer{&stop, 1u};
    const an::Result<probe::RunReport> stopped = workflow->run(stop.get_token(), &observer);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);

    // The master dark finished before the stop was asked for, so it is in the cache and a later run
    // reuses it rather than repeating work that was already paid for.
    EXPECT_EQ(workflow->cachedArtifactCount(), 1u);

    std::stop_source resumed;
    RecordingObserver secondObserver;
    const an::Result<probe::RunReport> completed = workflow->run(resumed.get_token(), &secondObserver);
    ASSERT_TRUE(completed.hasValue()) << completed.status().message();
    ASSERT_EQ(completed->stages.size(), 6u);
    EXPECT_TRUE(completed->stages.front().reused);
    EXPECT_EQ(completed->stages.front().operations, 0u);
}

TEST(IntegrationOrchestrationProbeTest, TheWorkflowCountsStagesBecauseTheEngineCountsOperations)
{
    const an::EngineContext engine = makeEngine();
    an::Result<probe::Workflow> workflow = probe::Workflow::create(engine, 3u, 3u);
    ASSERT_TRUE(workflow.hasValue()) << workflow.status().message();

    std::stop_source stop;
    RecordingObserver observer;
    const an::Result<probe::RunReport> report = workflow->run(stop.get_token(), &observer);
    ASSERT_TRUE(report.hasValue()) << report.status().message();

    // Every boundary states the same total, and the completed count never goes backwards. That is
    // what a progress display needs and what the engine cannot provide on its own: its reports carry
    // one operation's own units, and this run started more than a dozen operations.
    ASSERT_FALSE(observer.totals.empty());
    for (const std::uint32_t total : observer.totals)
    {
        EXPECT_EQ(total, observer.totals.front());
    }
    for (std::size_t index = 1u; index < observer.completedStages.size(); ++index)
    {
        EXPECT_GT(observer.completedStages[index], observer.completedStages[index - 1u]);
    }
    EXPECT_EQ(observer.completedStages.back(), observer.totals.front());

    // And the fact that makes the workflow-level counter necessary rather than merely convenient:
    // every operation this workflow runs is a single pass over one frame, and a single pass reports no
    // progress of its own - its caller counts frames - so a display built only from the engine's sink
    // would never move.
    EXPECT_EQ(report->engineProgressReports, 0u);
}
