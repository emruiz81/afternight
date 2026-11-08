// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <algorithm>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QTimeZone>
#include <optional>

#include "components/AnCardWidget.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/tabs/StackingTab.h"
#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"

namespace
{
FrameInfo createLightFrame(const QString& path, const QString& session = QString())
{
    FrameMetadata meta(
        FrameType::Light, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, QStringLiteral("L"), QStringLiteral("M42"),
        session.isEmpty() ? QDateTime::currentDateTimeUtc()
                          : QDateTime(QDate::fromString(session, "yyyy-MM-dd"), QTime(0, 0), QTimeZone::UTC),
        QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo createLightFrameWithExposure(const QString& path, double exposureSeconds, const QString& session = QString())
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), exposureSeconds, 100, 10, std::nullopt, QStringLiteral("L"),
                       QStringLiteral("M42"),
                       session.isEmpty()
                           ? QDateTime::currentDateTimeUtc()
                           : QDateTime(QDate::fromString(session, "yyyy-MM-dd"), QTime(0, 0), QTimeZone::UTC),
                       QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo createLightFrameCustom(const QString& path, const QSize& resolution, const QString& filter,
                                 const QString& objectName)
{
    FrameMetadata meta(FrameType::Light, resolution, 30.0, 100, 10, std::nullopt, filter, objectName,
                       QDateTime::currentDateTimeUtc(), QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}

FrameQualityMetrics makeQualityMetrics(double fwhm, double snr, int stars)
{
    FrameQualityMetrics metrics;
    metrics.fwhm = fwhm;
    metrics.snr = snr;
    metrics.stars = stars;
    metrics.eccentricity = 0.25;
    metrics.succeeded = true;
    return metrics;
}
} // namespace

class StackingTabTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        tab = new StackingTab(model);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    StackingTab* tab = nullptr;
};

TEST_F(StackingTabTest, InitialStateShowsEmptyMessage)
{
    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);

    EXPECT_EQ(table->rowCount(), 1);
    QTableWidgetItem* item = table->item(0, 0);
    ASSERT_TRUE(item != nullptr);
    EXPECT_TRUE(item->text().contains("No integration groups"));
}

TEST_F(StackingTabTest, DisplaysGroupsAfterAddingLights)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);

    // Should have at least 1 group
    EXPECT_GE(table->rowCount(), 1);
}

TEST_F(StackingTabTest, GroupTableHasSettingsColumns)
{
    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);

    // Group, Frames, Calib Groups, Total Time, Stack, Reject, Normalize, Weighting, Drizzle, Output
    EXPECT_EQ(table->columnCount(), 10);

    QStringList expectedHeaders = {"Group",  "Frames",    "Calib Groups", "Total Time", "Stack",
                                   "Reject", "Normalize", "Weighting",    "Drizzle",    "Output"};

    for (int i = 0; i < expectedHeaders.size(); ++i)
    {
        auto* header = table->horizontalHeaderItem(i);
        ASSERT_TRUE(header != nullptr);
        EXPECT_EQ(header->text(), expectedHeaders[i]);
    }
}

TEST_F(StackingTabTest, GroupBoxTitlesAreLargerThanContents)
{
    QWidget* groupsWidget = tab->findChild<QWidget*>("grpIntegrationGroups");
    auto* groupsBox = dynamic_cast<an::ui::AnCardWidget*>(groupsWidget);
    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(groupsBox != nullptr);
    ASSERT_TRUE(table != nullptr);

    const QFont groupboxFont = groupsBox->font();
    const QFont tableFont = table->font();

    if (groupboxFont.pointSizeF() > 0.0 && tableFont.pointSizeF() > 0.0)
    {
        const double groupPointSize = groupboxFont.pointSizeF();
        const double tablePointSize = tableFont.pointSizeF();
        EXPECT_TRUE(groupPointSize > tablePointSize || (qFuzzyCompare(groupPointSize + 1.0, tablePointSize + 1.0) &&
                                                        groupboxFont.weight() >= tableFont.weight()));
    }
    else if (groupboxFont.pixelSize() > 0 && tableFont.pixelSize() > 0)
    {
        const int groupPixelSize = groupboxFont.pixelSize();
        const int tablePixelSize = tableFont.pixelSize();
        EXPECT_TRUE(groupPixelSize > tablePixelSize ||
                    (groupPixelSize == tablePixelSize && groupboxFont.weight() >= tableFont.weight()));
    }
    else
    {
        FAIL() << "Could not compare groupbox/table font sizes (both pointSizeF and pixelSize unavailable).";
    }
}

