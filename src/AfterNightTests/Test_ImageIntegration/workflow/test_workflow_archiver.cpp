// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow archiver: each run's history directory, its manifest, report and log, the outputs it keeps and the ones
/// its retention policy does not, and the previews the report shows. A preview the run did not prepare is made only in
/// the engine session the archiver is given; without one the archiver writes none.

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ImageIntegration/ImportSession.h"

#include "controls/ImageViewFiles.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowFileOperations.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/reporting/WorkflowReportArchiveLoader.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/core/RegistrationMetricsWriter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/OutputIntent.h>

namespace
{

void writeSizedFile(const QString& path, int sizeBytes)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QByteArray(sizeBytes, 'x'));
    file.close();
}

WorkflowManifest createTestManifest(const QString& outputDir)
{
    WorkflowManifest m;
    m.outputDirectory = outputDir;
    m.executionId = "TEST_EXEC_ID_001";
    m.executionMode = ExecutionMode::Full;
    return m;
}

WorkflowExecutor::Result createTestResult(const QString& outputDir)
{
    WorkflowExecutor::Result r;
    r.success = true;
    r.totalDurationMs = 1234;

    WorkflowExecutor::Result::StageTiming s1;
    s1.stageName = "TestStage";
    s1.status = "completed";
    s1.durationMs = 100;
    r.stageResults.push_back(s1);

    IntegrationGroupResult g1;
    g1.groupKey = "Group1";
    g1.success = true;
    g1.outputPath = QDir(outputDir).filePath("stacked_Group1.fits");
    g1.outputFilename = "stacked_Group1.fits";
    g1.outputBytes = 1024;
    r.integrationGroupResults.push_back(g1);

    WorkflowExecutor::Result::CalibrationGroupResult c1;
    c1.type = "Bias";
    c1.groupKey = "BiasGroup";
    c1.masterPath = QDir(outputDir).filePath("master_bias.fits");
    c1.inputCount = 20;
    c1.success = true;
    r.calibrationGroupResults.push_back(c1);

    return r;
}

QString uuidStagedPathForTest(const QString& originalPath)
{
    return originalPath + QStringLiteral(".tmp_archive_123e4567-e89b-12d3-a456-426614174000");
}

/// Writes a grey Float32 master of a gentle gradient as a FITS file the session reads; fails the case when it cannot.
void writeFitsMaster(const an::consumer::EngineSession& session, const QString& path)
{
    const std::optional<an::consumer::ImageAsset> master = an::tests::viewer::makeAsset<float>(
        session, 64u, 64u, an::tests::viewer::Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.05f + 0.002f * static_cast<float>((x + y) % 9u);
        });
    ASSERT_TRUE(master.has_value());
    const an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    const an::Status written = an::ui::writeViewImage(session, *master, path, *intent, {});
    ASSERT_TRUE(written.ok()) << written.message();
}

TEST(WorkflowArchiver, CreatesHistoryDirectoryAndManifest)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);

    WorkflowArchiver archiver(outputDir);
    auto res = archiver.commit(manifest, result);

    EXPECT_TRUE(res.success);
    EXPECT_FALSE(res.archivePath.isEmpty());
    EXPECT_TRUE(QDir(res.archivePath).exists());

    QString manifestPath = QDir(res.archivePath).filePath("workflow_manifest.json");
    EXPECT_TRUE(QFile::exists(manifestPath));
    EXPECT_TRUE(QFile::exists(QDir(res.archivePath).filePath("workflow_report.md")));
    EXPECT_TRUE(QFile::exists(QDir(res.archivePath).filePath("workflow_report.html")));

    QFile f(manifestPath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QJsonObject root = doc.object();

    EXPECT_EQ(root["schemaVersion"].toInt(), 3);
    EXPECT_EQ(root["executionId"].toString(), "TEST_EXEC_ID_001");
    EXPECT_EQ(root["overallResult"].toString(), "success");
    EXPECT_FALSE(root.contains(QStringLiteral("executionCounters")));

    QJsonArray groups = root["integrationGroupResults"].toArray();
    ASSERT_EQ(groups.size(), 1);
    EXPECT_EQ(groups[0].toObject()["groupKey"].toString(), "Group1");

    QJsonArray calibration = root["calibrationGroupResults"].toArray();
    ASSERT_EQ(calibration.size(), 1);
    EXPECT_EQ(calibration[0].toObject()["type"].toString(), "Bias");
}

TEST(WorkflowArchiver, FailsCommitWhenCapturedWorkflowLogCannotBePersisted)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    WorkflowManifest manifest = createTestManifest(tempDir.path());
    WorkflowExecutor::Result result = createTestResult(tempDir.path());
    result.capturedLog = QStringLiteral("durable execution log");

    const QString originalPath = result.integrationGroupResults.front().outputPath;
    const QString stagedPath = uuidStagedPathForTest(originalPath);
    {
        QFile current(originalPath);
        ASSERT_TRUE(current.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(current.write("new output"), 10);
    }
    {
        QFile previous(stagedPath);
        ASSERT_TRUE(previous.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(previous.write("previous output"), 15);
    }
    result.integrationGroupResults.front().stagedFiles.push_back({originalPath, stagedPath});

    int writeAttempts = 0;
    WorkflowArchiver archiver(tempDir.path(), nullptr, [&](const QString&, const QByteArray&) {
        ++writeAttempts;
        return false;
    });
    const auto archiveResult = archiver.commit(manifest, result);

    EXPECT_FALSE(archiveResult.success);
    EXPECT_EQ(writeAttempts, 1);
    EXPECT_TRUE(archiveResult.error.contains(QStringLiteral("workflow_log.txt")));
    EXPECT_FALSE(QFileInfo::exists(stagedPath));
    QFile restored(originalPath);
    ASSERT_TRUE(restored.open(QIODevice::ReadOnly));
    EXPECT_EQ(restored.readAll(), QByteArray("previous output"));
    EXPECT_FALSE(QDir(archiveResult.archivePath).exists());
    EXPECT_FALSE(QFileInfo::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_manifest.json"))));
}

TEST(WorkflowArchiver, FinalManifestFailureRestoresStagedPreviousOutputAndRemovesIncompleteRun)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    WorkflowManifest manifest = createTestManifest(tempDir.path());
    WorkflowExecutor::Result result = createTestResult(tempDir.path());
    result.capturedLog = QStringLiteral("force the injected log writer");

    const QString originalPath = result.integrationGroupResults.front().outputPath;
    const QString stagedPath = uuidStagedPathForTest(originalPath);
    {
        QFile current(originalPath);
        ASSERT_TRUE(current.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(current.write("new output"), 10);
    }
    {
        QFile previous(stagedPath);
        ASSERT_TRUE(previous.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(previous.write("previous output"), 15);
    }
    result.integrationGroupResults.front().stagedFiles.push_back({originalPath, stagedPath});

    WorkflowArchiver archiver(tempDir.path(), nullptr, [&](const QString& logPath, const QByteArray& bytes) {
        QFile log(logPath);
        if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate) || log.write(bytes) != bytes.size())
        {
            return false;
        }
        log.close();
        return QDir().mkpath(QFileInfo(logPath).dir().filePath(QStringLiteral("workflow_manifest.json")));
    });
    const auto archiveResult = archiver.commit(manifest, result);

    EXPECT_FALSE(archiveResult.success);
    EXPECT_TRUE(archiveResult.error.contains(QStringLiteral("workflow_manifest.json")));
    EXPECT_FALSE(QFileInfo::exists(stagedPath));
    QFile restored(originalPath);
    ASSERT_TRUE(restored.open(QIODevice::ReadOnly));
    EXPECT_EQ(restored.readAll(), QByteArray("previous output"));
    EXPECT_FALSE(QFileInfo::exists(archiveResult.archivePath));
}

TEST(WorkflowArchiver, RebuiltReportPreservesAlignmentCorrectionsAndExecutionLog)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowManifest::RegistrationGroup registrationGroup;
    registrationGroup.groupKey = GroupKey{QStringLiteral("Group1")};
    manifest.registrationGroups.push_back(registrationGroup);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    AlignmentGroupResult alignment;
    alignment.groupKey = QStringLiteral("Group1");
    alignment.totalAligned = 4;
    alignment.sipCorrectedCount = 3;
    alignment.caCorrectedCount = 2;
    alignment.caPassthroughCount = 2;
    result.alignmentGroupResults.push_back(alignment);
    result.capturedLog = QStringLiteral("[2026-08-12T00:00:00.000] [INFO] archive-rebuild-log-sentinel");

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success) << archiveResult.error.toStdString();

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject archiveRoot = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QString rebuilt =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, archiveRoot);

    EXPECT_TRUE(rebuilt.contains(QStringLiteral("SIP Distortion Correction")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("Chromatic Aberration Correction")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Group1 | 2 | 2 |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("archive-rebuild-log-sentinel")));
}

