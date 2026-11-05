// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file test_partial_success.cpp
 * @brief Tests for the PartialSuccess / error-resilience workflow infrastructure.
 *
 * Covers:
 *  - StageResult::partialSuccess() factory and query methods
 *  - IntegrationErrorReport aggregation (addError, merge, hasFatalErrors, etc.)
 *  - WorkflowExecutor propagation of partial successes across stages
 *  - Pipeline continuation after per-frame/per-group failures
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowRunStateStore.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include "../../fixtures/ScopedTempPath.h"

// ============================================================================
// StageResult – PartialSuccess factory and query methods
// ============================================================================

TEST(StageResult, SuccessIsNotPartialSuccess)
{
    const StageResult r = StageResult::success();
    EXPECT_TRUE(r.isSuccess());
    EXPECT_FALSE(r.isPartialSuccess());
    EXPECT_FALSE(r.isFailure());
    EXPECT_FALSE(r.isCancelled());
    EXPECT_EQ(r.status, StageResult::Status::Success);
}

TEST(StageResult, FailureIsNotSuccess)
{
    const StageResult r = StageResult::failure(QStringLiteral("Boom"));
    EXPECT_FALSE(r.isSuccess());
    EXPECT_FALSE(r.isPartialSuccess());
    EXPECT_TRUE(r.isFailure());
    EXPECT_FALSE(r.isCancelled());
    EXPECT_EQ(r.status, StageResult::Status::Failure);
    EXPECT_EQ(r.errorMessage, QStringLiteral("Boom"));
}

TEST(StageResult, FailureWithIntegrationError)
{
    const auto err = IntegrationError::fileNotFound(QStringLiteral("/tmp/missing.fit"));
    const StageResult r = StageResult::failure(err);
    EXPECT_TRUE(r.isFailure());
    EXPECT_FALSE(r.isSuccess());
    EXPECT_EQ(r.errorCode(), IntegrationErrorCode::FileNotFound);
    EXPECT_TRUE(r.isRecoverable());
}

TEST(StageResult, CancelledIsNotSuccess)
{
    const StageResult r = StageResult::cancelled();
    EXPECT_FALSE(r.isSuccess());
    EXPECT_FALSE(r.isPartialSuccess());
    EXPECT_FALSE(r.isFailure());
    EXPECT_TRUE(r.isCancelled());
    EXPECT_EQ(r.status, StageResult::Status::Cancelled);
}

TEST(StageResult, SkippedIsNotSuccessOrFailure)
{
    const StageResult r = StageResult::skipped();
    EXPECT_FALSE(r.isSuccess());
    EXPECT_FALSE(r.isPartialSuccess());
    EXPECT_FALSE(r.isFailure());
    EXPECT_FALSE(r.isCancelled());
    EXPECT_EQ(r.status, StageResult::Status::Skipped);
}

TEST(StageResult, PartialSuccessIsAlsoSuccess)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));

    const StageResult r = StageResult::partialSuccess(report);

    EXPECT_TRUE(r.isSuccess()); // isSuccess includes PartialSuccess
    EXPECT_TRUE(r.isPartialSuccess());
    EXPECT_FALSE(r.isFailure());
    EXPECT_FALSE(r.isCancelled());
    EXPECT_EQ(r.status, StageResult::Status::PartialSuccess);
}

TEST(StageResult, PartialSuccessCarriesErrorReport)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    report.addError(IntegrationError::fileNotReadable(QStringLiteral("/tmp/b.fit")));
    report.addWarning(QStringLiteral("Low SNR"));

    const StageResult r = StageResult::partialSuccess(report);

    ASSERT_EQ(r.errorReport.errors.size(), 2u);
    EXPECT_EQ(r.errorReport.errors[0].code(), IntegrationErrorCode::FileNotFound);
    EXPECT_EQ(r.errorReport.errors[1].code(), IntegrationErrorCode::FileNotReadable);
    EXPECT_EQ(r.warnings.size(), 1);
    EXPECT_EQ(r.warnings[0], QStringLiteral("Low SNR"));
}

