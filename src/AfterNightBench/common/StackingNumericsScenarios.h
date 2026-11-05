// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the comet composite and rejection-map statistics rows.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The seed of the rows' synthetic star fields.
inline constexpr std::uint64_t kStackingNumericsSeed = 20261005u;

/// A comet stacked twice and put back together: every frame split at its aperture, both layers stacked, the mask
/// drawn and the comet stack blended into the star stack.
inline constexpr const char* kCometComposite = "stacking.comet_composite.f32";

/// A stack's two rejection maps read whole: the elements either map reached, and the clusters each holds.
inline constexpr const char* kRejectionMapStatistics = "stacking.rejection_map_statistics.f32";

/// The rows of the comet composite and the rejection-map statistics, each timed over the complete composition it runs.
///
/// The comet row derives eight frames from a 1024 by 1024 star field, a comet moving sixteen pixels a frame across it,
/// once as the stars register them and once as the comet does; splits every frame at its aperture into the layer
/// that composition keeps, stacks each layer's eight frames by a plain average, draws the mask over the eight
/// apertures and blends the comet stack into the star stack - what the workflow's stars-plus-comet composite does
/// with frames already registered. The rejection row derives a stack's low and high rejection maps of a 2048 by 2048
/// colour star field - the high map rejecting the stars, the low map scattered singles and blocks - and reads both
/// whole as the workflow's spatial statistics do: the elements either map reached, and the clusters of each. Each is
/// held to `StackingNumericsOracle.h`, which states its tolerance and why. Neither row claims a cold-control
/// exemption: both are image-wide work.
///
/// Each repeats its operation in one timed region the number of times `prepare_hard_floor_scenarios.py` chose on
/// `bench-windows` for its 150 ms floor at a safety factor of two.
///
/// @return the two rows, in a fixed order.
inline std::vector<ScenarioSpec> stackingNumericsScenarios()
{
    std::vector<ScenarioSpec> rows;

    const auto field = [](const char* id, std::uint32_t side, std::uint32_t channels) {
        ScenarioSpec row;
        row.id = id;
        row.group = "stacking";
        row.operationVersion = 1;
        row.inputSource = InputSource::Synthetic;
        row.input.width = side;
        row.input.height = side;
        row.input.channels = channels;
        row.input.sampleType = SampleType::Float32;
        row.input.layout = Layout::Planar;
        row.input.byteOrder = ByteOrder::Little;
        row.corpusId = "synthetic.stars";
        row.seed = kStackingNumericsSeed;
        row.correctnessClass = CorrectnessClass::AbsoluteRelative;
        row.workUnit = "pixels";
        row.elementsProcessed = std::uint64_t{row.input.width} * row.input.height * channels;
        row.allowedBackends = {BackendId::Unspecified};
        return row;
    };

    ScenarioSpec comet = field(kCometComposite, 1024u, 1u);
    comet.tolerance = 1.0e-5;
    comet.parameters["frames"] = "8";
    comet.parameters["step"] = "16";
    comet.parameters["radius"] = "24";
    comet.parameters["stack"] = "Average";
    comet.parameters[kTimedBatchParameter] = "15";
    comet.elementsProcessed *= 8u;
    comet.bytesProcessed = comet.input.totalBytes() * 8u * 5u;
    rows.push_back(comet);

    ScenarioSpec rejection = field(kRejectionMapStatistics, 2048u, 3u);
    rejection.tolerance = 1.0e-9;
    rejection.parameters["frames"] = "8";
    rejection.parameters["minimumRejections"] = "1";
    rejection.parameters["minimumClusterPixels"] = "8";
    rejection.parameters["maximumClusters"] = "32";
    rejection.parameters[kTimedBatchParameter] = "8";
    rejection.bytesProcessed = rejection.input.totalBytes() * 2u;
    rows.push_back(rejection);

    return rows;
}

} // namespace an::bench