TEST_F(StackingTabTest, GroupingControlsAreInIntegrationGroupsBox)
{
    QWidget* groupsWidget = tab->findChild<QWidget*>("grpIntegrationGroups");
    auto* groupsBox = dynamic_cast<an::ui::AnCardWidget*>(groupsWidget);
    ASSERT_TRUE(groupsBox != nullptr);

    EXPECT_TRUE(groupsBox->findChild<QCheckBox*>("chkGroupBySession") != nullptr);
    EXPECT_TRUE(groupsBox->findChild<QCheckBox*>("chkGroupByCustomTag") != nullptr);
    EXPECT_TRUE(groupsBox->findChild<QCheckBox*>("chkGroupByExposure") != nullptr);
    EXPECT_TRUE(groupsBox->findChild<QDoubleSpinBox*>("spinExposureTolerance") != nullptr);

    // Presets were removed; there should be no preset combo box.
    EXPECT_TRUE(tab->findChild<QComboBox*>("cmbGroupingPreset") == nullptr);
}

TEST_F(StackingTabTest, SettingsColumnsDisplayValues)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);
    QTRY_VERIFY(table->rowCount() > 0);
    QTRY_VERIFY(table->item(0, 4) != nullptr);
    QTRY_VERIFY(table->item(0, 5) != nullptr);
    QTRY_VERIFY(table->item(0, 6) != nullptr);
    QTRY_VERIFY(table->item(0, 8) != nullptr);

    // Stack column (index 4) should show method name
    QTableWidgetItem* stackItem = table->item(0, 4);
    ASSERT_TRUE(stackItem != nullptr);
    EXPECT_FALSE(stackItem->text().isEmpty());
    // Expected values: "Average", "Median", etc.

    // Reject column (index 5) should show rejection summary (or "-" when not applicable)
    QTableWidgetItem* rejectItem = table->item(0, 5);
    ASSERT_TRUE(rejectItem != nullptr);
    EXPECT_FALSE(rejectItem->text().isEmpty());

    // Normalize column (index 6)
    QTableWidgetItem* normalizeItem = table->item(0, 6);
    ASSERT_TRUE(normalizeItem != nullptr);
    EXPECT_FALSE(normalizeItem->text().isEmpty());

    // Drizzle column (index 8)
    QTableWidgetItem* drizzleItem = table->item(0, 8);
    ASSERT_TRUE(drizzleItem != nullptr);
    // Expected: "Off", "2x", "2.5x", etc.
}

// A group weighed by its stars' width shows its weighting and floor, in the cell and its tooltip, and nothing of
// per-channel widths: every channel is weighed by its light's one width. Both said a per-channel choice was off.
TEST_F(StackingTabTest, TheWeightingColumnSaysNothingOfPerChannelWidths)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});
    ASSERT_FALSE(model->integrationGroups().empty());
    StackingSettings settings;
    settings.weighting = QStringLiteral("Weighted FWHM");
    settings.minWeightFloor = 0.05;
    model->setStackingSettings(FrameType::Light, model->integrationGroups().front(), settings);

    tab->refreshAll();
    QCoreApplication::processEvents();

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);
    QTRY_VERIFY(table->rowCount() > 0 && table->item(0, 7) != nullptr);
    const QTableWidgetItem* weighting = table->item(0, 7);
    EXPECT_TRUE(weighting->text().contains(QStringLiteral("Weighted FWHM"))) << weighting->text().toStdString();
    EXPECT_TRUE(weighting->text().contains(QStringLiteral("Floor=0.050"))) << weighting->text().toStdString();
    EXPECT_FALSE(weighting->text().contains(QStringLiteral("Per-Ch"))) << weighting->text().toStdString();
    EXPECT_TRUE(weighting->toolTip().contains(QStringLiteral("Min weight floor: 0.050")))
        << weighting->toolTip().toStdString();
    EXPECT_FALSE(weighting->toolTip().contains(QStringLiteral("Per-channel"), Qt::CaseInsensitive))
        << weighting->toolTip().toStdString();
}

