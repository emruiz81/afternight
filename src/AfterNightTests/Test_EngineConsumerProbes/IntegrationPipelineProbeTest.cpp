// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the file-backed integration pipeline is asked, and why each question is one only a workflow
// over files can ask.
//
// Four questions: does the stacked frame carry the history of every stage that made it, and does every
// artifact name the work it satisfies; can a second run recognise every artifact from its header, and
// does a rewritten input invalidate exactly what depends on it; does a stop in the middle of the stack
// leave every finished file and no half-written one; and what can one progress display be built from.

#include "IntegrationPipelineProbe.h"
#include "ProbeSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/// How many lights every case stacks.
constexpr std::uint32_t kLights = 5u;

/// An engine context of four threads with the probe codecs installed.
///
/// @return the context; the case fails when the engine refuses.
[[nodiscard]] an::EngineContext makeEngine()
{
    an::Result<an::EngineContext> engine = probe::createProbeEngine(4u);
    EXPECT_TRUE(engine.hasValue()) << engine.status().message();
    return engine.hasValue() ? std::move(*engine) : an::EngineContext{};
}

/// The record of one stage in a report.
///
/// @param report the run's report.
/// @param stage which stage.
/// @return the record; a default record when the stage was not reached.
[[nodiscard]] probe::integration::StageRecord recordOf(const probe::integration::PipelineReport& report,
                                                       probe::integration::Stage stage)
{
    for (const probe::integration::StageRecord& record : report.stages)
    {
        if (record.stage == stage)
        {
            return record;
        }
    }
    return probe::integration::StageRecord{.stage = stage};
}

/// Whether any text contains a fragment.
[[nodiscard]] bool anyContains(const std::vector<std::string>& texts, std::string_view fragment)
{
    return std::any_of(texts.begin(), texts.end(),
                       [fragment](const std::string& text) { return text.find(fragment) != std::string::npos; });
}

/// An observer that records what it was told, and may stop the run when a stage's engine operation
/// first reports.
class RecordingObserver final : public probe::integration::PipelineObserver
{
public:
    /// An observer that never stops the run.
    RecordingObserver() = default;

    /// An observer that stops the run at a stage's first engine report.
    ///
    /// @param source the run's stop source.
    /// @param stopAt the stage.
    RecordingObserver(std::stop_source* source, probe::integration::Stage stopAt) : m_source(source), m_stopAt(stopAt)
    {
    }

    void onStageProgress(probe::integration::Stage stage, std::uint32_t completed,
                         std::uint32_t total) noexcept override
    {
        const std::scoped_lock lock(m_mutex);
        stageUnits.push_back({stage, completed, total});
    }

    void onEngineProgress(probe::integration::Stage stage, const an::ProgressReport& report) noexcept override
    {
        const std::scoped_lock lock(m_mutex);
        ++engineReports[static_cast<std::size_t>(stage)];
        if (report.total != 0u && report.completed > report.total)
        {
            overran = true;
        }
        if (m_source != nullptr && stage == m_stopAt)
        {
            m_source->request_stop();
        }
    }

    /// One workflow-level report.
    struct Unit
    {
        probe::integration::Stage stage;
        std::uint32_t completed;
        std::uint32_t total;
    };

    /// Every workflow-level report, in order.
    std::vector<Unit> stageUnits{};

    /// How many engine reports arrived during each stage.
    std::vector<std::uint64_t> engineReports = std::vector<std::uint64_t>(probe::integration::kStageCount, 0u);

    /// Whether an engine report ever said more was done than there was.
    bool overran = false;

private:
    std::mutex m_mutex;
    std::stop_source* m_source = nullptr;
    probe::integration::Stage m_stopAt = probe::integration::Stage::MasterBias;
};

} // namespace