TEST(StageResult, PartialSuccessWithEmptyReportIsStillPartialSuccess)
{
    IntegrationErrorReport report;
    const StageResult r = StageResult::partialSuccess(report);

    EXPECT_TRUE(r.isPartialSuccess());
    EXPECT_TRUE(r.isSuccess());
    EXPECT_TRUE(r.errorReport.errors.empty());
}

// ============================================================================
// IntegrationErrorReport – aggregation methods
// ============================================================================

TEST(IntegrationErrorReport, EmptyReportHasNoFatalErrors)
{
    IntegrationErrorReport report;
    EXPECT_FALSE(report.hasFatalErrors());
    EXPECT_EQ(report.recoverableErrorCount(), 0u);
    EXPECT_FALSE(report.wasCancelled());
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.warnings.empty());
}

TEST(IntegrationErrorReport, AddErrorIncreasesCount)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    report.addError(IntegrationError::fileNotReadable(QStringLiteral("/tmp/b.fit")));

    ASSERT_EQ(report.errors.size(), 2u);
    EXPECT_EQ(report.errors[0].code(), IntegrationErrorCode::FileNotFound);
    EXPECT_EQ(report.errors[1].code(), IntegrationErrorCode::FileNotReadable);
}

TEST(IntegrationErrorReport, AddWarningIncreasesWarnings)
{
    IntegrationErrorReport report;
    report.addWarning(QStringLiteral("Warning 1"));
    report.addWarning(QStringLiteral("Warning 2"));

    ASSERT_EQ(report.warnings.size(), 2);
    EXPECT_EQ(report.warnings[0], QStringLiteral("Warning 1"));
    EXPECT_EQ(report.warnings[1], QStringLiteral("Warning 2"));
}

TEST(IntegrationErrorReport, RecoverableErrorCountFiltersCorrectly)
{
    IntegrationErrorReport report;
    // Recoverable: FileNotFound, FileNotReadable, InvalidFileFormat
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    report.addError(IntegrationError::fileNotReadable(QStringLiteral("/tmp/b.fit")));
    // Non-recoverable:
    report.addError(IntegrationError::stackingFailed(QStringLiteral("OOM")));

    EXPECT_EQ(report.recoverableErrorCount(), 2u);
}

TEST(IntegrationErrorReport, HasFatalErrorsDetectsNonRecoverableNonCancellation)
{
    IntegrationErrorReport report;
    // Only recoverable errors → no fatal
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    EXPECT_FALSE(report.hasFatalErrors());

    // Add a fatal error
    report.addError(IntegrationError::stackingFailed(QStringLiteral("OOM")));
    EXPECT_TRUE(report.hasFatalErrors());
}

TEST(IntegrationErrorReport, HasFatalErrorsIgnoresCancellation)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::cancelled());
    EXPECT_FALSE(report.hasFatalErrors());
    EXPECT_TRUE(report.wasCancelled());
}

TEST(IntegrationErrorReport, WasCancelledReturnsTrueOnlyCancelled)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    EXPECT_FALSE(report.wasCancelled());

    report.addError(IntegrationError::cancelled());
    EXPECT_TRUE(report.wasCancelled());
}

TEST(IntegrationErrorReport, MergeCombinesErrorsAndWarnings)
{
    IntegrationErrorReport a;
    a.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    a.addWarning(QStringLiteral("warn-a"));

    IntegrationErrorReport b;
    b.addError(IntegrationError::fileNotReadable(QStringLiteral("/tmp/b.fit")));
    b.addError(IntegrationError::stackingFailed(QStringLiteral("reason")));
    b.addWarning(QStringLiteral("warn-b"));

    a.merge(b);

    EXPECT_EQ(a.errors.size(), 3u);
    EXPECT_EQ(a.warnings.size(), 2);
    EXPECT_EQ(a.errors[0].code(), IntegrationErrorCode::FileNotFound);
    EXPECT_EQ(a.errors[1].code(), IntegrationErrorCode::FileNotReadable);
    EXPECT_EQ(a.errors[2].code(), IntegrationErrorCode::StackingFailed);
}

TEST(IntegrationErrorReport, MergeWithEmptyIsNoOp)
{
    IntegrationErrorReport a;
    a.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));

    IntegrationErrorReport empty;
    a.merge(empty);

    EXPECT_EQ(a.errors.size(), 1u);
    EXPECT_TRUE(a.warnings.empty());
}

