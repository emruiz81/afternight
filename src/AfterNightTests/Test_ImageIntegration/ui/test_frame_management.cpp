// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QCoreApplication>
#include <QIcon>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "theme/AnDesignTokens.h"

namespace
{
FrameInfo createFrame(FrameType type, const QString& path)
{
    FrameMetadata meta(type, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, QStringLiteral("L"), QStringLiteral("M42"),
                       QDateTime::currentDateTimeUtc(), QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

class FrameManagementTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();
        treeModel = new GroupTreeModel(FrameType::Light, model);

        // Add a light frame
        framePath = "C:/light1.fit";
        model->addFrames(FrameType::Light, {createFrame(FrameType::Light, framePath)});

        // Wait for model update
        QCoreApplication::processEvents();
        QTest::qWait(100);

        // Find the frame index
        // Structure: Group -> Frame
        // Assuming 1 group
        QModelIndex root = QModelIndex();
        ASSERT_GE(treeModel->rowCount(root), 1);
        groupIndex = treeModel->index(0, 0, root);
        ASSERT_TRUE(groupIndex.isValid());
        ASSERT_GE(treeModel->rowCount(groupIndex), 1);
        frameIndex = treeModel->index(0, 0, groupIndex);
        ASSERT_TRUE(frameIndex.isValid());
    }

    void TearDown() override
    {
        delete treeModel;
        delete model;
    }

    IntegrationDataModel* model = nullptr;
    GroupTreeModel* treeModel = nullptr;
    QString framePath;
    QModelIndex groupIndex;
    QModelIndex frameIndex;
};

TEST_F(FrameManagementTest, ExcludeFrameViaModel_UpdatesCheckState)
{
    // Initially included (Checked)
    EXPECT_EQ(treeModel->data(frameIndex, Qt::CheckStateRole).toInt(), Qt::Checked);

    // Exclude via model
    model->updateFrameAttribute(framePath, FrameAttribute::Excluded, true);

    // Wait for signal
    QCoreApplication::processEvents();
    QTest::qWait(100);

    // Re-fetch index as model reset might invalidate it
    QModelIndex root = QModelIndex();
    groupIndex = treeModel->index(0, 0, root);
    frameIndex = treeModel->index(0, 0, groupIndex);
    ASSERT_TRUE(frameIndex.isValid());

    // Should be Unchecked
    EXPECT_EQ(treeModel->data(frameIndex, Qt::CheckStateRole).toInt(), Qt::Unchecked);

    // And visual style should indicate exclusion using token-derived gray background
    // with contrast-aware foreground text.
    QColor expectedBackground = an::ui::tokens().colors.textTertiary;
    expectedBackground.setAlpha(35);
    const QColor expectedForeground = an::ui::bestContrastTextColor(
        an::ui::compositeOver(expectedBackground, an::ui::tokens().colors.bgInput),
        an::ui::tokens().colors.textTertiary, QColor(255, 255, 255), QColor(0, 0, 0), 4.5);

    QColor foreground = treeModel->data(frameIndex, Qt::ForegroundRole).value<QColor>();
    QColor background = treeModel->data(frameIndex, Qt::BackgroundRole).value<QColor>();
    EXPECT_EQ(foreground, expectedForeground);
    EXPECT_EQ(background, expectedBackground);
}

TEST_F(FrameManagementTest, ExcludeFrameViaSetData_UpdatesModel)
{
    // Uncheck item
    bool result = treeModel->setData(frameIndex, Qt::Unchecked, Qt::CheckStateRole);
    EXPECT_TRUE(result);

    // Wait for signal
    QCoreApplication::processEvents();
    QTest::qWait(100);

    // Verify model updated
    auto frame = model->lightFrames().findById(FrameId(framePath));
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->excluded);
}

TEST_F(FrameManagementTest, SetReferenceFrame_ShowsIcon)
{
    // Initially no icon
    QVariant iconVar = treeModel->data(frameIndex, Qt::DecorationRole);
    EXPECT_FALSE(iconVar.isValid()); // Or at least not our star icon

    // Set as reference
    auto frame = model->lightFrames().findById(FrameId(framePath));
    ASSERT_TRUE(frame.has_value());
    RegistrationGroupKey regKey = RegistrationGroupKey::fromFrame(*frame);
    model->setRegistrationReferenceFrame(regKey, frame->id);

    // Wait for signal
    QCoreApplication::processEvents();
    QTest::qWait(100);

    // Re-fetch index
    QModelIndex root = QModelIndex();
    groupIndex = treeModel->index(0, 0, root);
    frameIndex = treeModel->index(0, 0, groupIndex);

    // Should have icon
    iconVar = treeModel->data(frameIndex, Qt::DecorationRole);
    EXPECT_TRUE(iconVar.isValid());
    // Can't easily check icon content, but validity implies something was returned
    // And our logic returns QIcon only if reference.
}
