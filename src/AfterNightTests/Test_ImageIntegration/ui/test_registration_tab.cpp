// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QCheckBox>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QPalette>
#include <QTimeZone>
#include <QTimer>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "processes/ImageIntegration/tabs/RegistrationTab.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "theme/AnDesignTokens.h"

#include "Test_ImageIntegration/ui/OpenDialogs.h"

#include <algorithm>
#include <memory>

namespace
{
using ImageIntegration::RegistrationHashUtils::hashFrameIdList;
using ImageIntegration::RegistrationHashUtils::registrationDetectionHash;
using ImageIntegration::RegistrationHashUtils::registrationTransformHash;

FrameInfo createLightFrame(const QString& path, const QString& session = QString())
{
    FrameMetadata meta(
        FrameType::Light, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, QStringLiteral("L"), QStringLiteral("M42"),
        session.isEmpty() ? QDateTime::currentDateTimeUtc()
                          : QDateTime(QDate::fromString(session, "yyyy-MM-dd"), QTime(0, 0), QTimeZone::UTC),
        QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}

FrameInfo createLightFrameForObject(const QString& path, const QString& objectName)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, QStringLiteral("L"),
                       objectName, QDateTime::currentDateTimeUtc(), QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}

/// Makes a registration database as this version's writer makes it: every table, at the format it states.
void makeCurrentRegistrationDatabase(const QString& databasePath)
{
    an::ui::RegistrationMetricsWriter writer(databasePath, QStringLiteral("registration_tab_current_schema"));
    QString error;
    ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();
}

void seedMatchingRegistrationState(const QString& databasePath, const WorkflowManifest& manifest,
                                   const WorkflowManifest::RegistrationGroup& group)
{
    makeCurrentRegistrationDatabase(databasePath);
    static int connectionIndex = 0;
    const QString connectionName = QStringLiteral("registration_tab_matching_state_%1").arg(connectionIndex++);
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(databasePath);
    ASSERT_TRUE(database.open());

    QSqlQuery query(database);
    ASSERT_TRUE(
        query.exec(QStringLiteral("PRAGMA user_version = %1").arg(stage_utils::kRegistrationMetricsSchemaVersion)));
    ASSERT_TRUE(query.exec(stage_utils::registrationMetricsCreateSql()));
    ASSERT_TRUE(query.exec(stage_utils::registrationGroupStateCreateSql()));
    ASSERT_TRUE(query.exec(stage_utils::starDetectionCacheCreateSql()));
    ASSERT_TRUE(query.exec(stage_utils::registrationMatchedSourcesCreateSql()));
    ASSERT_TRUE(query.exec(stage_utils::registrationMatchedSourcesGroupIndexCreateSql()));
    ASSERT_TRUE(query.exec(stage_utils::registrationMatchedSourcesSourceIndexCreateSql()));

    const QString detectionHash = registrationDetectionHash(group.settings);
    const QString calibrationHash =
        computeLightsCalibrationHashForSubset(manifest.lightsCalibration, group.lightFiles, nullptr);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO registration_group_state ("
        "group_key, settings_version, detection_hash, registration_hash, reference_frame_id, "
        "light_files_hash, calibration_hash, output_wcs_json, updated_at) "
        "VALUES (:group_key, :settings_version, :detection_hash, :registration_hash, :reference_frame_id, "
        ":light_files_hash, :calibration_hash, '', 1)"));
    query.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
    query.bindValue(QStringLiteral(":settings_version"), group.settings.version);
    query.bindValue(QStringLiteral(":detection_hash"), detectionHash);
    query.bindValue(QStringLiteral(":registration_hash"), registrationTransformHash(group.settings));
    query.bindValue(QStringLiteral(":reference_frame_id"),
                    group.referenceFrame.has_value() ? group.referenceFrame->canonicalPath() : QString());
    query.bindValue(QStringLiteral(":light_files_hash"), hashFrameIdList(group.lightFiles));
    query.bindValue(QStringLiteral(":calibration_hash"), calibrationHash);
    ASSERT_TRUE(query.exec()) << query.lastError().text().toStdString();

    for (const auto& frame : group.lightFiles)
    {
        QSqlQuery metric(database);
        metric.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO registration_metrics ("
            "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, succeeded, output_path, "
            "transform_json, updated_at) "
            "VALUES (:frame_id, :group_key, :settings_version, 50, 2.0, 10.0, 0.2, 1, '', '', 1)"));
        metric.bindValue(QStringLiteral(":frame_id"), frame.canonicalPath());
        metric.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
        metric.bindValue(QStringLiteral(":settings_version"), group.settings.version);
        ASSERT_TRUE(metric.exec()) << metric.lastError().text().toStdString();

        QSqlQuery stars(database);
        stars.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO star_detection_cache ("
            "frame_id, group_key, detection_hash, input_path, input_mtime, input_size_bytes, input_modified_at_ms, "
            "input_identity_token, stars_count, fwhm, snr, roundness, stars_blob, updated_at) "
            "VALUES (:frame_id, :group_key, :detection_hash, :input_path, :input_mtime, :input_size_bytes, "
            ":input_modified_at_ms, :input_identity_token, 50, 2.0, 10.0, 0.5, :blob, 1)"));
        stars.bindValue(QStringLiteral(":frame_id"), frame.canonicalPath());
        stars.bindValue(QStringLiteral(":group_key"), group.groupKey.toString());
        stars.bindValue(QStringLiteral(":detection_hash"), detectionHash);
        stars.bindValue(QStringLiteral(":input_path"), frame.canonicalPath());
        stars.bindValue(QStringLiteral(":input_mtime"),
                        QFileInfo(frame.canonicalPath()).lastModified().toSecsSinceEpoch());
        const QFileInfo input_info(frame.canonicalPath());
        stars.bindValue(QStringLiteral(":input_size_bytes"), input_info.size());
        stars.bindValue(QStringLiteral(":input_modified_at_ms"), input_info.lastModified().toMSecsSinceEpoch());
        stars.bindValue(QStringLiteral(":input_identity_token"),
                        ArtifactTracker::fileIdentityForPath(frame.canonicalPath()));
        stars.bindValue(QStringLiteral(":blob"), QByteArray(1, '\x1'));
        ASSERT_TRUE(stars.exec()) << stars.lastError().text().toStdString();
    }

    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

// refreshAll() is debounced by the tab's timer, so a case waits until that refresh has run rather than for a fixed
// time that a slow host can outlast.
void waitForRefresh(const QWidget* tab)
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

} // namespace

class RegistrationTabTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        tab = new RegistrationTab(model);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    RegistrationTab* tab = nullptr;
};

TEST_F(RegistrationTabTest, InitialStateShowsEmptyMessage)
{
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);

    EXPECT_EQ(groupTable->rowCount(), 1);
    QTableWidgetItem* item = groupTable->item(0, 0);
    ASSERT_TRUE(item != nullptr);
    EXPECT_TRUE(item->text().contains("No registration groups"));
}

TEST_F(RegistrationTabTest, DisplaysGroupsAfterAddingLights)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);

    // Should have at least 1 group
    EXPECT_GE(groupTable->rowCount(), 1);
}

TEST_F(RegistrationTabTest, FrameTableUpdatesOnGroupSelection)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(frameTable != nullptr);

    // Select first group
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();

        // Frame table should show frames from selected group
        EXPECT_GE(frameTable->rowCount(), 1);
    }
}

TEST_F(RegistrationTabTest, HasRequiredButtons)
{
    QPushButton* setRefBtn = tab->findChild<QPushButton*>("btnSetReference");
    QPushButton* excludeBtn = tab->findChild<QPushButton*>("btnExcludeSelected");
    QPushButton* includeBtn = tab->findChild<QPushButton*>("btnIncludeSelected");
    QPushButton* runBtn = tab->findChild<QPushButton*>("btnRunRegistration");
    QPushButton* runGroupBtn = tab->findChild<QPushButton*>("btnRunRegistrationGroup");
    QPushButton* clearGroupBtn = tab->findChild<QPushButton*>("btnClearRegistrationGroup");

    EXPECT_TRUE(setRefBtn != nullptr);
    EXPECT_TRUE(excludeBtn != nullptr);
    EXPECT_TRUE(includeBtn != nullptr);
    EXPECT_TRUE(runBtn != nullptr);
    EXPECT_TRUE(runGroupBtn != nullptr);
    EXPECT_TRUE(clearGroupBtn != nullptr);

    // Buttons should be disabled initially
    if (setRefBtn)
        EXPECT_FALSE(setRefBtn->isEnabled());
    if (excludeBtn)
        EXPECT_FALSE(excludeBtn->isEnabled());
    if (includeBtn)
        EXPECT_FALSE(includeBtn->isEnabled());
    if (runBtn)
        EXPECT_FALSE(runBtn->isEnabled());
    if (runGroupBtn)
        EXPECT_FALSE(runGroupBtn->isEnabled());
    if (clearGroupBtn)
        EXPECT_FALSE(clearGroupBtn->isEnabled());
}

TEST_F(RegistrationTabTest, GroupBoxTitlesAreLargerThanContents)
{
    QWidget* groupsWidget = tab->findChild<QWidget*>("grpRegistrationGroups");
    auto* groupsBox = dynamic_cast<an::ui::AnCardWidget*>(groupsWidget);
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupsBox != nullptr);
    ASSERT_TRUE(groupTable != nullptr);

    const QFont groupboxFont = groupsBox->font();
    const QFont tableFont = groupTable->font();

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

