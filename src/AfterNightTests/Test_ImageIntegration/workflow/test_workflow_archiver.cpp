// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow archiver: each run's history directory, its manifest, report and log, the outputs it keeps and the ones
/// its retention policy does not, and the previews the report shows. A preview the run did not prepare is made only in
/// the engine session the archiver is given; without one the archiver writes none. The report is read back as the
/// model the archive stores of it, never as text.

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QBuffer>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
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
#include <vector>

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ImageIntegration/ImportSession.h"

#include "controls/ImageViewFiles.h"
#include "processes/ImageIntegration/workflow/WorkflowArchiver.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionCounters.h"
#include "processes/ImageIntegration/workflow/WorkflowFileOperations.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/reporting/ReportArchiveReader.h"
#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
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

/// The report model a run's archive stores; fails the case, and returns nothing, when the archive gives none.
[[nodiscard]] std::optional<ImageIntegration::Reporting::ReportModel> storedReportOf(const QString& archivePath)
{
    ImageIntegration::Reporting::ReportArchiveRead read =
        ImageIntegration::Reporting::ReportArchiveReader::read(archivePath);
    EXPECT_TRUE(read.model.has_value()) << read.why.toStdString();
    return std::move(read.model);
}

/// A text file of an archive; fails the case when it cannot be read.
[[nodiscard]] QString archivedText(const QString& archivePath, const QString& name)
{
    QFile file(QDir(archivePath).filePath(name));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << name.toStdString();
    return QString::fromUtf8(file.readAll());
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
    // The report is its model and the document written from it; no Markdown is written.
    EXPECT_TRUE(QFile::exists(QDir(res.archivePath).filePath("workflow_report.json")));
    EXPECT_TRUE(QFile::exists(QDir(res.archivePath).filePath("workflow_report.html")));
    EXPECT_FALSE(QFile::exists(QDir(res.archivePath).filePath("workflow_report.md")));
    EXPECT_EQ(QDir::cleanPath(res.reportPath), QDir::cleanPath(QDir(res.archivePath).filePath("workflow_report.html")));

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

TEST(WorkflowArchiver, TheStoredReportStatesTheAlignmentCorrectionsAndTheExecutionLog)
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

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->registrationGroups.size(), 1u);
    const ImageIntegration::Reporting::ReportRegistrationGroup& group = stored->registrationGroups.front();
    EXPECT_EQ(group.aligned, std::optional<int>(4));
    EXPECT_EQ(group.distortionCorrected, std::optional<int>(3));
    EXPECT_EQ(group.chromaticAberrationCorrected, std::optional<int>(2));
    EXPECT_EQ(group.chromaticAberrationPassedThrough, std::optional<int>(2));
    EXPECT_TRUE(stored->run.log.contains(QStringLiteral("archive-rebuild-log-sentinel")));
    EXPECT_TRUE(archivedText(archiveResult.archivePath, QStringLiteral("workflow_report.html"))
                    .contains(QStringLiteral("archive-rebuild-log-sentinel")));
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

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->masters.size(), 1u);
    EXPECT_EQ(stored->masters.front().retention, ImageIntegration::Reporting::ReportRetention::NotRetainedByPolicy);
    EXPECT_FALSE(stored->masters.front().retentionReason.isEmpty());
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

    // The run's payload, its report and the newer run fit the history together; the oldest run does not as well.
    const QString oldestRun = historyDir.filePath(QStringLiteral("oldest_run"));
    ASSERT_TRUE(QDir().mkpath(oldestRun));
    writeSizedFile(QDir(oldestRun).filePath(QStringLiteral("payload.bin")), 3600000);
    QThread::msleep(15);
    const QString newerRun = historyDir.filePath(QStringLiteral("newer_run"));
    ASSERT_TRUE(QDir().mkpath(newerRun));
    writeSizedFile(QDir(newerRun).filePath(QStringLiteral("payload.bin")), 3600000);

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    writeSizedFile(result.integrationGroupResults.front().outputPath, 3000000);

    WorkflowExecutionCounterAccumulator counters;
    WorkflowArchiver archiver(outputDir, 10, 10000000, &counters);
    const auto archiveResult = archiver.commit(manifest, result);
    ASSERT_TRUE(archiveResult.success);

    EXPECT_FALSE(QDir(oldestRun).exists());
    EXPECT_TRUE(QDir(newerRun).exists());
    EXPECT_TRUE(
        QFile::exists(QDir(archiveResult.archivePath).filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));
    EXPECT_EQ(result.integrationGroupResults.front().outputRetentionStatus, OutputRetentionStatus::RetainedInHistory);
    EXPECT_EQ(archiveResult.itemsArchived, 1);
    EXPECT_EQ(counters.snapshot().bytesCopied(), 3000000u);
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

TEST(WorkflowArchiver, OutputRetentionStatusIsStatedInTheManifestAndInTheStoredReport)
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
    const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    EXPECT_EQ(root.value(QStringLiteral("overallResult")).toString(), QStringLiteral("success"));
    const QJsonObject groupJson = root.value(QStringLiteral("integrationGroupResults")).toArray().first().toObject();
    EXPECT_EQ(outputRetentionStatusFromString(groupJson.value(QStringLiteral("outputRetentionStatus")).toString()),
              OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_EQ(outputRetentionStatusFromString(QString()), OutputRetentionStatus::Unknown);

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->masters.size(), 1u);
    EXPECT_EQ(stored->masters.front().retention, ImageIntegration::Reporting::ReportRetention::NotRetainedByPolicy);
}

