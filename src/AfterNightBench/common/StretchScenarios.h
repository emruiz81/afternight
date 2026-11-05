// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The mono stretch catalogue and strict, engine-neutral parameter decoding.
#pragma once

#include "Scenario.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::bench
{
/// Only settings used by the selected curve are declared. Values are Float32, as the product's
/// stretch settings hold them; the engine widens those same values to double.
inline ParameterSet stretchParameters(std::uint32_t function, bool interactive = false)
{
    ParameterSet result{{"function", std::to_string(function)}};
    if (function <= 3u || function == 5u || function == 6u)
    {
        result.insert({{"amount", interactive ? "3" : "2"},
                       {"symmetry", interactive ? "0.05" : "0.25"},
                       {"lowProtection", "0"},
                       {"highProtection", "1"}});
        if (function <= 1u)
            result["bias"] = "-0.5";
    }
    else if (function == 4u)
        result.insert({{"blackPoint", interactive ? "0.02" : "0.1"}, {"whitePoint", interactive ? "0.2" : "0.9"}});
    else if (function == 8u)
        result.insert({{"midtones", interactive ? "0.03" : "0.25"}, {"shadows", "0"}, {"highlights", "1"}});
    else if (function == 9u)
        result.insert({{"targetMedian", interactive ? "0.2" : "0.3"}, {"blackpointSigma", "5"}});
    return result;
}

/// Parsed settings; unsupported options are refused instead of inheriting an engine default.
struct StretchSettings
{
    std::uint32_t function = 0u;
    float amount = 0.0f;
    float bias = 0.0f;
    float symmetry = 0.0f;
    float lowProtection = 0.0f;
    float highProtection = 1.0f;
    float blackPoint = 0.0f;
    float whitePoint = 1.0f;
    float midtones = 0.5f;
    float shadows = 0.0f;
    float highlights = 1.0f;
    float targetMedian = 0.3f;
    float blackpointSigma = 5.0f;
    // The statistical stretch's optional finishing passes and black-point rule, each declared only
    // when a row changes it from the default.
    bool noBlackClip = false;
    bool normalize = false;
    bool applyCurves = false;
    float curvesBoost = 0.0f;
    /// Whether the statistics are measured outside the timed call and handed to it, the preview's
    /// shape, rather than measured inside it.
    bool preparedAnalysis = false;
};

/// The statistical stretch's optional settings. A row declares one only to change it, and
/// `curvesBoost` only beside `applyCurves`.
inline bool isOptionalStatisticalParameter(const std::string& name)
{
    return name == "noBlackClip" || name == "normalize" || name == "applyCurves" || name == "curvesBoost" ||
           name == "analysis";
}

/// Validate the currently supported packed mono Float32 workload before acquiring any input.
inline std::optional<StretchSettings> parseStretchSettings(const ScenarioSpec& scenario, std::string& error)
{
    auto refuse = [&](const std::string& reason) -> std::optional<StretchSettings> {
        error = "stretch scenario '" + scenario.id + "': " + reason;
        return std::nullopt;
    };
    if (scenario.group != "stretch" || scenario.operationVersion != 2u || !scenario.input.isValid() ||
        scenario.input.sampleType != SampleType::Float32 || scenario.input.channels != 1u ||
        scenario.input.layout != Layout::Planar || scenario.input.byteOrder != ByteOrder::Little ||
        scenario.input.rowStrideBytes != 0u || scenario.input.planeStrideBytes != 0u ||
        !scenario.input.cfaPattern.empty() || scenario.inputSource == InputSource::CorpusFrameSet)
        return refuse("requires version 2, packed little-endian mono Float32 without CFA");
    StretchSettings settings;
    const auto function = scenario.parameters.find("function");
    if (function == scenario.parameters.end())
        return refuse("missing function");
    const auto& text = function->second;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), settings.function);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || settings.function > 9u)
        return refuse("invalid function");
    const auto expected = stretchParameters(settings.function);
    for (const auto& [name, value] : scenario.parameters)
        if (!expected.contains(name) && name != kTimedBatchParameter &&
            !(settings.function == 9u && isOptionalStatisticalParameter(name)))
            return refuse("unsupported or inactive parameter '" + name + "'");
    for (const auto& [name, value] : expected)
        if (!scenario.parameters.contains(name))
            return refuse("missing parameter '" + name + "'");
    if (!timedBatchIterations(scenario))
        return refuse("invalid timedBatch");
    for (const auto& [name, destination] :
         std::vector<std::pair<std::string, float*>>{{"amount", &settings.amount},
                                                     {"bias", &settings.bias},
                                                     {"symmetry", &settings.symmetry},
                                                     {"lowProtection", &settings.lowProtection},
                                                     {"highProtection", &settings.highProtection},
                                                     {"blackPoint", &settings.blackPoint},
                                                     {"whitePoint", &settings.whitePoint},
                                                     {"midtones", &settings.midtones},
                                                     {"shadows", &settings.shadows},
                                                     {"highlights", &settings.highlights},
                                                     {"targetMedian", &settings.targetMedian},
                                                     {"blackpointSigma", &settings.blackpointSigma},
                                                     {"curvesBoost", &settings.curvesBoost}})
    {
        const auto found = scenario.parameters.find(name);
        if (found == scenario.parameters.end())
            continue;
        const auto& value = found->second;
        const auto number = std::from_chars(value.data(), value.data() + value.size(), *destination);
        if (number.ec != std::errc{} || number.ptr != value.data() + value.size() || !std::isfinite(*destination))
            return refuse("invalid finite Float32 parameter '" + name + "'");
    }
    // A switch is written "1", the only value that changes it; "0" would declare a default.
    for (const auto& [name, destination] :
         std::vector<std::pair<std::string, bool*>>{{"noBlackClip", &settings.noBlackClip},
                                                    {"normalize", &settings.normalize},
                                                    {"applyCurves", &settings.applyCurves}})
    {
        const auto found = scenario.parameters.find(name);
        if (found == scenario.parameters.end())
            continue;
        if (found->second != "1")
            return refuse("a statistical switch '" + name + "' is declared only as 1");
        *destination = true;
    }
    if (const auto analysis = scenario.parameters.find("analysis"); analysis != scenario.parameters.end())
    {
        if (analysis->second != "prepared")
            return refuse("the only declared statistical analysis is 'prepared'");
        settings.preparedAnalysis = true;
    }
    if (scenario.parameters.contains("curvesBoost") && !settings.applyCurves)
        return refuse("curvesBoost is declared only with applyCurves");
    if (settings.amount < 0.0f || settings.bias < -1.0f || settings.bias > 1.0f || settings.lowProtection < 0.0f ||
        settings.lowProtection > settings.symmetry || settings.symmetry > settings.highProtection ||
        settings.highProtection > 1.0f || settings.blackPoint < 0.0f || settings.blackPoint >= settings.whitePoint ||
        settings.whitePoint > 1.0f || settings.midtones < 0.0f || settings.midtones > 1.0f || settings.shadows < 0.0f ||
        settings.shadows > settings.highlights || settings.highlights > 1.0f || settings.targetMedian <= 0.0f ||
        settings.targetMedian >= 1.0f || settings.blackpointSigma < 0.0f || settings.curvesBoost < 0.0f ||
        settings.curvesBoost > 1.0f)
        return refuse("parameter outside the supported benchmark domain");
    return settings;
}

