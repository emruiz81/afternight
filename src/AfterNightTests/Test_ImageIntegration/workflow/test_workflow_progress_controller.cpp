// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"

namespace
{
WorkflowManifest makeControllerManifest(const QString& outputDir)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = outputDir;

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("controller-registration")};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("controller-integration")}};
    integration.registrationGroupKey = registration.groupKey;
    integration.outputFilename = QStringLiteral("stacked_controller.fit");
    manifest.integrationGroups.push_back(integration);
    return manifest;
}

class ReportingStage : public IWorkflowStage
{
public:
    QString name() const override
    {
        return QStringLiteral("ReportingStage");
    }

    WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter& reporter) override
    {
        WorkflowProgress progress;
        progress.currentStage = WorkflowStage::Bias;
        progress.currentGroup = 1;
        progress.totalGroups = 1;
        progress.stageProgress = 1.0f;
        progress.overallProgress = 1.0f;
        progress.currentOperation = QStringLiteral("Reporting");
        progress.currentFile = QStringLiteral("frame.fit");
        reporter.report(progress);
        return StageResult::success();
    }
};

class CancellableStage : public IWorkflowStage
{
public:
    QString name() const override
    {
        return QStringLiteral("CancellableStage");
    }

    WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken& token, ProgressReporter& reporter) override
    {
        WorkflowProgress progress;
        progress.currentStage = WorkflowStage::Bias;
        progress.currentGroup = 1;
        progress.totalGroups = 1;
        progress.stageProgress = 0.1f;
        progress.overallProgress = 0.1f;
        progress.currentOperation = QStringLiteral("Waiting");
        reporter.report(progress);

        while (!token.isCancellationRequested())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return StageResult::cancelled();
    }
};

/// A stage that is slow to leave. The executor destroys its members in reverse order of declaration, so by the time
/// this destructor has slept its cancellation token and stop source are gone; a cancel that reached the executor on its
/// way out shows in the token's flag, which the destructor reads last.
class SlowToLeaveStage : public IWorkflowStage
{
public:
    explicit SlowToLeaveStage(std::shared_ptr<std::atomic<bool>> cancelReached)
        : m_cancelReached(std::move(cancelReached))
    {
    }

    ~SlowToLeaveStage() override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (m_token != nullptr && m_token->isCancellationRequested())
        {
            m_cancelReached->store(true);
        }
    }

    QString name() const override
    {
        return QStringLiteral("SlowToLeaveStage");
    }

    WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken& token, ProgressReporter&) override
    {
        m_token = &token;
        return StageResult::success();
    }

private:
    std::shared_ptr<std::atomic<bool>> m_cancelReached;
    CancellationToken* m_token = nullptr;
};

class FinalizationReportingStage : public IWorkflowStage
{
public:
    QString name() const override
    {
        return QStringLiteral("FinalizationReportingStage");
    }

    WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest& manifest, CancellationToken&, ProgressReporter& reporter) override
    {
        const auto report = [&](const float value, const QString& operation) {
            WorkflowProgress progress;
            progress.currentStage = WorkflowStage::LightsStacking;
            progress.currentGroup = 1;
            progress.totalGroups = 1;
            progress.stageProgress = value;
            progress.overallProgress = value;
            progress.currentOperation = operation;
            reporter.report(progress);
        };

        report(0.90f, QStringLiteral("Saving final master"));
        report(0.94f, QStringLiteral("Saving requested maps"));
        report(0.98f, QStringLiteral("Recording stacked artifact"));

        StageResult result = StageResult::success();
        IntegrationGroupResult group;
        group.groupKey = QStringLiteral("FinalizationGroup");
        group.outputFilename = QStringLiteral("stacked_FinalizationGroup.fit");
        group.outputPath = QDir(manifest.outputDirectory).filePath(group.outputFilename);
        QFile output(group.outputPath);
        if (!output.open(QIODevice::WriteOnly) || output.write("stack") < 0)
        {
            return StageResult::failure(QStringLiteral("Failed to create fake stacked output"));
        }
        output.close();
        group.success = true;
        group.durationMs = 500;
        group.finalizationTiming.captured = true;
        group.finalizationTiming.phases.push_back({.stageName = QStringLiteral("Digest & Artifact Recording"),
                                                   .status = QStringLiteral("completed"),
                                                   .durationMs = 100,
                                                   .groupsProcessed = 1});
        result.groupResults.push_back(std::move(group));

        report(1.0f, QStringLiteral("Lights stacking"));
        return result;
    }
};
} // namespace

TEST(WorkflowProgressController, InitialState)
{
    WorkflowProgressController controller(nullptr, nullptr);
    EXPECT_FALSE(controller.isRunning());
}

