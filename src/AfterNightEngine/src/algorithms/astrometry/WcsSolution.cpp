// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the immutable world-coordinate model and the transforms that move it.

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>

#include <AfterNightEngine/Algorithms/Astrometry/GnomonicProjection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>

namespace an
{
namespace
{

constexpr const char* kCreateOperation = "an::WcsSolution::create";
constexpr const char* kPixelToWorldOperation = "an::WcsSolution::pixelToWorld";
constexpr const char* kWorldToPixelOperation = "an::WcsSolution::worldToPixel";
constexpr const char* kQueryRadiusOperation = "an::WcsSolution::queryRadiusDegrees";
constexpr const char* kSummaryOperation = "an::WcsSolution::toAstrometricSolution";
constexpr const char* kCropOperation = "an::cropWcsSolution";
constexpr const char* kTranslateOperation = "an::translateWcsSolution";
constexpr const char* kResizeOperation = "an::resizeWcsSolution";
constexpr const char* kRotateOperation = "an::rotateWcsSolution";

constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
constexpr double kDegreesPerRadian = 180.0 / std::numbers::pi;
constexpr double kArcsecondsPerDegree = 3600.0;
constexpr double kArcminutesPerDegree = 60.0;

/// How far apart a resize's two axis scales may lie, as a fraction of the larger, for it to carry a
/// polynomial distortion rescaled by their mean.
///
/// A resize that keeps the aspect ratio rounds each side of its result to whole pixels, so its two
/// scales differ by up to about one pixel's share of the result's shorter side - inside this
/// tolerance once each side is a thousand pixels or more - and rescaling by their mean moves each
/// coefficient by a fraction of that order.
constexpr double kDistortionScaleTolerance = 1.0e-3;

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status solutionFailure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Folds an angle into one turn.
///
/// @param degrees any finite angle.
/// @return the same direction in the half-open range `[0, 360)`.
[[nodiscard]] double foldDegrees(const double degrees) noexcept
{
    double folded = std::fmod(degrees, 360.0);
    if (folded < 0.0)
    {
        folded += 360.0;
    }
    if (folded >= 360.0)
    {
        folded = 0.0;
    }
    // A negative zero compares equal to zero and prints as a different number, so the fold ends at
    // one spelling of the direction it means.
    return folded == 0.0 ? 0.0 : folded;
}

/// Moves both reference pixels of a solution by the same offset.
///
/// A translation of the sampling grid moves the linear reference pixel and the distortion's
/// reference pixel together; letting them drift apart would make the polynomial describe a bend
/// centred somewhere the matrix does not agree with.
///
/// @param request the solution being rebuilt.
/// @param dx how far to move along the horizontal axis, in pixels.
/// @param dy how far to move along the vertical axis, in pixels.
/// @return false when either reference pixel leaves the finite range.
[[nodiscard]] bool shiftReferencePixels(WcsSolutionRequest& request, const double dx, const double dy)
{
    request.referencePixel.x += dx;
    request.referencePixel.y += dy;

    SipDistortionRequest distortion = request.distortion.toRequest();
    distortion.referencePixel.x += dx;
    distortion.referencePixel.y += dy;
    Result<SipDistortion> moved = SipDistortion::create(distortion);
    if (!moved.hasValue())
    {
        return false;
    }
    request.distortion = *moved;
    return std::isfinite(request.referencePixel.x) && std::isfinite(request.referencePixel.y);
}

} // namespace

const char* wcsProjectionName(const WcsProjection projection) noexcept
{
    switch (projection)
    {
    case WcsProjection::Gnomonic:
        return "Gnomonic";
    }
    return "UnknownWcsProjection";
}

Result<WcsSolution> WcsSolution::create(const WcsSolutionRequest& request)
{
    if (!std::isfinite(request.referencePixel.x) || !std::isfinite(request.referencePixel.y))
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a solution's reference pixel must be two finite numbers",
                               kCreateOperation);
    }
    if (!isSkyCoordinate(request.referencePoint))
    {
        return solutionFailure(ErrorCode::InvalidArgument,
                               "a solution's reference point must be a finite direction with right ascension in "
                               "[0, 360) and declination in [-90, 90]",
                               kCreateOperation);
    }
    if (!isWcsProjection(request.projection))
    {
        return solutionFailure(ErrorCode::Unsupported, "that projection is not one this build implements",
                               kCreateOperation);
    }
    if (request.imageWidth == 0u || request.imageHeight == 0u)
    {
        return solutionFailure(ErrorCode::InvalidArgument,
                               "a solution describes a frame with a nonzero width and "
                               "height",
                               kCreateOperation);
    }
    if (!std::isfinite(request.linear.cd11) || !std::isfinite(request.linear.cd12) ||
        !std::isfinite(request.linear.cd21) || !std::isfinite(request.linear.cd22))
    {
        return solutionFailure(ErrorCode::InvalidArgument, "every entry of a solution's linear matrix must be finite",
                               kCreateOperation);
    }

    const double determinant = request.linear.determinant();
    if (!std::isfinite(determinant) || determinant == 0.0)
    {
        return solutionFailure(ErrorCode::InvalidData,
                               "a solution's linear matrix cannot be inverted, so it maps the whole frame to one "
                               "direction",
                               kCreateOperation);
    }

    if ((request.distortion.hasForwardModel() || request.distortion.hasInverseModel()) &&
        (request.distortion.referencePixel().x != request.referencePixel.x ||
         request.distortion.referencePixel().y != request.referencePixel.y))
    {
        return solutionFailure(ErrorCode::IncompatibleArguments,
                               "the distortion and the linear solution must share a reference pixel", kCreateOperation);
    }
    return WcsSolution(request);
}

