// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QTimeZone>

#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/FrameType.h"
#include "processes/ImageIntegration/model/GroupingOptions.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename, const QSize& size, double exposure, const QString& filter,
                    const QString& object_name, const QString& session = QString(), int iso = 100,
                    const QString& tag = QString())
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure,
                           std::nullopt, // temperature
                           iso, // iso_gain
                           std::nullopt, // offset
                           filter, object_name, QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC),
                           QStringLiteral("Default")); // readout_mode

    return FrameInfo(FrameId(path), path, metadata, session, tag);
}
} // namespace

TEST(IntegrationGroupEngine, GroupsByResolutionObjectFilterAlways)
{
    IntegrationGroupEngine engine;
    GroupingOptions options; // Default options (only basic grouping)

    std::vector<FrameInfo> frames;
    // Group 1: Res1, Obj1, Filter1
    frames.push_back(makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42"));
    frames.push_back(
        makeFrame(FrameType::Light, "f2.fit", QSize(1000, 1000), 120, "L", "M42")); // Diff exposure, same group

    // Group 2: Res2, Obj1, Filter1
    frames.push_back(makeFrame(FrameType::Light, "f3.fit", QSize(2000, 2000), 60, "L", "M42"));

    // Group 3: Res1, Obj2, Filter1
    frames.push_back(makeFrame(FrameType::Light, "f4.fit", QSize(1000, 1000), 60, "L", "M31"));

    // Group 4: Res1, Obj1, Filter2
    frames.push_back(makeFrame(FrameType::Light, "f5.fit", QSize(1000, 1000), 60, "R", "M42"));

    auto groups = engine.computeGroups(frames, options);
    EXPECT_EQ(groups.size(), 4u);
}

TEST(IntegrationGroupEngine, GroupingOptions_Session)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;
    options.bySession = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42", "Session1"));
    frames.push_back(makeFrame(FrameType::Light, "f2.fit", QSize(1000, 1000), 60, "L", "M42", "Session1"));
    frames.push_back(makeFrame(FrameType::Light, "f3.fit", QSize(1000, 1000), 60, "L", "M42", "Session2"));

    auto groups = engine.computeGroups(frames, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(IntegrationGroupEngine, GroupingOptions_ISO)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;
    options.byIsoGain = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42", "", 100));
    frames.push_back(makeFrame(FrameType::Light, "f2.fit", QSize(1000, 1000), 60, "L", "M42", "", 100));
    frames.push_back(makeFrame(FrameType::Light, "f3.fit", QSize(1000, 1000), 60, "L", "M42", "", 200));

    auto groups = engine.computeGroups(frames, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(IntegrationGroupEngine, GroupingOptions_Exposure)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;
    options.byExposure = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42"));
    frames.push_back(makeFrame(FrameType::Light, "f2.fit", QSize(1000, 1000), 60, "L", "M42"));
    frames.push_back(makeFrame(FrameType::Light, "f3.fit", QSize(1000, 1000), 120, "L", "M42"));

    auto groups = engine.computeGroups(frames, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(IntegrationGroupEngine, GroupingOptions_CustomTag)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;
    options.byCustomTag = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42", "", 100, "TagA"));
    frames.push_back(makeFrame(FrameType::Light, "f2.fit", QSize(1000, 1000), 60, "L", "M42", "", 100, "TagA"));
    frames.push_back(makeFrame(FrameType::Light, "f3.fit", QSize(1000, 1000), 60, "L", "M42", "", 100, "TagB"));

    auto groups = engine.computeGroups(frames, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(IntegrationGroupEngine, IgnoresExcludedFrames)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;

    FrameInfo included = makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42");
    FrameInfo excluded = makeFrame(FrameType::Light, "f2.fit", QSize(1000, 1000), 60, "L", "M42");
    excluded.excluded = true;

    auto groups = engine.computeGroups({included, excluded}, options);
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.begin()->second.size(), 1u);
}

TEST(IntegrationGroupEngine, HandlesEmptyObjectAsUnknown)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;

    FrameInfo frame = makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", ""); // Empty object

    auto groups = engine.computeGroups({frame}, options);
    ASSERT_EQ(groups.size(), 1u);
    // Note: IntegrationGroupKey wraps GroupKey, so we might need to check the string rep or access logic.
    // For now, assume we can check the key's string representation contains "(Unknown Object)"
    // or specifically check properties if exposed.

    // Let's assume IntegrationGroupKey exposes toString()
    EXPECT_TRUE(groups.begin()->first.toString().contains("(Unknown Object)"));
}

TEST(IntegrationGroupEngine, UsesManualGroupKeyWhenPresent)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;

    // Two frames with different auto-grouping properties but same manual key
    FrameInfo frame1 = makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42");
    frame1.manualGroupKey = GroupKey{QStringLiteral("CustomGroup")};

    FrameInfo frame2 = makeFrame(FrameType::Light, "f2.fit", QSize(2000, 2000), 120, "R", "M31");
    frame2.manualGroupKey = GroupKey{QStringLiteral("CustomGroup")};

    // Frame without manual key should go to its own group
    FrameInfo frame3 = makeFrame(FrameType::Light, "f3.fit", QSize(1000, 1000), 60, "L", "M42");

    auto groups = engine.computeGroups({frame1, frame2, frame3}, options);

    // Should have 2 groups: one for the manual key, one for auto-grouped frame3
    EXPECT_EQ(groups.size(), 2u);

    // Find the manual group
    bool foundManualGroup = false;
    for (const auto& [key, frames] : groups)
    {
        if (key.toString() == QStringLiteral("CustomGroup"))
        {
            EXPECT_EQ(frames.size(), 2u);
            foundManualGroup = true;
        }
    }
    EXPECT_TRUE(foundManualGroup);
}

TEST(IntegrationGroupEngine, ManualGroupKeyOverridesAutoGrouping)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;
    options.bySession = true;
    options.byIsoGain = true;

    // Frame with manual key should ignore auto-grouping completely
    FrameInfo frame = makeFrame(FrameType::Light, "f1.fit", QSize(1000, 1000), 60, "L", "M42", "Session1", 800);
    frame.manualGroupKey = GroupKey{QStringLiteral("MyCustomStack")};

    auto groups = engine.computeGroups({frame}, options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.begin()->first.toString(), QStringLiteral("MyCustomStack"));
}

TEST(IntegrationGroupEngine, DeterministicFrameOrdering)
{
    IntegrationGroupEngine engine;
    GroupingOptions options;

    // Add frames in random order
    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, "z_frame.fit", QSize(1000, 1000), 60, "L", "M42"));
    frames.push_back(makeFrame(FrameType::Light, "a_frame.fit", QSize(1000, 1000), 60, "L", "M42"));
    frames.push_back(makeFrame(FrameType::Light, "m_frame.fit", QSize(1000, 1000), 60, "L", "M42"));

    auto groups = engine.computeGroups(frames, options);

    ASSERT_EQ(groups.size(), 1u);
    const auto& frameList = groups.begin()->second;
    ASSERT_EQ(frameList.size(), 3u);

    // Frames should be sorted by FrameId (which uses canonical path)
    EXPECT_LT(frameList[0], frameList[1]);
    EXPECT_LT(frameList[1], frameList[2]);
}
