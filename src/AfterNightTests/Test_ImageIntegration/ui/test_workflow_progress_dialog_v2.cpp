// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QIODevice>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QProgressBar>
#include <QStringList>
#include <QTextEdit>
#include <QtTest/QTest>

#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"
#include "processes/ImageIntegration/workflow/WorkflowProgressController.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "theme/AnDesignTokens.h"

class WorkflowProgressDialogV2Test : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!QApplication::instance())
        {
            static int argc = 0;
            static char* argv[] = {nullptr};
            new QApplication(argc, argv);
        }
    }

    // The dialogs never start a run, so their controllers are lent no engine session.
    IntegrationDataModel model;
};

namespace
{
class ScopedAppPalette
{
public:
    ScopedAppPalette() : m_original(QApplication::palette()) {}

    ~ScopedAppPalette()
    {
        QApplication::setPalette(m_original);
    }

private:
    QPalette m_original;
};

QPalette makeTestPalette(const QColor& windowColor, const QColor& baseColor, const QColor& textColor,
                         const QColor& buttonColor)
{
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::Base, baseColor);
    palette.setColor(QPalette::AlternateBase, buttonColor);
    palette.setColor(QPalette::WindowText, textColor);
    palette.setColor(QPalette::Text, textColor);
    palette.setColor(QPalette::Button, buttonColor);
    palette.setColor(QPalette::ButtonText, textColor);
    return palette;
}

QString readRepoSourceFile(const QString& relativePath)
{
    QStringList seeds = {QDir::currentPath()};
    if (QCoreApplication::instance())
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }
    seeds.append(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());

    for (const QString& seed : seeds)
    {
        QDir dir(seed);
        while (true)
        {
            const QString candidate = dir.filePath(relativePath);
            if (QFileInfo::exists(candidate))
            {
                QFile file(candidate);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    return QString::fromUtf8(file.readAll());
                }
            }

            if (!dir.cdUp())
            {
                break;
            }
        }
    }

    return {};
}

} // namespace

TEST_F(WorkflowProgressDialogV2Test, SourceGuardUsesNonBlockingCompletionMessageBoxes)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.cpp"));
    ASSERT_FALSE(source.isEmpty());

    EXPECT_FALSE(source.contains(
        QRegularExpression(QStringLiteral(R"(QMessageBox::(warning|information|critical|question)\s*\()"))));
    EXPECT_FALSE(source.contains(QRegularExpression(QStringLiteral(R"(\.exec\s*\()"))));
    EXPECT_TRUE(source.contains(QStringLiteral("messageBox->open()")));
}

TEST_F(WorkflowProgressDialogV2Test, ConstructsWithController)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);
    // Should construct without crashing
}

TEST_F(WorkflowProgressDialogV2Test, InitializesWithPendingStages)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);
    dialog.setStages({"Bias", "Dark", "Flat"});

    // Can't easily inspect internal state without friend access or public getters,
    // but we can verify it handles the input without error.
}

TEST_F(WorkflowProgressDialogV2Test, HandlesProgressUpdate)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);
    dialog.setStages({"Bias", "Dark", "Flat"});

    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::Bias;
    progress.stageProgress = 0.5f;
    progress.currentGroup = 1;
    progress.totalGroups = 3;
    progress.currentOperation = "Stacking...";

    dialog.onProgressUpdated(progress);

    auto* opLabel = dialog.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    ASSERT_NE(opLabel, nullptr);
    EXPECT_EQ(opLabel->text(), QStringLiteral("[1/3] - Stacking..."));
}

TEST_F(WorkflowProgressDialogV2Test, ElapsedTimeUsesHHMMSSFormat)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    auto* elapsedLabel = dialog.findChild<QLabel*>(QStringLiteral("lblElapsed"));
    ASSERT_NE(elapsedLabel, nullptr);

    const QRegularExpression re(QStringLiteral("^Elapsed: \\d{2}:\\d{2}:\\d{2}"));
    EXPECT_TRUE(re.match(elapsedLabel->text()).hasMatch()) << elapsedLabel->text().toStdString();
}