TEST(WorkflowArchiver, ThePreparationAndExecutorTimingsAreStatedInTheManifestAndInTheStoredReport)
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

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_TRUE(stored->run.preparation.has_value());
    EXPECT_EQ(stored->run.preparation->manifestBuildMs, 131);
    EXPECT_EQ(stored->run.preparation->planAndValidationMs, 269);
    ASSERT_TRUE(stored->run.executor.has_value());
    EXPECT_EQ(stored->run.executor->stageExecutionMs, 700);
    EXPECT_EQ(stored->run.executor->stateCommitMs, 100);
    EXPECT_EQ(stored->run.executor->archiveMs, 300);
    EXPECT_EQ(stored->run.executor->reportMs, 134);
    EXPECT_EQ(stored->run.processingMs, result.totalDurationMs);

    // A run that timed neither states neither.
    WorkflowManifest untimedManifest = createTestManifest(outputDir);
    untimedManifest.executionId = QStringLiteral("TEST_EXEC_UNTIMED");
    WorkflowArchiver untimedArchiver(outputDir);
    const auto untimed = untimedArchiver.commit(untimedManifest, createTestResult(outputDir));
    ASSERT_TRUE(untimed.success);
    const std::optional<ImageIntegration::Reporting::ReportModel> untimedStored = storedReportOf(untimed.archivePath);
    ASSERT_TRUE(untimedStored.has_value());
    EXPECT_FALSE(untimedStored->run.preparation.has_value());
    EXPECT_FALSE(untimedStored->run.executor.has_value());
}

TEST(WorkflowArchiver, AMastersFinalizationPhasesAreStatedInTheManifestAndInTheStoredReport)
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

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->masters.size(), 1u);
    const std::vector<ImageIntegration::Reporting::ReportPhase>& stated = stored->masters.front().finalization;
    ASSERT_EQ(stated.size(), 3u);
    EXPECT_EQ(stated[0].name, QStringLiteral("Final Master Save"));
    EXPECT_EQ(stated[0].status, QStringLiteral("completed"));
    EXPECT_EQ(stated[0].durationMs, 110);
    EXPECT_EQ(stated[1].name, QStringLiteral("Requested Maps"));
    EXPECT_EQ(stated[1].status, QStringLiteral("skipped"));
    EXPECT_EQ(stated[1].durationMs, 0);
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
    EXPECT_FALSE(QFile::exists(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.json"))));
    EXPECT_FALSE(QFile::exists(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.html"))));
    EXPECT_FALSE(QFile::exists(QDir(res.archivePath).filePath(QStringLiteral("workflow_report.md"))));
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

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(res.archivePath);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->run.process, QStringLiteral("Image Integration Lite"));
    EXPECT_EQ(stored->run.profile, QStringLiteral("Balanced"));
    EXPECT_TRUE(archivedText(res.archivePath, QStringLiteral("workflow_report.html"))
                    .contains(QStringLiteral("Image Integration Lite")));
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

    const QString storedReport = archivedText(res.archivePath, QStringLiteral("workflow_report.json"));
    EXPECT_FALSE(storedReport.isEmpty());
    EXPECT_FALSE(storedReport.contains(QStringLiteral("archive-api-key")));
    const QString document = archivedText(res.archivePath, QStringLiteral("workflow_report.html"));
    EXPECT_FALSE(document.isEmpty());
    EXPECT_FALSE(document.contains(QStringLiteral("archive-api-key")));
}

// The report names a master's file where the run wrote it, in the output folder, and not the copy the history keeps;
// what it shows of a master that has no report picture is the thumbnail the archive holds.
TEST(WorkflowArchiver, TheReportNamesAMasterWhereTheRunWroteItAndShowsWhatTheArchiveHoldsOfIt)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    IntegrationGroupResult& group = result.integrationGroupResults.front();
    writeSizedFile(group.outputPath, 2048);
    const QString masterPath = group.outputPath;
    group.previewPath = QDir(outputDir).filePath(QStringLiteral("stacked_Group1.preview.png"));
    QImage thumbnail(16, 12, QImage::Format_RGB888);
    thumbnail.fill(QColor(30, 60, 90));
    ASSERT_TRUE(thumbnail.save(group.previewPath, "PNG"));

    WorkflowArchiver archiver(outputDir);
    const auto archived = archiver.commit(manifest, result);
    ASSERT_TRUE(archived.success) << archived.error.toStdString();
    ASSERT_EQ(result.integrationGroupResults.front().outputRetentionStatus, OutputRetentionStatus::RetainedInHistory);
    ASSERT_TRUE(
        QFile::exists(QDir(archived.archivePath).filePath(QStringLiteral("groups/Group1/stacked_Group1.fits"))));

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archived.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->masters.size(), 1u);
    const ImageIntegration::Reporting::ReportMaster& master = stored->masters.front();
    EXPECT_EQ(QDir::cleanPath(master.file), QDir::cleanPath(masterPath));
    EXPECT_EQ(master.retention, ImageIntegration::Reporting::ReportRetention::Retained);
    // No session and no prepared picture: the archived thumbnail stands in, by its place in the run's folder.
    EXPECT_EQ(master.preview, QStringLiteral("groups/Group1/preview.png"));
    EXPECT_TRUE(QFile::exists(QDir(archived.archivePath).filePath(master.preview)));
}

