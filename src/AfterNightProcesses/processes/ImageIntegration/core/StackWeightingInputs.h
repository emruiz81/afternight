// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What the stack's frame weighting reads of a light, what a group asks of it, and the weights it derives: one rule
/// for the stack, the Frame Quality dashboard's preview and the workflow report.
///
/// The stacking stage builds each light's measurement from its registration record and asks the engine's weighting for
/// the group's source, floor, star-count scaling and reference. The dashboard and the report computed the same frames'
/// weights by rules of their own - a width weighting without the stack's star-count penalty, a roundness rule that read
/// no roundness, a noise weighting by the signal-to-noise ratio - so what they showed was not what the stack applied.
/// Here all three build the measurement one way and ask the engine's own `computeStackFrameWeights`.

#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/FrameQuality.h"

#include <AfterNightEngine/Algorithms/Stacking/StackFrameWeighting.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWeightingParameters.h>
#include <AfterNightEngine/Core/Result.h>

#include <QString>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct StackingSettings;

namespace an
{
class ExecutionContext;
}

namespace ImageIntegration
{

/// The figures the stack's frame weighting reads of a light from its registration record, each only where the record
/// holds a value the engine takes: its star count and their roundness where stars were found - no roundness where the
/// registration measured none, which it records as an eccentricity of one - its star width, its signal-to-noise ratio,
/// its background noise with the scale that restates it (the IKSS scale, else the MAD) and the photometric terms.
///
/// @param metrics the light's registration metrics.
/// @param noise the light's background noise and scales; empty where a weighting reads no noise.
/// @return the measurement.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StackFrameMeasurement
stackFrameMeasurementFor(const FrameQualityMetrics& metrics, const FrameNoiseStatistics& noise);

/// Whether a light's measurement carries the figure a weighting reads: its background noise, its star count, its star
/// width or its zero point; true for a weighting that reads none of them.
///
/// @param weighting the weighting.
/// @param measurement the light's measurement.
/// @return whether the stack can weigh the light by it.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool carriesWeightingFigure(an::StackWeighting weighting,
                                                                  const an::StackFrameMeasurement& measurement);

/// What a weighting reads, as a person reads it: "per-frame star counts", "per-frame FWHM".
[[nodiscard]] AFTERNIGHTPROCESSES_API QString weightingInputDescription(an::StackWeighting weighting);

/// The weighting a group asks of its stack: the source, its floor - held to the unit interval, and the photometric
/// model's raw floor too - and the star-count scaling and its steepness (the engine's default where the stored one is
/// not positive). Every channel is weighed by its light's one star width: the registration measures one per light.
///
/// @param settings the group's settings, as sanitized.
/// @param weighting the source the stack weighs by.
/// @param scaling the star-count scaling the settings name.
/// @return the request.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StackWeightingRequest
stackWeightingRequestFor(const StackingSettings& settings, an::StackWeighting weighting, an::StarCountScaling scaling);

/// Where a group's reference is among the frames a stack weighs: found by identity, or the first of them by identity
/// when it is not among them - the light the stacking stage weighs against when the manifest names none it stacks.
///
/// @param frames the frames, in the order their measurements are.
/// @param reference the group's reference, or nothing.
/// @return the reference's index; zero for no frames.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::uint32_t
stackWeightingReferenceIndex(std::span<const FrameId> frames, const std::optional<FrameId>& reference);

/// The weights a stack derives from its frames' measurements.
struct DerivedStackWeights
{
    /// One weight per frame, in their order, with mean one.
    std::vector<double> weights;

    /// What the engine's weighting says of them: its disposition and how many it raised to the floor.
    an::StackFrameWeightingResult result;
};

/// The weights a stack derives for these measurements under this weighting against the frame at `referenceIndex`,
/// derived by the engine's own `computeStackFrameWeights` in an operation the caller admitted. A stack's frame weights
/// depend on the measurements, the weighting and the reference only, so the stack is described by the smallest image
/// that states one.
///
/// @param execution the admitted operation.
/// @param weighting the weighting, as `stackWeightingRequestFor` states it.
/// @param measurements one measurement per frame.
/// @param referenceIndex the reference frame's index.
/// @return the weights; the engine's refusal of the stack it would be, or of the measurements.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<DerivedStackWeights>
deriveStackWeights(const an::ExecutionContext& execution, const an::StackWeightingRequest& weighting,
                   std::span<const an::StackFrameMeasurement> measurements, std::uint32_t referenceIndex);

} // namespace ImageIntegration
