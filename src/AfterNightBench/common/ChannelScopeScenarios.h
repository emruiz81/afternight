// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the channel scope and image difference rows.

#pragma once

#include "Scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// The seed of the rows' synthetic star fields.
inline constexpr std::uint64_t kChannelScopeSeed = 20261002u;

/// A process run on the CIE lightness of a colour frame: converted, processed and recomposed.
inline constexpr const char* kChannelScopeLabLightness = "color.channel_scope.lab_l.f32";

/// The comparison view's difference of two colour frames, every channel's absolute difference.
inline constexpr const char* kImageDifference = "pointwise.difference.f32";

/// The gamma the scoped process applies to the lightness.
inline constexpr double kChannelScopeGamma = 2.2;

/// The rows of the channel scope and the image difference, timed over the complete composition a process or a
/// comparison runs.
///
/// The scope row reads a colour star field as sRGB-encoded, as the product's channel processors do, converts it to
/// L*a*b* packed into the unit interval, raises the lightness to one over 2.2, and recomposes red, green and blue;
/// the clock holds the conversion, the process and the recomposition, because a channel-dispatched process does all
/// three. It is held to the definition in `ChannelScopeOracle.h` after both are held to the unit interval. The
/// difference row takes the absolute difference of every channel of two colour frames - the second a fixed offset of
/// the first, so the two differ everywhere - and the engine narrows the double difference to Float32, so it is
/// bit-exact and held to
/// `DifferenceOracle.h` exactly. Each repeats its operation in one timed region the number of times
/// `prepare_hard_floor_scenarios.py` chose on `bench-windows` for its 150 ms floor at a safety factor of two, 17 and
/// 90.
///
/// @return the two rows, in a fixed order.
inline std::vector<ScenarioSpec> channelScopeScenarios()
{
    std::vector<ScenarioSpec> rows;

    ScenarioSpec scope;
    scope.id = kChannelScopeLabLightness;
    scope.group = "color";
    scope.operationVersion = 1;
    scope.inputSource = InputSource::Synthetic;
    scope.input.width = 2048u;
    scope.input.height = 2048u;
    scope.input.channels = 3u;
    scope.input.sampleType = SampleType::Float32;
    scope.input.layout = Layout::Planar;
    scope.input.byteOrder = ByteOrder::Little;
    scope.corpusId = "synthetic.stars";
    scope.seed = kChannelScopeSeed;
    scope.correctnessClass = CorrectnessClass::AbsoluteRelative;
    scope.tolerance = 1.0e-4;
    scope.parameters["scope"] = "cie_lightness";
    scope.parameters["encoding"] = "srgb";
    scope.parameters["units"] = "unit_interval";
    scope.parameters["operation"] = "gamma";
    scope.parameters["gamma"] = "2.2";
    scope.parameters[kTimedBatchParameter] = "17";
    scope.workUnit = "pixels";
    scope.elementsProcessed = std::uint64_t{scope.input.width} * scope.input.height;
    scope.bytesProcessed = scope.input.totalBytes() * 2u;
    scope.allowedBackends = {BackendId::Unspecified};
    rows.push_back(scope);

    ScenarioSpec difference;
    difference.id = kImageDifference;
    difference.group = "pointwise";
    difference.operationVersion = 1;
    difference.inputSource = InputSource::Synthetic;
    difference.input.width = 2048u;
    difference.input.height = 2048u;
    difference.input.channels = 3u;
    difference.input.sampleType = SampleType::Float32;
    difference.input.layout = Layout::Planar;
    difference.input.byteOrder = ByteOrder::Little;
    difference.corpusId = "synthetic.stars";
    difference.seed = kChannelScopeSeed;
    difference.correctnessClass = CorrectnessClass::BitExact;
    difference.parameters["measure"] = "absolute";
    difference.parameters["second"] = "offset";
    difference.parameters[kTimedBatchParameter] = "90";
    difference.workUnit = "samples";
    difference.elementsProcessed =
        std::uint64_t{difference.input.width} * difference.input.height * difference.input.channels;
    difference.bytesProcessed = difference.input.totalBytes() * 3u;
    difference.allowedBackends = {BackendId::Unspecified};
    rows.push_back(difference);

    return rows;
}

} // namespace an::bench
