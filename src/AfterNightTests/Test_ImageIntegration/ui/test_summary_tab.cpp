// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QTemporaryDir>
#include <QTabBar>
#include <QToolButton>
#include <QListView>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <optional>

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/SummaryTab.h"
#include "processes/ImageIntegration/widgets/TabIssueBadge.h"

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

void waitForRefresh(SummaryTab* tab)
{
    QSignalSpy spy(tab, &SummaryTab::refreshed);
    tab->refreshAll();
    ASSERT_TRUE(spy.wait(2000));
    QCoreApplication::processEvents();
}

FrameInfo createLight(const QString& path, const std::optional<FrameSampleLayout> layout = std::nullopt)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 1.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("TestObject"), QDateTime::currentDateTimeUtc(), QString(),
                       layout);
    return FrameInfo(FrameId(path), path, meta);
}

/// The disk estimate's tooltip for two 1000 x 1000 lights of one layout, their light calibration kept on disk.
QString diskEstimateFor(IntegrationDataModel* model, SummaryTab* tab, const QString& directory,
                        const std::optional<FrameSampleLayout> layout)
{
    model->setOutputDirectory(directory);
    model->setLightCalibrationCacheMode(LightCalibrationCacheMode::Disk);
    // The files need not exist: what the run writes is read from the frames' figures, not from the files' sizes.
    model->addFrames(FrameType::Light, {createLight(QDir(directory).filePath(QStringLiteral("light1.cr2")), layout),
                                        createLight(QDir(directory).filePath(QStringLiteral("light2.cr2")), layout)});
    waitForRefresh(tab);
    auto* resource = tab->findChild<QLabel*>(QStringLiteral("lblResourceStatus"));
    return resource != nullptr ? resource->toolTip() : QString();
}
} // namespace

class SummaryTabTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureApp();
        model = new IntegrationDataModel();
        tab = new SummaryTab(model);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    SummaryTab* tab = nullptr;
};

TEST_F(SummaryTabTest, InitialStateNotReadyAndExecuteDisabled)
{
    auto* title = tab->findChild<QLabel*>(QStringLiteral("lblStatusTitle"));
    ASSERT_TRUE(title != nullptr);
    EXPECT_TRUE(title->text().contains(QStringLiteral("Cannot")));

    auto* issues = tab->findChild<QWidget*>(QStringLiteral("statusIssuesWidget"));
    ASSERT_TRUE(issues != nullptr);
    EXPECT_FALSE(issues->isHidden());

    auto* execute = tab->findChild<QPushButton*>(QStringLiteral("btnExecute"));
    ASSERT_TRUE(execute != nullptr);
    EXPECT_FALSE(execute->isEnabled());
}

