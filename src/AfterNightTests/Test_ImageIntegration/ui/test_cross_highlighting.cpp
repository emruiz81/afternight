// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QCoreApplication>
#include <QTableWidget>
#include <QGroupBox>

#include "processes/ImageIntegration/tabs/CalibrationTab.h"
#include "processes/ImageIntegration/tabs/CalibrationGroupTable.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
FrameInfo createFrame(FrameType type, const QString& path, const QString& session = QString())
{
    FrameMetadata meta(type, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, QStringLiteral("L"), QStringLiteral("M42"),
                       session.isEmpty()
                           ? QDateTime::currentDateTimeUtc()
                           : QDateTime(QDate::fromString(session, "yyyy-MM-dd"), QTime(0, 0), QTimeZone::UTC),
                       QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}

} // namespace

class CrossHighlightingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        tab = new CalibrationTab(model);

        // Setup a scenario with Light -> Flat -> Bias chain
        // Same resolution, filter, session -> Should match exactly
        model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light1.fit", "2025-01-10")});
        model->addFrames(FrameType::Flat, {createFrame(FrameType::Flat, "C:/flat1.fit", "2025-01-10")});
        model->addFrames(FrameType::Bias, {createFrame(FrameType::Bias, "C:/bias1.fit", "2025-01-10")});

        tab->refreshAll();
        QCoreApplication::processEvents();
        QTest::qWait(250); // Wait for debounce

        tblLight = tab->findChild<QTableWidget*>("tblLightCalibration");
        tblFlat = tab->findChild<QTableWidget*>("tblFlatCalibration");
        tblBias = tab->findChild<QTableWidget*>("tblBiasCalibration");
        QTRY_VERIFY(tblLight != nullptr && tblLight->rowCount() > 0 && tblLight->item(0, 0) != nullptr);
        QTRY_VERIFY(tblFlat != nullptr && tblFlat->rowCount() > 0 && tblFlat->item(0, 0) != nullptr);
        QTRY_VERIFY(tblBias != nullptr && tblBias->rowCount() > 0 && tblBias->item(0, 0) != nullptr);
    }

    void TearDown() override
    {
        delete tab;
        delete model;
    }

    bool isRowHighlighted(QTableWidget* table, int row)
    {
        if (!table || row >= table->rowCount())
            return false;
        auto* calibrationTable = qobject_cast<CalibrationGroupTable*>(table);
        if (!calibrationTable)
            return false;

        QTableWidgetItem* item = table->item(row, 0);
        if (!item)
            return false;
        const QString keyStr = item->data(Qt::UserRole).toString();
        return !keyStr.isEmpty() && calibrationTable->isKeyHighlighted(keyStr);
    }

    IntegrationDataModel* model = nullptr;
    CalibrationTab* tab = nullptr;
    QTableWidget* tblLight = nullptr;
    QTableWidget* tblFlat = nullptr;
    QTableWidget* tblBias = nullptr;
};

TEST_F(CrossHighlightingTest, SelectLight_HighlightsFlatAndBias)
{
    ASSERT_TRUE(tblLight != nullptr);
    ASSERT_TRUE(tblFlat != nullptr);
    ASSERT_TRUE(tblBias != nullptr);

    ASSERT_GT(tblLight->rowCount(), 0);
    ASSERT_GT(tblFlat->rowCount(), 0);
    ASSERT_GT(tblBias->rowCount(), 0);

    // Initial state: no highlight
    EXPECT_FALSE(isRowHighlighted(tblFlat, 0));
    EXPECT_FALSE(isRowHighlighted(tblBias, 0));

    // Select Light row
    tblLight->selectRow(0);
    QCoreApplication::processEvents(); // Process signals

    // Check highlights
    QTRY_VERIFY(isRowHighlighted(tblFlat, 0));
    QTRY_VERIFY(isRowHighlighted(tblBias, 0));
}

TEST_F(CrossHighlightingTest, SelectFlat_HighlightsLightAndBias)
{
    ASSERT_TRUE(tblLight != nullptr);
    ASSERT_TRUE(tblFlat != nullptr);
    ASSERT_TRUE(tblBias != nullptr);

    // Select Flat row
    tblFlat->selectRow(0);
    QCoreApplication::processEvents();

    // Check highlights
    QTRY_VERIFY(isRowHighlighted(tblLight, 0)); // Light using this flat
    QTRY_VERIFY(isRowHighlighted(tblBias, 0)); // Bias used by this flat (as light-equivalent)
}

TEST_F(CrossHighlightingTest, SelectBias_HighlightsLightAndFlat)
{
    ASSERT_TRUE(tblLight != nullptr);
    ASSERT_TRUE(tblFlat != nullptr);
    ASSERT_TRUE(tblBias != nullptr);

    // Select Bias row
    tblBias->selectRow(0);
    QCoreApplication::processEvents();

    // Check highlights
    QTRY_VERIFY(isRowHighlighted(tblLight, 0)); // Light using this bias
    QTRY_VERIFY(isRowHighlighted(tblFlat, 0)); // Flat using this bias
}

TEST_F(CrossHighlightingTest, Deselect_ClearsHighlights)
{
    ASSERT_TRUE(tblLight != nullptr);
    ASSERT_TRUE(tblFlat != nullptr);

    // Select then deselect
    tblLight->selectRow(0);
    QCoreApplication::processEvents();
    QTRY_VERIFY(isRowHighlighted(tblFlat, 0));

    tblLight->clearSelection();
    QCoreApplication::processEvents();

    QTRY_VERIFY(!isRowHighlighted(tblFlat, 0));
}
