// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <QTreeView>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryDelegate.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryModel.h"
#include "theme/AnDesignTokens.h"

#include "fixtures/common/IntegrationFrameFiles.h"
#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace
{
void waitForRefresh(SummaryTab& tab)
{
    QSignalSpy spy(&tab, &SummaryTab::refreshed);
    tab.refreshAll();
    ASSERT_TRUE(spy.wait(2000));
    QCoreApplication::processEvents();
}

FrameInfo makeLight(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}

bool writeJson(const QString& path, const QJsonObject& obj)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return false;
    }
    const QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Compact));
    return true;
}

bool touchFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    file.write("test");
    return true;
}

bool writeSolidPreview(const QString& path, const QSize& size, const QColor& color)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image.save(path, "PNG");
}

/// Writes a master as a FITS file of `width` by `height` grey pixels, stating its exposure when given. The summary
/// reads only a master's size and exposure from its header; its samples are the fixture layer's constant level.
bool writeConstantImage(const QString& path, const std::uint32_t width, const std::uint32_t height,
                        const std::optional<double> exposureSeconds = std::nullopt)
{
    an::tests::fixtures::AcquisitionFacts facts;
    facts.exposureSeconds = exposureSeconds;
    const an::tests::fixtures::FixtureImage frame =
        an::tests::fixtures::acquisitionFrame(QFileInfo(path).completeBaseName().toStdString(), facts, width, height);
    std::string error;
    if (!an::tests::fixtures::writeAcquisitionFrame(frame, std::filesystem::path(path.toStdWString()), error))
    {
        ADD_FAILURE() << "Failed to write " << path.toStdString() << ": " << error;
        return false;
    }
    return QFileInfo::exists(path);
}

QPoint compareButtonCenter(const QTreeView& history, const QModelIndex& groupIndex)
{
    const QRect visRect = history.visualRect(groupIndex);
    const auto& tokens = an::ui::tokens();
    const int padding = tokens.spacing.sm;
    const int buttonWidth = tokens.spacing.xl * 3;
    const int compareOffset = padding + buttonWidth + padding;
    return QPoint(visRect.right() - compareOffset - buttonWidth / 2, visRect.top() + visRect.height() / 2);
}

QPoint workflowButtonCenter(const QTreeView& history, const QModelIndex& workflowIndex, const int buttonIndexFromRight)
{
    const QRect visRect = history.visualRect(workflowIndex);
    const auto& tokens = an::ui::tokens();
    const int padding = tokens.spacing.sm;
    const int buttonWidth = tokens.spacing.xl * 3;
    const int offset = padding + buttonIndexFromRight * (buttonWidth + padding);
    return QPoint(visRect.right() - offset - buttonWidth / 2, visRect.top() + visRect.height() / 2);
}

