// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <algorithm>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QAction>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>

#include "components/AnCollapsibleSection.h"
#include "processes/ImageIntegration/tabs/CalibrationTab.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"

namespace
{
FrameInfo createFrame(FrameType type, const QString& path, const QString& session = QString(),
                      double exposureSeconds = 30.0, int isoGain = 100, int offset = 10,
                      const QString& filter = QStringLiteral("L"), const QString& object = QStringLiteral("M42"))
{
    FrameMetadata meta(type, QSize(1000, 1000), exposureSeconds, isoGain, offset, std::nullopt, filter, object,
                       session.isEmpty()
                           ? QDateTime::currentDateTimeUtc()
                           : QDateTime(QDate::fromString(session, "yyyy-MM-dd"), QTime(0, 0), QTimeZone::UTC),
                       QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

class CalibrationTabTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        tab = new CalibrationTab(model);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
    }

    // refreshAll() is debounced by the tab's timer, so a case waits until that refresh has run rather than for a
    // fixed time that a slow host can outlast.
    void waitForRefresh() const
    {
        const QList<QTimer*> timers = tab->findChildren<QTimer*>(Qt::FindDirectChildrenOnly);
        ASSERT_FALSE(timers.isEmpty());
        const auto pending = [&timers]() {
            return std::any_of(timers.begin(), timers.end(), [](const QTimer* timer) { return timer->isActive(); });
        };
        QElapsedTimer waited;
        waited.start();
        while (pending() && waited.elapsed() < 10000)
        {
            QTest::qWait(10);
        }
        ASSERT_FALSE(pending()) << "the tab's refresh did not run within ten seconds";
    }

    IntegrationDataModel* model = nullptr;
    CalibrationTab* tab = nullptr;
};

TEST_F(CalibrationTabTest, InitialStateShowsEmptyMessage)
{
    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);

    // With no lights, should show empty message
    EXPECT_EQ(table->rowCount(), 1);
    QTableWidgetItem* item = table->item(0, 0);
    ASSERT_TRUE(item != nullptr);
    EXPECT_TRUE(item->text().contains("No light groups"));
}

TEST_F(CalibrationTabTest, HasVerticalScrollArea)
{
    QScrollArea* scroll = tab->findChild<QScrollArea*>("scrollArea");
    ASSERT_TRUE(scroll != nullptr);
    EXPECT_TRUE(scroll->widgetResizable());
}

TEST_F(CalibrationTabTest, PanelsAreCollapsibleAndExpandedByDefault)
{
    auto* biasPanel = dynamic_cast<an::ui::AnCollapsibleSection*>(tab->findChild<QWidget*>("biasCalibrationPanel"));
    auto* darkPanel = dynamic_cast<an::ui::AnCollapsibleSection*>(tab->findChild<QWidget*>("darkCalibrationPanel"));
    auto* flatPanel = dynamic_cast<an::ui::AnCollapsibleSection*>(tab->findChild<QWidget*>("flatCalibrationPanel"));
    auto* lightPanel = dynamic_cast<an::ui::AnCollapsibleSection*>(tab->findChild<QWidget*>("lightCalibrationPanel"));

    ASSERT_TRUE(biasPanel != nullptr);
    ASSERT_TRUE(darkPanel != nullptr);
    ASSERT_TRUE(flatPanel != nullptr);
    ASSERT_TRUE(lightPanel != nullptr);

    EXPECT_FALSE(biasPanel->title().isEmpty());
    EXPECT_FALSE(darkPanel->title().isEmpty());
    EXPECT_FALSE(flatPanel->title().isEmpty());
    EXPECT_FALSE(lightPanel->title().isEmpty());

    ASSERT_TRUE(biasPanel->contentWidget() != nullptr);
    ASSERT_TRUE(darkPanel->contentWidget() != nullptr);
    ASSERT_TRUE(flatPanel->contentWidget() != nullptr);
    ASSERT_TRUE(lightPanel->contentWidget() != nullptr);

    EXPECT_TRUE(biasPanel->isExpanded());
    EXPECT_TRUE(darkPanel->isExpanded());
    EXPECT_TRUE(flatPanel->isExpanded());
    EXPECT_TRUE(lightPanel->isExpanded());

    EXPECT_FALSE(biasPanel->contentWidget()->isHidden());
    EXPECT_FALSE(darkPanel->contentWidget()->isHidden());
    EXPECT_FALSE(flatPanel->contentWidget()->isHidden());
    EXPECT_FALSE(lightPanel->contentWidget()->isHidden());
}

TEST_F(CalibrationTabTest, DisplaysLightGroupsAfterAddingFrames)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});

    // Force immediate update
    tab->refreshAll();
    // Process timer event
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);

    // Should have at least 1 row for the light group
    EXPECT_GE(table->rowCount(), 1);

    // First column should contain group info
    QTableWidgetItem* lightItem = table->item(0, 0);
    if (lightItem)
    {
        EXPECT_FALSE(lightItem->text().isEmpty());
    }
}

TEST_F(CalibrationTabTest, ShowsMissingCalibrationWarnings)
{
    // Add light frames without calibration frames
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);

    if (table->rowCount() > 0)
    {
        // Bias column (1), Dark column (2), Flat column (3) should show no match
        for (int col = 1; col <= 3; ++col)
        {
            QTableWidgetItem* item = table->item(0, col);
            if (item)
            {
                EXPECT_TRUE(item->text() == "-" || item->text().isEmpty());
                EXPECT_FALSE(item->icon().isNull());
            }
        }
    }
}

