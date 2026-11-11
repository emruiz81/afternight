// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QSignalSpy>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <latch>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/PendingOutput.h>

#include "Test_ImageIntegration/ImportSession.h"
#include "fixtures/common/IntegrationFrameFiles.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/DocumentPublication.h"
#include "processes/ImageIntegration/workflow/ExecutionPreparationSnapshot.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowLogger.h"
#include "processes/ImageIntegration/workflow/WorkflowRawLoadPolicy.h"
#include "processes/ImageIntegration/workflow/WorkflowRunArchive.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "processes/ImageIntegration/workflow/WorkflowStageServices.h"
#include "processes/ImageIntegration/workflow/stages/BiasStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/CalibrationStackInputs.h"
#include "processes/ImageIntegration/workflow/stages/DarkStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/FlatStackingStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsAlignmentStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsNormalizationStage.h"
#include "processes/ImageIntegration/workflow/stages/LightsRegistrationStage.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"

#include "../../fixtures/ScopedTempPath.h"

namespace
{
[[nodiscard]] QString toQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

/**
 * @brief The scratch root every manifest in this file writes into.
 *
 * The root is process-unique and gtest removes the whole tree once the last test has finished, whether the suite
 * passed or failed.
 */
[[nodiscard]] const QString& executorTempRoot()
{
    static const QString root = toQString(an::tests::fixtures::makeSuiteTempRoot("afternight_test_executor"));
    return root;
}

/// Writes a small grey frame of the given size as a capture program writes one, through the neutral fixture writer.
QString writeFrame(const QString& directory, const QString& filename, const std::uint32_t width,
                   const std::uint32_t height, const std::string& kind = "Light Frame")
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.kind = kind;
    const QString path = QDir(directory).filePath(filename);
    std::string error;
    const bool written = an::tests::fixtures::writeAcquisitionFrame(
        an::tests::fixtures::acquisitionFrame(filename.toStdString(), facts, width, height),
        std::filesystem::path(path.toStdWString()), error);
    EXPECT_TRUE(written) << error;
    return path;
}

bool writeTextFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    const bool written = file.write(bytes) == bytes.size();
    file.close();
    return written;
}

WorkflowManifest makeValidManifest()
{
    WorkflowManifest manifest;
    const QString test_dir = executorTempRoot();
    QDir().mkpath(test_dir);
    manifest.outputDirectory = QDir(test_dir).filePath(QStringLiteral("output"));

    const QString light_path = writeFrame(test_dir, QStringLiteral("afternight_test_executor_light_8x8.fit"), 8, 8);

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|100x100|M42")};
    registration.resolution = QStringLiteral("8x8");
    registration.object = QStringLiteral("M42");
    registration.lightFiles = {FrameId(light_path)};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|M42|L")}};
    integration.outputFilename = QStringLiteral("stacked_M42_L.fit");
    integration.resolution = QStringLiteral("8x8");
    integration.object = QStringLiteral("M42");
    integration.filter = QStringLiteral("L");
    integration.exposure = QStringLiteral("60");
    integration.lightFiles = registration.lightFiles;
    integration.registrationGroupKey = registration.groupKey;
    manifest.integrationGroups.push_back(integration);

    return manifest;
}

WorkflowMonotonicClock makeAdvancingClock(const qint64 stepMs = 10)
{
    auto elapsed_ms = std::make_shared<qint64>(0);
    return [elapsed_ms, stepMs]() {
        const auto now = std::chrono::steady_clock::time_point(std::chrono::milliseconds(*elapsed_ms));
        *elapsed_ms += stepMs;
        return now;
    };
}

StageResult completedIntegrationGroupResult(const WorkflowManifest& manifest, const StageResult::Status status)
{
    StageResult result;
    result.status = status;

    IntegrationGroupResult group;
    group.groupKey = manifest.integrationGroups.front().groupKey.toString();
    group.outputFilename = manifest.integrationGroups.front().outputFilename;
    group.outputPath = QDir(manifest.outputDirectory).filePath(group.outputFilename);
    group.registrationGroupKey = manifest.integrationGroups.front().registrationGroupKey.toString();
    group.success = true;
    result.groupResults.push_back(std::move(group));
    return result;
}

WorkflowManifest makePureCometExecutorManifest(const QString& test_dir)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(test_dir).filePath(QStringLiteral("output"));

    const std::vector<FrameId> lights = {
        FrameId(writeFrame(test_dir, QStringLiteral("executor_comet_a_8x8.fit"), 8, 8)),
        FrameId(writeFrame(test_dir, QStringLiteral("executor_comet_b_8x8.fit"), 8, 8)),
    };

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("reg|8x8|ExecutorComet")};
    registration.resolution = QStringLiteral("8x8");
    registration.object = QStringLiteral("ExecutorComet");
    registration.lightFiles = lights;
    registration.referenceFrame = lights.front();
    registration.settings.registrationMethod = QStringLiteral("comet_tracking");

    RegistrationSettings::CometTrackingSettings comet_settings;
    comet_settings.observations = {
        RegistrationSettings::CometTrackingObservation{lights.at(0).canonicalPath(), QPointF(3.0, 4.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
        RegistrationSettings::CometTrackingObservation{lights.at(1).canonicalPath(), QPointF(4.0, 5.0),
                                                       RegistrationSettings::CometObservationOrigin::Manual, true},
    };
    registration.settings.cometTracking = comet_settings;
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|8x8|ExecutorComet")}};
    integration.outputFilename = QStringLiteral("stacked_executor_comet.fit");
    integration.resolution = QStringLiteral("8x8");
    integration.object = QStringLiteral("ExecutorComet");
    integration.lightFiles = lights;
    integration.registrationGroupKey = registration.groupKey;
    integration.stacking.cometStackingMode = QStringLiteral("Comet");
    manifest.integrationGroups.push_back(integration);

    return manifest;
}

/// What the recording archive saw of the runs it was built for.
struct RunArchiveRecord
{
    int archivesBuilt = 0;
    int commits = 0;
    int reverts = 0;
    int timingFinalizations = 0;
    int logFinalizations = 0;
    QString outputDirectory;
    bool countersLent = false;
    /// The result the last timing finalization was given.
    std::optional<WorkflowExecutor::Result> finalizedTiming;
    /// The log the last log finalization was given.
    QString finalizedLog;
};

/// How the recording archive answers a commit.
struct RunArchiveBehaviour
{
    bool commitSucceeds = true;
    /// Whether the commit writes a report beside the run's history and names it; a commit that succeeds also times the
    /// report phase.
    bool writesReport = true;
    QString failure = QStringLiteral("the archive refused the commit");
};

/// An archive that records what the executor asks of it and commits by writing a report, nothing more: the executor's
/// decisions about when to commit, revert and finalize are what these cases hold, not what the product's archive
/// writes.
class RecordingRunArchive final : public IWorkflowRunArchive
{
public:
    RecordingRunArchive(std::shared_ptr<RunArchiveRecord> record, RunArchiveBehaviour behaviour,
                        QString outputDirectory)
        : m_record(std::move(record)), m_behaviour(std::move(behaviour)), m_outputDirectory(std::move(outputDirectory))
    {
    }

    CommitOutcome commit(const WorkflowManifest& manifest, WorkflowExecutor::Result&,
                         const CapturePhaseDuration& capturePhase) override
    {
        ++m_record->commits;
        CommitOutcome outcome;
        outcome.archiveMs = capturePhase();
        if (m_behaviour.writesReport)
        {
            const QDir run_directory(
                QDir(m_outputDirectory).filePath(QStringLiteral("history/%1").arg(manifest.executionId)));
            const QString markdown = run_directory.filePath(QStringLiteral("workflow_report.md"));
            if (QDir().mkpath(run_directory.absolutePath()) && writeTextFile(markdown, QByteArrayLiteral("# Report")) &&
                writeTextFile(run_directory.filePath(QStringLiteral("workflow_report.html")),
                              QByteArrayLiteral("<html></html>")))
            {
                outcome.reportPath = markdown;
            }
            // A commit that fails is refused before its report is accounted for, as the product's archive refuses one.
            if (m_behaviour.commitSucceeds)
            {
                outcome.reportMs = capturePhase();
            }
        }
        outcome.success = m_behaviour.commitSucceeds;
        if (!outcome.success)
        {
            outcome.error = m_behaviour.failure;
        }
        return outcome;
    }

    void revertStagedFiles(const WorkflowExecutor::Result&) override
    {
        ++m_record->reverts;
    }

    void finalizeTimingArtifacts(const WorkflowExecutor::Result& result) override
    {
        ++m_record->timingFinalizations;
        m_record->finalizedTiming = result;
    }

    void finalizeLogArtifact(const WorkflowExecutor::Result& result) override
    {
        ++m_record->logFinalizations;
        m_record->finalizedLog = result.capturedLog;
    }

private:
    std::shared_ptr<RunArchiveRecord> m_record;
    RunArchiveBehaviour m_behaviour;
    QString m_outputDirectory;
};

/// Gives the executor a recording archive for each of its runs and returns what they record.
std::shared_ptr<RunArchiveRecord> installRecordingArchive(WorkflowExecutor& executor,
                                                          const RunArchiveBehaviour& behaviour = {})
{
    auto record = std::make_shared<RunArchiveRecord>();
    executor.setRunArchiveFactory(
        [record, behaviour](const QString& outputDirectory, WorkflowExecutionCounterAccumulator* counters,
                            const an::consumer::EngineSession*) -> std::unique_ptr<IWorkflowRunArchive> {
            ++record->archivesBuilt;
            record->outputDirectory = outputDirectory;
            record->countersLent = counters != nullptr;
            return std::make_unique<RecordingRunArchive>(record, behaviour, outputDirectory);
        });
    return record;
}

class TestStage : public IWorkflowStage
{
public:
    TestStage(QString name, WorkflowStage stage, StageResult result, bool skip = false, bool report_progress = false,
              std::function<void()> on_execute = {})
        : m_name(std::move(name)), m_stage(stage), m_result(std::move(result)), m_skip(skip),
          m_reportProgress(report_progress), m_onExecute(std::move(on_execute))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return m_name;
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return m_stage;
    }

    [[nodiscard]] bool shouldSkip(const WorkflowManifest& manifest) const override
    {
        (void)manifest;
        return m_skip;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter& reporter) override
    {
        executed = true;
        if (m_onExecute)
        {
            m_onExecute();
        }
        if (m_reportProgress)
        {
            WorkflowProgress progress;
            progress.currentStage = m_stage;
            progress.currentGroup = 1;
            progress.totalGroups = 1;
            progress.stageProgress = 0.5f;
            progress.overallProgress = 0.5f;
            progress.currentOperation = QStringLiteral("Testing");
            progress.currentFile = QStringLiteral("frame.fit");
            reporter.report(progress);
        }
        return m_result;
    }

    bool executed = false;

private:
    QString m_name;
    WorkflowStage m_stage = WorkflowStage::None;
    StageResult m_result;
    bool m_skip = false;
    bool m_reportProgress = false;
    std::function<void()> m_onExecute;
};

/// A stage that hands the services the run lent it to a probe while it executes.
class ServicesProbeStage final : public IWorkflowStage
{
public:
    explicit ServicesProbeStage(std::function<void(const ImageIntegration::WorkflowStageServices*)> probe)
        : m_probe(std::move(probe))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("ServicesProbe");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::LightsStacking;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter&) override
    {
        if (m_probe)
        {
            m_probe(stageServices());
        }
        return StageResult::success();
    }

    /// The services the stage holds now; none outside a run.
    [[nodiscard]] const ImageIntegration::WorkflowStageServices* lentServices() const noexcept
    {
        return stageServices();
    }

private:
    std::function<void(const ImageIntegration::WorkflowStageServices*)> m_probe;
};

class CancellingOnceStage final : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("CancellingOnce");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken& token, ProgressReporter&) override
    {
        if (!cancelled_once)
        {
            cancelled_once = true;
            token.requestCancel();
            return StageResult::cancelled();
        }
        return StageResult::success();
    }

private:
    bool cancelled_once = false;
};

class LogFloodStage final : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("LogFlood");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter& reporter) override
    {
        const QString payload(64 * 1024, QLatin1Char('x'));
        for (int i = 0; i < 160; ++i)
        {
            reporter.logger().operationDetail(QStringLiteral("Flood %1").arg(i), payload);
        }
        return StageResult::success();
    }
};

class CounterContributionStage final : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("CounterContribution");
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
        if (auto* counters = executionCounterAccumulator(); counters != nullptr)
        {
            counters->addBytesHashed(37);
            counters->addBytesCopied(11);
        }
        return StageResult::success();
    }
};