TEST_F(WorkflowProgressDialogV2Test, TimeDisplayShowsElapsedAndCurrentStageWithoutEta)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);
    dialog.setStages({"Bias", "LightsStacking"});

    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::LightsStacking;
    progress.currentGroup = 1;
    progress.totalGroups = 1;
    progress.overallProgress = 0.25f;
    progress.currentOperation = QStringLiteral("Stacking");
    dialog.onProgressUpdated(progress);

    auto* elapsedLabel = dialog.findChild<QLabel*>(QStringLiteral("lblElapsed"));
    ASSERT_NE(elapsedLabel, nullptr);
    EXPECT_TRUE(elapsedLabel->text().contains(QStringLiteral("Elapsed:"))) << elapsedLabel->text().toStdString();
    EXPECT_FALSE(elapsedLabel->text().contains(QStringLiteral("ETA:"))) << elapsedLabel->text().toStdString();
    EXPECT_TRUE(elapsedLabel->text().contains(QStringLiteral("Stage: Stacking"))) << elapsedLabel->text().toStdString();
}

TEST_F(WorkflowProgressDialogV2Test, HandlesCompletion)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = true;

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_TRUE(dialog.wasSuccessful());
    EXPECT_FALSE(dialog.wasCancelled());
    EXPECT_FALSE(dialog.wasFailed());
}

TEST_F(WorkflowProgressDialogV2Test, OpenReportButtonFollowsHtmlReportPath)
{
    WorkflowProgressController noReportController(&model, nullptr);
    WorkflowProgressDialogV2 noReportDialog(&noReportController);
    auto* noReportButton = noReportDialog.findChild<QPushButton*>(QStringLiteral("btnOpenReport"));
    ASSERT_NE(noReportButton, nullptr);

    WorkflowExecutor::Result noReportResult;
    noReportResult.success = true;
    noReportDialog.onFinished(noReportResult);
    EXPECT_TRUE(noReportButton->isHidden());
    EXPECT_TRUE(noReportButton->property("reportPath").toString().isEmpty());

    WorkflowProgressController reportController(&model, nullptr);
    WorkflowProgressDialogV2 reportDialog(&reportController);
    auto* reportButton = reportDialog.findChild<QPushButton*>(QStringLiteral("btnOpenReport"));
    ASSERT_NE(reportButton, nullptr);

    WorkflowExecutor::Result reportResult;
    reportResult.success = true;
    reportResult.htmlReportPath = QStringLiteral("C:/tmp/workflow_report.html");
    reportDialog.onFinished(reportResult);
    EXPECT_FALSE(reportButton->isHidden());
    EXPECT_EQ(reportButton->property("reportPath").toString(), reportResult.htmlReportPath);
}

TEST_F(WorkflowProgressDialogV2Test, FailureAndCancellationKeepLastRealProgress)
{
    WorkflowProgress progress;
    progress.currentStage = WorkflowStage::LightsRegistration;
    progress.overallProgress = 0.42f;
    progress.currentOperation = QStringLiteral("Registering");

    WorkflowProgressController failureController(&model, nullptr);
    WorkflowProgressDialogV2 failureDialog(&failureController);
    failureDialog.onProgressUpdated(progress);
    auto* failureBar = failureDialog.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(failureBar, nullptr);
    ASSERT_EQ(failureBar->value(), 42);

    WorkflowExecutor::Result failed;
    failed.success = false;
    failed.cancelled = false;
    failed.errorMessage = QStringLiteral("Registration failed");
    failureDialog.onFinished(failed);
    EXPECT_EQ(failureBar->value(), 42);

    WorkflowProgressController cancelController(&model, nullptr);
    WorkflowProgressDialogV2 cancelDialog(&cancelController);
    cancelDialog.onProgressUpdated(progress);
    auto* cancelBar = cancelDialog.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(cancelBar, nullptr);

    WorkflowExecutor::Result cancelled;
    cancelled.success = false;
    cancelled.cancelled = true;
    cancelDialog.onFinished(cancelled);
    EXPECT_EQ(cancelBar->value(), 42);
}

TEST_F(WorkflowProgressDialogV2Test, TracksCancellationState)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.cancelled = true;

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_FALSE(dialog.wasSuccessful());
    EXPECT_TRUE(dialog.wasCancelled());
    EXPECT_FALSE(dialog.wasFailed());
}

TEST_F(WorkflowProgressDialogV2Test, TracksFailureState)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    WorkflowExecutor::Result result;
    result.success = false;
    result.cancelled = false;
    result.errorMessage = QStringLiteral("Boom");

    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    EXPECT_FALSE(dialog.wasSuccessful());
    EXPECT_FALSE(dialog.wasCancelled());
    EXPECT_TRUE(dialog.wasFailed());
}