// ============================================================================
// WorkflowExecutor – PartialSuccess propagation
// ============================================================================

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
 * The executor archives each run into history/<executionId>/, so a fixed shared name accumulates
 * a new directory on every run of the suite. This root is process-unique and gtest removes the
 * whole tree once the last test has finished, whether the suite passed or failed.
 */
[[nodiscard]] const QString& partialSuccessTempRoot()
{
    static const QString root = toQString(an::tests::fixtures::makeSuiteTempRoot("afternight_test_partial_success"));
    return root;
}

/**
 * @brief Minimal IWorkflowStage mock that returns a configurable StageResult.
 */
class PartialTestStage : public IWorkflowStage
{
public:
    PartialTestStage(QString name, WorkflowStage stage, StageResult result, std::function<void()> on_execute = {})
        : m_name(std::move(name)), m_stage(stage), m_result(std::move(result)), m_onExecute(std::move(on_execute))
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

    StageResult execute(const WorkflowManifest&, CancellationToken&, ProgressReporter&) override
    {
        executed = true;
        if (m_onExecute)
        {
            m_onExecute();
        }
        return m_result;
    }

    bool executed = false;

private:
    QString m_name;
    WorkflowStage m_stage;
    StageResult m_result;
    std::function<void()> m_onExecute;
};

WorkflowManifest makeMinimalManifest()
{
    WorkflowManifest manifest;
    const QString test_dir = partialSuccessTempRoot();
    QDir().mkpath(test_dir);
    manifest.outputDirectory = test_dir;

    // Validation requires at least one registrationGroup.
    // Provide a minimal dummy so the executor doesn't reject the manifest.
    WorkflowManifest::RegistrationGroup reg;
    reg.groupKey = GroupKey{QStringLiteral("reg|dummy")};
    reg.resolution = QStringLiteral("64x64");
    reg.object = QStringLiteral("TestObject");
    manifest.registrationGroups.push_back(std::move(reg));

    return manifest;
}

WorkflowManifest makeManifestWithTwoIntegrationGroups(const QString& outputDir)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = outputDir;

    const FrameId light1(QStringLiteral("/tmp/partial_group_1.fit"));
    const FrameId light2(QStringLiteral("/tmp/partial_group_2.fit"));
    const FrameId bias1(QDir(outputDir).filePath(QStringLiteral("master_bias_partial_1.fit")));
    const FrameId bias2(QDir(outputDir).filePath(QStringLiteral("master_bias_partial_2.fit")));

    WorkflowManifest::RegistrationGroup reg1;
    reg1.groupKey = GroupKey{QStringLiteral("reg|partial|1")};
    reg1.resolution = QStringLiteral("64x64");
    reg1.object = QStringLiteral("PartialObject1");
    reg1.lightFiles = {light1};
    manifest.registrationGroups.push_back(reg1);

    WorkflowManifest::RegistrationGroup reg2;
    reg2.groupKey = GroupKey{QStringLiteral("reg|partial|2")};
    reg2.resolution = QStringLiteral("64x64");
    reg2.object = QStringLiteral("PartialObject2");
    reg2.lightFiles = {light2};
    manifest.registrationGroups.push_back(reg2);

    WorkflowManifest::IntegrationGroup group1;
    group1.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|partial|1")}};
    group1.outputFilename = QStringLiteral("partial_group_1.fit");
    group1.resolution = QStringLiteral("64x64");
    group1.object = QStringLiteral("PartialObject1");
    group1.filter = QStringLiteral("L");
    group1.exposure = QStringLiteral("60");
    group1.lightFiles = {light1};
    group1.registrationGroupKey = reg1.groupKey;
    manifest.integrationGroups.push_back(group1);

    WorkflowManifest::IntegrationGroup group2;
    group2.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("int|partial|2")}};
    group2.outputFilename = QStringLiteral("partial_group_2.fit");
    group2.resolution = QStringLiteral("64x64");
    group2.object = QStringLiteral("PartialObject2");
    group2.filter = QStringLiteral("L");
    group2.exposure = QStringLiteral("60");
    group2.lightFiles = {light2};
    group2.registrationGroupKey = reg2.groupKey;
    manifest.integrationGroups.push_back(group2);

    WorkflowManifest::LightsCalibrationParams calibration1;
    calibration1.lightPath = light1;
    calibration1.biasPath = bias1;
    manifest.lightsCalibration.push_back(calibration1);

    WorkflowManifest::LightsCalibrationParams calibration2;
    calibration2.lightPath = light2;
    calibration2.biasPath = bias2;
    manifest.lightsCalibration.push_back(calibration2);

    WorkflowManifest::CalibrationStage biasStage;
    biasStage.frameType = QStringLiteral("BIAS");
    WorkflowManifest::CalibrationStage::Group biasGroup1;
    biasGroup1.groupKey = GroupKey{QStringLiteral("bias|partial|1")};
    biasGroup1.outputFilename = QStringLiteral("master_bias_partial_1.fit");
    biasStage.groups.push_back(biasGroup1);
    WorkflowManifest::CalibrationStage::Group biasGroup2;
    biasGroup2.groupKey = GroupKey{QStringLiteral("bias|partial|2")};
    biasGroup2.outputFilename = QStringLiteral("master_bias_partial_2.fit");
    biasStage.groups.push_back(biasGroup2);
    manifest.biasStage = std::move(biasStage);

    manifest.calibrateLightsInMemory = false;
    manifest.stageSequence = {QStringLiteral("Bias"), QStringLiteral("LightsCalibration"),
                              QStringLiteral("LightsRegistration"), QStringLiteral("LightsNormalization"),
                              QStringLiteral("LightsStacking")};

    return manifest;
}

} // namespace