TEST(WorkflowArchiver, RebuiltReportRejectsOversizedExecutionLog)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString runDir = tempDir.path();
    const WorkflowManifest manifest = createTestManifest(runDir);
    QJsonObject archiveRoot;
    archiveRoot.insert(QStringLiteral("manifest"), manifest.toJson());
    archiveRoot.insert(QStringLiteral("overallResult"), QStringLiteral("success"));

    QFile workflowLog(QDir(runDir).filePath(QStringLiteral("workflow_log.txt")));
    ASSERT_TRUE(workflowLog.open(QIODevice::WriteOnly));
    const QByteArray oversizedLog(ImageIntegration::schema::kWorkflowLogMaxBytes + 1, 'L');
    ASSERT_EQ(workflowLog.write(oversizedLog), oversizedLog.size());
    workflowLog.close();

    const QString rebuilt = ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(runDir, archiveRoot);

    EXPECT_TRUE(rebuilt.contains(QStringLiteral("Execution log omitted")));
    EXPECT_FALSE(rebuilt.contains(QString(1024, QLatin1Char('L'))));
}

TEST(WorkflowArchiver, RejectsExecutionIdPathTraversal)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.executionId = QStringLiteral("../escaped_run");
    WorkflowExecutor::Result result = createTestResult(outputDir);

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);

    EXPECT_FALSE(archiveResult.success);
    EXPECT_FALSE(QDir(QDir(outputDir).filePath(QStringLiteral("escaped_run"))).exists());
}

TEST(WorkflowArchiver, RejectsExistingRunDirectoryWithoutMutatingIt)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    const QString runDir = QDir(outputDir).filePath(QStringLiteral("history/TEST_EXEC_ID_001"));
    ASSERT_TRUE(QDir().mkpath(runDir));
    const QString sentinelPath = QDir(runDir).filePath(QStringLiteral("sentinel.txt"));
    writeSizedFile(sentinelPath, 17);
    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);

    EXPECT_FALSE(archiveResult.success);
    EXPECT_TRUE(QFile::exists(sentinelPath));
    EXPECT_EQ(QFileInfo(sentinelPath).size(), 17);
    EXPECT_FALSE(QFile::exists(QDir(runDir).filePath(QStringLiteral("workflow_manifest.json"))));
}

TEST(WorkflowArchiver, UsesCommittedSourceBasenameForPayloadDestination)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    group.outputPath = QDir(outputDir).filePath(QStringLiteral("safe_output.fit"));
    group.outputFilename = QStringLiteral("../escaped_output.fit");
    writeSizedFile(group.outputPath, 64);

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);

    ASSERT_TRUE(archiveResult.success) << archiveResult.error.toStdString();
    EXPECT_TRUE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/safe_output.fit"))));
    EXPECT_FALSE(QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("escaped_output.fit"))));
}

TEST(WorkflowArchiver, ConsumesPreparedMasterPreviewWithoutReloadingOutput)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();

    QImage preparedPreview(32, 16, QImage::Format_RGB888);
    preparedPreview.fill(QColor(48, 96, 144));
    QBuffer preparedBuffer(&group.reportPreviewJpeg);
    ASSERT_TRUE(preparedBuffer.open(QIODevice::WriteOnly));
    ASSERT_TRUE(preparedPreview.save(&preparedBuffer, "JPEG", 90));
    group.reportPreviewSize = preparedPreview.size();
    const QByteArray preparedBytes = group.reportPreviewJpeg;

    // No source master is present: a valid prepared asset must be archived directly.
    EXPECT_FALSE(QFile::exists(group.outputPath));

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    const QString archivedPreview =
        QDir(archiveResult.archivePath).filePath(QStringLiteral("report_previews/Group1/master.jpg"));
    QFile archivedFile(archivedPreview);
    ASSERT_TRUE(archivedFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(archivedFile.readAll(), preparedBytes);
    archivedFile.close();

    const QImage archivedImage(archivedPreview);
    EXPECT_EQ(archivedImage.size(), preparedPreview.size());
}

TEST(WorkflowArchiver, CurrentRunPreviewFailureDoesNotReloadCommittedMaster)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    group.reportPreviewPreparedForCurrentRun = true;
    group.reportPreviewJpeg.clear();
    group.reportPreviewSize = {};

    // A master the session reads, so only the run's own failed attempt keeps the archiver from previewing it.
    writeFitsMaster(*session, group.outputPath);
    group.outputBytes = QFileInfo(group.outputPath).size();

    WorkflowArchiver archiver(outputDir);
    archiver.setEngineSession(&*session);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_FALSE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("report_previews/Group1/master.jpg"))));
}

TEST(WorkflowArchiver, PreviewsTheCommittedMasterOnlyInTheSessionItIsGiven)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    const auto commitWith = [&](const an::consumer::EngineSession* lent, const QString& executionId) {
        WorkflowManifest manifest = createTestManifest(outputDir);
        manifest.executionId = executionId;
        WorkflowExecutor::Result result = createTestResult(outputDir);
        auto& group = result.integrationGroupResults.front();
        group.outputBytes = QFileInfo(group.outputPath).size();
        WorkflowArchiver archiver(outputDir);
        archiver.setEngineSession(lent);
        return archiver.commit(manifest, result);
    };

    writeFitsMaster(*session, createTestResult(outputDir).integrationGroupResults.front().outputPath);

    // Without a session, nothing the run did not prepare is previewed.
    const auto withoutSession = commitWith(nullptr, QStringLiteral("TEST_EXEC_NO_SESSION"));
    ASSERT_TRUE(withoutSession.success) << withoutSession.error.toStdString();
    EXPECT_FALSE(
        QFile::exists(QDir(withoutSession.archivePath).filePath(QStringLiteral("report_previews/Group1/master.jpg"))));

    // In the session, the committed master is read and previewed for the report.
    const auto withSession = commitWith(&*session, QStringLiteral("TEST_EXEC_SESSION"));
    ASSERT_TRUE(withSession.success) << withSession.error.toStdString();
    const QImage preview(QDir(withSession.archivePath).filePath(QStringLiteral("report_previews/Group1/master.jpg")));
    ASSERT_FALSE(preview.isNull());
    EXPECT_EQ(preview.size(), QSize(64, 64));
}

TEST(WorkflowArchiver, OversizedRunSkipsPayloadCopyAndPersistsNotRetainedStatus)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, 512);
    QFile sourceFile(group.outputPath);
    ASSERT_TRUE(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray sourceBytes = sourceFile.readAll();

    QImage preparedPreview(24, 12, QImage::Format_RGB888);
    preparedPreview.fill(QColor(24, 48, 96));
    QBuffer preparedBuffer(&group.reportPreviewJpeg);
    ASSERT_TRUE(preparedBuffer.open(QIODevice::WriteOnly));
    ASSERT_TRUE(preparedPreview.save(&preparedBuffer, "JPEG", 90));
    group.reportPreviewSize = preparedPreview.size();
    const QByteArray preparedBytes = group.reportPreviewJpeg;

    WorkflowExecutionCounterAccumulator counters;
    WorkflowArchiver archiver(outputDir, 10, 128, &counters);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_EQ(archiveResult.itemsArchived, 0);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_FALSE(group.outputRetentionReason.isEmpty());
    EXPECT_TRUE(QFile::exists(group.outputPath));
    QFile sourceAfter(group.outputPath);
    ASSERT_TRUE(sourceAfter.open(QIODevice::ReadOnly));
    EXPECT_EQ(sourceAfter.readAll(), sourceBytes);

    const QDir archiveDir(archiveResult.archivePath);
    EXPECT_FALSE(QFile::exists(archiveDir.filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));
    QFile previewFile(archiveDir.filePath(QStringLiteral("report_previews/Group1/master.jpg")));
    ASSERT_TRUE(previewFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(previewFile.readAll(), preparedBytes);

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject groupJson = root.value(QStringLiteral("integrationGroupResults")).toArray().first().toObject();
    EXPECT_EQ(groupJson.value(QStringLiteral("result")).toString(), QStringLiteral("success"));
    EXPECT_EQ(groupJson.value(QStringLiteral("outputRetentionStatus")).toString(),
              QStringLiteral("not_retained_by_retention_policy"));
    EXPECT_FALSE(groupJson.value(QStringLiteral("outputRetentionReason")).toString().isEmpty());
    EXPECT_TRUE(groupJson.value(QStringLiteral("archivedFiles")).toArray().isEmpty());

    QFile summaryFile(archiveDir.filePath(QStringLiteral("groups/Group1/group_summary.json")));
    ASSERT_TRUE(summaryFile.open(QIODevice::ReadOnly));
    const QJsonObject summary = QJsonDocument::fromJson(summaryFile.readAll()).object();
    EXPECT_EQ(summary.value(QStringLiteral("outputRetentionStatus")).toString(),
              QStringLiteral("not_retained_by_retention_policy"));

    QFile reportFile(archiveResult.reportPath);
    ASSERT_TRUE(reportFile.open(QIODevice::ReadOnly));
    EXPECT_TRUE(QString::fromUtf8(reportFile.readAll())
                    .contains(QStringLiteral("Scientific output succeeded, but its history copy was not retained")));
}

TEST(WorkflowArchiver, RetentionPreflightAccountsForMandatoryMetadataWrittenAfterPayloadDecision)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, 64 * 1024);
    result.capturedLog = QString(260 * 1024, QLatin1Char('L'));

    WorkflowExecutionCounterAccumulator counters;
    WorkflowArchiver archiver(outputDir, 10, 300 * 1024, &counters);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_EQ(archiveResult.itemsArchived, 0);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_FALSE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));
    EXPECT_TRUE(QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_log.txt"))));
}

