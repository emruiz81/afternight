// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the output plane a drizzled set is written on, its guard and its analysis plane.
//
// The registration canvas planner selects the rectangle - the reference frame's, the union or the
// intersection - and checks the bounds it is given; this file only rounds. An intersection's corners
// move inward to whole reference pixels before anything is enlarged, and its drizzled extent rounds
// down where every other canvas rounds up, which is the product's arithmetic in the three stages that
// each carried a copy of it.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleCanvas.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace an
{
namespace
{

constexpr const char* kPlanOperation = "an::planDrizzleCanvas";
constexpr const char* kPlaneOperation = "an::planDrizzleCanvasOnPlane";

/// The drizzle model's enlargement range.
constexpr double kMinimumScale = 1.0;
constexpr double kMaximumScale = 4.0;

/// How far above one a scale has to be before it enlarges, as the product decided.
constexpr double kUnitScaleTolerance = 1.0e-6;

/// What one drop's half width adds to the guard, per drop fraction.
constexpr double kGuardDropHalfWidth = 0.5;

[[nodiscard]] Status canvasFailure(ErrorCode code, const char* message, const char* operation, const char* parameter)
{
    return Status::failure(code,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

[[nodiscard]] bool usableScale(double scale) noexcept
{
    return std::isfinite(scale) && scale >= kMinimumScale && scale <= kMaximumScale;
}

[[nodiscard]] bool usablePixelFraction(double pixelFraction) noexcept
{
    return std::isfinite(pixelFraction) && pixelFraction > 0.0 && pixelFraction <= 1.0;
}

/// Whether a rectangle is finite and not inverted.
[[nodiscard]] bool usableRectangle(const RegistrationBounds& rectangle) noexcept
{
    return std::isfinite(rectangle.minimumX) && std::isfinite(rectangle.minimumY) &&
           std::isfinite(rectangle.maximumX) && std::isfinite(rectangle.maximumY) &&
           rectangle.maximumX >= rectangle.minimumX && rectangle.maximumY >= rectangle.minimumY;
}

/// A whole, positive pixel count already known to be below the extent ceiling.
[[nodiscard]] std::uint32_t pixels(double value) noexcept
{
    return value < 1.0 ? 1u : static_cast<std::uint32_t>(value);
}

/// The record every canvas shares: the rectangle, its origin and its analysis plane.
[[nodiscard]] DrizzleCanvas canvasOver(const RegistrationBounds& rectangle, bool intersection) noexcept
{
    DrizzleCanvas canvas;
    canvas.boundingBox = rectangle;
    canvas.origin = PixelCoordinate{.x = -rectangle.minimumX, .y = -rectangle.minimumY};
    const double width = rectangle.width();
    const double height = rectangle.height();
    canvas.analysisWidth = pixels(intersection ? std::floor(width) : std::ceil(width));
    canvas.analysisHeight = pixels(intersection ? std::floor(height) : std::ceil(height));
    return canvas;
}

} // namespace

std::uint32_t drizzleIntersectionGuard(double scale, double pixelFraction) noexcept
{
    if (!std::isfinite(scale) || scale <= kMinimumScale + kUnitScaleTolerance)
    {
        return 0u;
    }
    double fraction = std::isfinite(pixelFraction) ? pixelFraction : 1.0;
    fraction = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
    const double guard = std::ceil(scale * (1.0 + kGuardDropHalfWidth * fraction));
    // A scale past the drizzle model's range is refused before any canvas is planned; held here
    // only so that the conversion below is defined for every finite argument.
    const double ceiling = static_cast<double>(kMaximumRegistrationCanvasExtent);
    return (std::max)(1u, static_cast<std::uint32_t>(guard < ceiling ? guard : ceiling));
}

Result<DrizzleCanvas> planDrizzleCanvas(std::span<const RegistrationBounds> bounds, std::uint32_t referenceIndex,
                                        const DrizzleCanvasRequest& request)
{
    if (!usableScale(request.scale))
    {
        return canvasFailure(ErrorCode::InvalidArgument, "the scale must be finite and between one and four",
                             kPlanOperation, "request.scale");
    }
    if (!usablePixelFraction(request.pixelFraction))
    {
        return canvasFailure(ErrorCode::InvalidArgument, "the drop fraction must be finite, above zero and at most one",
                             kPlanOperation, "request.pixelFraction");
    }
    // The registration planner selects the rectangle and refuses an empty set, an unknown mode, a
    // reference outside the set, a malformed rectangle, a selection with no area and one past the
    // extent ceiling.
    const Result<RegistrationCanvas> registration = planRegistrationCanvas(bounds, referenceIndex, request.mode);
    if (!registration.hasValue())
    {
        return registration.status();
    }
    const bool intersection = request.mode == RegistrationCanvasMode::Intersection;
    RegistrationBounds rectangle = registration->boundingBox;
    if (intersection)
    {
        rectangle.minimumX = std::ceil(rectangle.minimumX);
        rectangle.minimumY = std::ceil(rectangle.minimumY);
        rectangle.maximumX = (std::max)(rectangle.minimumX, std::floor(rectangle.maximumX));
        rectangle.maximumY = (std::max)(rectangle.minimumY, std::floor(rectangle.maximumY));
    }

    DrizzleCanvas canvas = canvasOver(rectangle, intersection);
    double width = 0.0;
    double height = 0.0;
    std::uint32_t guard = 0u;
    if (intersection)
    {
        // An intersection guards its edges where the scale enlarges, and loses one pixel where it
        // does not.
        guard = drizzleIntersectionGuard(request.scale, request.pixelFraction);
        const double shortened = guard > 0u ? 0.0 : 1.0;
        width = std::floor(rectangle.width() * request.scale) - shortened;
        height = std::floor(rectangle.height() * request.scale) - shortened;
    }
    else
    {
        width = std::ceil(rectangle.width() * request.scale);
        height = std::ceil(rectangle.height() * request.scale);
    }
    if (!(width >= 1.0) || !(height >= 1.0))
    {
        return canvasFailure(ErrorCode::EmptySelection, "the selected frames share no whole output pixel",
                             kPlanOperation, "bounds");
    }
    canvas.width = static_cast<std::uint32_t>(width);
    canvas.height = static_cast<std::uint32_t>(height);
    // A canvas too small to keep its guard at both edges keeps none, as the product skipped the crop.
    const std::uint64_t kept = 2u * static_cast<std::uint64_t>(guard);
    canvas.edgeGuard = canvas.width > kept && canvas.height > kept ? guard : 0u;
    canvas.outputWidth = canvas.width - 2u * canvas.edgeGuard;
    canvas.outputHeight = canvas.height - 2u * canvas.edgeGuard;
    return canvas;
}

Result<DrizzleCanvas> planDrizzleCanvasOnPlane(const RegistrationBounds& plane, double scale)
{
    if (!usableScale(scale))
    {
        return canvasFailure(ErrorCode::InvalidArgument, "the scale must be finite and between one and four",
                             kPlaneOperation, "scale");
    }
    if (!usableRectangle(plane) || !(plane.width() > 0.0) || !(plane.height() > 0.0))
    {
        return canvasFailure(ErrorCode::InvalidArgument,
                             "the plane must be finite, not inverted and of positive width and height", kPlaneOperation,
                             "plane");
    }
    const double ceiling = static_cast<double>(kMaximumRegistrationCanvasExtent);
    if (plane.width() > ceiling || plane.height() > ceiling)
    {
        return Status::failure(ErrorCode::LimitExceeded,
                               StatusDetail{.message = "the plane exceeds the canvas extent ceiling",
                                            .operation = kPlaneOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "plane"),
                                                        StatusContextField::ofCount(
                                                            ContextKey::Limit, kMaximumRegistrationCanvasExtent)}});
    }
    DrizzleCanvas canvas = canvasOver(plane, false);
    canvas.width = pixels(std::round(plane.width() * scale));
    canvas.height = pixels(std::round(plane.height() * scale));
    canvas.outputWidth = canvas.width;
    canvas.outputHeight = canvas.height;
    return canvas;
}

} // namespace an