// The document an archive holds is the one its stored model writes: nothing is in the one that is not in the other.
TEST(WorkflowArchiver, TheArchivedDocumentIsTheOneWrittenFromTheStoredModel)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    result.capturedLog = QStringLiteral("a line of the run's log");

    WorkflowArchiver archiver(outputDir);
    const auto res = archiver.commit(manifest, result);
    ASSERT_TRUE(res.success);

    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(res.archivePath);
    ASSERT_TRUE(stored.has_value());
    const ImageIntegration::Reporting::ReportDocument written = ImageIntegration::Reporting::writeReportDocument(
        *stored, ImageIntegration::Reporting::ReportDocumentOptions{
                     .form = ImageIntegration::Reporting::ReportForm::Linked, .runDirectory = res.archivePath});
    EXPECT_TRUE(written.faults.isEmpty()) << written.faults.join(QLatin1Char('\n')).toStdString();
    const QString archived = archivedText(res.archivePath, QStringLiteral("workflow_report.html"));
    EXPECT_TRUE(archived.startsWith(QStringLiteral("<!DOCTYPE html>"), Qt::CaseInsensitive));
    EXPECT_TRUE(archived == written.html) << "the archived document is not the stored model's";
    EXPECT_FALSE(archived.contains(QStringLiteral("markdown-body")));
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
    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->masters.size(), 1u);
    EXPECT_EQ(stored->masters.front().astrometry, ImageIntegration::Reporting::ReportAstrometryOutcome::Solved);
    EXPECT_EQ(stored->masters.front().astrometryDetail, QStringLiteral("Solved by the archive's solver."));

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

    // The report states the diagnostics as they stood once the payload was copied: the final snapshot.
    const std::optional<ImageIntegration::Reporting::ReportModel> stored = storedReportOf(archiveResult.archivePath);
    ASSERT_TRUE(stored.has_value());
    const auto counter = [&stored](const char* key) -> std::optional<std::uint64_t> {
        for (const ImageIntegration::Reporting::ReportCounter& stated : stored->run.counters)
        {
            if (stated.key == QLatin1String(key))
            {
                return stated.value;
            }
        }
        return std::nullopt;
    };
    EXPECT_EQ(counter("bytesCopied"), std::optional<std::uint64_t>(static_cast<std::uint64_t>(kPayloadBytes)));
    EXPECT_EQ(counter("inputDecodeOperations"), std::optional<std::uint64_t>(7u));
    EXPECT_EQ(counter("logicalDecodedPixelBytes"), std::optional<std::uint64_t>(65536u));
    EXPECT_EQ(counter("calibratedSpillCleanupFailures"), std::optional<std::uint64_t>(1u));
    EXPECT_EQ(counter("pixmapBytesWritten"), std::optional<std::uint64_t>(1024u));
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

// ---- What a run records for its report: the detail, the calibration previews, the lights a stack dropped and the
// groups the run reused.

/// A picture of one colour as the JPEG bytes a run prepares.
QByteArray preparedJpeg(const QSize& size, const QColor& colour)
{
    QImage picture(size, QImage::Format_RGB888);
    picture.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    EXPECT_TRUE(buffer.open(QIODevice::WriteOnly));
    EXPECT_TRUE(picture.save(&buffer, "JPEG", 90));
    return bytes;
}

