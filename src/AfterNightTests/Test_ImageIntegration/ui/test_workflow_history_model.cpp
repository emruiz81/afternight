// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryModel.h"

namespace
{

void createMockArchive(const QString& historyDir, const QString& execId, const QString& timestamp,
                       bool createExtras = true, const QString& groupKey = QStringLiteral("Group1"),
                       const QString& groupResult = QStringLiteral("success"))
{
    QDir runDir(QDir(historyDir).filePath(execId));
    runDir.mkpath(".");

    QJsonObject root;
    root["executionId"] = execId;
    root["archivedAtLocal"] = timestamp;
    root["executionMode"] = "Full";
    root["overallResult"] = "success";
    root["totalDurationMs"] = 1000;
    root["warningCount"] = 0;

    QJsonObject group;
    group["groupKey"] = groupKey;
    group["result"] = groupResult;

    QJsonArray groups;
    groups.append(group);
    root["integrationGroupResults"] = groups;

    QFile f(runDir.filePath("workflow_manifest.json"));
    (void)f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());
    f.close();

    if (createExtras)
    {
        QFile report(runDir.filePath("workflow_report.md"));
        (void)report.open(QIODevice::WriteOnly);
        report.close();

        QFile log(runDir.filePath("workflow_log.txt"));
        (void)log.open(QIODevice::WriteOnly);
        log.close();
    }
}

TEST(WorkflowHistoryModel, LoadsWorkflows)
{
    QTemporaryDir tempDir;
    QString historyDir = tempDir.path();

    createMockArchive(historyDir, "run1", "2023-01-01T10:00:00");
    createMockArchive(historyDir, "run2", "2023-01-02T10:00:00");

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    EXPECT_EQ(model.rowCount(), 2);

    // Sorts newest first
    QModelIndex idx0 = model.index(0, 0);
    EXPECT_EQ(idx0.data(WorkflowHistoryModel::ExecutionIdRole).toString(), "run2");
    EXPECT_FALSE(idx0.data(WorkflowHistoryModel::ReportPathRole).toString().isEmpty());
    EXPECT_FALSE(idx0.data(WorkflowHistoryModel::LogPathRole).toString().isEmpty());
    EXPECT_FALSE(idx0.data(WorkflowHistoryModel::RunDirPathRole).toString().isEmpty());
    EXPECT_FALSE(idx0.data(WorkflowHistoryModel::IsGroupItemRole).toBool());

    QModelIndex idx1 = model.index(1, 0);
    EXPECT_EQ(idx1.data(WorkflowHistoryModel::ExecutionIdRole).toString(), "run1");
}

TEST(WorkflowHistoryModel, LoadsGroups)
{
    QTemporaryDir tempDir;
    QString historyDir = tempDir.path();
    createMockArchive(historyDir, "run1", "2023-01-01T10:00:00");

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    QModelIndex parent = model.index(0, 0);
    EXPECT_EQ(model.rowCount(parent), 1);

    QModelIndex child = model.index(0, 0, parent);
    EXPECT_EQ(child.data(WorkflowHistoryModel::GroupKeyRole).toString(), "Group1");
    EXPECT_TRUE(child.data(WorkflowHistoryModel::IsGroupItemRole).toBool());
    EXPECT_EQ(model.parent(child), parent);
}

TEST(WorkflowHistoryModel, SupportsExpandCollapseStateRoles)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();

    createMockArchive(historyDir, "run1", "2023-01-01T10:00:00");
    createMockArchive(historyDir, "run2", "2023-01-02T10:00:00");

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    const QModelIndex newest = model.index(0, 0);
    const QModelIndex older = model.index(1, 0);
    ASSERT_TRUE(newest.isValid());
    ASSERT_TRUE(older.isValid());

    // Default behavior: newest expanded, older collapsed.
    EXPECT_TRUE(newest.data(WorkflowHistoryModel::IsExpandedRole).toBool());
    EXPECT_FALSE(older.data(WorkflowHistoryModel::IsExpandedRole).toBool());
    EXPECT_TRUE(newest.data(WorkflowHistoryModel::CanExpandRole).toBool());
    EXPECT_FALSE(newest.data(WorkflowHistoryModel::IsGroupItemRole).toBool());

    // Explicit toggle through model role mutation.
    EXPECT_TRUE(model.setData(older, true, WorkflowHistoryModel::IsExpandedRole));
    EXPECT_TRUE(older.data(WorkflowHistoryModel::IsExpandedRole).toBool());
}