void finishNextMessageBox(const QString& title, const QMessageBox::StandardButton button)
{
    auto* timer = new QTimer(qApp);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, title, button]() {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (!box || !box->isVisible() || box->windowTitle() != title)
            {
                continue;
            }

            box->done(button);
            timer->stop();
            timer->deleteLater();
            return;
        }
    });
    QTimer::singleShot(5000, timer, [timer]() {
        if (timer->isActive())
        {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

QRect historyThumbnailRect(const QTreeView& history, const QModelIndex& groupIndex)
{
    const QRect visRect = history.visualRect(groupIndex);
    const auto& tokens = an::ui::tokens();
    const int padding = tokens.spacing.sm;
    const int thumbnailSize = tokens.spacing.xl * 2;
    return QRect(visRect.left() + padding, visRect.top() + padding, thumbnailSize, thumbnailSize);
}

QImage renderHistoryViewport(QTreeView& history)
{
    QImage image(history.viewport()->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    history.viewport()->render(&image);
    return image;
}

bool colorNear(const QColor& actual, const QColor& expected, const int tolerance = 4)
{
    return qAbs(actual.red() - expected.red()) <= tolerance && qAbs(actual.green() - expected.green()) <= tolerance &&
           qAbs(actual.blue() - expected.blue()) <= tolerance;
}

QJsonObject manifestGroup(const QString& groupKey, const QString& outputFilename, const QString& filter,
                          const QString& object, const QString& exposure,
                          const QString& resolution = QStringLiteral("1000x1000"),
                          const QString& registrationGroupKey = QStringLiteral("1000x1000|M83"))
{
    QJsonObject group;
    group.insert(QStringLiteral("groupKey"), groupKey);
    group.insert(QStringLiteral("outputFilename"), outputFilename);
    group.insert(QStringLiteral("resolution"), resolution);
    group.insert(QStringLiteral("filter"), filter);
    group.insert(QStringLiteral("object"), object);
    group.insert(QStringLiteral("exposure"), exposure);
    group.insert(QStringLiteral("registrationGroupKey"), registrationGroupKey);
    return group;
}

QJsonObject stackingSettings(const bool drizzleEnabled)
{
    QJsonObject stacking;
    stacking.insert(QStringLiteral("stackMethod"), QStringLiteral("Average"));
    stacking.insert(QStringLiteral("rejectionMethod"), QStringLiteral("GESDT"));
    stacking.insert(QStringLiteral("sigmaLow"), 0.3);
    stacking.insert(QStringLiteral("sigmaHigh"), 0.05);
    stacking.insert(QStringLiteral("weighting"), QStringLiteral("Weighted FWHM"));

    QJsonObject drizzle;
    drizzle.insert(QStringLiteral("enabled"), drizzleEnabled);
    drizzle.insert(QStringLiteral("scale"), 2.0);
    drizzle.insert(QStringLiteral("pixelFraction"), 0.8);
    drizzle.insert(QStringLiteral("kernel"), QStringLiteral("Square"));
    stacking.insert(QStringLiteral("drizzle"), drizzle);
    return stacking;
}

QJsonObject drizzleSettings()
{
    QJsonObject drizzle;
    drizzle.insert(QStringLiteral("enabled"), true);
    drizzle.insert(QStringLiteral("scale"), 2.0);
    drizzle.insert(QStringLiteral("pixelFraction"), 0.8);
    drizzle.insert(QStringLiteral("kernel"), QStringLiteral("Square"));
    return drizzle;
}
} // namespace

TEST(SummaryTabActions, RunHistoryActionsEmitIntentSignals)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    // Setup workflow history
    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const QString exec_id = "test-exec-actions-001";
    ASSERT_TRUE(QDir().mkpath(history_root.filePath(exec_id)));
    const QDir run_dir(history_root.filePath(exec_id));

    // Create workflow_manifest.json (v3)
    QJsonObject run_json;
    run_json.insert(QStringLiteral("schemaVersion"), 3);
    run_json.insert(QStringLiteral("executionId"), exec_id);
    run_json.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-01T12:00:00.000"));
    run_json.insert(QStringLiteral("overallResult"), "success");

    QJsonObject group;
    group["groupKey"] = "TestGroup";
    group["outputPath"] = QDir(temp_dir.path()).filePath("stacked.fits");
    group["result"] = "success";
    QJsonArray groups;
    groups.append(group);
    run_json.insert("integrationGroupResults", groups);

    ASSERT_TRUE(writeJson(run_dir.filePath("workflow_manifest.json"), run_json));

    // Create report file to enable report button
    QFile rep(run_dir.filePath("workflow_report.md"));
    (void)rep.open(QIODevice::WriteOnly);
    rep.close();

    // Create log file to enable log button
    QFile log(run_dir.filePath("workflow_log.txt"));
    (void)log.open(QIODevice::WriteOnly);
    log.close();

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    // Find the QTreeView (it replaced QListView)
    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* delegate = qobject_cast<WorkflowHistoryDelegate*>(history->itemDelegate());
    ASSERT_TRUE(delegate != nullptr);

    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);
    ASSERT_GT(histModel->rowCount(), 0);

    // Index 0 is the Workflow Header
    const QModelIndex idx = histModel->index(0, 0);

    QRect visRect = history->visualRect(idx);
    int right = visRect.right();
    int top = visRect.top();
    int h = visRect.height();
    int centerY = top + h / 2;
    const auto& tokens = an::ui::tokens();
    const int padding = tokens.spacing.sm;
    const int buttonWidth = tokens.spacing.xl * 3;
    const auto workflowButtonCenter = [&](const int button_index_from_right) {
        const int offset = padding + button_index_from_right * (buttonWidth + padding);
        return QPoint(right - offset - buttonWidth / 2, centerY);
    };

    QPoint openPos = workflowButtonCenter(0);
    QPoint reportPos = workflowButtonCenter(1);
    QPoint logPos = workflowButtonCenter(2);
    QPoint rerunPos = workflowButtonCenter(3);

    QSignalSpy openSpy(&tab, &SummaryTab::openRequested);
    QSignalSpy rerunSpy(&tab, &SummaryTab::rerunRequested);
    QSignalSpy reportSpy(&tab, &SummaryTab::exportReportRequested);

    // Let's create a dummy output file so openRequested is emitted
    QFile f(QDir(temp_dir.path()).filePath("stacked.fits"));
    (void)f.open(QIODevice::WriteOnly);
    f.close();

    // Click "Open All"
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, openPos);
    EXPECT_EQ(openSpy.count(), 1);

    // Click "Report" -> should emit an intent to regenerate/open the report from the archived run.
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, reportPos);
    ASSERT_EQ(reportSpy.count(), 1);
    ASSERT_EQ(reportSpy.at(0).size(), 1);
    EXPECT_EQ(reportSpy.at(0).at(0).toString(), run_dir.absolutePath());

    // Click "Re-run" -> should emit intent signal with run directory.
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, rerunPos);
    EXPECT_EQ(rerunSpy.count(), 1);
}