QByteArray bytesOf(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject archivedManifestOf(const QString& archivePath)
{
    QFile file(QDir(archivePath).filePath(QStringLiteral("workflow_manifest.json")));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

TEST(WorkflowArchiver, ArchivesThePreparedDetailBesideTheWholeFrameAndNeverAlone)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowExecutor::Result result = createTestResult(outputDir);
    auto& group = result.integrationGroupResults.front();
    group.reportPreviewJpeg = preparedJpeg(QSize(32, 16), QColor(48, 96, 144));
    group.reportPreviewSize = QSize(32, 16);
    group.reportDetailJpeg = preparedJpeg(QSize(20, 10), QColor(144, 96, 48));
    group.reportDetailSize = QSize(20, 10);
    const QByteArray detailBytes = group.reportDetailJpeg;

    WorkflowArchiver archiver(outputDir);
    const auto archived = archiver.commit(manifest, result);
    ASSERT_TRUE(archived.success) << archived.error.toStdString();
    const QDir pictures(QDir(archived.archivePath).filePath(QStringLiteral("report_previews/Group1")));
    EXPECT_EQ(bytesOf(pictures.filePath(QStringLiteral("detail.jpg"))), detailBytes);
    EXPECT_EQ(QImage(pictures.filePath(QStringLiteral("detail.jpg"))).size(), QSize(20, 10));
    EXPECT_TRUE(result.integrationGroupResults.front().reportDetailJpeg.isEmpty())
        << "the bytes are let go of once they are written";

    // A detail with no whole frame beside it, and one larger than a detail is, are not written.
    WorkflowManifest alone = createTestManifest(outputDir);
    alone.executionId = QStringLiteral("TEST_EXEC_DETAIL_ALONE");
    WorkflowExecutor::Result aloneResult = createTestResult(outputDir);
    aloneResult.integrationGroupResults.front().reportDetailJpeg = detailBytes;
    aloneResult.integrationGroupResults.front().reportDetailSize = QSize(20, 10);
    WorkflowArchiver aloneArchiver(outputDir);
    const auto aloneArchived = aloneArchiver.commit(alone, aloneResult);
    ASSERT_TRUE(aloneArchived.success) << aloneArchived.error.toStdString();
    EXPECT_FALSE(
        QFile::exists(QDir(aloneArchived.archivePath).filePath(QStringLiteral("report_previews/Group1/detail.jpg"))));

    WorkflowManifest oversized = createTestManifest(outputDir);
    oversized.executionId = QStringLiteral("TEST_EXEC_DETAIL_OVERSIZED");
    WorkflowExecutor::Result oversizedResult = createTestResult(outputDir);
    auto& oversizedGroup = oversizedResult.integrationGroupResults.front();
    oversizedGroup.reportPreviewJpeg = preparedJpeg(QSize(32, 16), QColor(48, 96, 144));
    oversizedGroup.reportPreviewSize = QSize(32, 16);
    oversizedGroup.reportDetailJpeg = preparedJpeg(QSize(1200, 540), QColor(1, 2, 3));
    oversizedGroup.reportDetailSize = QSize(1200, 540);
    WorkflowArchiver oversizedArchiver(outputDir);
    const auto oversizedArchived = oversizedArchiver.commit(oversized, oversizedResult);
    ASSERT_TRUE(oversizedArchived.success) << oversizedArchived.error.toStdString();
    EXPECT_TRUE(QFile::exists(
        QDir(oversizedArchived.archivePath).filePath(QStringLiteral("report_previews/Group1/master.jpg"))));
    EXPECT_FALSE(QFile::exists(
        QDir(oversizedArchived.archivePath).filePath(QStringLiteral("report_previews/Group1/detail.jpg"))));
}

TEST(WorkflowArchiver, AMasterReadForItsPreviewGivesItsDetailInTheSameRead)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    writeFitsMaster(*session, createTestResult(outputDir).integrationGroupResults.front().outputPath);

    const auto commitWith = [&](const an::consumer::EngineSession* lent, const QString& executionId) {
        WorkflowManifest manifest = createTestManifest(outputDir);
        manifest.executionId = executionId;
        WorkflowExecutor::Result result = createTestResult(outputDir);
        result.integrationGroupResults.front().outputBytes =
            QFileInfo(result.integrationGroupResults.front().outputPath).size();
        WorkflowArchiver archiver(outputDir);
        archiver.setEngineSession(lent);
        return archiver.commit(manifest, result);
    };

    const auto withoutSession = commitWith(nullptr, QStringLiteral("TEST_EXEC_DETAIL_NO_SESSION"));
    ASSERT_TRUE(withoutSession.success) << withoutSession.error.toStdString();
    EXPECT_FALSE(
        QFile::exists(QDir(withoutSession.archivePath).filePath(QStringLiteral("report_previews/Group1/detail.jpg"))));

    const auto withSession = commitWith(&*session, QStringLiteral("TEST_EXEC_DETAIL_SESSION"));
    ASSERT_TRUE(withSession.success) << withSession.error.toStdString();
    const QDir pictures(QDir(withSession.archivePath).filePath(QStringLiteral("report_previews/Group1")));
    const QImage whole(pictures.filePath(QStringLiteral("master.jpg")));
    const QImage detail(pictures.filePath(QStringLiteral("detail.jpg")));
    ASSERT_FALSE(whole.isNull());
    ASSERT_FALSE(detail.isNull());
    // The master is smaller than a detail's region, so it is its own detail.
    EXPECT_EQ(detail.size(), QSize(64, 64));
    EXPECT_EQ(whole.size(), QSize(64, 64));
}