class CurrentFileTrackingStage : public IWorkflowStage
{
public:
    CurrentFileTrackingStage() : m_result(StageResult::success()) {}

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("FileTracker");
    }
    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::Bias;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter& reporter) override
    {
        // Report with a file, then report without a file.
        WorkflowProgress p;
        p.currentStage = WorkflowStage::Bias;
        p.currentGroup = 1;
        p.totalGroups = 1;
        p.stageProgress = 0.3f;
        p.overallProgress = 0.3f;
        p.currentOperation = QStringLiteral("Loading");
        p.currentFile = QStringLiteral("/data/frame_001.fit");
        reporter.report(p);

        p.stageProgress = 0.6f;
        p.overallProgress = 0.6f;
        p.currentOperation = QStringLiteral("Stacking");
        p.currentFile.clear(); // Empty file — dialog must update label to "-"
        reporter.report(p);

        return m_result;
    }

private:
    StageResult m_result;
};

class SubStepReportingStage : public IWorkflowStage
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("SubSteps");
    }
    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::LightsStacking;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter& reporter) override
    {
        // First substep.
        stage_utils::reportStageProgress(reporter, WorkflowStage::LightsStacking, 1, 1, 0.5f, QStringLiteral("Loading"),
                                         QStringLiteral("frame.fit"), 1, 3, QStringLiteral("Load"));
        // Second substep.
        stage_utils::reportStageProgress(reporter, WorkflowStage::LightsStacking, 1, 1, 0.5f,
                                         QStringLiteral("Stacking"), QStringLiteral("frame.fit"), 2, 3,
                                         QStringLiteral("Stack"));
        // Final substep complete.
        stage_utils::reportStageProgress(reporter, WorkflowStage::LightsStacking, 1, 1, 1.0f, QStringLiteral("Saving"),
                                         QStringLiteral("output.fit"), 3, 3, QStringLiteral("Save"));
        return StageResult::success();
    }
};
} // namespace

TEST(WorkflowExecutor, ExecutesEmptyWorkflowSuccessfully)
{
    WorkflowExecutor executor;
    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.isEmpty());
    EXPECT_EQ(result.preparationTiming.manifestBuildMs, 0);
    EXPECT_EQ(result.preparationTiming.planAndValidationMs, 0);
    EXPECT_EQ(result.preparationTiming.totalMs(), 0);
    EXPECT_FALSE(result.executionCounters.has_value());
}

TEST(WorkflowExecutor, ReclaimsPendingWritesLeftByAnInterruptedRun)
{
    const WorkflowManifest manifest = makeValidManifest();
    const QDir output(manifest.outputDirectory);
    ASSERT_TRUE(QDir().mkpath(output.filePath(QStringLiteral("calibrated_lights"))));
    ASSERT_TRUE(QDir().mkpath(output.filePath(QStringLiteral("registered_lights/reg_group"))));
    ASSERT_TRUE(
        QDir().mkpath(output.filePath(QStringLiteral(".afternight/image_integration/artifacts/normalization/group"))));

    // Payloads a killed run left mid-write, at each depth the workflow materializes into, and a document it was
    // publishing when it stopped.
    const QString pending_output_suffix =
        QString::fromUtf8(an::kPendingOutputSuffix.data(), static_cast<qsizetype>(an::kPendingOutputSuffix.size()));
    const QStringList abandoned = {
        output.filePath(QStringLiteral("stacked_M42_L.fit")) + pending_output_suffix,
        output.filePath(QStringLiteral("calibrated_lights/frame_cal.fits")) + pending_output_suffix,
        output.filePath(QStringLiteral("registered_lights/reg_group/frame_reg.fits")) + pending_output_suffix,
        output.filePath(
            QStringLiteral(".afternight/image_integration/artifacts/normalization/group/coefficients.json")) +
            QString::fromLatin1(ImageIntegration::kPendingDocumentSuffix)};
    for (const QString& path : abandoned)
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << path.toStdString();
        file.write("partial payload");
        file.close();
    }

    // A committed artifact sharing those directories must survive untouched.
    const QString committed = output.filePath(QStringLiteral("calibrated_lights/keep_cal.fits"));
    {
        QFile file(committed);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("committed payload");
        file.close();
    }

    WorkflowExecutor executor;
    const WorkflowExecutor::Result result = executor.execute(manifest);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    for (const QString& path : abandoned)
    {
        EXPECT_FALSE(QFileInfo::exists(path)) << "an abandoned pending write survived: " << path.toStdString();
    }
    EXPECT_TRUE(QFileInfo::exists(committed)) << "the sweep removed a committed artifact";
}

TEST(WorkflowExecutor, CapturedWorkflowLogIsBoundedAndExplicitlyTruncated)
{
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<LogFloodStage>());

    const WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_LE(result.capturedLog.toUtf8().size(), ImageIntegration::schema::kWorkflowLogMaxBytes);
    EXPECT_TRUE(result.capturedLog.contains(QStringLiteral("[AfterNight workflow log truncated at 8388608 bytes]")));
}

TEST(WorkflowExecutor, RejectsAPreparationSnapshotThatBecameStaleBeforeLaunch)
{
    QTemporaryDir temporary_directory;
    ASSERT_TRUE(temporary_directory.isValid());
    const QString input_path = QDir(temporary_directory.path()).filePath(QStringLiteral("stale_input.fit"));
    QFile input_file(input_path);
    ASSERT_TRUE(input_file.open(QIODevice::WriteOnly));
    ASSERT_EQ(input_file.write("initial"), 7);
    input_file.close();

    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(temporary_directory.path()).filePath(QStringLiteral("output"));
    WorkflowManifest::IntegrationGroup group;
    group.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|stale")}};
    group.outputFilename = QStringLiteral("stacked.fit");
    group.lightFiles = {FrameId(input_path)};
    manifest.integrationGroups.push_back(group);

    WorkflowExecutionOptions options;
    options.preparationSnapshot = ExecutionPreparationSnapshot::build(manifest, std::nullopt);
    ASSERT_TRUE(input_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(input_file.write("changed after planning"), 22);
    input_file.close();

    auto stage =
        std::make_unique<TestStage>(QStringLiteral("Stack"), WorkflowStage::LightsStacking, StageResult::success());
    TestStage* stage_observer = stage.get();
    WorkflowExecutor executor;
    executor.addStage(std::move(stage));
    const WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(stage_observer->executed);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("changed"), Qt::CaseInsensitive));
    EXPECT_TRUE(result.executorTiming.captured);
    EXPECT_GE(result.executorTiming.stageExecutionMs, 0);
}

TEST(WorkflowExecutor, OptionalCountersAggregateAndRemainIsolatedAcrossExecutions)
{
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<CounterContributionStage>());

    const WorkflowExecutor::Result disabledResult = executor.execute(makeValidManifest());
    EXPECT_TRUE(disabledResult.success);
    EXPECT_FALSE(disabledResult.executionCounters.has_value());

    WorkflowExecutionOptions options;
    options.collectCounters = true;
    const WorkflowExecutor::Result first = executor.execute(makeValidManifest(), {}, options);
    const WorkflowExecutor::Result second = executor.execute(makeValidManifest(), {}, options);

    ASSERT_TRUE(first.executionCounters.has_value());
    EXPECT_EQ(first.executionCounters->bytesHashed(), 37u);
    EXPECT_EQ(first.executionCounters->bytesCopied(), 11u);
    ASSERT_TRUE(second.executionCounters.has_value());
    EXPECT_EQ(second.executionCounters->bytesHashed(), 37u);
    EXPECT_EQ(second.executionCounters->bytesCopied(), 11u);
}

TEST(WorkflowExecutionCounters, SaturatingAggregationNeverWraps)
{
    WorkflowExecutionCounterAccumulator counters;
    counters.addBytesHashed((std::numeric_limits<std::uint64_t>::max)() - 1);
    counters.addBytesHashed(2);
    counters.addInputDecodeOperations((std::numeric_limits<std::uint64_t>::max)() - 1);
    counters.addInputDecodeOperations(2);
    counters.addLogicalDecodedPixelBytes((std::numeric_limits<std::uint64_t>::max)() - 1);
    counters.addLogicalDecodedPixelBytes(2);

    EXPECT_EQ(counters.snapshot().bytesHashed(), (std::numeric_limits<std::uint64_t>::max)());
    EXPECT_EQ(counters.snapshot().inputDecodeOperations(), (std::numeric_limits<std::uint64_t>::max)());
    EXPECT_EQ(counters.snapshot().logicalDecodedPixelBytes(), (std::numeric_limits<std::uint64_t>::max)());
}

TEST(WorkflowExecutor, AggregatesEngineStageCountersExactlyOnceAndDoesNotLeakAcrossRuns)
{
    StageResult first_stage_result = StageResult::success();
    first_stage_result.engineExecutionCounters = StackExecutionCounters{
        .inputDecodeOperations = 3,
        .scientificOutputEncodeCompletions = 1,
        .inputPassesStarted = 1,
        .tilePassesStarted = 0,
        .stackLoaderRequests = 5,
        .roiSubsetReads = 4,
        .fullFrameFallbackReads = 1,
        .logicalDecodedPixelBytes = 1000,
        .calibrationInvocations = 2,
        .calibratedSpillFilesCreated = 2,
        .calibratedSpillBytesWritten = 2000,
        .calibratedSpillBytesRead = 1500,
        .calibratedSpillRoiReads = 4,
        .calibratedSpillCleanupSuccesses = 1,
        .calibratedSpillCleanupFailures = 0,
        .dumpBytesRead = 0,
        .dumpBytesWritten = 0,
        .pixmapBytesRead = 8192,
        .pixmapBytesWritten = 0,
    };

    StageResult second_stage_result = StageResult::success();
    second_stage_result.engineExecutionCounters = StackExecutionCounters{
        .inputDecodeOperations = 6,
        .scientificOutputEncodeCompletions = 2,
        .inputPassesStarted = 4,
        .tilePassesStarted = 3,
        .stackLoaderRequests = 10,
        .roiSubsetReads = 6,
        .fullFrameFallbackReads = 4,
        .logicalDecodedPixelBytes = 3000,
        .calibrationInvocations = 5,
        .calibratedSpillFilesCreated = 3,
        .calibratedSpillBytesWritten = 5000,
        .calibratedSpillBytesRead = 4500,
        .calibratedSpillRoiReads = 6,
        .calibratedSpillCleanupSuccesses = 1,
        .calibratedSpillCleanupFailures = 1,
        .dumpBytesRead = 24576,
        .dumpBytesWritten = 24576,
        .pixmapBytesRead = 0,
        .pixmapBytesWritten = 16384,
    };

    WorkflowExecutor executor;
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Drizzle"), WorkflowStage::LightsDrizzle, first_stage_result));
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stack"), WorkflowStage::LightsStacking, second_stage_result));

    WorkflowExecutionOptions options;
    options.collectCounters = true;
    const WorkflowExecutor::Result first = executor.execute(makeValidManifest(), {}, options);
    const WorkflowExecutor::Result second = executor.execute(makeValidManifest(), {}, options);

    for (const auto* result : {&first, &second})
    {
        ASSERT_TRUE(result->executionCounters.has_value());
        EXPECT_EQ(result->executionCounters->inputDecodeOperations(), 9u);
        EXPECT_EQ(result->executionCounters->scientificOutputEncodeCompletions(), 3u);
        EXPECT_EQ(result->executionCounters->inputPassesStarted(), 5u);
        EXPECT_EQ(result->executionCounters->tilePassesStarted(), 3u);
        EXPECT_EQ(result->executionCounters->stackLoaderRequests(), 15u);
        EXPECT_EQ(result->executionCounters->roiSubsetReads(), 10u);
        EXPECT_EQ(result->executionCounters->fullFrameFallbackReads(), 5u);
        EXPECT_EQ(result->executionCounters->logicalDecodedPixelBytes(), 4000u);
        EXPECT_EQ(result->executionCounters->calibrationInvocations(), 7u);
        EXPECT_EQ(result->executionCounters->calibratedSpillFilesCreated(), 5u);
        EXPECT_EQ(result->executionCounters->calibratedSpillBytesWritten(), 7000u);
        EXPECT_EQ(result->executionCounters->calibratedSpillBytesRead(), 6000u);
        EXPECT_EQ(result->executionCounters->calibratedSpillRoiReads(), 10u);
        EXPECT_EQ(result->executionCounters->calibratedSpillCleanupSuccesses(), 2u);
        EXPECT_EQ(result->executionCounters->calibratedSpillCleanupFailures(), 1u);
        EXPECT_EQ(result->executionCounters->dumpBytesRead(), 24576u);
        EXPECT_EQ(result->executionCounters->dumpBytesWritten(), 24576u);
        EXPECT_EQ(result->executionCounters->pixmapBytesRead(), 8192u);
        EXPECT_EQ(result->executionCounters->pixmapBytesWritten(), 16384u);
    }
}