TEST(SummaryTabActions, DeleteWorkflowArchiveButtonRemovesExecutionFolderAfterConfirmation)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const QString exec_id = QStringLiteral("test-exec-delete-001");
    ASSERT_TRUE(QDir().mkpath(history_root.filePath(exec_id)));
    const QDir run_dir(history_root.filePath(exec_id));
    ASSERT_TRUE(QDir().mkpath(run_dir.filePath(QStringLiteral("groups/TestGroup/previews"))));

    QJsonObject run_json;
    run_json.insert(QStringLiteral("schemaVersion"), 3);
    run_json.insert(QStringLiteral("executionId"), exec_id);
    run_json.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-01T12:00:00.000"));
    run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));

    QJsonObject group;
    group.insert(QStringLiteral("groupKey"), QStringLiteral("TestGroup"));
    group.insert(QStringLiteral("outputPath"), run_dir.filePath(QStringLiteral("groups/TestGroup/stacked.fits")));
    group.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked.fits"));
    group.insert(QStringLiteral("result"), QStringLiteral("success"));
    QJsonArray groups;
    groups.append(group);
    run_json.insert(QStringLiteral("integrationGroupResults"), groups);

    ASSERT_TRUE(writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json));
    ASSERT_TRUE(touchFile(run_dir.filePath(QStringLiteral("workflow_report.md"))));
    ASSERT_TRUE(touchFile(run_dir.filePath(QStringLiteral("workflow_log.txt"))));
    ASSERT_TRUE(touchFile(run_dir.filePath(QStringLiteral("groups/TestGroup/stacked.fits"))));
    ASSERT_TRUE(touchFile(run_dir.filePath(QStringLiteral("groups/TestGroup/group_summary.json"))));
    ASSERT_TRUE(touchFile(run_dir.filePath(QStringLiteral("groups/TestGroup/previews/thumb.png"))));

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);
    ASSERT_EQ(histModel->rowCount(), 1);

    const QModelIndex workflowIndex = histModel->index(0, 0);
    ASSERT_TRUE(workflowIndex.isValid());
    ASSERT_FALSE(history->visualRect(workflowIndex).isEmpty());

    finishNextMessageBox(QStringLiteral("Delete Workflow Archive"), QMessageBox::Yes);
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier,
                      workflowButtonCenter(*history, workflowIndex, 4));

    QTRY_VERIFY(!QFileInfo::exists(run_dir.absolutePath()));
    QTRY_COMPARE(histModel->rowCount(), 0);
    EXPECT_TRUE(QFileInfo::exists(history_root.absolutePath()));
}

TEST(SummaryTabActions, RunHistoryThumbnailPreservesWidePreviewAspectRatio)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const QString exec_id = QStringLiteral("test-exec-wide-thumb-001");
    ASSERT_TRUE(QDir().mkpath(history_root.filePath(exec_id)));
    const QDir run_dir(history_root.filePath(exec_id));

    const QString group_key = QStringLiteral("Canon DSLR Wide");
    const QString output_path = QDir(temp_dir.path()).filePath(QStringLiteral("stacked_wide.fit"));
    ASSERT_TRUE(touchFile(output_path));

    const QString group_dir_name = ImageIntegration::sanitizeFilename(group_key);
    const QString preview_rel = QStringLiteral("groups/%1/preview.png").arg(group_dir_name);
    const QColor preview_color(220, 20, 40);
    ASSERT_TRUE(writeSolidPreview(run_dir.filePath(preview_rel), QSize(80, 40), preview_color));

    QJsonObject group;
    group.insert(QStringLiteral("groupKey"), group_key);
    group.insert(QStringLiteral("outputPath"), output_path);
    group.insert(QStringLiteral("outputFilename"), QStringLiteral("stacked_wide.fit"));
    group.insert(QStringLiteral("previewPath"), preview_rel);
    group.insert(QStringLiteral("result"), QStringLiteral("success"));

    QJsonObject run_json;
    run_json.insert(QStringLiteral("schemaVersion"), 3);
    run_json.insert(QStringLiteral("executionId"), exec_id);
    run_json.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-01T12:00:00.000"));
    run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
    run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});
    ASSERT_TRUE(writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json));

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_TRUE(groupIndex.data(WorkflowHistoryModel::IsGroupItemRole).toBool());
    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());

    const QRect thumbRect = historyThumbnailRect(*history, groupIndex);
    ASSERT_TRUE(history->viewport()->rect().contains(thumbRect.center()));

    QImage rendered;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        rendered = renderHistoryViewport(*history);
        if (colorNear(rendered.pixelColor(thumbRect.center()), preview_color))
        {
            break;
        }
        QTest::qWait(20);
        QCoreApplication::processEvents();
    }

    ASSERT_TRUE(colorNear(rendered.pixelColor(thumbRect.center()), preview_color));

    const QPoint topMargin(thumbRect.center().x(), thumbRect.top() + 2);
    const QPoint bottomMargin(thumbRect.center().x(), thumbRect.bottom() - 2);
    EXPECT_FALSE(colorNear(rendered.pixelColor(topMargin), preview_color));
    EXPECT_FALSE(colorNear(rendered.pixelColor(bottomMargin), preview_color));
}