WcsSolution WcsSolution::withoutDistortion() const
{
    WcsSolutionRequest request = toRequest();
    request.distortion = SipDistortion{};
    // Every field was validated when this solution was built and removing a distortion cannot
    // invalidate one, so the only way this can fail is a defect here.
    Result<WcsSolution> linear = WcsSolution::create(request);
    return linear.hasValue() ? *linear : *this;
}

Result<SkyCoordinate> WcsSolution::pixelToWorld(const PixelCoordinate& pixel) const
{
    // The distortion's own failures are returned unchanged: the operation they name is the one
    // that actually refused, and restating it here would hide which of the two steps it was.
    const Result<PixelCoordinate> corrected = applySipForward(m_distortion, pixel);
    if (!corrected.hasValue())
    {
        return corrected.status();
    }

    const double u = corrected->x - m_referencePixel.x;
    const double v = corrected->y - m_referencePixel.y;
    const GnomonicCoordinate plane{.xDegrees = m_linear.cd11 * u + m_linear.cd12 * v,
                                   .yDegrees = m_linear.cd21 * u + m_linear.cd22 * v};
    if (!std::isfinite(plane.xDegrees) || !std::isfinite(plane.yDegrees))
    {
        return solutionFailure(ErrorCode::OutOfRange, "that pixel has no finite offset on the projection plane",
                               kPixelToWorldOperation);
    }

    return inverseGnomonic(plane, m_referencePoint);
}

Result<PixelCoordinate> WcsSolution::worldToPixel(const SkyCoordinate& direction) const
{
    const Result<GnomonicCoordinate> plane = projectGnomonic(direction, m_referencePoint);
    if (!plane.hasValue())
    {
        return plane.status();
    }

    const double determinant = m_linear.determinant();
    const double u = (m_linear.cd22 * plane->xDegrees - m_linear.cd12 * plane->yDegrees) / determinant;
    const double v = (-m_linear.cd21 * plane->xDegrees + m_linear.cd11 * plane->yDegrees) / determinant;
    if (!std::isfinite(u) || !std::isfinite(v))
    {
        return solutionFailure(ErrorCode::OutOfRange, "that direction has no finite position in the frame's pixel grid",
                               kWorldToPixelOperation);
    }

    const PixelCoordinate undistorted{.x = u + m_referencePixel.x, .y = v + m_referencePixel.y};
    const Result<SipInverseResult> corrected = applySipInverse(m_distortion, undistorted);
    if (!corrected.hasValue())
    {
        return corrected.status();
    }
    return corrected->pixel;
}

double WcsSolution::pixelScaleXArcsecPerPixel() const noexcept
{
    return std::hypot(m_linear.cd11, m_linear.cd21) * kArcsecondsPerDegree;
}

double WcsSolution::pixelScaleYArcsecPerPixel() const noexcept
{
    return std::hypot(m_linear.cd12, m_linear.cd22) * kArcsecondsPerDegree;
}

double WcsSolution::meanPixelScaleArcsecPerPixel() const noexcept
{
    return std::sqrt(std::abs(m_linear.determinant())) * kArcsecondsPerDegree;
}

double WcsSolution::orientationDegrees() const noexcept
{
    // Celestial north in pixel coordinates is the second column of the inverted matrix, which is
    // (-cd12, cd11) divided by the determinant. Dividing rather than cancelling matters: the
    // determinant's sign is the frame's parity, and dropping it would report a mirrored frame's
    // north pointing the opposite way.
    const double determinant = m_linear.determinant();
    const double northX = -m_linear.cd12 / determinant;
    const double northY = m_linear.cd11 / determinant;
    return foldDegrees(std::atan2(-northX, northY) * kDegreesPerRadian);
}