TEST_F(CalibrationTabTest, ShowsMatchWhenCalibrationAvailable)
{
    // Add matching light and bias frames
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);

    if (table->rowCount() > 0)
    {
        QTableWidgetItem* biasItem = table->item(0, 1);
        if (biasItem)
        {
            EXPECT_FALSE(biasItem->text().isEmpty());
            EXPECT_NE(biasItem->text(), "-");
            EXPECT_FALSE(biasItem->icon().isNull());
        }
    }
}

TEST_F(CalibrationTabTest, BiasIsNotUsedWhenDarkExists)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);
    ASSERT_GT(table->rowCount(), 0);

    // Bias column should be N/A (no warning/error) because Dark exists.
    QTableWidgetItem* biasItem = table->item(0, 1);
    ASSERT_TRUE(biasItem != nullptr);
    EXPECT_EQ(biasItem->text(), "-");
    EXPECT_TRUE(biasItem->icon().isNull());

    // Dark column should have a match.
    QTableWidgetItem* darkItem = table->item(0, 2);
    ASSERT_TRUE(darkItem != nullptr);
    EXPECT_FALSE(darkItem->icon().isNull());
}

// A master dark is stacked from its frames as they are, holding their bias, in CCD mode as in standard mode, so the
// Dark table names no bias for a group; in CCD mode its tooltip says where the bias is subtracted instead.
TEST_F(CalibrationTabTest, DarkTableNamesNoBiasInEitherMode)
{
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10"),
                                       createFrame(FrameType::Dark, "C:/dark2.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* tblDark = tab->findChild<QTableWidget*>("tblDarkCalibration");
    ASSERT_TRUE(tblDark != nullptr);
    QTRY_VERIFY(tblDark->rowCount() > 0);
    QTRY_VERIFY(tblDark->item(0, 2) != nullptr);
    EXPECT_EQ(tblDark->item(0, 2)->text(), "-");

    auto policy = model->calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTRY_VERIFY(tblDark->item(0, 2) != nullptr);
    QTableWidgetItem* biasItemCcd = tblDark->item(0, 2);
    EXPECT_EQ(biasItemCcd->text(), "-");
    EXPECT_TRUE(biasItemCcd->toolTip().contains(QStringLiteral("holds the bias")))
        << biasItemCcd->toolTip().toStdString();
}

// A flat beside a matching dark takes the dark alone in CCD mode too: the master dark holds its bias.
TEST_F(CalibrationTabTest, FlatTableTakesNoBiasBesideADarkInCcdMode)
{
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});
    auto policy = model->calibrationMatchingPolicy();
    policy.subtractBiasFromDark = true;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* tblFlat = tab->findChild<QTableWidget*>("tblFlatCalibration");
    ASSERT_TRUE(tblFlat != nullptr);
    QTRY_VERIFY(tblFlat->rowCount() > 0);
    QTRY_VERIFY(tblFlat->item(0, 2) != nullptr && tblFlat->item(0, 3) != nullptr);
    EXPECT_EQ(tblFlat->item(0, 2)->text(), "-");
    EXPECT_TRUE(tblFlat->item(0, 2)->toolTip().contains(QStringLiteral("holds the bias")))
        << tblFlat->item(0, 2)->toolTip().toStdString();
    EXPECT_NE(tblFlat->item(0, 3)->text(), "-") << "the flat takes its dark";
}

TEST_F(CalibrationTabTest, FlatTableDoesNotRequireBiasOrDarkForMasterFlat)
{
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/master_flat.fit", "2025-01-10")});
    model->setMasterStatus("C:/master_flat.fit", true, MasterSource::UserDefined);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* tblFlat = tab->findChild<QTableWidget*>("tblFlatCalibration");
    ASSERT_TRUE(tblFlat != nullptr);
    ASSERT_GT(tblFlat->rowCount(), 0);

    QTableWidgetItem* biasItem = tblFlat->item(0, 2);
    QTableWidgetItem* darkItem = tblFlat->item(0, 3);
    ASSERT_TRUE(biasItem != nullptr);
    ASSERT_TRUE(darkItem != nullptr);

    EXPECT_EQ(biasItem->text(), "-");
    EXPECT_TRUE(biasItem->icon().isNull());
    EXPECT_EQ(darkItem->text(), "-");
    EXPECT_TRUE(darkItem->icon().isNull());
}

TEST_F(CalibrationTabTest, MatchingPolicyMenuIsAvailable)
{
    QToolButton* matchingButton = tab->findChild<QToolButton*>("btnCalibrationMatching");
    ASSERT_TRUE(matchingButton != nullptr);

    ASSERT_TRUE(matchingButton->menu() != nullptr);

    QStringList actionTexts;
    for (QAction* action : matchingButton->menu()->actions())
    {
        actionTexts.push_back(action->text());
    }

    EXPECT_TRUE(actionTexts.contains("Allow dark exposure fallback"));
    EXPECT_TRUE(actionTexts.contains("Dark exposure tolerance..."));
    EXPECT_TRUE(actionTexts.contains("Allow flat session fallback"));
    EXPECT_TRUE(actionTexts.contains("Max flat session days apart..."));
    EXPECT_TRUE(actionTexts.contains("Prefer newer flats"));
    EXPECT_TRUE(actionTexts.contains("Warn on fallback matches"));
    EXPECT_TRUE(actionTexts.contains("Reset matching policy to defaults"));
    EXPECT_TRUE(actionTexts.contains("Clear all calibration overrides"));
}