TEST(SummaryTabActions, CompareMasterActionEmitsMatchingGroupOutputsAcrossExecutions)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));

    const auto createRun = [&](const QString& exec_id, const QString& timestamp, const QString& group_key,
                               const QString& output_name) -> QString {
        if (!QDir().mkpath(history_root.filePath(exec_id)))
        {
            ADD_FAILURE() << "Failed to create workflow history directory";
            return {};
        }
        const QDir run_dir(history_root.filePath(exec_id));

        const QString output_path = QDir(temp_dir.path()).filePath(output_name);
        QFile output(output_path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            ADD_FAILURE() << "Failed to create stacked output";
            return {};
        }
        output.close();

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), group_key);
        group.insert(QStringLiteral("outputPath"), output_path);
        group.insert(QStringLiteral("outputFilename"), output_name);
        group.insert(QStringLiteral("lightFrameCount"), exec_id == QStringLiteral("run-newest-l") ? 42 : 40);
        group.insert(QStringLiteral("durationMs"), exec_id == QStringLiteral("run-newest-l") ? 72000 : 61000);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        const QString filter = group_key.contains(QStringLiteral("Ha")) ? QStringLiteral("Ha") : QStringLiteral("L");
        QJsonObject manifest;
        manifest.insert(
            QStringLiteral("integrationGroups"),
            QJsonArray{manifestGroup(group_key, output_name, filter, QStringLiteral("M83"), QStringLiteral("300s"))});

        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("executionId"), exec_id);
        run_json.insert(QStringLiteral("archivedAtLocal"), timestamp);
        run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run_json.insert(QStringLiteral("totalDurationMs"), exec_id == QStringLiteral("run-newest-l") ? 75000 : 64000);
        run_json.insert(QStringLiteral("manifest"), manifest);
        run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        if (!writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json))
        {
            ADD_FAILURE() << "Failed to write workflow manifest";
            return {};
        }
        return output_path;
    };

    const QString newestL = createRun(QStringLiteral("run-newest-l"), QStringLiteral("2025-01-03T12:00:00.000"),
                                      QStringLiteral("Filter=L"), QStringLiteral("stacked_L_new.fit"));
    const QString olderL = createRun(QStringLiteral("run-older-l"), QStringLiteral("2025-01-02T12:00:00.000"),
                                     QStringLiteral("Filter=L"), QStringLiteral("stacked_L_old.fit"));
    (void)createRun(QStringLiteral("run-older-ha"), QStringLiteral("2025-01-01T12:00:00.000"),
                    QStringLiteral("Filter=Ha"), QStringLiteral("stacked_Ha_old.fit"));
    ASSERT_FALSE(newestL.isEmpty());
    ASSERT_FALSE(olderL.isEmpty());

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);
    ASSERT_GT(histModel->rowCount(), 0);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_TRUE(groupIndex.data(WorkflowHistoryModel::IsGroupItemRole).toBool());
    EXPECT_TRUE(groupIndex.data(WorkflowHistoryModel::CanCompareMasterRole).toBool());

    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());
    const QPoint comparePos = compareButtonCenter(*history, groupIndex);

    QSignalSpy compareSpy(&tab, &SummaryTab::compareRequested);

    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, comparePos);

    ASSERT_EQ(compareSpy.count(), 1);
    ASSERT_EQ(compareSpy.at(0).size(), 5);

    const QStringList paths = compareSpy.at(0).at(0).toStringList();
    EXPECT_EQ(paths, QStringList({newestL, olderL}));
    const QStringList labels = compareSpy.at(0).at(1).toStringList();
    ASSERT_EQ(labels.size(), 2);
    const QStringList contexts = compareSpy.at(0).at(2).toStringList();
    EXPECT_EQ(contexts.size(), 2);
    const QVariantList rows = compareSpy.at(0).at(3).toList();
    ASSERT_EQ(rows.size(), 2);
    const QVariantMap newestRow = rows.at(0).toMap();
    EXPECT_EQ(newestRow.value(QStringLiteral("path")).toString(), newestL);
    EXPECT_EQ(newestRow.value(QStringLiteral("executionId")).toString(), QStringLiteral("run-newest-l"));
    EXPECT_EQ(newestRow.value(QStringLiteral("executedAtText")).toString(), QStringLiteral("2025-01-03 12:00"));
    EXPECT_FALSE(newestRow.contains(QStringLiteral("durationText")));
    EXPECT_FALSE(newestRow.contains(QStringLiteral("exposure")));
    EXPECT_EQ(newestRow.value(QStringLiteral("integrationTimeText")).toString(), QStringLiteral("3h 30m"));
    EXPECT_EQ(newestRow.value(QStringLiteral("frameCount")).toInt(), 42);
    EXPECT_EQ(newestRow.value(QStringLiteral("filter")).toString(), QStringLiteral("L"));
    EXPECT_EQ(newestRow.value(QStringLiteral("object")).toString(), QStringLiteral("M83"));
    EXPECT_FALSE(labels.at(0).contains(QStringLiteral("Run ")));
    EXPECT_FALSE(labels.at(1).contains(QStringLiteral("Run ")));
    EXPECT_TRUE(labels.at(0).contains(QStringLiteral("Filter=L")));
    EXPECT_TRUE(labels.at(1).contains(QStringLiteral("Filter=L")));
    EXPECT_EQ(compareSpy.at(0).at(4).toString(), QStringLiteral("Filter=L"));
}