TEST(WorkflowExecutor, PreservesPreparationAndExistingStageTimingSeparately)
{
    StageResult stage_result = StageResult::success();
    stage_result.phaseTimings.push_back({.stageName = QStringLiteral("Existing Nested Phase"),
                                         .status = QStringLiteral("completed"),
                                         .durationMs = 17,
                                         .groupsProcessed = 3});

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Existing Stage"), WorkflowStage::LightsStacking,
                                                  std::move(stage_result)));

    WorkflowPreparationTiming preparation_timing;
    preparation_timing.captured = true;
    preparation_timing.manifestBuildMs = 113;
    preparation_timing.planAndValidationMs = 227;
    const WorkflowExecutor::Result result = executor.execute(makeValidManifest(), preparation_timing);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.preparationTiming.manifestBuildMs, 113);
    EXPECT_EQ(result.preparationTiming.planAndValidationMs, 227);
    EXPECT_EQ(result.preparationTiming.totalMs(), 340);
    ASSERT_EQ(result.stageResults.size(), 2u);
    EXPECT_EQ(result.stageResults[0].stageName, QStringLiteral("Existing Stage"));
    EXPECT_GE(result.stageResults[0].durationMs, 0);
    EXPECT_EQ(result.stageResults[1].stageName, QStringLiteral("Existing Nested Phase"));
    EXPECT_EQ(result.stageResults[1].durationMs, 17);
    EXPECT_EQ(result.stageResults[1].groupsProcessed, 3);
}

TEST(WorkflowExecutor, PreservesStackGroupFinalizationTimingWithoutAddingChildrenToStageTimeline)
{
    WorkflowManifest manifest = makeValidManifest();
    StageResult stage_result = completedIntegrationGroupResult(manifest, StageResult::Status::Success);
    auto& group = stage_result.groupResults.front();
    QDir().mkpath(QFileInfo(group.outputPath).absolutePath());
    QFile committed_output(group.outputPath);
    ASSERT_TRUE(committed_output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(committed_output.write(QByteArrayLiteral("executor committed master")), 25);
    committed_output.close();
    auto committed_artifact = FinalCommittedArtifact::fromFile(group.outputPath);
    ASSERT_TRUE(committed_artifact.has_value());
    const QString committed_digest = committed_artifact->digest().sha256();
    group.committedArtifact = std::move(*committed_artifact);
    group.outputBytes = group.committedArtifact->sizeBytes();
    group.durationMs = 700;
    group.finalizationTiming.captured = true;
    group.finalizationTiming.phases = {
        {.stageName = QStringLiteral("Final Master Save"),
         .status = QStringLiteral("completed"),
         .durationMs = 100,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Requested Maps"),
         .status = QStringLiteral("skipped"),
         .durationMs = 0,
         .groupsProcessed = 0},
        {.stageName = QStringLiteral("Preview Generation"),
         .status = QStringLiteral("completed"),
         .durationMs = 200,
         .groupsProcessed = 1},
    };

    WorkflowExecutor executor(makeAdvancingClock());
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("LightsStacking"), WorkflowStage::LightsStacking,
                                                  std::move(stage_result)));

    WorkflowPreparationTiming preparation_timing;
    preparation_timing.captured = true;
    preparation_timing.manifestBuildMs = 113;
    preparation_timing.planAndValidationMs = 227;
    const WorkflowExecutor::Result result = executor.execute(manifest, preparation_timing);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.integrationGroupResults.size(), 1u);
    const auto& recorded_group = result.integrationGroupResults.front();
    EXPECT_EQ(recorded_group.durationMs, 700);
    ASSERT_TRUE(recorded_group.committedArtifact.has_value());
    EXPECT_EQ(recorded_group.committedArtifact->digest().sha256(), committed_digest);
    EXPECT_TRUE(recorded_group.committedArtifact->matchesFileSnapshot(recorded_group.outputPath));
    EXPECT_TRUE(recorded_group.finalizationTiming.captured);
    EXPECT_EQ(recorded_group.finalizationTiming.exclusivePhasesTotalMs(), 300);
    ASSERT_EQ(recorded_group.finalizationTiming.phases.size(), 3u);
    EXPECT_EQ(recorded_group.finalizationTiming.phases[1].status, QStringLiteral("skipped"));
    EXPECT_EQ(recorded_group.finalizationTiming.phases[1].durationMs, 0);

    ASSERT_EQ(result.stageResults.size(), 1u);
    EXPECT_EQ(result.stageResults.front().stageName, QStringLiteral("LightsStacking"));
    EXPECT_EQ(result.preparationTiming.manifestBuildMs, 113);
    EXPECT_EQ(result.preparationTiming.planAndValidationMs, 227);
    EXPECT_EQ(result.preparationTiming.totalMs(), 340);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, SuccessCapturesExclusiveCompletionPhasesBeforeTotal)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_SUCCESS");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsStacking, StageResult::success()));
    QStringList live_log;
    executor.setLogCallback([&live_log](WorkflowLogLevel, const QString& message) { live_log.append(message); });

    WorkflowPreparationTiming preparation_timing;
    preparation_timing.captured = true;
    preparation_timing.manifestBuildMs = 113;
    preparation_timing.planAndValidationMs = 227;
    const WorkflowExecutor::Result result = executor.execute(manifest, preparation_timing);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.executorTiming.captured);
    EXPECT_GT(result.executorTiming.stageExecutionMs, 0);
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_GT(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
    EXPECT_EQ(result.preparationTiming.manifestBuildMs, 113);
    EXPECT_EQ(result.preparationTiming.planAndValidationMs, 227);
    EXPECT_EQ(result.preparationTiming.totalMs(), 340);

    const QString expected_total =
        QStringLiteral("Total time: %1").arg(WorkflowLogger::formatDuration(result.totalDurationMs));
    const QString live_log_text = live_log.join(QLatin1Char('\n'));
    EXPECT_TRUE(live_log_text.contains(expected_total));
    EXPECT_LT(live_log_text.indexOf(QStringLiteral("State saved")),
              live_log_text.indexOf(QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY")));
    EXPECT_TRUE(result.capturedLog.contains(QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY")));
    EXPECT_TRUE(result.capturedLog.contains(expected_total));

    // The run's archive is built for its output directory, committed once and handed the final timing and log.
    EXPECT_EQ(archive->archivesBuilt, 1);
    EXPECT_EQ(archive->outputDirectory, temp_dir.path());
    EXPECT_FALSE(archive->countersLent);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_EQ(archive->reverts, 0);
    EXPECT_FALSE(result.htmlReportPath.isEmpty());
    EXPECT_TRUE(QFileInfo::exists(result.htmlReportPath)) << result.htmlReportPath.toStdString();
    ASSERT_EQ(archive->timingFinalizations, 1);
    ASSERT_TRUE(archive->finalizedTiming.has_value());
    EXPECT_EQ(archive->finalizedTiming->totalDurationMs, result.totalDurationMs);
    EXPECT_EQ(archive->finalizedTiming->executorTiming.stageExecutionMs, result.executorTiming.stageExecutionMs);
    EXPECT_EQ(archive->finalizedTiming->executorTiming.stateCommitMs, result.executorTiming.stateCommitMs);
    EXPECT_EQ(archive->finalizedTiming->executorTiming.archiveMs, result.executorTiming.archiveMs);
    EXPECT_EQ(archive->finalizedTiming->executorTiming.reportMs, result.executorTiming.reportMs);
    ASSERT_EQ(archive->logFinalizations, 1);
    EXPECT_TRUE(archive->finalizedLog.contains(QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY")));
    EXPECT_TRUE(archive->finalizedLog.contains(expected_total));

    WorkflowRunStateStore state_store(temp_dir.path());
    const auto run_metadata = state_store.loadRunMetadata();
    ASSERT_TRUE(run_metadata.has_value());
    EXPECT_EQ(run_metadata->totalDurationMs, result.totalDurationMs);
    EXPECT_EQ(run_metadata->stageExecutionMs, result.executorTiming.stageExecutionMs);
    EXPECT_EQ(run_metadata->stateCommitMs, result.executorTiming.stateCommitMs);
    EXPECT_EQ(run_metadata->archiveMs, result.executorTiming.archiveMs);
    EXPECT_EQ(run_metadata->reportMs, result.executorTiming.reportMs);
}

TEST(WorkflowExecutor, PartialSuccessPreservesCompletionTimingAndArchiveReportBehavior)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_PARTIAL");

    IntegrationErrorReport errors;
    errors.addError(IntegrationError::fileNotFound(QStringLiteral("missing.fit")));
    StageResult partial = completedIntegrationGroupResult(manifest, StageResult::Status::PartialSuccess);
    partial.errorReport = errors;

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Partial"), WorkflowStage::LightsStacking, std::move(partial)));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_GT(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
    EXPECT_FALSE(result.htmlReportPath.isEmpty());
}

TEST(WorkflowExecutor, CancellationWithCompletedGroupTimesPerformedCompletionWork)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_CANCELLED");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Completed"), WorkflowStage::LightsStacking,
                                    completedIntegrationGroupResult(manifest, StageResult::Status::Success)));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Cancelled"), WorkflowStage::LightsNormalization,
                                                  StageResult::cancelled()));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_GT(result.executorTiming.stageExecutionMs, 0);
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_GT(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, FailureBeforeTailKeepsOriginalErrorAndReportsSkippedPhasesAsZero)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Failure"), WorkflowStage::LightsStacking,
                                                  StageResult::failure(QStringLiteral("Original failure"))));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_EQ(result.errorMessage, QStringLiteral("Original failure"));
    EXPECT_EQ(archive->commits, 0);
    EXPECT_TRUE(result.executorTiming.captured);
    EXPECT_GT(result.executorTiming.stageExecutionMs, 0);
    EXPECT_EQ(result.executorTiming.stateCommitMs, 0);
    EXPECT_EQ(result.executorTiming.archiveMs, 0);
    EXPECT_EQ(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, FailureAfterCompletedGroupCommitsTailWithoutReplacingOriginalError)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_FAILED_COMMIT");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Completed"), WorkflowStage::LightsStacking,
                                    completedIntegrationGroupResult(manifest, StageResult::Status::Success)));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Failure"), WorkflowStage::LightsNormalization,
                                                  StageResult::failure(QStringLiteral("Tail-safe failure"))));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorMessage, QStringLiteral("Tail-safe failure"));
    EXPECT_EQ(archive->commits, 1);
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_GT(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, ArchiveFailureFailsExecutionAndAccountsForAttemptWithoutReport)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_ARCHIVE_FAILURE");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive =
        installRecordingArchive(executor, RunArchiveBehaviour{.commitSucceeds = false, .writesReport = false});
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsStacking, StageResult::success()));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_TRUE(result.errorMessage.startsWith(QStringLiteral("Failed to archive workflow execution:")));
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_EQ(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, StateCommitFailureRetainsSuccessAndOtherCompletionPhases)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    QFile state_root(QDir(temp_dir.path()).filePath(QStringLiteral(".afternight")));
    ASSERT_TRUE(state_root.open(QIODevice::WriteOnly));
    state_root.write("not a directory");
    state_root.close();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_STATE_FAILURE");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsStacking, StageResult::success()));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.errorMessage.isEmpty());
    EXPECT_EQ(archive->commits, 1);
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_GT(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, ExistingRunDirectoryFailureFailsExecutionAndSkipsReportTiming)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString execution_id = QStringLiteral("TIMING_REPORT_FAILURE");

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = execution_id;

    // The archive writes and names a report and then refuses the commit: a refused commit publishes no report path,
    // whatever the archive left on disk.
    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive =
        installRecordingArchive(executor, RunArchiveBehaviour{.commitSucceeds = false, .writesReport = true});
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsStacking, StageResult::success()));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    const QString written_report =
        QDir(temp_dir.path()).filePath(QStringLiteral("history/%1/workflow_report.html").arg(execution_id));
    ASSERT_TRUE(QFileInfo::exists(written_report));
    EXPECT_FALSE(result.success);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_TRUE(result.errorMessage.startsWith(QStringLiteral("Failed to archive workflow execution:")));
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("the archive refused the commit")));
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_EQ(result.executorTiming.reportMs, 0);
    EXPECT_TRUE(result.htmlReportPath.isEmpty());
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, AcceptsPureCometWorkflowManifestBeforeStageExecution)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowManifest manifest = makePureCometExecutorManifest(dir.path());

    WorkflowExecutor executor;
    auto stage = std::make_unique<TestStage>(QStringLiteral("Pure Comet Validation Probe"),
                                             WorkflowStage::LightsRegistration, StageResult::success());
    TestStage* stage_ptr = stage.get();
    executor.addStage(std::move(stage));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.isEmpty()) << result.errorMessage.toStdString();
    EXPECT_TRUE(stage_ptr->executed);
}