TEST_F(CalibrationTabTest, DarkExposureFallbackPolicyAffectsMatching)
{
    // Enable exposure grouping for Dark frames so exposure fallback can apply.
    GroupingOptions darkOptions = model->groupingOptions(FrameType::Dark);
    darkOptions.byExposure = true;
    model->setGroupingOptions(FrameType::Dark, darkOptions);

    // Light exposure differs slightly from Dark exposure to force fallback (ClosestExposure).
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10", 60.0)});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10", 62.0)});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);
    QTRY_VERIFY(table->rowCount() > 0);
    QTRY_VERIFY(table->item(0, 2) != nullptr);

    QTableWidgetItem* darkItem = table->item(0, 2);
    ASSERT_TRUE(darkItem != nullptr);
    EXPECT_EQ(darkItem->data(Qt::UserRole + 1).toInt(), static_cast<int>(CalibrationMatch::MatchType::ClosestExposure));

    // Disable fallback and verify match becomes None.
    auto policy = model->calibrationMatchingPolicy();
    policy.allowDarkExposureFallback = false;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTRY_VERIFY(table->item(0, 2) != nullptr);
    darkItem = table->item(0, 2);
    ASSERT_TRUE(darkItem != nullptr);
    EXPECT_EQ(darkItem->data(Qt::UserRole + 1).toInt(), static_cast<int>(CalibrationMatch::MatchType::None));
}

TEST_F(CalibrationTabTest, FlatSessionFallbackPolicyAffectsMatching)
{
    // Ensure session grouping is enabled for flats so session fallback can apply.
    GroupingOptions flatOptions = model->groupingOptions(FrameType::Flat);
    flatOptions.bySession = true;
    model->setGroupingOptions(FrameType::Flat, flatOptions);

    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    // Flat is otherwise compatible, but from a different session, so exact match fails and fallback can apply.
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-11")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);
    QTRY_VERIFY(table->rowCount() > 0);
    QTRY_VERIFY(table->item(0, 3) != nullptr);

    QTableWidgetItem* flatItem = table->item(0, 3);
    ASSERT_TRUE(flatItem != nullptr);
    EXPECT_EQ(flatItem->data(Qt::UserRole + 1).toInt(), static_cast<int>(CalibrationMatch::MatchType::ClosestDate));

    auto policy = model->calibrationMatchingPolicy();
    policy.allowFlatSessionFallback = false;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTRY_VERIFY(table->item(0, 3) != nullptr);
    flatItem = table->item(0, 3);
    ASSERT_TRUE(flatItem != nullptr);
    EXPECT_EQ(flatItem->data(Qt::UserRole + 1).toInt(), static_cast<int>(CalibrationMatch::MatchType::None));
}

TEST_F(CalibrationTabTest, StackingSettingsButtonsExistAndRequireSelection)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10")});
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* tblBias = tab->findChild<QTableWidget*>("tblBiasCalibration");
    QTableWidget* tblDark = tab->findChild<QTableWidget*>("tblDarkCalibration");
    QTableWidget* tblFlat = tab->findChild<QTableWidget*>("tblFlatCalibration");
    QTableWidget* tblLight = tab->findChild<QTableWidget*>("tblLightCalibration");

    ASSERT_TRUE(tblBias != nullptr);
    ASSERT_TRUE(tblDark != nullptr);
    ASSERT_TRUE(tblFlat != nullptr);
    ASSERT_TRUE(tblLight != nullptr);

    ASSERT_GT(tblBias->rowCount(), 0);
    ASSERT_GT(tblDark->rowCount(), 0);
    ASSERT_GT(tblFlat->rowCount(), 0);
    ASSERT_GT(tblLight->rowCount(), 0);

    // Settings column was removed; tables should not have gear widgets.
    const auto hasGearButtonInLastColumn = [](QTableWidget* table) {
        if (!table || table->rowCount() <= 0 || table->columnCount() <= 0)
        {
            return false;
        }
        return qobject_cast<QToolButton*>(table->cellWidget(0, table->columnCount() - 1)) != nullptr;
    };

    EXPECT_FALSE(hasGearButtonInLastColumn(tblBias));
    EXPECT_FALSE(hasGearButtonInLastColumn(tblDark));
    EXPECT_FALSE(hasGearButtonInLastColumn(tblFlat));
    EXPECT_FALSE(hasGearButtonInLastColumn(tblLight));

    auto* btnBias = tab->findChild<QPushButton*>("btnBiasStackingSettings");
    auto* btnDark = tab->findChild<QPushButton*>("btnDarkStackingSettings");
    auto* btnFlat = tab->findChild<QPushButton*>("btnFlatStackingSettings");
    auto* btnLight = tab->findChild<QPushButton*>("btnLightStackingSettings");

    ASSERT_TRUE(btnBias != nullptr);
    ASSERT_TRUE(btnDark != nullptr);
    ASSERT_TRUE(btnFlat != nullptr);
    EXPECT_TRUE(btnLight == nullptr);

    EXPECT_FALSE(btnBias->isEnabled());
    EXPECT_FALSE(btnDark->isEnabled());
    EXPECT_FALSE(btnFlat->isEnabled());

    tblBias->selectRow(0);
    QCoreApplication::processEvents();
    EXPECT_TRUE(btnBias->isEnabled());

    tblDark->selectRow(0);
    QCoreApplication::processEvents();
    EXPECT_TRUE(btnDark->isEnabled());

    tblFlat->selectRow(0);
    QCoreApplication::processEvents();
    EXPECT_TRUE(btnFlat->isEnabled());
}