/// Ten ramp controls and nineteen cases per capture. Inversion has no adjustable parameter, so it
/// has one profile. Every output is held to the family's oracle at the common tier.
/// Non-quantized controls and small boundary fixtures follow for the families with a
/// complete-output oracle.
inline std::vector<ScenarioSpec> stretchScenarios()
{
    std::vector<ScenarioSpec> result;
    for (const std::string dataset : {"ramp", "eagle", "carina"})
        for (std::uint32_t function = 0u; function < 10u; ++function)
            for (const bool interactive : {false, true})
            {
                const bool ramp = dataset == "ramp";
                if (interactive && (ramp || function == 7u))
                    continue;
                ScenarioSpec scenario;
                scenario.id = ramp ? "stretch.function_" + std::to_string(function) + ".f32"
                                   : "stretch." + dataset + ".function_" + std::to_string(function) +
                                         (interactive ? ".interactive" : ".audit") + ".mono_f32.ordinary";
                scenario.group = "stretch";
                scenario.operationVersion = 2u;
                scenario.parameters = stretchParameters(function, interactive);
                scenario.inputSource = ramp ? InputSource::Synthetic : InputSource::Corpus;
                scenario.corpusId = ramp ? "synthetic.ramp" : "stretch." + dataset + ".unit_f32.v1";
                scenario.seed = ramp ? 20260822u : 0u;
                scenario.input.width = ramp ? 257u : dataset == "eagle" ? 3008u : 8288u;
                scenario.input.height = ramp ? 1u : dataset == "eagle" ? 3004u : 5644u;
                scenario.input.byteOrder = ByteOrder::Little;
                scenario.correctnessClass = CorrectnessClass::EstimatorTolerance;
                scenario.tolerance = 1.0e-6;
                scenario.elementsProcessed = static_cast<std::uint64_t>(scenario.input.width) * scenario.input.height;
                scenario.bytesProcessed = scenario.elementsProcessed * 4u;
                scenario.workUnit = "samples";
                scenario.allowedBackends = {BackendId::Unspecified};
                result.push_back(std::move(scenario));
            }
    // Non-quantized unit Float32 controls cannot be answered by a sensor-code lookup alone.
    for (const auto function : {0u, 1u, 2u, 3u, 5u, 6u, 8u})
        for (const bool interactive : {false, true})
        {
            auto scenario = result[function];
            scenario.id = "stretch.uniform_1mp.function_" + std::to_string(function) +
                          (interactive ? ".interactive" : ".audit") + ".mono_f32.ordinary";
            scenario.parameters = stretchParameters(function, interactive);
            scenario.corpusId = "synthetic.uniform_noise";
            scenario.input.width = 1024u;
            scenario.input.height = 1024u;
            scenario.elementsProcessed = 1024u * 1024u;
            scenario.bytesProcessed = scenario.elementsProcessed * 4u;
            result.push_back(std::move(scenario));
        }
    // Small boundary fixtures qualify both directions under limiting and protected profiles.
    for (const auto function : {2u, 3u})
        for (const std::string profile :
             {"zero", "tiny", "large", "protected", "coincident", "symmetry_black", "symmetry_white", "max_float"})
        {
            auto scenario = result[function];
            scenario.id =
                "stretch.boundaries.function_" + std::to_string(function) + "." + profile + ".mono_f32.ordinary";
            scenario.corpusId = "synthetic.asinh_boundaries";
            scenario.input.width = 4097u;
            scenario.elementsProcessed = 4097u;
            scenario.bytesProcessed = scenario.elementsProcessed * 4u;
            scenario.parameters["amount"] = profile == "zero"        ? "0"
                                            : profile == "tiny"      ? "0.000001"
                                            : profile == "max_float" ? "3.4028234663852886e38"
                                            : profile == "large"     ? "100"
                                                                     : "3";
            scenario.parameters["symmetry"] = "0.375";
            scenario.parameters["lowProtection"] = "0.125";
            scenario.parameters["highProtection"] = "0.75";
            if (profile == "coincident")
                scenario.parameters["lowProtection"] = scenario.parameters["highProtection"] = "0.375";
            if (profile == "symmetry_black")
                scenario.parameters["lowProtection"] = scenario.parameters["symmetry"] = "0";
            if (profile == "symmetry_white")
                scenario.parameters["highProtection"] = scenario.parameters["symmetry"] = "1";
            result.push_back(std::move(scenario));
        }
    // The generalized-hyperbolic family changes formula with its bias: each regime, and a bias one
    // millionth from the two values where one regime's formula becomes another's, at the limiting
    // amounts and with the symmetry point at either end. A zero amount and coincident protection
    // are the same curve in every regime.
    struct GhsBoundary
    {
        std::string profile;
        const char* amount;
        const char* bias;
        const char* symmetry;
        const char* lowProtection;
        const char* highProtection;
    };
    std::vector<GhsBoundary> ghsBoundaries{{"zero", "0", "-0.5", "0.375", "0.125", "0.75"},
                                           {"coincident", "3", "-0.5", "0.375", "0.375", "0.375"}};
    for (const auto& [regime, bias] :
         std::vector<std::pair<std::string, const char*>>{{"log", "-1"},
                                                          {"near_log", "-0.999999"},
                                                          {"integral", "-0.5"},
                                                          {"below_exponential", "-0.000001"},
                                                          {"exponential", "0"},
                                                          {"above_exponential", "0.000001"},
                                                          {"hyperbolic", "1"}})
    {
        for (const auto& [limit, amount] : std::vector<std::pair<std::string, const char*>>{
                 {"tiny", "0.000001"}, {"protected", "3"}, {"large", "100"}, {"max_float", "3.4028234663852886e38"}})
            ghsBoundaries.push_back({limit + "_" + regime, amount, bias, "0.375", "0.125", "0.75"});
        ghsBoundaries.push_back({"symmetry_black_" + regime, "3", bias, "0", "0", "0.75"});
        ghsBoundaries.push_back({"symmetry_white_" + regime, "3", bias, "1", "0.125", "1"});
    }
    for (const auto function : {0u, 1u})
        for (const GhsBoundary& boundary : ghsBoundaries)
        {
            auto scenario = result[function];
            scenario.id = "stretch.boundaries.function_" + std::to_string(function) + "." + boundary.profile +
                          ".mono_f32.ordinary";
            scenario.corpusId = "synthetic.ghs_boundaries";
            scenario.input.width = 4097u;
            scenario.elementsProcessed = 4097u;
            scenario.bytesProcessed = scenario.elementsProcessed * 4u;
            scenario.parameters["amount"] = boundary.amount;
            scenario.parameters["bias"] = boundary.bias;
            scenario.parameters["symmetry"] = boundary.symmetry;
            scenario.parameters["lowProtection"] = boundary.lowProtection;
            scenario.parameters["highProtection"] = boundary.highProtection;
            result.push_back(std::move(scenario));
        }
    // The midtones transfer is a step at a balance of zero, the identity at one half and zero below
    // the highlight point at one; a balance one millionth from either end is steepest. Each is
    // taken over the whole unit interval and between interior shadow and highlight points, beside
    // an automatic-stretch shadow point, a highlight point alone, a range of 2^-20 and equal points.
    struct MtfBoundary
    {
        std::string profile;
        const char* midtones;
        const char* shadows;
        const char* highlights;
    };
    const std::vector<MtfBoundary> mtfBoundaries{{"identity", "0.5", "0", "1"},
                                                 {"zero_midtones", "0", "0", "1"},
                                                 {"tiny_midtones", "0.000001", "0", "1"},
                                                 {"near_one_midtones", "0.999999", "0", "1"},
                                                 {"one_midtones", "1", "0", "1"},
                                                 {"shadows", "0.03", "0.02", "1"},
                                                 {"highlights", "0.25", "0", "0.75"},
                                                 {"clipped", "0.25", "0.125", "0.75"},
                                                 {"clipped_tiny_midtones", "0.000001", "0.125", "0.75"},
                                                 {"clipped_near_one_midtones", "0.999999", "0.125", "0.75"},
                                                 {"narrow", "0.25", "0.5", "0.50000095367431640625"},
                                                 {"step", "0.25", "0.375", "0.375"},
                                                 {"step_black", "0.25", "0", "0"}};
    for (const MtfBoundary& boundary : mtfBoundaries)
    {
        auto scenario = result[8];
        scenario.id = "stretch.boundaries.function_8." + boundary.profile + ".mono_f32.ordinary";
        scenario.corpusId = "synthetic.mtf_boundaries";
        scenario.input.width = 4097u;
        scenario.elementsProcessed = 4097u;
        scenario.bytesProcessed = scenario.elementsProcessed * 4u;
        scenario.parameters["midtones"] = boundary.midtones;
        scenario.parameters["shadows"] = boundary.shadows;
        scenario.parameters["highlights"] = boundary.highlights;
        result.push_back(std::move(scenario));
    }
    // The statistical stretch is defined by an image's exact minimum and median beside a spread
    // estimated from a bounded sample, so its controls hold more samples than that sample does. A
    // non-quantized control; each capture with its statistics measured outside the timed call, as a
    // preview reuses them; and limits over a sky whose black margin and cold pixels lie far below
    // the black point, over the non-quantized control, and with the black point at Eagle's minimum.
    const auto findRow = [&](const std::string& id) {
        return *std::find_if(result.begin(), result.end(), [&](const ScenarioSpec& row) { return row.id == id; });
    };
    for (const bool interactive : {false, true})
    {
        auto scenario = findRow("stretch.uniform_1mp.function_8.audit.mono_f32.ordinary");
        scenario.id = std::string("stretch.uniform_1mp.function_9.") + (interactive ? "interactive" : "audit") +
                      ".mono_f32.ordinary";
        scenario.parameters = stretchParameters(9u, interactive);
        result.push_back(std::move(scenario));
    }
    for (const std::string dataset : {"eagle", "carina"})
    {
        auto scenario = findRow("stretch." + dataset + ".function_9.audit.mono_f32.ordinary");
        scenario.id = "stretch." + dataset + ".function_9.prepared.mono_f32.ordinary";
        scenario.parameters["analysis"] = "prepared";
        result.push_back(std::move(scenario));
    }
    struct StatisticalBoundary
    {
        std::string profile;
        std::string base;
        std::vector<std::pair<std::string, std::string>> changes;
    };
    const std::string sky = "sky";
    const std::string uniform = "stretch.uniform_1mp.function_9.audit.mono_f32.ordinary";
    const std::vector<StatisticalBoundary> statisticalBoundaries{
        {"sky", sky, {}},
        {"sky_no_black_clip", sky, {{"noBlackClip", "1"}}},
        {"sky_normalize_curves", sky, {{"normalize", "1"}, {"applyCurves", "1"}, {"curvesBoost", "0.5"}}},
        {"eagle_no_black_clip", "stretch.eagle.function_9.audit.mono_f32.ordinary", {{"noBlackClip", "1"}}},
        {"uniform_no_black_clip", uniform, {{"noBlackClip", "1"}}},
        {"uniform_sigma_black", uniform, {{"blackpointSigma", "1"}}},
        {"uniform_zero_sigma", uniform, {{"blackpointSigma", "0"}}},
        {"uniform_normalize", uniform, {{"normalize", "1"}}},
        {"uniform_curves", uniform, {{"applyCurves", "1"}, {"curvesBoost", "0.5"}}},
        {"uniform_full_boost", uniform, {{"applyCurves", "1"}, {"curvesBoost", "1"}}},
        {"uniform_tiny_target", uniform, {{"targetMedian", "0.000001"}}},
        {"uniform_near_one_target", uniform, {{"targetMedian", "0.999999"}}}};
    for (const StatisticalBoundary& boundary : statisticalBoundaries)
    {
        auto scenario = findRow(boundary.base == sky ? uniform : boundary.base);
        scenario.id = "stretch.boundaries.function_9." + boundary.profile + ".mono_f32.ordinary";
        if (boundary.base == sky)
            scenario.corpusId = "synthetic.statistical_sky";
        for (const auto& [name, value] : boundary.changes)
            scenario.parameters[name] = value;
        result.push_back(std::move(scenario));
    }
    return result;
}
} // namespace an::bench