TEST(WorkflowExecutor, PartialSuccessSetsPartialFlag)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/missing_frame.fit")));

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(
        QStringLiteral("Calibration"), WorkflowStage::LightsCalibration, StageResult::partialSuccess(report)));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.isEmpty());
}

TEST(WorkflowExecutor, PartialSuccessCollectsErrorMessages)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));
    report.addError(IntegrationError::fileNotReadable(QStringLiteral("/tmp/b.fit")));

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(
        QStringLiteral("Registration"), WorkflowStage::LightsRegistration, StageResult::partialSuccess(report)));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    ASSERT_EQ(result.errors.size(), 2);
    // Each error should be prefixed with the stage name
    EXPECT_TRUE(result.errors[0].contains(QStringLiteral("[Registration]")));
    EXPECT_TRUE(result.errors[1].contains(QStringLiteral("[Registration]")));
}

TEST(WorkflowExecutor, PartialSuccessDoesNotStopPipeline)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));

    WorkflowExecutor executor;
    std::vector<QString> order;

    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageA"), WorkflowStage::LightsCalibration,
                                                         StageResult::partialSuccess(report),
                                                         [&]() { order.push_back(QStringLiteral("A")); }));

    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageB"), WorkflowStage::LightsRegistration,
                                                         StageResult::success(),
                                                         [&]() { order.push_back(QStringLiteral("B")); }));

    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageC"), WorkflowStage::LightsStacking,
                                                         StageResult::success(),
                                                         [&]() { order.push_back(QStringLiteral("C")); }));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], QStringLiteral("A"));
    EXPECT_EQ(order[1], QStringLiteral("B"));
    EXPECT_EQ(order[2], QStringLiteral("C"));
}

TEST(WorkflowExecutor, MultiplePartialSuccessesAccumulateErrors)
{
    IntegrationErrorReport report1;
    report1.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));

    IntegrationErrorReport report2;
    report2.addError(IntegrationError::fileNotReadable(QStringLiteral("/tmp/b.fit")));
    report2.addError(IntegrationError::registrationFailed(QStringLiteral("Too few stars")));

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageA"), WorkflowStage::LightsCalibration,
                                                         StageResult::partialSuccess(report1)));
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageB"), WorkflowStage::LightsRegistration,
                                                         StageResult::partialSuccess(report2)));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    // 1 error from stage A + 2 errors from stage B
    ASSERT_EQ(result.errors.size(), 3);
    EXPECT_TRUE(result.errors[0].contains(QStringLiteral("[StageA]")));
    EXPECT_TRUE(result.errors[1].contains(QStringLiteral("[StageB]")));
    EXPECT_TRUE(result.errors[2].contains(QStringLiteral("[StageB]")));
}