TEST(WorkflowArchiver, EachCalibrationMasterIsPreviewedInTheSessionAndNoneWithoutOne)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();

    // A bias the run makes under its output directory, a flat the user supplied from elsewhere, and a dark whose
    // file is not there.
    const QString suppliedDir = QDir(outputDir).filePath(QStringLiteral("supplied"));
    ASSERT_TRUE(QDir().mkpath(suppliedDir));
    const QString biasPath = QDir(outputDir).filePath(QStringLiteral("master_bias.fits"));
    const QString flatPath = QDir(suppliedDir).filePath(QStringLiteral("master_flat.fits"));
    writeFitsMaster(*session, biasPath);
    writeFitsMaster(*session, flatPath);

    const auto manifestOf = [&](const QString& executionId) {
        WorkflowManifest manifest = createTestManifest(outputDir);
        manifest.executionId = executionId;
        WorkflowManifest::CalibrationStage bias;
        bias.frameType = QStringLiteral("Bias");
        WorkflowManifest::CalibrationStage::Group biasGroup;
        biasGroup.groupKey = GroupKey{QStringLiteral("BiasGroup")};
        biasGroup.outputFilename = QStringLiteral("master_bias.fits");
        bias.groups.push_back(biasGroup);
        manifest.biasStage = bias;
        WorkflowManifest::CalibrationStage dark;
        dark.frameType = QStringLiteral("Dark");
        WorkflowManifest::CalibrationStage::Group darkGroup;
        darkGroup.groupKey = GroupKey{QStringLiteral("DarkGroup")};
        darkGroup.outputFilename = QStringLiteral("master_dark.fits");
        dark.groups.push_back(darkGroup);
        manifest.darkStage = dark;
        WorkflowManifest::CalibrationStage flat;
        flat.frameType = QStringLiteral("Flat");
        WorkflowManifest::CalibrationStage::Group flatGroup;
        flatGroup.groupKey = GroupKey{QStringLiteral("FlatGroup")};
        flatGroup.outputFilename = QStringLiteral("unused_flat.fits");
        flatGroup.existingMaster = FrameId(flatPath);
        flat.groups.push_back(flatGroup);
        manifest.flatStage = flat;
        return manifest;
    };

    const WorkflowManifest named = manifestOf(QStringLiteral("TEST_EXEC_CALIBRATION_NAMES"));
    const std::vector<ImageIntegration::ArchivedCalibrationPreview> previews =
        ImageIntegration::archivedCalibrationPreviews(named);
    ASSERT_EQ(previews.size(), 3u);
    EXPECT_EQ(previews[0].type, QStringLiteral("Bias"));
    EXPECT_EQ(previews[0].fileName, QStringLiteral("master_bias.jpg"));
    EXPECT_EQ(QDir::cleanPath(previews[0].masterPath), QDir::cleanPath(biasPath));
    EXPECT_EQ(previews[1].fileName, QStringLiteral("master_dark.jpg"));
    EXPECT_EQ(previews[2].type, QStringLiteral("Flat"));
    EXPECT_EQ(previews[2].fileName, QStringLiteral("master_flat.jpg"));
    EXPECT_EQ(QDir::cleanPath(previews[2].masterPath), QDir::cleanPath(FrameId(flatPath).canonicalPath()));

    WorkflowExecutor::Result withResult = createTestResult(outputDir);
    WorkflowManifest withManifest = manifestOf(QStringLiteral("TEST_EXEC_CALIBRATION_SESSION"));
    WorkflowArchiver withArchiver(outputDir);
    withArchiver.setEngineSession(&*session);
    const auto with = withArchiver.commit(withManifest, withResult);
    ASSERT_TRUE(with.success) << with.error.toStdString();
    const QDir calibration(QDir(with.archivePath).filePath(QStringLiteral("report_previews/calibration")));
    EXPECT_EQ(QImage(calibration.filePath(QStringLiteral("master_bias.jpg"))).size(), QSize(64, 64));
    EXPECT_EQ(QImage(calibration.filePath(QStringLiteral("master_flat.jpg"))).size(), QSize(64, 64));
    EXPECT_FALSE(QFile::exists(calibration.filePath(QStringLiteral("master_dark.jpg"))))
        << "a master whose file is not there has no picture";

    WorkflowExecutor::Result withoutResult = createTestResult(outputDir);
    WorkflowManifest withoutManifest = manifestOf(QStringLiteral("TEST_EXEC_CALIBRATION_NO_SESSION"));
    WorkflowArchiver withoutArchiver(outputDir);
    const auto without = withoutArchiver.commit(withoutManifest, withoutResult);
    ASSERT_TRUE(without.success) << without.error.toStdString();
    EXPECT_FALSE(QDir(QDir(without.archivePath).filePath(QStringLiteral("report_previews/calibration"))).exists());

    // A registration-only run writes no report, and no picture a report alone would show.
    WorkflowExecutor::Result registrationResult = createTestResult(outputDir);
    WorkflowManifest registrationOnly = manifestOf(QStringLiteral("TEST_EXEC_CALIBRATION_REGISTRATION"));
    registrationOnly.executionMode = ExecutionMode::RegistrationOnly;
    WorkflowArchiver registrationArchiver(outputDir);
    registrationArchiver.setEngineSession(&*session);
    const auto registration = registrationArchiver.commit(registrationOnly, registrationResult);
    ASSERT_TRUE(registration.success) << registration.error.toStdString();
    EXPECT_FALSE(QDir(QDir(registration.archivePath).filePath(QStringLiteral("report_previews/calibration"))).exists());
}

TEST(WorkflowArchiver, TwoCalibrationMastersOfOneNameHaveAPreviewEach)
{
    WorkflowManifest manifest = createTestManifest(QStringLiteral("D:/Output"));
    WorkflowManifest::CalibrationStage dark;
    dark.frameType = QStringLiteral("Dark");
    for (const QString& folder : {QStringLiteral("D:/Library/60s"), QStringLiteral("D:/Library/120s")})
    {
        WorkflowManifest::CalibrationStage::Group group;
        group.groupKey = GroupKey{QStringLiteral("dark|%1").arg(folder)};
        group.existingMaster = FrameId(folder + QStringLiteral("/master_dark.fits"));
        dark.groups.push_back(group);
    }
    manifest.darkStage = dark;

    const std::vector<ImageIntegration::ArchivedCalibrationPreview> previews =
        ImageIntegration::archivedCalibrationPreviews(manifest);
    ASSERT_EQ(previews.size(), 2u);
    EXPECT_NE(previews[0].fileName, previews[1].fileName);
    EXPECT_TRUE(previews[0].fileName.startsWith(QStringLiteral("master_dark_")));
    EXPECT_TRUE(previews[0].fileName.endsWith(QStringLiteral(".jpg")));
    // The names are the same whenever they are asked for: the report finds the file the archive wrote.
    EXPECT_EQ(ImageIntegration::archivedCalibrationPreviews(manifest)[1].fileName, previews[1].fileName);
}

TEST(WorkflowArchiver, TheFoldersOfAnArchiveAreNamedOverItsResultsAndThenItsReusedGroups)
{
    EXPECT_EQ(ImageIntegration::archiveGroupFolderNames({QStringLiteral("Group1"), QStringLiteral("Group2")}),
              (std::vector<QString>{QStringLiteral("Group1"), QStringLiteral("Group2")}));
    // Two keys that give one file name take a hash each, as the archive has always told them apart.
    const std::vector<QString> colliding =
        ImageIntegration::archiveGroupFolderNames({QStringLiteral("Group:1"), QStringLiteral("Group?1")});
    ASSERT_EQ(colliding.size(), 2u);
    EXPECT_NE(colliding[0], colliding[1]);
}