TEST_F(StackingTabTest, TotalTimeColumnUsesHumanFriendlyFormat)
{
    model->addFrames(FrameType::Light, {
                                           createLightFrameWithExposure("C:/light1.fit", 70.0, "2025-01-10"),
                                           createLightFrameWithExposure("C:/light2.fit", 20.0, "2025-01-10"),
                                       });

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);
    ASSERT_GT(table->rowCount(), 0);

    QTableWidgetItem* totalTimeItem = table->item(0, 3);
    ASSERT_TRUE(totalTimeItem != nullptr);
    EXPECT_EQ(totalTimeItem->text(), QStringLiteral("1m 30s"));
    ASSERT_FALSE(totalTimeItem->toolTip().isEmpty());
    EXPECT_TRUE(totalTimeItem->toolTip().contains(QStringLiteral("Exposure breakdown")));
    EXPECT_TRUE(totalTimeItem->toolTip().contains(QStringLiteral("1 x 20s")));
    EXPECT_TRUE(totalTimeItem->toolTip().contains(QStringLiteral("1 x 70s")));

    model->reset();
    model->addFrames(FrameType::Light, {
                                           createLightFrameWithExposure("C:/light3.fit", 3700.0, "2025-01-10"),
                                       });

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    ASSERT_GT(table->rowCount(), 0);
    QTableWidgetItem* totalTimeItem2 = table->item(0, 3);
    ASSERT_TRUE(totalTimeItem2 != nullptr);
    EXPECT_EQ(totalTimeItem2->text(), QStringLiteral("1h 01m"));
    EXPECT_TRUE(totalTimeItem2->toolTip().contains(QStringLiteral("1 x 3700s")));
}

TEST_F(StackingTabTest, StackingSettingsButtonExists)
{
    QPushButton* btnSettings = tab->findChild<QPushButton*>("btnStackingSettings");
    ASSERT_TRUE(btnSettings != nullptr);
    EXPECT_TRUE(btnSettings->text().contains("Stacking Settings"));
}

TEST_F(StackingTabTest, SettingsButtonDisabledWhenNoGroups)
{
    // No frames added, so no groups
    QPushButton* btnSettings = tab->findChild<QPushButton*>("btnStackingSettings");

    ASSERT_TRUE(btnSettings != nullptr);

    EXPECT_FALSE(btnSettings->isEnabled());
}

TEST_F(StackingTabTest, SettingsButtonEnabledWhenGroupSelected)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    QPushButton* btnSettings = tab->findChild<QPushButton*>("btnStackingSettings");

    ASSERT_TRUE(table != nullptr);
    ASSERT_TRUE(btnSettings != nullptr);

    // Select first row
    table->selectRow(0);
    QCoreApplication::processEvents();

    EXPECT_TRUE(btnSettings->isEnabled());
}

TEST_F(StackingTabTest, BestNormalizationReferenceCandidateUsesOnlySelectedIntegrationGroup)
{
    const FrameInfo m42Poor = createLightFrameCustom(QStringLiteral("C:/m42_poor.fit"), QSize(1000, 1000),
                                                     QStringLiteral("L"), QStringLiteral("M42"));
    const FrameInfo m42Best = createLightFrameCustom(QStringLiteral("C:/m42_best.fit"), QSize(1000, 1000),
                                                     QStringLiteral("L"), QStringLiteral("M42"));
    const FrameInfo m31Best = createLightFrameCustom(QStringLiteral("C:/m31_best.fit"), QSize(1000, 1000),
                                                     QStringLiteral("L"), QStringLiteral("M31"));

    model->addFrames(FrameType::Light, {m42Poor, m42Best, m31Best});
    model->setFrameQualityMetrics(m42Poor.id, makeQualityMetrics(4.0, 5.0, 100));
    model->setFrameQualityMetrics(m42Best.id, makeQualityMetrics(2.0, 35.0, 900));
    model->setFrameQualityMetrics(m31Best.id, makeQualityMetrics(1.0, 90.0, 3000));

    GroupingOptions effectiveOptions = model->lightIntegrationGroupingOptions();
    effectiveOptions.byResolution = true;
    effectiveOptions.byFilter = true;
    effectiveOptions.byObject = true;
    const GroupKey m42Key = LightGroupKey::fromFrame(m42Poor).toGroupKey(effectiveOptions);

    const auto best = bestNormalizationReferenceCandidate(model, m42Key);
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->canonicalPath(), m42Best.id.canonicalPath());
}

