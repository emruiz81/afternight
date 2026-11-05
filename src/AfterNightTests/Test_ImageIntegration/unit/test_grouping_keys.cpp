// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>

#include <optional>
#include <unordered_set>

#include "processes/ImageIntegration/model/GroupingKeys.h"

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

TEST(BiasGroupKey, ComputesFromMetadata)
{
    FrameInfo frame =
        makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(100, 100), 0.0, std::nullopt, 200, 15, QString(),
                  QString(), QString(), QStringLiteral("TagA"), QStringLiteral("HighGain"));

    BiasGroupKey key = BiasGroupKey::fromFrame(frame);

    EXPECT_EQ(key.resolution, QStringLiteral("100x100"));
    ASSERT_TRUE(key.isoGain.has_value());
    EXPECT_EQ(key.isoGain.value(), 200);
    ASSERT_TRUE(key.offset.has_value());
    EXPECT_EQ(key.offset.value(), 15);
    EXPECT_EQ(key.readoutMode, QStringLiteral("HighGain"));
    EXPECT_EQ(key.customTag, QStringLiteral("TagA"));
}

TEST(DarkGroupKey, IncludesExposure)
{
    FrameInfo frame = makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(200, 200), 120.5, 5.0, 100, 10,
                                QString(), QString(), QString(), QString());

    DarkGroupKey key = DarkGroupKey::fromFrame(frame);
    EXPECT_DOUBLE_EQ(key.exposureSeconds, 120.5);
}

TEST(FlatGroupKey, IncludesFilter)
{
    FrameInfo frame = makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(300, 300), 2.0, std::nullopt, 150, 5,
                                QStringLiteral("Ha"), QString(), QStringLiteral("2025-01-01"), QString());

    FlatGroupKey key = FlatGroupKey::fromFrame(frame);
    EXPECT_EQ(key.filter, QStringLiteral("Ha"));
    EXPECT_EQ(key.session, QStringLiteral("2025-01-01"));
}

TEST(LightGroupKey, IncludesAllFields)
{
    FrameInfo frame =
        makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(400, 300), 60.0, 0.0, 120, 8,
                  QStringLiteral("L"), QStringLiteral("M42"), QStringLiteral("2025-02-02"), QStringLiteral("Custom"));

    LightGroupKey key = LightGroupKey::fromFrame(frame);
    EXPECT_EQ(key.resolution, QStringLiteral("400x300"));
    ASSERT_TRUE(key.isoGain.has_value());
    EXPECT_EQ(key.isoGain.value(), 120);
    ASSERT_TRUE(key.offset.has_value());
    EXPECT_EQ(key.offset.value(), 8);
    EXPECT_DOUBLE_EQ(key.exposureSeconds, 60.0);
    EXPECT_EQ(key.filter, QStringLiteral("L"));
    EXPECT_EQ(key.objectName, QStringLiteral("M42"));
    EXPECT_EQ(key.session, QStringLiteral("2025-02-02"));
    EXPECT_EQ(key.customTag, QStringLiteral("Custom"));
}

TEST(GroupKey, EqualityWorks)
{
    GroupKey left{QStringLiteral("A|B")};
    GroupKey right{QStringLiteral("A|B")};
    GroupKey other{QStringLiteral("C|D")};

    EXPECT_EQ(left, right);
    EXPECT_NE(left, other);
}

TEST(GroupKey, HashingWorks)
{
    GroupKey left{QStringLiteral("A|B")};
    GroupKey right{QStringLiteral("A|B")};
    GroupKey other{QStringLiteral("C|D")};

    std::unordered_set<GroupKey> keys;
    keys.insert(left);
    keys.insert(right);
    keys.insert(other);

    EXPECT_EQ(keys.size(), 2u);
}

TEST(GroupKey, SerializesToString)
{
    GroupKey key{QStringLiteral("X|Y|Z")};
    EXPECT_EQ(key.toString(), QStringLiteral("X|Y|Z"));
}

TEST(GroupKey, ParsesFromString)
{
    auto parsed = GroupKey::fromString(QStringLiteral("X|Y|Z"));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->toString(), QStringLiteral("X|Y|Z"));
}