TEST(WorkflowHistoryModel, PreservesExpandCollapseStateAcrossRefresh)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();

    createMockArchive(historyDir, "run1", "2023-01-01T10:00:00");
    createMockArchive(historyDir, "run2", "2023-01-02T10:00:00");

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    const QModelIndex newest = model.index(0, 0);
    ASSERT_TRUE(newest.isValid());
    ASSERT_EQ(newest.data(WorkflowHistoryModel::ExecutionIdRole).toString(), QStringLiteral("run2"));

    // Collapse newest run and ensure the state survives refresh.
    EXPECT_TRUE(model.setData(newest, false, WorkflowHistoryModel::IsExpandedRole));
    EXPECT_FALSE(newest.data(WorkflowHistoryModel::IsExpandedRole).toBool());

    model.refresh();

    const QModelIndex newestAfterRefresh = model.index(0, 0);
    ASSERT_TRUE(newestAfterRefresh.isValid());
    ASSERT_EQ(newestAfterRefresh.data(WorkflowHistoryModel::ExecutionIdRole).toString(), QStringLiteral("run2"));
    EXPECT_FALSE(newestAfterRefresh.data(WorkflowHistoryModel::IsExpandedRole).toBool());
}

TEST(WorkflowHistoryModel, DisplaysCompactGroupKeyWithoutHiddenPlaceholders)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();
    createMockArchive(historyDir, QStringLiteral("run1"), QStringLiteral("2023-01-01T10:00:00"), true,
                      QStringLiteral("3008x3004|-|-|-|Blue|M 83|-"));

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    const QModelIndex parent = model.index(0, 0);
    ASSERT_TRUE(parent.isValid());
    ASSERT_EQ(model.rowCount(parent), 1);

    const QModelIndex child = model.index(0, 0, parent);
    ASSERT_TRUE(child.isValid());
    EXPECT_EQ(child.data(WorkflowHistoryModel::GroupKeyRole).toString(), QStringLiteral("3008x3004|Blue|M 83"));
}

TEST(WorkflowHistoryModel, PrefersHtmlReportPathWhenAvailable)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();
    createMockArchive(historyDir, QStringLiteral("run1"), QStringLiteral("2023-01-01T10:00:00"), false);

    const QDir runDir(QDir(historyDir).filePath(QStringLiteral("run1")));
    QFile markdownReport(runDir.filePath(QStringLiteral("workflow_report.md")));
    ASSERT_TRUE(markdownReport.open(QIODevice::WriteOnly));
    markdownReport.close();

    QFile htmlReport(runDir.filePath(QStringLiteral("workflow_report.html")));
    ASSERT_TRUE(htmlReport.open(QIODevice::WriteOnly));
    htmlReport.close();

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    const QModelIndex idx = model.index(0, 0);
    ASSERT_TRUE(idx.isValid());
    const QString reportPath = idx.data(WorkflowHistoryModel::ReportPathRole).toString();
    EXPECT_TRUE(reportPath.endsWith(QStringLiteral("workflow_report.html")));
}

TEST(WorkflowHistoryModel, HidesExecutionsWithoutProducedMasterStacks)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();

    createMockArchive(historyDir, QStringLiteral("failed_run"), QStringLiteral("2023-01-01T10:00:00"), true,
                      QStringLiteral("GroupFailed"), QStringLiteral("failed"));
    createMockArchive(historyDir, QStringLiteral("success_run"), QStringLiteral("2023-01-02T10:00:00"), true,
                      QStringLiteral("GroupSuccess"), QStringLiteral("success"));

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    ASSERT_EQ(model.rowCount(), 1);
    const QModelIndex idx = model.index(0, 0);
    ASSERT_TRUE(idx.isValid());
    EXPECT_EQ(idx.data(WorkflowHistoryModel::ExecutionIdRole).toString(), QStringLiteral("success_run"));
}

