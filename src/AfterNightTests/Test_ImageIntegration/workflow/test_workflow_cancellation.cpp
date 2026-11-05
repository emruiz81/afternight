// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QDir>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <thread>

#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"

namespace
{

// Test stage that blocks until signaled, allowing cancellation testing
class BlockingTestStage : public IWorkflowStage
{
public:
    BlockingTestStage(std::atomic<bool>& started, std::atomic<bool>& shouldBlock)
        : m_started(started), m_shouldBlock(shouldBlock)
    {
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("BlockingTestStage");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    [[nodiscard]] bool shouldSkip(const WorkflowManifest&) const override
    {
        return false;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken& token, ProgressReporter&) override
    {
        m_started.store(true, std::memory_order_release);

        // Spin-wait while blocking is requested and not cancelled
        while (m_shouldBlock.load() && !token.isCancellationRequested())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (token.isCancellationRequested())
        {
            return StageResult::cancelled();
        }
        return StageResult::success();
    }

private:
    std::atomic<bool>& m_started;
    std::atomic<bool>& m_shouldBlock;
};

// Simple stage that just succeeds
class SuccessStage : public IWorkflowStage
{
public:
    explicit SuccessStage(std::atomic<int>& counter) : m_counter(counter) {}

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("SuccessStage");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::Dark;
    }

    [[nodiscard]] bool shouldSkip(const WorkflowManifest&) const override
    {
        return false;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter&) override
    {
        m_counter.fetch_add(1);
        return StageResult::success();
    }

private:
    std::atomic<int>& m_counter;
};

class PhaseTimingStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("LightsStacking");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::LightsStacking;
    }

    [[nodiscard]] bool shouldSkip(const WorkflowManifest&) const override
    {
        return false;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter&) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        StageResult result = StageResult::success();
        result.phaseTimings.push_back({.stageName = QStringLiteral("Final Master WCS"),
                                       .status = QStringLiteral("completed"),
                                       .durationMs = 5,
                                       .groupsProcessed = 2});
        return result;
    }
};

WorkflowManifest makeMinimalManifest(const QString& outputDirectory)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = outputDirectory;

    // Add a minimal registration group so manifest validation passes
    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("test|64x64|TestObject")};
    registration.resolution = QStringLiteral("64x64");
    registration.object = QStringLiteral("TestObject");
    manifest.registrationGroups.push_back(registration);

    return manifest;
}

} // namespace

class WorkflowCancellationTest : public ::testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(WorkflowCancellationTest, CancelBeforeExecutionPreventsStages)
{
    WorkflowExecutor executor;
    std::atomic<int> stageCounter{0};
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    executor.addStage(std::make_unique<SuccessStage>(stageCounter));

    // Cancel before execution
    executor.requestCancel();

    auto result = executor.execute(makeMinimalManifest(tempDir.path()));

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_TRUE(result.executorTiming.captured);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
    EXPECT_EQ(stageCounter.load(), 0) << "Stage should not have executed after pre-cancel";
}

TEST_F(WorkflowCancellationTest, CancelDuringExecutionStopsWorkflow)
{
    WorkflowExecutor executor;
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    std::atomic<bool> stageStarted{false};
    std::atomic<bool> shouldBlock{true};
    std::atomic<int> secondStageCounter{0};

    executor.addStage(std::make_unique<BlockingTestStage>(stageStarted, shouldBlock));
    executor.addStage(std::make_unique<SuccessStage>(secondStageCounter));

    WorkflowExecutor::Result result;

    std::thread worker([&]() { result = executor.execute(makeMinimalManifest(tempDir.path())); });

    // Wait for stage to start, but never hang the test indefinitely.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!stageStarted.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!stageStarted.load(std::memory_order_acquire))
    {
        executor.requestCancel();
        shouldBlock.store(false);
        worker.join();
        FAIL() << "Stage did not start within timeout; possible manifest validation failure or executor early exit.";
    }

    // Request cancellation while stage is running
    executor.requestCancel();

    // Allow stage to exit its loop
    shouldBlock.store(false);

    worker.join();

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(secondStageCounter.load(), 0) << "Second stage should not execute after cancellation";
}

TEST_F(WorkflowCancellationTest, CancelDoesNotCrashWhenNotRunning)
{
    WorkflowProgressController controller(nullptr, nullptr);

    // Should not crash when cancelling without an active workflow
    EXPECT_NO_THROW(controller.cancel());
}

TEST_F(WorkflowCancellationTest, IsCancelRequestedReflectsState)
{
    WorkflowExecutor executor;

    EXPECT_FALSE(executor.isCancelRequested());

    executor.requestCancel();

    EXPECT_TRUE(executor.isCancelRequested());
}

TEST_F(WorkflowCancellationTest, CancellationIsThreadSafe)
{
    WorkflowExecutor executor;
    constexpr int kNumThreads = 10;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int i = 0; i < kNumThreads; ++i)
    {
        threads.emplace_back([&executor]() {
            executor.requestCancel();
            (void)executor.isCancelRequested();
        });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_TRUE(executor.isCancelRequested());
}

TEST_F(WorkflowCancellationTest, ReportsNestedPhaseOutsideParentStageDuration)
{
    WorkflowExecutor executor;
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    executor.addStage(std::make_unique<PhaseTimingStage>());

    const auto result = executor.execute(makeMinimalManifest(tempDir.path()));

    ASSERT_EQ(result.stageResults.size(), 2u);
    EXPECT_EQ(result.stageResults[0].stageName, QStringLiteral("LightsStacking"));
    EXPECT_EQ(result.stageResults[1].stageName, QStringLiteral("Final Master WCS"));
    EXPECT_EQ(result.stageResults[1].durationMs, 5);
    EXPECT_EQ(result.stageResults[1].groupsProcessed, 2);
    EXPECT_GE(result.stageResults[0].durationMs, 10);
}