TEST(WorkflowExecutor, PartialSuccessFollowedByFailureStopsPipeline)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));

    WorkflowExecutor executor;
    auto stageC = std::make_unique<PartialTestStage>(QStringLiteral("StageC"), WorkflowStage::LightsStacking,
                                                     StageResult::success());
    auto* stageCPtr = stageC.get();

    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageA"), WorkflowStage::LightsCalibration,
                                                         StageResult::partialSuccess(report)));
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageB"), WorkflowStage::LightsRegistration,
                                                         StageResult::failure(QStringLiteral("Fatal error in B"))));
    executor.addStage(std::move(stageC));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_FALSE(result.success); // Hard failure stops everything
    EXPECT_TRUE(result.partialSuccess); // Partial was set by StageA
    EXPECT_FALSE(result.errorMessage.isEmpty()); // Failure message from StageB
    EXPECT_FALSE(stageCPtr->executed); // StageC was never reached
    // Partial errors from StageA should still be collected
    EXPECT_EQ(result.errors.size(), 1);
}

TEST(WorkflowExecutor, PartialSuccessFollowedByCancellationStopsPipeline)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit")));

    WorkflowExecutor executor;
    auto stageC = std::make_unique<PartialTestStage>(QStringLiteral("StageC"), WorkflowStage::LightsStacking,
                                                     StageResult::success());
    auto* stageCPtr = stageC.get();

    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageA"), WorkflowStage::LightsCalibration,
                                                         StageResult::partialSuccess(report)));
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageB"), WorkflowStage::LightsRegistration,
                                                         StageResult::cancelled()));
    executor.addStage(std::move(stageC));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(stageCPtr->executed);
}

TEST(WorkflowExecutor, PureSuccessPipelineHasNoPartialFlag)
{
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageA"), WorkflowStage::LightsCalibration,
                                                         StageResult::success()));
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageB"), WorkflowStage::LightsRegistration,
                                                         StageResult::success()));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.partialSuccess);
    EXPECT_TRUE(result.errors.isEmpty());
}

TEST(WorkflowExecutor, PartialSuccessWithEmptyReportSetsFlag)
{
    IntegrationErrorReport empty;
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("StageA"), WorkflowStage::LightsCalibration,
                                                         StageResult::partialSuccess(empty)));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.partialSuccess);
    EXPECT_TRUE(result.errors.isEmpty());
}

TEST(WorkflowExecutor, PartialSuccessErrorMessageContainsOriginalError)
{
    IntegrationErrorReport report;
    report.addError(IntegrationError::registrationFailed(QStringLiteral("Too few stars detected")));

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(
        QStringLiteral("Registration"), WorkflowStage::LightsRegistration, StageResult::partialSuccess(report)));

    const WorkflowExecutor::Result result = executor.execute(makeMinimalManifest());

    ASSERT_EQ(result.errors.size(), 1);
    // Error message should contain the original user-friendly text
    EXPECT_TRUE(result.errors[0].contains(QStringLiteral("Registration/alignment failed")))
        << result.errors[0].toStdString();
    EXPECT_TRUE(result.errors[0].contains(QStringLiteral("Too few stars detected"))) << result.errors[0].toStdString();
}

TEST(WorkflowExecutor, StageSequenceSkipsUnselectedStages)
{
    WorkflowManifest manifest = makeMinimalManifest();
    manifest.stageSequence = {QStringLiteral("LightsRegistration")};

    WorkflowExecutor executor;
    std::vector<QString> order;

    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("LightsCalibration"),
                                                         WorkflowStage::LightsCalibration, StageResult::success(),
                                                         [&]() { order.push_back(QStringLiteral("Calibration")); }));
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("LightsRegistration"),
                                                         WorkflowStage::LightsRegistration, StageResult::success(),
                                                         [&]() { order.push_back(QStringLiteral("Registration")); }));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_TRUE(result.success);
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order.front(), QStringLiteral("Registration"));
    ASSERT_EQ(result.stageResults.size(), 1u);
    EXPECT_EQ(result.stageResults.front().stageName, QStringLiteral("LightsRegistration"));
}