TEST(WorkflowHistoryModel, RelativeGroupOutputPathsAreResolvedForComparison)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();

    const auto createRun = [&historyDir](const QString& execId, const QString& timestamp) {
        const QDir runDir(QDir(historyDir).filePath(execId));
        ASSERT_TRUE(QDir().mkpath(runDir.filePath(QStringLiteral("groups/Group1"))));

        const QString relativeOutput = QStringLiteral("groups/Group1/stacked_Group1.fit");
        QFile output(runDir.filePath(relativeOutput));
        ASSERT_TRUE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
        output.write("master");
        output.close();

        QJsonObject archived;
        archived.insert(QStringLiteral("filename"), QStringLiteral("stacked_Group1.fit"));
        archived.insert(QStringLiteral("sizeBytes"), output.size());

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), QStringLiteral("Group1"));
        group.insert(QStringLiteral("outputPath"), relativeOutput);
        group.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_Group1.fit"));
        group.insert(QStringLiteral("result"), QStringLiteral("success"));
        group.insert(QStringLiteral("archivedFiles"), QJsonArray{archived});

        QJsonObject root;
        root.insert(QStringLiteral("executionId"), execId);
        root.insert(QStringLiteral("archivedAtLocal"), timestamp);
        root.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
        root.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        root.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        QFile manifest(runDir.filePath(QStringLiteral("workflow_manifest.json")));
        ASSERT_TRUE(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.write(QJsonDocument(root).toJson());
        manifest.close();
    };

    createRun(QStringLiteral("run_old"), QStringLiteral("2025-01-01T10:00:00.000"));
    createRun(QStringLiteral("run_new"), QStringLiteral("2025-01-02T10:00:00.000"));

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    ASSERT_EQ(model.rowCount(), 2);
    const QModelIndex newestRun = model.index(0, 0);
    ASSERT_TRUE(newestRun.isValid());
    const QModelIndex newestGroup = model.index(0, 0, newestRun);
    ASSERT_TRUE(newestGroup.isValid());

    const QString outputPath = newestGroup.data(WorkflowHistoryModel::OutputPathRole).toString();
    EXPECT_TRUE(QFileInfo(outputPath).isAbsolute());
    EXPECT_TRUE(QFileInfo::exists(outputPath));
    EXPECT_TRUE(newestGroup.data(WorkflowHistoryModel::CanCompareMasterRole).toBool());
}

TEST(WorkflowHistoryModel, CombinedRgbGroupsCanCompareAcrossExecutions)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();
    const QString registrationKey = QStringLiteral("reg|32x32|RgbHistory");
    const QString rgbGroupKey = ImageIntegration::combinedRgbGroupKey(registrationKey);
    const QString rgbFilename = ImageIntegration::combinedRgbOutputFilename(registrationKey);

    const auto createRun = [&](const QString& execId, const QString& timestamp) {
        const QDir runDir(QDir(historyDir).filePath(execId));
        ASSERT_TRUE(QDir().mkpath(runDir.filePath(QStringLiteral("groups/rgb"))));

        const QString relativeOutput = QStringLiteral("groups/rgb/%1").arg(rgbFilename);
        QFile output(runDir.filePath(relativeOutput));
        ASSERT_TRUE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
        output.write("rgb-master");
        output.close();

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), rgbGroupKey);
        group.insert(QStringLiteral("outputPath"), relativeOutput);
        group.insert(QStringLiteral("outputFilename"), rgbFilename);
        group.insert(QStringLiteral("registrationGroupKey"), registrationKey);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QJsonObject root;
        root.insert(QStringLiteral("executionId"), execId);
        root.insert(QStringLiteral("archivedAtLocal"), timestamp);
        root.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
        root.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        root.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        QFile manifest(runDir.filePath(QStringLiteral("workflow_manifest.json")));
        ASSERT_TRUE(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.write(QJsonDocument(root).toJson());
        manifest.close();
    };

    createRun(QStringLiteral("run_old"), QStringLiteral("2025-01-01T10:00:00.000"));
    createRun(QStringLiteral("run_new"), QStringLiteral("2025-01-02T10:00:00.000"));

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    ASSERT_EQ(model.rowCount(), 2);
    const QModelIndex newestRun = model.index(0, 0);
    ASSERT_TRUE(newestRun.isValid());
    const QModelIndex newestGroup = model.index(0, 0, newestRun);
    ASSERT_TRUE(newestGroup.isValid());
    EXPECT_EQ(newestGroup.data(WorkflowHistoryModel::RawGroupKeyRole).toString(), rgbGroupKey);
    EXPECT_TRUE(newestGroup.data(WorkflowHistoryModel::CanCompareMasterRole).toBool());
}

