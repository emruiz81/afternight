// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTreeView>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"
#include "processes/ImageIntegration/tabs/WorkflowHistoryModel.h"

namespace
{
void ensureApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}

FrameInfo makeLight(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("Obj"),
                       QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    return FrameInfo(FrameId(path), path, meta);
}

void waitForRefresh(SummaryTab& tab)
{
    QSignalSpy spy(&tab, &SummaryTab::refreshed);
    tab.refreshAll();
    ASSERT_TRUE(spy.wait(2000));
    QCoreApplication::processEvents();
}

QString sessionNotesPath(const QString& outputDir)
{
    return QDir(outputDir).filePath(QStringLiteral(".afternight/image_integration/session_notes.txt"));
}

void waitForNotesToPersist(const QString& outputDir, const QString& expectedSubstring)
{
    const QString path = sessionNotesPath(outputDir);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
        {
            const QString content = QString::fromUtf8(file.readAll());
            if (content.contains(expectedSubstring))
            {
                return;
            }
        }

        QCoreApplication::processEvents();
        QTest::qWait(50);
    }

    FAIL() << "Timed out waiting for session notes to persist to: " << path.toStdString();
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

QString getSummaryText(SummaryTab& tab)
{
    auto* widget = tab.findChild<QWidget*>(QStringLiteral("integrationReportWidget"));
    if (!widget)
    {
        return QString();
    }

    QString fullText;
    const auto labels = widget->findChildren<QLabel*>();
    for (const auto* lbl : labels)
    {
        fullText += lbl->text() + QStringLiteral(" ");
    }
    return fullText;
}
} // namespace

TEST(SummaryDashboard, ShowsReadinessBannerAndExecuteButton)
{
    ensureApp();
    IntegrationDataModel model;
    SummaryTab tab(&model);

    auto* title = tab.findChild<QLabel*>(QStringLiteral("lblStatusTitle"));
    ASSERT_TRUE(title != nullptr);

    auto* execute = tab.findChild<QPushButton*>(QStringLiteral("btnExecute"));
    ASSERT_TRUE(execute != nullptr);

    // With an empty model, validation should be in an error state.
    EXPECT_TRUE(title->text().contains(QStringLiteral("Cannot")));
    EXPECT_FALSE(execute->isEnabled());
}

TEST(SummaryDashboard, BecomesReadyWhenOutputDirAndLightsExist)
{
    ensureApp();

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);

    model.setOutputDirectory(temp_dir.path());
    model.addFrames(FrameType::Light, {makeLight(QDir(temp_dir.path()).filePath(QStringLiteral("M42_001.fit")))});

    waitForRefresh(tab);

    auto* title = tab.findChild<QLabel*>(QStringLiteral("lblStatusTitle"));
    ASSERT_TRUE(title != nullptr);

    auto* issues = tab.findChild<QWidget*>(QStringLiteral("statusIssuesWidget"));
    ASSERT_TRUE(issues != nullptr);

    auto* execute = tab.findChild<QPushButton*>(QStringLiteral("btnExecute"));
    ASSERT_TRUE(execute != nullptr);

    EXPECT_TRUE(title->text().contains(QStringLiteral("Ready")));
    EXPECT_TRUE(issues->isHidden());
    EXPECT_TRUE(execute->isEnabled());
}

TEST(SummaryDashboard, RunHistoryReadsFromHistoryDir)
{
    ensureApp();

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    const QString output_dir = temp_dir.path();
    const QDir history_root(QDir(output_dir).filePath(QStringLiteral("history")));
    ASSERT_TRUE(QDir().mkpath(history_root.filePath(QStringLiteral("20250101_120000_000"))));

    const QDir run_dir(history_root.filePath(QStringLiteral("20250101_120000_000")));
    const QString manifest_path = run_dir.filePath(QStringLiteral("workflow_manifest.json"));

    QJsonObject run_json;
    run_json.insert(QStringLiteral("archivedAtUtc"), QStringLiteral("2025-01-01T12:00:00.000Z"));
    run_json.insert(QStringLiteral("archivedAtLocal"), QStringLiteral("2025-01-01T12:00:00.000"));
    run_json.insert(QStringLiteral("executionId"), QStringLiteral("test-exec-001"));
    run_json.insert(QStringLiteral("executionMode"), QStringLiteral("Full"));
    run_json.insert(QStringLiteral("overallResult"), QStringLiteral("success"));
    run_json.insert(QStringLiteral("totalDurationMs"), 5000);
    run_json.insert(QStringLiteral("warningCount"), 0);
    run_json.insert(
        QStringLiteral("integrationGroupResults"),
        QJsonArray{QJsonObject{{QStringLiteral("groupKey"), QStringLiteral("TestGroup")},
                               {QStringLiteral("outputPath"), QDir(output_dir).filePath(QStringLiteral("stacked.fit"))},
                               {QStringLiteral("outputFilename"), QStringLiteral("stacked.fit")},
                               {QStringLiteral("outputBytes"), 1024},
                               {QStringLiteral("lightFrameCount"), 5},
                               {QStringLiteral("durationMs"), 5000},
                               {QStringLiteral("result"), QStringLiteral("success")}}});

    ASSERT_TRUE(writeJson(manifest_path, run_json));

    IntegrationDataModel model;
    SummaryTab tab(&model);
    model.setOutputDirectory(output_dir);
    waitForRefresh(tab);

    // We look for QTreeView now (created dynamically in setupRunHistoryView)
    auto* history = tab.findChild<QTreeView*>();
    ASSERT_TRUE(history != nullptr);
    auto* histModel = history->model();
    ASSERT_TRUE(histModel != nullptr);
    ASSERT_GT(histModel->rowCount(), 0);

    const QModelIndex idx = histModel->index(0, 0);
    // Role WorkflowHistoryModel::ExecutionIdRole
    EXPECT_EQ(histModel->data(idx, WorkflowHistoryModel::ExecutionIdRole).toString(), QStringLiteral("test-exec-001"));
}