// The Frame Quality dashboard previews the weights the group's stack applies, so a stored weighting the stack lets go
// of - beside a median, which takes no weights, as a group handed over from Image Integration Lite or saved by hand
// can hold - previews none; the same group under an average previews its weighting.
TEST_F(StackingTabTest, TheDashboardPreviewsNoWeightingTheStackLetsGoOf)
{
    const FrameInfo first = createLightFrame(QStringLiteral("C:/weighed_1.fit"), QStringLiteral("2025-01-10"));
    const FrameInfo second = createLightFrame(QStringLiteral("C:/weighed_2.fit"), QStringLiteral("2025-01-10"));
    model->addFrames(FrameType::Light, {first, second});

    GroupingOptions effectiveOptions = model->lightIntegrationGroupingOptions();
    effectiveOptions.byResolution = true;
    effectiveOptions.byFilter = true;
    effectiveOptions.byObject = true;
    const GroupKey key = LightGroupKey::fromFrame(first).toGroupKey(effectiveOptions);

    StackingSettings median;
    median.stackMethod = QStringLiteral("Median");
    median.weighting = QStringLiteral("Noise");
    model->setStackingSettings(FrameType::Light, key, median);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    auto* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    auto* dashboard = tab->findChild<FrameQualityDashboard*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(dashboard, nullptr);
    QTRY_VERIFY(table->rowCount() > 0);
    table->selectRow(0);
    QCoreApplication::processEvents();
    EXPECT_FALSE(dashboard->weightingEnabled()) << "a median takes no frame weights";

    StackingSettings average = median;
    average.stackMethod = QStringLiteral("Average");
    model->setStackingSettings(FrameType::Light, key, average);
    table->clearSelection();
    QCoreApplication::processEvents();
    table->selectRow(0);
    QCoreApplication::processEvents();
    EXPECT_TRUE(dashboard->weightingEnabled());
}

// The dashboard's banner says why the stack lets go of the weighting a group stores - its exposure merge, its median -
// rather than say the group's weighting is None, which only a group storing none is told.
TEST_F(StackingTabTest, TheDashboardSaysWhyTheStackLetsGoOfTheWeightingTheGroupStores)
{
    const FrameInfo first = createLightFrame(QStringLiteral("C:/merged_1.fit"), QStringLiteral("2025-01-10"));
    const FrameInfo second = createLightFrame(QStringLiteral("C:/merged_2.fit"), QStringLiteral("2025-01-10"));
    model->addFrames(FrameType::Light, {first, second});

    GroupingOptions effectiveOptions = model->lightIntegrationGroupingOptions();
    effectiveOptions.byResolution = true;
    effectiveOptions.byFilter = true;
    effectiveOptions.byObject = true;
    const GroupKey key = LightGroupKey::fromFrame(first).toGroupKey(effectiveOptions);

    StackingSettings merge;
    merge.weighting = QStringLiteral("Noise");
    merge.hdr.enabled = true;
    model->setStackingSettings(FrameType::Light, key, merge);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    auto* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    auto* dashboard = tab->findChild<FrameQualityDashboard*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(dashboard, nullptr);
    auto* banner = dashboard->findChild<QLabel*>(QStringLiteral("weightingPreviewBanner"));
    ASSERT_NE(banner, nullptr);
    QTRY_VERIFY(table->rowCount() > 0);
    const auto reselect = [table]() {
        table->clearSelection();
        QCoreApplication::processEvents();
        table->selectRow(0);
        QCoreApplication::processEvents();
    };

    reselect();
    EXPECT_FALSE(dashboard->weightingEnabled()) << "an exposure merge weighs no frame";
    EXPECT_FALSE(banner->text().contains(QStringLiteral("Weighting: None"))) << banner->text().toStdString();
    EXPECT_TRUE(banner->text().contains(QStringLiteral("exposure merge"))) << banner->text().toStdString();
    EXPECT_TRUE(banner->text().contains(QStringLiteral("Noise"))) << banner->text().toStdString();

    StackingSettings median;
    median.stackMethod = QStringLiteral("Median");
    median.weighting = QStringLiteral("Noise");
    model->setStackingSettings(FrameType::Light, key, median);
    reselect();
    EXPECT_FALSE(dashboard->weightingEnabled());
    EXPECT_TRUE(banner->text().contains(QStringLiteral("Median takes no frame weights")))
        << banner->text().toStdString();

    StackingSettings none;
    none.weighting = QStringLiteral("None");
    model->setStackingSettings(FrameType::Light, key, none);
    reselect();
    EXPECT_FALSE(dashboard->weightingEnabled());
    EXPECT_TRUE(banner->text().contains(QStringLiteral("Weighting: None"))) << banner->text().toStdString();
}