bool WcsSolution::mirrored() const noexcept
{
    return m_linear.determinant() > 0.0;
}

WcsFieldOfView WcsSolution::fieldOfViewArcmin() const noexcept
{
    return WcsFieldOfView{
        .widthArcmin = pixelScaleXArcsecPerPixel() * static_cast<double>(m_imageWidth) / kArcminutesPerDegree,
        .heightArcmin = pixelScaleYArcsecPerPixel() * static_cast<double>(m_imageHeight) / kArcminutesPerDegree};
}

Result<SkyCoordinate> WcsSolution::fieldCenter() const
{
    return pixelToWorld(
        PixelCoordinate{.x = static_cast<double>(m_imageWidth) * 0.5, .y = static_cast<double>(m_imageHeight) * 0.5});
}

Result<double> WcsSolution::queryRadiusDegrees(const double marginArcsec) const
{
    if (!std::isfinite(marginArcsec) || marginArcsec < 0.0)
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a query margin must be finite and must not be negative",
                               kQueryRadiusOperation);
    }

    const WcsFieldOfView field = fieldOfViewArcmin();
    const double halfDiagonalDegrees = std::hypot(field.widthArcmin, field.heightArcmin) / (2.0 * kArcminutesPerDegree);
    const double radius = halfDiagonalDegrees + (marginArcsec / kArcsecondsPerDegree);
    if (!std::isfinite(radius))
    {
        return solutionFailure(ErrorCode::OutOfRange, "the frame's own extent has no finite query radius",
                               kQueryRadiusOperation);
    }
    return radius;
}

Result<AstrometricSolution> WcsSolution::toAstrometricSolution() const
{
    const double scale = meanPixelScaleArcsecPerPixel();
    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return solutionFailure(ErrorCode::OutOfRange, "the solution's plate scale is not a positive finite number",
                               kSummaryOperation);
    }

    return AstrometricSolution::create(
        AstrometricSolutionRequest{.referencePoint = m_referencePoint,
                                   .referencePixelX = m_referencePixel.x,
                                   .referencePixelY = m_referencePixel.y,
                                   .pixelScaleArcsecPerPixel = scale,
                                   .orientationDegrees = orientationDegrees(),
                                   .mirrored = mirrored(),
                                   .hasDistortion = m_distortion.hasForwardModel() || m_distortion.hasInverseModel()});
}

Result<WcsSolution> cropWcsSolution(const WcsSolution& solution, const std::uint32_t originX,
                                    const std::uint32_t originY, const std::uint32_t width, const std::uint32_t height)
{
    if (width == 0u || height == 0u)
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a crop keeps a region with a nonzero width and height",
                               kCropOperation);
    }
    if (width > solution.imageWidth() || height > solution.imageHeight() || originX > solution.imageWidth() - width ||
        originY > solution.imageHeight() - height)
    {
        return solutionFailure(ErrorCode::OutOfRange, "the kept region is not inside the frame this solution describes",
                               kCropOperation);
    }

    WcsSolutionRequest request = solution.toRequest();
    request.imageWidth = width;
    request.imageHeight = height;
    if (!shiftReferencePixels(request, -static_cast<double>(originX), -static_cast<double>(originY)))
    {
        return solutionFailure(ErrorCode::OutOfRange, "the cropped frame's reference pixel is not a finite number",
                               kCropOperation);
    }
    return WcsSolution::create(request);
}

Result<WcsSolution> translateWcsSolution(const WcsSolution& solution, const double dx, const double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy))
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a translation moves a frame by two finite offsets",
                               kTranslateOperation);
    }

    WcsSolutionRequest request = solution.toRequest();
    if (!shiftReferencePixels(request, dx, dy))
    {
        return solutionFailure(ErrorCode::InvalidArgument,
                               "the translated frame's reference pixel is not a finite number", kTranslateOperation);
    }
    return WcsSolution::create(request);
}