TEST(WorkflowExecutor, CancelledRunSavesCommittedSubsetManifest)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    WorkflowManifest manifest = makeManifestWithTwoIntegrationGroups(tempDir.path());

    StageResult completedStacking = StageResult::success();
    IntegrationGroupResult groupResult;
    groupResult.groupKey = manifest.integrationGroups.front().groupKey.toString();
    groupResult.outputFilename = manifest.integrationGroups.front().outputFilename;
    groupResult.outputPath = QDir(tempDir.path()).filePath(groupResult.outputFilename);
    groupResult.registrationGroupKey = manifest.integrationGroups.front().registrationGroupKey.toString();
    groupResult.success = true;
    completedStacking.groupResults.push_back(groupResult);

    WorkflowExecutor executor;
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("LightsStacking"),
                                                         WorkflowStage::LightsStacking, completedStacking));
    executor.addStage(std::make_unique<PartialTestStage>(QStringLiteral("Cleanup"), WorkflowStage::LightsNormalization,
                                                         StageResult::cancelled()));

    const WorkflowExecutor::Result result = executor.execute(manifest);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);

    WorkflowRunStateStore store(tempDir.path());
    const auto savedManifest = store.loadLastSuccessfulManifest(manifest.executionScope, manifest.executionScopeKey);

    ASSERT_TRUE(savedManifest.has_value());
    ASSERT_EQ(savedManifest->integrationGroups.size(), 1u);
    EXPECT_EQ(savedManifest->integrationGroups.front().groupKey, manifest.integrationGroups.front().groupKey);
    ASSERT_EQ(savedManifest->registrationGroups.size(), 1u);
    EXPECT_EQ(savedManifest->registrationGroups.front().groupKey, manifest.registrationGroups.front().groupKey);
    ASSERT_EQ(savedManifest->lightsCalibration.size(), 1u);
    EXPECT_EQ(savedManifest->lightsCalibration.front().lightPath, manifest.lightsCalibration.front().lightPath);
    ASSERT_TRUE(savedManifest->biasStage.has_value());
    ASSERT_EQ(savedManifest->biasStage->groups.size(), 1u);
    EXPECT_EQ(savedManifest->biasStage->groups.front().groupKey, manifest.biasStage->groups.front().groupKey);
}

// ============================================================================
// IntegrationError – factory methods and query methods used in new code
// ============================================================================

TEST(IntegrationError, SuccessHasNoneCode)
{
    const auto e = IntegrationError::success();
    EXPECT_TRUE(e.isSuccess());
    EXPECT_TRUE(e.isRecoverable());
    EXPECT_FALSE(e.isCancellation());
    EXPECT_EQ(e.code(), IntegrationErrorCode::None);
}

TEST(IntegrationError, FileNotFoundIsRecoverable)
{
    const auto e = IntegrationError::fileNotFound(QStringLiteral("/tmp/a.fit"));
    EXPECT_FALSE(e.isSuccess());
    EXPECT_TRUE(e.isRecoverable());
    EXPECT_FALSE(e.isCancellation());
    EXPECT_EQ(e.code(), IntegrationErrorCode::FileNotFound);
    EXPECT_EQ(e.filePath(), QStringLiteral("/tmp/a.fit"));
}

TEST(IntegrationError, FileNotReadableIsRecoverable)
{
    const auto e = IntegrationError::fileNotReadable(QStringLiteral("/tmp/a.fit"), QStringLiteral("permission denied"));
    EXPECT_TRUE(e.isRecoverable());
    EXPECT_EQ(e.code(), IntegrationErrorCode::FileNotReadable);
    EXPECT_EQ(e.details(), QStringLiteral("permission denied"));
}

TEST(IntegrationError, StackingFailedIsNotRecoverable)
{
    const auto e = IntegrationError::stackingFailed(QStringLiteral("OOM"));
    EXPECT_FALSE(e.isRecoverable());
    EXPECT_FALSE(e.isCancellation());
    EXPECT_EQ(e.code(), IntegrationErrorCode::StackingFailed);
}