TEST(WorkflowProgressController, ForwardsProgressSignalsAndFinishes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowProgressController controller(nullptr, nullptr, nullptr, []() {
        auto executor = std::make_unique<WorkflowExecutor>();
        executor->addStage(std::make_unique<ReportingStage>());
        return executor;
    });

    QSignalSpy progressSpy(&controller, &WorkflowProgressController::progressUpdated);
    WorkflowExecutor::Result finishedResult;
    bool finished = false;
    QObject::connect(&controller, &WorkflowProgressController::finished,
                     [&finishedResult, &finished](const WorkflowExecutor::Result& result) {
                         finishedResult = result;
                         finished = true;
                     });

    WorkflowPreparationTiming preparation_timing;
    preparation_timing.captured = true;
    preparation_timing.manifestBuildMs = 101;
    preparation_timing.planAndValidationMs = 202;
    controller.start(makeControllerManifest(dir.path()), preparation_timing);

    QTRY_VERIFY_WITH_TIMEOUT(progressSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);

    EXPECT_TRUE(finishedResult.success);
    EXPECT_EQ(finishedResult.preparationTiming.manifestBuildMs, 101);
    EXPECT_EQ(finishedResult.preparationTiming.planAndValidationMs, 202);
    EXPECT_EQ(finishedResult.preparationTiming.totalMs(), 303);
    EXPECT_TRUE(finishedResult.executionCounters.has_value());
    ASSERT_GE(progressSpy.count(), 1);
    const auto args = progressSpy.takeFirst();
    const WorkflowProgress progress = qvariant_cast<WorkflowProgress>(args.at(0));
    EXPECT_EQ(progress.currentOperation, QStringLiteral("Reporting"));
}

TEST(WorkflowProgressController, CancelRequestsExecutorCancellation)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowProgressController controller(nullptr, nullptr, nullptr, []() {
        auto executor = std::make_unique<WorkflowExecutor>();
        executor->addStage(std::make_unique<CancellableStage>());
        return executor;
    });

    WorkflowExecutor::Result finishedResult;
    bool finished = false;
    QObject::connect(&controller, &WorkflowProgressController::finished,
                     [&finishedResult, &finished](const WorkflowExecutor::Result& result) {
                         finishedResult = result;
                         finished = true;
                     });

    controller.start(makeControllerManifest(dir.path()));
    QTRY_VERIFY_WITH_TIMEOUT(controller.isRunning(), 5000);

    controller.cancel();

    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);
    EXPECT_TRUE(finishedResult.cancelled);
    EXPECT_FALSE(finishedResult.success);
}

// A window that closes the moment its run lands destroys its controller while the worker thread is deleting the
// executor the run used. The run is over, so there is nothing to cancel: the controller must not reach an executor that
// is on its way out, where its stop source may be gone already.
TEST(WorkflowProgressController, GoingTheMomentItsRunFinishesCancelsNothingOfTheExecutorBeingDeleted)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto cancelReached = std::make_shared<std::atomic<bool>>(false);
    {
        WorkflowProgressController controller(nullptr, nullptr, nullptr, [cancelReached]() {
            auto executor = std::make_unique<WorkflowExecutor>();
            executor->addStage(std::make_unique<SlowToLeaveStage>(cancelReached));
            return executor;
        });
        bool finished = false;
        QObject::connect(&controller, &WorkflowProgressController::finished,
                         [&finished](const WorkflowExecutor::Result&) { finished = true; });

        controller.start(makeControllerManifest(dir.path()));
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
        // The controller goes now, with its thread still running and its executor still being deleted; it waits for
        // the thread, so the stage's destructor has read the token when this block ends.
    }
    EXPECT_FALSE(cancelReached->load());
}

TEST(WorkflowProgressController, ForwardsMonotonicFinalizationProgressAndCompletesExactlyOnce)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowProgressController controller(nullptr, nullptr, nullptr, []() {
        auto executor = std::make_unique<WorkflowExecutor>();
        executor->addStage(std::make_unique<FinalizationReportingStage>());
        return executor;
    });

    std::vector<WorkflowProgress> progress_updates;
    QObject::connect(&controller, &WorkflowProgressController::progressUpdated,
                     [&progress_updates](const WorkflowProgress& progress) { progress_updates.push_back(progress); });
    WorkflowExecutor::Result finished_result;
    bool finished = false;
    QObject::connect(&controller, &WorkflowProgressController::finished,
                     [&finished_result, &finished](const WorkflowExecutor::Result& result) {
                         finished_result = result;
                         finished = true;
                     });

    controller.start(makeControllerManifest(dir.path()));

    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isRunning(), 5000);
    ASSERT_TRUE(finished_result.success) << finished_result.errorMessage.toStdString();
    ASSERT_EQ(finished_result.integrationGroupResults.size(), 1u);
    EXPECT_TRUE(finished_result.integrationGroupResults.front().finalizationTiming.captured);
    ASSERT_FALSE(progress_updates.empty());

    int completion_count = 0;
    float previous = -1.0f;
    for (const auto& progress : progress_updates)
    {
        EXPECT_GE(progress.stageProgress, previous);
        previous = progress.stageProgress;
        completion_count += progress.stageProgress >= 0.999f ? 1 : 0;
        if (progress.currentOperation != QStringLiteral("Lights stacking"))
        {
            EXPECT_LT(progress.stageProgress, 1.0f);
        }
    }
    EXPECT_EQ(completion_count, 1);
    EXPECT_NEAR(progress_updates.back().stageProgress, 1.0f, 0.001f);
}