Result<WcsSolution> resizeWcsSolution(const WcsSolution& solution, const double scaleX, const double scaleY,
                                      const std::uint32_t width, const std::uint32_t height)
{
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.0 || scaleY <= 0.0)
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a resize scales each axis by a finite positive factor",
                               kResizeOperation);
    }
    if (width == 0u || height == 0u)
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a resize produces a frame with a nonzero width and height",
                               kResizeOperation);
    }

    const bool distorted = solution.distortion().hasForwardModel() || solution.distortion().hasInverseModel();
    if (distorted && std::abs(scaleX - scaleY) / (std::max)(scaleX, scaleY) > kDistortionScaleTolerance)
    {
        return solutionFailure(ErrorCode::Unsupported,
                               "a polynomial distortion is rescaled only when both axes scale together, within a "
                               "thousandth of the larger scale; drop the distortion explicitly if the linear solution "
                               "is enough",
                               kResizeOperation);
    }

    WcsSolutionRequest request = solution.toRequest();
    request.imageWidth = width;
    request.imageHeight = height;
    request.referencePixel.x = solution.referencePixel().x * scaleX;
    request.referencePixel.y = solution.referencePixel().y * scaleY;
    request.linear.cd11 = solution.linear().cd11 / scaleX;
    request.linear.cd21 = solution.linear().cd21 / scaleX;
    request.linear.cd12 = solution.linear().cd12 / scaleY;
    request.linear.cd22 = solution.linear().cd22 / scaleY;

    if (distorted)
    {
        // The polynomial's reference pixel moves with the grid on each axis, as the linear one does, and every
        // coefficient is rescaled by the mean of the two scales, which is either of them when they are equal. Halving
        // before the sum is the same mean without overflowing at the top of the range.
        const double meanScale = scaleX * 0.5 + scaleY * 0.5;
        SipDistortionRequest distortion = solution.distortion().toRequest();
        distortion.referencePixel.x *= scaleX;
        distortion.referencePixel.y *= scaleY;
        SipPolynomial* const components[] = {&distortion.forwardX, &distortion.forwardY, &distortion.inverseX,
                                             &distortion.inverseY};
        for (SipPolynomial* const component : components)
        {
            for (std::size_t i = 0u; i < kSipCoefficientRowCount; ++i)
            {
                for (std::size_t j = 0u; j < kSipCoefficientRowCount; ++j)
                {
                    if (component->coefficients[i][j] == 0.0)
                    {
                        continue;
                    }
                    component->coefficients[i][j] *=
                        std::pow(meanScale, 1.0 - static_cast<double>(i) - static_cast<double>(j));
                }
            }
        }
        Result<SipDistortion> rescaled = SipDistortion::create(distortion);
        if (!rescaled.hasValue())
        {
            return solutionFailure(ErrorCode::OutOfRange,
                                   "the rescaled distortion polynomial has a coefficient that is not finite",
                                   kResizeOperation);
        }
        request.distortion = *rescaled;
    }

    return WcsSolution::create(request);
}

Result<WcsSolution> rotateWcsSolution(const WcsSolution& solution, const double angleDegrees, const std::uint32_t width,
                                      const std::uint32_t height)
{
    if (!std::isfinite(angleDegrees))
    {
        return solutionFailure(ErrorCode::InvalidArgument, "a rotation turns a frame by a finite angle",
                               kRotateOperation);
    }
    if (width == 0u || height == 0u)
    {
        return solutionFailure(ErrorCode::InvalidArgument,
                               "a rotation produces a canvas with a nonzero width and height", kRotateOperation);
    }
    if (solution.distortion().hasForwardModel() || solution.distortion().hasInverseModel())
    {
        return solutionFailure(ErrorCode::Unsupported,
                               "a polynomial distortion is not carried through a rotation; re-solve the rotated frame "
                               "or drop the distortion explicitly",
                               kRotateOperation);
    }

    const double angle = angleDegrees * kRadiansPerDegree;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double oldCenterX = static_cast<double>(solution.imageWidth()) * 0.5;
    const double oldCenterY = static_cast<double>(solution.imageHeight()) * 0.5;
    const double newCenterX = static_cast<double>(width) * 0.5;
    const double newCenterY = static_cast<double>(height) * 0.5;
    const double offsetX = solution.referencePixel().x - oldCenterX;
    const double offsetY = solution.referencePixel().y - oldCenterY;

    WcsSolutionRequest request = solution.toRequest();
    request.imageWidth = width;
    request.imageHeight = height;
    request.referencePixel.x = cosine * offsetX - sine * offsetY + newCenterX;
    request.referencePixel.y = sine * offsetX + cosine * offsetY + newCenterY;
    // The matrix absorbs the inverse rotation, because it now consumes offsets measured in the
    // rotated grid and must undo that rotation before it means what it meant before.
    request.linear.cd11 = solution.linear().cd11 * cosine - solution.linear().cd12 * sine;
    request.linear.cd12 = solution.linear().cd11 * sine + solution.linear().cd12 * cosine;
    request.linear.cd21 = solution.linear().cd21 * cosine - solution.linear().cd22 * sine;
    request.linear.cd22 = solution.linear().cd21 * sine + solution.linear().cd22 * cosine;
    return WcsSolution::create(request);
}

} // namespace an