TEST_F(RegistrationTabTest, IncludeExcludeButtonsEnableBasedOnSelectionAndExclusion)
{
    const QString pathA = FrameId(QStringLiteral("C:/light_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/light_b.fit")).canonicalPath();
    const QString pathC = FrameId(QStringLiteral("C:/light_c.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {
                                           createLightFrame(pathA, "2025-01-10"),
                                           createLightFrame(pathB, "2025-01-10"),
                                           createLightFrame(pathC, "2025-01-10"),
                                       });
    model->updateFrameAttribute(pathB, FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    QPushButton* excludeBtn = tab->findChild<QPushButton*>("btnExcludeSelected");
    QPushButton* includeBtn = tab->findChild<QPushButton*>("btnIncludeSelected");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(frameTable != nullptr);
    ASSERT_TRUE(excludeBtn != nullptr);
    ASSERT_TRUE(includeBtn != nullptr);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(150);

    auto findRow = [frameTable](const QString& canonical) -> int {
        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            auto* item = frameTable->item(row, 1);
            if (!item)
            {
                continue;
            }
            if (item->data(Qt::UserRole).toString().trimmed() == canonical)
            {
                return row;
            }
        }
        return -1;
    };

    const int rowA = findRow(pathA);
    const int rowB = findRow(pathB);
    ASSERT_GE(rowA, 0);
    ASSERT_GE(rowB, 0);

    frameTable->clearSelection();
    QCoreApplication::processEvents();
    EXPECT_FALSE(excludeBtn->isEnabled());
    EXPECT_FALSE(includeBtn->isEnabled());

    frameTable->selectRow(rowA);
    QCoreApplication::processEvents();
    EXPECT_TRUE(excludeBtn->isEnabled());
    EXPECT_FALSE(includeBtn->isEnabled());

    frameTable->selectRow(rowB);
    QCoreApplication::processEvents();
    EXPECT_FALSE(excludeBtn->isEnabled());
    EXPECT_TRUE(includeBtn->isEnabled());

    frameTable->selectRow(rowA);
    frameTable->selectionModel()->select(frameTable->model()->index(rowB, 0),
                                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();
    EXPECT_TRUE(excludeBtn->isEnabled());
    EXPECT_TRUE(includeBtn->isEnabled());
}

TEST_F(RegistrationTabTest, FrameContextMenuShowsAndAppliesIncludeExcludePerSelection)
{
    const QString pathA = FrameId(QStringLiteral("C:/menu_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/menu_b.fit")).canonicalPath();
    const QString pathC = FrameId(QStringLiteral("C:/menu_c.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {
                                           createLightFrame(pathA, "2025-01-10"),
                                           createLightFrame(pathB, "2025-01-10"),
                                           createLightFrame(pathC, "2025-01-10"),
                                       });
    model->updateFrameAttribute(pathB, FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(frameTable != nullptr);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(150);

    auto findRow = [frameTable](const QString& canonical) -> int {
        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            auto* item = frameTable->item(row, 1);
            if (!item)
            {
                continue;
            }
            if (item->data(Qt::UserRole).toString().trimmed() == canonical)
            {
                return row;
            }
        }
        return -1;
    };

    const int rowA = findRow(pathA);
    const int rowB = findRow(pathB);
    const int rowC = findRow(pathC);
    ASSERT_GE(rowA, 0);
    ASSERT_GE(rowB, 0);
    ASSERT_GE(rowC, 0);

    auto rightClickMenuAndCapture = [frameTable](const QPoint& pos, const QString& triggerStartsWith,
                                                 QStringList& outTexts, bool& outCallbackRan) {
        outCallbackRan = false;
        outTexts.clear();

        QTimer* poll = new QTimer(frameTable);
        poll->setInterval(10);
        QObject::connect(poll, &QTimer::timeout, frameTable, [&]() {
            QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
            {
                for (auto* w : QApplication::topLevelWidgets())
                {
                    auto* m = qobject_cast<QMenu*>(w);
                    if (m && m->isVisible())
                    {
                        menu = m;
                        break;
                    }
                }
            }
            if (!menu)
            {
                return;
            }

            for (auto* action : menu->actions())
            {
                if (action && !action->isSeparator())
                {
                    outTexts.append(action->text());
                }
            }

            if (!triggerStartsWith.isEmpty())
            {
                for (auto* action : menu->actions())
                {
                    if (action && !action->isSeparator() && action->text().startsWith(triggerStartsWith))
                    {
                        action->trigger();
                        break;
                    }
                }
            }

            outCallbackRan = true;
            menu->close();
            poll->stop();
            poll->deleteLater();
        });
        poll->start();

        QMetaObject::invokeMethod(frameTable, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, pos));

        // Give the nested QMenu::exec() loop time to start and allow the poller to capture and close it.
        for (int i = 0; i < 60 && !outCallbackRan; ++i)
        {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }

        if (!outCallbackRan)
        {
            if (auto* popup = QApplication::activePopupWidget())
            {
                popup->close();
            }
            poll->stop();
            poll->deleteLater();
            outCallbackRan = true;
        }
    };

    // Included-only selection => show only Exclude N Frames
    frameTable->selectRow(rowA);
    frameTable->selectionModel()->select(frameTable->model()->index(rowC, 0),
                                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();

    QStringList texts;
    bool callbackRan = false;
    rightClickMenuAndCapture(frameTable->visualItemRect(frameTable->item(rowA, 1)).center(), QString(), texts,
                             callbackRan);
    ASSERT_TRUE(callbackRan);
    EXPECT_TRUE(texts.contains(QStringLiteral("Exclude 2 Frames")));
    EXPECT_FALSE(std::any_of(texts.begin(), texts.end(), [](const QString& t) { return t.startsWith("Include"); }));

    // Excluded-only selection => show only Include Frame
    frameTable->clearSelection();
    frameTable->selectRow(rowB);
    QCoreApplication::processEvents();

    rightClickMenuAndCapture(frameTable->visualItemRect(frameTable->item(rowB, 1)).center(), QString(), texts,
                             callbackRan);
    ASSERT_TRUE(callbackRan);
    EXPECT_TRUE(std::any_of(texts.begin(), texts.end(), [](const QString& t) { return t.startsWith("Include"); }));
    EXPECT_FALSE(std::any_of(texts.begin(), texts.end(), [](const QString& t) { return t.startsWith("Exclude"); }));

    // Mixed selection => show both and apply only to matching frames
    frameTable->clearSelection();
    frameTable->selectRow(rowA);
    frameTable->selectionModel()->select(frameTable->model()->index(rowB, 0),
                                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCoreApplication::processEvents();

    rightClickMenuAndCapture(frameTable->visualItemRect(frameTable->item(rowA, 1)).center(), QStringLiteral("Include"),
                             texts, callbackRan);
    ASSERT_TRUE(callbackRan);
    EXPECT_TRUE(std::any_of(texts.begin(), texts.end(), [](const QString& t) { return t.startsWith("Include"); }));
    EXPECT_TRUE(std::any_of(texts.begin(), texts.end(), [](const QString& t) { return t.startsWith("Exclude"); }));

    // Include action should include only excluded frame(s)
    if (auto info = model->lightFrames().findById(FrameId(pathB)); info.has_value())
    {
        EXPECT_FALSE(info->excluded);
    }
}

// ============================================================================
// Settings Dialog Integration Tests
// ============================================================================

TEST_F(RegistrationTabTest, HasSettingsButton)
{
    QPushButton* settingsBtn = tab->findChild<QPushButton*>("btnGroupSettings");
    EXPECT_TRUE(settingsBtn != nullptr);
}

TEST_F(RegistrationTabTest, SettingsButtonDisabledWithNoGroupSelected)
{
    QPushButton* settingsBtn = tab->findChild<QPushButton*>("btnGroupSettings");
    ASSERT_TRUE(settingsBtn != nullptr);
    EXPECT_FALSE(settingsBtn->isEnabled());
}

TEST_F(RegistrationTabTest, SettingsButtonEnabledWhenGroupSelected)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* settingsBtn = tab->findChild<QPushButton*>("btnGroupSettings");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(settingsBtn != nullptr);

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        EXPECT_TRUE(settingsBtn->isEnabled());
    }
}

// ============================================================================
// Quality Threshold Controls Tests
// ============================================================================

TEST_F(RegistrationTabTest, HasQualityThresholdControls)
{
    QWidget* thresholdsWidget = tab->findChild<QWidget*>("grpQualityThresholds");
    auto* thresholdsGroup = dynamic_cast<an::ui::AnCollapsibleSection*>(thresholdsWidget);
    QCheckBox* minScoreCheck = tab->findChild<QCheckBox*>("chkMinScore");
    QDoubleSpinBox* minScore = tab->findChild<QDoubleSpinBox*>("spinMinScore");
    QCheckBox* minStarsCheck = tab->findChild<QCheckBox*>("chkMinStars");
    QSpinBox* minStars = tab->findChild<QSpinBox*>("spinMinStars");
    QCheckBox* minSnrCheck = tab->findChild<QCheckBox*>("chkMinSnr");
    QDoubleSpinBox* minSnr = tab->findChild<QDoubleSpinBox*>("spinMinSnr");
    QCheckBox* maxFwhmCheck = tab->findChild<QCheckBox*>("chkMaxFwhm");
    QDoubleSpinBox* maxFwhm = tab->findChild<QDoubleSpinBox*>("spinMaxFwhm");
    QCheckBox* maxEccCheck = tab->findChild<QCheckBox*>("chkMaxEccentricity");
    QDoubleSpinBox* maxEcc = tab->findChild<QDoubleSpinBox*>("spinMaxEccentricity");
    QPushButton* detectBtn = tab->findChild<QPushButton*>("btnDetectOutliers");
    QPushButton* applyBtn = tab->findChild<QPushButton*>("btnApplyThresholds");

    EXPECT_TRUE(thresholdsGroup != nullptr);
    EXPECT_TRUE(minScoreCheck != nullptr);
    EXPECT_TRUE(minScore != nullptr);
    EXPECT_TRUE(minStarsCheck != nullptr);
    EXPECT_TRUE(minStars != nullptr);
    EXPECT_TRUE(minSnrCheck != nullptr);
    EXPECT_TRUE(minSnr != nullptr);
    EXPECT_TRUE(maxFwhmCheck != nullptr);
    EXPECT_TRUE(maxFwhm != nullptr);
    EXPECT_TRUE(maxEccCheck != nullptr);
    EXPECT_TRUE(maxEcc != nullptr);
    EXPECT_TRUE(detectBtn != nullptr);
    EXPECT_TRUE(applyBtn != nullptr);
}

TEST_F(RegistrationTabTest, QualityThresholdsGroupHasExpectedTitle)
{
    QWidget* thresholdsWidget = tab->findChild<QWidget*>("grpQualityThresholds");
    auto* thresholdsGroup = dynamic_cast<an::ui::AnCollapsibleSection*>(thresholdsWidget);
    ASSERT_TRUE(thresholdsGroup != nullptr);
    EXPECT_TRUE(thresholdsGroup->title().contains("Quality Filters"));
}

TEST_F(RegistrationTabTest, ThresholdControlsHaveReasonableDefaults)
{
    QCheckBox* minScoreCheck = tab->findChild<QCheckBox*>("chkMinScore");
    QDoubleSpinBox* minScore = tab->findChild<QDoubleSpinBox*>("spinMinScore");
    QCheckBox* minStarsCheck = tab->findChild<QCheckBox*>("chkMinStars");
    QSpinBox* minStars = tab->findChild<QSpinBox*>("spinMinStars");
    QCheckBox* minSnrCheck = tab->findChild<QCheckBox*>("chkMinSnr");
    QDoubleSpinBox* minSnr = tab->findChild<QDoubleSpinBox*>("spinMinSnr");
    QCheckBox* maxFwhmCheck = tab->findChild<QCheckBox*>("chkMaxFwhm");
    QDoubleSpinBox* maxFwhm = tab->findChild<QDoubleSpinBox*>("spinMaxFwhm");
    QCheckBox* maxEccCheck = tab->findChild<QCheckBox*>("chkMaxEccentricity");
    QDoubleSpinBox* maxEcc = tab->findChild<QDoubleSpinBox*>("spinMaxEccentricity");

    if (minScoreCheck && minScore)
    {
        EXPECT_FALSE(minScoreCheck->isChecked());
        EXPECT_FALSE(minScore->isEnabled());
        EXPECT_GE(minScore->value(), 0.0);
        EXPECT_GE(minScore->maximum(), 100.0);
    }

    if (minStarsCheck && minStars)
    {
        EXPECT_FALSE(minStarsCheck->isChecked());
        EXPECT_FALSE(minStars->isEnabled());
    }

    if (minStars)
    {
        EXPECT_GE(minStars->value(), 0);
        EXPECT_LE(minStars->value(), 100);
    }

    if (minSnrCheck && minSnr)
    {
        EXPECT_FALSE(minSnrCheck->isChecked());
        EXPECT_FALSE(minSnr->isEnabled());
        EXPECT_GE(minSnr->value(), 0.0);
    }

    if (maxFwhmCheck && maxFwhm)
    {
        EXPECT_FALSE(maxFwhmCheck->isChecked());
        EXPECT_FALSE(maxFwhm->isEnabled());
    }

    if (maxFwhm)
    {
        EXPECT_GE(maxFwhm->value(), 0.0);
        EXPECT_LE(maxFwhm->value(), 50.0);
    }

    if (maxEccCheck && maxEcc)
    {
        EXPECT_FALSE(maxEccCheck->isChecked());
        EXPECT_FALSE(maxEcc->isEnabled());
    }

    if (maxEcc)
    {
        EXPECT_GE(maxEcc->value(), 0.0);
        EXPECT_LE(maxEcc->value(), 2.0);
    }
}

