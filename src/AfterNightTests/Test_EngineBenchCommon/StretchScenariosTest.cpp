// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the registered stretch workloads and their strict parameter boundary.
#include "StretchScenarios.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace an::bench;

TEST(StretchScenariosTest, EveryCaptureProfileHasOneIdentityAndBothRampsAndCapturesParse)
{
    const auto cases = stretchScenarios();
    ASSERT_EQ(cases.size(), 195u);
    std::set<std::string> ids;
    for (const auto& scenario : cases)
    {
        EXPECT_TRUE(ids.insert(scenario.id).second);
        EXPECT_TRUE(scenario.validate().empty()) << scenario.id;
        std::string error;
        EXPECT_TRUE(parseStretchSettings(scenario, error).has_value()) << error;
    }
}

TEST(StretchScenariosTest, RefusesMissingUnknownInactiveAndMalformedSettings)
{
    for (const auto& original : stretchScenarios())
    {
        std::string error;
        for (const auto& [name, value] : original.parameters)
        {
            auto scenario = original;
            scenario.parameters.erase(name);
            // An optional statistical setting only changes its default, so the row without it is the
            // default row; a boost left without the curve it belongs to is not a row at all.
            const bool optional = isOptionalStatisticalParameter(name) &&
                                  !(name == "applyCurves" && original.parameters.contains("curvesBoost"));
            EXPECT_EQ(parseStretchSettings(scenario, error).has_value(), optional) << name;
            for (const auto* invalid : {"", "NaN", "inf", "1tail", "1e100", " 1"})
            {
                scenario = original;
                scenario.parameters[name] = invalid;
                EXPECT_FALSE(parseStretchSettings(scenario, error)) << name << '=' << invalid;
            }
        }
        auto scenario = original;
        scenario.parameters["gamma"] = "2";
        EXPECT_FALSE(parseStretchSettings(scenario, error));
        scenario = original;
        scenario.parameters["timedBatch"] = "4";
        EXPECT_TRUE(parseStretchSettings(scenario, error));
        scenario.parameters["timedBatch"] = "0";
        EXPECT_FALSE(parseStretchSettings(scenario, error));
    }
    auto scenario = stretchScenarios()[7];
    scenario.parameters["amount"] = "2";
    std::string error;
    EXPECT_FALSE(parseStretchSettings(scenario, error));
}

TEST(StretchScenariosTest, RefusesUnsupportedRepresentationsAndInvalidDomains)
{
    const auto original = stretchScenarios()[0];
    std::string error;
    auto scenario = original;
    scenario.input.channels = 3u;
    EXPECT_FALSE(parseStretchSettings(scenario, error));
    scenario = original;
    scenario.input.rowStrideBytes = 2048u;
    EXPECT_FALSE(parseStretchSettings(scenario, error));
    scenario = original;
    scenario.input.sampleType = SampleType::Float64;
    EXPECT_FALSE(parseStretchSettings(scenario, error));
    scenario = original;
    scenario.operationVersion = 1u;
    EXPECT_FALSE(parseStretchSettings(scenario, error));
    for (const auto& [name, value] :
         ParameterSet{{"amount", "-1"}, {"bias", "2"}, {"lowProtection", "0.5"}, {"highProtection", "0.1"}})
    {
        scenario = original;
        scenario.parameters[name] = value;
        EXPECT_FALSE(parseStretchSettings(scenario, error)) << name;
    }
    // The midtones transfer admits the engine's closed domain: either balance end and equal
    // endpoints are curves, a balance outside the unit interval or crossed endpoints are not.
    const auto transfer = stretchScenarios()[8];
    for (const auto& [name, value] : std::vector<std::pair<std::string, std::string>>{
             {"midtones", "0"}, {"midtones", "1"}, {"shadows", "1"}, {"highlights", "0"}})
    {
        scenario = transfer;
        scenario.parameters[name] = value;
        EXPECT_TRUE(parseStretchSettings(scenario, error)) << name << '=' << value << ' ' << error;
    }
    for (const auto& [name, value] : std::vector<std::pair<std::string, std::string>>{
             {"midtones", "-0.1"}, {"midtones", "1.5"}, {"shadows", "-0.5"}, {"highlights", "1.5"}})
    {
        scenario = transfer;
        scenario.parameters[name] = value;
        EXPECT_FALSE(parseStretchSettings(scenario, error)) << name << '=' << value;
    }
    scenario = transfer;
    scenario.parameters["shadows"] = "0.5";
    scenario.parameters["highlights"] = "0.25";
    EXPECT_FALSE(parseStretchSettings(scenario, error));
}