TEST(WorkflowArchiver, RetentionPreflightAccountsForUnboundedWarningsAndFinalMetadata)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, 64 * 1024);
    result.warnings.push_back(QString(260 * 1024, QLatin1Char('W')));
    result.warningCount = 1;

    WorkflowExecutionCounterAccumulator counters;
    WorkflowArchiver archiver(outputDir, 10, 300 * 1024, &counters);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_EQ(archiveResult.itemsArchived, 0);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_FALSE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject groupJson = root.value(QStringLiteral("integrationGroupResults")).toArray().first().toObject();
    EXPECT_EQ(groupJson.value(QStringLiteral("outputRetentionStatus")).toString(),
              QStringLiteral("not_retained_by_retention_policy"));
    EXPECT_TRUE(groupJson.value(QStringLiteral("archivedFiles")).toArray().isEmpty());
}

TEST(WorkflowArchiver, RenderedMetadataPreflightDropsPayloadBeforeTransfer)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.processLabel = QString(180 * 1024, QLatin1Char('P'));
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, 64 * 1024);

    WorkflowExecutionCounterAccumulator counters;
    WorkflowArchiver archiver(outputDir, 10, 320 * 1024, &counters);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_EQ(archiveResult.itemsArchived, 0);
    EXPECT_TRUE(archiveResult.payloadTransports.empty());
    EXPECT_EQ(archiveResult.transportDiagnostics.logicalBytesCloned, 0u);
    EXPECT_EQ(archiveResult.transportDiagnostics.ordinaryCopyBytes, 0u);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_FALSE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject groupJson = root.value(QStringLiteral("integrationGroupResults")).toArray().first().toObject();
    EXPECT_EQ(groupJson.value(QStringLiteral("outputRetentionStatus")).toString(),
              QStringLiteral("not_retained_by_retention_policy"));
    EXPECT_TRUE(groupJson.value(QStringLiteral("archivedFiles")).toArray().isEmpty());
}

TEST(WorkflowArchiver, RetainableRunPrunesOldHistoryBeforeCopying)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    const QDir historyDir(QDir(outputDir).filePath(QStringLiteral("history")));
    ASSERT_TRUE(QDir().mkpath(historyDir.path()));

    const QString oldestRun = historyDir.filePath(QStringLiteral("oldest_run"));
    ASSERT_TRUE(QDir().mkpath(oldestRun));
    writeSizedFile(QDir(oldestRun).filePath(QStringLiteral("payload.bin")), 360000);
    QThread::msleep(15);
    const QString newerRun = historyDir.filePath(QStringLiteral("newer_run"));
    ASSERT_TRUE(QDir().mkpath(newerRun));
    writeSizedFile(QDir(newerRun).filePath(QStringLiteral("payload.bin")), 360000);

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    writeSizedFile(result.integrationGroupResults.front().outputPath, 300000);

    WorkflowExecutionCounterAccumulator counters;
    WorkflowArchiver archiver(outputDir, 10, 1000000, &counters);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_FALSE(QDir(oldestRun).exists());
    EXPECT_TRUE(QDir(newerRun).exists());
    EXPECT_TRUE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));
    EXPECT_EQ(result.integrationGroupResults.front().outputRetentionStatus, OutputRetentionStatus::RetainedInHistory);
    EXPECT_EQ(archiveResult.itemsArchived, 1);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 300000u);
}

TEST(WorkflowArchiver, FailedArchiveDoesNotPrunePreviouslyCommittedHistory)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    const QDir historyDir(QDir(outputDir).filePath(QStringLiteral("history")));
    ASSERT_TRUE(QDir().mkpath(historyDir.path()));

    const QString oldRun = historyDir.filePath(QStringLiteral("old_valid_run"));
    ASSERT_TRUE(QDir().mkpath(oldRun));
    writeSizedFile(QDir(oldRun).filePath(QStringLiteral("workflow_manifest.json")), 256);

    WorkflowManifest manifest = createTestManifest(outputDir);
    const QString currentRun = historyDir.filePath(manifest.executionId);
    ASSERT_TRUE(QDir().mkpath(QDir(currentRun).filePath(QStringLiteral("workflow_manifest.json"))));
    WorkflowExecutor::Result result = createTestResult(outputDir);
    writeSizedFile(result.integrationGroupResults.front().outputPath, 512);

    WorkflowArchiver archiver(outputDir, 1, 1024 * 1024);
    const auto archiveResult = archiver.commit(manifest, result);

    EXPECT_FALSE(archiveResult.success);
    EXPECT_TRUE(QDir(oldRun).exists());
    EXPECT_TRUE(QFile::exists(QDir(oldRun).filePath(QStringLiteral("workflow_manifest.json"))));
}

TEST(WorkflowArchiver, RegistrationDatabaseSnapshotIncludesUncheckpointedWalState)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    const QString sourcePath = QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));

    core::SqliteConnection live(sourcePath, QStringLiteral("WorkflowArchiverWalSource"));
    QString error;
    ASSERT_TRUE(live.open(&error)) << error.toStdString();
    QSqlQuery query(live.db());
    ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) << query.lastError().text().toStdString();
    ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA wal_autocheckpoint=0"))) << query.lastError().text().toStdString();
    ASSERT_TRUE(query.exec(QStringLiteral("CREATE TABLE snapshot_rows (id INTEGER PRIMARY KEY, value TEXT)")))
        << query.lastError().text().toStdString();
    ASSERT_TRUE(query.exec(QStringLiteral("INSERT INTO snapshot_rows (value) VALUES ('wal-visible')")))
        << query.lastError().text().toStdString();
    ASSERT_TRUE(QFileInfo::exists(sourcePath + QStringLiteral("-wal")));

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success) << archiveResult.error.toStdString();

    const QString archivedPath = QDir(archiveResult.archivePath).filePath(QStringLiteral("registration_metrics.db"));
    core::SqliteConnection archived(archivedPath, QStringLiteral("WorkflowArchiverWalSnapshot"));
    ASSERT_TRUE(archived.openReadOnly(&error)) << error.toStdString();
    QSqlQuery archivedQuery(archived.db());
    ASSERT_TRUE(archivedQuery.exec(QStringLiteral("PRAGMA integrity_check")))
        << archivedQuery.lastError().text().toStdString();
    ASSERT_TRUE(archivedQuery.next());
    EXPECT_EQ(archivedQuery.value(0).toString(), QStringLiteral("ok"));
    ASSERT_TRUE(archivedQuery.exec(QStringLiteral("SELECT value FROM snapshot_rows")))
        << archivedQuery.lastError().text().toStdString();
    ASSERT_TRUE(archivedQuery.next());
    EXPECT_EQ(archivedQuery.value(0).toString(), QStringLiteral("wal-visible"));
}

TEST(WorkflowArchiver, RetentionPreflightAccountsForUncheckpointedRegistrationDatabaseWal)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    const QString sourcePath = QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));

    core::SqliteConnection live(sourcePath, QStringLiteral("WorkflowArchiverWalRetentionSource"));
    QString error;
    ASSERT_TRUE(live.open(&error)) << error.toStdString();
    QSqlQuery query(live.db());
    ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) << query.lastError().text().toStdString();
    ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA wal_autocheckpoint=0"))) << query.lastError().text().toStdString();
    ASSERT_TRUE(query.exec(QStringLiteral("CREATE TABLE snapshot_rows (id INTEGER PRIMARY KEY, value BLOB)")))
        << query.lastError().text().toStdString();
    ASSERT_TRUE(query.exec(QStringLiteral("INSERT INTO snapshot_rows (value) VALUES (randomblob(4194304))")))
        << query.lastError().text().toStdString();

    const QString walPath = sourcePath + QStringLiteral("-wal");
    const qint64 mainBytes = QFileInfo(sourcePath).size();
    const qint64 walBytes = QFileInfo(walPath).size();
    ASSERT_GT(walBytes, 1024 * 1024);

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, 512);

    const qint64 maxHistoryBytes = mainBytes + walBytes / 2;
    WorkflowArchiver archiver(outputDir, 10, maxHistoryBytes);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success) << archiveResult.error.toStdString();

    EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_EQ(archiveResult.itemsArchived, 0);
    EXPECT_FALSE(QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("registration_metrics.db"))));
}

TEST(WorkflowArchiver, OutputRetentionStatusRoundTripsAndAnArchiveWithoutItReadsAsUnknown)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    writeSizedFile(result.integrationGroupResults.front().outputPath, 512);

    WorkflowArchiver archiver(outputDir, 10, 128);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    EXPECT_EQ(root.value(QStringLiteral("overallResult")).toString(), QStringLiteral("success"));

    const QString rebuilt =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("Scientific output succeeded, but its history copy was not retained")));

    QJsonArray bareGroups = root.value(QStringLiteral("integrationGroupResults")).toArray();
    QJsonObject bareGroup = bareGroups.first().toObject();
    bareGroup.remove(QStringLiteral("outputRetentionStatus"));
    bareGroup.remove(QStringLiteral("outputRetentionReason"));
    bareGroups.replace(0, bareGroup);
    root.insert(QStringLiteral("integrationGroupResults"), bareGroups);

    const QString rebuiltBare =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_FALSE(
        rebuiltBare.contains(QStringLiteral("Scientific output succeeded, but its history copy was not retained")));
}