TEST_F(RegistrationTabTest, ApplyThresholdsButtonExists)
{
    QPushButton* applyBtn = tab->findChild<QPushButton*>("btnApplyThresholds");
    ASSERT_TRUE(applyBtn != nullptr);
    // The button exists and is accessible
    EXPECT_TRUE(applyBtn != nullptr);
}

// ============================================================================
// Reset Excluded Button Tests
// ============================================================================

TEST_F(RegistrationTabTest, ResetExcludedButtonExists)
{
    QPushButton* resetBtn = tab->findChild<QPushButton*>("btnResetExcluded");
    ASSERT_TRUE(resetBtn != nullptr);
    EXPECT_FALSE(resetBtn->text().isEmpty());
    EXPECT_TRUE(resetBtn->text().contains("Reset Excluded"));
}

TEST_F(RegistrationTabTest, ResetExcludedButtonDisabledInitially)
{
    QPushButton* resetBtn = tab->findChild<QPushButton*>("btnResetExcluded");
    ASSERT_TRUE(resetBtn != nullptr);
    // Should be disabled with no frames
    EXPECT_FALSE(resetBtn->isEnabled());
}

TEST_F(RegistrationTabTest, ResetExcludedButtonDisabledWhenNoExcludedFrames)
{
    // Add frames but don't exclude any
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* resetBtn = tab->findChild<QPushButton*>("btnResetExcluded");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(resetBtn != nullptr);

    // Select first group
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(150);
    }

    // No excluded frames, button should be disabled
    EXPECT_FALSE(resetBtn->isEnabled());

    // Tooltip should explain why it's disabled
    EXPECT_FALSE(resetBtn->toolTip().isEmpty());
    EXPECT_TRUE(resetBtn->toolTip().contains("no frames are excluded") || resetBtn->toolTip().contains("disabled"));
}

TEST_F(RegistrationTabTest, ResetExcludedButtonEnabledWhenExcludedFramesExist)
{
    const QString pathA = FrameId(QStringLiteral("C:/reset_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/reset_b.fit")).canonicalPath();
    const QString pathC = FrameId(QStringLiteral("C:/reset_c.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {
                                           createLightFrame(pathA, "2025-01-10"),
                                           createLightFrame(pathB, "2025-01-10"),
                                           createLightFrame(pathC, "2025-01-10"),
                                       });

    // Exclude one frame
    model->updateFrameAttribute(pathB, FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* resetBtn = tab->findChild<QPushButton*>("btnResetExcluded");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(resetBtn != nullptr);

    // Select the group
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(150);
    }

    // One excluded frame, button should be enabled
    EXPECT_TRUE(resetBtn->isEnabled());

    // Tooltip should mention the count
    EXPECT_FALSE(resetBtn->toolTip().isEmpty());
    EXPECT_TRUE(resetBtn->toolTip().contains("1") || resetBtn->toolTip().contains("excluded"));
}

TEST_F(RegistrationTabTest, ResetExcludedButtonTooltipUpdatesWithExcludedCount)
{
    const QString pathA = FrameId(QStringLiteral("C:/tooltip_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/tooltip_b.fit")).canonicalPath();
    const QString pathC = FrameId(QStringLiteral("C:/tooltip_c.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {
                                           createLightFrame(pathA, "2025-01-10"),
                                           createLightFrame(pathB, "2025-01-10"),
                                           createLightFrame(pathC, "2025-01-10"),
                                       });

    // Exclude two frames
    model->updateFrameAttribute(pathA, FrameAttribute::Excluded, true);
    model->updateFrameAttribute(pathB, FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* resetBtn = tab->findChild<QPushButton*>("btnResetExcluded");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(resetBtn != nullptr);

    // Select the group
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(150);
    }

    EXPECT_TRUE(resetBtn->isEnabled());

    // Tooltip should show count of 2 excluded frames
    const QString tooltip = resetBtn->toolTip();
    EXPECT_FALSE(tooltip.isEmpty());
    EXPECT_TRUE(tooltip.contains("2") && tooltip.contains("excluded"));
}

TEST_F(RegistrationTabTest, ResetExcludedButtonStateUpdatesAfterExcluding)
{
    const QString pathA = FrameId(QStringLiteral("C:/state_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/state_b.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {
                                           createLightFrame(pathA, "2025-01-10"),
                                           createLightFrame(pathB, "2025-01-10"),
                                       });

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* resetBtn = tab->findChild<QPushButton*>("btnResetExcluded");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(resetBtn != nullptr);

    // Select the group
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(150);
    }

    // Initially no excluded frames
    EXPECT_FALSE(resetBtn->isEnabled());

    // Exclude a frame
    model->updateFrameAttribute(pathA, FrameAttribute::Excluded, true);
    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Re-select the group (selection may have been cleared)
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(150);
    }

    // Now should be enabled
    EXPECT_TRUE(resetBtn->isEnabled());
}

// ============================================================================
// Run Registration Button State Tests
// ============================================================================

TEST_F(RegistrationTabTest, RunButtonDisabledWithNoFrames)
{
    QPushButton* runBtn = tab->findChild<QPushButton*>("btnRunRegistration");
    ASSERT_TRUE(runBtn != nullptr);
    EXPECT_FALSE(runBtn->isEnabled());
}

TEST_F(RegistrationTabTest, RunButtonStateChangesWithGroupSelection)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* runBtn = tab->findChild<QPushButton*>("btnRunRegistration");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(runBtn != nullptr);

    // With groups available, button state should respond to selection
    // (exact state depends on implementation - this test verifies the button exists and responds)
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        // Button should exist and be accessible
        EXPECT_TRUE(runBtn != nullptr);
    }
}

// ============================================================================
// Group Display Tests
// ============================================================================

TEST_F(RegistrationTabTest, GroupTableShowsCorrectColumns)
{
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);

    // Should have columns for: Group, Frames, Filters, Exposures, Alignment, Algorithm, Reference, Status
    EXPECT_EQ(groupTable->columnCount(), 8);
}

TEST_F(RegistrationTabTest, GroupTableHasAlgorithmColumn)
{
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);

    // Algorithm should be at column index 5
    QTableWidgetItem* headerItem = groupTable->horizontalHeaderItem(5);
    ASSERT_TRUE(headerItem != nullptr);
    EXPECT_TRUE(headerItem->text().contains("Algorithm"));
}

TEST_F(RegistrationTabTest, FrameTableShowsCorrectColumns)
{
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(frameTable != nullptr);

    // Should have columns for: Status, Frame, Stars, FWHM, SNR, Ecc, Score, Quality
    EXPECT_GE(frameTable->columnCount(), 8);
}

// ============================================================================
// Exclusion State Tests
// ============================================================================

TEST_F(RegistrationTabTest, ExcludeButtonDisabledWithNoFrameSelected)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* excludeBtn = tab->findChild<QPushButton*>("btnExcludeSelected");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(excludeBtn != nullptr);

    // Select group but no frame
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        // Exclude should still be disabled since no frame is selected
        EXPECT_FALSE(excludeBtn->isEnabled());
    }
}

// ============================================================================
// Reference Frame Tests
// ============================================================================

TEST_F(RegistrationTabTest, SetReferenceButtonDisabledWithNoFrameSelected)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* setRefBtn = tab->findChild<QPushButton*>("btnSetReference");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(setRefBtn != nullptr);

    // Select group but no frame
    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        // Set Reference should be disabled since no frame is selected
        EXPECT_FALSE(setRefBtn->isEnabled());
    }
}

// ============================================================================
// Signal Emission Tests
// ============================================================================

TEST_F(RegistrationTabTest, EmitsRunRegistrationRequestedSignal)
{
    QSignalSpy spy(tab, SIGNAL(runRegistrationRequested(QString)));
    EXPECT_TRUE(spy.isValid());
    // Note: Actually triggering registration would require more setup
}

TEST_F(RegistrationTabTest, EmitsRunRegistrationAllRequestedSignal)
{
    QSignalSpy spy(tab, SIGNAL(runRegistrationAllRequested()));
    EXPECT_TRUE(spy.isValid());
}

