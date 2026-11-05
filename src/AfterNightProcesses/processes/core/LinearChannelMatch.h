// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One plane matched linearly to another: the scale and offset that bring a component's robust location and scale onto
/// a reference's, measured over the samples both hold inside the match's limits and applied to every sample.
///
/// The channel composition and the integration's red, green and blue combination both match their components this way.
/// The limits - a sample below one ten-millionth of full scale or above seven eighths of it takes no part - are stated
/// once, beside the one match both call.
#pragma once

#include "AfterNightProcessesExport.h"

#include <AfterNightEngine/Algorithms/Normalization/GlobalNormalization.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::processes
{

/// The lowest sample, as a fraction of full scale, that decides a match.
inline constexpr double kLinearMatchLowestSample = 1.0e-7;

/// The highest sample, as a fraction of full scale, that decides a match.
inline constexpr double kLinearMatchHighestSample = 0.875;

/// One plane a match reads: its samples, where they are, and what they mean.
struct LinearMatchPlane
{
    const void* samples = nullptr;
    const an::ImageLayout* layout = nullptr;
    const an::ImageDescriptor* descriptor = nullptr;
};

/// The match that brings a component onto a reference: the ratio of their robust scales and the offset that moves the
/// component's robust location onto the reference's, both measured by the median absolute deviation over the samples
/// each plane holds inside the limits, over the whole of two planes of one extent.
///
/// @param execution the operation the measurement runs in.
/// @param reference the plane matched to.
/// @param component the plane matched.
/// @return the match; or what describing a plane, measuring it or estimating the match refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::FrameNormalization>
measureLinearChannelMatch(const an::ExecutionContext& execution, const LinearMatchPlane& reference,
                          const LinearMatchPlane& component);

/// Applies a match to every sample of a component, the ones outside the limits included.
///
/// @param execution the operation the match runs in.
/// @param match the match.
/// @param component the plane matched.
/// @param destination where the matched samples go: the component's representation and extent.
/// @param destinationLayout where they sit.
/// @return success; or what describing the plane or applying the match refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status
applyLinearChannelMatch(const an::ExecutionContext& execution, const an::FrameNormalization& match,
                        const LinearMatchPlane& component, void* destination, const an::ImageLayout& destinationLayout);

} // namespace an::ui::processes