TEST(WorkflowArchiver, PreparationTimingRoundTripsAndAnArchiveWithoutItDefaultsToZero)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    result.preparationTiming.captured = true;
    result.preparationTiming.manifestBuildMs = 131;
    result.preparationTiming.planAndValidationMs = 269;
    result.executorTiming.captured = true;
    result.executorTiming.stageExecutionMs = 700;
    result.executorTiming.stateCommitMs = 100;
    result.executorTiming.archiveMs = 300;
    result.executorTiming.reportMs = 134;
    result.totalDurationMs = static_cast<int>(result.executorTiming.exclusivePhasesTotalMs());

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QFile manifestFile(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject preparationTiming = root.value(QStringLiteral("preparationTiming")).toObject();
    EXPECT_EQ(preparationTiming.value(QStringLiteral("manifestBuildMs")).toInteger(), 131);
    EXPECT_EQ(preparationTiming.value(QStringLiteral("planAndValidationMs")).toInteger(), 269);
    const QJsonObject executorTiming = root.value(QStringLiteral("executorTiming")).toObject();
    EXPECT_EQ(executorTiming.value(QStringLiteral("stageExecutionMs")).toInteger(), 700);
    EXPECT_EQ(executorTiming.value(QStringLiteral("stateCommitMs")).toInteger(), 100);
    EXPECT_EQ(executorTiming.value(QStringLiteral("archiveMs")).toInteger(), 300);
    EXPECT_EQ(executorTiming.value(QStringLiteral("reportMs")).toInteger(), 134);

    QFile markdownFile(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_report.md")));
    ASSERT_TRUE(markdownFile.open(QIODevice::ReadOnly));
    const QString archivedMarkdown = QString::fromUtf8(markdownFile.readAll());
    EXPECT_TRUE(archivedMarkdown.contains(QStringLiteral("| Manifest Build | 131ms |")));
    EXPECT_TRUE(archivedMarkdown.contains(QStringLiteral("| Plan & Validation | 269ms |")));
    EXPECT_TRUE(archivedMarkdown.contains(QStringLiteral("| State Commit | 100ms |")));
    EXPECT_TRUE(archivedMarkdown.contains(QStringLiteral("| Archive | 300ms |")));
    EXPECT_TRUE(archivedMarkdown.contains(QStringLiteral("| Report | 134ms |")));

    QFile htmlFile(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_report.html")));
    ASSERT_TRUE(htmlFile.open(QIODevice::ReadOnly));
    const QString archivedHtml = QString::fromUtf8(htmlFile.readAll());
    EXPECT_TRUE(archivedHtml.contains(QStringLiteral("Manifest Build")));
    EXPECT_TRUE(archivedHtml.contains(QStringLiteral("Plan &amp; Validation")));

    const QString rebuilt =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Manifest Build | 131ms |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Plan & Validation | 269ms |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| State Commit | 100ms |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Archive | 300ms |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Report | 134ms |")));

    root.remove(QStringLiteral("preparationTiming"));
    root.remove(QStringLiteral("executorTiming"));
    const QString rebuiltBare =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_FALSE(rebuiltBare.contains(QStringLiteral("| Manifest Build |")));
    EXPECT_FALSE(rebuiltBare.contains(QStringLiteral("| Plan & Validation |")));
    EXPECT_FALSE(rebuiltBare.contains(QStringLiteral("| State Commit |")));
    EXPECT_FALSE(rebuiltBare.contains(QStringLiteral("| Archive |")));
    EXPECT_FALSE(rebuiltBare.contains(QStringLiteral("| Report |")));
}

TEST(WorkflowArchiver, StackGroupFinalizationTimingRoundTripsAndAGroupWithoutItRemainsUncaptured)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    group.durationMs = 900;
    group.finalizationTiming.captured = true;
    group.finalizationTiming.phases = {
        {.stageName = QStringLiteral("Final Master Save"),
         .status = QStringLiteral("completed"),
         .durationMs = 110,
         .groupsProcessed = 1},
        {.stageName = QStringLiteral("Requested Maps"),
         .status = QStringLiteral("skipped"),
         .durationMs = 0,
         .groupsProcessed = 0},
        {.stageName = QStringLiteral("Preview Generation"),
         .status = QStringLiteral("completed"),
         .durationMs = 130,
         .groupsProcessed = 1},
    };

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QFile manifestFile(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QJsonObject groupObject = root.value(QStringLiteral("integrationGroupResults")).toArray().first().toObject();
    const QJsonObject finalization = groupObject.value(QStringLiteral("finalizationTiming")).toObject();
    EXPECT_TRUE(finalization.value(QStringLiteral("captured")).toBool());
    const QJsonArray phases = finalization.value(QStringLiteral("phases")).toArray();
    ASSERT_EQ(phases.size(), 3);
    EXPECT_EQ(phases.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Final Master Save"));
    EXPECT_EQ(phases.at(0).toObject().value(QStringLiteral("durationMs")).toInt(), 110);
    EXPECT_EQ(phases.at(1).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("skipped"));
    EXPECT_EQ(phases.at(1).toObject().value(QStringLiteral("durationMs")).toInt(), 0);

    const QString rebuilt =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Final Master Save | completed | 110ms |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Requested Maps | skipped | 0ms |")));

    groupObject.remove(QStringLiteral("finalizationTiming"));
    QJsonArray bareGroups;
    bareGroups.append(groupObject);
    root.insert(QStringLiteral("integrationGroupResults"), bareGroups);
    const QString rebuiltBare =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_FALSE(rebuiltBare.contains(QStringLiteral("Stack Group Finalization")));
}

TEST(WorkflowArchiver, RegistrationOnlyRunDoesNotCreateWorkflowReport)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.executionId = QStringLiteral("TEST_REGISTRATION_ONLY");
    manifest.executionScope = QStringLiteral("registration_all");
    manifest.executionMode = ExecutionMode::RegistrationOnly;
    manifest.stageSequence = {QStringLiteral("LightsRegistration")};

    WorkflowExecutor::Result result;
    result.success = true;
    result.totalDurationMs = 345;
    WorkflowExecutor::Result::StageTiming stage;
    stage.stageName = QStringLiteral("LightsRegistration");
    stage.status = QStringLiteral("completed");
    stage.durationMs = 345;
    result.stageResults.push_back(stage);

    WorkflowArchiver archiver(outputDir);
    const auto res = archiver.commit(manifest, result);

    ASSERT_TRUE(res.success);
    EXPECT_TRUE(res.reportPath.isEmpty());
    EXPECT_TRUE(QFile::exists(QDir(res.archivePath).filePath(QStringLiteral("workflow_manifest.json"))));
    EXPECT_FALSE(QFile::exists(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.md"))));
    EXPECT_FALSE(QFile::exists(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.html"))));
}

TEST(WorkflowArchiver, MirrorsLiteMetadataAtArchiveRootAndManifest)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.processLabel = QStringLiteral("Image Integration Lite");
    manifest.processingProfileId = QStringLiteral("balanced");
    manifest.processingProfileLabel = QStringLiteral("Balanced");
    const WorkflowExecutor::Result result = createTestResult(outputDir);

    WorkflowArchiver archiver(outputDir);
    const auto res = archiver.commit(manifest, result);
    ASSERT_TRUE(res.success);

    QFile f(QDir(res.archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    EXPECT_EQ(root.value(QStringLiteral("processLabel")).toString(), QStringLiteral("Image Integration Lite"));
    const QJsonObject rootProfile = root.value(QStringLiteral("processingProfile")).toObject();
    EXPECT_EQ(rootProfile.value(QStringLiteral("id")).toString(), QStringLiteral("balanced"));
    EXPECT_EQ(rootProfile.value(QStringLiteral("label")).toString(), QStringLiteral("Balanced"));

    const QJsonObject embeddedManifest = root.value(QStringLiteral("manifest")).toObject();
    EXPECT_EQ(embeddedManifest.value(QStringLiteral("processLabel")).toString(),
              QStringLiteral("Image Integration Lite"));
    const QJsonObject embeddedProfile = embeddedManifest.value(QStringLiteral("processingProfile")).toObject();
    EXPECT_EQ(embeddedProfile.value(QStringLiteral("id")).toString(), QStringLiteral("balanced"));
    EXPECT_EQ(embeddedProfile.value(QStringLiteral("label")).toString(), QStringLiteral("Balanced"));

    QFile html(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.html")));
    ASSERT_TRUE(html.open(QIODevice::ReadOnly));
    EXPECT_TRUE(QString::fromUtf8(html.readAll()).contains(QStringLiteral("AfterNight Image Integration Lite Report")));
}

TEST(WorkflowArchiver, RedactsAstrometryApiKeyFromArchiveAndReports)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.plateSolve.defaultSolver = QStringLiteral("astrometry_web");
    manifest.plateSolve.astrometryApiKey = QStringLiteral("archive-api-key");
    WorkflowExecutor::Result result = createTestResult(outputDir);

    WorkflowArchiver archiver(outputDir);
    const auto res = archiver.commit(manifest, result);
    ASSERT_TRUE(res.success);

    QFile manifestFile(QDir(res.archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QByteArray manifestPayload = manifestFile.readAll();
    EXPECT_FALSE(QString::fromUtf8(manifestPayload).contains(QStringLiteral("archive-api-key")));

    const QJsonObject root = QJsonDocument::fromJson(manifestPayload).object();
    const QJsonObject embeddedManifest = root.value(QStringLiteral("manifest")).toObject();
    const QJsonObject plateSolve = embeddedManifest.value(QStringLiteral("plateSolve")).toObject();
    EXPECT_FALSE(plateSolve.contains(QStringLiteral("astrometryApiKey")));
    EXPECT_TRUE(plateSolve.value(QStringLiteral("astrometryApiKeyRedacted")).toBool());
    EXPECT_FALSE(plateSolve.value(QStringLiteral("astrometryApiKeyHash")).toString().isEmpty());

    QFile markdownFile(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.md")));
    ASSERT_TRUE(markdownFile.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_FALSE(QString::fromUtf8(markdownFile.readAll()).contains(QStringLiteral("archive-api-key")));

    QFile htmlFile(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.html")));
    ASSERT_TRUE(htmlFile.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_FALSE(QString::fromUtf8(htmlFile.readAll()).contains(QStringLiteral("archive-api-key")));
}

TEST(WorkflowArchiver, GeneratedHtmlReportContainsEmbeddedStyles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);

    WorkflowArchiver archiver(outputDir);
    const auto res = archiver.commit(manifest, result);
    ASSERT_TRUE(res.success);

    const QString htmlPath = QDir(res.archivePath).filePath(QStringLiteral("workflow_report.html"));
    ASSERT_TRUE(QFile::exists(htmlPath));

    QFile htmlFile(htmlPath);
    ASSERT_TRUE(htmlFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString html = QString::fromUtf8(htmlFile.readAll());

    EXPECT_TRUE(html.contains(QStringLiteral("<!DOCTYPE html>")));
    EXPECT_TRUE(html.contains(QStringLiteral("<style>")));
    EXPECT_TRUE(html.contains(QStringLiteral("markdown-body")));
    EXPECT_TRUE(html.contains(QStringLiteral("color-scheme: ")));
    EXPECT_TRUE(html.contains(QStringLiteral("--an-accent")));
    EXPECT_TRUE(html.contains(QStringLiteral("an-report-hero")));
#if defined(AN_MARKDOWN_BACKEND_CMARK_GFM)
    EXPECT_TRUE(html.contains(QStringLiteral("<table>")));
#endif
}

TEST(WorkflowArchiver, ArchivesStagedFiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);

    // Create a "staged" file (representing a previous output that was renamed)
    QString originalPath = QDir(outputDir).filePath("stacked_Group1.fits");
    QString stagedPath = uuidStagedPathForTest(originalPath);
    {
        QFile current(originalPath);
        ASSERT_TRUE(current.open(QIODevice::WriteOnly));
        current.write("New Content");
        current.close();
    }

    QFile f(stagedPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("Old Content");
    f.close();

    // Update result to include this staged file
    result.integrationGroupResults[0].stagedFiles.push_back({originalPath, stagedPath});
    const QString previewPath = originalPath + ".preview.png";
    {
        QFile preview(previewPath);
        ASSERT_TRUE(preview.open(QIODevice::WriteOnly));
        preview.write("preview");
        preview.close();
    }
    result.integrationGroupResults[0].previewPath = previewPath;

    WorkflowArchiver archiver(outputDir);
    auto res = archiver.commit(manifest, result);

    EXPECT_TRUE(res.success);

    // Current output should be copied to history/groups/Group1/stacked_Group1.fits.
    // Staged previous output should be moved aside without becoming the comparable master.
    // Note: sanitization happens in Archiver. "Group1" -> "Group1"
    QString archiveGroupDir = QDir(res.archivePath).filePath("groups/Group1");
    QString archiveFile = QDir(archiveGroupDir).filePath("stacked_Group1.fits");
    QString previousFile = QDir(archiveGroupDir).filePath("previous/stacked_Group1.fits");

    EXPECT_TRUE(QFile::exists(archiveFile));
    EXPECT_TRUE(QFile::exists(previousFile));
    EXPECT_FALSE(QFile::exists(stagedPath));
    {
        QFile archived(archiveFile);
        ASSERT_TRUE(archived.open(QIODevice::ReadOnly));
        EXPECT_EQ(archived.readAll(), QByteArray("New Content"));
    }
    {
        QFile previous(previousFile);
        ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
        EXPECT_EQ(previous.readAll(), QByteArray("Old Content"));
    }

    const QString archivedPreview = QDir(archiveGroupDir).filePath("preview.png");
    EXPECT_TRUE(QFile::exists(archivedPreview));
    EXPECT_FALSE(QFile::exists(previewPath));

    const QString groupSummaryPath = QDir(archiveGroupDir).filePath("group_summary.json");
    EXPECT_TRUE(QFile::exists(groupSummaryPath));
    QFile groupSummaryFile(groupSummaryPath);
    ASSERT_TRUE(groupSummaryFile.open(QIODevice::ReadOnly));
    const QJsonObject groupSummaryJson = QJsonDocument::fromJson(groupSummaryFile.readAll()).object();
    EXPECT_EQ(groupSummaryJson["groupKey"].toString(), QStringLiteral("Group1"));
    EXPECT_EQ(groupSummaryJson["result"].toString(), QStringLiteral("success"));
    EXPECT_EQ(groupSummaryJson["outputPath"].toString(), QStringLiteral("groups/Group1/stacked_Group1.fits"));
    EXPECT_EQ(groupSummaryJson["previewPath"].toString(), QStringLiteral("groups/Group1/preview.png"));
    const QJsonArray archivedFiles = groupSummaryJson["archivedFiles"].toArray();
    ASSERT_EQ(archivedFiles.size(), 1);
    EXPECT_EQ(archivedFiles.at(0).toObject()["filename"].toString(), QStringLiteral("stacked_Group1.fits"));

    QFile manifestFile(QDir(res.archivePath).filePath("workflow_manifest.json"));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject groupObj = root["integrationGroupResults"].toArray().at(0).toObject();
    EXPECT_EQ(groupObj["outputPath"].toString(), QStringLiteral("groups/Group1/stacked_Group1.fits"));
    EXPECT_EQ(groupObj["previewPath"].toString(), QStringLiteral("groups/Group1/preview.png"));
}

TEST(WorkflowArchiver, CleanupOrphanedUuidStagedFileRestoresMissingOriginal)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    const QString originalPath = QDir(outputDir).filePath(QStringLiteral("stacked_Group1.fits"));
    const QString stagedPath = uuidStagedPathForTest(originalPath);

    QFile staged(stagedPath);
    ASSERT_TRUE(staged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    staged.write("Old Content");
    staged.close();

    const int processed = WorkflowArchiver::cleanupOrphanedStagedFiles(outputDir);

    EXPECT_EQ(processed, 1);
    EXPECT_FALSE(QFile::exists(stagedPath));
    ASSERT_TRUE(QFile::exists(originalPath));

    QFile restored(originalPath);
    ASSERT_TRUE(restored.open(QIODevice::ReadOnly));
    EXPECT_EQ(restored.readAll(), "Old Content");
}

TEST(WorkflowArchiver, CleanupOrphanedUuidStagedFileRemovesWhenOriginalExists)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    const QString originalPath = QDir(outputDir).filePath(QStringLiteral("stacked_Group1.fits"));
    const QString stagedPath = uuidStagedPathForTest(originalPath);

    QFile original(originalPath);
    ASSERT_TRUE(original.open(QIODevice::WriteOnly | QIODevice::Truncate));
    original.write("Current Content");
    original.close();

    QFile staged(stagedPath);
    ASSERT_TRUE(staged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    staged.write("Old Content");
    staged.close();

    const int processed = WorkflowArchiver::cleanupOrphanedStagedFiles(outputDir);

    EXPECT_EQ(processed, 1);
    EXPECT_FALSE(QFile::exists(stagedPath));
    ASSERT_TRUE(QFile::exists(originalPath));

    QFile current(originalPath);
    ASSERT_TRUE(current.open(QIODevice::ReadOnly));
    EXPECT_EQ(current.readAll(), "Current Content");
}

TEST(WorkflowArchiver, RevertsStagedFiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    QString outputDir = tempDir.path();

    WorkflowExecutor::Result result = createTestResult(outputDir);

    QString originalPath = QDir(outputDir).filePath("stacked_Group1.fits");
    QString stagedPath = uuidStagedPathForTest(originalPath);

    // Create staged file (old)
    {
        QFile f(stagedPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("Old Content");
        f.close();
    }

    // Create "new" file (which would exist if process partly succeeded, but we want to simulate failure reverting)
    // Actually, revert logic: if new file exists, delete it?
    // WorkflowArchiver::revertStagedFiles checks if staged exists.
    // If original exists (the new broken one), it removes it.
    // Then renames staged to original.

    {
        QFile f(originalPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("New Broken Content");
        f.close();
    }

    result.integrationGroupResults[0].stagedFiles.push_back({originalPath, stagedPath});

    WorkflowArchiver archiver(outputDir);
    archiver.revertStagedFiles(result);

    EXPECT_FALSE(QFile::exists(stagedPath));
    EXPECT_TRUE(QFile::exists(originalPath));

    QFile fCheck(originalPath);
    ASSERT_TRUE(fCheck.open(QIODevice::ReadOnly));
    EXPECT_EQ(fCheck.readAll(), "Old Content");
}

TEST(WorkflowArchiver, DisambiguatesGroupDirectoriesWhenSanitizedNamesCollide)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result;
    result.success = true;

    IntegrationGroupResult groupA;
    groupA.groupKey = QStringLiteral("Group/A");
    groupA.success = true;
    groupA.outputPath = QDir(outputDir).filePath(QStringLiteral("stacked_A.fits"));
    groupA.outputFilename = QStringLiteral("stacked_A.fits");
    result.integrationGroupResults.push_back(groupA);

    IntegrationGroupResult groupB;
    groupB.groupKey = QStringLiteral("Group|A");
    groupB.success = true;
    groupB.outputPath = QDir(outputDir).filePath(QStringLiteral("stacked_B.fits"));
    groupB.outputFilename = QStringLiteral("stacked_B.fits");
    result.integrationGroupResults.push_back(groupB);

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QDir groupsDir(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups")));
    const QStringList groupDirs = groupsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    EXPECT_EQ(groupDirs.size(), 2);
    EXPECT_NE(groupDirs[0], groupDirs[1]);
}

TEST(WorkflowArchiver, CleansUpOldArchives)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    QString outputDir = tempDir.path();
    QDir historyDir(QDir(outputDir).filePath("history"));
    historyDir.mkpath(".");

    // Create more than kRunHistoryMaxEntries (20) dummy directories
    // We need them to have timestamps.
    // WorkflowArchiver sorts by QDir::Time (modification time).
    // We'll verify it deletes the oldest.

    // Config::kRunHistoryMaxEntries is 20.
    int createCount = 25;
    for (int i = 0; i < createCount; ++i)
    {
        QString name = QString("run_%1").arg(i, 3, 10, QChar('0'));
        historyDir.mkdir(name);
        // Sleep to ensure diff mod times? Or just rely on creation order?
        // Mod time resolution might be low.
        // QDir::Time sorts by modification time.
        // Let's explicitly set mod times if possible, or just sleep a tiny bit.
        // On Windows mod time is usually decent.
        // But creating 25 dirs is fast.
        // Let's create them.
        QThread::msleep(10);
    }

    // Create a real archive which triggers cleanup
    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    WorkflowArchiver archiver(outputDir);
    archiver.commit(manifest, result);

    // Now we should have 20 (max) + 1 (just created) ?
    // No, commit calls cleanup AFTER creating the new one.
    // So we should have exactly kRunHistoryMaxEntries = 20.

    QFileInfoList entries = historyDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    EXPECT_LE(entries.size(), ImageIntegration::Config::kRunHistoryMaxEntries);

    // Ensure the new one exists
    EXPECT_TRUE(historyDir.exists(manifest.executionId));
}

TEST(WorkflowArchiver, CleansUpOldArchivesByMaxBytes)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    const QDir historyDir(QDir(outputDir).filePath("history"));
    ASSERT_TRUE(QDir().mkpath(historyDir.path()));

    const QString oldRun1 = historyDir.filePath("old_run_1");
    const QString oldRun2 = historyDir.filePath("old_run_2");
    ASSERT_TRUE(QDir().mkpath(oldRun1));
    writeSizedFile(QDir(oldRun1).filePath("payload.bin"), 200000);
    QThread::msleep(15);
    ASSERT_TRUE(QDir().mkpath(oldRun2));
    writeSizedFile(QDir(oldRun2).filePath("payload.bin"), 200000);

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);

    // Keep entry count generous to isolate byte-limit cleanup behavior.
    // Report archives now include both Markdown and HTML outputs which add
    // significant size (~20-50 KB), so use large payloads and a byte cap
    // that forces exactly one trim (the oldest) while keeping the rest.
    WorkflowArchiver archiver(outputDir, 100, 350000);
    const auto commitResult = archiver.commit(manifest, result);
    ASSERT_TRUE(commitResult.success);

    // old_run_1 is the oldest and should be trimmed first due to byte pressure.
    EXPECT_FALSE(QDir(oldRun1).exists());
    EXPECT_TRUE(QDir(oldRun2).exists());
    EXPECT_TRUE(QDir(historyDir.filePath(manifest.executionId)).exists());
}

TEST(WorkflowArchiver, EmbeddedManifestCanBeRoundTripped)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.executionScope = QStringLiteral("incremental");
    manifest.executionScopeKey = QStringLiteral("Ha_120s_G100");
    manifest.reprocessExisting = true;

    WorkflowManifest::IntegrationGroup integrationGroup;
    integrationGroup.groupKey = IntegrationGroupKey{GroupKey{"Ha_120s_G100"}};
    integrationGroup.outputFilename = QStringLiteral("stacked_Ha_120s_G100.fits");
    integrationGroup.lightFiles = {FrameId(QStringLiteral("light_001.fit")), FrameId(QStringLiteral("light_002.fit"))};
    integrationGroup.registrationGroupKey = GroupKey{"4000x3000_NGC7000"};
    manifest.integrationGroups.push_back(integrationGroup);

    const WorkflowExecutor::Result result = createTestResult(outputDir);
    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QFile manifestFile(QDir(archiveResult.archivePath).filePath("workflow_manifest.json"));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    ASSERT_TRUE(root.contains("manifest"));

    const WorkflowManifest parsed = WorkflowManifest::fromJson(root["manifest"].toObject());
    EXPECT_EQ(parsed.executionId, manifest.executionId);
    EXPECT_EQ(parsed.executionScope, manifest.executionScope);
    EXPECT_EQ(parsed.executionScopeKey, manifest.executionScopeKey);
    ASSERT_EQ(parsed.integrationGroups.size(), 1);
    EXPECT_EQ(parsed.integrationGroups.front().outputFilename, integrationGroup.outputFilename);
    EXPECT_EQ(parsed.integrationGroups.front().registrationGroupKey.toString(),
              integrationGroup.registrationGroupKey.toString());
}

TEST(WorkflowArchiver, PersistsStructuredStackingQualityReport)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    ASSERT_FALSE(result.integrationGroupResults.empty());

    auto& group = result.integrationGroupResults.front();
    group.qualityReport.valid = true;
    group.qualityReport.inputFrameCount = 4;
    group.qualityReport.effectiveFrameCount = 3.5;
    group.qualityReport.theoreticalSnrGain = 2.0;
    group.qualityReport.actualSnrGain = std::sqrt(3.5);
    group.qualityReport.integrationEfficiency = 0.875;
    group.qualityReport.minCoverage = 2.0;
    group.qualityReport.medianCoverage = 3.5;
    group.qualityReport.maxCoverage = 4.0;
    group.qualityReport.coverageStddev = 0.25;
    group.qualityReport.hasRejectionStats = true;
    group.qualityReport.globalRejectionRate = 0.03;
    group.qualityReport.maxFrameRejectionRate = 0.08;
    group.qualityReport.hasWeightingStats = true;
    group.qualityReport.weightMin = 0.7;
    group.qualityReport.weightMax = 1.3;
    group.qualityReport.weightEfficiency = 0.91;
    group.qualityReport.hasSpatialRejectionClusters = true;
    group.qualityReport.spatialClusterCount = 2;
    group.qualityReport.spatialClusterPixels = 48;
    group.qualityReport.spatialLargestClusterPixels = 36;
    group.qualityReport.spatialClusterFraction = 0.046875;
    group.qualityReport.spatialLargestClusterFraction = 0.03515625;
    group.qualityReport.spatialClusters.push_back(
        {RejectionClusterMapKind::High, 0, 36, 10, 12, 21, 14, 15.5, 13.0, 0.45, 0.9, 16.2});
    group.qualityReport.spatialRestackRequested = true;
    group.qualityReport.spatialRestackApplied = true;
    group.qualityReport.spatialRestackInitialClusterCount = 2;
    group.qualityReport.spatialRestackFlaggedFrameThreshold = 0.025;
    group.qualityReport.spatialRestackExcludedFrameIndices = {3};
    group.qualityReport.spatialRestackExcludedFrameRates = {0.08};

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QFile manifestFile(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject groupObj = root[QStringLiteral("integrationGroupResults")].toArray().at(0).toObject();
    ASSERT_TRUE(groupObj.contains(QStringLiteral("qualityReport")));
    const QJsonObject qualityObj = groupObj[QStringLiteral("qualityReport")].toObject();
    EXPECT_TRUE(qualityObj[QStringLiteral("valid")].toBool());
    EXPECT_DOUBLE_EQ(qualityObj[QStringLiteral("effectiveFrameCount")].toDouble(), 3.5);
    EXPECT_DOUBLE_EQ(qualityObj[QStringLiteral("integrationEfficiency")].toDouble(), 0.875);
    EXPECT_DOUBLE_EQ(qualityObj[QStringLiteral("globalRejectionRate")].toDouble(), 0.03);
    EXPECT_DOUBLE_EQ(qualityObj[QStringLiteral("weightEfficiency")].toDouble(), 0.91);
    EXPECT_TRUE(qualityObj[QStringLiteral("hasSpatialRejectionClusters")].toBool());
    EXPECT_EQ(qualityObj[QStringLiteral("spatialClusterCount")].toInt(), 2);
    EXPECT_TRUE(qualityObj[QStringLiteral("spatialRestackApplied")].toBool());

    const StackQualityRecord reloadedReport = stackingQualityReportFromJson(qualityObj);
    EXPECT_TRUE(reloadedReport.valid);
    EXPECT_EQ(reloadedReport.inputFrameCount, 4u);
    EXPECT_DOUBLE_EQ(reloadedReport.effectiveFrameCount, 3.5);
    EXPECT_DOUBLE_EQ(reloadedReport.integrationEfficiency, 0.875);
    EXPECT_TRUE(reloadedReport.hasRejectionStats);
    EXPECT_DOUBLE_EQ(reloadedReport.globalRejectionRate, 0.03);
    EXPECT_TRUE(reloadedReport.hasWeightingStats);
    EXPECT_DOUBLE_EQ(reloadedReport.weightEfficiency, 0.91);
    EXPECT_TRUE(reloadedReport.hasSpatialRejectionClusters);
    EXPECT_EQ(reloadedReport.spatialClusterCount, 2u);
    ASSERT_EQ(reloadedReport.spatialClusters.size(), 1u);
    EXPECT_EQ(reloadedReport.spatialClusters.front().mapKind, RejectionClusterMapKind::High);
    EXPECT_TRUE(reloadedReport.spatialRestackApplied);
    ASSERT_EQ(reloadedReport.spatialRestackExcludedFrameIndices.size(), 1u);
    EXPECT_EQ(reloadedReport.spatialRestackExcludedFrameIndices.front(), 3u);

    EXPECT_FALSE(reloadedReport.hasLocalWeightingStats);

    QFile groupSummaryFile(
        QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/group_summary.json")));
    ASSERT_TRUE(groupSummaryFile.open(QIODevice::ReadOnly));
    const QJsonObject groupSummary = QJsonDocument::fromJson(groupSummaryFile.readAll()).object();
    ASSERT_TRUE(groupSummary.contains(QStringLiteral("qualityReport")));
    EXPECT_TRUE(groupSummary[QStringLiteral("qualityReport")].toObject()[QStringLiteral("valid")].toBool());
}

TEST(WorkflowArchiver, PersistsLocalWeightingFieldsInQualityReport)
{
    StackQualityRecord original;
    original.valid = true;
    original.inputFrameCount = 5;
    original.effectiveFrameCount = 4.8;
    original.hasLocalWeightingStats = true;
    original.localWeightWindowSize = 9;
    original.localWeightGridSpacing = 16;
    original.localWeightSnrExponent = 2.5;
    original.localWeightGridCount = 5;
    original.localWeightMean = 0.85;
    original.localWeightMin = 0.42;
    original.localWeightMax = 1.0;
    original.localWeightStddev = 0.12;

    const QJsonObject json = stackingQualityReportToJson(original);
    const StackQualityRecord restored = stackingQualityReportFromJson(json);

    EXPECT_TRUE(restored.hasLocalWeightingStats);
    EXPECT_EQ(restored.localWeightWindowSize, 9);
    EXPECT_EQ(restored.localWeightGridSpacing, 16);
    EXPECT_DOUBLE_EQ(restored.localWeightSnrExponent, 2.5);
    EXPECT_EQ(restored.localWeightGridCount, 5u);
    EXPECT_DOUBLE_EQ(restored.localWeightMean, 0.85);
    EXPECT_DOUBLE_EQ(restored.localWeightMin, 0.42);
    EXPECT_DOUBLE_EQ(restored.localWeightMax, 1.0);
    EXPECT_DOUBLE_EQ(restored.localWeightStddev, 0.12);
}

TEST(WorkflowArchiver, PersistsSpatialRejectionClusterFieldsInQualityReport)
{
    StackQualityRecord original;
    original.valid = true;
    original.hasSpatialRejectionClusters = true;
    original.spatialClusterCount = 3;
    original.spatialClusterPixels = 64;
    original.spatialLargestClusterPixels = 28;
    original.spatialClusterFraction = 0.125;
    original.spatialLargestClusterFraction = 0.0546875;
    original.spatialRejectedElements = 71;
    original.spatialRejectedElementFraction = 0.138671875;
    original.spatialClusters.push_back(
        {RejectionClusterMapKind::Merged, 0, 28, 4, 5, 10, 9, 7.1, 6.8, 0.22, 0.75, 6.16});
    original.spatialRestackRequested = true;
    original.spatialRestackApplied = false;
    original.spatialRestackInitialClusterCount = 3;
    original.spatialRestackFlaggedFrameThreshold = 0.031;
    original.spatialRestackExcludedFrameIndices = {1, 4};
    original.spatialRestackExcludedFrameRates = {0.041, 0.052};

    const QJsonObject json = stackingQualityReportToJson(original);
    const StackQualityRecord restored = stackingQualityReportFromJson(json);

    EXPECT_TRUE(restored.hasSpatialRejectionClusters);
    EXPECT_EQ(restored.spatialClusterCount, 3u);
    EXPECT_EQ(restored.spatialClusterPixels, 64u);
    EXPECT_EQ(restored.spatialLargestClusterPixels, 28u);
    EXPECT_DOUBLE_EQ(restored.spatialClusterFraction, 0.125);
    EXPECT_DOUBLE_EQ(restored.spatialLargestClusterFraction, 0.0546875);
    EXPECT_EQ(restored.spatialRejectedElements, 71u);
    EXPECT_DOUBLE_EQ(restored.spatialRejectedElementFraction, 0.138671875);
    ASSERT_EQ(restored.spatialClusters.size(), 1u);
    EXPECT_EQ(restored.spatialClusters.front().mapKind, RejectionClusterMapKind::Merged);
    EXPECT_TRUE(restored.spatialRestackRequested);
    EXPECT_FALSE(restored.spatialRestackApplied);
    EXPECT_EQ(restored.spatialRestackInitialClusterCount, 3u);
    EXPECT_DOUBLE_EQ(restored.spatialRestackFlaggedFrameThreshold, 0.031);
    ASSERT_EQ(restored.spatialRestackExcludedFrameIndices.size(), 2u);
    EXPECT_EQ(restored.spatialRestackExcludedFrameIndices[1], 4u);
    ASSERT_EQ(restored.spatialRestackExcludedFrameRates.size(), 2u);
    EXPECT_DOUBLE_EQ(restored.spatialRestackExcludedFrameRates[0], 0.041);
}

TEST(WorkflowArchiver, PersistsTheNormalizationTheStackAppliedInQualityReport)
{
    StackQualityRecord original;
    original.valid = true;
    original.normalization = QStringLiteral("The normalization stage's coefficients, reference light_002.fits");

    const StackQualityRecord restored = stackingQualityReportFromJson(stackingQualityReportToJson(original));
    EXPECT_EQ(restored.normalization, original.normalization);
    EXPECT_TRUE(stackingQualityReportFromJson(QJsonObject{{QStringLiteral("valid"), true}}).normalization.isEmpty())
        << "a run archived before the stack said it states nothing";
}

// Where each master's solution came from is archived with its group, so a report rebuilt from the archive prints the
// same row as the run's own; an archive written before it was kept states nothing.
TEST(WorkflowArchiver, PersistsWhereEachMastersSolutionCameFrom)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    ASSERT_FALSE(result.integrationGroupResults.empty());
    result.integrationGroupResults.front().masterSolution = MasterSolutionOutcome::Solved;
    result.integrationGroupResults.front().masterSolutionDetail = QStringLiteral("Solved by the archive's solver.");

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success) << archiveResult.error.toStdString();
    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject archiveRoot = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject group = archiveRoot[QStringLiteral("integrationGroupResults")].toArray().at(0).toObject();
    EXPECT_EQ(masterSolutionOutcomeFromString(group[QStringLiteral("masterSolution")].toString()),
              MasterSolutionOutcome::Solved);
    EXPECT_EQ(group[QStringLiteral("masterSolutionDetail")].toString(),
              QStringLiteral("Solved by the archive's solver."));
    const QString rebuilt =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, archiveRoot);
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("- **Master Solution**: Solved - Solved by the archive's solver.")))
        << "the rebuilt report lost the row";

    EXPECT_EQ(masterSolutionOutcomeFromString(QString()), MasterSolutionOutcome::NotRecorded);
}

TEST(WorkflowArchiver, PersistsQualityMapArtifacts)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    ASSERT_FALSE(result.integrationGroupResults.empty());

    const QString coveragePath = QDir(outputDir).filePath(QStringLiteral("stacked_Group1_coverage.fits"));
    const QString snrPath = QDir(outputDir).filePath(QStringLiteral("stacked_Group1_snrgain.fits"));
    writeSizedFile(coveragePath, 128);
    writeSizedFile(snrPath, 256);

    auto& group = result.integrationGroupResults.front();
    group.qualityMaps.push_back(QualityMapResult{QStringLiteral("Coverage Map"), coveragePath, {}});
    group.qualityMaps.push_back(QualityMapResult{QStringLiteral("SNR Gain Map"), snrPath, {}});

    WorkflowArchiver archiver(outputDir);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    QFile manifestFile(QDir(archiveResult.archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject groupObj = root[QStringLiteral("integrationGroupResults")].toArray().at(0).toObject();
    const QJsonArray qualityMaps = groupObj[QStringLiteral("qualityMaps")].toArray();
    ASSERT_EQ(qualityMaps.size(), 2);
    EXPECT_EQ(qualityMaps.at(0).toObject()[QStringLiteral("title")].toString(), QStringLiteral("Coverage Map"));
    EXPECT_EQ(qualityMaps.at(1).toObject()[QStringLiteral("path")].toString(), snrPath);

    QFile groupSummaryFile(
        QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/group_summary.json")));
    ASSERT_TRUE(groupSummaryFile.open(QIODevice::ReadOnly));
    const QJsonObject groupSummary = QJsonDocument::fromJson(groupSummaryFile.readAll()).object();
    ASSERT_TRUE(groupSummary.contains(QStringLiteral("qualityMaps")));
    EXPECT_EQ(groupSummary[QStringLiteral("qualityMaps")].toArray().size(), 2);
}

TEST(WorkflowArchiver, SuccessfulOutputCopyCountsPayloadOnceAndPersistsSnapshot)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    constexpr int kPayloadBytes = 4097;

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    writeSizedFile(result.integrationGroupResults.front().outputPath, kPayloadBytes);

    WorkflowExecutionCounterAccumulator counters;
    auto committed_artifact =
        FinalCommittedArtifact::fromFile(result.integrationGroupResults.front().outputPath, &counters);
    ASSERT_TRUE(committed_artifact.has_value());
    const QString committed_digest = committed_artifact->digest().sha256();
    result.integrationGroupResults.front().committedArtifact = std::move(*committed_artifact);
    result.integrationGroupResults.front().outputBytes = kPayloadBytes;
    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(kPayloadBytes));
    counters.addInputDecodeOperations(7);
    counters.addScientificOutputEncodeCompletions(2);
    counters.addInputPassesStarted(3);
    counters.addTilePassesStarted(1);
    counters.addStackLoaderRequests(12);
    counters.addRoiSubsetReads(9);
    counters.addFullFrameFallbackReads(3);
    counters.addLogicalDecodedPixelBytes(65536);
    counters.addCalibrationInvocations(4);
    counters.addCalibratedSpillFilesCreated(4);
    counters.addCalibratedSpillBytesWritten(32768);
    counters.addCalibratedSpillBytesRead(24576);
    counters.addCalibratedSpillRoiReads(9);
    counters.addCalibratedSpillCleanupSuccesses(2);
    counters.addCalibratedSpillCleanupFailures(1);
    counters.addDumpBytesRead(8192);
    counters.addDumpBytesWritten(4096);
    counters.addPixmapBytesRead(2048);
    counters.addPixmapBytesWritten(1024);
    WorkflowArchiver archiver(outputDir, &counters, ImageIntegration::ArchiveFileTransport::ordinaryCopyOnly());
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);
    EXPECT_EQ(counters.snapshot().bytesCopied(), static_cast<std::uint64_t>(kPayloadBytes));
    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(kPayloadBytes));
    EXPECT_EQ(result.integrationGroupResults.front().outputRetentionStatus, OutputRetentionStatus::RetainedInHistory);

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    ASSERT_TRUE(root.contains(QStringLiteral("executionCounters")))
        << QJsonDocument(root).toJson(QJsonDocument::Compact).constData();
    const QJsonObject archivedCounters = root.value(QStringLiteral("executionCounters")).toObject();
    EXPECT_EQ(archivedCounters.value(QStringLiteral("bytesHashed")).toString(), QString::number(kPayloadBytes));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("bytesCopied")).toString(), QString::number(kPayloadBytes));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("inputDecodeOperations")).toString(), QStringLiteral("7"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("scientificOutputEncodeCompletions")).toString(),
              QStringLiteral("2"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("stackLoaderRequests")).toString(), QStringLiteral("12"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("roiSubsetReads")).toString(), QStringLiteral("9"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("fullFrameFallbackReads")).toString(), QStringLiteral("3"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("logicalDecodedPixelBytes")).toString(), QStringLiteral("65536"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibrationInvocations")).toString(), QStringLiteral("4"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibratedSpillFilesCreated")).toString(), QStringLiteral("4"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibratedSpillBytesWritten")).toString(),
              QStringLiteral("32768"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibratedSpillBytesRead")).toString(), QStringLiteral("24576"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibratedSpillRoiReads")).toString(), QStringLiteral("9"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibratedSpillCleanupSuccesses")).toString(),
              QStringLiteral("2"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("calibratedSpillCleanupFailures")).toString(), QStringLiteral("1"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("dumpBytesRead")).toString(), QStringLiteral("8192"));
    EXPECT_EQ(archivedCounters.value(QStringLiteral("pixmapBytesWritten")).toString(), QStringLiteral("1024"));
    const QJsonArray archived_groups = root.value(QStringLiteral("integrationGroupResults")).toArray();
    ASSERT_EQ(archived_groups.size(), 1);
    const QJsonArray archived_files = archived_groups.at(0).toObject().value(QStringLiteral("archivedFiles")).toArray();
    ASSERT_EQ(archived_files.size(), 1);
    const QJsonObject archived_file = archived_files.at(0).toObject();
    EXPECT_EQ(archived_file.value(QStringLiteral("sha256")).toString(), committed_digest);
    EXPECT_EQ(archived_file.value(QStringLiteral("digestProvenance")).toString(),
              QStringLiteral("current_run_committed"));
    EXPECT_TRUE(
        QDateTime::fromString(archived_file.value(QStringLiteral("sourceModifiedAtUtc")).toString(), Qt::ISODateWithMs)
            .isValid());

    QFile reportFile(archiveResult.reportPath);
    ASSERT_TRUE(reportFile.open(QIODevice::ReadOnly));
    const QString archivedReport = QString::fromUtf8(reportFile.readAll());
    EXPECT_TRUE(archivedReport.contains(QStringLiteral("| Bytes copied | %1 |").arg(kPayloadBytes)));
    EXPECT_TRUE(archivedReport.contains(QStringLiteral("| Input decode operations | 7 |")));
    EXPECT_TRUE(archivedReport.contains(QStringLiteral("| Logical decoded pixel bytes | 65536 |")));
    EXPECT_TRUE(archivedReport.contains(QStringLiteral("| Calibrated spill cleanup failures | 1 |")));

    const QString rebuilt =
        ImageIntegration::Reporting::buildWorkflowReportMarkdownFromArchive(archiveResult.archivePath, root);
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("### Execution Counters")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Bytes copied | %1 |").arg(kPayloadBytes)));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Dense pixmap payload bytes written | 1024 |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Logical decoded pixel bytes | 65536 |")));
    EXPECT_TRUE(rebuilt.contains(QStringLiteral("| Calibrated spill cleanup failures | 1 |")));
}

