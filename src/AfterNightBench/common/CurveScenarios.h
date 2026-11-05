// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the tone curve rows.

#pragma once

#include "CurveOracle.h"
#include "Scenario.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The parameter naming what a curve row's clock holds.
inline constexpr const char* kCurveBoundaryParameter = "timedBoundary";
/// The curve applied from a curve made before the clock: the application alone.
inline constexpr const char* kCurvePrepared = "prepared";
/// The product's Apply: the curve made from its points and applied, both inside the clock.
inline constexpr const char* kCurveComplete = "complete";
/// The product's real-time preview: what the engine makes of the curve for a preview, and its application.
inline constexpr const char* kCurvePreview = "preview";

/// The parameter naming how a row's samples are made from its fixture before the clock.
inline constexpr const char* kCurveSamplesParameter = "samples";
/// Sixteen-bit codes carried into the unit interval.
inline constexpr const char* kCurveCodes = "codes-over-65535";
/// A colour frame made from a Bayer capture, each two-by-two block one pixel.
inline constexpr const char* kCurveBinnedColour = "binned-rggb-over-65535";
/// The fixture's own floating samples, held to the unit interval.
inline constexpr const char* kCurveHeldToUnit = "held-to-unit";
/// The fixture's own floating samples.
inline constexpr const char* kCurveAsGiven = "as-given";
/// The fixture's own sixteen-bit codes, curved as codes: the product applies a curve to an integer image in its own
/// representation.
inline constexpr const char* kCurveCodesAsGiven = "codes-as-given";

/// The entries of the table the product samples a floating image's curve into.
inline constexpr std::uint32_t kCurveTableEntries = 8193u;
/// The entries of the exact table the product holds a sixteen-bit image's curve in: one for every code.
inline constexpr std::uint32_t kCurveCodeTableEntries = 65536u;
/// The entries of a sixteen-bit real-time preview table.
inline constexpr std::uint32_t kCurvePreviewTableEntries = 1024u;

/// The control points every curve row draws: an S-shaped tone curve, steep through the middle and flat at both ends,
/// the shape the curves dialog is most often given, and the fewest points Akima's rule is defined over.
inline constexpr std::array<CurvePoint, 5u> kCurveRowPoints{
    CurvePoint{0.0, 0.0}, CurvePoint{0.25, 0.12}, CurvePoint{0.5, 0.5}, CurvePoint{0.75, 0.88}, CurvePoint{1.0, 1.0}};

/// The gamma-plus-curve row: the gamma, and the two-segment curve's middle point.
inline constexpr double kGammaCurveGamma = 2.2;
inline constexpr double kGammaCurveMiddleInput = 0.5;
inline constexpr double kGammaCurveMiddleOutput = 0.25;
/// The entries of the table the fused gamma-plus-curve program samples its curve into.
inline constexpr std::uint32_t kGammaCurveTableEntries = 4097u;

/// The calibrated, registered light the continuous rows read, from the integration corpus.
inline constexpr const char* kCurveCalibratedLight = "carina.asi294mm.red.light0.aligned.f32";