TEST(SummaryTabActions, CompareMasterActionDisabledWhenNoMatchingMasterExists)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const auto createRun = [&](const QString& exec_id, const QString& timestamp, const QString& group_key,
                               const QString& output_name, const QString& filter) -> QString {
        if (!QDir().mkpath(history_root.filePath(exec_id)))
        {
            ADD_FAILURE() << "Failed to create workflow history directory";
            return {};
        }
        const QDir run_dir(history_root.filePath(exec_id));

        const QString output_path = QDir(temp_dir.path()).filePath(output_name);
        if (!touchFile(output_path))
        {
            ADD_FAILURE() << "Failed to create stacked output";
            return {};
        }

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), group_key);
        group.insert(QStringLiteral("outputPath"), output_path);
        group.insert(QStringLiteral("outputFilename"), output_name);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QJsonObject manifest;
        manifest.insert(
            QStringLiteral("integrationGroups"),
            QJsonArray{manifestGroup(group_key, output_name, filter, QStringLiteral("M83"), QStringLiteral("300s"))});

        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("executionId"), exec_id);
        run_json.insert(QStringLiteral("archivedAtLocal"), timestamp);
        run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run_json.insert(QStringLiteral("manifest"), manifest);
        run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        if (!writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json))
        {
            ADD_FAILURE() << "Failed to write workflow manifest";
            return {};
        }
        return output_path;
    };

    const QString newestL =
        createRun(QStringLiteral("run-newest-l"), QStringLiteral("2025-01-03T12:00:00.000"), QStringLiteral("Filter=L"),
                  QStringLiteral("stacked_L_new.fit"), QStringLiteral("L"));
    const QString olderHa =
        createRun(QStringLiteral("run-older-ha"), QStringLiteral("2025-01-02T12:00:00.000"),
                  QStringLiteral("Filter=Ha"), QStringLiteral("stacked_Ha_old.fit"), QStringLiteral("Ha"));
    ASSERT_FALSE(newestL.isEmpty());
    ASSERT_FALSE(olderHa.isEmpty());

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_TRUE(groupIndex.data(WorkflowHistoryModel::IsGroupItemRole).toBool());
    EXPECT_FALSE(groupIndex.data(WorkflowHistoryModel::CanCompareMasterRole).toBool());
    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());

    QSignalSpy compareSpy(&tab, &SummaryTab::compareRequested);
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, compareButtonCenter(*history, groupIndex));

    EXPECT_EQ(compareSpy.count(), 0);
}