TEST(WorkflowArchiver, AReusedMastersPicturesAreCopiedFromTheRunThatMadeItAndReadFromTheFileWhenThatRunIsGone)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    const QString masterPath = createTestResult(outputDir).integrationGroupResults.front().outputPath;
    writeFitsMaster(*session, masterPath);

    // The run that made the master, with the pictures it prepared.
    WorkflowManifest madeManifest = createTestManifest(outputDir);
    madeManifest.executionId = QStringLiteral("20260101_000000_001");
    WorkflowExecutor::Result madeResult = createTestResult(outputDir);
    auto& made = madeResult.integrationGroupResults.front();
    made.outputBytes = QFileInfo(masterPath).size();
    made.reportPreviewJpeg = preparedJpeg(QSize(32, 16), QColor(48, 96, 144));
    made.reportPreviewSize = QSize(32, 16);
    made.reportDetailJpeg = preparedJpeg(QSize(20, 10), QColor(144, 96, 48));
    made.reportDetailSize = QSize(20, 10);
    const QByteArray wholeBytes = made.reportPreviewJpeg;
    const QByteArray detailBytes = made.reportDetailJpeg;
    WorkflowArchiver madeArchiver(outputDir);
    const auto madeArchived = madeArchiver.commit(madeManifest, madeResult);
    ASSERT_TRUE(madeArchived.success) << madeArchived.error.toStdString();

    // A later run that reused it: no result for the group, the group named among the ones it reused.
    const auto reuse = [&](const QString& executionId, const an::consumer::EngineSession* lent) {
        WorkflowManifest manifest = createTestManifest(outputDir);
        manifest.executionId = executionId;
        WorkflowExecutor::Result result = createTestResult(outputDir);
        result.integrationGroupResults.clear();
        result.reusedIntegrationGroups.push_back(
            ReusedIntegrationGroup{.groupKey = QStringLiteral("Group1"), .outputPath = masterPath});
        WorkflowArchiver archiver(outputDir);
        archiver.setEngineSession(lent);
        return archiver.commit(manifest, result);
    };

    // Copied, with no session and so without reading the master: the pictures are the very bytes of that run's.
    const auto copied = reuse(QStringLiteral("20260102_000000_001"), nullptr);
    ASSERT_TRUE(copied.success) << copied.error.toStdString();
    const QDir copiedPictures(QDir(copied.archivePath).filePath(QStringLiteral("report_previews/Group1")));
    EXPECT_EQ(bytesOf(copiedPictures.filePath(QStringLiteral("master.jpg"))), wholeBytes);
    EXPECT_EQ(bytesOf(copiedPictures.filePath(QStringLiteral("detail.jpg"))), detailBytes);

    // The run that made it is gone from the history: the run in between reused the master too and made no pictures
    // of its own to copy, so they are read from the file, in the session.
    ASSERT_TRUE(QDir(madeArchived.archivePath).removeRecursively());
    const auto read = reuse(QStringLiteral("20260103_000000_001"), &*session);
    ASSERT_TRUE(read.success) << read.error.toStdString();
    const QDir readPictures(QDir(read.archivePath).filePath(QStringLiteral("report_previews/Group1")));
    EXPECT_EQ(QImage(readPictures.filePath(QStringLiteral("master.jpg"))).size(), QSize(64, 64));
    EXPECT_EQ(QImage(readPictures.filePath(QStringLiteral("detail.jpg"))).size(), QSize(64, 64));
    EXPECT_NE(bytesOf(readPictures.filePath(QStringLiteral("master.jpg"))), wholeBytes);

    // And without a session there is nothing to read the file in: the archive holds neither picture.
    const auto neither = reuse(QStringLiteral("20260104_000000_001"), nullptr);
    ASSERT_TRUE(neither.success) << neither.error.toStdString();
    EXPECT_FALSE(QDir(QDir(neither.archivePath).filePath(QStringLiteral("report_previews/Group1"))).exists());
}

TEST(WorkflowArchiver, AReusedMasterThatIsNotTheOneAnEarlierRunMadeTakesNoPictureFromThatRun)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    const QString masterPath = createTestResult(outputDir).integrationGroupResults.front().outputPath;
    writeFitsMaster(*session, masterPath);

    WorkflowManifest madeManifest = createTestManifest(outputDir);
    madeManifest.executionId = QStringLiteral("20260101_000000_001");
    WorkflowExecutor::Result madeResult = createTestResult(outputDir);
    auto& made = madeResult.integrationGroupResults.front();
    // The run recorded a master of another size than the one on disk now.
    made.outputBytes = QFileInfo(masterPath).size() + 2880;
    made.reportPreviewJpeg = preparedJpeg(QSize(32, 16), QColor(48, 96, 144));
    made.reportPreviewSize = QSize(32, 16);
    WorkflowArchiver madeArchiver(outputDir);
    ASSERT_TRUE(madeArchiver.commit(madeManifest, madeResult).success);

    WorkflowManifest manifest = createTestManifest(outputDir);
    manifest.executionId = QStringLiteral("20260102_000000_001");
    WorkflowExecutor::Result result = createTestResult(outputDir);
    result.integrationGroupResults.clear();
    result.reusedIntegrationGroups.push_back(
        ReusedIntegrationGroup{.groupKey = QStringLiteral("Group1"), .outputPath = masterPath});
    WorkflowArchiver archiver(outputDir);
    const auto archived = archiver.commit(manifest, result);
    ASSERT_TRUE(archived.success) << archived.error.toStdString();
    EXPECT_FALSE(
        QFile::exists(QDir(archived.archivePath).filePath(QStringLiteral("report_previews/Group1/master.jpg"))));
}