TEST(WorkflowArchiver, StaleCommittedSourceIsNotFalselyRetainedOrCopied)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    constexpr int kPayloadBytes = 2048;

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, kPayloadBytes);

    WorkflowExecutionCounterAccumulator counters;
    auto committed_artifact = FinalCommittedArtifact::fromFile(group.outputPath, &counters);
    ASSERT_TRUE(committed_artifact.has_value());
    group.committedArtifact = std::move(*committed_artifact);
    group.outputBytes = kPayloadBytes;

    QFile source(group.outputPath);
    ASSERT_TRUE(source.open(QIODevice::ReadWrite));
    ASSERT_TRUE(
        source.setFileTime(QFileInfo(group.outputPath).lastModified().addSecs(10), QFileDevice::FileModificationTime));
    source.close();

    WorkflowArchiver archiver(outputDir, &counters);
    const auto archiveResult = archiver.commit(manifest, result);

    ASSERT_TRUE(archiveResult.success);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(archiveResult.itemsArchived, 0);
    EXPECT_EQ(counters.snapshot().bytesHashed(), static_cast<std::uint64_t>(kPayloadBytes));
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
    EXPECT_EQ(group.outputRetentionStatus, OutputRetentionStatus::NotRetainedByArchiveFailure);
    EXPECT_TRUE(group.outputRetentionReason.contains(QStringLiteral("changed"), Qt::CaseInsensitive));

    QFile manifestFile(archiveResult.manifestPath);
    ASSERT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject archived_group = root.value(QStringLiteral("integrationGroupResults")).toArray().at(0).toObject();
    EXPECT_TRUE(archived_group.value(QStringLiteral("archivedFiles")).toArray().isEmpty());
    EXPECT_EQ(archived_group.value(QStringLiteral("outputRetentionStatus")).toString(),
              QStringLiteral("not_retained_by_archive_failure"));
}

TEST(WorkflowArchiver, SkippedAndFailedCopiesDoNotIncrementBytesCopied)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    const QString sourcePath = QDir(outputDir).filePath(QStringLiteral("copy_source.bin"));
    writeSizedFile(sourcePath, 2048);

    WorkflowExecutionCounterAccumulator counters;
    const QString disabledDestination = QDir(outputDir).filePath(QStringLiteral("disabled_copy.bin"));
    ASSERT_TRUE(copyWorkflowFile(sourcePath, disabledDestination));
    EXPECT_EQ(QFileInfo(disabledDestination).size(), QFileInfo(sourcePath).size());
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);

    EXPECT_FALSE(copyWorkflowFile(QDir(outputDir).filePath(QStringLiteral("missing.bin")),
                                  QDir(outputDir).filePath(QStringLiteral("skipped.bin")), &counters));

    const QString blockedDestination = QDir(outputDir).filePath(QStringLiteral("blocked_destination"));
    ASSERT_TRUE(QDir().mkpath(blockedDestination));
    EXPECT_FALSE(copyWorkflowFile(sourcePath, blockedDestination, &counters));
    EXPECT_EQ(counters.snapshot().bytesCopied(), 0u);
}

} // namespace
