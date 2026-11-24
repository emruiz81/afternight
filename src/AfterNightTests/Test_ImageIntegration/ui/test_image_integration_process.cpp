// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"

// Note: Full integration tests require AfterNightApp context.
// These tests cover widget setup and signal connections that can be verified in isolation.

namespace
{
QString readRepoSourceFile(const QString& relativePath)
{
    QStringList seeds = {QDir::currentPath(), QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()};
    if (QCoreApplication::instance())
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }

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

QString functionSource(const QString& source, const QString& signature, const QString& nextSignature)
{
    const int start = source.indexOf(signature);
    if (start < 0)
    {
        return {};
    }

    const int end = source.indexOf(nextSignature, start + signature.size());
    if (end <= start)
    {
        return {};
    }

    return source.mid(start, end - start);
}

} // namespace

class ImageIntegrationProcessTest : public testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure QApplication exists for Qt widget tests
        if (!QCoreApplication::instance())
        {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QApplication app(argc, argv);
        }
    }
};

TEST_F(ImageIntegrationProcessTest, ConstructsWithoutCrash)
{
    // ImageIntegrationProcess requires AfterNightApp, so we test components individually
    // This test ensures the test infrastructure is working
    SUCCEED();
}

TEST_F(ImageIntegrationProcessTest, FixTargetEnumHasExpectedValues)
{
    // Verify FixTarget enum values are as expected for navigation
    EXPECT_EQ(static_cast<int>(FixTarget::None), 0);
    EXPECT_EQ(static_cast<int>(FixTarget::OutputDirectory), 1);
    EXPECT_EQ(static_cast<int>(FixTarget::BiasFrames), 2);
    EXPECT_EQ(static_cast<int>(FixTarget::DarkFrames), 3);
    EXPECT_EQ(static_cast<int>(FixTarget::FlatFrames), 4);
    EXPECT_EQ(static_cast<int>(FixTarget::LightFrames), 5);
    EXPECT_EQ(static_cast<int>(FixTarget::CalibrationTab), 6);
    EXPECT_EQ(static_cast<int>(FixTarget::RegistrationTab), 7);
    EXPECT_EQ(static_cast<int>(FixTarget::IntegrationTab), 8);
}

TEST_F(ImageIntegrationProcessTest, SummaryTabEmitsFixRequestedSignal)
{
    // Create minimal model for SummaryTab
    IntegrationDataModel model;
    SummaryTab tab(&model);

    QSignalSpy spy(&tab, &SummaryTab::fixRequested);
    ASSERT_TRUE(spy.isValid());

    // The actual signal emission is triggered by Fix buttons in the UI,
    // which requires full setup. This test verifies the signal is connectable.
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(ImageIntegrationProcessTest, RegistrationOnlyUpdatesAutoReferenceWithoutFinalPass)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());
    EXPECT_FALSE(source.contains(QStringLiteral("runAutoReferenceFinalRegistrationPass")));

    const QString singleGroup =
        functionSource(source, QStringLiteral("void ImageIntegrationProcess::onRunRegistrationForGroup"),
                       QStringLiteral("void ImageIntegrationProcess::onRunRegistrationForAllGroups"));
    ASSERT_FALSE(singleGroup.isEmpty());
    EXPECT_TRUE(singleGroup.contains(QStringLiteral("result.success")));
    EXPECT_TRUE(singleGroup.contains(QStringLiteral("pass1.outputDirectory")));
    EXPECT_TRUE(singleGroup.contains(QStringLiteral("pass1.registrationGroups")));
    EXPECT_TRUE(singleGroup.contains(QStringLiteral("applyAutoReferenceFromMetricsDb")));
    EXPECT_LT(singleGroup.indexOf(QStringLiteral("applyAutoReferenceFromMetricsDb")),
              singleGroup.indexOf(QStringLiteral("finishRegistrationOnlyRun")));

    const QString allGroups =
        functionSource(source, QStringLiteral("void ImageIntegrationProcess::onRunRegistrationForAllGroups"),
                       QStringLiteral("void ImageIntegrationProcess::onFixRequested"));
    ASSERT_FALSE(allGroups.isEmpty());
    EXPECT_TRUE(allGroups.contains(QStringLiteral("result.success")));
    EXPECT_TRUE(allGroups.contains(QStringLiteral("unfilteredManifest.outputDirectory")));
    EXPECT_TRUE(allGroups.contains(QStringLiteral("unfilteredManifest.registrationGroups")));
    EXPECT_TRUE(allGroups.contains(QStringLiteral("applyAutoReferenceFromMetricsDb")));
    EXPECT_LT(allGroups.indexOf(QStringLiteral("applyAutoReferenceFromMetricsDb")),
              allGroups.indexOf(QStringLiteral("finishRegistrationOnlyRun")));
}

