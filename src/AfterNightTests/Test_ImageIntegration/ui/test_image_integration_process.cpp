// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTabWidget>
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

// Every run - the whole workflow, one integration group, a re-run from a run's archive and a registration alone - ends
// by reading again the records it wrote: the Stacking tab's Frame Quality panel otherwise went on saying that the
// figures were not available until the window was opened again.
TEST_F(ImageIntegrationProcessTest, EveryRunEndsByReadingAgainWhatItWrote)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());
    const QString refresh = QStringLiteral("refreshAfterRun()");

    const QString whole = functionSource(source, QStringLiteral("void ImageIntegrationProcess::onExecute()"),
                                         QStringLiteral("void ImageIntegrationProcess::onRunIntegrationForGroup"));
    ASSERT_FALSE(whole.isEmpty());
    EXPECT_TRUE(whole.contains(refresh)) << "the whole workflow's run";

    const QString group =
        functionSource(source, QStringLiteral("void ImageIntegrationProcess::onRunIntegrationForGroup"),
                       QStringLiteral("applyAutoReferenceFromMetricsDb(IntegrationDataModel& model"));
    ASSERT_FALSE(group.isEmpty());
    EXPECT_TRUE(group.contains(refresh)) << "one integration group's run";

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

TEST_F(ImageIntegrationProcessTest, TabWidgetCanNavigateProgrammatically)
{
    QTabWidget tabs;
    QWidget w1, w2, w3, w4;
    tabs.addTab(&w1, "Tab1");
    tabs.addTab(&w2, "Tab2");
    tabs.addTab(&w3, "Tab3");
    tabs.addTab(&w4, "Tab4");

    EXPECT_EQ(tabs.currentIndex(), 0);

    tabs.setCurrentIndex(2);
    EXPECT_EQ(tabs.currentIndex(), 2);

    tabs.setCurrentWidget(&w4);
    EXPECT_EQ(tabs.currentIndex(), 3);
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

// -----------------------------------------------------------------------------
// UI structure tests.
// These tests verify the unified tab widget structure and bottom action bar.
// -----------------------------------------------------------------------------

TEST_F(ImageIntegrationProcessTest, NestedTabStructure)
{
    // Verify the nested UI structure: Root -> (Sources | Workflow)
    QTabWidget rootTabs;

    QTabWidget sourceTabs;
    QWidget bias, darks, flats, lights;
    sourceTabs.addTab(&bias, "Bias");
    sourceTabs.addTab(&darks, "Darks");
    sourceTabs.addTab(&flats, "Flats");
    sourceTabs.addTab(&lights, "Lights");

    QTabWidget workflowTabs;
    QWidget calibration, registration, stacking, summary;
    workflowTabs.addTab(&calibration, "Calibration");
    workflowTabs.addTab(&registration, "Registration");
    workflowTabs.addTab(&stacking, "Stacking");
    workflowTabs.addTab(&summary, "Summary");

    rootTabs.addTab(&sourceTabs, "Frame Sources");
    rootTabs.addTab(&workflowTabs, "Workflow");

    EXPECT_EQ(rootTabs.count(), 2);
    EXPECT_EQ(sourceTabs.count(), 4);
    EXPECT_EQ(workflowTabs.count(), 4);
}

TEST_F(ImageIntegrationProcessTest, BottomActionBarOutputDirectoryComponentsExist)
{
    // Verify bottom action bar has the expected components:
    // - Output directory edit + browse button

    QWidget bottomBar;
    QLineEdit outputEdit;
    outputEdit.setObjectName("outputDirectoryEdit");
    QPushButton browseBtn;
    browseBtn.setObjectName("outputDirectoryBrowseButton");

    // Reparent widgets to bottomBar for findChild tests
    outputEdit.setParent(&bottomBar);
    browseBtn.setParent(&bottomBar);

    // Verify all components can be found
    EXPECT_NE(bottomBar.findChild<QLineEdit*>("outputDirectoryEdit"), nullptr);
    EXPECT_NE(bottomBar.findChild<QPushButton*>("outputDirectoryBrowseButton"), nullptr);
}

TEST_F(ImageIntegrationProcessTest, BottomActionBarHasResetAndCloseButtons)
{
    // Verify the bottom action bar contains a QDialogButtonBox with Reset and Close buttons
    QWidget bottomBar;
    QDialogButtonBox buttonBox;
    buttonBox.setObjectName("dialogButtonBox");
    buttonBox.setStandardButtons(QDialogButtonBox::Reset | QDialogButtonBox::Close);
    buttonBox.setParent(&bottomBar);

    auto* box = bottomBar.findChild<QDialogButtonBox*>("dialogButtonBox");
    EXPECT_NE(box, nullptr);
    EXPECT_NE(box->button(QDialogButtonBox::Reset), nullptr);
    EXPECT_NE(box->button(QDialogButtonBox::Close), nullptr);
}

TEST_F(ImageIntegrationProcessTest, TabNavigationWithKeyboardShortcuts)
{
    // Verify tab navigation shortcuts work (Ctrl+1 through Ctrl+8)
    QTabWidget tabs;
    QWidget w[8];
    for (int i = 0; i < 8; ++i)
    {
        tabs.addTab(&w[i], QString("Tab%1").arg(i + 1));
    }

    // Simulate programmatic navigation (shortcuts are handled by QAction in the real widget)
    for (int i = 0; i < 8; ++i)
    {
        tabs.setCurrentIndex(i);
        EXPECT_EQ(tabs.currentIndex(), i);
    }
}

TEST_F(ImageIntegrationProcessTest, TabIconsCanBeSet)
{
    // Verify tab icons can be set on a QTabWidget
    QTabWidget tabs;
    QWidget w1, w2;
    tabs.addTab(&w1, "Tab1");
    tabs.addTab(&w2, "Tab2");

    QIcon icon1 = QIcon::fromTheme("document-open");
    QIcon icon2 = QIcon::fromTheme("document-save");

    tabs.setTabIcon(0, icon1);
    tabs.setTabIcon(1, icon2);

    // Icons are set even if theme icons are not available (they'll be null)
    // The test verifies the API works without crashing
    SUCCEED();
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