TEST(SummaryTabActions, CompareMasterActionIncludesArchivedPreviousMasterForOverwrittenOutput)
{
    // The summary reads a master's size and exposure from its header in the session the application lends.
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const QString group_key = QStringLiteral("1000x1000|-|-|-|Blue|M83|-");
    const QString output_name = QStringLiteral("stacked_1000x1000_Blue_M83.fit");
    const QString live_blue = QDir(temp_dir.path()).filePath(output_name);
    ASSERT_TRUE(writeConstantImage(live_blue, 50, 50, 1260.0));

    const auto createRun = [&](const QString& exec_id, const QString& timestamp, bool with_archive,
                               bool drizzle_enabled) -> QString {
        if (!QDir().mkpath(history_root.filePath(exec_id)))
        {
            ADD_FAILURE() << "Failed to create workflow history directory";
            return {};
        }
        const QDir run_dir(history_root.filePath(exec_id));

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), group_key);
        group.insert(QStringLiteral("outputPath"), live_blue);
        group.insert(QStringLiteral("outputFilename"), output_name);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QString archived_path;
        if (with_archive)
        {
            const QString group_dir_name = ImageIntegration::sanitizeFilename(group_key);
            const QDir group_dir(run_dir.filePath(QStringLiteral("groups/%1").arg(group_dir_name)));
            if (!QDir().mkpath(group_dir.absolutePath()))
            {
                ADD_FAILURE() << "Failed to create archived group directory";
                return {};
            }

            archived_path = group_dir.filePath(output_name);
            if (!writeConstantImage(archived_path, 100, 100, 900.0))
            {
                ADD_FAILURE() << "Failed to create archived previous master";
                return {};
            }

            QJsonObject archived;
            archived.insert(QStringLiteral("filename"), output_name);
            archived.insert(QStringLiteral("sizeBytes"), QFileInfo(archived_path).size());
            group.insert(QStringLiteral("archivedFiles"), QJsonArray{archived});
        }

        QJsonObject manifest;
        QJsonObject manifest_group =
            manifestGroup(group_key, output_name, QStringLiteral("Blue"), QStringLiteral("M83"), QStringLiteral("-"));
        manifest_group.insert(QStringLiteral("stacking"), stackingSettings(drizzle_enabled));
        if (drizzle_enabled)
        {
            manifest_group.insert(QStringLiteral("drizzle"), drizzleSettings());
        }
        manifest.insert(QStringLiteral("integrationGroups"), QJsonArray{manifest_group});

        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("executionId"), exec_id);
        run_json.insert(QStringLiteral("archivedAtLocal"), timestamp);
        run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run_json.insert(QStringLiteral("manifest"), manifest);
        run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        if (!writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json))
        {
            ADD_FAILURE() << "Failed to write workflow manifest";
            return {};
        }
        return archived_path;
    };

    (void)createRun(QStringLiteral("run-older-blue"), QStringLiteral("2025-01-01T12:00:00.000"), false, true);
    const QString archived_blue =
        createRun(QStringLiteral("run-newest-blue"), QStringLiteral("2025-01-03T12:00:00.000"), true, false);
    ASSERT_FALSE(archived_blue.isEmpty());
    ASSERT_TRUE(writeConstantImage(live_blue, 50, 50, 1260.0));

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());

    QSignalSpy compareSpy(&tab, &SummaryTab::compareRequested);
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, compareButtonCenter(*history, groupIndex));

    ASSERT_EQ(compareSpy.count(), 1);
    const QStringList paths = compareSpy.at(0).at(0).toStringList();
    EXPECT_EQ(paths, QStringList({live_blue, archived_blue}));
    const QStringList labels = compareSpy.at(0).at(1).toStringList();
    ASSERT_EQ(labels.size(), 2);
    EXPECT_FALSE(labels.at(0).contains(QStringLiteral("Run ")));
    EXPECT_FALSE(labels.at(1).contains(QStringLiteral("Previous before")));
    EXPECT_FALSE(labels.at(1).contains(QStringLiteral("Archived master")));
    EXPECT_TRUE(labels.at(0).contains(QStringLiteral("Blue")));
    EXPECT_TRUE(labels.at(1).contains(QStringLiteral("Blue")));
    const QStringList contexts = compareSpy.at(0).at(2).toStringList();
    ASSERT_EQ(contexts.size(), 2);
    EXPECT_TRUE(contexts.at(0).contains(QStringLiteral("Drizzle: Disabled")));
    EXPECT_TRUE(contexts.at(0).contains(QStringLiteral("Resolution: 50x50")));
    EXPECT_TRUE(contexts.at(1).contains(QStringLiteral("Drizzle: Enabled x2")));
    EXPECT_TRUE(contexts.at(1).contains(QStringLiteral("Resolution: 100x100")));
    EXPECT_FALSE(contexts.at(1).contains(QStringLiteral("Archived previous master")));
    const QVariantList rows = compareSpy.at(0).at(3).toList();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("integrationTimeText")).toString(), QStringLiteral("21m 0s"));
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("integrationTimeText")).toString(), QStringLiteral("15m 0s"));
}