/// The tone curve rows.
///
/// The product's curves dialog applies a curve through its points under the rule the dialog names: the engine
/// samples a floating image's curve into a table of 8,193 entries and interpolates it, and an eight- or sixteen-bit
/// image's into a table of every code, for the preview and the Apply alike. The rows time the Apply over a 1024 by 1024
/// gradient under each of the three cubic rules, from a curve made before the clock and complete with its making, and
/// the complete Apply over two mono captures, a colour frame made from a Bayer capture and a calibrated light held to
/// the unit interval, whose continuous samples no sixteen-bit grid shortens; the preview over the gradient; the Apply
/// and the preview over the gradient's sixteen-bit codes and the Apply over the two captures' own codes; and a gamma
/// followed by a two-segment curve, one fused program. Every output is compared whole with `CurveOracle.h`, or for the
/// gamma row with `GammaTransferOracle.h`.
///
/// The calibrated light is held to the unit interval before the clock, inside the curve's points; the engine's answer
/// outside the points - the end values - is its suites' to hold.
///
/// @return the rows, in a fixed order.
inline std::vector<ScenarioSpec> curveScenarios()
{
    std::vector<ScenarioSpec> rows;
    const auto gradient = [](const std::string& id) {
        ScenarioSpec row;
        row.id = id;
        row.group = "pointwise";
        row.operationVersion = 2;
        row.inputSource = InputSource::Synthetic;
        row.corpusId = "synthetic.gradient";
        row.seed = 20260822u;
        row.input.width = 1024u;
        row.input.height = 1024u;
        row.input.channels = 1u;
        row.input.sampleType = SampleType::Float32;
        row.input.layout = Layout::Planar;
        row.input.byteOrder = ByteOrder::Little;
        row.correctnessClass = CorrectnessClass::AbsoluteRelative;
        row.tolerance = 1e-6;
        row.workUnit = "samples";
        row.elementsProcessed = std::uint64_t{row.input.width} * row.input.height;
        row.bytesProcessed = row.elementsProcessed * sizeof(float);
        row.allowedBackends = {BackendId::Unspecified};
        return row;
    };
    struct Rule
    {
        const char* stem;
        const char* name;
    };
    const Rule rules[] = {{"akima", "akima"}, {"natural_cubic", "natural-cubic"}, {"steffen", "steffen"}};
    for (const char* boundary : {kCurvePrepared, kCurveComplete})
    {
        for (const Rule& rule : rules)
        {
            const bool prepared = std::string(boundary) == kCurvePrepared;
            ScenarioSpec row =
                gradient(std::string("pointwise.curve_") + rule.stem + (prepared ? "" : ".complete") + ".f32");
            row.parameters["interpolation"] = rule.name;
            row.parameters["tableEntries"] = std::to_string(kCurveTableEntries);
            row.parameters[kCurveBoundaryParameter] = boundary;
            row.parameters[kCurveSamplesParameter] = kCurveAsGiven;
            rows.push_back(row);
        }
    }

    ScenarioSpec preview = gradient("pointwise.curve_akima.preview.f32");
    preview.parameters["interpolation"] = "akima";
    preview.parameters["tableEntries"] = std::to_string(kCurveTableEntries);
    preview.parameters["previewTableEntries"] = std::to_string(kCurvePreviewTableEntries);
    preview.parameters[kCurveBoundaryParameter] = kCurvePreview;
    preview.parameters[kCurveSamplesParameter] = kCurveAsGiven;
    // A preview reads its Apply's sampled table rather than the curve itself, so it is held within a step of
    // that table and of the curve's steepest rise.
    preview.tolerance = 5e-3;
    rows.push_back(preview);

    struct Source
    {
        const char* name;
        const char* corpusId;
        std::uint32_t width;
        std::uint32_t height;
        const char* samples;
    };
    const Source sources[] = {{"eagle", "eagle.qhy533m.ha.light", 3008u, 3004u, kCurveCodes},
                              {"carina", "carina.asi294mm.red.light", 8288u, 5644u, kCurveCodes},
                              {"ngc253_rgb", "ngc253.qhy183c.light", 5544u, 3684u, kCurveBinnedColour},
                              {"carina_calibrated", kCurveCalibratedLight, 8288u, 5644u, kCurveHeldToUnit}};
    ScenarioSpec codes = gradient("pointwise.curve_akima.complete.u16");
    codes.operationVersion = 1;
    codes.input.sampleType = SampleType::UInt16;
    codes.bytesProcessed = codes.elementsProcessed * sizeof(std::uint16_t);
    // A code's answer is the held curve narrowed to the nearest code, so it is held within a little more than one
    // sixteen-bit code of the curve.
    codes.tolerance = 2e-5;
    codes.parameters["interpolation"] = "akima";
    codes.parameters["tableEntries"] = std::to_string(kCurveCodeTableEntries);
    codes.parameters[kCurveBoundaryParameter] = kCurveComplete;
    codes.parameters[kCurveSamplesParameter] = kCurveCodesAsGiven;
    rows.push_back(codes);
    ScenarioSpec codesPreview = codes;
    codesPreview.id = "pointwise.curve_akima.preview.u16";
    codesPreview.parameters[kCurveBoundaryParameter] = kCurvePreview;
    codesPreview.parameters["previewTableEntries"] = std::to_string(kCurvePreviewTableEntries);
    codesPreview.tolerance = preview.tolerance;
    rows.push_back(codesPreview);

    for (const Source& source : sources)
    {
        const bool colour = source.samples == std::string(kCurveBinnedColour);
        const bool floating = source.samples == std::string(kCurveHeldToUnit);
        ScenarioSpec row;
        row.id = std::string("pointwise.curve_akima.complete.") + source.name + ".f32";
        row.group = "pointwise";
        row.operationVersion = 1;
        row.inputSource = InputSource::Corpus;
        row.corpusId = source.corpusId;
        row.input.width = source.width;
        row.input.height = source.height;
        row.input.channels = 1u;
        row.input.sampleType = floating ? SampleType::Float32 : SampleType::UInt16;
        row.input.layout = Layout::Planar;
        row.input.byteOrder = ByteOrder::Little;
        row.correctnessClass = CorrectnessClass::AbsoluteRelative;
        row.tolerance = 1e-6;
        row.parameters["interpolation"] = "akima";
        row.parameters["tableEntries"] = std::to_string(kCurveTableEntries);
        row.parameters[kCurveBoundaryParameter] = kCurveComplete;
        row.parameters[kCurveSamplesParameter] = source.samples;
        const std::uint64_t pixels = colour ? std::uint64_t{source.width / 2u} * (source.height / 2u)
                                            : std::uint64_t{source.width} * source.height;
        row.workUnit = "samples";
        row.elementsProcessed = pixels * (colour ? 3u : 1u);
        row.bytesProcessed = row.elementsProcessed * sizeof(float);
        row.allowedBackends = {BackendId::Unspecified};
        rows.push_back(row);
        if (source.samples == std::string(kCurveCodes))
        {
            ScenarioSpec ownCodes = row;
            ownCodes.id = std::string("pointwise.curve_akima.complete.") + source.name + ".u16";
            ownCodes.tolerance = codes.tolerance;
            ownCodes.parameters[kCurveSamplesParameter] = kCurveCodesAsGiven;
            ownCodes.parameters["tableEntries"] = std::to_string(kCurveCodeTableEntries);
            ownCodes.bytesProcessed = ownCodes.elementsProcessed * sizeof(std::uint16_t);
            rows.push_back(ownCodes);
        }
    }

    ScenarioSpec gammaCurve = gradient("pointwise.gamma_curve_fused.f32");
    gammaCurve.operationVersion = 1;
    gammaCurve.tolerance = 1e-5;
    gammaCurve.parameters["gamma"] = "2.2";
    gammaCurve.parameters["curveMiddleInput"] = "0.5";
    gammaCurve.parameters["curveMiddleOutput"] = "0.25";
    gammaCurve.parameters["tableEntries"] = std::to_string(kGammaCurveTableEntries);
    gammaCurve.elementsProcessed *= 2u;
    gammaCurve.workUnit = "pointwise-ops";
    rows.push_back(gammaCurve);
    return rows;
}

/// Whether a scenario is one of `curveScenarios()`'s rows other than the gamma row.
///
/// @param id the scenario's identity.
/// @return true for a curve row.
[[nodiscard]] inline bool isCurveScenario(const std::string& id)
{
    return id.rfind("pointwise.curve_", 0u) == 0u;
}

} // namespace an::bench