TEST_F(StackingTabTest, ALightGroupsFramesAreTheGroupsOwnWhatIsMoreTheCollectionHandsThemOutByValue)
{
    // The stacking settings dialog reads its group's frames through this: a set the size a real session holds, so a
    // copy taken from frames already freed would read memory the copy itself has been handed.
    std::vector<FrameInfo> lights;
    for (int index = 0; index < 72; ++index)
    {
        lights.push_back(createLightFrame(QStringLiteral("C:/lights/light_%1.fit").arg(index, 3, 10, QLatin1Char('0')),
                                          QStringLiteral("2025-01-10")));
    }
    model->addFrames(FrameType::Light, lights);
    const auto groups =
        IntegrationGroupEngine{}.computeGroups(model->lightFrames().frames(), model->lightIntegrationGroupingOptions());
    ASSERT_EQ(groups.size(), 1u);

    const std::vector<FrameInfo> frames = lightIntegrationGroupFrames(*model, groups.begin()->first.key);

    ASSERT_EQ(frames.size(), lights.size());
    std::vector<QString> expected;
    std::vector<QString> read;
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        expected.push_back(lights[index].displayPath);
        read.push_back(frames[index].displayPath);
        EXPECT_EQ(frames[index].metadata.exposureSeconds(), 30.0) << frames[index].displayPath.toStdString();
    }
    std::sort(expected.begin(), expected.end());
    std::sort(read.begin(), read.end());
    EXPECT_EQ(read, expected);
}

TEST_F(StackingTabTest, AutomaticReferenceModeRedisplaysBestReferenceAfterManualSelection)
{
    const FrameInfo m42Poor = createLightFrameCustom(QStringLiteral("C:/m42_poor.fit"), QSize(1000, 1000),
                                                     QStringLiteral("L"), QStringLiteral("M42"));
    const FrameInfo m42Best = createLightFrameCustom(QStringLiteral("C:/m42_best.fit"), QSize(1000, 1000),
                                                     QStringLiteral("L"), QStringLiteral("M42"));

    model->addFrames(FrameType::Light, {m42Poor, m42Best});
    model->setFrameQualityMetrics(m42Poor.id, makeQualityMetrics(4.0, 5.0, 100));
    model->setFrameQualityMetrics(m42Best.id, makeQualityMetrics(2.0, 35.0, 900));

    GroupingOptions effectiveOptions = model->lightIntegrationGroupingOptions();
    effectiveOptions.byResolution = true;
    effectiveOptions.byFilter = true;
    effectiveOptions.byObject = true;
    const GroupKey m42Key = LightGroupKey::fromFrame(m42Poor).toGroupKey(effectiveOptions);

    StackingSettingsDialog dialog(StackingSettingsDialog::FrameType::LIGHT, QStringLiteral("Light"));
    dialog.setNormalizationReferenceCandidates({m42Poor.id.canonicalPath(), m42Best.id.canonicalPath()});
    dialog.setNormalizationMode(QStringLiteral("Additive With Scaling"));
    dialog.setManualNormalizationReference(m42Poor.id.canonicalPath());
    dialog.setNormalizationReferenceMode(QStringLiteral("Manual"));

    QObject::connect(&dialog, &StackingSettingsDialog::automaticNormalizationReferenceDisplayRequested, &dialog, [&]() {
        if (const auto best = bestNormalizationReferenceCandidate(model, m42Key); best.has_value())
        {
            dialog.setManualNormalizationReference(best->canonicalPath());
        }
    });

    auto* referenceCombo = dialog.findChild<QComboBox*>(QStringLiteral("normalizationReferenceFrame"));
    ASSERT_NE(referenceCombo, nullptr);
    ASSERT_TRUE(referenceCombo->isEnabled());
    EXPECT_EQ(dialog.getManualNormalizationReference(), m42Poor.id.canonicalPath());

    dialog.setNormalizationReferenceMode(QStringLiteral("Automatic (best quality)"));

    EXPECT_FALSE(referenceCombo->isEnabled());
    EXPECT_EQ(dialog.getManualNormalizationReference(), m42Best.id.canonicalPath());
}