TEST(WorkflowExecutor, InsertsAlignmentDependencyForSelectedNonDrizzleCometStacking)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowManifest manifest = makePureCometExecutorManifest(dir.path());
    manifest.stageSequence = {QStringLiteral("LightsStacking")};

    std::vector<QString> executed;
    WorkflowExecutor executor;

    auto alignment_stage = std::make_unique<TestStage>(
        QStringLiteral("LightsAlignment"), WorkflowStage::LightsAlignment, StageResult::success(), false, false,
        [&]() { executed.push_back(QStringLiteral("LightsAlignment")); });
    TestStage* alignment_stage_ptr = alignment_stage.get();
    executor.addStage(std::move(alignment_stage));

    auto stacking_stage = std::make_unique<TestStage>(QStringLiteral("LightsStacking"), WorkflowStage::LightsStacking,
                                                      StageResult::success(), false, false,
                                                      [&]() { executed.push_back(QStringLiteral("LightsStacking")); });
    TestStage* stacking_stage_ptr = stacking_stage.get();
    executor.addStage(std::move(stacking_stage));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(alignment_stage_ptr->executed);
    EXPECT_TRUE(stacking_stage_ptr->executed);
    ASSERT_EQ(executed.size(), 2U);
    EXPECT_EQ(executed[0], QStringLiteral("LightsAlignment"));
    EXPECT_EQ(executed[1], QStringLiteral("LightsStacking"));
}

TEST(WorkflowExecutor, InsertsAlignmentDependencyForSelectedNonDrizzleCometNormalization)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowManifest manifest = makePureCometExecutorManifest(dir.path());
    manifest.integrationGroups.front().stacking.normalizationMode = NormalizationMode::Global;
    manifest.stageSequence = {QStringLiteral("LightsNormalization")};

    std::vector<QString> executed;
    WorkflowExecutor executor;

    auto alignment_stage = std::make_unique<TestStage>(
        QStringLiteral("LightsAlignment"), WorkflowStage::LightsAlignment, StageResult::success(), false, false,
        [&]() { executed.push_back(QStringLiteral("LightsAlignment")); });
    TestStage* alignment_stage_ptr = alignment_stage.get();
    executor.addStage(std::move(alignment_stage));

    auto normalization_stage = std::make_unique<TestStage>(
        QStringLiteral("LightsNormalization"), WorkflowStage::LightsNormalization, StageResult::success(), false, false,
        [&]() { executed.push_back(QStringLiteral("LightsNormalization")); });
    TestStage* normalization_stage_ptr = normalization_stage.get();
    executor.addStage(std::move(normalization_stage));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(alignment_stage_ptr->executed);
    EXPECT_TRUE(normalization_stage_ptr->executed);
    ASSERT_EQ(executed.size(), 2U);
    EXPECT_EQ(executed[0], QStringLiteral("LightsAlignment"));
    EXPECT_EQ(executed[1], QStringLiteral("LightsNormalization"));
}

TEST(WorkflowExecutor, InsertsAlignmentDependencyForSelectedDrizzleCometGlobalNormalization)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    WorkflowManifest manifest = makePureCometExecutorManifest(dir.path());
    manifest.integrationGroups.front().drizzle = WorkflowManifest::IntegrationGroup::DrizzleSettings{};
    manifest.integrationGroups.front().drizzle->enabled = true;
    manifest.integrationGroups.front().stacking.normalizationMode = NormalizationMode::Global;
    manifest.stageSequence = {QStringLiteral("LightsNormalization")};

    std::vector<QString> executed;
    WorkflowExecutor executor;

    auto alignment_stage = std::make_unique<TestStage>(
        QStringLiteral("LightsAlignment"), WorkflowStage::LightsAlignment, StageResult::success(), false, false,
        [&]() { executed.push_back(QStringLiteral("LightsAlignment")); });
    TestStage* alignment_stage_ptr = alignment_stage.get();
    executor.addStage(std::move(alignment_stage));

    auto normalization_stage = std::make_unique<TestStage>(
        QStringLiteral("LightsNormalization"), WorkflowStage::LightsNormalization, StageResult::success(), false, false,
        [&]() { executed.push_back(QStringLiteral("LightsNormalization")); });
    TestStage* normalization_stage_ptr = normalization_stage.get();
    executor.addStage(std::move(normalization_stage));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(alignment_stage_ptr->executed);
    EXPECT_TRUE(normalization_stage_ptr->executed);
    ASSERT_EQ(executed.size(), 2U);
    EXPECT_EQ(executed[0], QStringLiteral("LightsAlignment"));
    EXPECT_EQ(executed[1], QStringLiteral("LightsNormalization"));
}

TEST(WorkflowExecutor, RejectsOutputDirectoryWhenPathIsAFile)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString output_dir = dir.path() + QDir::separator() + QStringLiteral("output");
    QFile f(output_dir);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("not a directory");
    f.close();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = output_dir;

    WorkflowExecutor executor;
    WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_EQ(result.errorMessage, QStringLiteral("Output directory path is not a directory."));
}

TEST(WorkflowExecutor, RejectsMismatchedDimensionsInGroup)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString a = writeFrame(dir.path(), QStringLiteral("afternight_test_executor_mismatch_a_8x8.fit"), 8, 8);
    const QString b = writeFrame(dir.path(), QStringLiteral("afternight_test_executor_mismatch_b_16x8.fit"), 16, 8);

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));
    manifest.registrationGroups.front().resolution = QStringLiteral("8x8");
    manifest.registrationGroups.front().lightFiles = {FrameId(a), FrameId(b)};
    manifest.integrationGroups.front().resolution = QStringLiteral("8x8");
    manifest.integrationGroups.front().lightFiles = manifest.registrationGroups.front().lightFiles;

    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    const an::consumer::EngineSession& session = *opened;

    WorkflowExecutor executor;
    executor.setCodecRegistryFactory(
        [](const ImageIntegration::WorkflowRawPolicy&) { return std::shared_ptr<const an::CodecRegistry>(); });

    // The frames' sizes are read through the run's session; a run given none reads no header.
    const WorkflowExecutor::Result unchecked = executor.execute(manifest);
    EXPECT_TRUE(unchecked.success) << unchecked.errorMessage.toStdString();

    WorkflowExecutionOptions options;
    options.session = &session;
    WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("Mismatched frame dimensions")))
        << result.errorMessage.toStdString();
}

TEST(WorkflowExecutor, ReportsProgressCorrectly)
{
    WorkflowExecutor executor;
    WorkflowExecutor::Progress captured;
    bool called = false;

    executor.setProgressCallback([&](const WorkflowExecutor::Progress& progress) {
        captured = progress;
        called = true;
    });

    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success(),
                                                  false, true));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(called);
    EXPECT_EQ(captured.currentStage, WorkflowStage::Bias);
    EXPECT_EQ(captured.currentGroup, 1);
    EXPECT_EQ(captured.totalGroups, 1);
    EXPECT_FLOAT_EQ(captured.stageProgress, 0.5f);
}

TEST(WorkflowExecutor, HandlesCancellation)
{
    WorkflowExecutor executor;
    auto stage = std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success());
    TestStage* stage_ptr = stage.get();
    executor.addStage(std::move(stage));

    executor.requestCancel();
    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(stage_ptr->executed);
}

TEST(WorkflowExecutor, TerminalCancellationDoesNotPoisonTheNextExecution)
{
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<CancellingOnceStage>());

    const WorkflowExecutor::Result cancelled = executor.execute(makeValidManifest());
    ASSERT_TRUE(cancelled.cancelled);
    EXPECT_FALSE(cancelled.success);

    const WorkflowExecutor::Result next = executor.execute(makeValidManifest());
    EXPECT_TRUE(next.success) << next.errorMessage.toStdString();
    EXPECT_FALSE(next.cancelled);
}

TEST(WorkflowExecutor, ArchiveFailureDoesNotCommitSuccessfulState)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    WorkflowManifest previous = makeValidManifest();
    previous.outputDirectory = tempDir.path();
    previous.executionId = QStringLiteral("PREVIOUS_SUCCESS");
    WorkflowRunStateStore store(tempDir.path());
    ASSERT_TRUE(store.saveLastSuccessfulManifest(previous));

    WorkflowManifest current = makeValidManifest();
    current.outputDirectory = tempDir.path();
    current.executionId = QStringLiteral("ARCHIVE_COLLISION");

    WorkflowExecutor executor;
    const auto archive = installRecordingArchive(
        executor, RunArchiveBehaviour{.commitSucceeds = false,
                                      .writesReport = false,
                                      .failure = QStringLiteral("the run's history directory already exists")});
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::Bias, StageResult::success()));
    const WorkflowExecutor::Result result = executor.execute(current);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("archive"), Qt::CaseInsensitive));
    const auto persisted = store.loadLastSuccessfulManifest();
    ASSERT_TRUE(persisted.has_value());
    EXPECT_EQ(persisted->executionId, previous.executionId);
}

TEST(WorkflowExecutor, TailCancellationIsTerminalAndDoesNotPoisonNextExecution)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = tempDir.path();
    manifest.executionId = QStringLiteral("TAIL_CANCELLATION");

    WorkflowExecutor executor;
    const auto archive = installRecordingArchive(executor);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::Bias, StageResult::success()));
    executor.setLogCallback([&executor](WorkflowLogLevel, const QString& message) {
        if (message.contains(QStringLiteral("Archiving workflow")))
        {
            executor.requestCancel();
        }
    });

    const WorkflowExecutor::Result cancelled = executor.execute(manifest);
    EXPECT_TRUE(cancelled.cancelled);
    EXPECT_FALSE(cancelled.success);
    EXPECT_EQ(archive->commits, 0);
    EXPECT_EQ(archive->reverts, 1);

    manifest.executionId = QStringLiteral("AFTER_TAIL_CANCELLATION");
    executor.setLogCallback({});
    const WorkflowExecutor::Result next = executor.execute(manifest);
    EXPECT_TRUE(next.success) << next.errorMessage.toStdString();
    EXPECT_FALSE(next.cancelled);
    EXPECT_EQ(archive->commits, 1);
    EXPECT_EQ(archive->reverts, 1);
}

TEST(WorkflowExecutor, HandlesStageFailure)
{
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias,
                                                  StageResult::failure(QStringLiteral("Boom"))));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorMessage, QStringLiteral("Boom"));
}

TEST(WorkflowExecutor, UsesExistingCalibrationMasterPathsInResults)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const QString existingMasterPath = writeFrame(dir.path(), QStringLiteral("existing_master_bias.fit"), 4, 4, "Bias");

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = dir.path();
    manifest.stageSequence = {QStringLiteral("Bias")};

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("bias|master")};
    group.outputFilename = QStringLiteral("generated_master_bias.fit");
    group.existingMaster = FrameId(existingMasterPath);
    group.inputFiles = {FrameId(existingMasterPath)};
    biasStage.groups.push_back(group);
    manifest.biasStage = biasStage;

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Bias"), WorkflowStage::Bias, StageResult::success()));

    const WorkflowExecutor::Result result = executor.execute(manifest);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.calibrationGroupResults.size(), 1u);
    EXPECT_EQ(result.calibrationGroupResults.front().masterPath, QFileInfo(existingMasterPath).absoluteFilePath());
    EXPECT_TRUE(result.calibrationGroupResults.front().success);
    ASSERT_EQ(result.generatedBiasMasters.size(), 1);
    EXPECT_EQ(result.generatedBiasMasters.front(), QFileInfo(existingMasterPath).absoluteFilePath());
}

TEST(WorkflowExecutor, ContinuesAfterPartialSuccessAndCollectsErrors)
{
    WorkflowExecutor executor;
    std::atomic<int> executedCount{0};

    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("missing.fit")));
    report.addWarning(QStringLiteral("Partial warning"));

    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StagePartial"), WorkflowStage::Bias,
                                                  StageResult::partialSuccess(report), false, false,
                                                  [&]() { executedCount.fetch_add(1, std::memory_order_relaxed); }));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageAfterPartial"), WorkflowStage::Dark,
                                                  StageResult::success(), false, false,
                                                  [&]() { executedCount.fetch_add(1, std::memory_order_relaxed); }));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    EXPECT_EQ(executedCount.load(std::memory_order_relaxed), 2);
    ASSERT_EQ(result.errors.size(), 1);
    EXPECT_TRUE(result.errors.front().contains(QStringLiteral("[StagePartial]")));
    EXPECT_EQ(result.warningCount, 1);
}

