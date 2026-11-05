// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/model/CalibrationMatcher.h"

namespace
{
LightGroupKey makeLightKey(const QString& resolution, std::optional<int> iso_gain, std::optional<int> offset,
                           double exposure, const QString& filter, const QString& object_name, const QString& session,
                           const QString& custom_tag)
{
    LightGroupKey key;
    key.resolution = resolution;
    key.isoGain = iso_gain;
    key.offset = offset;
    key.exposureSeconds = exposure;
    key.filter = filter;
    key.objectName = object_name;
    key.session = session;
    key.customTag = custom_tag;
    return key;
}

BiasCalibrationGroup makeBiasGroup(const BiasGroupKey& key, const GroupingOptions& options, const QString& path)
{
    return {key, key.toGroupKey(options), path};
}

DarkCalibrationGroup makeDarkGroup(const DarkGroupKey& key, const GroupingOptions& options, const QString& path)
{
    return {key, key.toGroupKey(options), path};
}

FlatCalibrationGroup makeFlatGroup(const FlatGroupKey& key, const GroupingOptions& options, const QString& path)
{
    return {key, key.toGroupKey(options), path};
}
} // namespace

TEST(CalibrationMatcher, ExactSessionMatchPreferred)
{
    GroupingOptions bias_options;
    GroupingOptions dark_options;
    GroupingOptions flat_options;
    flat_options.bySession = true;
    flat_options.byFilter = true;
    flat_options.byObject = true;
    flat_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, dark_options, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QStringLiteral("L"),
                                       QStringLiteral("M42"), QStringLiteral("2025-01-02"), QString());

    FlatGroupKey exact_flat;
    exact_flat.resolution = QStringLiteral("100x100");
    exact_flat.isoGain = 100;
    exact_flat.offset = 10;
    exact_flat.filter = QStringLiteral("L");
    exact_flat.objectName = QStringLiteral("M42");
    exact_flat.session = QStringLiteral("2025-01-02");

    FlatGroupKey close_flat = exact_flat;
    close_flat.session = QStringLiteral("2025-01-01");

    matcher.setFlatGroups({makeFlatGroup(close_flat, flat_options, QStringLiteral("flat_close.fit")),
                           makeFlatGroup(exact_flat, flat_options, QStringLiteral("flat_exact.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("flat_exact.fit"));
}

TEST(CalibrationMatcher, FallbackToClosestDateFlat)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;
    flat_options.byFilter = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QStringLiteral("L"),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat_a;
    flat_a.resolution = QStringLiteral("100x100");
    flat_a.filter = QStringLiteral("L");
    flat_a.session = QStringLiteral("2025-01-05");

    FlatGroupKey flat_b = flat_a;
    flat_b.session = QStringLiteral("2025-01-08");

    matcher.setFlatGroups({makeFlatGroup(flat_a, flat_options, QStringLiteral("flat_a.fit")),
                           makeFlatGroup(flat_b, flat_options, QStringLiteral("flat_b.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ClosestDate);
    EXPECT_EQ(match.masterPath, QStringLiteral("flat_b.fit"));
}

TEST(CalibrationMatcher, FlatSessionFallbackIgnoresOffsetWhenIsoGainGroupingDisabled)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;
    flat_options.byFilter = true;
    flat_options.byIsoGain = false;
    flat_options.byOffset = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 200, 40, 30.0, QStringLiteral("L"), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat;
    flat.resolution = QStringLiteral("100x100");
    flat.isoGain = 200;
    flat.offset = 5;
    flat.filter = QStringLiteral("L");
    flat.session = QStringLiteral("2025-01-08");

    matcher.setFlatGroups({makeFlatGroup(flat, flat_options, QStringLiteral("flat_offset_5.fit"))});

    const auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ClosestDate);
    EXPECT_EQ(match.masterPath, QStringLiteral("flat_offset_5.fit"));
}

TEST(CalibrationMatcher, RespectsMaxDaysApartLimit)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;

    CalibrationMatchingPolicy policy;
    policy.maxFlatSessionDaysApart = 1;

    CalibrationMatcher matcher({}, {}, flat_options, policy);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat;
    flat.resolution = QStringLiteral("100x100");
    flat.session = QStringLiteral("2025-01-05");

    matcher.setFlatGroups({makeFlatGroup(flat, flat_options, QStringLiteral("flat.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::None);
}

TEST(CalibrationMatcher, RequiresExactFilterMatch)
{
    GroupingOptions flat_options;
    flat_options.byFilter = true;
    flat_options.bySession = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QStringLiteral("L"),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat;
    flat.resolution = QStringLiteral("100x100");
    flat.filter = QStringLiteral("Ha");
    flat.session = QStringLiteral("2025-01-10");

    matcher.setFlatGroups({makeFlatGroup(flat, flat_options, QStringLiteral("flat.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::None);
}

TEST(CalibrationMatcher, RequiresExactDimensionMatch)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat;
    flat.resolution = QStringLiteral("200x200");
    flat.session = QStringLiteral("2025-01-10");

    matcher.setFlatGroups({makeFlatGroup(flat, flat_options, QStringLiteral("flat.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::None);
}

TEST(CalibrationMatcher, ManualOverrideTakesPriority)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat;
    flat.resolution = QStringLiteral("100x100");
    flat.session = QStringLiteral("2025-01-10");

    matcher.setFlatGroups({makeFlatGroup(flat, flat_options, QStringLiteral("flat.fit"))});

    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Flat;
    override.masterPath = QStringLiteral("override.fit");
    override.groupKey = GroupKey{QStringLiteral("override")};
    matcher.setOverrides({override});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(match.masterPath, QStringLiteral("override.fit"));
}

TEST(CalibrationMatcher, ReturnsMissingWhenNoMatch)
{
    CalibrationMatcher matcher({}, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::None);
}

TEST(CalibrationMatcher, ReturnsWarningForFallback)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat;
    flat.resolution = QStringLiteral("100x100");
    flat.session = QStringLiteral("2025-01-09");

    matcher.setFlatGroups({makeFlatGroup(flat, flat_options, QStringLiteral("flat.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ClosestDate);
    EXPECT_FALSE(match.warningMessage.isEmpty());
}

TEST(CalibrationMatcher, MultipleFlatsChoosesNearest)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat_a;
    flat_a.resolution = QStringLiteral("100x100");
    flat_a.session = QStringLiteral("2025-01-01");

    FlatGroupKey flat_b = flat_a;
    flat_b.session = QStringLiteral("2025-01-08");

    matcher.setFlatGroups({makeFlatGroup(flat_a, flat_options, QStringLiteral("flat_a.fit")),
                           makeFlatGroup(flat_b, flat_options, QStringLiteral("flat_b.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.masterPath, QStringLiteral("flat_b.fit"));
}

TEST(CalibrationMatcher, DeterministicTieBreaking)
{
    GroupingOptions flat_options;
    flat_options.bySession = true;

    CalibrationMatchingPolicy policy;
    policy.preferNewerFlat = true;

    CalibrationMatcher matcher({}, {}, flat_options, policy);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), std::nullopt, std::nullopt, 30.0, QString(),
                                       QString(), QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat_old;
    flat_old.resolution = QStringLiteral("100x100");
    flat_old.session = QStringLiteral("2025-01-09");

    FlatGroupKey flat_new = flat_old;
    flat_new.session = QStringLiteral("2025-01-11");

    matcher.setFlatGroups({makeFlatGroup(flat_old, flat_options, QStringLiteral("flat_old.fit")),
                           makeFlatGroup(flat_new, flat_options, QStringLiteral("flat_new.fit"))});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.masterPath, QStringLiteral("flat_new.fit"));
}

TEST(CalibrationMatcher, BiasMatchingWorks)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;
    bias_options.byCustomTag = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QStringLiteral("Tag"));

    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;
    bias_key.readoutMode = QStringLiteral("Default");
    bias_key.customTag = QStringLiteral("Tag");

    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias.fit"))});

    auto match = matcher.findBias(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("bias.fit"));
}

TEST(CalibrationMatcher, DarkMatchingWorks)
{
    GroupingOptions dark_options;
    dark_options.byIsoGain = true;
    dark_options.byExposure = true;

    CalibrationMatcher matcher({}, dark_options, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    DarkGroupKey dark_key;
    dark_key.resolution = QStringLiteral("100x100");
    dark_key.isoGain = 100;
    dark_key.offset = 10;
    dark_key.exposureSeconds = 30.0;
    dark_key.temperatureC = 0.0;

    matcher.setDarkGroups({makeDarkGroup(dark_key, dark_options, QStringLiteral("dark.fit"))});

    auto match = matcher.findDark(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("dark.fit"));
}

TEST(CalibrationMatcher, DarkMatching_PrefersClosestTemperatureWhenMultipleExactMatches)
{
    GroupingOptions dark_options;
    dark_options.byIsoGain = true;
    dark_options.byExposure = true;

    CalibrationMatcher matcher({}, dark_options, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    DarkGroupKey dark_minus10;
    dark_minus10.resolution = QStringLiteral("100x100");
    dark_minus10.isoGain = 100;
    dark_minus10.offset = 10;
    dark_minus10.exposureSeconds = 30.0;
    dark_minus10.temperatureC = -10.0;

    DarkGroupKey dark_minus5 = dark_minus10;
    dark_minus5.temperatureC = -5.0;

    matcher.setDarkGroups({
        makeDarkGroup(dark_minus10, dark_options, QStringLiteral("dark_-10.fit")),
        makeDarkGroup(dark_minus5, dark_options, QStringLiteral("dark_-5.fit")),
    });

    const auto match = matcher.findDark(light, -9.0);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("dark_-10.fit"));
    EXPECT_GE(match.confidence, 0.0);
    EXPECT_LE(match.confidence, 1.0);
    EXPECT_FALSE(match.needsUserAttention);
}

TEST(CalibrationMatcher, DarkMatching_IndicatesAmbiguousTemperatureMatches)
{
    GroupingOptions dark_options;
    dark_options.byIsoGain = true;
    dark_options.byExposure = true;

    CalibrationMatcher matcher({}, dark_options, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    DarkGroupKey dark_minus10;
    dark_minus10.resolution = QStringLiteral("100x100");
    dark_minus10.isoGain = 100;
    dark_minus10.offset = 10;
    dark_minus10.exposureSeconds = 30.0;
    dark_minus10.temperatureC = -10.0;

    DarkGroupKey dark_minus5 = dark_minus10;
    dark_minus5.temperatureC = -5.0;

    matcher.setDarkGroups({
        makeDarkGroup(dark_minus10, dark_options, QStringLiteral("dark_-10.fit")),
        makeDarkGroup(dark_minus5, dark_options, QStringLiteral("dark_-5.fit")),
    });

    const auto match = matcher.findDark(light, -7.5);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_TRUE(match.needsUserAttention);
    EXPECT_LE(match.confidence, 0.75);
    EXPECT_FALSE(match.warningMessage.isEmpty());
}

// =============================================================================
// Override Precedence and Bias-for-Dark Tests
// =============================================================================

TEST(CalibrationMatcher, BiasOverrideTakesPriority)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;

    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias_auto.fit"))});

    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Bias;
    override.masterPath = QStringLiteral("bias_override.fit");
    override.groupKey = GroupKey{QStringLiteral("override")};
    matcher.setOverrides({override});

    auto match = matcher.findBias(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(match.masterPath, QStringLiteral("bias_override.fit"));
}

TEST(CalibrationMatcher, DarkOverrideTakesPriority)
{
    GroupingOptions dark_options;
    dark_options.byIsoGain = true;
    dark_options.byExposure = true;

    CalibrationMatcher matcher({}, dark_options, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    DarkGroupKey dark_key;
    dark_key.resolution = QStringLiteral("100x100");
    dark_key.isoGain = 100;
    dark_key.offset = 10;
    dark_key.exposureSeconds = 30.0;
    dark_key.temperatureC = 0.0;

    matcher.setDarkGroups({makeDarkGroup(dark_key, dark_options, QStringLiteral("dark_auto.fit"))});

    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Dark;
    override.masterPath = QStringLiteral("dark_override.fit");
    override.groupKey = GroupKey{QStringLiteral("override")};
    matcher.setOverrides({override});

    auto match = matcher.findDark(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(match.masterPath, QStringLiteral("dark_override.fit"));
}

TEST(CalibrationMatcher, MultipleOverridesForDifferentTypes)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;
    GroupingOptions dark_options;
    dark_options.byExposure = true;
    GroupingOptions flat_options;
    flat_options.byFilter = true;

    CalibrationMatcher matcher(bias_options, dark_options, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QStringLiteral("L"), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    // Set auto-matched groups
    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;
    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias_auto.fit"))});

    DarkGroupKey dark_key;
    dark_key.resolution = QStringLiteral("100x100");
    dark_key.isoGain = 100;
    dark_key.offset = 10;
    dark_key.exposureSeconds = 30.0;
    matcher.setDarkGroups({makeDarkGroup(dark_key, dark_options, QStringLiteral("dark_auto.fit"))});

    FlatGroupKey flat_key;
    flat_key.resolution = QStringLiteral("100x100");
    flat_key.filter = QStringLiteral("L");
    matcher.setFlatGroups({makeFlatGroup(flat_key, flat_options, QStringLiteral("flat_auto.fit"))});

    // Set overrides for bias and flat only, leave dark to auto-match
    CalibrationOverride bias_override;
    bias_override.lightKey = light;
    bias_override.type = CalibrationOverrideType::Bias;
    bias_override.masterPath = QStringLiteral("bias_override.fit");
    bias_override.groupKey = GroupKey{QStringLiteral("override")};

    CalibrationOverride flat_override;
    flat_override.lightKey = light;
    flat_override.type = CalibrationOverrideType::Flat;
    flat_override.masterPath = QStringLiteral("flat_override.fit");
    flat_override.groupKey = GroupKey{QStringLiteral("override")};

    matcher.setOverrides({bias_override, flat_override});

    auto bias_match = matcher.findBias(light);
    EXPECT_EQ(bias_match.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(bias_match.masterPath, QStringLiteral("bias_override.fit"));

    auto dark_match = matcher.findDark(light);
    EXPECT_EQ(dark_match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(dark_match.masterPath, QStringLiteral("dark_auto.fit"));

    auto flat_match = matcher.findFlat(light);
    EXPECT_EQ(flat_match.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(flat_match.masterPath, QStringLiteral("flat_override.fit"));
}

TEST(CalibrationMatcher, OverrideForDifferentLightKeyNotApplied)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    LightGroupKey light1 = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                        QStringLiteral("2025-01-10"), QString());

    LightGroupKey light2 = makeLightKey(QStringLiteral("100x100"), 200, 10, 30.0, QString(), QString(),
                                        QStringLiteral("2025-01-10"), QString());

    BiasGroupKey bias_key1;
    bias_key1.resolution = QStringLiteral("100x100");
    bias_key1.isoGain = 100;
    bias_key1.offset = 10;

    BiasGroupKey bias_key2;
    bias_key2.resolution = QStringLiteral("100x100");
    bias_key2.isoGain = 200;
    bias_key2.offset = 10;

    matcher.setBiasGroups({makeBiasGroup(bias_key1, bias_options, QStringLiteral("bias_100.fit")),
                           makeBiasGroup(bias_key2, bias_options, QStringLiteral("bias_200.fit"))});

    // Override only for light1
    CalibrationOverride override;
    override.lightKey = light1;
    override.type = CalibrationOverrideType::Bias;
    override.masterPath = QStringLiteral("bias_override.fit");
    override.groupKey = GroupKey{QStringLiteral("override")};
    matcher.setOverrides({override});

    // light1 should use override
    auto match1 = matcher.findBias(light1);
    EXPECT_EQ(match1.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(match1.masterPath, QStringLiteral("bias_override.fit"));

    // light2 should use auto-match
    auto match2 = matcher.findBias(light2);
    EXPECT_EQ(match2.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match2.masterPath, QStringLiteral("bias_200.fit"));
}

TEST(CalibrationMatcher, BiasForDarkMatching_FindsBiasForDarkGroup)
{
    // When calibrating darks, we need to find matching bias frames
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    // Dark frames also need bias matching - create a light key from dark metadata
    LightGroupKey dark_as_light = makeLightKey(QStringLiteral("100x100"), 100, 10, 60.0, QString(), QString(),
                                               QStringLiteral("2025-01-10"), QString());

    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;

    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias.fit"))});

    auto match = matcher.findBias(dark_as_light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("bias.fit"));
}

TEST(CalibrationMatcher, EmptyOverridesListUsesAutoMatch)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;

    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias.fit"))});

    // Explicitly set empty overrides
    matcher.setOverrides({});

    auto match = matcher.findBias(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("bias.fit"));
}

TEST(CalibrationMatcher, OverrideWithEmptyPathIgnored)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;

    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias.fit"))});

    // Override with empty path should be ignored
    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Bias;
    override.masterPath = QString(); // Empty path
    override.groupKey = GroupKey{QStringLiteral("override")};
    matcher.setOverrides({override});

    auto match = matcher.findBias(light);
    // Should fall back to auto-match since override path is empty
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::Exact);
    EXPECT_EQ(match.masterPath, QStringLiteral("bias.fit"));
}

// =============================================================================
// Manually Unassigned Calibration Tests
// =============================================================================

TEST(CalibrationMatcher, UnassignedMarker_BiasReturnsManuallyUnassigned)
{
    GroupingOptions bias_options;
    bias_options.byIsoGain = true;

    CalibrationMatcher matcher(bias_options, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    BiasGroupKey bias_key;
    bias_key.resolution = QStringLiteral("100x100");
    bias_key.isoGain = 100;
    bias_key.offset = 10;

    matcher.setBiasGroups({makeBiasGroup(bias_key, bias_options, QStringLiteral("bias.fit"))});

    // Set override with unassigned marker
    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Bias;
    override.masterPath = QString::fromLatin1(kUnassignedCalibrationMarker);
    override.groupKey = GroupKey{};
    matcher.setOverrides({override});

    auto match = matcher.findBias(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManuallyUnassigned);
    EXPECT_TRUE(match.masterPath.isEmpty());
}

TEST(CalibrationMatcher, UnassignedMarker_DarkReturnsManuallyUnassigned)
{
    GroupingOptions dark_options;
    dark_options.byExposure = true;

    CalibrationMatcher matcher({}, dark_options, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    DarkGroupKey dark_key;
    dark_key.resolution = QStringLiteral("100x100");
    dark_key.isoGain = 100;
    dark_key.offset = 10;
    dark_key.exposureSeconds = 30.0;

    matcher.setDarkGroups({makeDarkGroup(dark_key, dark_options, QStringLiteral("dark.fit"))});

    // Set override with unassigned marker
    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Dark;
    override.masterPath = QString::fromLatin1(kUnassignedCalibrationMarker);
    override.groupKey = GroupKey{};
    matcher.setOverrides({override});

    auto match = matcher.findDark(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManuallyUnassigned);
    EXPECT_TRUE(match.masterPath.isEmpty());
}

TEST(CalibrationMatcher, UnassignedMarker_FlatReturnsManuallyUnassigned)
{
    GroupingOptions flat_options;
    flat_options.byFilter = true;
    flat_options.bySession = true;

    CalibrationMatcher matcher({}, {}, flat_options);

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QStringLiteral("L"), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    FlatGroupKey flat_key;
    flat_key.resolution = QStringLiteral("100x100");
    flat_key.filter = QStringLiteral("L");
    flat_key.session = QStringLiteral("2025-01-10");

    matcher.setFlatGroups({makeFlatGroup(flat_key, flat_options, QStringLiteral("flat.fit"))});

    // Set override with unassigned marker
    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Flat;
    override.masterPath = QString::fromLatin1(kUnassignedCalibrationMarker);
    override.groupKey = GroupKey{};
    matcher.setOverrides({override});

    auto match = matcher.findFlat(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManuallyUnassigned);
    EXPECT_TRUE(match.masterPath.isEmpty());
}

TEST(CalibrationMatcher, UnassignedMarker_ConfidenceIsOne)
{
    CalibrationMatcher matcher({}, {}, {});

    LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 30.0, QString(), QString(),
                                       QStringLiteral("2025-01-10"), QString());

    CalibrationOverride override;
    override.lightKey = light;
    override.type = CalibrationOverrideType::Bias;
    override.masterPath = QString::fromLatin1(kUnassignedCalibrationMarker);
    override.groupKey = GroupKey{};
    matcher.setOverrides({override});

    auto match = matcher.findBias(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManuallyUnassigned);
    EXPECT_DOUBLE_EQ(match.confidence, 1.0);
}

// A master assigned by hand names a file, not a group: the match points at the group whose master that file is, as the
// panel names it, so whatever reads which group a light takes - the groups a run stacks, the panels that point at the
// group a light uses - finds the group. A master no group makes, one browsed to, keeps the key it was stored with.
TEST(CalibrationMatcher, AMasterAssignedByHandPointsAtTheGroupWhoseMasterItIs)
{
    GroupingOptions dark_options;
    dark_options.byExposure = true;
    CalibrationMatcher matcher({}, dark_options, {});

    const LightGroupKey light = makeLightKey(QStringLiteral("100x100"), 100, 10, 300.0, QStringLiteral("L"),
                                             QStringLiteral("M42"), QStringLiteral("2025-01-10"), QString());
    DarkGroupKey short_dark;
    short_dark.resolution = QStringLiteral("100x100");
    short_dark.exposureSeconds = 120.0;
    DarkGroupKey long_dark = short_dark;
    long_dark.exposureSeconds = 600.0;
    const DarkCalibrationGroup short_group =
        makeDarkGroup(short_dark, dark_options, QStringLiteral("C:/output/master_dark_120.fit"));
    const DarkCalibrationGroup long_group =
        makeDarkGroup(long_dark, dark_options, QStringLiteral("C:/output/master_dark_600.fit"));
    ASSERT_FALSE(short_group.groupKey == long_group.groupKey);
    matcher.setDarkGroups({long_group, short_group});

    CalibrationOverride assigned;
    assigned.lightKey = light;
    assigned.type = CalibrationOverrideType::Dark;
    assigned.masterPath = QStringLiteral("c:\\output\\master_dark_120.fit");
    assigned.groupKey = GroupKey{assigned.masterPath};
    matcher.setOverrides({assigned});

    const CalibrationMatch match = matcher.findDark(light);
    EXPECT_EQ(match.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(match.masterPath, assigned.masterPath);
    EXPECT_EQ(match.matchedGroupKey, short_group.groupKey.toString());

    CalibrationOverride browsed = assigned;
    browsed.masterPath = QStringLiteral("D:/elsewhere/a_master_dark.fit");
    browsed.groupKey = GroupKey{browsed.masterPath};
    matcher.setOverrides({browsed});
    const CalibrationMatch outside = matcher.findDark(light);
    EXPECT_EQ(outside.type, CalibrationMatch::MatchType::ManualOverride);
    EXPECT_EQ(outside.matchedGroupKey, browsed.groupKey.toString());
}

// Whether a frame takes the bias beside its dark: a master dark holds the bias, so where a dark is applied a light
// takes the bias only in CCD mode and a flat never; where none is - none matched, or the person unassigned it - both
// do.
TEST(CalibrationMatcher, AFrameTakesTheBiasWhereNoDarkIsAppliedToIt)
{
    CalibrationMatch matched;
    matched.type = CalibrationMatch::MatchType::Exact;
    CalibrationMatch unassigned;
    unassigned.type = CalibrationMatch::MatchType::ManuallyUnassigned;
    const CalibrationMatch none;

    EXPECT_FALSE(lightTakesBias(matched, false));
    EXPECT_TRUE(lightTakesBias(matched, true));
    EXPECT_TRUE(lightTakesBias(unassigned, false));
    EXPECT_TRUE(lightTakesBias(none, false));

    EXPECT_FALSE(flatTakesBias(matched));
    EXPECT_TRUE(flatTakesBias(unassigned));
    EXPECT_TRUE(flatTakesBias(none));
}
