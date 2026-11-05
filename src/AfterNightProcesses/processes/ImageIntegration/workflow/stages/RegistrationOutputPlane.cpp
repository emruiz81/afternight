// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The plane a registered group is aligned onto, planned from the registration's transforms.

#include "stdafx.hpp"
#include "RegistrationOutputPlane.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace stage_utils
{

namespace
{

/// Every frame's rectangle on the reference's plane, each frame's distortion corrected first.
[[nodiscard]] an::Result<std::vector<an::RegistrationBounds>>
boundsOf(const std::span<const RegistrationOutputPlaneFrame> frames, const std::size_t referenceIndex)
{
    if (frames.empty() || referenceIndex >= frames.size())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "A registered group's plane needs its frames and its reference among them");
    }
    std::vector<an::RegistrationBounds> bounds;
    bounds.reserve(frames.size());
    for (const RegistrationOutputPlaneFrame& frame : frames)
    {
        const an::Result<an::RegistrationBounds> bounded =
            an::computeRegistrationBounds(frame.transform, frame.width, frame.height, an::PixelCoordinate{},
                                          frame.distortion.has_value() ? &*frame.distortion : nullptr);
        if (!bounded.hasValue())
        {
            return bounded.status();
        }
        bounds.push_back(*bounded);
    }
    return bounds;
}

/// A canvas planned for a group's canvas mode, or the standard canvas in place of an intersection the engine finds
/// empty, and whether it stands in.
template <typename Canvas, typename Plan>
[[nodiscard]] an::Result<std::pair<Canvas, bool>>
planWithStandardStandIn(const ImageIntegration::RegistrationCanvasMode mode, const Plan& plan)
{
    an::Result<Canvas> planned = plan(engineCanvasMode(mode));
    if (planned.hasValue())
    {
        return std::pair<Canvas, bool>{*planned, false};
    }
    if (mode != ImageIntegration::RegistrationCanvasMode::Intersection ||
        planned.status().code() != an::ErrorCode::EmptySelection)
    {
        return planned.status();
    }
    planned = plan(engineCanvasMode(ImageIntegration::RegistrationCanvasMode::Standard));
    if (!planned.hasValue())
    {
        return planned.status();
    }
    return std::pair<Canvas, bool>{*planned, true};
}

} // namespace

an::RegistrationCanvasMode engineCanvasMode(const ImageIntegration::RegistrationCanvasMode mode) noexcept
{
    switch (mode)
    {
    case ImageIntegration::RegistrationCanvasMode::Mosaic:
        return an::RegistrationCanvasMode::Union;
    case ImageIntegration::RegistrationCanvasMode::Intersection:
        return an::RegistrationCanvasMode::Intersection;
    case ImageIntegration::RegistrationCanvasMode::Standard:
        break;
    }
    return an::RegistrationCanvasMode::Reference;
}

an::Result<PlannedRegistrationOutputPlane>
planRegistrationOutputPlane(const std::span<const RegistrationOutputPlaneFrame> frames,
                            const std::size_t referenceIndex, const ImageIntegration::RegistrationCanvasMode mode)
{
    const an::Result<std::vector<an::RegistrationBounds>> bounds = boundsOf(frames, referenceIndex);
    if (!bounds.hasValue())
    {
        return bounds.status();
    }
    const auto reference = static_cast<std::uint32_t>(referenceIndex);
    const an::Result<std::pair<an::RegistrationCanvas, bool>> planned =
        planWithStandardStandIn<an::RegistrationCanvas>(mode, [&](const an::RegistrationCanvasMode engineMode) {
            return an::planRegistrationCanvas(*bounds, reference, engineMode);
        });
    if (!planned.hasValue())
    {
        return planned.status();
    }
    return PlannedRegistrationOutputPlane{
        .canvas = planned->first,
        .usedMode = planned->second ? ImageIntegration::RegistrationCanvasMode::Standard : mode,
        .intersectionReplaced = planned->second};
}

an::Result<PlannedDrizzleCanvas> planDrizzleOutputPlane(const std::span<const RegistrationOutputPlaneFrame> frames,
                                                        const std::size_t referenceIndex,
                                                        const ImageIntegration::RegistrationCanvasMode mode,
                                                        const double scale, const double pixelFraction)
{
    const an::Result<std::vector<an::RegistrationBounds>> bounds = boundsOf(frames, referenceIndex);
    if (!bounds.hasValue())
    {
        return bounds.status();
    }
    const auto reference = static_cast<std::uint32_t>(referenceIndex);
    const an::Result<std::pair<an::DrizzleCanvas, bool>> planned =
        planWithStandardStandIn<an::DrizzleCanvas>(mode, [&](const an::RegistrationCanvasMode engineMode) {
            return an::planDrizzleCanvas(*bounds, reference,
                                         {.mode = engineMode, .scale = scale, .pixelFraction = pixelFraction});
        });
    if (!planned.hasValue())
    {
        return planned.status();
    }
    return PlannedDrizzleCanvas{.canvas = planned->first,
                                .usedMode = planned->second ? ImageIntegration::RegistrationCanvasMode::Standard : mode,
                                .intersectionReplaced = planned->second};
}

QString intersectionReplacedWarning(const QString& groupLabel)
{
    return QStringLiteral("No area is common to every light of %1, so its intersection canvas is replaced by the "
                          "standard reference-frame canvas.")
        .arg(groupLabel);
}

an::Result<an::WcsSolution> registrationOutputPlaneSolution(const an::WcsSolution& reference,
                                                            const an::RegistrationCanvas& canvas,
                                                            const bool referenceDistortionUndone)
{
    const an::Result<an::WcsSolution> moved = an::translateWcsSolution(reference, canvas.origin.x, canvas.origin.y);
    if (!moved.hasValue())
    {
        return moved.status();
    }
    an::WcsSolutionRequest request = moved->toRequest();
    request.imageWidth = canvas.width;
    request.imageHeight = canvas.height;
    if (referenceDistortionUndone)
    {
        request.distortion = an::SipDistortion{};
    }
    return an::WcsSolution::create(request);
}

std::optional<RegistrationOutputPlaneExpectations>
computeRegistrationOutputPlaneExpectations(const std::span<const RegistrationOutputPlaneFrame> frames,
                                           const std::size_t referenceIndex,
                                           const ImageIntegration::RegistrationCanvasMode mode, QString& error)
{
    const an::Result<PlannedRegistrationOutputPlane> plane = planRegistrationOutputPlane(frames, referenceIndex, mode);
    if (!plane.hasValue())
    {
        error = QString::fromUtf8(plane.status().message());
        return std::nullopt;
    }
    error.clear();
    const bool distorted = std::any_of(frames.begin(), frames.end(), [](const RegistrationOutputPlaneFrame& frame) {
        return frame.distortion.has_value();
    });
    return RegistrationOutputPlaneExpectations{.width = static_cast<int>(plane->canvas.width),
                                               .height = static_cast<int>(plane->canvas.height),
                                               .sipCorrectionApplied = distorted};
}

} // namespace stage_utils