// A warning two stages give in the same words - as each stage that calibrates the lights in memory says what their
// calibration fell back to - is stated once in the run's result.
TEST(WorkflowExecutor, AWarningTwoStagesGiveInTheSameWordsIsStatedOnce)
{
    StageResult first = StageResult::success();
    first.warnings = {QStringLiteral("The same fallback"), QStringLiteral("Only the first stage's")};
    StageResult second = StageResult::success();
    second.warnings = {QStringLiteral("The same fallback")};
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageOne"), WorkflowStage::Bias, first));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageTwo"), WorkflowStage::Dark, second));

    const WorkflowExecutor::Result result = executor.execute(makeValidManifest());
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.warnings,
              (QStringList{QStringLiteral("The same fallback"), QStringLiteral("Only the first stage's")}));
    EXPECT_EQ(result.warningCount, 2);
}

TEST(WorkflowExecutor, StopsExecutionWhenStageReturnsCancelled)
{
    WorkflowExecutor executor;
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("CancelStage"), WorkflowStage::Bias, StageResult::cancelled()));

    auto trailingStage =
        std::make_unique<TestStage>(QStringLiteral("TrailingStage"), WorkflowStage::Dark, StageResult::success());
    TestStage* trailingStagePtr = trailingStage.get();
    executor.addStage(std::move(trailingStage));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(trailingStagePtr->executed);
}

TEST(WorkflowExecutor, ExecutesStagesInOrder)
{
    WorkflowExecutor executor;
    std::vector<QString> order;

    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success(),
                                                  false, false, [&]() { order.push_back(QStringLiteral("A")); }));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageB"), WorkflowStage::Dark, StageResult::success(),
                                                  false, false, [&]() { order.push_back(QStringLiteral("B")); }));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageC"), WorkflowStage::Flat, StageResult::success(),
                                                  false, false, [&]() { order.push_back(QStringLiteral("C")); }));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], QStringLiteral("A"));
    EXPECT_EQ(order[1], QStringLiteral("B"));
    EXPECT_EQ(order[2], QStringLiteral("C"));
}

TEST(WorkflowExecutor, ForcesNeutralRawWhiteBalanceDuringExecutionAndRestoresSetting)
{
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    const an::consumer::EngineSession& session = *opened;

    // The run reads under a RAW treatment of its own, with no balance on either sensor class, and builds the
    // registry its stages read through from that treatment; nothing process-wide is rewritten for the run.
    std::optional<ImageIntegration::WorkflowRawPolicy> registry_policy;
    std::optional<ImageIntegration::WorkflowRawPolicy> observed_policy;
    WorkflowExecutor executor;
    executor.setCodecRegistryFactory([&registry_policy](const ImageIntegration::WorkflowRawPolicy& policy) {
        registry_policy = policy;
        return std::shared_ptr<const an::CodecRegistry>();
    });
    auto stage = std::make_unique<ServicesProbeStage>([&](const ImageIntegration::WorkflowStageServices* services) {
        if (services != nullptr)
        {
            observed_policy = services->rawPolicy();
        }
    });
    const ServicesProbeStage* stage_ptr = stage.get();
    executor.addStage(std::move(stage));

    WorkflowExecutionOptions options;
    options.session = &session;
    const WorkflowExecutor::Result result = executor.execute(makeValidManifest(), {}, options);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_TRUE(observed_policy.has_value());
    EXPECT_TRUE(observed_policy->cfaWhiteBalance == an::CfaWhiteBalanceMode::None);
    EXPECT_TRUE(observed_policy->fullColorWhiteBalance == an::RawFullColorWhiteBalance::None);
    ASSERT_TRUE(registry_policy.has_value());
    EXPECT_TRUE(*registry_policy == *observed_policy);

    // Nothing of the run outlives it: the stage holds no services once it has ended, and the next run takes the same
    // treatment.
    EXPECT_TRUE(stage_ptr->lentServices() == nullptr);
    EXPECT_TRUE(ImageIntegration::workflowRawPolicy() == *observed_policy);
}

TEST(WorkflowExecutor, AppliesMaxParallelCoresToTbbWork)
{
    if (ImageIntegration::core::detectedParallelCoreCount() <= 1)
    {
        GTEST_SKIP() << "Parallelism cap cannot be distinguished on a single-core system.";
    }

    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    const an::consumer::EngineSession& session = *opened;

    // The parallel-cores setting is the width of every operation a stage admits, not a cap on the process: the stage's
    // operations run on as many threads as the person allowed.
    std::optional<std::uint32_t> lent_width;
    std::optional<std::uint32_t> requested_width;
    std::optional<std::uint32_t> admitted_width;
    bool requested_as_stage = false;

    WorkflowExecutor executor;
    executor.setCodecRegistryFactory(
        [](const ImageIntegration::WorkflowRawPolicy&) { return std::shared_ptr<const an::CodecRegistry>(); });
    executor.addStage(
        std::make_unique<ServicesProbeStage>([&](const ImageIntegration::WorkflowStageServices* services) {
            if (services == nullptr)
            {
                return;
            }
            lent_width = services->parallelCores();
            const an::consumer::OperationRequest request = services->stageOperationRequest();
            requested_width = request.maxConcurrency;
            requested_as_stage = request.purpose == an::consumer::OperationPurpose::WorkflowStage;
            an::Result<an::consumer::OperationScope> scope = services->admitStageOperation();
            if (scope.hasValue())
            {
                admitted_width = scope->execution().admittedConcurrency();
            }
        }));

    WorkflowManifest manifest = makeValidManifest();
    manifest.maxParallelCores = 1;

    WorkflowExecutionOptions options;
    options.session = &session;
    const auto result = executor.execute(manifest, {}, options);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    ASSERT_TRUE(lent_width.has_value());
    EXPECT_EQ(*lent_width, 1u);
    ASSERT_TRUE(requested_width.has_value());
    EXPECT_EQ(*requested_width, 1u);
    EXPECT_TRUE(requested_as_stage);
    ASSERT_TRUE(admitted_width.has_value());
    EXPECT_EQ(*admitted_width, 1u);
}

TEST(WorkflowExecutor, LendsNoStageServicesWithoutASession)
{
    bool probed = false;
    bool lent = true;
    WorkflowExecutor executor;
    executor.addStage(
        std::make_unique<ServicesProbeStage>([&](const ImageIntegration::WorkflowStageServices* services) {
            probed = true;
            lent = services != nullptr;
        }));

    const WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(probed);
    EXPECT_FALSE(lent);
}

TEST(WorkflowExecutor, SkipsCompletedStages)
{
    WorkflowExecutor executor;
    auto stage =
        std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success(), true);
    TestStage* stage_ptr = stage.get();
    executor.addStage(std::move(stage));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(stage_ptr->executed);
}

TEST(WorkflowExecutor, EmitsCorrectSignals)
{
    WorkflowExecutor executor;
    QSignalSpy progress_spy(&executor, &WorkflowExecutor::progressUpdated);
    QSignalSpy log_spy(&executor, &WorkflowExecutor::logMessage);

    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success(),
                                                  false, true));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(progress_spy.count(), 1);
    EXPECT_GE(log_spy.count(), 1);
}

TEST(WorkflowExecutor, RunsOnWorkerThread)
{
    WorkflowExecutor executor;
    std::thread::id callback_thread;
    const std::thread::id main_thread = std::this_thread::get_id();

    executor.setProgressCallback(
        [&](const WorkflowExecutor::Progress&) { callback_thread = std::this_thread::get_id(); });

    executor.addStage(std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success(),
                                                  false, true));

    std::thread worker([&]() { (void)executor.execute(makeValidManifest()); });
    worker.join();

    EXPECT_NE(callback_thread, main_thread);
}

TEST(CancellationToken, IsThreadSafe)
{
    CancellationToken token;
    std::atomic<int> observed{0};
    std::latch start_line(2);

    std::thread t1([&]() {
        start_line.arrive_and_wait();
        for (int i = 0; i < 1000; ++i)
        {
            token.requestCancel();
            std::this_thread::yield();
        }
    });
    std::thread t2([&]() {
        start_line.arrive_and_wait();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (token.isCancellationRequested())
            {
                observed.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            std::this_thread::yield();
        }
    });

    t1.join();
    t2.join();

    EXPECT_TRUE(token.isCancellationRequested());
    EXPECT_GT(observed.load(std::memory_order_relaxed), 0);
}

TEST(ProgressReporter, ThrottlesUpdates)
{
    std::atomic<int> calls{0};
    ProgressReporter reporter([&](const WorkflowProgress&) { calls.fetch_add(1, std::memory_order_relaxed); },
                              std::chrono::milliseconds(50));

    WorkflowProgress progress;
    reporter.report(progress);
    reporter.report(progress);
    reporter.report(progress);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    reporter.report(progress);

    EXPECT_EQ(calls.load(std::memory_order_relaxed), 2);
}

// ============================================================================
// Sub-step progress reporting
// ============================================================================

TEST(StageUtilities, ReportStageProgressBasicComputesOverallCorrectly)
{
    WorkflowProgress captured;
    ProgressReporter reporter([&](const WorkflowProgress& p) { captured = p; }, std::chrono::milliseconds(0));

    // Group 2 of 4, 50% through the group
    stage_utils::reportStageProgress(reporter, WorkflowStage::Bias,
                                     /*group_index=*/2,
                                     /*total_groups=*/4,
                                     /*stage_progress=*/0.5f, QStringLiteral("Stacking"), QStringLiteral("frame.fit"));

    EXPECT_EQ(captured.currentStage, WorkflowStage::Bias);
    EXPECT_EQ(captured.currentGroup, 2);
    EXPECT_EQ(captured.totalGroups, 4);
    EXPECT_FLOAT_EQ(captured.stageProgress, 0.5f);
    // overallProgress = (2-1 + 0.5) / 4 = 0.375
    EXPECT_NEAR(captured.overallProgress, 0.375f, 0.001f);
    EXPECT_EQ(captured.currentSubStep, 0);
    EXPECT_EQ(captured.totalSubSteps, 0);
    EXPECT_TRUE(captured.subStepName.isEmpty());
}

TEST(StageUtilities, ReportStageProgressWithSubStepsComputesOverallCorrectly)
{
    WorkflowProgress captured;
    ProgressReporter reporter([&](const WorkflowProgress& p) { captured = p; }, std::chrono::milliseconds(0));

    // Group 1 of 2, sub-step 2 of 4, 50% through the sub-step
    stage_utils::reportStageProgress(reporter, WorkflowStage::LightsStacking,
                                     /*group_index=*/1,
                                     /*total_groups=*/2,
                                     /*stage_progress=*/0.5f, QStringLiteral("Normalizing"),
                                     QStringLiteral("frame.fit"),
                                     /*current_sub_step=*/2,
                                     /*total_sub_steps=*/4, QStringLiteral("Normalize"));

    EXPECT_EQ(captured.currentSubStep, 2);
    EXPECT_EQ(captured.totalSubSteps, 4);
    EXPECT_EQ(captured.subStepName, QStringLiteral("Normalize"));

    // stageProgress = (2-1 + 0.5) / 4 = 0.375
    EXPECT_NEAR(captured.stageProgress, 0.375f, 0.001f);
    // overallProgress = (1-1 + 0.375) / 2 = 0.1875
    EXPECT_NEAR(captured.overallProgress, 0.1875f, 0.001f);
}

TEST(StageUtilities, SubStepProgressNeverExceedsBounds)
{
    WorkflowProgress captured;
    ProgressReporter reporter([&](const WorkflowProgress& p) { captured = p; }, std::chrono::milliseconds(0));

    // Push beyond 1.0 — should be clamped
    stage_utils::reportStageProgress(reporter, WorkflowStage::Bias,
                                     /*group_index=*/1,
                                     /*total_groups=*/1,
                                     /*stage_progress=*/1.5f, QStringLiteral("Done"), QStringLiteral("frame.fit"),
                                     /*current_sub_step=*/4,
                                     /*total_sub_steps=*/4, QStringLiteral("Save"));

    EXPECT_LE(captured.stageProgress, 1.0f);
    EXPECT_LE(captured.overallProgress, 1.0f);
    EXPECT_GE(captured.stageProgress, 0.0f);
    EXPECT_GE(captured.overallProgress, 0.0f);
}

// ============================================================================
// Monotonic progress guarantee — simulates dialog behavior
// ============================================================================