TEST(SummaryTabActions, CompareMasterActionMatchesCompatibleHistoryTraitsWhenGroupKeysChange)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const auto createRun = [&](const QString& exec_id, const QString& timestamp, const QString& group_key,
                               const QString& output_name, const QString& filter, const QString& exposure) -> QString {
        if (!QDir().mkpath(history_root.filePath(exec_id)))
        {
            ADD_FAILURE() << "Failed to create workflow history directory";
            return {};
        }
        const QDir run_dir(history_root.filePath(exec_id));

        const QString output_path = QDir(temp_dir.path()).filePath(output_name);
        if (!touchFile(output_path))
        {
            ADD_FAILURE() << "Failed to create stacked output";
            return {};
        }

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), group_key);
        group.insert(QStringLiteral("outputPath"), output_path);
        group.insert(QStringLiteral("outputFilename"), output_name);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QJsonObject manifest;
        manifest.insert(QStringLiteral("integrationGroups"),
                        QJsonArray{manifestGroup(group_key, output_name, filter, QStringLiteral("M83"), exposure)});

        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("executionId"), exec_id);
        run_json.insert(QStringLiteral("archivedAtLocal"), timestamp);
        run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run_json.insert(QStringLiteral("manifest"), manifest);
        run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        if (!writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json))
        {
            ADD_FAILURE() << "Failed to write workflow manifest";
            return {};
        }
        return output_path;
    };

    const QString newest_blue_120 =
        createRun(QStringLiteral("run-newest-blue-120"), QStringLiteral("2025-01-03T12:00:00.000"),
                  QStringLiteral("1000x1000|-|-|120|Blue|M83|-"), QStringLiteral("stacked_blue_120.fit"),
                  QStringLiteral("Blue"), QStringLiteral("120"));
    const QString older_blue =
        createRun(QStringLiteral("run-older-blue"), QStringLiteral("2025-01-02T12:00:00.000"),
                  QStringLiteral("1000x1000|-|-|-|Blue|M83|-"), QStringLiteral("stacked_blue.fit"),
                  QStringLiteral("Blue"), QStringLiteral("-"));
    (void)createRun(QStringLiteral("run-older-red"), QStringLiteral("2025-01-01T12:00:00.000"),
                    QStringLiteral("1000x1000|-|-|-|Red|M83|-"), QStringLiteral("stacked_red.fit"),
                    QStringLiteral("Red"), QStringLiteral("-"));
    ASSERT_FALSE(newest_blue_120.isEmpty());
    ASSERT_FALSE(older_blue.isEmpty());

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());

    QSignalSpy compareSpy(&tab, &SummaryTab::compareRequested);
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, compareButtonCenter(*history, groupIndex));

    ASSERT_EQ(compareSpy.count(), 1);
    const QStringList paths = compareSpy.at(0).at(0).toStringList();
    EXPECT_EQ(paths, QStringList({newest_blue_120, older_blue}));
    const QStringList labels = compareSpy.at(0).at(1).toStringList();
    ASSERT_EQ(labels.size(), 2);
    EXPECT_NE(labels.at(0), labels.at(1));
}

TEST(SummaryTabActions, CompareMasterActionAllowsMatchingGroupsWithDifferentOutputSizes)
{
    // The summary reads a master's size and exposure from its header in the session the application lends.
    an::tests::integration::ScopedProcessWindowContext lent;
    ASSERT_TRUE(lent.context().valid());

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const auto createRun = [&](const QString& exec_id, const QString& timestamp, const QString& group_key,
                               const QString& output_name, const QString& resolution, const std::uint32_t width,
                               const std::uint32_t height) -> QString {
        if (!QDir().mkpath(history_root.filePath(exec_id)))
        {
            ADD_FAILURE() << "Failed to create workflow history directory";
            return {};
        }
        const QDir run_dir(history_root.filePath(exec_id));

        const QString output_path = QDir(temp_dir.path()).filePath(output_name);
        if (!writeConstantImage(output_path, width, height))
        {
            ADD_FAILURE() << "Failed to create stacked output";
            return {};
        }

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), group_key);
        group.insert(QStringLiteral("outputPath"), output_path);
        group.insert(QStringLiteral("outputFilename"), output_name);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QJsonObject manifest;
        manifest.insert(QStringLiteral("integrationGroups"),
                        QJsonArray{manifestGroup(group_key, output_name, QStringLiteral("Blue"), QStringLiteral("M83"),
                                                 QStringLiteral("-"), resolution)});

        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("executionId"), exec_id);
        run_json.insert(QStringLiteral("archivedAtLocal"), timestamp);
        run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run_json.insert(QStringLiteral("manifest"), manifest);
        run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        if (!writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json))
        {
            ADD_FAILURE() << "Failed to write workflow manifest";
            return {};
        }
        return output_path;
    };

    const QString drizzle_blue =
        createRun(QStringLiteral("run-newest-blue-drizzle"), QStringLiteral("2025-01-03T12:00:00.000"),
                  QStringLiteral("2000x2000|-|-|-|Blue|M83|-"), QStringLiteral("stacked_blue_drizzle.fit"),
                  QStringLiteral("2000x2000"), 2200, 1900);
    const QString normal_blue =
        createRun(QStringLiteral("run-older-blue-normal"), QStringLiteral("2025-01-02T12:00:00.000"),
                  QStringLiteral("1000x1000|-|-|-|Blue|M83|-"), QStringLiteral("stacked_blue_normal.fit"),
                  QStringLiteral("1000x1000"), 900, 800);
    ASSERT_FALSE(drizzle_blue.isEmpty());
    ASSERT_FALSE(normal_blue.isEmpty());

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());

    QSignalSpy compareSpy(&tab, &SummaryTab::compareRequested);
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, compareButtonCenter(*history, groupIndex));

    ASSERT_EQ(compareSpy.count(), 1);
    const QStringList paths = compareSpy.at(0).at(0).toStringList();
    EXPECT_EQ(paths, QStringList({drizzle_blue, normal_blue}));
    const QVariantList rows = compareSpy.at(0).at(3).toList();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("resolution")).toString(), QStringLiteral("2200x1900"));
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("resolution")).toString(), QStringLiteral("900x800"));
}