TEST_F(StackingTabTest, HasExecutionOptionsGroup)
{
    QWidget* optionsWidget = tab->findChild<QWidget*>("grpExecutionOptions");
    auto* grpOptions = dynamic_cast<an::ui::AnCardWidget*>(optionsWidget);
    ASSERT_TRUE(grpOptions != nullptr);

    QCheckBox* chk64Bit = tab->findChild<QCheckBox*>("chkGenerate64Bit");
    QCheckBox* chkReprocess = tab->findChild<QCheckBox*>("chkReprocessExisting");
    QCheckBox* chkOpenIntegrated = tab->findChild<QCheckBox*>("chkOpenIntegratedFiles");
    QCheckBox* chkCombineRgb = tab->findChild<QCheckBox*>("chkCombineRgbImages");

    EXPECT_TRUE(chk64Bit != nullptr);
    EXPECT_TRUE(chkReprocess != nullptr);
    EXPECT_TRUE(chkOpenIntegrated != nullptr);
    ASSERT_TRUE(chkCombineRgb != nullptr);
    EXPECT_EQ(chkCombineRgb->text(), QStringLiteral("Combine RGB images"));
}

TEST_F(StackingTabTest, OpenIntegratedFilesEnabledByDefault)
{
    QCheckBox* chkOpenIntegrated = tab->findChild<QCheckBox*>("chkOpenIntegratedFiles");
    ASSERT_TRUE(chkOpenIntegrated != nullptr);

    EXPECT_TRUE(chkOpenIntegrated->isChecked());
    EXPECT_TRUE(tab->isOpenIntegratedFilesEnabled());
}

TEST_F(StackingTabTest, OpenIntegratedFilesGetterTracksCheckboxState)
{
    QCheckBox* chkOpenIntegrated = tab->findChild<QCheckBox*>("chkOpenIntegratedFiles");
    ASSERT_TRUE(chkOpenIntegrated != nullptr);

    chkOpenIntegrated->setChecked(false);
    EXPECT_FALSE(tab->isOpenIntegratedFilesEnabled());

    chkOpenIntegrated->setChecked(true);
    EXPECT_TRUE(tab->isOpenIntegratedFilesEnabled());
}

TEST_F(StackingTabTest, CombineRgbImagesDisabledAndUncheckedByDefault)
{
    QCheckBox* chkCombineRgb = tab->findChild<QCheckBox*>("chkCombineRgbImages");
    ASSERT_TRUE(chkCombineRgb != nullptr);

    EXPECT_FALSE(chkCombineRgb->isEnabled());
    EXPECT_FALSE(chkCombineRgb->isChecked());
    EXPECT_FALSE(tab->isCombineRgbImagesEnabled());
}

TEST_F(StackingTabTest, CombineRgbImagesEnablesForRgbFiltersInSameRegistrationGroup)
{
    model->addFrames(FrameType::Light, {createLightFrameCustom(QStringLiteral("C:/m42_red.fit"), QSize(1000, 1000),
                                                               QStringLiteral("Deep Red"), QStringLiteral("M42")),
                                        createLightFrameCustom(QStringLiteral("C:/m42_green.fit"), QSize(1000, 1000),
                                                               QStringLiteral("GREEN channel"), QStringLiteral("M42")),
                                        createLightFrameCustom(QStringLiteral("C:/m42_blue.fit"), QSize(1000, 1000),
                                                               QStringLiteral("blue"), QStringLiteral("M42"))});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QCheckBox* chkCombineRgb = tab->findChild<QCheckBox*>("chkCombineRgbImages");
    ASSERT_TRUE(chkCombineRgb != nullptr);
    EXPECT_TRUE(chkCombineRgb->isEnabled());
    EXPECT_FALSE(chkCombineRgb->isChecked());

    chkCombineRgb->setChecked(true);
    EXPECT_TRUE(tab->isCombineRgbImagesEnabled());
}