TEST(SummaryDashboard, IntegrationSummaryShowsTotalExposureAndFilterBreakdown)
{
    ensureApp();

    IntegrationDataModel model;
    SummaryTab tab(&model);

    FrameMetadata metaA(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, std::nullopt, std::nullopt,
                        QStringLiteral("HA"), QStringLiteral("Obj"),
                        QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QString());
    FrameMetadata metaB(FrameType::Light, QSize(1000, 1000), 60.0, std::nullopt, std::nullopt, std::nullopt,
                        QStringLiteral("OIII"), QStringLiteral("Obj"),
                        QDateTime::fromSecsSinceEpoch(1700003600, QTimeZone::UTC), QString());

    model.addFrames(
        FrameType::Light,
        {FrameInfo(FrameId(QStringLiteral("C:/test/ha.fit")), QStringLiteral("C:/test/ha.fit"), metaA),
         FrameInfo(FrameId(QStringLiteral("C:/test/oiii.fit")), QStringLiteral("C:/test/oiii.fit"), metaB)});

    waitForRefresh(tab);

    const QString reportText = getSummaryText(tab);
    EXPECT_TRUE(reportText.contains(QStringLiteral("1m")) || reportText.contains(QStringLiteral("90s")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("HA")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("OIII")));
}

TEST(SummaryDashboard, IntegrationSummarySupportsMultipleTargetsAndSetups)
{
    ensureApp();

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    SummaryTab tab(&model);
    model.setOutputDirectory(temp_dir.path());

    FrameMetadata metaA(FrameType::Light, QSize(1000, 1000), 30.0, std::nullopt, 100, 10, QStringLiteral("HA"),
                        QStringLiteral("M42"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                        QStringLiteral("ASI2600MM"));

    FrameMetadata metaB(FrameType::Light, QSize(1000, 1000), 60.0, std::nullopt, 200, 20, QStringLiteral("OIII"),
                        QStringLiteral("M31"), QDateTime::fromSecsSinceEpoch(1700003600, QTimeZone::UTC),
                        QStringLiteral("ASI533MM"));

    const QString p1 = QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit"));
    const QString p2 = QDir(temp_dir.path()).filePath(QStringLiteral("light2.fit"));
    model.addFrames(FrameType::Light, {FrameInfo(FrameId(p1), p1, metaA), FrameInfo(FrameId(p2), p2, metaB)});
    waitForRefresh(tab);

    const QString reportText = getSummaryText(tab);
    EXPECT_TRUE(reportText.contains(QStringLiteral("Targets")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("M42")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("M31")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("Camera")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("ASI2600MM")));
    EXPECT_TRUE(reportText.contains(QStringLiteral("ASI533MM")));
}

TEST(SummaryDashboard, SessionNotesPersistToOutputDir)
{
    ensureApp();

    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    IntegrationDataModel model;
    {
        SummaryTab tab(&model);
        model.setOutputDirectory(temp_dir.path());
        waitForRefresh(tab);

        auto* notes = tab.findChild<QPlainTextEdit*>(QStringLiteral("txtSessionNotes"));
        ASSERT_TRUE(notes != nullptr);
        notes->setPlainText(QStringLiteral("Night was clear."));
        waitForNotesToPersist(temp_dir.path(), QStringLiteral("Night was clear."));
    }

    SummaryTab tab2(&model);
    model.setOutputDirectory(temp_dir.path());
    waitForRefresh(tab2);

    auto* notes2 = tab2.findChild<QPlainTextEdit*>(QStringLiteral("txtSessionNotes"));
    ASSERT_TRUE(notes2 != nullptr);
    EXPECT_TRUE(notes2->toPlainText().contains(QStringLiteral("Night was clear.")));
}