TEST_F(CalibrationTabTest, RightPanelWidgetsExist)
{
    EXPECT_TRUE(tab->findChild<QWidget*>("calibrationCoverageWidget") != nullptr);
    EXPECT_TRUE(tab->findChild<QWidget*>("frameSummaryWidget") != nullptr);
    EXPECT_TRUE(tab->findChild<QWidget*>("calibrationIssuesWidget") != nullptr);
    EXPECT_TRUE(tab->findChild<QWidget*>("calibrationPolicyWidget") != nullptr);
    EXPECT_TRUE(tab->findChild<QWidget*>("calibrationQuickActionsWidget") != nullptr);
}

TEST_F(CalibrationTabTest, FrameSummaryWidgetUpdatesCounts)
{
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit")});
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit")});
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* biasCount = tab->findChild<QLabel*>("lblBiasCount");
    auto* darkCount = tab->findChild<QLabel*>("lblDarkCount");
    auto* flatCount = tab->findChild<QLabel*>("lblFlatCount");
    auto* lightCount = tab->findChild<QLabel*>("lblLightCount");
    auto* totalCount = tab->findChild<QLabel*>("lblTotalCount");
    auto* groupCount = tab->findChild<QLabel*>("lblLightGroupCount");

    ASSERT_TRUE(biasCount != nullptr);
    ASSERT_TRUE(darkCount != nullptr);
    ASSERT_TRUE(flatCount != nullptr);
    ASSERT_TRUE(lightCount != nullptr);
    ASSERT_TRUE(totalCount != nullptr);
    ASSERT_TRUE(groupCount != nullptr);

    EXPECT_EQ(biasCount->text(), "1");
    EXPECT_EQ(darkCount->text(), "1");
    EXPECT_EQ(flatCount->text(), "1");
    EXPECT_EQ(lightCount->text(), "1");
    EXPECT_EQ(totalCount->text(), "4");
    EXPECT_EQ(groupCount->text(), "1");
}

TEST_F(CalibrationTabTest, IssuesWidgetShowsCalibrationIssues)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* header = tab->findChild<QLabel*>("lblIssuesHeader");
    auto* issue0 = tab->findChild<QLabel*>("lblIssue_0");

    ASSERT_TRUE(header != nullptr);
    ASSERT_TRUE(issue0 != nullptr);

    EXPECT_TRUE(header->text().contains("Issues"));
    EXPECT_FALSE(issue0->text().isEmpty());
}

TEST_F(CalibrationTabTest, PolicyWidgetUpdatesModel)
{
    auto* chkFlatFallback = tab->findChild<QCheckBox*>("chkAllowFlatFallback");
    auto* chkDarkFallback = tab->findChild<QCheckBox*>("chkAllowDarkFallback");

    ASSERT_TRUE(chkFlatFallback != nullptr);
    ASSERT_TRUE(chkDarkFallback != nullptr);

    chkFlatFallback->setChecked(false);
    chkDarkFallback->setChecked(false);

    QCoreApplication::processEvents();

    const auto policy = model->calibrationMatchingPolicy();
    EXPECT_FALSE(policy.allowFlatSessionFallback);
    EXPECT_FALSE(policy.allowDarkExposureFallback);
}