TEST_F(SummaryTabTest, ExecuteRequestedEmittedWhenReady)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    model->setOutputDirectory(temp_dir.path());
    model->addFrames(FrameType::Light, {createLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    waitForRefresh(tab);

    auto* issues = tab->findChild<QWidget*>(QStringLiteral("statusIssuesWidget"));
    ASSERT_TRUE(issues != nullptr);
    EXPECT_TRUE(issues->isHidden());

    auto* execute = tab->findChild<QPushButton*>(QStringLiteral("btnExecute"));
    ASSERT_TRUE(execute != nullptr);
    ASSERT_TRUE(execute->isEnabled());

    QSignalSpy spy(tab, &SummaryTab::executeRequested);
    execute->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(SummaryTabTest, FixButtonEmitsFixRequested)
{
    waitForRefresh(tab);

    auto* issues = tab->findChild<QWidget*>(QStringLiteral("statusIssuesWidget"));
    ASSERT_TRUE(issues != nullptr);
    ASSERT_FALSE(issues->isHidden());

    QToolButton* fixButton = nullptr;
    const auto fixButtons = issues->findChildren<QToolButton*>();
    for (auto* b : fixButtons)
    {
        if (b && b->text() == QStringLiteral("Go to"))
        {
            fixButton = b;
            break;
        }
    }
    ASSERT_TRUE(fixButton != nullptr);

    QSignalSpy spy(tab, &SummaryTab::fixRequested);
    fixButton->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(SummaryTabTest, IssueCardClickEmitsFixRequested)
{
    tab->resize(900, 600);
    tab->show();
    waitForRefresh(tab);

    auto* issues = tab->findChild<QWidget*>(QStringLiteral("statusIssuesWidget"));
    ASSERT_TRUE(issues != nullptr);
    ASSERT_FALSE(issues->isHidden());

    QToolButton* fixButton = nullptr;
    const auto fixButtons = issues->findChildren<QToolButton*>();
    for (auto* b : fixButtons)
    {
        if (b && b->text() == QStringLiteral("Go to"))
        {
            fixButton = b;
            break;
        }
    }
    ASSERT_TRUE(fixButton != nullptr);

    auto* card = qobject_cast<QFrame*>(fixButton->parentWidget());
    ASSERT_TRUE(card != nullptr);
    EXPECT_EQ(card->objectName(), QStringLiteral("statusIssueCard"));

    QSignalSpy spy(tab, &SummaryTab::fixRequested);
    QTest::mouseClick(card, Qt::LeftButton, Qt::NoModifier, card->rect().center());
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(SummaryTabTest, RunHistoryUsesScrollAreaExtraSpace)
{
    auto* scrollLayout = tab->findChild<QVBoxLayout*>(QStringLiteral("scrollContentLayout"));
    ASSERT_TRUE(scrollLayout != nullptr);

    auto* runHistoryFrame = tab->findChild<QFrame*>(QStringLiteral("runHistoryFrame"));
    ASSERT_TRUE(runHistoryFrame != nullptr);
    const int runHistoryIndex = scrollLayout->indexOf(runHistoryFrame);
    ASSERT_GE(runHistoryIndex, 0);
    EXPECT_GT(scrollLayout->stretch(runHistoryIndex), 0);

    for (int i = 0; i < scrollLayout->count(); ++i)
    {
        auto* item = scrollLayout->itemAt(i);
        ASSERT_TRUE(item != nullptr);
        if (const auto* spacer = item->spacerItem())
        {
            EXPECT_FALSE(spacer->expandingDirections().testFlag(Qt::Vertical));
        }
    }

    auto* historyView = tab->findChild<QTreeView*>();
    ASSERT_TRUE(historyView != nullptr);
    EXPECT_EQ(historyView->sizePolicy().verticalPolicy(), QSizePolicy::Policy::Expanding);

    auto* runHistoryLayout = qobject_cast<QVBoxLayout*>(runHistoryFrame->layout());
    ASSERT_TRUE(runHistoryLayout != nullptr);
    const int historyViewIndex = runHistoryLayout->indexOf(historyView);
    ASSERT_GE(historyViewIndex, 0);
    EXPECT_GT(runHistoryLayout->stretch(historyViewIndex), 0);
}

TEST(TabIssueBadgeTest, ClickSelectsOwningTab)
{
    ensureApp();

    QTabBar bar;
    bar.addTab(QStringLiteral("Frames"));
    bar.addTab(QStringLiteral("Summary"));

    auto* badge = new TabIssueBadge(&bar);
    badge->setText(QStringLiteral("2"));
    bar.setTabButton(1, QTabBar::RightSide, badge);
    bar.setCurrentIndex(0);

    bar.show();
    QCoreApplication::processEvents();

    QTest::mouseClick(badge, Qt::LeftButton, Qt::NoModifier, badge->rect().center());
    EXPECT_EQ(bar.currentIndex(), 1);
}

TEST_F(SummaryTabTest, ResourceStatusShowsDiskEstimateTooltipWhenOutputDirSet)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());

    model->setOutputDirectory(temp_dir.path());
    model->addFrames(FrameType::Light, {createLight(QDir(temp_dir.path()).filePath(QStringLiteral("light1.fit")))});

    waitForRefresh(tab);

    auto* resource = tab->findChild<QLabel*>(QStringLiteral("lblResourceStatus"));
    ASSERT_TRUE(resource != nullptr);
    EXPECT_TRUE(resource->text().contains(QStringLiteral("Disk space:")));
    EXPECT_TRUE(resource->toolTip().contains(QStringLiteral("Available:")));
}

TEST_F(SummaryTabTest, TheDiskEstimateWeighsAColourMosaicAsTheFilesTheRunWrites)
{
    // A colour camera's light is one mosaic plane, however small its raw file: calibrated it is that plane in 32-bit
    // floats (4 MB at 1000 x 1000), registered and stacked it is three demosaiced channels (12 MB).
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString tooltip =
        diskEstimateFor(model, tab, temp_dir.path(), FrameSampleLayout{.channels = 1, .mosaic = true});

    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Calibrated lights (2): ~7.6 MB"))) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Registered lights (2): ~22.9 MB"))) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Stacked outputs (1): ~11.4 MB"))) << tooltip.toStdString();
}

TEST_F(SummaryTabTest, TheDiskEstimateWeighsAMonochromeLightAsOnePlane)
{
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString tooltip =
        diskEstimateFor(model, tab, temp_dir.path(), FrameSampleLayout{.channels = 1, .mosaic = false});

    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Calibrated lights (2): ~7.6 MB"))) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Registered lights (2): ~7.6 MB"))) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Stacked outputs (1): ~3.8 MB"))) << tooltip.toStdString();
}

TEST_F(SummaryTabTest, ALightWhoseLayoutIsNotRecordedCountsAsAColourMosaic)
{
    // A project saved before the import recorded the layout: the heavier reading, and the tooltip says so.
    QTemporaryDir temp_dir;
    ASSERT_TRUE(temp_dir.isValid());
    const QString tooltip = diskEstimateFor(model, tab, temp_dir.path(), std::nullopt);

    EXPECT_TRUE(tooltip.contains(QStringLiteral("- Registered lights (2): ~22.9 MB"))) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("count as color mosaics"))) << tooltip.toStdString();
}