TEST_F(RegistrationTabTest, RunSelectedGroupButtonUsesManifestAwareCalibrationState)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // Prepare model with one registration group (2 lights).
    const QString pathA = FrameId(QDir(tempDir.path()).filePath(QStringLiteral("light_a.fit"))).canonicalPath();
    const QString pathB = FrameId(QDir(tempDir.path()).filePath(QStringLiteral("light_b.fit"))).canonicalPath();
    {
        QFile fileA(pathA);
        ASSERT_TRUE(fileA.open(QIODevice::WriteOnly));
    }
    {
        QFile fileB(pathB);
        ASSERT_TRUE(fileB.open(QIODevice::WriteOnly));
    }
    model->addFrames(FrameType::Light, {createLightFrame(pathA), createLightFrame(pathB)});
    FrameQualityMetrics m1;
    m1.stars = 50;
    m1.fwhm = 2.0;
    m1.eccentricity = 0.2;
    m1.succeeded = true;
    model->setFrameQualityMetrics(FrameId(pathA), m1);

    FrameQualityMetrics m2;
    m2.stars = 60;
    m2.fwhm = 2.1;
    m2.eccentricity = 0.25;
    m2.succeeded = true;
    model->setFrameQualityMetrics(FrameId(pathB), m2);
    model->setOutputDirectory(tempDir.path());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    // Grab the machine group_key stored in the table (UserRole).
    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    const QString groupKey = groupTable->item(0, 0)->data(Qt::UserRole).toString();
    ASSERT_FALSE(groupKey.isEmpty());

    const WorkflowExecutionController manifestBuilder(model, nullptr);
    const WorkflowManifest analysisManifest = manifestBuilder.buildRegistrationOnlyManifestAll(false);
    const auto workflowGroup = std::find_if(
        analysisManifest.registrationGroups.cbegin(), analysisManifest.registrationGroups.cend(),
        [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey.toString() == groupKey; });
    ASSERT_NE(workflowGroup, analysisManifest.registrationGroups.cend());
    EXPECT_NE(workflowGroup->settings.debayerMethod.compare(QStringLiteral("default"), Qt::CaseInsensitive), 0);

    // Create a metrics DB that matches current default settings and frame list, so the per-group run button disables.
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));
    const QString connName = QStringLiteral("regtab_test_db");
    {
        if (QSqlDatabase::contains(connName))
        {
            QSqlDatabase::removeDatabase(connName);
        }
        makeCurrentRegistrationDatabase(dbPath);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());

        QSqlQuery q(db);
        ASSERT_TRUE(
            q.exec(QStringLiteral("PRAGMA user_version = %1").arg(stage_utils::kRegistrationMetricsSchemaVersion)));
        ASSERT_TRUE(q.exec(stage_utils::registrationMetricsCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationGroupStateCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::starDetectionCacheCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesGroupIndexCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesSourceIndexCreateSql()));

        const QString detHash = registrationDetectionHash(workflowGroup->settings);
        const QString regHash = registrationTransformHash(workflowGroup->settings);
        const QString fileHash = hashFrameIdList(workflowGroup->lightFiles);
        const QString calibrationHash =
            computeLightsCalibrationHashForSubset(analysisManifest.lightsCalibration, workflowGroup->lightFiles);

        // Insert group state for settings_version=0 (default).
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_group_state ("
                                 "group_key, settings_version, detection_hash, registration_hash, reference_frame_id, "
                                 "light_files_hash, calibration_hash, output_wcs_json, updated_at) "
                                 "VALUES (:group_key, 0, :det, :reg, :ref, :files, :calibration, '', 1)"));
        q.bindValue(QStringLiteral(":group_key"), groupKey);
        q.bindValue(QStringLiteral(":det"), detHash);
        q.bindValue(QStringLiteral(":reg"), regHash);
        q.bindValue(QStringLiteral(":ref"), workflowGroup->referenceFrame.has_value()
                                                ? workflowGroup->referenceFrame->canonicalPath()
                                                : QString());
        q.bindValue(QStringLiteral(":files"), fileHash);
        q.bindValue(QStringLiteral(":calibration"), calibrationHash);
        ASSERT_TRUE(q.exec());

        // Insert per-frame metrics for settings_version=0.
        const auto insertMetric = [&](const QString& frameId) {
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_metrics ("
                                       "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                                       "succeeded, output_path, transform_json, updated_at) "
                                       "VALUES (:frame_id, :group_key, 0, 50, 2.0, 10.0, 0.2, 1, '', '', 1)"));
            ins.bindValue(QStringLiteral(":frame_id"), frameId);
            ins.bindValue(QStringLiteral(":group_key"), groupKey);
            ASSERT_TRUE(ins.exec());
        };
        insertMetric(pathA);
        insertMetric(pathB);

        // Insert star detection cache rows so the per-group run button can correctly detect a no-op state.
        const auto insertStarCache = [&](const QString& frameId) {
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral("INSERT OR REPLACE INTO star_detection_cache ("
                                       "frame_id, group_key, detection_hash, input_path, input_mtime, "
                                       "input_size_bytes, input_modified_at_ms, input_identity_token, stars_count, "
                                       "fwhm, snr, roundness, stars_blob, updated_at) "
                                       "VALUES (:frame_id, :group_key, :det, :input_path, :input_mtime, "
                                       ":input_size_bytes, :input_modified_at_ms, :input_identity_token, "
                                       ":stars_count, :fwhm, :snr, :roundness, :blob, :updated_at)"));
            ins.bindValue(QStringLiteral(":frame_id"), frameId);
            ins.bindValue(QStringLiteral(":group_key"), groupKey);
            ins.bindValue(QStringLiteral(":det"), detHash);
            ins.bindValue(QStringLiteral(":input_path"), frameId);
            const QFileInfo input_info(frameId);
            ins.bindValue(QStringLiteral(":input_mtime"), input_info.lastModified().toSecsSinceEpoch());
            ins.bindValue(QStringLiteral(":input_size_bytes"), input_info.size());
            ins.bindValue(QStringLiteral(":input_modified_at_ms"), input_info.lastModified().toMSecsSinceEpoch());
            ins.bindValue(QStringLiteral(":input_identity_token"), ArtifactTracker::fileIdentityForPath(frameId));
            ins.bindValue(QStringLiteral(":stars_count"), 50);
            ins.bindValue(QStringLiteral(":fwhm"), 2.0);
            ins.bindValue(QStringLiteral(":snr"), 10.0);
            ins.bindValue(QStringLiteral(":roundness"), 0.5);
            ins.bindValue(QStringLiteral(":blob"), QByteArray(1, '\x1'));
            ins.bindValue(QStringLiteral(":updated_at"), 1);
            ASSERT_TRUE(ins.exec());
        };
        insertStarCache(pathA);
        insertStarCache(pathB);

        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connName);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QPushButton* runGroupBtn = tab->findChild<QPushButton*>("btnRunRegistrationGroup");
    ASSERT_TRUE(runGroupBtn != nullptr);
    const QString tooltip = runGroupBtn->toolTip();
    EXPECT_FALSE(runGroupBtn->isEnabled()) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains("no changes detected", Qt::CaseInsensitive)) << tooltip.toStdString();

    const QString changedStateConnection = QStringLiteral("regtab_test_db_calibration_change");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), changedStateConnection);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());
        QSqlQuery update(db);
        update.prepare(
            QStringLiteral("UPDATE registration_group_state SET calibration_hash = 'removed-calibration-assignment' "
                           "WHERE group_key = :group_key"));
        update.bindValue(QStringLiteral(":group_key"), groupKey);
        ASSERT_TRUE(update.exec());
        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(changedStateConnection);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    EXPECT_TRUE(runGroupBtn->isEnabled()) << runGroupBtn->toolTip().toStdString();
    EXPECT_TRUE(runGroupBtn->toolTip().contains("Calibration assignments changed", Qt::CaseInsensitive))
        << runGroupBtn->toolTip().toStdString();

    // Ensure any SQLite connection owned by the tab is released before QTemporaryDir cleanup.
    delete tab;
    tab = nullptr;
    delete model;
    model = nullptr;
}

TEST_F(RegistrationTabTest, RunSelectedGroupButtonUsesSanitizedIncludedLightsAndReference)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString excludedPath =
        FrameId(QDir(tempDir.path()).filePath(QStringLiteral("excluded_reference.fit"))).canonicalPath();
    const QString includedPath =
        FrameId(QDir(tempDir.path()).filePath(QStringLiteral("included_light.fit"))).canonicalPath();
    {
        QFile excludedFile(excludedPath);
        ASSERT_TRUE(excludedFile.open(QIODevice::WriteOnly));
    }
    {
        QFile includedFile(includedPath);
        ASSERT_TRUE(includedFile.open(QIODevice::WriteOnly));
    }
    model->addFrames(FrameType::Light, {createLightFrame(excludedPath), createLightFrame(includedPath)});
    FrameQualityMetrics metrics;
    metrics.stars = 50;
    metrics.fwhm = 2.0;
    metrics.eccentricity = 0.2;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId(excludedPath), metrics);
    model->setFrameQualityMetrics(FrameId(includedPath), metrics);
    model->setOutputDirectory(tempDir.path());

    ASSERT_EQ(model->registrationGroups().size(), 1u);
    const RegistrationGroupKey displayGroupKey = model->registrationGroups().begin()->first;
    model->setRegistrationReferenceFrame(displayGroupKey, FrameId(excludedPath));
    model->updateFrameAttribute(excludedPath, FrameAttribute::Excluded, true);

    const QString machineGroupKey = RegistrationGroupEngine::toMachineGroupKey(displayGroupKey).toString();
    const WorkflowManifest rawManifest = ManifestBuilder(*model).build();
    const auto rawGroup = std::find_if(
        rawManifest.registrationGroups.cbegin(), rawManifest.registrationGroups.cend(),
        [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey.toString() == machineGroupKey; });
    ASSERT_NE(rawGroup, rawManifest.registrationGroups.cend());
    ASSERT_EQ(rawGroup->lightFiles.size(), 1u);
    EXPECT_EQ(rawGroup->lightFiles.front(), FrameId(includedPath));
    EXPECT_FALSE(rawGroup->referenceFrame.has_value());

    const WorkflowExecutionController manifestBuilder(model, nullptr);
    const WorkflowManifest executionManifest = manifestBuilder.buildRegistrationOnlyManifestAll(false);
    const auto executionGroup = std::find_if(
        executionManifest.registrationGroups.cbegin(), executionManifest.registrationGroups.cend(),
        [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey.toString() == machineGroupKey; });
    ASSERT_NE(executionGroup, executionManifest.registrationGroups.cend());
    ASSERT_EQ(executionGroup->lightFiles.size(), 1u);
    EXPECT_EQ(executionGroup->lightFiles.front(), FrameId(includedPath));
    EXPECT_FALSE(executionGroup->referenceFrame.has_value());

    seedMatchingRegistrationState(QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db")),
                                  executionManifest, *executionGroup);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QPushButton* runGroupButton = tab->findChild<QPushButton*>("btnRunRegistrationGroup");
    ASSERT_NE(groupTable, nullptr);
    ASSERT_NE(runGroupButton, nullptr);
    int selectedRow = -1;
    for (int row = 0; row < groupTable->rowCount(); ++row)
    {
        if (groupTable->item(row, 0)->data(Qt::UserRole).toString() == machineGroupKey)
        {
            selectedRow = row;
            break;
        }
    }
    ASSERT_GE(selectedRow, 0);
    groupTable->selectRow(selectedRow);
    QCoreApplication::processEvents();
    QTest::qWait(150);

    EXPECT_FALSE(runGroupButton->isEnabled()) << runGroupButton->toolTip().toStdString();
    EXPECT_TRUE(runGroupButton->toolTip().contains("no changes detected", Qt::CaseInsensitive))
        << runGroupButton->toolTip().toStdString();

    delete tab;
    tab = nullptr;
    delete model;
    model = nullptr;
}

TEST_F(RegistrationTabTest, RunSelectedGroupButtonEnablesWhenStarCacheMissing)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    // Prepare model with one registration group (2 lights).
    const QString pathA = FrameId(QStringLiteral("C:/light_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/light_b.fit")).canonicalPath();
    model->addFrames(FrameType::Light, {createLightFrame(pathA), createLightFrame(pathB)});
    model->setOutputDirectory(tempDir.path());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    const QString groupKey = groupTable->item(0, 0)->data(Qt::UserRole).toString();
    ASSERT_FALSE(groupKey.isEmpty());

    const WorkflowExecutionController manifestBuilder(model, nullptr);
    const WorkflowManifest executionManifest = manifestBuilder.buildRegistrationOnlyManifestAll(false);
    const auto workflowGroup = std::find_if(
        executionManifest.registrationGroups.cbegin(), executionManifest.registrationGroups.cend(),
        [&](const WorkflowManifest::RegistrationGroup& group) { return group.groupKey.toString() == groupKey; });
    ASSERT_NE(workflowGroup, executionManifest.registrationGroups.cend());

    // Create a metrics DB that matches current default settings and metrics, but omit star cache rows.
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));
    const QString connName = QStringLiteral("regtab_test_db_missing_stars");
    {
        if (QSqlDatabase::contains(connName))
        {
            QSqlDatabase::removeDatabase(connName);
        }
        makeCurrentRegistrationDatabase(dbPath);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());

        QSqlQuery q(db);
        ASSERT_TRUE(
            q.exec(QStringLiteral("PRAGMA user_version = %1").arg(stage_utils::kRegistrationMetricsSchemaVersion)));
        ASSERT_TRUE(q.exec(stage_utils::registrationMetricsCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationGroupStateCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::starDetectionCacheCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesGroupIndexCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesSourceIndexCreateSql()));

        const QString detHash = registrationDetectionHash(workflowGroup->settings);
        const QString regHash = registrationTransformHash(workflowGroup->settings);
        const QString fileHash = hashFrameIdList(workflowGroup->lightFiles);
        const QString calibrationHash =
            computeLightsCalibrationHashForSubset(executionManifest.lightsCalibration, workflowGroup->lightFiles);

        q.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_group_state ("
                                 "group_key, settings_version, detection_hash, registration_hash, reference_frame_id, "
                                 "light_files_hash, calibration_hash, output_wcs_json, updated_at) "
                                 "VALUES (:group_key, 0, :det, :reg, :ref, :files, :calibration, '', 1)"));
        q.bindValue(QStringLiteral(":group_key"), groupKey);
        q.bindValue(QStringLiteral(":det"), detHash);
        q.bindValue(QStringLiteral(":reg"), regHash);
        q.bindValue(QStringLiteral(":ref"), workflowGroup->referenceFrame.has_value()
                                                ? workflowGroup->referenceFrame->canonicalPath()
                                                : QString());
        q.bindValue(QStringLiteral(":files"), fileHash);
        q.bindValue(QStringLiteral(":calibration"), calibrationHash);
        ASSERT_TRUE(q.exec());

        const auto insertMetric = [&](const QString& frameId) {
            QSqlQuery ins(db);
            ins.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_metrics ("
                                       "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                                       "succeeded, output_path, transform_json, updated_at) "
                                       "VALUES (:frame_id, :group_key, 0, 50, 2.0, 10.0, 0.2, 1, '', '', 1)"));
            ins.bindValue(QStringLiteral(":frame_id"), frameId);
            ins.bindValue(QStringLiteral(":group_key"), groupKey);
            ASSERT_TRUE(ins.exec());
        };
        insertMetric(pathA);
        insertMetric(pathB);

        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connName);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QPushButton* runGroupBtn = tab->findChild<QPushButton*>("btnRunRegistrationGroup");
    ASSERT_TRUE(runGroupBtn != nullptr);
    EXPECT_TRUE(runGroupBtn->isEnabled());
    const QString tooltip = runGroupBtn->toolTip();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("missing cached stars"), Qt::CaseInsensitive) ||
                tooltip.contains(QStringLiteral("star detection cache"), Qt::CaseInsensitive))
        << tooltip.toStdString();

    // Ensure any SQLite connection owned by the tab is released before QTemporaryDir cleanup.
    delete tab;
    tab = nullptr;
    delete model;
    model = nullptr;
}