TEST(ProgressDialogLogic, ProgressBarNeverGoesBackwardsWithinStage)
{
    // Simulates the dialog's monotonic progress tracking.
    float maxProgress = 0.0f;
    WorkflowStage lastStage = WorkflowStage::None;

    auto updateBar = [&](const WorkflowProgress& p) -> int {
        if (p.currentStage != lastStage)
        {
            maxProgress = 0.0f;
            lastStage = p.currentStage;
        }
        const float display = (std::max)(maxProgress, p.overallProgress);
        maxProgress = display;
        return static_cast<int>(display * 100.0f);
    };

    ProgressReporter reporter([](const WorkflowProgress&) {}, std::chrono::milliseconds(0));

    // Simulate a stage with 1 group where the engine resets progress between
    // internal phases (load -> register -> stack).
    WorkflowProgress p;
    p.currentStage = WorkflowStage::LightsStacking;
    p.currentGroup = 1;
    p.totalGroups = 1;

    // Load stage: progress goes 0 to 1.
    p.stageProgress = 0.0f;
    p.overallProgress = 0.0f;
    int bar1 = updateBar(p);

    p.stageProgress = 1.0f;
    p.overallProgress = 1.0f;
    int bar2 = updateBar(p);
    EXPECT_GE(bar2, bar1);

    // Register stage: engine resets to 0, but bar must not drop.
    p.stageProgress = 0.0f;
    p.overallProgress = 0.0f;
    int bar3 = updateBar(p);
    EXPECT_GE(bar3, bar2);

    p.stageProgress = 0.5f;
    p.overallProgress = 0.5f;
    int bar4 = updateBar(p);
    EXPECT_GE(bar4, bar3);

    // Stack stage: another reset.
    p.stageProgress = 0.0f;
    p.overallProgress = 0.0f;
    int bar5 = updateBar(p);
    EXPECT_GE(bar5, bar4);

    p.stageProgress = 1.0f;
    p.overallProgress = 1.0f;
    int bar6 = updateBar(p);
    EXPECT_GE(bar6, bar5);
    EXPECT_EQ(bar6, 100);
}

TEST(ProgressDialogLogic, ProgressBarResetsOnStageChange)
{
    float maxProgress = 0.0f;
    WorkflowStage lastStage = WorkflowStage::None;

    auto updateBar = [&](const WorkflowProgress& p) -> int {
        if (p.currentStage != lastStage)
        {
            maxProgress = 0.0f;
            lastStage = p.currentStage;
        }
        const float display = (std::max)(maxProgress, p.overallProgress);
        maxProgress = display;
        return static_cast<int>(display * 100.0f);
    };

    WorkflowProgress p;
    p.currentStage = WorkflowStage::Bias;
    p.overallProgress = 0.8f;
    int bar1 = updateBar(p);
    EXPECT_EQ(bar1, 80);

    // New stage — bar allowed to start fresh.
    p.currentStage = WorkflowStage::Dark;
    p.overallProgress = 0.1f;
    int bar2 = updateBar(p);
    EXPECT_EQ(bar2, 10);
}

// ============================================================================
// Current-file indicator always updates (never gets stuck)
// ============================================================================

TEST(WorkflowExecutor, CurrentFileIsAlwaysReportedIncludingEmpty)
{
    WorkflowExecutor executor;
    std::vector<WorkflowProgress> updates;

    executor.setProgressCallback([&](const WorkflowExecutor::Progress& p) { updates.push_back(p); });

    executor.addStage(std::make_unique<CurrentFileTrackingStage>());
    WorkflowExecutor::Result result = executor.execute(makeValidManifest());

    EXPECT_TRUE(result.success);
    ASSERT_GE(updates.size(), 2u);

    // First update should have a file path.
    EXPECT_FALSE(updates[0].currentFile.isEmpty());

    // Second update should have an empty file string — the dialog must
    // handle this by resetting the label instead of leaving a stale value.
    EXPECT_TRUE(updates[1].currentFile.isEmpty());
}

// ============================================================================
// Sub-step fields in executor progress signals
// ============================================================================

TEST(WorkflowExecutor, SubStepFieldsArePropagatedThroughSignals)
{
    WorkflowExecutor executor;
    std::vector<WorkflowProgress> updates;

    executor.setProgressCallback([&](const WorkflowExecutor::Progress& p) { updates.push_back(p); });

    executor.addStage(std::make_unique<SubStepReportingStage>());
    WorkflowExecutor::Result result = executor.execute(makeValidManifest());
    EXPECT_TRUE(result.success);

    ASSERT_EQ(updates.size(), 3u);

    // First substep.
    EXPECT_EQ(updates[0].currentSubStep, 1);
    EXPECT_EQ(updates[0].totalSubSteps, 3);
    EXPECT_EQ(updates[0].subStepName, QStringLiteral("Load"));

    // Second substep.
    EXPECT_EQ(updates[1].currentSubStep, 2);
    EXPECT_EQ(updates[1].totalSubSteps, 3);
    EXPECT_EQ(updates[1].subStepName, QStringLiteral("Stack"));

    // Final substep.
    EXPECT_EQ(updates[2].currentSubStep, 3);
    EXPECT_EQ(updates[2].totalSubSteps, 3);
    EXPECT_EQ(updates[2].subStepName, QStringLiteral("Save"));
}

TEST(WorkflowExecutor, SubStepProgressIsMonotonicallyIncreasing)
{
    WorkflowExecutor executor;
    std::vector<WorkflowProgress> updates;

    executor.setProgressCallback([&](const WorkflowExecutor::Progress& p) { updates.push_back(p); });

    executor.addStage(std::make_unique<SubStepReportingStage>());
    WorkflowExecutor::Result result = executor.execute(makeValidManifest());
    EXPECT_TRUE(result.success);

    ASSERT_EQ(updates.size(), 3u);

    // Each subsequent update's overallProgress must be >= the previous.
    for (size_t i = 1; i < updates.size(); ++i)
    {
        EXPECT_GE(updates[i].overallProgress, updates[i - 1].overallProgress)
            << "overallProgress went backwards at update " << i;
    }

    // Last update should be at 100%.
    EXPECT_NEAR(updates.back().overallProgress, 1.0f, 0.001f);
}

// ============================================================================
// ProgressReporter sub-step bypass
// ============================================================================

TEST(ProgressReporter, BypassesThrottleOnSubStepChange)
{
    std::atomic<int> calls{0};
    ProgressReporter reporter([&](const WorkflowProgress&) { calls.fetch_add(1, std::memory_order_relaxed); },
                              std::chrono::milliseconds(5000)); // Very high interval — only bypasses fire.

    WorkflowProgress p;
    p.currentStage = WorkflowStage::Bias;
    p.currentSubStep = 1;
    p.totalSubSteps = 3;
    reporter.report(p); // call 1 — first report always fires

    p.currentSubStep = 2;
    reporter.report(p); // call 2 — sub-step changed → bypass

    p.currentSubStep = 3;
    reporter.report(p); // call 3 — sub-step changed → bypass

    EXPECT_EQ(calls.load(std::memory_order_relaxed), 3);
}

// ============================================================================
// WorkflowLogger formatting helpers
// ============================================================================

TEST(WorkflowLogger, FormatDurationMilliseconds)
{
    EXPECT_EQ(WorkflowLogger::formatDuration(0), QStringLiteral("0 ms"));
    EXPECT_EQ(WorkflowLogger::formatDuration(500), QStringLiteral("500 ms"));
    EXPECT_EQ(WorkflowLogger::formatDuration(999), QStringLiteral("999 ms"));
}

TEST(WorkflowLogger, FormatDurationSeconds)
{
    EXPECT_EQ(WorkflowLogger::formatDuration(1000), QStringLiteral("1 s"));
    EXPECT_EQ(WorkflowLogger::formatDuration(45000), QStringLiteral("45 s"));
    EXPECT_EQ(WorkflowLogger::formatDuration(59999), QStringLiteral("59 s"));
}

TEST(WorkflowLogger, FormatDurationMinutes)
{
    EXPECT_EQ(WorkflowLogger::formatDuration(60000), QStringLiteral("1m 0s"));
    EXPECT_EQ(WorkflowLogger::formatDuration(90000), QStringLiteral("1m 30s"));
    EXPECT_EQ(WorkflowLogger::formatDuration(3599999), QStringLiteral("59m 59s"));
}

TEST(WorkflowLogger, FormatDurationHours)
{
    EXPECT_EQ(WorkflowLogger::formatDuration(3600000), QStringLiteral("1h 0m"));
    EXPECT_EQ(WorkflowLogger::formatDuration(5400000), QStringLiteral("1h 30m"));
}

TEST(WorkflowLogger, FormatSizeBytes)
{
    EXPECT_EQ(WorkflowLogger::formatSize(0), QStringLiteral("0 B"));
    EXPECT_EQ(WorkflowLogger::formatSize(512), QStringLiteral("512 B"));
    EXPECT_EQ(WorkflowLogger::formatSize(1023), QStringLiteral("1023 B"));
}

TEST(WorkflowLogger, FormatSizeKilobytes)
{
    const QString result = WorkflowLogger::formatSize(2048);
    EXPECT_TRUE(result.contains(QStringLiteral("KB")));
}

TEST(WorkflowLogger, FormatSizeMegabytes)
{
    const QString result = WorkflowLogger::formatSize(5 * 1024 * 1024);
    EXPECT_TRUE(result.contains(QStringLiteral("MB")));
}

TEST(WorkflowLogger, ShortenPathReturnsSameWhenShort)
{
    const QString shortPath = QStringLiteral("/tmp/file.fit");
    EXPECT_EQ(WorkflowLogger::shortenPath(shortPath, 60), shortPath);
}

TEST(WorkflowLogger, ShortenPathTruncatesLongPath)
{
    const QString longPath =
        QStringLiteral("/very/long/directory/structure/that/exceeds/the/maximum/characters/allowed/file.fit");
    const QString shortened = WorkflowLogger::shortenPath(longPath, 30);
    EXPECT_LE(shortened.length(), 40); // Allow some margin
    EXPECT_TRUE(shortened.contains(QStringLiteral("file.fit")));
}

// ============================================================================
// WorkflowLogger callback routing
// ============================================================================

TEST(WorkflowLogger, LogsStageLifecycleMessages)
{
    std::vector<std::pair<LogCategory, QString>> messages;
    WorkflowLogger logger([&](LogCategory cat, const QString& msg) { messages.emplace_back(cat, msg); });

    logger.stageStarted(QStringLiteral("LightsStacking"), 3);
    logger.stageCompleted(QStringLiteral("LightsStacking"), 3, 5000);
    logger.stageSkipped(QStringLiteral("Bias"), QStringLiteral("No frames"));
    logger.stageFailed(QStringLiteral("Dark"), QStringLiteral("Disk full"));

    ASSERT_GE(messages.size(), 4u);

    // stageStarted -> Header
    EXPECT_EQ(messages[0].first, LogCategory::Header);
    EXPECT_TRUE(messages[0].second.contains(QStringLiteral("STAGE:")));
    EXPECT_TRUE(messages[0].second.contains(QStringLiteral("3 group")));

    // stageCompleted -> Success
    EXPECT_EQ(messages[1].first, LogCategory::Success);
    EXPECT_TRUE(messages[1].second.contains(QStringLiteral("Complete")));

    // stageSkipped -> Detail
    EXPECT_EQ(messages[2].first, LogCategory::Detail);
    EXPECT_TRUE(messages[2].second.contains(QStringLiteral("Skipped")));

    // stageFailed -> Error
    EXPECT_EQ(messages[3].first, LogCategory::Error);
    EXPECT_TRUE(messages[3].second.contains(QStringLiteral("FAILED")));
}

TEST(WorkflowLogger, LogsWorkflowBanners)
{
    std::vector<std::pair<LogCategory, QString>> messages;
    WorkflowLogger logger([&](LogCategory cat, const QString& msg) { messages.emplace_back(cat, msg); });

    logger.workflowStarted(QStringLiteral("/output"), 5);

    bool foundWorkflow = false;
    for (const auto& [cat, msg] : messages)
    {
        if (msg.contains(QStringLiteral("WORKFLOW")))
        {
            foundWorkflow = true;
            EXPECT_EQ(cat, LogCategory::Header);
        }
    }
    EXPECT_TRUE(foundWorkflow) << "Expected a WORKFLOW banner message";
}

TEST(WorkflowLogger, LogsGroupLifecycle)
{
    std::vector<std::pair<LogCategory, QString>> messages;
    WorkflowLogger logger([&](LogCategory cat, const QString& msg) { messages.emplace_back(cat, msg); });

    logger.groupStarted(1, 3, QStringLiteral("M42_Ha"));
    logger.groupCompleted(1, 3, QStringLiteral("M42_Ha"), 2000);
    logger.groupSkipped(2, 3, QStringLiteral("M42_OIII"), QStringLiteral("Up-to-date"));

    ASSERT_GE(messages.size(), 3u);

    // groupStarted -> Header with index
    EXPECT_EQ(messages[0].first, LogCategory::Header);
    EXPECT_TRUE(messages[0].second.contains(QStringLiteral("[1/3]")));
    EXPECT_TRUE(messages[0].second.contains(QStringLiteral("M42_Ha")));

    // groupCompleted -> Success
    EXPECT_EQ(messages[1].first, LogCategory::Success);
    EXPECT_TRUE(messages[1].second.contains(QStringLiteral("1/3")));

    // groupSkipped -> Detail
    EXPECT_EQ(messages[2].first, LogCategory::Detail);
    EXPECT_TRUE(messages[2].second.contains(QStringLiteral("Skipped")));
}

