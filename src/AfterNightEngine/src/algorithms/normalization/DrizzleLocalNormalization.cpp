// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the grid of a local normalization measured on a drizzle canvas's analysis plane, and where the
// drizzled frames lie on that plane.
//
// The shape is the canvas's analysis extent with the caller's grid size and channels, checked by the
// same rule every other grid is. The plane mapping is the scale with no offset under the finite-only
// rule `DrizzledStackFrameSource` states, refused for a canvas that does not lie on its analysis plane
// at that scale. Nothing here measures or allocates.

#include <AfterNightEngine/Algorithms/Normalization/DrizzleLocalNormalization.h>

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <cmath>

namespace an
{
namespace
{

constexpr const char* kPlaneOperation = "an::drizzleLocalNormalizationPlane";

/// The drizzle model's enlargement range.
constexpr double kMinimumScale = 1.0;
constexpr double kMaximumScale = 4.0;

/// A refusal of the plane mapping naming the parameter at fault.
///
/// @param code why.
/// @param message what is wrong, for a person.
/// @param parameter the parameter's spelling.
/// @return the failure.
[[nodiscard]] Status planeFailure(ErrorCode code, const char* message, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = kPlaneOperation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Whether one axis of a canvas lies on its analysis plane at a scale: its last pixel begins before the plane's far
/// edge, enlarged. Its first pixel always ends past the near edge, because the canvas starts where the plane does.
///
/// @param canvasExtent the canvas's pixels along the axis.
/// @param planeExtent the analysis plane's pixels along the axis.
/// @param scale the enlargement.
/// @return true when the axis lies on the plane.
[[nodiscard]] bool liesOnPlane(std::uint32_t canvasExtent, std::uint32_t planeExtent, double scale) noexcept
{
    return static_cast<double>(canvasExtent) - 1.0 < static_cast<double>(planeExtent) * scale;
}

} // namespace

Result<LocalNormalizationGridShape> drizzleLocalNormalizationShape(const DrizzleCanvas& canvas, std::uint32_t gridSize,
                                                                   std::uint32_t channelCount)
{
    const LocalNormalizationGridShape shape{.gridSize = gridSize,
                                            .width = canvas.analysisWidth,
                                            .height = canvas.analysisHeight,
                                            .channelCount = channelCount};
    const Status checked = checkLocalNormalizationGridShape(shape);
    if (checked.failed())
    {
        return checked;
    }
    return shape;
}

Result<StackPlaneMapping> drizzleLocalNormalizationPlane(const DrizzleCanvas& canvas, double scale)
{
    if (!std::isfinite(scale) || scale < kMinimumScale || scale > kMaximumScale)
    {
        return planeFailure(ErrorCode::InvalidArgument, "the scale must be finite and between one and four", "scale");
    }
    if (canvas.width == 0u || canvas.height == 0u || canvas.analysisWidth == 0u || canvas.analysisHeight == 0u)
    {
        return planeFailure(ErrorCode::InvalidArgument, "the canvas has no extent or no analysis plane", "canvas");
    }
    if (!liesOnPlane(canvas.width, canvas.analysisWidth, scale) ||
        !liesOnPlane(canvas.height, canvas.analysisHeight, scale))
    {
        return planeFailure(ErrorCode::IncompatibleArguments,
                            "a pixel of the canvas lies wholly past its analysis plane enlarged by the scale",
                            "canvas");
    }
    return StackPlaneMapping{.scale = scale, .offsetX = 0.0, .offsetY = 0.0, .frameValidity = ValidityRule::FiniteOnly};
}

} // namespace an