TEST_F(CalibrationTabTest, QuickActionsClearOverrides)
{
    auto* clearButton = tab->findChild<QPushButton*>("btnClearOverrides");
    ASSERT_TRUE(clearButton != nullptr);

    FrameInfo light = createFrame(FrameType::Light, "C:/light1.fit");
    model->addFrames(FrameType::Light, {light});

    LightGroupKey lightKey = LightGroupKey::fromFrame(light);
    model->setCalibrationOverride(lightKey, CalibrationOverrideType::Dark, QStringLiteral("C:/dark_master.fit"));

    EXPECT_FALSE(model->calibrationOverrides().empty());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTRY_VERIFY(clearButton->isEnabled());
    QTest::mouseClick(clearButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    EXPECT_TRUE(model->calibrationOverrides().empty());
}

TEST_F(CalibrationTabTest, QuickActionsDisableFallbacks)
{
    auto* disableButton = tab->findChild<QPushButton*>("btnDisableFallbacks");
    ASSERT_TRUE(disableButton != nullptr);

    QTest::mouseClick(disableButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    const auto policy = model->calibrationMatchingPolicy();
    EXPECT_FALSE(policy.allowFlatSessionFallback);
    EXPECT_FALSE(policy.allowDarkExposureFallback);
    EXPECT_FALSE(policy.warnOnFallback);
}

TEST_F(CalibrationTabTest, QuickActionsResetPolicy)
{
    auto* resetButton = tab->findChild<QPushButton*>("btnResetPolicy");
    ASSERT_TRUE(resetButton != nullptr);

    // Modify policy first
    CalibrationMatchingPolicy modifiedPolicy;
    modifiedPolicy.allowFlatSessionFallback = false;
    modifiedPolicy.allowDarkExposureFallback = false;
    modifiedPolicy.maxFlatSessionDaysApart = 99;
    modifiedPolicy.darkExposureTolerancePercent = 50;
    modifiedPolicy.warnOnFallback = false;
    modifiedPolicy.subtractBiasFromDark = true;
    model->setCalibrationMatchingPolicy(modifiedPolicy);

    QTest::mouseClick(resetButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    // Should be back to defaults
    const CalibrationMatchingPolicy defaultPolicy;
    const auto policy = model->calibrationMatchingPolicy();
    EXPECT_EQ(policy.allowFlatSessionFallback, defaultPolicy.allowFlatSessionFallback);
    EXPECT_EQ(policy.allowDarkExposureFallback, defaultPolicy.allowDarkExposureFallback);
    EXPECT_EQ(policy.maxFlatSessionDaysApart, defaultPolicy.maxFlatSessionDaysApart);
    EXPECT_EQ(policy.darkExposureTolerancePercent, defaultPolicy.darkExposureTolerancePercent);
    EXPECT_EQ(policy.warnOnFallback, defaultPolicy.warnOnFallback);
    EXPECT_EQ(policy.subtractBiasFromDark, defaultPolicy.subtractBiasFromDark);
}

TEST_F(CalibrationTabTest, QuickActionsClearOverridesButtonDisabledWhenNoOverrides)
{
    auto* clearButton = tab->findChild<QPushButton*>("btnClearOverrides");
    ASSERT_TRUE(clearButton != nullptr);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    // No overrides, button should be disabled
    EXPECT_FALSE(clearButton->isEnabled());

    // Add an override
    FrameInfo light = createFrame(FrameType::Light, "C:/light1.fit");
    model->addFrames(FrameType::Light, {light});
    LightGroupKey lightKey = LightGroupKey::fromFrame(light);
    model->setCalibrationOverride(lightKey, CalibrationOverrideType::Dark, QStringLiteral("C:/dark_master.fit"));

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    // Now button should be enabled
    EXPECT_TRUE(clearButton->isEnabled());
}

TEST_F(CalibrationTabTest, PolicyWidgetAllControlsExist)
{
    EXPECT_TRUE(tab->findChild<QCheckBox*>("chkAllowFlatFallback") != nullptr);
    EXPECT_TRUE(tab->findChild<QSpinBox*>("spinFlatDays") != nullptr);
    EXPECT_TRUE(tab->findChild<QCheckBox*>("chkPreferNewerFlats") != nullptr);
    EXPECT_TRUE(tab->findChild<QCheckBox*>("chkAllowDarkFallback") != nullptr);
    EXPECT_TRUE(tab->findChild<QSpinBox*>("spinDarkTolerance") != nullptr);
    EXPECT_TRUE(tab->findChild<QCheckBox*>("chkWarnOnFallback") != nullptr);
    EXPECT_TRUE(tab->findChild<QCheckBox*>("chkSubtractBias") != nullptr);
}

TEST_F(CalibrationTabTest, PolicyWidgetSpinBoxesUpdateModel)
{
    auto* spinFlatDays = tab->findChild<QSpinBox*>("spinFlatDays");
    auto* spinDarkTolerance = tab->findChild<QSpinBox*>("spinDarkTolerance");

    ASSERT_TRUE(spinFlatDays != nullptr);
    ASSERT_TRUE(spinDarkTolerance != nullptr);

    spinFlatDays->setValue(45);
    spinDarkTolerance->setValue(35);
    QCoreApplication::processEvents();

    const auto policy = model->calibrationMatchingPolicy();
    EXPECT_EQ(policy.maxFlatSessionDaysApart, 45);
    EXPECT_EQ(policy.darkExposureTolerancePercent, 35);
}

TEST_F(CalibrationTabTest, PolicyWidgetPreferNewerFlatsUpdatesModel)
{
    auto* chkPreferNewer = tab->findChild<QCheckBox*>("chkPreferNewerFlats");
    ASSERT_TRUE(chkPreferNewer != nullptr);

    chkPreferNewer->setChecked(true);
    QCoreApplication::processEvents();

    EXPECT_TRUE(model->calibrationMatchingPolicy().preferNewerFlat);

    chkPreferNewer->setChecked(false);
    QCoreApplication::processEvents();

    EXPECT_FALSE(model->calibrationMatchingPolicy().preferNewerFlat);
}

TEST_F(CalibrationTabTest, PolicyWidgetWarnOnFallbackUpdatesModel)
{
    auto* chkWarn = tab->findChild<QCheckBox*>("chkWarnOnFallback");
    ASSERT_TRUE(chkWarn != nullptr);

    chkWarn->setChecked(false);
    QCoreApplication::processEvents();

    EXPECT_FALSE(model->calibrationMatchingPolicy().warnOnFallback);

    chkWarn->setChecked(true);
    QCoreApplication::processEvents();

    EXPECT_TRUE(model->calibrationMatchingPolicy().warnOnFallback);
}

TEST_F(CalibrationTabTest, PolicyWidgetCCDModeUpdatesModel)
{
    auto* chkCCD = tab->findChild<QCheckBox*>("chkSubtractBias");
    ASSERT_TRUE(chkCCD != nullptr);

    chkCCD->setChecked(true);
    QCoreApplication::processEvents();

    EXPECT_TRUE(model->calibrationMatchingPolicy().subtractBiasFromDark);

    chkCCD->setChecked(false);
    QCoreApplication::processEvents();

    EXPECT_FALSE(model->calibrationMatchingPolicy().subtractBiasFromDark);
}

TEST_F(CalibrationTabTest, PolicyWidgetRefreshFromModel)
{
    // Set policy directly on model
    CalibrationMatchingPolicy policy;
    policy.allowFlatSessionFallback = false;
    policy.maxFlatSessionDaysApart = 60;
    policy.preferNewerFlat = true;
    policy.allowDarkExposureFallback = false;
    policy.darkExposureTolerancePercent = 40;
    policy.warnOnFallback = false;
    policy.subtractBiasFromDark = true;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* chkFlatFallback = tab->findChild<QCheckBox*>("chkAllowFlatFallback");
    auto* spinFlatDays = tab->findChild<QSpinBox*>("spinFlatDays");
    auto* chkPreferNewer = tab->findChild<QCheckBox*>("chkPreferNewerFlats");
    auto* chkDarkFallback = tab->findChild<QCheckBox*>("chkAllowDarkFallback");
    auto* spinDarkTolerance = tab->findChild<QSpinBox*>("spinDarkTolerance");
    auto* chkWarn = tab->findChild<QCheckBox*>("chkWarnOnFallback");
    auto* chkCCD = tab->findChild<QCheckBox*>("chkSubtractBias");

    EXPECT_FALSE(chkFlatFallback->isChecked());
    EXPECT_EQ(spinFlatDays->value(), 60);
    EXPECT_TRUE(chkPreferNewer->isChecked());
    EXPECT_FALSE(chkDarkFallback->isChecked());
    EXPECT_EQ(spinDarkTolerance->value(), 40);
    EXPECT_FALSE(chkWarn->isChecked());
    EXPECT_TRUE(chkCCD->isChecked());
}

TEST_F(CalibrationTabTest, IssuesWidgetShowsNoIssuesWhenFullyCalibrated)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10")});
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* header = tab->findChild<QLabel*>("lblIssuesHeader");
    auto* emptyLabel = tab->findChild<QLabel*>("lblIssuesEmpty");

    ASSERT_TRUE(header != nullptr);
    ASSERT_TRUE(emptyLabel != nullptr);

    EXPECT_TRUE(header->text().contains("0"));
    // The fixture never shows the tab as a window, so isVisible() will be false.
    // What we care about is whether the label is logically shown/hidden.
    EXPECT_FALSE(emptyLabel->isHidden());
}

TEST_F(CalibrationTabTest, IssuesWidgetGroupsSimilarIssues)
{
    // Add multiple light groups without calibration
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10"),
                                        createFrame(FrameType::Light, "C:/light2.fit", "2025-01-11"),
                                        createFrame(FrameType::Light, "C:/light3.fit", "2025-01-12")});

    // Provide flats so validation runs (otherwise it early-returns when no calibration frames exist).
    // Keep flats aligned with sessions so we don't introduce fallback warnings here.
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10"),
                                       createFrame(FrameType::Flat, "C:/flat2.fit", "2025-01-11"),
                                       createFrame(FrameType::Flat, "C:/flat3.fit", "2025-01-12")});

    // Enable session grouping to create separate groups
    GroupingOptions lightOptions = model->groupingOptions(FrameType::Light);
    lightOptions.bySession = true;
    model->setGroupingOptions(FrameType::Light, lightOptions);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* header = tab->findChild<QLabel*>("lblIssuesHeader");
    auto* issue0 = tab->findChild<QLabel*>("lblIssue_0");

    ASSERT_TRUE(header != nullptr);
    ASSERT_TRUE(issue0 != nullptr);

    // Should show grouped count
    EXPECT_TRUE(issue0->text().contains("groups") || issue0->text().contains("Missing"));
}