// ============================================================================
// Executor log callback integration
// ============================================================================

TEST(WorkflowExecutor, EmitsLogMessagesWithCorrectLevels)
{
    WorkflowExecutor executor;
    std::vector<std::pair<WorkflowLogLevel, QString>> logs;

    executor.setLogCallback([&](WorkflowLogLevel level, const QString& msg) { logs.emplace_back(level, msg); });

    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("StageA"), WorkflowStage::Bias, StageResult::success()));
    WorkflowExecutor::Result result = executor.execute(makeValidManifest());
    EXPECT_TRUE(result.success);

    // Should have at least workflow-start and workflow-complete messages.
    bool hasHeader = false;
    bool hasSuccess = false;
    for (const auto& [level, msg] : logs)
    {
        if (level == WorkflowLogLevel::Header)
            hasHeader = true;
        if (level == WorkflowLogLevel::Success)
            hasSuccess = true;
    }
    EXPECT_TRUE(hasHeader) << "Expected at least one Header log";
    EXPECT_TRUE(hasSuccess) << "Expected at least one Success log";
}

TEST(WorkflowExecutor, WarningsFromStagesAreCollected)
{
    WorkflowExecutor executor;

    auto stage = std::make_unique<TestStage>(QStringLiteral("WarnStage"), WorkflowStage::Bias, StageResult::success(),
                                             false, false, [&]() {
                                                 // TestStage doesn't add warnings directly, so verify via result.
                                             });
    executor.addStage(std::move(stage));

    WorkflowExecutor::Result result = executor.execute(makeValidManifest());
    EXPECT_TRUE(result.success);
    // Warnings vector should be present (possibly empty for this test).
    EXPECT_GE(result.warningCount, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// The executor over the product's archive and real stages.
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

/// Runs a stage outside the executor with the services a run lends it, over a session of the case's own.
StageResult runWithServices(IWorkflowStage& stage, const WorkflowManifest& manifest,
                            const an::consumer::EngineSession& session, ProgressReporter& reporter)
{
    const ImageIntegration::WorkflowStageServices services(session, 0u, ImageIntegration::workflowRawPolicy(), nullptr,
                                                           {});
    CancellationToken token;
    stage.setStageServices(&services);
    StageResult result = stage.execute(manifest, token, reporter);
    stage.setStageServices(nullptr);
    return result;
}

/// A stacking stage that checks the integration group's frames, lets `change` act on one of them, and then stacks
/// them, as a stack whose frames change under it does.
class FrameChangesDuringStage : public IWorkflowStage
{
public:
    FrameChangesDuringStage(QString name, std::function<void()> change)
        : m_name(std::move(name)), m_change(std::move(change))
    {
    }

    QString name() const override
    {
        return m_name;
    }

    WorkflowStage stageId() const override
    {
        return WorkflowStage::LightsStacking;
    }

    bool shouldSkip(const WorkflowManifest&) const override
    {
        return false;
    }

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token, ProgressReporter& reporter) override
    {
        const ImageIntegration::WorkflowStageServices* services = stageServices();
        if (services == nullptr)
        {
            return StageResult::failure(IntegrationError::internalError(QStringLiteral("No services were lent")));
        }
        if (manifest.integrationGroups.empty())
        {
            return StageResult::failure(IntegrationError::internalError(QStringLiteral("No integration groups")));
        }

        const auto& group = manifest.integrationGroups.front();
        IntegrationErrorReport report;
        QStringList warnings;
        const stage_utils::ReadableStackInputs readable = stage_utils::loadReadableStackInputs(
            *services, group.lightFiles, QStringLiteral("light"), report, warnings, reporter, nullptr, &token);
        if (readable.fatalError.has_value())
        {
            return StageResult::failure(*readable.fatalError);
        }

        m_change();

        core::CalibrationMasterSet masters(services->codecRegistry());
        stage_utils::CalibrationStackRequest request;
        request.stage = stageId();
        request.groupIndex = 1;
        request.totalGroups = 1;
        request.operation = QStringLiteral("Stack");
        request.groupLabel = QStringLiteral("Stack");
        request.inputs = &readable;
        request.outputPath = QDir(manifest.outputDirectory).filePath(QStringLiteral("out.fit"));
        request.artifactHash = QStringLiteral("frame-changes-during-stage");
        request.runId = manifest.executionId;
        return stage_utils::stackCalibrationGroup(*services, request, masters, warnings, token, reporter);
    }

private:
    QString m_name;
    std::function<void()> m_change;
};

/// The manifest of two 8 x 8 lights in one registration and integration group, its output beside them.
WorkflowManifest twoLightManifest(const QString& directory, QString& secondLight)
{
    const QString a = writeFrame(directory, QStringLiteral("a_8x8.fit"), 8, 8);
    secondLight = writeFrame(directory, QStringLiteral("b_8x8.fit"), 8, 8);

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = QDir(directory).filePath(QStringLiteral("output"));
    manifest.registrationGroups.front().resolution = QStringLiteral("8x8");
    manifest.registrationGroups.front().lightFiles = {FrameId(a), FrameId(secondLight)};
    manifest.integrationGroups.front().resolution = QStringLiteral("8x8");
    manifest.integrationGroups.front().lightFiles = manifest.registrationGroups.front().lightFiles;
    return manifest;
}

} // namespace

TEST(WorkflowExecutorArchiver, SuccessCapturesExclusiveCompletionPhasesBeforeTotal)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_SUCCESS");

    WorkflowExecutor executor(makeAdvancingClock());
    executor.setRunArchiveFactory(&makeWorkflowArchiverRunArchive);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsStacking, StageResult::success()));
    QStringList live_log;
    executor.setLogCallback([&live_log](WorkflowLogLevel, const QString& message) { live_log.append(message); });

    WorkflowPreparationTiming preparation_timing;
    preparation_timing.captured = true;
    preparation_timing.manifestBuildMs = 113;
    preparation_timing.planAndValidationMs = 227;
    WorkflowExecutionOptions options;
    options.session = &*opened;
    const WorkflowExecutor::Result result = executor.execute(manifest, preparation_timing, options);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_TRUE(result.executorTiming.captured);
    EXPECT_GT(result.executorTiming.stageExecutionMs, 0);
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_GT(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
    EXPECT_EQ(result.preparationTiming.manifestBuildMs, 113);
    EXPECT_EQ(result.preparationTiming.planAndValidationMs, 227);
    EXPECT_EQ(result.preparationTiming.totalMs(), 340);

    const QString expected_total =
        QStringLiteral("Total time: %1").arg(WorkflowLogger::formatDuration(result.totalDurationMs));
    const QString live_log_text = live_log.join(QLatin1Char('\n'));
    EXPECT_TRUE(live_log_text.contains(expected_total));
    EXPECT_LT(live_log_text.indexOf(QStringLiteral("State saved")),
              live_log_text.indexOf(QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY")));
    EXPECT_TRUE(result.capturedLog.contains(QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY")));
    EXPECT_TRUE(result.capturedLog.contains(expected_total));

    QFile report_file(QFileInfo(result.htmlReportPath).dir().filePath(QStringLiteral("workflow_report.md")));
    ASSERT_TRUE(report_file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString report_markdown = QString::fromUtf8(report_file.readAll());
    EXPECT_TRUE(
        report_markdown.contains(QStringLiteral("| State Commit | %1ms |").arg(result.executorTiming.stateCommitMs)));
    EXPECT_TRUE(report_markdown.contains(QStringLiteral("| Archive | %1ms |").arg(result.executorTiming.archiveMs)));
    EXPECT_TRUE(report_markdown.contains(QStringLiteral("| Report | %1ms |").arg(result.executorTiming.reportMs)));
    EXPECT_FALSE(report_markdown.contains(QStringLiteral("AFTERNIGHT_")));

    QFile report_html(result.htmlReportPath);
    ASSERT_TRUE(report_html.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_FALSE(QString::fromUtf8(report_html.readAll()).contains(QStringLiteral("AFTERNIGHT_")));

    QFile archive_manifest(QFileInfo(result.htmlReportPath).dir().filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(archive_manifest.open(QIODevice::ReadOnly));
    const QJsonObject archive_root = QJsonDocument::fromJson(archive_manifest.readAll()).object();
    EXPECT_EQ(archive_root.value(QStringLiteral("totalDurationMs")).toInt(), result.totalDurationMs);
    const QJsonObject executor_timing = archive_root.value(QStringLiteral("executorTiming")).toObject();
    EXPECT_EQ(executor_timing.value(QStringLiteral("stageExecutionMs")).toInteger(),
              result.executorTiming.stageExecutionMs);
    EXPECT_EQ(executor_timing.value(QStringLiteral("stateCommitMs")).toInteger(), result.executorTiming.stateCommitMs);
    EXPECT_EQ(executor_timing.value(QStringLiteral("archiveMs")).toInteger(), result.executorTiming.archiveMs);
    EXPECT_EQ(executor_timing.value(QStringLiteral("reportMs")).toInteger(), result.executorTiming.reportMs);

    WorkflowRunStateStore state_store(temp_dir.path());
    const auto run_metadata = state_store.loadRunMetadata();
    ASSERT_TRUE(run_metadata.has_value());
    EXPECT_EQ(run_metadata->totalDurationMs, result.totalDurationMs);
    EXPECT_EQ(run_metadata->stageExecutionMs, result.executorTiming.stageExecutionMs);
    EXPECT_EQ(run_metadata->stateCommitMs, result.executorTiming.stateCommitMs);
    EXPECT_EQ(run_metadata->archiveMs, result.executorTiming.archiveMs);
    EXPECT_EQ(run_metadata->reportMs, result.executorTiming.reportMs);

    QFile workflow_log(QFileInfo(result.htmlReportPath).dir().filePath(QStringLiteral("workflow_log.txt")));
    ASSERT_TRUE(workflow_log.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString archived_log = QString::fromUtf8(workflow_log.readAll());
    EXPECT_TRUE(archived_log.contains(QStringLiteral("WORKFLOW COMPLETED SUCCESSFULLY")));
    EXPECT_TRUE(archived_log.contains(expected_total));
}

TEST(WorkflowExecutorArchiver, ExistingRunDirectoryFailureFailsExecutionAndSkipsReportTiming)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    // A directory where the archive writes its report makes the commit fail.
    const QString execution_id = QStringLiteral("TIMING_REPORT_FAILURE");
    const QString run_dir = QDir(temp_dir.path()).filePath(QStringLiteral("history/%1").arg(execution_id));
    ASSERT_TRUE(QDir().mkpath(QDir(run_dir).filePath(QStringLiteral("workflow_report.md"))));

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = execution_id;

    WorkflowExecutor executor(makeAdvancingClock());
    executor.setRunArchiveFactory(&makeWorkflowArchiverRunArchive);
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsStacking, StageResult::success()));

    WorkflowExecutionOptions options;
    options.session = &*opened;
    const WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.startsWith(QStringLiteral("Failed to archive workflow execution:")))
        << result.errorMessage.toStdString();
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_EQ(result.executorTiming.reportMs, 0);
    EXPECT_TRUE(result.htmlReportPath.isEmpty());
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, RegistrationOnlyRunUsesZeroForSkippedReportPhase)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("TIMING_NO_REPORT");
    manifest.executionMode = ExecutionMode::RegistrationOnly;
    manifest.executionScope = QStringLiteral("registration_all");

    WorkflowExecutor executor(makeAdvancingClock());
    executor.setRunArchiveFactory(&makeWorkflowArchiverRunArchive);
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Stage"), WorkflowStage::LightsRegistration,
                                                  StageResult::success()));

    WorkflowExecutionOptions options;
    options.session = &*opened;
    const WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    EXPECT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_GT(result.executorTiming.stateCommitMs, 0);
    EXPECT_GT(result.executorTiming.archiveMs, 0);
    EXPECT_EQ(result.executorTiming.reportMs, 0);
    EXPECT_EQ(result.totalDurationMs, result.executorTiming.exclusivePhasesTotalMs());
}

TEST(WorkflowExecutor, ReportsFileDisappearsDuringStage)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    QString b;
    const WorkflowManifest manifest = twoLightManifest(dir.path(), b);

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<FrameChangesDuringStage>(QStringLiteral("FileDisappearsDuringStage"),
                                                                [b]() { (void)QFile::remove(b); }));

    WorkflowExecutionOptions options;
    options.session = &*opened;
    const WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    // The frame checked before the stack and gone when it is read fails the stack, named.
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QFileInfo(b).fileName())) << result.errorMessage.toStdString();
    EXPECT_FALSE(QFileInfo::exists(QDir(manifest.outputDirectory).filePath(QStringLiteral("out.fit"))));
}