TEST(IntegrationError, RegistrationFailedIsNotRecoverable)
{
    const auto e = IntegrationError::registrationFailed(QStringLiteral("Too few stars"));
    EXPECT_FALSE(e.isRecoverable());
    EXPECT_EQ(e.code(), IntegrationErrorCode::RegistrationFailed);
}

TEST(IntegrationError, CancelledIsCancellation)
{
    const auto e = IntegrationError::cancelled();
    EXPECT_TRUE(e.isCancellation());
    EXPECT_FALSE(e.isRecoverable());
    EXPECT_EQ(e.code(), IntegrationErrorCode::Cancelled);
}

TEST(IntegrationError, DirectoryNotWritableIsNotRecoverable)
{
    const auto e = IntegrationError::directoryNotWritable(QStringLiteral("/tmp/readonly"));
    EXPECT_FALSE(e.isRecoverable());
    EXPECT_EQ(e.code(), IntegrationErrorCode::DirectoryNotWritable);
}

TEST(IntegrationError, ToUserStringIncludesFilename)
{
    const auto e = IntegrationError::fileNotFound(QStringLiteral("/data/images/light_001.fit"));
    const QString user = e.toUserString();
    EXPECT_TRUE(user.contains(QStringLiteral("light_001.fit")));
    // Should not contain the error code number
    EXPECT_FALSE(user.contains(QStringLiteral("[100]")));
}

TEST(IntegrationError, ToLogStringIncludesErrorCode)
{
    const auto e = IntegrationError::fileNotFound(QStringLiteral("/data/images/light_001.fit"));
    const QString log = e.toLogString();
    EXPECT_TRUE(log.contains(QStringLiteral("[100]")));
    EXPECT_TRUE(log.contains(QStringLiteral("File:")));
}

TEST(IntegrationError, MemoryExhaustedHasAdvice)
{
    const auto e = IntegrationError::memoryExhausted();
    EXPECT_EQ(e.code(), IntegrationErrorCode::MemoryExhausted);
    EXPECT_FALSE(e.isRecoverable());
    EXPECT_FALSE(e.details().isEmpty());
}

TEST(IntegrationError, InvalidFileFormatIsRecoverable)
{
    const auto e =
        IntegrationError::invalidFileFormat(QStringLiteral("/tmp/bad.raw"), QStringLiteral("unknown TIFF tag"));
    EXPECT_TRUE(e.isRecoverable());
    EXPECT_EQ(e.code(), IntegrationErrorCode::InvalidFileFormat);
}

// ============================================================================
// isRecoverableError / isCancellationError free functions
// ============================================================================

TEST(IntegrationErrorHelpers, IsRecoverableCategorization)
{
    EXPECT_TRUE(isRecoverableError(IntegrationErrorCode::None));
    EXPECT_TRUE(isRecoverableError(IntegrationErrorCode::FileNotFound));
    EXPECT_TRUE(isRecoverableError(IntegrationErrorCode::FileNotReadable));
    EXPECT_TRUE(isRecoverableError(IntegrationErrorCode::InvalidFileFormat));

    EXPECT_FALSE(isRecoverableError(IntegrationErrorCode::StackingFailed));
    EXPECT_FALSE(isRecoverableError(IntegrationErrorCode::RegistrationFailed));
    EXPECT_FALSE(isRecoverableError(IntegrationErrorCode::DirectoryNotWritable));
    EXPECT_FALSE(isRecoverableError(IntegrationErrorCode::MemoryExhausted));
    EXPECT_FALSE(isRecoverableError(IntegrationErrorCode::Cancelled));
}

TEST(IntegrationErrorHelpers, IsCancellationCategorization)
{
    EXPECT_TRUE(isCancellationError(IntegrationErrorCode::Cancelled));
    EXPECT_TRUE(isCancellationError(IntegrationErrorCode::Timeout));

    EXPECT_FALSE(isCancellationError(IntegrationErrorCode::None));
    EXPECT_FALSE(isCancellationError(IntegrationErrorCode::FileNotFound));
    EXPECT_FALSE(isCancellationError(IntegrationErrorCode::StackingFailed));
}