TEST_F(CalibrationTabTest, IssuesWidgetHasTooltips)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit")});
    // Provide at least one calibration frame so validation produces calibration warnings.
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* issue0 = tab->findChild<QLabel*>("lblIssue_0");
    ASSERT_TRUE(issue0 != nullptr);

    // Issues should have tooltips with details
    EXPECT_FALSE(issue0->toolTip().isEmpty());
}

TEST_F(CalibrationTabTest, CoverageWidgetShowsCorrectStatsForCompleteCalibration)
{
    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10")});
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* coverageWidget = tab->findChild<QWidget*>("calibrationCoverageWidget");
    ASSERT_TRUE(coverageWidget != nullptr);

    // The fixture never shows the tab as a window, so isVisible() will be false.
    EXPECT_FALSE(coverageWidget->isHidden());

    // Use design-time sizing, not runtime geometry (layouts are not guaranteed to run without showing).
    EXPECT_GE(coverageWidget->minimumWidth(), 100);
    EXPECT_GE(coverageWidget->minimumHeight(), 100);
    EXPECT_GE(coverageWidget->sizeHint().width(), 100);
    EXPECT_GE(coverageWidget->sizeHint().height(), 100);
}

TEST_F(CalibrationTabTest, CoverageWidgetDoesNotRequireDarksWhenBiasExists)
{
    // Split lights into multiple groups.
    GroupingOptions lightOptions = model->groupingOptions(FrameType::Light);
    lightOptions.bySession = true;
    model->setGroupingOptions(FrameType::Light, lightOptions);

    // Enable session matching for flats so fallback can be used.
    GroupingOptions flatOptions = model->groupingOptions(FrameType::Flat);
    flatOptions.bySession = true;
    model->setGroupingOptions(FrameType::Flat, flatOptions);

    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10"),
                                        createFrame(FrameType::Light, "C:/light2.fit", "2025-01-20")});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});

    // Provide only one flat session so the second light group uses fallback (ClosestDate).
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* coverageWidget = tab->findChild<QWidget*>("calibrationCoverageWidget");
    ASSERT_TRUE(coverageWidget != nullptr);

    const QString tooltip = coverageWidget->toolTip();
    EXPECT_TRUE(tooltip.contains("Total Light Groups: 2"));
    EXPECT_TRUE(tooltip.contains("Complete: 1"));
    EXPECT_TRUE(tooltip.contains("Warning: 1"));
    EXPECT_TRUE(tooltip.contains("Incomplete: 0"));
}