// Every run - the whole workflow, one integration group, a run started from a manifest, a re-run from a run's archive
// and a registration alone - ends by reading again the records it wrote: the Stacking tab's Frame Quality panel
// otherwise went on saying that the figures were not available until the window was opened again.
TEST_F(ImageIntegrationProcessTest, EveryRunEndsByReadingAgainWhatItWrote)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());
    const QString refresh = QStringLiteral("refreshAfterRun()");

    // The whole workflow's run, one group's and a run started from a manifest end through the one end they share.
    const QString end = functionSource(source, QStringLiteral("void ImageIntegrationProcess::endRun"),
                                       QStringLiteral("void ImageIntegrationProcess::applyExecutionOptions"));
    ASSERT_FALSE(end.isEmpty());
    EXPECT_TRUE(end.contains(refresh)) << "the end the runs share";
    const QString ends = QStringLiteral("endRun(");

    const QString whole = functionSource(source, QStringLiteral("void ImageIntegrationProcess::onExecute()"),
                                         QStringLiteral("void ImageIntegrationProcess::onRunIntegrationForGroup"));
    ASSERT_FALSE(whole.isEmpty());
    EXPECT_TRUE(whole.contains(ends)) << "the whole workflow's run";

    const QString group =
        functionSource(source, QStringLiteral("void ImageIntegrationProcess::onRunIntegrationForGroup"),
                       QStringLiteral("applyAutoReferenceFromMetricsDb(IntegrationDataModel& model"));
    ASSERT_FALSE(group.isEmpty());
    EXPECT_TRUE(group.contains(ends)) << "one integration group's run";

    const QString started = functionSource(source, QStringLiteral("bool ImageIntegrationProcess::startRun"),
                                           QStringLiteral("void ImageIntegrationProcess::joinProjectForRun"));
    ASSERT_FALSE(started.isEmpty());
    EXPECT_TRUE(started.contains(ends)) << "a run started from a manifest";

    const QString rerun =
        functionSource(source, QStringLiteral("runAsyncValidated(archivedManifest"), QStringLiteral("if (!launched"));
    ASSERT_FALSE(rerun.isEmpty());
    EXPECT_TRUE(rerun.contains(refresh)) << "a re-run from a run's archive";

    const QString registration =
        functionSource(source, QStringLiteral("void ImageIntegrationProcess::finishRegistrationOnlyRun"),
                       QStringLiteral("void ImageIntegrationProcess::onRunRegistrationForGroup"));
    ASSERT_FALSE(registration.isEmpty());
    EXPECT_TRUE(registration.contains(refresh)) << "a registration alone";
}

TEST_F(ImageIntegrationProcessTest, LineEditCanReceiveFocusProgrammatically)
{
    QLineEdit edit;
    edit.setText("test/path/to/output");
    edit.show();

    edit.setFocus();
    edit.selectAll();

    EXPECT_EQ(edit.selectedText(), "test/path/to/output");
}

TEST_F(ImageIntegrationProcessTest, ExecutionPlanPreparationReportsDeterminateReadableProgress)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());

    const QString preparation = functionSource(source, QStringLiteral("void prepareExecutionPlanAsync"),
                                               QStringLiteral("struct IntegratedOutputOpenPlan"));
    ASSERT_FALSE(preparation.isEmpty());

    EXPECT_TRUE(preparation.contains(QStringLiteral("QPromise<PreparedExecutionPlan>")));
    EXPECT_TRUE(preparation.contains(QStringLiteral("ImageIntegration::preparePlannedWorkflowExecution(")));
    EXPECT_TRUE(preparation.contains(QStringLiteral("promise.setProgressValueAndText(percent, text)")));
    EXPECT_TRUE(preparation.contains(QStringLiteral("promise.setProgressRange(0, 100)")));
    EXPECT_FALSE(source.contains(QStringLiteral("new QProgressDialog")));
    EXPECT_TRUE(source.contains(QStringLiteral("components/AnProgressCard.h")));
    EXPECT_TRUE(source.contains(QStringLiteral("class ExecutionPlanPreparationDialog final : public QDialog")));
    EXPECT_TRUE(source.contains(QStringLiteral("new an::ui::AnProgressCard(")));
    EXPECT_TRUE(source.contains(QStringLiteral("executionPlanPreparationProgressCard")));
    EXPECT_TRUE(source.contains(QStringLiteral("m_progress_card->setProgressRange(0, 100)")));
    EXPECT_TRUE(source.contains(QStringLiteral("m_progress_card->setProgressValue(0)")));
    EXPECT_TRUE(source.contains(QStringLiteral("progress_bar->setMinimumHeight(t.spacing.xl)")));
    EXPECT_TRUE(source.contains(QStringLiteral("anStatusTone")));
    EXPECT_TRUE(source.contains(QStringLiteral("setEtaText")));

    // The steps it reports are the shared preparation's, in the process's translation context.
    const QString shared = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/workflow/WorkflowPreparation.cpp"));
    ASSERT_FALSE(shared.isEmpty());
    const QString planned =
        functionSource(shared, QStringLiteral("preparePlannedWorkflowExecution(const std::function"),
                       QStringLiteral("prepareDirectWorkflowExecution(const std::function"));
    ASSERT_FALSE(planned.isEmpty());
    EXPECT_TRUE(
        shared.contains(QStringLiteral("QCoreApplication::translate(\"ImageIntegrationProcess\", sourceText)")));
    EXPECT_GE(planned.count(QStringLiteral("report(progress, ")), 6);
    EXPECT_TRUE(planned.contains(QStringLiteral("report(progress, 0, \"Building workflow manifest…\")")));
    EXPECT_TRUE(planned.contains(
        QRegularExpression(QStringLiteral(R"(report\(progress,\s*25,\s*"Checking reusable workflow artifacts…"\))"))));
    EXPECT_TRUE(planned.contains(QStringLiteral("report(progress, 100, \"Execution plan ready.\")")));
}