TEST(IntegrationPipelineProbeTest, TheStackCarriesTheHistoryOfEveryStageAndEveryArtifactNamesItsWork)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const an::EngineContext engine = makeEngine();
    an::Result<probe::integration::Pipeline> pipeline =
        probe::integration::Pipeline::create(engine, scratch.file("project"), kLights);
    ASSERT_TRUE(pipeline.hasValue()) << probe::describeFailure(pipeline.status());

    std::stop_source stop;
    const an::Result<probe::integration::PipelineReport> report = pipeline->run(engine, stop.get_token(), nullptr);
    ASSERT_TRUE(report.hasValue()) << probe::describeFailure(report.status());

    ASSERT_EQ(report->stages.size(), probe::integration::kStageCount);
    for (const probe::integration::StageRecord& record : report->stages)
    {
        EXPECT_GT(record.produced, 0u) << probe::integration::stageName(record.stage);
        EXPECT_EQ(record.reused, 0u) << probe::integration::stageName(record.stage);
    }

    // Every light, the reference included, was matched and warped.
    EXPECT_EQ(report->registeredLights, kLights);

    // The history of the stacked frame is the workflow's record, walked through artifact identities:
    // the stack names the registered lights it read, each of those names the calibrated light it was
    // warped from, and each record carries the lineage its image had when it was written - the
    // operation's own transition and the publication.
    const probe::integration::ArtifactRecord* const stack = pipeline->artifact(report->stackIdentity);
    ASSERT_NE(stack, nullptr);
    EXPECT_TRUE(anyContains(stack->lineage, "Stack")) << ::testing::PrintToString(stack->lineage);
    EXPECT_TRUE(anyContains(stack->lineage, "light_stack.fits")) << ::testing::PrintToString(stack->lineage);
    ASSERT_EQ(stack->inputs.size(), kLights);
    for (const std::string& registeredIdentity : stack->inputs)
    {
        const probe::integration::ArtifactRecord* const registered = pipeline->artifact(registeredIdentity);
        ASSERT_NE(registered, nullptr);
        EXPECT_EQ(registered->stage, probe::integration::Stage::AlignLights);
        EXPECT_TRUE(anyContains(registered->lineage, "warpRegisteredImage"))
            << ::testing::PrintToString(registered->lineage);
        ASSERT_EQ(registered->inputs.size(), 1u);
        const probe::integration::ArtifactRecord* const calibrated = pipeline->artifact(registered->inputs.front());
        ASSERT_NE(calibrated, nullptr);
        EXPECT_EQ(calibrated->stage, probe::integration::Stage::CalibrateLights);
        EXPECT_TRUE(anyContains(calibrated->lineage, "calibrat")) << ::testing::PrintToString(calibrated->lineage);
        ASSERT_EQ(calibrated->inputs.size(), 3u);
        EXPECT_NE(pipeline->artifact(calibrated->inputs.front()), nullptr);
    }

    // What the stacked file itself carries, read back: the history of its own stage at most. The
    // registered frames it was stacked from were read from files, and a file carries its samples and
    // its identity but not what was done to it, so nothing before the stack reaches this far without
    // the workflow's record.
    EXPECT_FALSE(anyContains(report->stackLineage, "warpRegisteredImage"))
        << ::testing::PrintToString(report->stackLineage);

    // Both products name the work they satisfy in their own headers, and the two identities differ.
    EXPECT_EQ(report->stackIdentity.size(), 32u);
    EXPECT_EQ(report->drizzleIdentity.size(), 32u);
    EXPECT_NE(report->stackIdentity, report->drizzleIdentity);

    // The stacked sky is the calibrated sky: the pedestal and the dark are gone, and a vignetted
    // background of about seven hundred codes remains.
    EXPECT_GT(report->stackMedian, 400.0);
    EXPECT_LT(report->stackMedian, 1000.0);

    // The drizzle doubled the grid.
    EXPECT_EQ(report->drizzleWidth, 320u);
}

TEST(IntegrationPipelineProbeTest, ASecondRunReusesEveryArtifactFromItsHeaderAndARewrittenLightInvalidatesOnlyItsWork)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const an::EngineContext engine = makeEngine();
    an::Result<probe::integration::Pipeline> pipeline =
        probe::integration::Pipeline::create(engine, scratch.file("project"), kLights);
    ASSERT_TRUE(pipeline.hasValue()) << probe::describeFailure(pipeline.status());

    std::stop_source stop;
    const an::Result<probe::integration::PipelineReport> first = pipeline->run(engine, stop.get_token(), nullptr);
    ASSERT_TRUE(first.hasValue()) << probe::describeFailure(first.status());
    const std::vector<std::string> filesAfterFirst = pipeline->files();

    const an::Result<probe::integration::PipelineReport> second = pipeline->run(engine, stop.get_token(), nullptr);
    ASSERT_TRUE(second.hasValue()) << probe::describeFailure(second.status());
    for (const probe::integration::StageRecord& record : second->stages)
    {
        EXPECT_EQ(record.produced, 0u) << probe::integration::stageName(record.stage);
        EXPECT_GT(record.reused, 0u) << probe::integration::stageName(record.stage);
    }
    EXPECT_EQ(pipeline->files(), filesAfterFirst);
    EXPECT_EQ(second->stackIdentity, first->stackIdentity);

    // Rewrite one light that is not the reference. The masters do not read it, so they are reused;
    // its own calibration and warp are redone, and so is everything that combines every light.
    ASSERT_TRUE(pipeline->rewriteLight(engine, 3u).ok());
    const an::Result<probe::integration::PipelineReport> third = pipeline->run(engine, stop.get_token(), nullptr);
    ASSERT_TRUE(third.hasValue()) << probe::describeFailure(third.status());
    using probe::integration::Stage;
    EXPECT_EQ(recordOf(*third, Stage::MasterBias).reused, 1u);
    EXPECT_EQ(recordOf(*third, Stage::MasterDark).reused, 1u);
    EXPECT_EQ(recordOf(*third, Stage::MasterFlat).reused, 1u);
    EXPECT_EQ(recordOf(*third, Stage::CalibrateLights).produced, 1u);
    EXPECT_EQ(recordOf(*third, Stage::CalibrateLights).reused, kLights - 1u);
    EXPECT_EQ(recordOf(*third, Stage::AlignLights).produced, 1u);
    EXPECT_EQ(recordOf(*third, Stage::StackLights).produced, 1u);
    EXPECT_EQ(recordOf(*third, Stage::DrizzleLights).produced, 1u);
    EXPECT_NE(third->stackIdentity, first->stackIdentity);
}