TEST_F(StackingTabTest, CombineRgbImagesStaysDisabledWhenRgbFiltersAreInDifferentRegistrationGroups)
{
    model->addFrames(FrameType::Light, {createLightFrameCustom(QStringLiteral("C:/m42_red.fit"), QSize(1000, 1000),
                                                               QStringLiteral("Red"), QStringLiteral("M42")),
                                        createLightFrameCustom(QStringLiteral("C:/m31_green.fit"), QSize(1000, 1000),
                                                               QStringLiteral("Green"), QStringLiteral("M31")),
                                        createLightFrameCustom(QStringLiteral("C:/m42_blue.fit"), QSize(1000, 1000),
                                                               QStringLiteral("Blue"), QStringLiteral("M42"))});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QCheckBox* chkCombineRgb = tab->findChild<QCheckBox*>("chkCombineRgbImages");
    ASSERT_TRUE(chkCombineRgb != nullptr);
    EXPECT_FALSE(chkCombineRgb->isEnabled());
    EXPECT_FALSE(chkCombineRgb->isChecked());
    EXPECT_FALSE(tab->isCombineRgbImagesEnabled());
}

TEST_F(StackingTabTest, MaxParallelCoresDefaultsToDetectedCoreCount)
{
    QLabel* label = tab->findChild<QLabel*>("lblMaxParallelCores");
    QSpinBox* spinCores = tab->findChild<QSpinBox*>("spinMaxParallelCores");
    ASSERT_TRUE(label != nullptr);
    ASSERT_TRUE(spinCores != nullptr);

    const int detected_cores = ImageIntegration::core::detectedParallelCoreCount();
    EXPECT_EQ(label->text(), QStringLiteral("CPU Threads:"));
    EXPECT_EQ(spinCores->minimum(), 1);
    EXPECT_EQ(spinCores->maximum(), detected_cores);
    EXPECT_EQ(spinCores->value(), detected_cores);
    EXPECT_TRUE(spinCores->suffix().isEmpty());
    EXPECT_GE(spinCores->minimumWidth(), spinCores->sizeHint().width());
    EXPECT_EQ(tab->maxParallelCores(), detected_cores);
}

TEST_F(StackingTabTest, MaxParallelCoresGetterTracksSpinValue)
{
    QSpinBox* spinCores = tab->findChild<QSpinBox*>("spinMaxParallelCores");
    ASSERT_TRUE(spinCores != nullptr);

    const int value = spinCores->maximum() > 1 ? spinCores->maximum() - 1 : 1;
    spinCores->setValue(value);

    EXPECT_EQ(tab->maxParallelCores(), value);
}

TEST_F(StackingTabTest, DoubleClickOpensSettingsDialog)
{
    // This test just verifies the double-click handler is connected.
    // We can't easily test dialog opening without mocking.
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    ASSERT_TRUE(table != nullptr);
    ASSERT_GT(table->rowCount(), 0);

    // Just verify the table has content - actual dialog test would require mocking
    EXPECT_FALSE(table->item(0, 0)->text().isEmpty());
}

TEST_F(StackingTabTest, HasRunIntegrationButton)
{
    QPushButton* btn = tab->findChild<QPushButton*>("btnRunIntegration");
    ASSERT_TRUE(btn != nullptr);
    EXPECT_TRUE(btn->text().contains("Integration") || btn->text().contains("Run"));
}

TEST_F(StackingTabTest, RunIntegrationButtonDisabledInitially)
{
    QPushButton* btn = tab->findChild<QPushButton*>("btnRunIntegration");
    ASSERT_TRUE(btn != nullptr);
    EXPECT_FALSE(btn->isEnabled());
}

