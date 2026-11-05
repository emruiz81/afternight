// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QTimeZone>

#include <optional>
#include <vector>

#include "processes/ImageIntegration/model/GroupingEngine.h"

namespace
{
FrameInfo makeFrame(FrameType type, const QString& filename, const QSize& size, double exposure,
                    std::optional<double> temperature, std::optional<int> iso_gain, std::optional<int> offset,
                    const QString& filter, const QString& object_name, const QString& session,
                    const QString& custom_tag, const QString& readout_mode = QStringLiteral("Default"))
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    FrameMetadata metadata(type, size, exposure, temperature, iso_gain, offset, filter, object_name,
                           QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), readout_mode);

    return FrameInfo(FrameId(path), path, metadata, session, custom_tag);
}
} // namespace

TEST(GroupingEngine, GroupsBiasesByDimensions)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byIsoGain = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias1.fit"), QSize(100, 100), 0.0, std::nullopt, 100,
                               10, QString(), QString(), QString(), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias2.fit"), QSize(200, 200), 0.0, std::nullopt, 100,
                               10, QString(), QString(), QString(), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Bias, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, GroupsBiasesByGainOffset)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byIsoGain = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias1.fit"), QSize(100, 100), 0.0, std::nullopt, 100,
                               10, QString(), QString(), QString(), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias2.fit"), QSize(100, 100), 0.0, std::nullopt, 200,
                               20, QString(), QString(), QString(), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Bias, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, GroupsDarksByExposure)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byExposure = true;
    options.byIsoGain = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"), QSize(100, 100), 10.0, 5.0, 100, 10,
                               QString(), QString(), QString(), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Dark, QStringLiteral("dark2.fit"), QSize(100, 100), 20.0, 5.0, 100, 10,
                               QString(), QString(), QString(), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Dark, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, GroupsDarksByTemperature)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byExposure = true;
    options.byIsoGain = true;
    options.byTemperature = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Dark, QStringLiteral("dark1.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                               QString(), QString(), QString(), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Dark, QStringLiteral("dark2.fit"), QSize(100, 100), 10.0, 10.0, 100, 10,
                               QString(), QString(), QString(), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Dark, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, GroupsFlatsByFilter)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byFilter = true;
    options.bySession = false;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Flat, QStringLiteral("flat1.fit"), QSize(100, 100), 2.0, std::nullopt, 100,
                               10, QStringLiteral("L"), QString(), QStringLiteral("2025-01-01"), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Flat, QStringLiteral("flat2.fit"), QSize(100, 100), 2.0, std::nullopt, 100,
                               10, QStringLiteral("Ha"), QString(), QStringLiteral("2025-01-01"), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Flat, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, GroupsFlatsBySession)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byFilter = false;
    options.bySession = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Flat, QStringLiteral("flat1.fit"), QSize(100, 100), 2.0, std::nullopt, 100,
                               10, QStringLiteral(""), QString(), QStringLiteral("2025-01-01"), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Flat, QStringLiteral("flat2.fit"), QSize(100, 100), 2.0, std::nullopt, 100,
                               10, QStringLiteral(""), QString(), QStringLiteral("2025-01-02"), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Flat, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, GroupsLightsByAllCriteria)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byIsoGain = true;
    options.byExposure = true;
    options.byFilter = true;
    options.byObject = true;
    options.bySession = true;
    options.byCustomTag = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(200, 200), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                               QStringLiteral("A")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(200, 200), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                               QStringLiteral("B")));

    auto groups = engine.groupFrames(frames, FrameType::Light, options);
    EXPECT_EQ(groups.size(), 2u);
}

TEST(GroupingEngine, DeterministicOutputOrder)
{
    GroupingEngine engine;
    GroupingOptions options;

    FrameInfo first = makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                                QString(), QString(), QStringLiteral(""), QStringLiteral(""));
    FrameInfo second = makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                                 QString(), QString(), QStringLiteral(""), QStringLiteral(""));

    first.manualGroupKey = GroupKey{QStringLiteral("B")};
    second.manualGroupKey = GroupKey{QStringLiteral("A")};

    std::vector<FrameInfo> frames{first, second};
    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].key.toString(), QStringLiteral("A"));
    EXPECT_EQ(groups[1].key.toString(), QStringLiteral("B"));
}

TEST(GroupingEngine, StableOnRepeatedCalls)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byIsoGain = true;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias1.fit"), QSize(100, 100), 0.0, std::nullopt, 100,
                               10, QString(), QString(), QString(), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Bias, QStringLiteral("bias2.fit"), QSize(100, 100), 0.0, std::nullopt, 200,
                               10, QString(), QString(), QString(), QStringLiteral("")));

    auto first = engine.groupFrames(frames, FrameType::Bias, options);
    auto second = engine.groupFrames(frames, FrameType::Bias, options);

    ASSERT_EQ(first.size(), second.size());
    for (size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(first[i].key, second[i].key);
        EXPECT_EQ(first[i].frames.size(), second[i].frames.size());
    }
}