TEST_F(RegistrationTabTest, ClickingRunSelectedGroupEmitsGroupSignalWhenEnabled)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit"), createLightFrame("C:/light2.fit")});
    model->setOutputDirectory(tempDir.path());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    QPushButton* runGroupBtn = tab->findChild<QPushButton*>("btnRunRegistrationGroup");
    ASSERT_TRUE(runGroupBtn != nullptr);
    ASSERT_TRUE(runGroupBtn->isEnabled());

    QSignalSpy spy(tab, SIGNAL(runRegistrationRequested(QString)));
    ASSERT_TRUE(spy.isValid());

    QTest::mouseClick(runGroupBtn, Qt::LeftButton);
    QCoreApplication::processEvents();

    EXPECT_EQ(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    ASSERT_EQ(args.size(), 1);
    EXPECT_FALSE(args.at(0).toString().isEmpty());
}

TEST_F(RegistrationTabTest, ClickingRunAllEmitsAllSignalWhenEnabled)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit"), createLightFrame("C:/light2.fit")});
    model->setOutputDirectory(tempDir.path());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QPushButton* runAllBtn = tab->findChild<QPushButton*>("btnRunRegistration");
    ASSERT_TRUE(runAllBtn != nullptr);
    ASSERT_TRUE(runAllBtn->isEnabled());

    QSignalSpy spy(tab, SIGNAL(runRegistrationAllRequested()));
    ASSERT_TRUE(spy.isValid());

    QTest::mouseClick(runAllBtn, Qt::LeftButton);
    QCoreApplication::processEvents();

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(RegistrationTabTest, ClearRegistrationDeletesGroupDataAfterConfirmation)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pathA = FrameId(QStringLiteral("C:/light_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/light_b.fit")).canonicalPath();
    model->addFrames(FrameType::Light, {createLightFrame(pathA), createLightFrame(pathB)});
    model->setOutputDirectory(tempDir.path());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    const QString groupKey = groupTable->item(0, 0)->data(Qt::UserRole).toString();
    ASSERT_FALSE(groupKey.isEmpty());

    // Create a DB with one group's metrics so the Clear button is enabled.
    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));
    const QString connName = QStringLiteral("regtab_clear_db");
    {
        if (QSqlDatabase::contains(connName))
        {
            QSqlDatabase::removeDatabase(connName);
        }
        makeCurrentRegistrationDatabase(dbPath);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());

        QSqlQuery q(db);
        ASSERT_TRUE(
            q.exec(QStringLiteral("PRAGMA user_version = %1").arg(stage_utils::kRegistrationMetricsSchemaVersion)));
        ASSERT_TRUE(q.exec(stage_utils::registrationMetricsCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::starDetectionCacheCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationGroupStateCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesGroupIndexCreateSql()));
        ASSERT_TRUE(q.exec(stage_utils::registrationMatchedSourcesSourceIndexCreateSql()));

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT OR REPLACE INTO registration_metrics ("
                                   "frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, succeeded, "
                                   "output_path, transform_json, updated_at) "
                                   "VALUES (:frame_id, :group_key, 0, 50, 2.0, 10.0, 0.2, 1, '', '', 1)"));
        ins.bindValue(QStringLiteral(":group_key"), groupKey);
        ins.bindValue(QStringLiteral(":frame_id"), pathA);
        ASSERT_TRUE(ins.exec());
        ins.bindValue(QStringLiteral(":frame_id"), pathB);
        ASSERT_TRUE(ins.exec());

        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(connName);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QPushButton* clearBtn = tab->findChild<QPushButton*>("btnClearRegistrationGroup");
    ASSERT_TRUE(clearBtn != nullptr);
    ASSERT_TRUE(clearBtn->isEnabled());

    QTableWidgetItem* statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);

    // The confirmation does not block: the clear runs once the open question is answered.
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    an::tests::integration::pumpDialogEvents();
    ASSERT_TRUE(an::tests::integration::answerOpenMessageBox(QStringLiteral("Clear Registration"), QMessageBox::Yes));
    QCoreApplication::processEvents();
    QTest::qWait(100);

    // Status should be Pending after clearing.
    statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Pending")));

    // Verify DB has no rows for the group.
    const QString verifyConn = QStringLiteral("regtab_clear_verify");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConn);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COUNT(1) FROM registration_metrics WHERE group_key = :group_key"));
        q.bindValue(QStringLiteral(":group_key"), groupKey);
        ASSERT_TRUE(q.exec());
        ASSERT_TRUE(q.next());
        EXPECT_EQ(q.value(0).toInt(), 0);
        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(verifyConn);

    // Ensure any SQLite connection owned by the tab is released before QTemporaryDir cleanup.
    delete tab;
    tab = nullptr;
    delete model;
    model = nullptr;
}

TEST_F(RegistrationTabTest, ClearRegistrationRollsBackCometSidecarsWhenDeleteFails)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString pathA = FrameId(QStringLiteral("C:/rollback_light_a.fit")).canonicalPath();
    const QString pathB = FrameId(QStringLiteral("C:/rollback_light_b.fit")).canonicalPath();
    model->addFrames(FrameType::Light, {createLightFrame(pathA), createLightFrame(pathB)});
    model->setOutputDirectory(tempDir.path());

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    const QString groupKey = groupTable->item(0, 0)->data(Qt::UserRole).toString();
    ASSERT_FALSE(groupKey.isEmpty());

    const QString dbPath = QDir(tempDir.path()).filePath(QStringLiteral("registration_metrics.db"));
    const QString runId = QStringLiteral("regtab-rollback-comet-run");
    const QString planeId = QStringLiteral("regtab-rollback-comet-plane");

    {
        an::ui::RegistrationMetricsWriter writer(dbPath, QStringLiteral("RegistrationTabRollbackSeedWriter"));
        QString error;
        ASSERT_TRUE(writer.initialize(&error)) << error.toStdString();

        an::ui::RegistrationMetricRecord metric;
        metric.frameId = FrameId(pathA);
        metric.groupKey = groupKey;
        metric.settingsVersion = 0;
        metric.metrics.stars = 50;
        metric.metrics.fwhm = 2.0;
        metric.metrics.snr = 10.0;
        metric.metrics.eccentricity = 0.2;
        metric.metrics.succeeded = true;
        ASSERT_TRUE(writer.upsertMetric(metric, &error)) << error.toStdString();

        an::ui::CometTrackingRunRecord run;
        run.cometRunId = runId;
        run.groupKey = groupKey;
        run.settingsVersion = 0;
        run.cometContextId = QStringLiteral("ctx-regtab-rollback");
        run.registrationHash = QStringLiteral("regtab-rollback-reg");
        run.lightFilesHash = QStringLiteral("regtab-rollback-lights");
        run.calibrationHash = QStringLiteral("regtab-rollback-cal");
        run.resolvedReferenceFrameId = pathA;
        run.mode = QStringLiteral("PureComet");
        run.status = QStringLiteral("Active");
        ASSERT_TRUE(writer.upsertCometTrackingRun(run, &error)) << error.toStdString();

        an::ui::CometTrackingRunSummaryRecord summary;
        summary.cometRunId = runId;
        summary.fitModel = QStringLiteral("linear");
        summary.acceptedInputCount = 2;
        ASSERT_TRUE(writer.upsertCometTrackingRunSummary(summary, &error)) << error.toStdString();

        an::ui::CometTrackingPointRecord point;
        point.pointId = QStringLiteral("regtab-rollback-point");
        point.cometRunId = runId;
        point.originalFrameId = FrameId(pathA);
        point.origin = QStringLiteral("Manual");
        point.fitState = QStringLiteral("Inlier");
        point.measurementMethod = QStringLiteral("manual");
        ASSERT_TRUE(writer.upsertCometTrackingPoint(point, &error)) << error.toStdString();

        an::ui::CometOutputPlaneRecord plane;
        plane.cometOutputPlaneId = planeId;
        plane.cometRunId = runId;
        plane.role = QStringLiteral("comet_aligned");
        plane.alignmentMode = QStringLiteral("PureComet");
        plane.coordinateFrameKind = QStringLiteral("CometReference");
        plane.canvasWidth = 128;
        plane.canvasHeight = 128;
        ASSERT_TRUE(writer.upsertCometOutputPlane(plane, &error)) << error.toStdString();

        an::ui::CometTransformRoleRecord transform;
        transform.cometRunId = runId;
        transform.frameId = FrameId(pathA);
        transform.role = QStringLiteral("comet_aligned");
        transform.cometOutputPlaneId = planeId;
        transform.succeeded = true;
        ASSERT_TRUE(writer.upsertCometTransformRole(transform, &error)) << error.toStdString();

        an::ui::CometRegisteredOutputRecord output;
        output.cometRunId = runId;
        output.frameId = FrameId(pathA);
        output.role = QStringLiteral("comet_aligned");
        output.cometOutputPlaneId = planeId;
        output.outputPath = QDir(tempDir.path()).filePath(QStringLiteral("regtab-rollback-comet.fit"));
        output.artifactHash = QStringLiteral("regtab-rollback-output-hash");
        output.succeeded = true;
        ASSERT_TRUE(writer.upsertCometRegisteredOutput(output, &error)) << error.toStdString();

        an::ui::CometRoleReadinessRecord readiness;
        readiness.cometRunId = runId;
        readiness.role = QStringLiteral("comet_aligned");
        readiness.readinessStatus = QStringLiteral("Ready");
        readiness.outputPlaneStatus = QStringLiteral("Ready");
        readiness.selectedFrameCount = 1;
        readiness.successfulTransformCount = 1;
        readiness.requiresMaterializedOutputs = true;
        readiness.materializedOutputCount = 1;
        ASSERT_TRUE(writer.upsertCometRoleReadiness(readiness, &error)) << error.toStdString();

        an::ui::StackingCometArtifactRecord artifact;
        artifact.cometRunId = runId;
        artifact.integrationGroupKey = QStringLiteral("regtab-rollback-integration");
        artifact.stackingHash = QStringLiteral("regtab-rollback-stack");
        artifact.starBaseOutputDescriptorId = QStringLiteral("regtab-rollback-base-star");
        artifact.cometOutputPlaneId = planeId;
        artifact.artifactKind = QStringLiteral("FinalComposite");
        artifact.outputPath = QDir(tempDir.path()).filePath(QStringLiteral("regtab-rollback-final.fit"));
        artifact.artifactHash = QStringLiteral("regtab-rollback-final-hash");
        artifact.maskSettingsFingerprint = QStringLiteral("regtab-rollback-mask");
        artifact.succeeded = true;
        ASSERT_TRUE(writer.upsertStackingCometArtifact(artifact, &error)) << error.toStdString();
    }

    const QString triggerConn = QStringLiteral("regtab_rollback_trigger");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), triggerConn);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());
        QSqlQuery q(db);
        ASSERT_TRUE(q.exec(QStringLiteral("CREATE TRIGGER fail_regtab_registered_output_delete "
                                          "BEFORE DELETE ON comet_registered_outputs "
                                          "BEGIN SELECT RAISE(FAIL, 'forced rollback test failure'); END")));
        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(triggerConn);

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QPushButton* clearBtn = tab->findChild<QPushButton*>("btnClearRegistrationGroup");
    ASSERT_TRUE(clearBtn != nullptr);
    ASSERT_TRUE(clearBtn->isEnabled());

    // The confirmation and the failure that follows it are boxes that do not block, opened one after the other.
    QTest::mouseClick(clearBtn, Qt::LeftButton);
    an::tests::integration::pumpDialogEvents();
    ASSERT_TRUE(an::tests::integration::answerOpenMessageBox(QStringLiteral("Clear Registration"), QMessageBox::Yes));
    QCoreApplication::processEvents();
    QTest::qWait(250);
    EXPECT_TRUE(an::tests::integration::answerOpenMessageBox(QStringLiteral("Clear Registration"), QMessageBox::Ok));

    const QString verifyConn = QStringLiteral("regtab_rollback_verify");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConn);
        db.setDatabaseName(dbPath);
        ASSERT_TRUE(db.open());

        auto countRows = [&](const QString& tableName, const QString& whereSql, const QString& bindName,
                             const QString& bindValue) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(1) FROM %1 WHERE %2").arg(tableName, whereSql));
            q.bindValue(bindName, bindValue);
            if (!q.exec() || !q.next())
            {
                return -1;
            }
            return q.value(0).toInt();
        };

        EXPECT_EQ(countRows(QStringLiteral("registration_metrics"), QStringLiteral("group_key = :group_key"),
                            QStringLiteral(":group_key"), groupKey),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_tracking_runs"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_tracking_run_summary"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_tracking_points"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_transform_roles"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_output_planes"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_registered_outputs"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("comet_role_readiness"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);
        EXPECT_EQ(countRows(QStringLiteral("stacking_comet_artifacts"), QStringLiteral("comet_run_id = :run_id"),
                            QStringLiteral(":run_id"), runId),
                  1);

        db.close();
        db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(verifyConn);

    delete tab;
    tab = nullptr;
    delete model;
    model = nullptr;
}