TEST(SummaryTabActions, CompareMasterActionAllowsDrizzleAndNormalWhenRegistrationGroupsDiffer)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    const QDir history_root(QDir(temp_dir.path()).filePath(QStringLiteral("history")));
    const auto createRun = [&](const QString& exec_id, const QString& timestamp, const QString& group_key,
                               const QString& output_name, const QString& resolution,
                               const QString& registration_group_key) -> QString {
        if (!QDir().mkpath(history_root.filePath(exec_id)))
        {
            ADD_FAILURE() << "Failed to create workflow history directory";
            return {};
        }
        const QDir run_dir(history_root.filePath(exec_id));

        const QString output_path = QDir(temp_dir.path()).filePath(output_name);
        if (!touchFile(output_path))
        {
            ADD_FAILURE() << "Failed to create stacked output";
            return {};
        }

        QJsonObject group;
        group.insert(QStringLiteral("groupKey"), group_key);
        group.insert(QStringLiteral("outputPath"), output_path);
        group.insert(QStringLiteral("outputFilename"), output_name);
        group.insert(QStringLiteral("registrationGroupKey"), registration_group_key);
        group.insert(QStringLiteral("result"), QStringLiteral("success"));

        QJsonObject manifest;
        manifest.insert(QStringLiteral("integrationGroups"),
                        QJsonArray{manifestGroup(group_key, output_name, QStringLiteral("Blue"), QStringLiteral("M83"),
                                                 QStringLiteral("-"), resolution, registration_group_key)});

        QJsonObject run_json;
        run_json.insert(QStringLiteral("schemaVersion"), 3);
        run_json.insert(QStringLiteral("executionId"), exec_id);
        run_json.insert(QStringLiteral("archivedAtLocal"), timestamp);
        run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
        run_json.insert(QStringLiteral("manifest"), manifest);
        run_json.insert(QStringLiteral("integrationGroupResults"), QJsonArray{group});

        if (!writeJson(run_dir.filePath(QStringLiteral("workflow_manifest.json")), run_json))
        {
            ADD_FAILURE() << "Failed to write workflow manifest";
            return {};
        }
        return output_path;
    };

    const QString normal_blue =
        createRun(QStringLiteral("run-newest-blue-normal"), QStringLiteral("2025-01-03T12:00:00.000"),
                  QStringLiteral("1000x1000|-|-|-|Blue|M83|-"), QStringLiteral("stacked_blue_normal.fit"),
                  QStringLiteral("1000x1000"), QStringLiteral("normal-registration|M83"));
    const QString drizzle_blue =
        createRun(QStringLiteral("run-older-blue-drizzle"), QStringLiteral("2025-01-02T12:00:00.000"),
                  QStringLiteral("2000x2000|-|-|-|Blue|M83|-"), QStringLiteral("stacked_blue_drizzle.fit"),
                  QStringLiteral("2000x2000"), QStringLiteral("drizzle-registration|M83"));
    ASSERT_FALSE(normal_blue.isEmpty());
    ASSERT_FALSE(drizzle_blue.isEmpty());

    waitForRefresh(tab);

    tab.resize(900, 700);
    tab.show();
    QTest::qWait(10);

    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);

    const QModelIndex runIndex = histModel->index(0, 0);
    ASSERT_TRUE(runIndex.isValid());
    history->expand(runIndex);

    const QModelIndex groupIndex = histModel->index(0, 0, runIndex);
    ASSERT_TRUE(groupIndex.isValid());
    ASSERT_FALSE(history->visualRect(groupIndex).isEmpty());

    QSignalSpy compareSpy(&tab, &SummaryTab::compareRequested);
    QTest::mouseClick(history->viewport(), Qt::LeftButton, Qt::NoModifier, compareButtonCenter(*history, groupIndex));

    ASSERT_EQ(compareSpy.count(), 1);
    const QStringList paths = compareSpy.at(0).at(0).toStringList();
    EXPECT_EQ(paths, QStringList({normal_blue, drizzle_blue}));
}