TEST(GroupingEngine, HandlesEmptyInput)
{
    GroupingEngine engine;
    GroupingOptions options;

    std::vector<FrameInfo> frames;
    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    EXPECT_TRUE(groups.empty());
}

TEST(GroupingEngine, HandlesSingleFrame)
{
    GroupingEngine engine;
    GroupingOptions options;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                               QString(), QString(), QStringLiteral(""), QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].frames.size(), 1u);
}

TEST(GroupingEngine, HandlesThousandsOfFrames)
{
    GroupingEngine engine;
    GroupingOptions options;

    std::vector<FrameInfo> frames;
    frames.reserve(1000);

    for (int i = 0; i < 1000; ++i)
    {
        frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light%1.fit").arg(i), QSize(100, 100), 10.0, 0.0,
                                   100, 10, QString(), QString(), QStringLiteral(""), QStringLiteral("")));
    }

    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].frames.size(), 1000u);
}

TEST(GroupingEngine, IgnoresExcludedFrames)
{
    GroupingEngine engine;
    GroupingOptions options;

    FrameInfo included = makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                                   QString(), QString(), QStringLiteral(""), QStringLiteral(""));
    FrameInfo excluded = makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                                   QString(), QString(), QStringLiteral(""), QStringLiteral(""));
    excluded.excluded = true;

    std::vector<FrameInfo> frames{included, excluded};
    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].frames.size(), 1u);
}

TEST(GroupingEngine, RespectsManualGroupAssignments)
{
    GroupingEngine engine;
    GroupingOptions options;

    FrameInfo first =
        makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"), QStringLiteral(""));
    FrameInfo second =
        makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(200, 200), 20.0, 0.0, 200, 20,
                  QStringLiteral("Ha"), QStringLiteral("M31"), QStringLiteral("2025-01-02"), QStringLiteral(""));

    first.manualGroupKey = GroupKey{QStringLiteral("ManualGroup")};
    second.manualGroupKey = GroupKey{QStringLiteral("ManualGroup")};

    std::vector<FrameInfo> frames{first, second};
    auto groups = engine.groupFrames(frames, FrameType::Light, options);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].key.toString(), QStringLiteral("ManualGroup"));
    EXPECT_EQ(groups[0].frames.size(), 2u);
}

TEST(GroupingEngine, RespectsGroupingOptions)
{
    GroupingEngine engine;
    GroupingOptions options;
    options.byFilter = false;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                               QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(100, 100), 10.0, 0.0, 100, 10,
                               QStringLiteral("Ha"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                               QStringLiteral("")));

    auto groups = engine.groupFrames(frames, FrameType::Light, options);
    EXPECT_EQ(groups.size(), 1u);
}
// ============================================================================
// RegistrationGroupEngine Tests
// ============================================================================

#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"

TEST(RegistrationGroupEngine, GroupsByResolutionAndObjectOnly)
{
    RegistrationGroupEngine engine;

    // Frames with same resolution+object but different filters/sessions/exposures
    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light1.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-01-01"),
                               QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light2.fit"), QSize(1000, 1000), 120.0, 0.0, 200, 20,
                               QStringLiteral("Ha"), QStringLiteral("M42"), QStringLiteral("2025-01-02"),
                               QStringLiteral("Tag")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("light3.fit"), QSize(1000, 1000), 300.0, 0.0, 400, 0,
                               QStringLiteral("OIII"), QStringLiteral("M42"), QStringLiteral("2025-01-03"),
                               QStringLiteral("")));

    auto groups = engine.computeGroups(frames);

    // All frames should be in one group (same Resolution + Object)
    EXPECT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.begin()->second.size(), 3u);
}

TEST(RegistrationGroupEngine, EmptyObjectUsesUnknownSentinel)
{
    RegistrationGroupEngine engine;

    FrameInfo frame = makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                                QStringLiteral("L"), QString(), // empty object
                                QStringLiteral("2025-01-01"), QStringLiteral(""));

    auto groups = engine.computeGroups({frame});

    ASSERT_EQ(groups.size(), 1u);
    const RegistrationGroupKey& key = groups.begin()->first;
    EXPECT_EQ(key.objectName, QStringLiteral("(Unknown Object)"));
}

TEST(RegistrationGroupEngine, NullObjectTreatedSameAsEmpty)
{
    RegistrationGroupEngine engine;

    FrameInfo with_empty = makeFrame(FrameType::Light, QStringLiteral("a.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                                     QStringLiteral("L"), QString(), // empty
                                     QStringLiteral(""), QStringLiteral(""));
    FrameInfo with_whitespace = makeFrame(FrameType::Light, QStringLiteral("b.fit"), QSize(1000, 1000), 60.0, 0.0, 100,
                                          10, QStringLiteral("L"), QStringLiteral("   "), // whitespace only
                                          QStringLiteral(""), QStringLiteral(""));

    auto groups = engine.computeGroups({with_empty, with_whitespace});

    // Both should be in the same "(Unknown Object)" group
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.begin()->second.size(), 2u);
    EXPECT_EQ(groups.begin()->first.objectName, QStringLiteral("(Unknown Object)"));
}