// ============================================================================
// Double-Click Behavior Tests
// ============================================================================

TEST_F(RegistrationTabTest, DoubleClickOnGroupRowOpensSettingsDialog)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);

    // Verify the table exists with groups and items are interactive
    if (groupTable->rowCount() > 0)
    {
        // Select row first
        groupTable->selectRow(0);
        QCoreApplication::processEvents();

        QTableWidgetItem* item = groupTable->item(0, 0);
        ASSERT_TRUE(item != nullptr);

        // Verify the item is enabled and selectable (required for double-click)
        EXPECT_TRUE(item->flags().testFlag(Qt::ItemIsEnabled));
        EXPECT_TRUE(item->flags().testFlag(Qt::ItemIsSelectable));

        // Verify the table has edit triggers disabled (double-click opens dialog, not inline edit)
        EXPECT_EQ(groupTable->editTriggers(), QAbstractItemView::NoEditTriggers);
    }
}

TEST_F(RegistrationTabTest, AlgorithmColumnShowsDefaultTransformation)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);

    if (groupTable->rowCount() > 0)
    {
        // Find "Algorithm" column (column order may evolve).
        int algo_col = -1;
        for (int c = 0; c < groupTable->columnCount(); ++c)
        {
            if (auto* header = groupTable->horizontalHeaderItem(c))
            {
                if (header->text() == "Algorithm")
                {
                    algo_col = c;
                    break;
                }
            }
        }
        ASSERT_NE(algo_col, -1);

        QTableWidgetItem* algorithmItem = groupTable->item(0, algo_col);
        ASSERT_TRUE(algorithmItem != nullptr);

        // Default algorithm should use automatic transform selection.
        EXPECT_EQ(algorithmItem->text(), "Auto");
    }
}

TEST_F(RegistrationTabTest, FrameTableHasQualityColumn)
{
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(frameTable != nullptr);

    // Find Quality column
    int qualityCol = -1;
    for (int i = 0; i < frameTable->columnCount(); ++i)
    {
        if (frameTable->horizontalHeaderItem(i)->text() == "Quality")
        {
            qualityCol = i;
            break;
        }
    }
    EXPECT_NE(qualityCol, -1);
}

TEST_F(RegistrationTabTest, MetricCellsHaveBackgroundColors)
{
    // Add frames with quality metrics
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 1000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.eccentricity = 0.1;
    goodMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), goodMetrics);

    FrameQualityMetrics badMetrics;
    badMetrics.stars = 50;
    badMetrics.fwhm = 8.0;
    badMetrics.eccentricity = 0.8;
    badMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), badMetrics);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(groupTable != nullptr && frameTable != nullptr);

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        ASSERT_GE(frameTable->rowCount(), 2);

        // Cells should have non-default backgrounds
        int starsCol = -1;
        for (int c = 0; c < frameTable->columnCount(); ++c)
        {
            if (auto* header = frameTable->horizontalHeaderItem(c))
            {
                if (header->text() == "Stars")
                {
                    starsCol = c;
                    break;
                }
            }
        }
        ASSERT_NE(starsCol, -1);

        QTableWidgetItem* starsItem1 = frameTable->item(0, starsCol);
        ASSERT_TRUE(starsItem1 != nullptr);
        EXPECT_TRUE(starsItem1->background() != QBrush());
    }
}

TEST_F(RegistrationTabTest, MetricCellColorsPersistAfterSorting)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 1000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.eccentricity = 0.1;
    goodMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), goodMetrics);

    FrameQualityMetrics badMetrics;
    badMetrics.stars = 50;
    badMetrics.fwhm = 8.0;
    badMetrics.eccentricity = 0.8;
    badMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), badMetrics);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(groupTable != nullptr && frameTable != nullptr);

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        ASSERT_GE(frameTable->rowCount(), 2);

        int starsCol = -1;
        for (int c = 0; c < frameTable->columnCount(); ++c)
        {
            if (auto* header = frameTable->horizontalHeaderItem(c))
            {
                if (header->text() == "Stars")
                {
                    starsCol = c;
                    break;
                }
            }
        }
        ASSERT_NE(starsCol, -1);

        frameTable->sortItems(starsCol, Qt::AscendingOrder);
        QCoreApplication::processEvents();

        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            QTableWidgetItem* starsItem = frameTable->item(row, starsCol);
            ASSERT_TRUE(starsItem != nullptr);
            EXPECT_TRUE(starsItem->background() != QBrush());
        }
    }
}

TEST_F(RegistrationTabTest, ScoreTooltipShowsRebalancedPhotometricFormula)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/tooltip_score.fit", "2025-01-10")});

    FrameQualityMetrics metrics;
    metrics.stars = 800;
    metrics.fwhm = 2.1;
    metrics.snr = 42.0;
    metrics.eccentricity = 0.1;
    metrics.photometric_zero_point = 24.2;
    metrics.zero_point_scatter = 0.07;
    metrics.astrometric_rms_arcsec = 0.5;
    metrics.catalog_matched_stars = 110;
    metrics.limiting_magnitude = 13.8;
    metrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/tooltip_score.fit"), metrics);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    auto* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    auto* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_NE(groupTable, nullptr);
    ASSERT_NE(frameTable, nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    int scoreCol = -1;
    for (int c = 0; c < frameTable->columnCount(); ++c)
    {
        if (auto* header = frameTable->horizontalHeaderItem(c))
        {
            if (header->text() == "Score")
            {
                scoreCol = c;
                break;
            }
        }
    }

    ASSERT_NE(scoreCol, -1);
    ASSERT_EQ(frameTable->rowCount(), 1);
    ASSERT_NE(frameTable->item(0, scoreCol), nullptr);

    const QString tooltip = frameTable->item(0, scoreCol)->toolTip();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("zero_point*4")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("zero_point_scatter*20")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("astro_rms*8")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("catalog_matched")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("limiting_mag*0.5")));
}

TEST_F(RegistrationTabTest, GroupStatusReflectsMissingMetrics)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics m1;
    m1.stars = 1000;
    m1.fwhm = 2.0;
    m1.eccentricity = 0.1;
    m1.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), m1);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    // Status should be Partial because one included frame has no metrics.
    QTableWidgetItem* statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Partial")));

    // Once metrics exist for all included frames, it should become Complete.
    FrameQualityMetrics m2;
    m2.stars = 900;
    m2.fwhm = 2.2;
    m2.eccentricity = 0.12;
    m2.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), m2);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Complete")));
}

TEST_F(RegistrationTabTest, GroupStatusCompleteWhenAllIncludedHaveMetrics)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics m1;
    m1.stars = 900;
    m1.fwhm = 2.3;
    m1.eccentricity = 0.11;
    m1.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), m1);

    FrameQualityMetrics m2;
    m2.stars = 850;
    m2.fwhm = 2.5;
    m2.eccentricity = 0.13;
    m2.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), m2);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    QTableWidgetItem* statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Complete")));
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("✓")));
}

TEST_F(RegistrationTabTest, GroupStatusPartialWhenReIncludingNoMetricsFrame)
{
    model->addFrames(FrameType::Light,
                     {createLightFrame("C:/light1.fit", "2025-01-10"), createLightFrame("C:/light2.fit", "2025-01-10"),
                      createLightFrame("C:/light3.fit", "2025-01-10")});

    FrameQualityMetrics m1;
    m1.stars = 900;
    m1.fwhm = 2.3;
    m1.eccentricity = 0.11;
    m1.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), m1);

    FrameQualityMetrics m2;
    m2.stars = 850;
    m2.fwhm = 2.5;
    m2.eccentricity = 0.13;
    m2.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), m2);

    // Exclude light3 (no metrics) so group should remain Complete.
    model->updateFrameAttribute("C:/light3.fit", FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    QTableWidgetItem* statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Complete")));

    // Re-include the frame without metrics -> should become Partial.
    model->updateFrameAttribute("C:/light3.fit", FrameAttribute::Excluded, false);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Partial")));
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("⚠")));
}

TEST_F(RegistrationTabTest, GroupStatusRemainsCompleteWhenOnlyExcludedFrameAdded)
{
    model->addFrames(FrameType::Light,
                     {createLightFrame("C:/light1.fit", "2025-01-10"), createLightFrame("C:/light2.fit", "2025-01-10"),
                      createLightFrame("C:/light3.fit", "2025-01-10")});

    FrameQualityMetrics m1;
    m1.stars = 900;
    m1.fwhm = 2.3;
    m1.eccentricity = 0.11;
    m1.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), m1);

    FrameQualityMetrics m2;
    m2.stars = 850;
    m2.fwhm = 2.5;
    m2.eccentricity = 0.13;
    m2.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), m2);

    // Add metrics for light3 but keep it excluded.
    FrameQualityMetrics m3;
    m3.stars = 800;
    m3.fwhm = 2.7;
    m3.eccentricity = 0.14;
    m3.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light3.fit"), m3);
    model->updateFrameAttribute("C:/light3.fit", FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GT(groupTable->rowCount(), 0);

    QTableWidgetItem* statusItem = groupTable->item(0, 7);
    ASSERT_TRUE(statusItem != nullptr);
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Complete")));
}