TEST(WorkflowArchiver, TheLightsLeftOutTheLightsDroppedAndTheGroupsReusedRoundTripThroughTheArchive)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();

    WorkflowManifest manifest = createTestManifest(outputDir);
    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("Group1")}};
    integration.outputFilename = QStringLiteral("stacked_Group1.fits");
    manifest.integrationGroups.push_back(integration);
    manifest.leftOutLights = {
        WorkflowManifest::LeftOutLight{.light = FrameId(QDir(outputDir).filePath(QStringLiteral("excluded.fit"))),
                                       .kind = WorkflowManifest::LeftOutLight::Kind::Excluded,
                                       .groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("GroupNotMade")}}},
        WorkflowManifest::LeftOutLight{.light = FrameId(QDir(outputDir).filePath(QStringLiteral("from_stack.fit"))),
                                       .kind = WorkflowManifest::LeftOutLight::Kind::StackExcluded,
                                       .groupKey = integration.groupKey}};

    WorkflowExecutor::Result result = createTestResult(outputDir);
    const QString droppedA = QDir(outputDir).filePath(QStringLiteral("dropped_a.fit"));
    const QString droppedB = QDir(outputDir).filePath(QStringLiteral("dropped_b.fit"));
    result.integrationGroupResults.front().droppedLights = {
        DroppedLight{.light = droppedA, .reason = DroppedLightReason::NoRegisteredOutput},
        DroppedLight{.light = droppedB, .reason = DroppedLightReason::NoCometAlignedImage}};
    result.reusedIntegrationGroups = {
        ReusedIntegrationGroup{.groupKey = QStringLiteral("Group2"),
                               .outputPath = QDir(outputDir).filePath(QStringLiteral("stacked_Group2.fits"))}};

    WorkflowArchiver archiver(outputDir);
    const auto archived = archiver.commit(manifest, result);
    ASSERT_TRUE(archived.success) << archived.error.toStdString();

    const QJsonObject root = archivedManifestOf(archived.archivePath);
    const WorkflowManifest readManifest = WorkflowManifest::fromJson(root.value(QStringLiteral("manifest")).toObject());

    ASSERT_EQ(readManifest.leftOutLights.size(), 2u);
    EXPECT_EQ(readManifest.leftOutLights[0].kind, WorkflowManifest::LeftOutLight::Kind::Excluded);
    EXPECT_EQ(readManifest.leftOutLights[0].groupKey.toString(), QStringLiteral("GroupNotMade"));
    EXPECT_EQ(readManifest.leftOutLights[1].kind, WorkflowManifest::LeftOutLight::Kind::StackExcluded);
    EXPECT_EQ(readManifest.leftOutLights[1].light, manifest.leftOutLights[1].light);

    const QJsonArray archivedGroups = root.value(QStringLiteral("integrationGroupResults")).toArray();
    ASSERT_EQ(archivedGroups.size(), 1);
    const std::vector<DroppedLight> dropped =
        droppedLightsFromJson(archivedGroups.first().toObject().value(QStringLiteral("droppedLights")).toArray());
    ASSERT_EQ(dropped.size(), 2u);
    EXPECT_EQ(dropped[0].light, droppedA);
    EXPECT_EQ(dropped[0].reason, DroppedLightReason::NoRegisteredOutput);
    EXPECT_EQ(dropped[1].light, droppedB);
    EXPECT_EQ(dropped[1].reason, DroppedLightReason::NoCometAlignedImage);

    const std::vector<ReusedIntegrationGroup> reused =
        reusedIntegrationGroupsFromJson(root.value(QStringLiteral("reusedIntegrationGroups")).toArray());
    ASSERT_EQ(reused.size(), 1u);
    EXPECT_EQ(reused.front().groupKey, QStringLiteral("Group2"));
    EXPECT_EQ(reused.front().outputPath, QDir(outputDir).filePath(QStringLiteral("stacked_Group2.fits")));

    // The group's own summary names the dropped lights too.
    QFile summary(QDir(archived.archivePath).filePath(QStringLiteral("groups/Group1/group_summary.json")));
    ASSERT_TRUE(summary.open(QIODevice::ReadOnly));
    EXPECT_EQ(
        QJsonDocument::fromJson(summary.readAll()).object().value(QStringLiteral("droppedLights")).toArray().size(), 2);

    // A run that dropped no light and reused no group states neither key, and each stored reason reads back as
    // itself; a word that names no reason names no light.
    WorkflowManifest plain = createTestManifest(outputDir);
    plain.executionId = QStringLiteral("TEST_EXEC_PLAIN_RECORDS");
    WorkflowExecutor::Result plainResult = createTestResult(outputDir);
    WorkflowArchiver plainArchiver(outputDir);
    const auto plainArchived = plainArchiver.commit(plain, plainResult);
    ASSERT_TRUE(plainArchived.success) << plainArchived.error.toStdString();
    const QJsonObject plainRoot = archivedManifestOf(plainArchived.archivePath);
    EXPECT_FALSE(plainRoot.contains(QStringLiteral("reusedIntegrationGroups")));
    EXPECT_FALSE(plainRoot.value(QStringLiteral("integrationGroupResults"))
                     .toArray()
                     .first()
                     .toObject()
                     .contains(QStringLiteral("droppedLights")));
    for (const DroppedLightReason reason :
         {DroppedLightReason::NoRegisteredOutput, DroppedLightReason::NoRegistrationTransform,
          DroppedLightReason::NoStarBaseProduct, DroppedLightReason::NoCometAlignedImage})
    {
        const std::optional<DroppedLightReason> back = droppedLightReasonFromString(droppedLightReasonToString(reason));
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, reason);
        EXPECT_FALSE(droppedLightReasonText(reason).isEmpty());
    }
    EXPECT_FALSE(droppedLightReasonFromString(QStringLiteral("for no reason")).has_value());
    EXPECT_TRUE(droppedLightsFromJson(QJsonArray{QJsonObject{{QStringLiteral("light"), QStringLiteral("a.fit")},
                                                             {QStringLiteral("reason"), QStringLiteral("unknown")}}})
                    .empty());
}