TEST(RegistrationGroupEngine, DifferentResolutionsCreateSeparateGroups)
{
    RegistrationGroupEngine engine;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("hi_res.fit"), QSize(6000, 4000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("lo_res.fit"), QSize(3000, 2000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral("")));

    auto groups = engine.computeGroups(frames);

    // Same object but different resolutions = separate groups
    EXPECT_EQ(groups.size(), 2u);
}

TEST(RegistrationGroupEngine, DifferentObjectsCreateSeparateGroups)
{
    RegistrationGroupEngine engine;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("m42.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("m31.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M31"), QStringLiteral(""), QStringLiteral("")));

    auto groups = engine.computeGroups(frames);

    // Same resolution but different objects = separate groups
    EXPECT_EQ(groups.size(), 2u);
}

TEST(RegistrationGroupEngine, IgnoresExcludedFrames)
{
    RegistrationGroupEngine engine;

    FrameInfo included =
        makeFrame(FrameType::Light, QStringLiteral("included.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral(""));
    FrameInfo excluded =
        makeFrame(FrameType::Light, QStringLiteral("excluded.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral(""));
    excluded.excluded = true;

    auto groups = engine.computeGroups({included, excluded});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.begin()->second.size(), 1u);
}

TEST(RegistrationGroupEngine, DeterministicOrdering)
{
    RegistrationGroupEngine engine;

    std::vector<FrameInfo> frames;
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("z_frame.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("a_frame.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral("")));
    frames.push_back(makeFrame(FrameType::Light, QStringLiteral("m_frame.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                               QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral("")));

    auto groups1 = engine.computeGroups(frames);
    auto groups2 = engine.computeGroups(frames);

    // Frame IDs within group should be sorted identically
    ASSERT_EQ(groups1.size(), groups2.size());
    auto it1 = groups1.begin();
    auto it2 = groups2.begin();
    ASSERT_EQ(it1->second.size(), it2->second.size());
    for (size_t i = 0; i < it1->second.size(); ++i)
    {
        EXPECT_EQ(it1->second[i], it2->second[i]);
    }
}

TEST(RegistrationGroupEngine, MachineKeyIsStable)
{
    RegistrationGroupKey key1;
    key1.resolution = QStringLiteral("1000x1000");
    key1.objectName = QStringLiteral("M42");

    RegistrationGroupKey key2;
    key2.resolution = QStringLiteral("1000x1000");
    key2.objectName = QStringLiteral("M42");

    GroupKey machine1 = RegistrationGroupEngine::toMachineGroupKey(key1);
    GroupKey machine2 = RegistrationGroupEngine::toMachineGroupKey(key2);

    EXPECT_EQ(machine1, machine2);
    EXPECT_EQ(machine1.toString(), machine2.toString());
}

TEST(RegistrationGroupEngine, MachineKeyEscapesPipes)
{
    RegistrationGroupKey key;
    key.resolution = QStringLiteral("1000|1000"); // pipe in resolution
    key.objectName = QStringLiteral("NGC|7000"); // pipe in object

    GroupKey machine = RegistrationGroupEngine::toMachineGroupKey(key);
    const QString str = machine.toString();

    // Should not contain consecutive pipes that would cause parsing issues
    EXPECT_FALSE(str.contains(QStringLiteral("||")));
    // Pipes should be escaped to underscores
    EXPECT_TRUE(str.contains(QStringLiteral("1000_1000")));
    EXPECT_TRUE(str.contains(QStringLiteral("NGC_7000")));
}

TEST(RegistrationGroupEngine, HandlesEmptyInput)
{
    RegistrationGroupEngine engine;
    std::vector<FrameInfo> empty;

    auto groups = engine.computeGroups(empty);

    EXPECT_TRUE(groups.empty());
}

TEST(RegistrationGroupEngine, HandlesSingleFrame)
{
    RegistrationGroupEngine engine;

    FrameInfo frame = makeFrame(FrameType::Light, QStringLiteral("single.fit"), QSize(1000, 1000), 60.0, 0.0, 100, 10,
                                QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral(""), QStringLiteral(""));

    auto groups = engine.computeGroups({frame});

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.begin()->second.size(), 1u);
}

TEST(RegistrationGroupEngine, HandlesManyGroups)
{
    RegistrationGroupEngine engine;

    std::vector<FrameInfo> frames;
    for (int i = 0; i < 100; ++i)
    {
        QString object = QStringLiteral("Object_%1").arg(i);
        frames.push_back(makeFrame(FrameType::Light, QStringLiteral("frame_%1.fit").arg(i), QSize(1000, 1000), 60.0,
                                   0.0, 100, 10, QStringLiteral("L"), object, QStringLiteral(""), QStringLiteral("")));
    }

    auto groups = engine.computeGroups(frames);

    EXPECT_EQ(groups.size(), 100u);
}