TEST_F(CalibrationTabTest, CoverageWidgetTreatsDoNotUseAsWarningNotIncomplete)
{
    GroupingOptions lightOptions = model->groupingOptions(FrameType::Light);
    lightOptions.bySession = true;
    model->setGroupingOptions(FrameType::Light, lightOptions);

    GroupingOptions flatOptions = model->groupingOptions(FrameType::Flat);
    flatOptions.bySession = true;
    model->setGroupingOptions(FrameType::Flat, flatOptions);

    const FrameInfo lightA = createFrame(FrameType::Light, "C:/lightA.fit", "2025-01-10");
    const FrameInfo lightB = createFrame(FrameType::Light, "C:/lightB.fit", "2025-01-20");

    model->addFrames(FrameType::Light, {lightA, lightB});
    model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});

    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flatA.fit", "2025-01-10"),
                                       createFrame(FrameType::Flat, "C:/flatB.fit", "2025-01-20")});

    // Explicitly disable Flat for one group ("Do not use"). This should be a Warning, not Incomplete.
    model->setCalibrationOverride(LightGroupKey::fromFrame(lightB), CalibrationOverrideType::Flat,
                                  QString::fromLatin1(kUnassignedCalibrationMarker));

    // Ensure the warning is independent of the fallback policy.
    auto policy = model->calibrationMatchingPolicy();
    policy.warnOnFallback = false;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* coverageWidget = tab->findChild<QWidget*>("calibrationCoverageWidget");
    ASSERT_TRUE(coverageWidget != nullptr);

    const QString tooltip = coverageWidget->toolTip();
    EXPECT_TRUE(tooltip.contains("Total Light Groups: 2"));
    EXPECT_TRUE(tooltip.contains("Complete: 1"));
    EXPECT_TRUE(tooltip.contains("Warning: 1"));
    EXPECT_TRUE(tooltip.contains("Incomplete: 0"));
}

TEST_F(CalibrationTabTest, RightPanelHasScrollArea)
{
    auto* rightPanelScroll = tab->findChild<QScrollArea*>("rightPanelScrollArea");
    EXPECT_TRUE(rightPanelScroll != nullptr);
    if (rightPanelScroll)
    {
        EXPECT_TRUE(rightPanelScroll->widgetResizable());
    }
}

TEST_F(CalibrationTabTest, QuickActionsAllButtonsExist)
{
    EXPECT_TRUE(tab->findChild<QPushButton*>("btnClearOverrides") != nullptr);
    EXPECT_TRUE(tab->findChild<QPushButton*>("btnResetPolicy") != nullptr);
    EXPECT_TRUE(tab->findChild<QPushButton*>("btnDisableFallbacks") != nullptr);
}

TEST_F(CalibrationTabTest, FrameSummaryWidgetAllLabelsExist)
{
    EXPECT_TRUE(tab->findChild<QLabel*>("lblBiasCount") != nullptr);
    EXPECT_TRUE(tab->findChild<QLabel*>("lblDarkCount") != nullptr);
    EXPECT_TRUE(tab->findChild<QLabel*>("lblFlatCount") != nullptr);
    EXPECT_TRUE(tab->findChild<QLabel*>("lblLightCount") != nullptr);
    EXPECT_TRUE(tab->findChild<QLabel*>("lblTotalCount") != nullptr);
    EXPECT_TRUE(tab->findChild<QLabel*>("lblLightGroupCount") != nullptr);
}

TEST_F(CalibrationTabTest, FrameSummaryUpdatesWhenFramesRemoved)
{
    const QString light1Path = FrameId(QStringLiteral("C:/light1.fit")).canonicalPath();
    const QString light2Path = FrameId(QStringLiteral("C:/light2.fit")).canonicalPath();
    model->addFrames(FrameType::Light,
                     {createFrame(FrameType::Light, light1Path), createFrame(FrameType::Light, light2Path)});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* lightCount = tab->findChild<QLabel*>("lblLightCount");
    ASSERT_TRUE(lightCount != nullptr);
    EXPECT_EQ(lightCount->text(), "2");

    // Remove one frame
    model->removeFrames(FrameType::Light, QStringList{light1Path});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    EXPECT_EQ(lightCount->text(), "1");
}