TEST(WorkflowHistoryModel, CombinedRgbGroupsDoNotCompareAgainstMonoMasters)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();
    const QString registrationKey = QStringLiteral("reg|32x32|RgbHistory");
    const QString rgbGroupKey = ImageIntegration::combinedRgbGroupKey(registrationKey);
    const QString rgbFilename = ImageIntegration::combinedRgbOutputFilename(registrationKey);

    const auto createRun = [&](const QString& execId, const QString& timestamp, const QString& groupKey,
                               const QString& filename, const QString& payload) {
        const QDir runDir(QDir(historyDir).filePath(execId));
        ASSERT_TRUE(QDir().mkpath(runDir.filePath(QStringLiteral("groups/output"))));

        const QString relativeOutput = QStringLiteral("groups/output/%1").arg(filename);
        QFile output(runDir.filePath(relativeOutput));
        ASSERT_TRUE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
        output.write(payload.toUtf8());
        output.close();

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), groupKey);
        group.insert(QStringLiteral("outputPath"), relativeOutput);
        group.insert(QStringLiteral("outputFilename"), filename);
        group.insert(QStringLiteral("registrationGroupKey"), registrationKey);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QJsonObject root;
        root.insert(QStringLiteral("executionId"), execId);
        root.insert(QStringLiteral("archivedAtLocal"), timestamp);
        root.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
        root.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        root.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        QFile manifest(runDir.filePath(QStringLiteral("workflow_manifest.json")));
        ASSERT_TRUE(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.write(QJsonDocument(root).toJson());
        manifest.close();
    };

    createRun(QStringLiteral("run_old_mono"), QStringLiteral("2025-01-01T10:00:00.000"),
              QStringLiteral("32x32|-|-|-|Red|M83|-"), QStringLiteral("stacked_Red.fit"),
              QStringLiteral("mono-red-master"));
    createRun(QStringLiteral("run_new_rgb"), QStringLiteral("2025-01-02T10:00:00.000"), rgbGroupKey, rgbFilename,
              QStringLiteral("rgb-master"));

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    ASSERT_EQ(model.rowCount(), 2);
    const QModelIndex newestRun = model.index(0, 0);
    ASSERT_TRUE(newestRun.isValid());
    const QModelIndex newestGroup = model.index(0, 0, newestRun);
    ASSERT_TRUE(newestGroup.isValid());
    EXPECT_EQ(newestGroup.data(WorkflowHistoryModel::RawGroupKeyRole).toString(), rgbGroupKey);
    EXPECT_FALSE(newestGroup.data(WorkflowHistoryModel::CanCompareMasterRole).toBool());
}