TEST(StretchScenariosTest, StatisticalOptionsAreDeclaredOnlyToChangeTheirDefaultAndOnlyForThatStretch)
{
    const auto statistical = stretchScenarios()[9];
    ASSERT_EQ(statistical.id, "stretch.function_9.f32");
    std::string error;
    auto scenario = statistical;
    for (const auto* name : {"noBlackClip", "normalize", "applyCurves"})
    {
        scenario = statistical;
        scenario.parameters[name] = "1";
        const auto settings = parseStretchSettings(scenario, error);
        ASSERT_TRUE(settings) << name << ' ' << error;
        EXPECT_TRUE(settings->noBlackClip || settings->normalize || settings->applyCurves) << name;
        scenario.parameters[name] = "0";
        EXPECT_FALSE(parseStretchSettings(scenario, error)) << name << "=0 declares a default";
        scenario.parameters[name] = "true";
        EXPECT_FALSE(parseStretchSettings(scenario, error)) << name;
    }
    scenario = statistical;
    scenario.parameters["curvesBoost"] = "0.5";
    EXPECT_FALSE(parseStretchSettings(scenario, error)) << "a boost without its curve";
    scenario.parameters["applyCurves"] = "1";
    const auto curves = parseStretchSettings(scenario, error);
    ASSERT_TRUE(curves) << error;
    EXPECT_TRUE(curves->applyCurves);
    EXPECT_EQ(curves->curvesBoost, 0.5f);
    for (const auto* boost : {"-0.1", "1.5"})
    {
        scenario.parameters["curvesBoost"] = boost;
        EXPECT_FALSE(parseStretchSettings(scenario, error)) << boost;
    }
    scenario = statistical;
    scenario.parameters["analysis"] = "prepared";
    const auto prepared = parseStretchSettings(scenario, error);
    ASSERT_TRUE(prepared) << error;
    EXPECT_TRUE(prepared->preparedAnalysis);
    scenario.parameters["analysis"] = "complete";
    EXPECT_FALSE(parseStretchSettings(scenario, error));
    // No other stretch borrows them.
    for (const auto* name : {"noBlackClip", "normalize", "applyCurves", "analysis"})
    {
        scenario = stretchScenarios()[8];
        scenario.parameters[name] = std::string(name) == "analysis" ? "prepared" : "1";
        EXPECT_FALSE(parseStretchSettings(scenario, error)) << name;
    }
}

TEST(StretchScenariosTest, StatisticalRowsCoverControlsPreparedCapturesAndLimits)
{
    std::set<std::string> ids;
    std::size_t prepared = 0u;
    std::size_t boundaries = 0u;
    for (const auto& scenario : stretchScenarios())
    {
        if (scenario.parameters.at("function") != "9")
            continue;
        ids.insert(scenario.id);
        std::string error;
        const auto settings = parseStretchSettings(scenario, error);
        ASSERT_TRUE(settings) << error;
        prepared += settings->preparedAnalysis ? 1u : 0u;
        if (scenario.id.starts_with("stretch.boundaries."))
        {
            ++boundaries;
            EXPECT_GT(scenario.elementsProcessed, 400000u) << scenario.id << " must exceed the bounded sample";
        }
        if (scenario.id != "stretch.function_9.f32")
        {
            EXPECT_EQ(scenario.correctnessClass, CorrectnessClass::EstimatorTolerance) << scenario.id;
            EXPECT_EQ(scenario.tolerance, 1.0e-6) << scenario.id;
        }
    }
    EXPECT_EQ(ids.size(), 21u);
    EXPECT_EQ(prepared, 2u);
    EXPECT_EQ(boundaries, 12u);
    for (const auto* id :
         {"stretch.uniform_1mp.function_9.audit.mono_f32.ordinary",
          "stretch.uniform_1mp.function_9.interactive.mono_f32.ordinary",
          "stretch.carina.function_9.prepared.mono_f32.ordinary", "stretch.boundaries.function_9.sky.mono_f32.ordinary",
          "stretch.boundaries.function_9.eagle_no_black_clip.mono_f32.ordinary"})
        EXPECT_TRUE(ids.contains(id)) << id;
}