TEST_F(WorkflowProgressDialogV2Test, RejectWhileRunningRequestsCancellationAndKeepsDialogVisible)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);
    dialog.show();
    QCoreApplication::processEvents();
    QTest::qWait(10);

    auto* cancelButton = dialog.findChild<QPushButton*>(QStringLiteral("btnCancel"));
    ASSERT_NE(cancelButton, nullptr);
    ASSERT_TRUE(cancelButton->isEnabled());

    dialog.close();
    QCoreApplication::processEvents();

    EXPECT_FALSE(cancelButton->isEnabled());
    EXPECT_EQ(cancelButton->text(), QStringLiteral("Cancelling..."));
    EXPECT_TRUE(dialog.isVisible());
}

TEST_F(WorkflowProgressDialogV2Test, RejectAfterFinishedClosesDialog)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);
    dialog.show();
    QCoreApplication::processEvents();

    WorkflowExecutor::Result result;
    result.success = true;
    dialog.onFinished(result);
    QCoreApplication::processEvents();
    QTest::qWait(50);

    dialog.close();
    QCoreApplication::processEvents();
    EXPECT_FALSE(dialog.isVisible());
}

TEST_F(WorkflowProgressDialogV2Test, AppliesThemeAwareStylesForDarkAndLightPalettes)
{
    ScopedAppPalette paletteGuard;

    QApplication::setPalette(
        makeTestPalette(QColor(24, 28, 34), QColor(20, 24, 30), QColor(216, 224, 236), QColor(30, 36, 44)));
    WorkflowProgressController darkController(&model, nullptr);
    WorkflowProgressDialogV2 darkDialog(&darkController);
    auto* darkLog = darkDialog.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    auto* darkBar = darkDialog.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(darkLog, nullptr);
    ASSERT_NE(darkBar, nullptr);
    const QColor darkLogBg = darkLog->palette().color(QPalette::Base);

    QApplication::setPalette(
        makeTestPalette(QColor(245, 247, 252), QColor(252, 253, 255), QColor(32, 40, 54), QColor(238, 242, 248)));
    WorkflowProgressController lightController(&model, nullptr);
    WorkflowProgressDialogV2 lightDialog(&lightController);
    auto* lightLog = lightDialog.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    auto* lightBar = lightDialog.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(lightLog, nullptr);
    ASSERT_NE(lightBar, nullptr);
    const QColor lightLogBg = lightLog->palette().color(QPalette::Base);

    // Log background color should differ between dark and light themes.
    EXPECT_NE(darkLogBg, lightLogBg);
    EXPECT_TRUE(darkLogBg.isValid());
    EXPECT_TRUE(lightLogBg.isValid());
}

TEST_F(WorkflowProgressDialogV2Test, UsesTokenFontsAndRadiusForCoreWidgets)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    auto* opLabel = dialog.findChild<QLabel*>(QStringLiteral("lblCurrentOperation"));
    auto* logTitle = dialog.findChild<QLabel*>(QStringLiteral("lblLogTitle"));
    auto* elapsed = dialog.findChild<QLabel*>(QStringLiteral("lblElapsed"));
    auto* logText = dialog.findChild<QTextEdit*>(QStringLiteral("workflowLogText"));
    ASSERT_NE(opLabel, nullptr);
    ASSERT_NE(logTitle, nullptr);
    ASSERT_NE(elapsed, nullptr);
    ASSERT_NE(logText, nullptr);

    EXPECT_EQ(opLabel->font(), an::ui::tokens().typography.heading3);
    EXPECT_EQ(logTitle->font(), an::ui::tokens().typography.heading3);
    EXPECT_EQ(elapsed->font(), an::ui::tokens().typography.body);
    EXPECT_EQ(logText->font(), an::ui::tokens().typography.monospace);
    EXPECT_EQ(logText->property("anFrameRadius").toInt(), an::ui::tokens().radius.sm);
}

TEST_F(WorkflowProgressDialogV2Test, UsesResponsiveTokenSizedDialog)
{
    WorkflowProgressController controller(&model, nullptr);
    WorkflowProgressDialogV2 dialog(&controller);

    const auto& spacing = an::ui::tokens().spacing;
    EXPECT_EQ(dialog.minimumSize(), QSize(spacing.xl * 26, spacing.xl * 16));
    EXPECT_NE(dialog.minimumSize(), dialog.maximumSize());
    EXPECT_TRUE(dialog.isSizeGripEnabled());

    auto* progressBar = dialog.findChild<QProgressBar*>(QStringLiteral("stageProgressBar"));
    ASSERT_NE(progressBar, nullptr);
    EXPECT_EQ(progressBar->minimumHeight(), spacing.xl);
}