TEST(WorkflowHistoryModel, GroupTooltipIncludesStackedMasterProcessingContext)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();
    const QString execId = QStringLiteral("context_run");
    const QDir runDir(QDir(historyDir).filePath(execId));
    ASSERT_TRUE(QDir().mkpath(runDir.absolutePath()));

    const QString groupKey = QStringLiteral("1000x1000|-|-|-|Blue|M83|-");
    const QString registrationKey = QStringLiteral("1000x1000|M83");

    QJsonObject stacking;
    stacking.insert(QStringLiteral("stackMethod"), QStringLiteral("Average"));
    stacking.insert(QStringLiteral("rejectionMethod"), QStringLiteral("Winsorized Sigma Clipping"));
    stacking.insert(QStringLiteral("sigmaLow"), 2.8);
    stacking.insert(QStringLiteral("sigmaHigh"), 3.4);
    stacking.insert(QStringLiteral("weighting"), QStringLiteral("PSF Signal Weight"));
    stacking.insert(QStringLiteral("normalizationMode"), 2);
    stacking.insert(QStringLiteral("globalNormalizationMethod"), 2);
    stacking.insert(QStringLiteral("scaleEstimator"), 1);

    QJsonObject localNormalization;
    localNormalization.insert(QStringLiteral("enabled"), true);
    localNormalization.insert(QStringLiteral("gridSize"), 8);
    localNormalization.insert(QStringLiteral("referenceStrategy"), QStringLiteral("Integrate Best Frames"));
    stacking.insert(QStringLiteral("localNormalization"), localNormalization);

    QJsonObject drizzle;
    drizzle.insert(QStringLiteral("enabled"), true);
    drizzle.insert(QStringLiteral("scale"), 2.0);
    drizzle.insert(QStringLiteral("pixelFraction"), 0.8);
    drizzle.insert(QStringLiteral("kernel"), QStringLiteral("gaussian"));

    QJsonObject integrationGroup;
    integrationGroup.insert(QStringLiteral("groupKey"), groupKey);
    integrationGroup.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_blue.fit"));
    integrationGroup.insert(QStringLiteral("resolution"), QStringLiteral("2000x2000"));
    integrationGroup.insert(QStringLiteral("filter"), QStringLiteral("Blue"));
    integrationGroup.insert(QStringLiteral("object"), QStringLiteral("M83"));
    integrationGroup.insert(QStringLiteral("stacking"), stacking);
    integrationGroup.insert(QStringLiteral("drizzle"), drizzle);
    integrationGroup.insert(QStringLiteral("registrationGroupKey"), registrationKey);

    QJsonObject registrationSettings;
    registrationSettings.insert(QStringLiteral("registrationMethod"), QStringLiteral("star_matching"));
    registrationSettings.insert(QStringLiteral("transformation"), QStringLiteral("Homography"));
    registrationSettings.insert(QStringLiteral("interpolation"), QStringLiteral("Lanczos4"));

    QJsonObject registrationGroup;
    registrationGroup.insert(QStringLiteral("groupKey"), registrationKey);
    registrationGroup.insert(QStringLiteral("settings"), registrationSettings);

    QJsonObject manifest;
    manifest.insert(QStringLiteral("integrationGroups"), QJsonArray{integrationGroup});
    manifest.insert(QStringLiteral("registrationGroups"), QJsonArray{registrationGroup});

    QJsonObject resultGroup;
    resultGroup.insert(QStringLiteral("groupKey"), groupKey);
    resultGroup.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_blue.fit"));
    resultGroup.insert(QStringLiteral("outputPath"), runDir.filePath(QStringLiteral("stacked_blue.fit")));
    resultGroup.insert(QStringLiteral("result"), QStringLiteral("success"));
    resultGroup.insert(QStringLiteral("lightFrameCount"), 12);

    QJsonObject root;
    root.insert(QStringLiteral("executionId"), execId);
    root.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-04T10:00:00.000"));
    root.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
    root.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
    root.insert(QStringLiteral("manifest"), manifest);
    root.insert(QStringLiteral("integrationGroupResults"), QJsonArray{resultGroup});

    QFile file(runDir.filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    const QModelIndex parent = model.index(0, 0);
    ASSERT_TRUE(parent.isValid());
    const QModelIndex child = model.index(0, 0, parent);
    ASSERT_TRUE(child.isValid());

    const QString tooltip = child.data(Qt::ToolTipRole).toString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Stacking")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Average")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Winsorized Sigma Clipping")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("PSF Signal Weight")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Drizzle")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("2")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("2000x2000")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Registration")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("star matching")) || tooltip.contains(QStringLiteral("star_matching")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Homography")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Lanczos4")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Normalization")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Local (Fine)")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("Additive+Scale")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("IKSS")));
}

