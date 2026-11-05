// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QCoreApplication>

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"

namespace
{
FrameInfo createFrame(FrameType type, const QString& path, const QString& filter)
{
    FrameMetadata meta(type, QSize(1000, 1000), 30.0, 100, 10, std::nullopt, filter, QStringLiteral("M42"),
                       QDateTime::currentDateTimeUtc(), QStringLiteral(""));
    return FrameInfo(FrameId(path), path, meta);
}
} // namespace

class BatchAssignmentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        model = new IntegrationDataModel();

        // Add 3 light groups
        // G1: Filter R
        model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light_r1.fit", "R")});
        // G2: Filter R
        model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light_r2.fit", "R")});
        // G3: Filter G
        model->addFrames(FrameType::Light, {createFrame(FrameType::Light, "C:/light_g1.fit", "G")});

        QCoreApplication::processEvents();
    }

    void TearDown() override
    {
        delete model;
    }

    IntegrationDataModel* model = nullptr;
};

TEST_F(BatchAssignmentTest, AssignToAll_SetsOverrideForAllGroups)
{
    // Emulate "Assign to All" logic from LightCalibrationPanel context menu
    const QString masterPath = "C:/master_bias.fit";
    const CalibrationOverrideType type = CalibrationOverrideType::Bias;
    const auto groups = model->lightGroups();

    model->beginBatchUpdate();
    for (const auto& g : groups)
    {
        if (!g.frames.empty())
        {
            model->setCalibrationOverride(LightGroupKey::fromFrame(g.frames.front()), type, masterPath);
        }
    }
    model->endBatchUpdate();

    // Check results
    const auto& overrides = model->calibrationOverrides();
    // 3 frames, but they form 2 groups (R and G). So we expect 2 overrides.
    EXPECT_EQ(overrides.size(), 2);
    for (const auto& ov : overrides)
    {
        EXPECT_EQ(ov.masterPath, masterPath);
        EXPECT_EQ(ov.type, type);
    }
}

TEST_F(BatchAssignmentTest, AssignToFilter_SetsOverrideForMatchingGroupsOnly)
{
    const QString masterPath = "C:/master_bias_r.fit";
    const CalibrationOverrideType type = CalibrationOverrideType::Bias;
    const QString filter = "R";
    const auto groups = model->lightGroups();

    model->beginBatchUpdate();
    for (const auto& g : groups)
    {
        if (!g.frames.empty())
        {
            auto key = LightGroupKey::fromFrame(g.frames.front());
            if (key.filter == filter)
            {
                model->setCalibrationOverride(key, type, masterPath);
            }
        }
    }
    model->endBatchUpdate();

    // Check results
    const auto& overrides = model->calibrationOverrides();
    // Only 1 R group (containing 2 frames).
    EXPECT_EQ(overrides.size(), 1);

    for (const auto& ov : overrides)
    {
        EXPECT_EQ(ov.masterPath, masterPath);
        EXPECT_EQ(ov.lightKey.filter, filter);
    }
}

TEST_F(BatchAssignmentTest, ClearAll_RemovesAllOverrides)
{
    const auto groups = model->lightGroups();
    ASSERT_GE(groups.size(), 2u);

    // Setup initial overrides
    model->setCalibrationOverride(LightGroupKey::fromFrame(groups[0].frames.front()), CalibrationOverrideType::Bias,
                                  "C:/m1.fit");
    model->setCalibrationOverride(LightGroupKey::fromFrame(groups[1].frames.front()), CalibrationOverrideType::Bias,
                                  "C:/m2.fit");
    EXPECT_EQ(model->calibrationOverrides().size(), 2);

    // Clear all logic
    model->beginBatchUpdate();
    for (const auto& g : groups)
    {
        if (!g.frames.empty())
        {
            model->clearCalibrationOverride(LightGroupKey::fromFrame(g.frames.front()), CalibrationOverrideType::Bias);
        }
    }
    model->endBatchUpdate();

    EXPECT_TRUE(model->calibrationOverrides().empty());
}