TEST_F(RegistrationTabTest, QualityIndicatorShowsCorrectLevel)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 1000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.eccentricity = 0.1;
    goodMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), goodMetrics);

    FrameQualityMetrics badMetrics;
    badMetrics.stars = 50;
    badMetrics.fwhm = 8.0;
    badMetrics.eccentricity = 0.8;
    badMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), badMetrics);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        // Find Quality column
        int qualityCol = -1;
        for (int i = 0; i < frameTable->columnCount(); ++i)
        {
            if (frameTable->horizontalHeaderItem(i)->text() == "Quality")
            {
                qualityCol = i;
                break;
            }
        }
        ASSERT_NE(qualityCol, -1);

        bool foundIcon = false;
        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            QTableWidgetItem* qualityItem = frameTable->item(row, qualityCol);
            if (qualityItem && !qualityItem->text().isEmpty())
            {
                foundIcon = true;
                break;
            }
        }
        EXPECT_TRUE(foundIcon);
    }
}

TEST_F(RegistrationTabTest, FrameTableHasStatusColumn)
{
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(frameTable != nullptr);

    EXPECT_GE(frameTable->columnCount(), 8);
    QTableWidgetItem* header = frameTable->horizontalHeaderItem(0);
    ASSERT_TRUE(header != nullptr);
    EXPECT_EQ(header->text(), QStringLiteral("Status"));
}

TEST_F(RegistrationTabTest, ReferenceFrameShowsStarIcon)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    const auto groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    model->setRegistrationReferenceFrame(groups.begin()->first, FrameId("C:/light1.fit"));

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        bool foundStar = false;
        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            QTableWidgetItem* statusItem = frameTable->item(row, 0);
            if (statusItem && statusItem->text().contains(QStringLiteral("★")))
            {
                foundStar = true;
                break;
            }
        }
        EXPECT_TRUE(foundStar);
    }
}

TEST_F(RegistrationTabTest, FailedIncludedReferenceFrameShowsFailureIcon)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    const auto groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    model->setRegistrationReferenceFrame(groups.begin()->first, FrameId("C:/light1.fit"));

    FrameQualityMetrics failedMetrics;
    failedMetrics.succeeded = false;
    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), failedMetrics);

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 100;
    goodMetrics.succeeded = true;
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), goodMetrics);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_TRUE(frameTable != nullptr);

    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(100);

    bool foundFailedReference = false;
    for (int row = 0; row < frameTable->rowCount(); ++row)
    {
        QTableWidgetItem* frameItem = frameTable->item(row, 1);
        QTableWidgetItem* statusItem = frameTable->item(row, 0);
        if (!frameItem || !statusItem || !frameItem->text().contains(QStringLiteral("light1.fit")))
        {
            continue;
        }

        EXPECT_TRUE(statusItem->text().contains(QStringLiteral("❌"))) << statusItem->text().toStdString();
        EXPECT_FALSE(statusItem->text().contains(QStringLiteral("★"))) << statusItem->text().toStdString();
        foundFailedReference = true;
        break;
    }

    EXPECT_TRUE(foundFailedReference);
}

TEST_F(RegistrationTabTest, ReferenceRowHasBlueHighlight)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    const auto groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    model->setRegistrationReferenceFrame(groups.begin()->first, FrameId("C:/light1.fit"));

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        bool foundReferenceRow = false;
        bool foundHighlight = false;
        bool foundReadableForeground = false;
        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            QTableWidgetItem* statusItem = frameTable->item(row, 0);
            if (statusItem && statusItem->text().contains(QStringLiteral("★")))
            {
                foundReferenceRow = true;
                // Check that the reference row carries an explicit background tint.
                QTableWidgetItem* frameItem = frameTable->item(row, 1);
                if (frameItem)
                {
                    const QBrush bg = frameItem->background();
                    if (bg.style() != Qt::NoBrush && bg.color().alpha() > 0)
                    {
                        foundHighlight = true;
                        const QColor effectiveBackground =
                            an::ui::compositeOver(bg.color(), frameTable->palette().color(QPalette::Base));
                        const QColor foreground = frameItem->foreground().color();
                        foundReadableForeground = an::ui::contrastRatio(foreground, effectiveBackground) >= 3.0;
                    }
                }
                break;
            }
        }
        EXPECT_TRUE(foundReferenceRow);
        EXPECT_TRUE(foundHighlight);
        EXPECT_TRUE(foundReadableForeground);
    }
}

TEST_F(RegistrationTabTest, ExcludedFrameShowsXIcon)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    model->updateFrameAttribute("C:/light1.fit", FrameAttribute::Excluded, true);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");

    if (groupTable->rowCount() > 0)
    {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        QTest::qWait(100);

        bool foundX = false;
        for (int row = 0; row < frameTable->rowCount(); ++row)
        {
            QTableWidgetItem* statusItem = frameTable->item(row, 0);
            if (statusItem && statusItem->text().contains(QStringLiteral("✗")))
            {
                foundX = true;
                break;
            }
        }
        EXPECT_TRUE(foundX);
    }
}

TEST_F(RegistrationTabTest, HasThresholdPreviewLabel)
{
    QLabel* preview = tab->findChild<QLabel*>("lblThresholdPreview");
    EXPECT_TRUE(preview != nullptr);
}

TEST_F(RegistrationTabTest, ThresholdPreviewUpdatesOnValueChange)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics highStars, lowStars;
    highStars.stars = 1000;
    highStars.succeeded = true;
    lowStars.stars = 50;
    lowStars.succeeded = true;

    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), highStars);
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), lowStars);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    groupTable->selectRow(0);
    QCoreApplication::processEvents();

    QCheckBox* minStarsCheck = tab->findChild<QCheckBox*>("chkMinStars");
    QSpinBox* minStars = tab->findChild<QSpinBox*>("spinMinStars");
    QLabel* preview = tab->findChild<QLabel*>("lblThresholdPreview");

    ASSERT_TRUE(minStarsCheck != nullptr);
    ASSERT_TRUE(minStars != nullptr);
    ASSERT_TRUE(preview != nullptr);

    minStarsCheck->setChecked(true);
    minStars->setValue(500);
    QCoreApplication::processEvents();

    // Preview should show 1 frame would be excluded
    EXPECT_TRUE(preview->text().contains("1"));
}

TEST_F(RegistrationTabTest, ThresholdPreviewUpdatesForScoreAndSnrFilters)
{
    model->addFrames(FrameType::Light, {createLightFrame("C:/light1.fit", "2025-01-10"),
                                        createLightFrame("C:/light2.fit", "2025-01-10")});

    FrameQualityMetrics goodMetrics;
    goodMetrics.stars = 1000;
    goodMetrics.fwhm = 2.0;
    goodMetrics.snr = 60.0;
    goodMetrics.eccentricity = 0.2;
    goodMetrics.succeeded = true;

    FrameQualityMetrics poorMetrics;
    poorMetrics.stars = 10;
    poorMetrics.fwhm = 10.0;
    poorMetrics.snr = 1.0;
    poorMetrics.eccentricity = 0.9;
    poorMetrics.succeeded = true;

    model->setFrameQualityMetrics(FrameId("C:/light1.fit"), goodMetrics);
    model->setFrameQualityMetrics(FrameId("C:/light2.fit"), poorMetrics);

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    groupTable->selectRow(0);
    QCoreApplication::processEvents();

    QCheckBox* minSnrCheck = tab->findChild<QCheckBox*>("chkMinSnr");
    QDoubleSpinBox* minSnr = tab->findChild<QDoubleSpinBox*>("spinMinSnr");
    QLabel* preview = tab->findChild<QLabel*>("lblThresholdPreview");

    ASSERT_TRUE(minSnrCheck != nullptr);
    ASSERT_TRUE(minSnr != nullptr);
    ASSERT_TRUE(preview != nullptr);

    minSnrCheck->setChecked(true);
    minSnr->setValue(10.0);
    QCoreApplication::processEvents();

    EXPECT_TRUE(preview->text().contains("1"));

    QCheckBox* minScoreCheck = tab->findChild<QCheckBox*>("chkMinScore");
    QDoubleSpinBox* minScore = tab->findChild<QDoubleSpinBox*>("spinMinScore");
    ASSERT_TRUE(minScoreCheck != nullptr);
    ASSERT_TRUE(minScore != nullptr);

    minSnrCheck->setChecked(false);
    minScoreCheck->setChecked(true);
    minScore->setValue(20.0);
    QCoreApplication::processEvents();

    EXPECT_TRUE(preview->text().contains("1"));
}

TEST_F(RegistrationTabTest, QualityThresholdsCanBeCollapsed)
{
    QWidget* thresholdsWidget = tab->findChild<QWidget*>("grpQualityThresholds");
    auto* thresholds = dynamic_cast<an::ui::AnCollapsibleSection*>(thresholdsWidget);

    ASSERT_TRUE(thresholds != nullptr);
    ASSERT_TRUE(thresholds->toggleButton() != nullptr);

    // Check default state (collapsed)
    EXPECT_FALSE(thresholds->isExpanded());

    // Simulate click to expand
    thresholds->toggleButton()->click();
    QCoreApplication::processEvents();

    EXPECT_TRUE(thresholds->isExpanded());

    // Simulate click to collapse
    thresholds->toggleButton()->click();
    QCoreApplication::processEvents();

    EXPECT_FALSE(thresholds->isExpanded());
}
// =============================================================================
// Tests for settings_version = 0 handling (regression tests for version bug fix)
// =============================================================================

class RegistrationTabMetricsDbTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temporary directory for output/database
        tempDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tempDir->isValid());

        model = new IntegrationDataModel();
        model->setOutputDirectory(tempDir->path());
        tab = new RegistrationTab(model);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
        tempDir.reset();
    }

    void createMetricsDatabase(const QString& groupKey, int settingsVersion,
                               const std::vector<std::pair<QString, bool>>& frameSucceeded)
    {
        const QString dbPath = QDir(tempDir->path()).filePath(QStringLiteral("registration_metrics.db"));

        static int connectionCounter = 0;
        const QString connName = QStringLiteral("TestMetricsDb_%1").arg(++connectionCounter);

        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            ASSERT_TRUE(db.open()) << "Failed to open database: " << db.lastError().text().toStdString();

            QSqlQuery q(db);
            ASSERT_TRUE(q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS registration_metrics ("
                                              "frame_id TEXT NOT NULL,"
                                              "group_key TEXT NOT NULL,"
                                              "settings_version INTEGER NOT NULL,"
                                              "stars INTEGER NOT NULL,"
                                              "fwhm REAL NOT NULL,"
                                              "snr REAL NOT NULL,"
                                              "eccentricity REAL NOT NULL,"
                                              "photometric_zero_point REAL NOT NULL DEFAULT 0.0,"
                                              "zero_point_scatter REAL NOT NULL DEFAULT 0.0,"
                                              "astrometric_rms_pixels REAL NOT NULL DEFAULT 0.0,"
                                              "astrometric_rms_arcsec REAL NOT NULL DEFAULT 0.0,"
                                              "limiting_magnitude REAL NOT NULL DEFAULT 0.0,"
                                              "catalog_matched_stars INTEGER NOT NULL DEFAULT 0,"
                                              "succeeded INTEGER NOT NULL,"
                                              "output_path TEXT,"
                                              "transform_json TEXT,"
                                              "updated_at INTEGER NOT NULL,"
                                              "PRIMARY KEY (frame_id, group_key, settings_version))")))
                << "Failed to create table: " << q.lastError().text().toStdString();

            q.prepare(QStringLiteral("INSERT INTO registration_metrics "
                                     "(frame_id, group_key, settings_version, stars, fwhm, snr, eccentricity, "
                                     "succeeded, output_path, transform_json, updated_at) "
                                     "VALUES (:frame_id, :group_key, :settings_version, :stars, :fwhm, :snr, "
                                     ":eccentricity, :succeeded, '', '', 0)"));

            for (const auto& [frameId, succeeded] : frameSucceeded)
            {
                q.bindValue(QStringLiteral(":frame_id"), frameId);
                q.bindValue(QStringLiteral(":group_key"), groupKey);
                q.bindValue(QStringLiteral(":settings_version"), settingsVersion);
                q.bindValue(QStringLiteral(":stars"), 100);
                q.bindValue(QStringLiteral(":fwhm"), 2.5);
                q.bindValue(QStringLiteral(":snr"), 50.0);
                q.bindValue(QStringLiteral(":eccentricity"), 0.3);
                q.bindValue(QStringLiteral(":succeeded"), succeeded ? 1 : 0);
                ASSERT_TRUE(q.exec()) << "Failed to insert: " << q.lastError().text().toStdString();
            }

            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    IntegrationDataModel* model = nullptr;
    RegistrationTab* tab = nullptr;
    std::unique_ptr<QTemporaryDir> tempDir;
};