TEST(WorkflowHistoryModel, MasterContextUsesPerImageDrizzleSettingsWithoutComparisonArrow)
{
    QTemporaryDir tempDir;
    const QString historyDir = tempDir.path();
    const QString execId = QStringLiteral("drizzle_context_run");
    const QDir runDir(QDir(historyDir).filePath(execId));
    ASSERT_TRUE(QDir().mkpath(runDir.absolutePath()));

    QJsonObject stacking;
    stacking.insert(QStringLiteral("stackMethod"), QStringLiteral("Average"));
    stacking.insert(QStringLiteral("rejectionMethod"), QStringLiteral("None"));
    stacking.insert(QStringLiteral("weighting"), QStringLiteral("Disabled"));

    QJsonObject enabledDrizzle;
    enabledDrizzle.insert(QStringLiteral("enabled"), true);
    enabledDrizzle.insert(QStringLiteral("scale"), 2.0);
    enabledDrizzle.insert(QStringLiteral("pixelFraction"), 0.8);
    enabledDrizzle.insert(QStringLiteral("kernel"), QStringLiteral("Square"));

    QJsonObject enabledGroup;
    enabledGroup.insert(QStringLiteral("groupKey"), QStringLiteral("enabled"));
    enabledGroup.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_blue_drizzle.fit"));
    enabledGroup.insert(QStringLiteral("resolution"), QStringLiteral("6016x6008"));
    enabledGroup.insert(QStringLiteral("filter"), QStringLiteral("Blue"));
    enabledGroup.insert(QStringLiteral("stacking"), stacking);
    enabledGroup.insert(QStringLiteral("drizzle"), enabledDrizzle);

    QJsonObject disabledDrizzle;
    disabledDrizzle.insert(QStringLiteral("enabled"), false);
    disabledDrizzle.insert(QStringLiteral("scale"), 2.0);
    disabledDrizzle.insert(QStringLiteral("pixelFraction"), 0.8);
    disabledDrizzle.insert(QStringLiteral("kernel"), QStringLiteral("Square"));

    QJsonObject disabledGroup;
    disabledGroup.insert(QStringLiteral("groupKey"), QStringLiteral("disabled"));
    disabledGroup.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_blue.fit"));
    disabledGroup.insert(QStringLiteral("resolution"), QStringLiteral("3008x3004"));
    disabledGroup.insert(QStringLiteral("filter"), QStringLiteral("Blue"));
    disabledGroup.insert(QStringLiteral("stacking"), stacking);
    disabledGroup.insert(QStringLiteral("drizzle"), disabledDrizzle);

    QJsonObject enabledResult;
    enabledResult.insert(QStringLiteral("groupKey"), QStringLiteral("enabled"));
    enabledResult.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_blue_drizzle.fit"));
    enabledResult.insert(QStringLiteral("outputPath"), runDir.filePath(QStringLiteral("stacked_blue_drizzle.fit")));
    enabledResult.insert(QStringLiteral("result"), QStringLiteral("success"));

    QJsonObject disabledResult;
    disabledResult.insert(QStringLiteral("groupKey"), QStringLiteral("disabled"));
    disabledResult.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_blue.fit"));
    disabledResult.insert(QStringLiteral("outputPath"), runDir.filePath(QStringLiteral("stacked_blue.fit")));
    disabledResult.insert(QStringLiteral("result"), QStringLiteral("success"));

    QJsonObject manifest;
    manifest.insert(QStringLiteral("integrationGroups"), QJsonArray{enabledGroup, disabledGroup});

    QJsonObject root;
    root.insert(QStringLiteral("executionId"), execId);
    root.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-04T10:00:00.000"));
    root.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
    root.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
    root.insert(QStringLiteral("manifest"), manifest);
    root.insert(QStringLiteral("integrationGroupResults"), QJsonArray{enabledResult, disabledResult});

    QFile file(runDir.filePath(QStringLiteral("workflow_manifest.json")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();

    WorkflowHistoryModel model;
    model.setHistoryDirectory(historyDir);

    const QModelIndex parent = model.index(0, 0);
    ASSERT_TRUE(parent.isValid());

    auto contextForOutput = [&model, &parent](const QString& filename) {
        for (int row = 0; row < model.rowCount(parent); ++row)
        {
            const QModelIndex child = model.index(row, 0, parent);
            if (child.data(WorkflowHistoryModel::OutputFilenameRole).toString() == filename)
            {
                return child.data(WorkflowHistoryModel::MasterContextRole).toString();
            }
        }
        return QString();
    };

    const QString enabledContext = contextForOutput(QStringLiteral("stacked_blue_drizzle.fit"));
    ASSERT_FALSE(enabledContext.isEmpty());
    EXPECT_TRUE(enabledContext.contains(QStringLiteral("Drizzle: Enabled x2")));
    EXPECT_TRUE(enabledContext.contains(QStringLiteral("Resolution: 6016x6008")));
    EXPECT_TRUE(enabledContext.contains(QStringLiteral("Square")));
    EXPECT_TRUE(enabledContext.contains(QStringLiteral("pixfrac 0.8")));
    EXPECT_FALSE(enabledContext.contains(QStringLiteral("->")));

    const QString disabledContext = contextForOutput(QStringLiteral("stacked_blue.fit"));
    ASSERT_FALSE(disabledContext.isEmpty());
    EXPECT_TRUE(disabledContext.contains(QStringLiteral("Drizzle: Disabled")));
    EXPECT_TRUE(disabledContext.contains(QStringLiteral("Resolution: 3008x3004")));
    EXPECT_FALSE(disabledContext.contains(QStringLiteral("Square")));
    EXPECT_FALSE(disabledContext.contains(QStringLiteral("pixfrac")));
    EXPECT_FALSE(disabledContext.contains(QStringLiteral("->")));
}

} // namespace