// The pictures a report shows are in the run's folder before its size is measured, so the decision whether the run's
// payloads fit the history counts them. The same run is committed with a session, which writes the pictures, and
// without one, which writes none: the least history the run fits in without them no longer holds it with them.
TEST(WorkflowArchiver, TheRetentionDecisionCountsThePicturesWrittenForTheReport)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    const QString outputDir = tempDir.path();
    an::Result<an::consumer::EngineSession> session = an::tests::integration::openImportSession();
    ASSERT_TRUE(session.hasValue()) << session.status().message();
    const QString masterPath = createTestResult(outputDir).integrationGroupResults.front().outputPath;
    writeFitsMaster(*session, masterPath);
    constexpr int kCalibrationMasters = 24;
    for (int index = 0; index < kCalibrationMasters; ++index)
    {
        writeFitsMaster(*session, QDir(outputDir).filePath(QStringLiteral("master_dark_%1.fits").arg(index)));
    }

    struct Committed
    {
        OutputRetentionStatus status = OutputRetentionStatus::Unknown;
        qint64 pictureBytes = 0;
    };
    int commits = 0;
    const auto commit = [&](const an::consumer::EngineSession* lent, const qint64 limit) {
        WorkflowManifest manifest = createTestManifest(outputDir);
        // Every run has a name of the same length, so the runs differ by their pictures alone.
        manifest.executionId = QStringLiteral("TEST_EXEC_RETENTION_%1").arg(commits++, 4, 10, QLatin1Char('0'));
        WorkflowManifest::CalibrationStage dark;
        dark.frameType = QStringLiteral("Dark");
        for (int index = 0; index < kCalibrationMasters; ++index)
        {
            WorkflowManifest::CalibrationStage::Group group;
            group.groupKey = GroupKey{QStringLiteral("DarkGroup%1").arg(index)};
            group.outputFilename = QStringLiteral("master_dark_%1.fits").arg(index);
            dark.groups.push_back(group);
        }
        manifest.darkStage = dark;
        WorkflowExecutor::Result result = createTestResult(outputDir);
        result.integrationGroupResults.front().outputBytes = QFileInfo(masterPath).size();
        WorkflowArchiver archiver(outputDir, 20, limit);
        archiver.setEngineSession(lent);
        const auto archived = archiver.commit(manifest, result);
        EXPECT_TRUE(archived.success) << archived.error.toStdString();
        Committed committed;
        committed.status = result.integrationGroupResults.front().outputRetentionStatus;
        QDirIterator pictures(QDir(archived.archivePath).filePath(QStringLiteral("report_previews")), QDir::Files,
                              QDirIterator::Subdirectories);
        while (pictures.hasNext())
        {
            pictures.next();
            committed.pictureBytes += pictures.fileInfo().size();
        }
        // The run is removed, so each commit is measured against an empty history.
        EXPECT_TRUE(QDir(archived.archivePath).removeRecursively());
        return committed;
    };

    constexpr qint64 kGenerous = 512ll * 1024 * 1024;
    const Committed withPictures = commit(&*session, kGenerous);
    ASSERT_EQ(withPictures.status, OutputRetentionStatus::RetainedInHistory);
    ASSERT_GT(withPictures.pictureBytes, 8 * 1024) << "the master's two pictures and the calibration masters'";
    ASSERT_EQ(commit(nullptr, kGenerous).pictureBytes, 0);

    // The least history the run fits in when it writes no picture, to within a few hundred bytes.
    qint64 low = 0;
    qint64 high = kGenerous;
    while (high - low > 256)
    {
        const qint64 middle = low + (high - low) / 2;
        (commit(nullptr, middle).status == OutputRetentionStatus::RetainedInHistory ? high : low) = middle;
    }
    ASSERT_EQ(commit(nullptr, high).status, OutputRetentionStatus::RetainedInHistory);

    // With its pictures the run needs that much more.
    EXPECT_EQ(commit(&*session, high).status, OutputRetentionStatus::NotRetainedByPolicy);
    EXPECT_EQ(commit(&*session, high + withPictures.pictureBytes + 4096).status,
              OutputRetentionStatus::RetainedInHistory);
}

} // namespace