TEST(GroupKey, SerializesToJsonObject)
{
    GroupKey key{QStringLiteral("X|Y|Z")};
    QJsonValue json = key.toJson();

    ASSERT_TRUE(json.isObject());
    const QJsonObject obj = json.toObject();
    EXPECT_EQ(obj.value(QStringLiteral("v")).toInt(), 1);
    EXPECT_EQ(obj.value(QStringLiteral("value")).toString(), QStringLiteral("X|Y|Z"));
}

TEST(GroupKey, ParsesFromJsonStringOrObject)
{
    auto from_string = GroupKey::fromJson(QJsonValue(QStringLiteral("X|Y|Z")));
    ASSERT_TRUE(from_string.has_value());
    EXPECT_EQ(from_string->toString(), QStringLiteral("X|Y|Z"));

    GroupKey original{QStringLiteral("A|B")};
    auto from_object = GroupKey::fromJson(original.toJson());
    ASSERT_TRUE(from_object.has_value());
    EXPECT_EQ(from_object->toString(), QStringLiteral("A|B"));
}

TEST(LightGroupKey, UsesUnknownObjectTokenWhenMissing)
{
    FrameInfo frame = makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(400, 300), 60.0, 0.0, 120, 8,
                                QStringLiteral("L"),
                                QString(), // object
                                QStringLiteral("2025-02-02"), QStringLiteral("Custom"));

    const LightGroupKey key = LightGroupKey::fromFrame(frame);

    GroupingOptions options;
    options.byResolution = true;
    options.byFilter = true;
    options.byObject = true;
    options.bySession = true;
    options.byIsoGain = false;
    options.byExposure = false;
    options.byCustomTag = false;

    const GroupKey group = key.toGroupKey(options);
    EXPECT_TRUE(group.toString().contains(QStringLiteral("(Unknown Object)")));
}

TEST(CalibrationGroupKey, MatchesCorrectly)
{
    FrameInfo light =
        makeFrame(FrameType::Light, QStringLiteral("light.fit"), QSize(600, 400), 30.0, 5.0, 200, 20,
                  QStringLiteral("Ha"), QStringLiteral("M31"), QStringLiteral("2025-03-03"), QStringLiteral("Tag"));

    FrameInfo bias = makeFrame(FrameType::Bias, QStringLiteral("bias.fit"), QSize(600, 400), 0.0, std::nullopt, 200, 20,
                               QString(), QString(), QStringLiteral(""), QStringLiteral("Tag"));

    FrameInfo dark = makeFrame(FrameType::Dark, QStringLiteral("dark.fit"), QSize(600, 400), 30.0, 5.0, 200, 20,
                               QString(), QString(), QStringLiteral(""), QStringLiteral("Tag"));

    FrameInfo flat =
        makeFrame(FrameType::Flat, QStringLiteral("flat.fit"), QSize(600, 400), 2.0, std::nullopt, 200, 20,
                  QStringLiteral("Ha"), QStringLiteral("M31"), QStringLiteral("2025-03-03"), QStringLiteral("Tag"));

    CalibrationGroupKey calibration = CalibrationGroupKey::fromLightKey(LightGroupKey::fromFrame(light));

    GroupingOptions options;
    options.byIsoGain = true;
    options.byExposure = true;
    options.byFilter = true;
    options.byObject = true;
    options.bySession = true;
    options.byCustomTag = true;

    EXPECT_TRUE(calibration.matchesBias(BiasGroupKey::fromFrame(bias), options));
    EXPECT_TRUE(calibration.matchesDark(DarkGroupKey::fromFrame(dark), options));
    EXPECT_TRUE(calibration.matchesFlat(FlatGroupKey::fromFrame(flat), options));
}

TEST(IntegrationGroupKey, WrapperWorks)
{
    IntegrationGroupKey key{GroupKey{QStringLiteral("Group|Key")}};
    IntegrationGroupKey same{GroupKey{QStringLiteral("Group|Key")}};

    EXPECT_EQ(key.toString(), QStringLiteral("Group|Key"));
    EXPECT_EQ(key, same);
}