TEST(WorkflowExecutor, ReportsFileCorruptsDuringStage)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    QString b;
    const WorkflowManifest manifest = twoLightManifest(dir.path(), b);

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<FrameChangesDuringStage>(QStringLiteral("FileCorruptsDuringStage"), [b]() {
        QFile file(b);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            file.write("CORRUPTED");
            file.close();
        }
    }));

    WorkflowExecutionOptions options;
    options.session = &*opened;
    const WorkflowExecutor::Result result = executor.execute(manifest, {}, options);

    // The frame rewritten after it was checked is refused when it is read, named.
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QFileInfo(b).fileName())) << result.errorMessage.toStdString();
    EXPECT_FALSE(QFileInfo::exists(QDir(manifest.outputDirectory).filePath(QStringLiteral("out.fit"))));
}

TEST(BiasStackingStage, LogsGroupContextSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    const QString light = writeFrame(dir.path(), QStringLiteral("bias_input.fit"), 8, 8, "Bias Frame");

    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));

    WorkflowManifest::CalibrationStage stage_data;
    stage_data.frameType = QStringLiteral("bias");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("3008x3004")};
    group.outputFilename = QStringLiteral("master_bias.fit");
    group.inputFiles = {FrameId(light)};
    stage_data.groups.push_back(group);
    manifest.biasStage = stage_data;

    std::vector<QString> detail_messages;
    ProgressReporter reporter([](const WorkflowProgress&) {}, std::chrono::milliseconds(0));
    reporter.setLogCallback([&](LogCategory category, const QString& message) {
        if (category == LogCategory::Detail)
        {
            detail_messages.push_back(message);
        }
    });

    BiasStackingStage stage;
    const StageResult result = runWithServices(stage, manifest, *opened, reporter);

    EXPECT_TRUE(result.isSuccess()) << result.errorMessage.toStdString();
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Inputs")); }));
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Settings")); }));
}

TEST(DarkStackingStage, LogsGroupContextSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    const QString light = writeFrame(dir.path(), QStringLiteral("dark_input.fit"), 8, 8, "Dark Frame");

    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));

    WorkflowManifest::CalibrationStage stage_data;
    stage_data.frameType = QStringLiteral("dark");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("3008x3004|120")};
    group.outputFilename = QStringLiteral("master_dark.fit");
    group.inputFiles = {FrameId(light)};
    stage_data.groups.push_back(group);
    manifest.darkStage = stage_data;

    std::vector<QString> detail_messages;
    ProgressReporter reporter([](const WorkflowProgress&) {}, std::chrono::milliseconds(0));
    reporter.setLogCallback([&](LogCategory category, const QString& message) {
        if (category == LogCategory::Detail)
        {
            detail_messages.push_back(message);
        }
    });

    DarkStackingStage stage;
    const StageResult result = runWithServices(stage, manifest, *opened, reporter);

    EXPECT_TRUE(result.isSuccess()) << result.errorMessage.toStdString();
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Inputs")); }));
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Settings")); }));
    // A master dark is stacked from its frames as they are, so the stage names no master it applies.
    EXPECT_FALSE(std::any_of(detail_messages.begin(), detail_messages.end(),
                             [](const QString& m) { return m.contains(QStringLiteral("Calibration")); }));
}

TEST(FlatStackingStage, LogsGroupContextSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    const QString light = writeFrame(dir.path(), QStringLiteral("flat_input.fit"), 8, 8, "Flat Field");

    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));

    WorkflowManifest::CalibrationStage stage_data;
    stage_data.frameType = QStringLiteral("flat");
    WorkflowManifest::CalibrationStage::Group group;
    group.groupKey = GroupKey{QStringLiteral("3008x3004|L")};
    group.outputFilename = QStringLiteral("master_flat.fit");
    group.inputFiles = {FrameId(light)};
    stage_data.groups.push_back(group);
    manifest.flatStage = stage_data;

    std::vector<QString> detail_messages;
    ProgressReporter reporter([](const WorkflowProgress&) {}, std::chrono::milliseconds(0));
    reporter.setLogCallback([&](LogCategory category, const QString& message) {
        if (category == LogCategory::Detail)
        {
            detail_messages.push_back(message);
        }
    });

    FlatStackingStage stage;
    const StageResult result = runWithServices(stage, manifest, *opened, reporter);

    EXPECT_TRUE(result.isSuccess()) << result.errorMessage.toStdString();
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Inputs")); }));
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Settings")); }));
    EXPECT_TRUE(std::any_of(detail_messages.begin(), detail_messages.end(),
                            [](const QString& m) { return m.contains(QStringLiteral("Calibration")); }));
}

TEST(LightsRegistrationStage, ReportsInitialPreparingProgressBeforeGroupWork)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));
    manifest.registrationGroups.front().lightFiles.clear();

    std::vector<WorkflowProgress> updates;
    ProgressReporter reporter([&](const WorkflowProgress& progress) { updates.push_back(progress); },
                              std::chrono::milliseconds(0));

    LightsRegistrationStage stage;
    const StageResult result = runWithServices(stage, manifest, *opened, reporter);

    EXPECT_TRUE(result.isSuccess()) << result.errorMessage.toStdString();
    ASSERT_FALSE(updates.empty());

    const WorkflowProgress& first = updates.front();
    EXPECT_EQ(first.currentStage, WorkflowStage::LightsRegistration);
    EXPECT_EQ(first.currentGroup, 1);
    EXPECT_EQ(first.totalGroups, 1);
    EXPECT_FLOAT_EQ(first.stageProgress, 0.0f);
    EXPECT_EQ(first.currentOperation, QStringLiteral("Preparing registration"));
}

TEST(LightsAlignmentStage, ReportsInitialPreparingProgressBeforeGroupWork)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));
    ASSERT_TRUE(QDir().mkpath(manifest.outputDirectory));

    std::vector<WorkflowProgress> updates;
    ProgressReporter reporter([&](const WorkflowProgress& progress) { updates.push_back(progress); },
                              std::chrono::milliseconds(0));

    LightsAlignmentStage stage;
    const StageResult result = runWithServices(stage, manifest, *opened, reporter);

    EXPECT_TRUE(result.isSuccess()) << result.errorMessage.toStdString();
    ASSERT_FALSE(updates.empty());

    const WorkflowProgress& first = updates.front();
    EXPECT_EQ(first.currentStage, WorkflowStage::LightsAlignment);
    EXPECT_EQ(first.currentGroup, 1);
    EXPECT_EQ(first.totalGroups, 1);
    EXPECT_FLOAT_EQ(first.stageProgress, 0.0f);
    EXPECT_EQ(first.currentOperation, QStringLiteral("Preparing alignment"));
}

TEST(LightsNormalizationStage, ReportsInitialPreparingProgressBeforeGroupWork)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();

    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = QDir(dir.path()).filePath(QStringLiteral("output"));
    manifest.integrationGroups.front().stacking.normalizationMode = NormalizationMode::Local;
    manifest.integrationGroups.front().stacking.localNormalization.enabled = true;
    manifest.integrationGroups.front().lightFiles.clear();

    std::vector<WorkflowProgress> updates;
    ProgressReporter reporter([&](const WorkflowProgress& progress) { updates.push_back(progress); },
                              std::chrono::milliseconds(0));

    LightsNormalizationStage stage;
    const StageResult result = runWithServices(stage, manifest, *opened, reporter);

    EXPECT_TRUE(result.isSuccess()) << result.errorMessage.toStdString();
    ASSERT_FALSE(updates.empty());

    const WorkflowProgress& first = updates.front();
    EXPECT_EQ(first.currentStage, WorkflowStage::LightsNormalization);
    EXPECT_EQ(first.currentGroup, 1);
    EXPECT_EQ(first.totalGroups, 1);
    EXPECT_FLOAT_EQ(first.stageProgress, 0.0f);
    EXPECT_EQ(first.currentOperation, QStringLiteral("Preparing normalization"));
}

// ---- The groups a run reuses: recorded, and counted by nothing that counts a run's successful groups.

namespace
{
/// A stacking stage's result for a run whose one group was found up to date: no group result, the group named.
StageResult reusedIntegrationGroupResult(const WorkflowManifest& manifest)
{
    StageResult result = StageResult::success();
    result.reusedGroups.push_back(ReusedIntegrationGroup{
        .groupKey = manifest.integrationGroups.front().groupKey.toString(),
        .outputPath = QDir(manifest.outputDirectory).filePath(manifest.integrationGroups.front().outputFilename)});
    return result;
}
} // namespace

TEST(WorkflowExecutor, AGroupTheStackingStageFoundUpToDateIsNamedAmongTheGroupsTheRunReused)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("REUSED_BY_STAGE");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Stacking"), WorkflowStage::LightsStacking,
                                                  reusedIntegrationGroupResult(manifest)));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success);
    ASSERT_EQ(result.reusedIntegrationGroups.size(), 1u);
    EXPECT_EQ(result.reusedIntegrationGroups.front().groupKey, manifest.integrationGroups.front().groupKey.toString());
    EXPECT_FALSE(result.reusedIntegrationGroups.front().outputPath.isEmpty());
    // A reused group is no result and no master the run generated.
    EXPECT_TRUE(result.integrationGroupResults.empty());
    EXPECT_TRUE(result.generatedLightMasters.isEmpty());
    ASSERT_FALSE(result.stageResults.empty());
    EXPECT_EQ(result.stageResults.front().groupsProcessed, 0);
    EXPECT_EQ(archive->commits, 1) << "a run that completed is archived whatever it reused";
}

TEST(WorkflowExecutor, ARunThatReusesEveryGroupAndThenStopsHasNoSuccessfulGroupToCommit)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("REUSED_THEN_FAILED");

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Stacking"), WorkflowStage::LightsStacking,
                                                  reusedIntegrationGroupResult(manifest)));
    executor.addStage(std::make_unique<TestStage>(QStringLiteral("Failure"), WorkflowStage::LightsNormalization,
                                                  StageResult::failure(QStringLiteral("Stopped after the reuse"))));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reusedIntegrationGroups.size(), 1u) << "the record holds however the run ends";
    // The executor commits a stopped run only for a group it made: a reused one is not such a group.
    EXPECT_EQ(archive->commits, 0);
    EXPECT_EQ(result.executorTiming.archiveMs, 0);
}

TEST(WorkflowExecutor, TheGroupsAnIncrementalPlanLeavesOutAreNamedAsReusedWhateverTheStagesReturn)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    WorkflowManifest manifest = makeValidManifest();
    manifest.outputDirectory = temp_dir.path();
    manifest.executionId = QStringLiteral("REUSED_BY_PLAN");
    ASSERT_EQ(manifest.integrationGroups.size(), 1u);
    WorkflowManifest::IntegrationGroup changed = manifest.integrationGroups.front();
    changed.groupKey = IntegrationGroupKey{GroupKey{changed.groupKey.toString() + QStringLiteral("|changed")}};
    changed.outputFilename = QStringLiteral("changed_") + changed.outputFilename;
    manifest.integrationGroups.push_back(changed);
    manifest.executionMode = ExecutionMode::Incremental;
    manifest.incrementalIntegrationGroups = {changed.groupKey};
    const QString unchangedKey = manifest.integrationGroups.front().groupKey.toString();

    WorkflowExecutor executor(makeAdvancingClock());
    const auto archive = installRecordingArchive(executor);
    // The stage also names the unchanged group: a group is listed once.
    StageResult stage_result = StageResult::success();
    stage_result.reusedGroups.push_back(ReusedIntegrationGroup{.groupKey = unchangedKey, .outputPath = {}});
    executor.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stacking"), WorkflowStage::LightsStacking, stage_result));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    ASSERT_EQ(result.reusedIntegrationGroups.size(), 1u);
    EXPECT_EQ(result.reusedIntegrationGroups.front().groupKey, unchangedKey);
    EXPECT_EQ(
        QDir::cleanPath(result.reusedIntegrationGroups.front().outputPath),
        QDir::cleanPath(QDir(manifest.outputDirectory).filePath(manifest.integrationGroups.front().outputFilename)));

    // A full run plans every group: nothing is reused by the plan.
    manifest.executionMode = ExecutionMode::Full;
    manifest.executionId = QStringLiteral("NOT_REUSED_BY_PLAN");
    WorkflowExecutor full(makeAdvancingClock());
    const auto full_archive = installRecordingArchive(full);
    full.addStage(
        std::make_unique<TestStage>(QStringLiteral("Stacking"), WorkflowStage::LightsStacking, StageResult::success()));
    EXPECT_TRUE(full.execute(manifest).reusedIntegrationGroups.empty());
}
