// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the image combination, external-tool hand-off and edge tightening rows.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The seed of the rows' synthetic star fields.
inline constexpr std::uint64_t kCombinationSeed = 20261004u;

/// A colour frame combined with a luminance frame: the LRGB combination at the corpus's second setting.
inline constexpr const char* kLrgbCombine = "color.lrgb_combine.f32";

/// Every one of the product's twenty-seven blend modes of one colour frame over another.
inline constexpr const char* kBlendModes = "pointwise.blend_modes.f32";

/// A starless background and its stars recombined: the stars' edges tightened, added, and blended through a mask.
inline constexpr const char* kStarRecombine = "stars.recombine.f32";

/// A colour frame handed to an external tool and brought back: the automatic stretch, the sixteen-bit codes, and the
/// inverse of the answer of a tool that changes nothing.
inline constexpr const char* kExternalHandoff = "pointwise.external_handoff.f32";

/// A colour frame's edges tightened alone: the four-direction Sobel subtraction the product's Tighten Edges runs and
/// the recombination row composes.
inline constexpr const char* kEdgeTighten = "filter.edge_tighten.f32";

/// The rows of the image combinations, the hand-off and the edge tightening the recombination composes, each timed over
/// the complete operation it runs.
///
/// The LRGB row combines a 2048 by 2048 colour star field with a luminance frame derived from it, at lightness weight
/// 0.6, lightness midtones 0.3, saturation midtones 0.4 and channel weights 0.9, 1 and 0.8: decomposed into its
/// lightness, chroma and hue, stretched, blended, recomposed and weighted. The blend row runs all twenty-seven modes,
/// each one call of the composition, of a 1024 by 1024 colour star field over a second frame derived from it. The
/// recombination row tightens the edges of a 2048 by 2048 colour star field by one half, adds it to a derived starless
/// background and blends the sum over it through a derived one-channel mask, as the profiled recombination does. The
/// hand-off row takes a 2048 by 2048 colour star field through the automatic stretch per channel into sixteen-bit codes
/// and brings those codes back through the inverse, as StarNet does with a tool that returns what it was handed. The
/// edge tightening row tightens the recombination row's star field by one half alone, through the filter the product's
/// Tighten Edges runs, a neighbour past the frame's edge read as zero. Each is held to `CombinationOracle.h`, which
/// states its tolerance and why.
///
/// Each repeats its operation in one timed region the number of times `prepare_hard_floor_scenarios.py` chose on
/// `bench-windows` for its 150 ms floor at a safety factor of two.
///
/// @return the five rows, in a fixed order.
inline std::vector<ScenarioSpec> combinationScenarios()
{
    std::vector<ScenarioSpec> rows;

    const auto colour = [](const char* id, const char* group, std::uint32_t side) {
        ScenarioSpec row;
        row.id = id;
        row.group = group;
        row.operationVersion = 1;
        row.inputSource = InputSource::Synthetic;
        row.input.width = side;
        row.input.height = side;
        row.input.channels = 3u;
        row.input.sampleType = SampleType::Float32;
        row.input.layout = Layout::Planar;
        row.input.byteOrder = ByteOrder::Little;
        row.corpusId = "synthetic.stars";
        row.seed = kCombinationSeed;
        row.correctnessClass = CorrectnessClass::AbsoluteRelative;
        row.workUnit = "pixels";
        row.elementsProcessed = std::uint64_t{row.input.width} * row.input.height;
        row.allowedBackends = {BackendId::Unspecified};
        return row;
    };

    ScenarioSpec lrgb = colour(kLrgbCombine, "color", 2048u);
    lrgb.tolerance = 1.0e-4;
    lrgb.parameters["luminance"] = "derived";
    lrgb.parameters["lightnessWeight"] = "0.6";
    lrgb.parameters["lightnessMidtones"] = "0.3";
    lrgb.parameters["saturationMidtones"] = "0.4";
    lrgb.parameters["channelWeights"] = "0.9,1,0.8";
    lrgb.parameters[kTimedBatchParameter] = "17";
    lrgb.bytesProcessed = lrgb.input.totalBytes() / 3u * 7u;
    rows.push_back(lrgb);

    ScenarioSpec blend = colour(kBlendModes, "pointwise", 1024u);
    blend.tolerance = 1.0e-4;
    blend.parameters["modes"] = "27";
    blend.parameters["second"] = "derived";
    blend.parameters["opacity"] = "1";
    blend.parameters[kTimedBatchParameter] = "9";
    blend.elementsProcessed *= 27u;
    blend.bytesProcessed = blend.input.totalBytes() * 3u * 27u;
    rows.push_back(blend);

    ScenarioSpec recombine = colour(kStarRecombine, "stars", 2048u);
    recombine.tolerance = 1.0e-5;
    recombine.parameters["starless"] = "derived";
    recombine.parameters["mask"] = "derived";
    recombine.parameters["tightenEdges"] = "0.5";
    recombine.parameters[kTimedBatchParameter] = "36";
    recombine.bytesProcessed = recombine.input.totalBytes() / 3u * 10u;
    rows.push_back(recombine);

    ScenarioSpec handoff = colour(kExternalHandoff, "pointwise", 2048u);
    handoff.tolerance = 2.0e-3;
    handoff.parameters["preStretch"] = "unlinked";
    handoff.parameters["format"] = "uint16";
    handoff.parameters["answer"] = "handed";
    handoff.parameters[kTimedBatchParameter] = "12";
    handoff.bytesProcessed = handoff.input.totalBytes() * 3u;
    rows.push_back(handoff);

    ScenarioSpec tighten = colour(kEdgeTighten, "filter", 2048u);
    tighten.tolerance = 1.0e-5;
    tighten.parameters["amount"] = "0.5";
    tighten.parameters["border"] = "constant-zero";
    tighten.parameters[kTimedBatchParameter] = "94";
    tighten.bytesProcessed = tighten.input.totalBytes() * 2u;
    rows.push_back(tighten);

    return rows;
}

} // namespace an::bench