TEST_F(StackingTabTest, RunIntegrationButtonEnabledWhenGroupSelected)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    QPushButton* btn = tab->findChild<QPushButton*>("btnRunIntegration");

    ASSERT_TRUE(table != nullptr);
    ASSERT_TRUE(btn != nullptr);

    table->selectRow(0);
    QCoreApplication::processEvents();

    EXPECT_TRUE(btn->isEnabled());
}

TEST_F(StackingTabTest, RunIntegrationButtonEmitsSignal)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    QPushButton* btn = tab->findChild<QPushButton*>("btnRunIntegration");

    table->selectRow(0);
    QCoreApplication::processEvents();

    QSignalSpy spy(tab, SIGNAL(runIntegrationAllRequested()));
    btn->click();

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(StackingTabTest, RunIntegrationGroupButtonEmitsSignal)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    QTableWidget* table = tab->findChild<QTableWidget*>("tblIntegrationGroups");
    QPushButton* btn = tab->findChild<QPushButton*>("btnRunIntegrationGroup");

    ASSERT_TRUE(table != nullptr);
    ASSERT_TRUE(btn != nullptr);

    // Get the expected group key from the table
    ASSERT_GT(table->rowCount(), 0);
    const QString expectedKey = table->item(0, 0)->data(Qt::UserRole).toString();

    table->selectRow(0);
    QCoreApplication::processEvents();
    QTRY_VERIFY(btn->isEnabled());

    QSignalSpy spy(tab, SIGNAL(runIntegrationRequested(QString)));
    btn->click();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), expectedKey);
}

TEST_F(StackingTabTest, ShowFrameInTableSelectsCorrectIntegrationGroup)
{
    // Two different groups (filter and object differ).
    const QString path1 = QStringLiteral("C:/light_L_M42.fit");
    const QString path2 = QStringLiteral("C:/light_R_M31.fit");
    model->addFrames(FrameType::Light,
                     {createLightFrameCustom(path1, QSize(1000, 1000), QStringLiteral("L"), QStringLiteral("M42")),
                      createLightFrameCustom(path2, QSize(1000, 1000), QStringLiteral("R"), QStringLiteral("M31"))});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    // Expected key for path2 computed using the same IntegrationGroupEngine logic.
    IntegrationGroupEngine engine;
    const auto groups = engine.computeGroups(model->lightFrames().frames(), model->lightIntegrationGroupingOptions());

    const QString canonical2 = FrameId(path2).canonicalPath();
    const FrameId id2(canonical2);
    ASSERT_TRUE(id2.isValid());

    std::optional<GroupKey> expected;
    for (const auto& [key, frames] : groups)
    {
        if (std::find(frames.begin(), frames.end(), id2) != frames.end())
        {
            expected = key.key;
            break;
        }
    }
    ASSERT_TRUE(expected.has_value());

    tab->showFrameInTable(path2);
    QCoreApplication::processEvents();

    const auto& selected = model->selectedGroupKeys();
    ASSERT_EQ(selected.size(), 1);
    EXPECT_EQ(selected.front().toString(), expected->toString());
}

TEST_F(StackingTabTest, ShowFrameInTableDoesNothingForExcludedFrame)
{
    const QString path = QStringLiteral("C:/light_excluded.fit");
    const QString otherPath = QStringLiteral("C:/light_included.fit");
    model->addFrames(
        FrameType::Light,
        {createLightFrameCustom(path, QSize(1000, 1000), QStringLiteral("L"), QStringLiteral("M42")),
         createLightFrameCustom(otherPath, QSize(1000, 1000), QStringLiteral("L"), QStringLiteral("M31"))});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(250);

    // Select a known good group first.
    tab->showFrameInTable(otherPath);
    QCoreApplication::processEvents();
    ASSERT_EQ(model->selectedGroupKeys().size(), 1);
    const GroupKey before = model->selectedGroupKeys().front();

    // Now exclude the target frame and ensure jumping to it doesn't change selection.
    model->updateFrameAttribute(path, FrameAttribute::Excluded, true);
    QCoreApplication::processEvents();

    tab->showFrameInTable(path);
    QCoreApplication::processEvents();

    ASSERT_EQ(model->selectedGroupKeys().size(), 1);
    EXPECT_EQ(model->selectedGroupKeys().front().toString(), before.toString());
}