TEST(IntegrationPipelineProbeTest, AStopDuringTheStackLeavesEveryFinishedArtifactAndNoPartialFile)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const an::EngineContext engine = makeEngine();
    an::Result<probe::integration::Pipeline> pipeline =
        probe::integration::Pipeline::create(engine, scratch.file("project"), kLights);
    ASSERT_TRUE(pipeline.hasValue()) << probe::describeFailure(pipeline.status());

    std::stop_source stop;
    RecordingObserver stopper(&stop, probe::integration::Stage::StackLights);
    const an::Result<probe::integration::PipelineReport> stopped = pipeline->run(engine, stop.get_token(), &stopper);
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled) << probe::describeFailure(stopped.status());

    // Everything before the stack is on disk under its own name; the stack is not, and nothing that
    // is not a finished artifact or an input is anywhere under the project.
    const std::vector<std::string> files = pipeline->files();
    const auto has = [&files](std::string_view name) {
        return std::find(files.begin(), files.end(), name) != files.end();
    };
    EXPECT_TRUE(has("masters/bias.fits"));
    EXPECT_TRUE(has("masters/dark.fits"));
    EXPECT_TRUE(has("masters/flat.fits"));
    for (std::uint32_t index = 0u; index < kLights; ++index)
    {
        EXPECT_TRUE(has("calibrated/light_" + std::to_string(index) + "_cal.fits"));
        EXPECT_TRUE(has("registered/light_" + std::to_string(index) + "_reg.fits"));
    }
    EXPECT_FALSE(has("integration/light_stack.fits"));
    for (const std::string& file : files)
    {
        EXPECT_TRUE(file.ends_with(".fits")) << "a partial file was left behind: " << file;
    }
    EXPECT_EQ(files.size(), 3u * 3u + kLights + 3u + kLights + kLights);

    // A new run after the stop reuses everything that finished and completes.
    std::stop_source resumed;
    const an::Result<probe::integration::PipelineReport> completed =
        pipeline->run(engine, resumed.get_token(), nullptr);
    ASSERT_TRUE(completed.hasValue()) << probe::describeFailure(completed.status());
    using probe::integration::Stage;
    EXPECT_EQ(recordOf(*completed, Stage::CalibrateLights).produced, 0u);
    EXPECT_EQ(recordOf(*completed, Stage::AlignLights).produced, 0u);
    EXPECT_EQ(recordOf(*completed, Stage::StackLights).produced, 1u);
}

TEST(IntegrationPipelineProbeTest, OneProgressDisplayIsBuiltFromFramesTheWorkflowCountsAndUnitsOperationsCount)
{
    const probe::ScratchDirectory scratch;
    ASSERT_TRUE(scratch.valid());
    const an::EngineContext engine = makeEngine();
    an::Result<probe::integration::Pipeline> pipeline =
        probe::integration::Pipeline::create(engine, scratch.file("project"), kLights);
    ASSERT_TRUE(pipeline.hasValue()) << probe::describeFailure(pipeline.status());

    std::stop_source stop;
    RecordingObserver observer;
    const an::Result<probe::integration::PipelineReport> report = pipeline->run(engine, stop.get_token(), &observer);
    ASSERT_TRUE(report.hasValue()) << probe::describeFailure(report.status());

    // The workflow's own count covers every stage, never goes backwards within one, and ends at its
    // total. It is the half of the display that is always available.
    using probe::integration::Stage;
    for (std::uint32_t stage = 0u; stage < probe::integration::kStageCount; ++stage)
    {
        std::uint32_t last = 0u;
        std::uint32_t total = 0u;
        for (const RecordingObserver::Unit& unit : observer.stageUnits)
        {
            if (static_cast<std::uint32_t>(unit.stage) != stage)
            {
                continue;
            }
            EXPECT_GE(unit.completed, last);
            last = unit.completed;
            total = unit.total;
        }
        EXPECT_GT(total, 0u) << probe::integration::stageName(static_cast<Stage>(stage));
        EXPECT_EQ(last, total) << probe::integration::stageName(static_cast<Stage>(stage));
    }

    // The engine's half. Every stage that reads a file, detects a star or runs a stack or a drizzle has
    // operations that count their own units; no report says more was done than there was.
    EXPECT_FALSE(observer.overran);
    for (const Stage stage :
         {Stage::MasterBias, Stage::CalibrateLights, Stage::RegisterLights, Stage::StackLights, Stage::DrizzleLights})
    {
        EXPECT_GT(observer.engineReports[static_cast<std::size_t>(stage)], 0u) << probe::integration::stageName(stage);
    }
    EXPECT_TRUE(std::find(report->engineStageNames.begin(), report->engineStageNames.end(), "decode FITS") !=
                report->engineStageNames.end());
    EXPECT_TRUE(std::find_if(report->engineStageNames.begin(), report->engineStageNames.end(),
                             [](const std::string& name) { return name.rfind("star detection", 0u) == 0u; }) !=
                report->engineStageNames.end());
}