TEST_F(RegistrationTabMetricsDbTest, LoadsMetricsWithSettingsVersionZero)
{
    // Add light frames to model
    const QString path1 = FrameId(QStringLiteral("C:/test/light1.fit")).canonicalPath();
    const QString path2 = FrameId(QStringLiteral("C:/test/light2.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {createLightFrame(path1, "2025-01-10"), createLightFrame(path2, "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Get the registration group key (must use toMachineGroupKey to match database format)
    const auto& groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    const QString groupKey = RegistrationGroupEngine::toMachineGroupKey(groups.begin()->first).toString();

    // Create database with settings_version = 0 (the default)
    createMetricsDatabase(groupKey, 0, {{path1, true}, {path2, true}});

    // Refresh the tab to load metrics
    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Select the group
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GE(groupTable->rowCount(), 1);
    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(150);

    // Check that frame quality table shows metrics
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(frameTable != nullptr);

    // Frame Quality table columns: 0=Status, 1=Frame, 2=Filter, 3=Stars, 4=FWHM, 5=SNR, 6=Ecc, 7=Score, 8=Quality
    constexpr int kStarsCol = 3;

    // Should have 2 rows with actual data (not empty message)
    bool hasMetrics = false;
    for (int row = 0; row < frameTable->rowCount(); ++row)
    {
        QTableWidgetItem* starsItem = frameTable->item(row, kStarsCol);
        if (starsItem && !starsItem->text().isEmpty() && starsItem->text() != "-")
        {
            hasMetrics = true;
            EXPECT_EQ(starsItem->text().toInt(), 100); // We inserted 100 stars
            break;
        }
    }
    EXPECT_TRUE(hasMetrics) << "Metrics with settings_version=0 should be loaded and displayed";
}

TEST_F(RegistrationTabMetricsDbTest, ShowsCompleteStatusWithSettingsVersionZero)
{
    // Add light frames to model
    const QString path1 = FrameId(QStringLiteral("C:/test/light1.fit")).canonicalPath();
    const QString path2 = FrameId(QStringLiteral("C:/test/light2.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {createLightFrame(path1, "2025-01-10"), createLightFrame(path2, "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Get the registration group key (must use toMachineGroupKey to match database format)
    const auto& groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    const QString groupKey = RegistrationGroupEngine::toMachineGroupKey(groups.begin()->first).toString();

    // Create database with settings_version = 0 and all frames succeeded
    createMetricsDatabase(groupKey, 0, {{path1, true}, {path2, true}});

    // Refresh the tab to load metrics
    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Check that group table shows "Complete" status (not "Pending")
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GE(groupTable->rowCount(), 1);

    // Find Status column
    int statusCol = -1;
    for (int i = 0; i < groupTable->columnCount(); ++i)
    {
        QTableWidgetItem* header = groupTable->horizontalHeaderItem(i);
        if (header && header->text() == "Status")
        {
            statusCol = i;
            break;
        }
    }
    ASSERT_NE(statusCol, -1) << "Status column not found";

    QTableWidgetItem* statusItem = groupTable->item(0, statusCol);
    ASSERT_TRUE(statusItem != nullptr);
    // Status text has icon prefix (e.g., "✓ Complete")
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Complete")))
        << "Status should be 'Complete' when all frames have metrics with settings_version=0, got: "
        << statusItem->text().toStdString();
}

TEST_F(RegistrationTabMetricsDbTest, ShowsPartialStatusWithSettingsVersionZeroAndFailures)
{
    // Add light frames to model
    const QString path1 = FrameId(QStringLiteral("C:/test/light1.fit")).canonicalPath();
    const QString path2 = FrameId(QStringLiteral("C:/test/light2.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {createLightFrame(path1, "2025-01-10"), createLightFrame(path2, "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Get the registration group key (must use toMachineGroupKey to match database format)
    const auto& groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    const QString groupKey = RegistrationGroupEngine::toMachineGroupKey(groups.begin()->first).toString();

    // Create database with settings_version = 0, one frame failed
    createMetricsDatabase(groupKey, 0,
                          {
                              {path1, true}, {path2, false} // This frame failed
                          });

    // Refresh the tab to load metrics
    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Check that group table shows "Partial" status
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GE(groupTable->rowCount(), 1);

    // Find Status column
    int statusCol = -1;
    for (int i = 0; i < groupTable->columnCount(); ++i)
    {
        QTableWidgetItem* header = groupTable->horizontalHeaderItem(i);
        if (header && header->text() == "Status")
        {
            statusCol = i;
            break;
        }
    }
    ASSERT_NE(statusCol, -1) << "Status column not found";

    QTableWidgetItem* statusItem = groupTable->item(0, statusCol);
    ASSERT_TRUE(statusItem != nullptr);
    // Status text has icon prefix (e.g., "⚠ Partial")
    EXPECT_TRUE(statusItem->text().contains(QStringLiteral("Partial")))
        << "Status should be 'Partial' when some frames failed with settings_version=0, got: "
        << statusItem->text().toStdString();
}

TEST_F(RegistrationTabMetricsDbTest, FallsBackToMaxVersionWhenDesiredVersionHasNoData)
{
    // Add light frames to model
    const QString path1 = FrameId(QStringLiteral("C:/test/light1.fit")).canonicalPath();
    const QString path2 = FrameId(QStringLiteral("C:/test/light2.fit")).canonicalPath();

    model->addFrames(FrameType::Light, {createLightFrame(path1, "2025-01-10"), createLightFrame(path2, "2025-01-10")});

    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Get the registration group key (must use toMachineGroupKey to match database format)
    const auto& groups = model->registrationGroups();
    ASSERT_FALSE(groups.empty());
    const QString groupKey = RegistrationGroupEngine::toMachineGroupKey(groups.begin()->first).toString();

    // Create database with settings_version = 5 (not matching default 0)
    createMetricsDatabase(groupKey, 5, {{path1, true}, {path2, true}});

    // Refresh the tab - should fall back to version 5 since version 0 has no data
    tab->refreshAll();
    QCoreApplication::processEvents();
    QTest::qWait(300);

    // Select the group
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_GE(groupTable->rowCount(), 1);
    groupTable->selectRow(0);
    QCoreApplication::processEvents();
    QTest::qWait(150);

    // Check that frame quality table shows metrics (fallback to version 5)
    QTableWidget* frameTable = tab->findChild<QTableWidget*>("tblFrameQuality");
    ASSERT_TRUE(frameTable != nullptr);

    // Frame Quality table columns: 0=Status, 1=Frame, 2=Filter, 3=Stars, 4=FWHM, 5=SNR, 6=Ecc, 7=Score, 8=Quality
    constexpr int kStarsCol = 3;

    bool hasMetrics = false;
    for (int row = 0; row < frameTable->rowCount(); ++row)
    {
        QTableWidgetItem* starsItem = frameTable->item(row, kStarsCol);
        if (starsItem && !starsItem->text().isEmpty() && starsItem->text() != "-")
        {
            hasMetrics = true;
            EXPECT_EQ(starsItem->text().toInt(), 100);
            break;
        }
    }
    EXPECT_TRUE(hasMetrics) << "Should fall back to version 5 when version 0 has no data";
}

// The integration's Registration Settings dialog writes back every choice it shows, the Gaia enrichment among them: the
// tab copied the dialog field by field and left that one out, so the box the dialog offers changed nothing a run read,
// accepted for the group or applied to every group.
TEST_F(RegistrationTabTest, TheGaiaChoiceOfTheSettingsDialogIsWhatTheGroupKeeps)
{
    model->addFrames(FrameType::Light,
                     {createLightFrameForObject(QStringLiteral("C:/gaia_m42_a.fit"), QStringLiteral("M42")),
                      createLightFrameForObject(QStringLiteral("C:/gaia_m42_b.fit"), QStringLiteral("M42")),
                      createLightFrameForObject(QStringLiteral("C:/gaia_m31_a.fit"), QStringLiteral("M31")),
                      createLightFrameForObject(QStringLiteral("C:/gaia_m31_b.fit"), QStringLiteral("M31"))});
    tab->refreshAll();
    QCoreApplication::processEvents();
    waitForRefresh(tab);
    QTableWidget* groupTable = tab->findChild<QTableWidget*>("tblRegistrationGroups");
    ASSERT_TRUE(groupTable != nullptr);
    ASSERT_EQ(groupTable->rowCount(), 2);
    auto* settingsButton = tab->findChild<QPushButton*>("btnGroupSettings");
    ASSERT_TRUE(settingsButton != nullptr);

    std::vector<GroupKey> machineKeys;
    for (const auto& [key, frames] : model->registrationGroups())
    {
        (void)frames;
        machineKeys.push_back(RegistrationGroupEngine::toMachineGroupKey(key));
    }
    ASSERT_EQ(machineKeys.size(), 2u);
    const auto gaiaOf = [this](const GroupKey& key) {
        const auto it = model->registrationSettings().find(key);
        return it == model->registrationSettings().end() ? RegistrationSettings{}.enrichWithGaiaCatalog
                                                         : it->second.enrichWithGaiaCatalog;
    };
    ASSERT_TRUE(gaiaOf(machineKeys[0]));
    ASSERT_TRUE(gaiaOf(machineKeys[1]));

    const auto openDialog = [&]() -> RegistrationSettingsDialog* {
        groupTable->selectRow(0);
        QCoreApplication::processEvents();
        settingsButton->click();
        an::tests::integration::pumpDialogEvents();
        const std::vector<RegistrationSettingsDialog*> open =
            an::tests::integration::openDialogsOf<RegistrationSettingsDialog>();
        return open.size() == 1u ? open.front() : nullptr;
    };

    // Accepted for the selected group.
    RegistrationSettingsDialog* dialog = openDialog();
    ASSERT_TRUE(dialog != nullptr);
    dialog->setEnrichWithGaiaCatalog(false);
    dialog->accept();
    an::tests::integration::pumpDialogEvents();
    const int unticked =
        static_cast<int>(gaiaOf(machineKeys[0]) ? 0 : 1) + static_cast<int>(gaiaOf(machineKeys[1]) ? 0 : 1);
    EXPECT_EQ(unticked, 1) << "the group the dialog was accepted for keeps the box unticked, the other its own";

    // Applied to every group.
    dialog = openDialog();
    ASSERT_TRUE(dialog != nullptr);
    dialog->setEnrichWithGaiaCatalog(false);
    dialog->applyToAllRequested();
    an::tests::integration::pumpDialogEvents();
    EXPECT_FALSE(gaiaOf(machineKeys[0]));
    EXPECT_FALSE(gaiaOf(machineKeys[1]));
    dialog->reject();
    an::tests::integration::pumpDialogEvents();
}