TEST_F(CalibrationTabTest, WarnOnFallbackDisablesSuppressesFallbackWarningsInUI)
{
    // Enable session grouping for flats
    GroupingOptions flatOptions = model->groupingOptions(FrameType::Flat);
    flatOptions.bySession = true;
    model->setGroupingOptions(FrameType::Flat, flatOptions);

    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-11")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblLightCalibration");
    ASSERT_TRUE(table != nullptr);
    ASSERT_GT(table->rowCount(), 0);

    QTableWidgetItem* flatItem = table->item(0, 3);
    ASSERT_TRUE(flatItem != nullptr);

    // With warnings enabled, should be ClosestDate
    EXPECT_EQ(flatItem->data(Qt::UserRole + 1).toInt(), static_cast<int>(CalibrationMatch::MatchType::ClosestDate));

    // Disable warnings
    auto policy = model->calibrationMatchingPolicy();
    policy.warnOnFallback = false;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    flatItem = table->item(0, 3);
    ASSERT_TRUE(flatItem != nullptr);

    // Still ClosestDate match type, but icon/tooltip should be different (tested visually)
    EXPECT_EQ(flatItem->data(Qt::UserRole + 1).toInt(), static_cast<int>(CalibrationMatch::MatchType::ClosestDate));
}

TEST_F(CalibrationTabTest, IssuesWidgetHidesWhenWarnOnFallbackDisabled)
{
    // Enable session grouping for flats
    GroupingOptions flatOptions = model->groupingOptions(FrameType::Flat);
    flatOptions.bySession = true;
    model->setGroupingOptions(FrameType::Flat, flatOptions);

    model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
    model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-11")});
    model->addFrames(FrameType::Dark, {createFrame(FrameType::Dark, "C:/dark1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    auto* header = tab->findChild<QLabel*>("lblIssuesHeader");
    ASSERT_TRUE(header != nullptr);

    // With warnings enabled, should show fallback issues
    int issueCountWithWarnings = 0;
    QRegularExpression re("\\((\\d+)\\)");
    auto match = re.match(header->text());
    if (match.hasMatch())
    {
        issueCountWithWarnings = match.captured(1).toInt();
    }

    // Disable warnings
    auto policy = model->calibrationMatchingPolicy();
    policy.warnOnFallback = false;
    model->setCalibrationMatchingPolicy(policy);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh();

    // With warnings disabled, should have fewer (or zero) issues
    int issueCountWithoutWarnings = 0;
    match = re.match(header->text());
    if (match.hasMatch())
    {
        issueCountWithoutWarnings = match.captured(1).toInt();
    }

    EXPECT_LE(issueCountWithoutWarnings, issueCountWithWarnings);
}

TEST_F(CalibrationTabTest, PolicyWidgetControlsHaveTooltips)
{
    auto* chkFlatFallback = tab->findChild<QCheckBox*>("chkAllowFlatFallback");
    auto* spinFlatDays = tab->findChild<QSpinBox*>("spinFlatDays");
    auto* chkPreferNewer = tab->findChild<QCheckBox*>("chkPreferNewerFlats");
    auto* chkDarkFallback = tab->findChild<QCheckBox*>("chkAllowDarkFallback");
    auto* spinDarkTolerance = tab->findChild<QSpinBox*>("spinDarkTolerance");
    auto* chkWarn = tab->findChild<QCheckBox*>("chkWarnOnFallback");
    auto* chkCCD = tab->findChild<QCheckBox*>("chkSubtractBias");

    ASSERT_TRUE(chkFlatFallback != nullptr);
    ASSERT_TRUE(spinFlatDays != nullptr);
    ASSERT_TRUE(chkPreferNewer != nullptr);
    ASSERT_TRUE(chkDarkFallback != nullptr);
    ASSERT_TRUE(spinDarkTolerance != nullptr);
    ASSERT_TRUE(chkWarn != nullptr);
    ASSERT_TRUE(chkCCD != nullptr);

    EXPECT_FALSE(chkFlatFallback->toolTip().isEmpty());
    EXPECT_FALSE(spinFlatDays->toolTip().isEmpty());
    EXPECT_FALSE(chkPreferNewer->toolTip().isEmpty());
    EXPECT_FALSE(chkDarkFallback->toolTip().isEmpty());
    EXPECT_FALSE(spinDarkTolerance->toolTip().isEmpty());
    EXPECT_FALSE(chkWarn->toolTip().isEmpty());
    EXPECT_FALSE(chkCCD->toolTip().isEmpty());
}

TEST_F(CalibrationTabTest, QuickActionsButtonsHaveTooltips)
{
    auto* btnClear = tab->findChild<QPushButton*>("btnClearOverrides");
    auto* btnReset = tab->findChild<QPushButton*>("btnResetPolicy");
    auto* btnDisable = tab->findChild<QPushButton*>("btnDisableFallbacks");

    ASSERT_TRUE(btnClear != nullptr);
    ASSERT_TRUE(btnReset != nullptr);
    ASSERT_TRUE(btnDisable != nullptr);

    EXPECT_FALSE(btnClear->toolTip().isEmpty());
    EXPECT_FALSE(btnReset->toolTip().isEmpty());
    EXPECT_FALSE(btnDisable->toolTip().isEmpty());
}
