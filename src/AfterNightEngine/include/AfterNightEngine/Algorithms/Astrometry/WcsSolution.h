// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The typed world-coordinate model of one solved frame.
///
/// The model is a value. It carries the reference pixel, where it points, the linear matrix and the
/// polynomial distortion, in one pixel convention stated once. It is immutable, so it needs no lock
/// and two threads may project through one solution at the same time, and every derived number - the
/// plate scale, the rotation, the field of view - comes from the one matrix it holds rather than from
/// a header handed to a third-party library. What a geometry operation does to a frame is a named
/// transform of the solution rather than an edit of its cards.
///
/// **Every pixel position here is an engine pixel coordinate**: the first pixel's centre is
/// `(0.5, 0.5)`, the convention of `DetectedStar` and `SipDistortion.h`. The FITS spelling, whose
/// `CRPIX` puts that centre at one, is converted once in `WcsCards.h`.
///
/// **The projection is stated, not assumed.** This build implements the tangent-plane projection,
/// which is what every plate solver in this domain writes. A header naming a different projection is
/// refused by name at the card bridge rather than silently read as a tangent plane.
///
/// **This is the full model; `Image/AstrometricSolution.h` is the summary that travels with an
/// image.** `toAstrometricSolution` is the one-way bridge between them: everything the summary
/// carries is derivable from this, and the matrix and the polynomial are not derivable from the
/// summary.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>

#include <cstdint>

namespace an
{

/// Which sky projection a world-coordinate solution uses.
enum class WcsProjection : std::uint8_t
{
    /// The tangent-plane projection, which FITS spells `TAN`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table. It is also the only
    /// one: a build that implements one projection and an enumeration that names one are the same
    /// statement, and adding a second projection is then a change to both rather than a discovery
    /// that some code assumed the first.
    Gnomonic
};

/// Whether a value names one of the projections this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param projection the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isWcsProjection(WcsProjection projection) noexcept
{
    return static_cast<std::uint8_t>(projection) <= static_cast<std::uint8_t>(WcsProjection::Gnomonic);
}

/// The projection's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param projection the projection to name. A value outside the enumeration is named
///        `"UnknownWcsProjection"`.
/// @return a null-terminated string literal compiled into this library, which outlives every caller
///         and is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* wcsProjectionName(WcsProjection projection) noexcept;

/// The linear part of a world-coordinate solution: degrees of sky per pixel step.
///
/// The convention's `CD` matrix. It maps an offset from the reference pixel to an offset on the
/// projection plane, in degrees, with the first output axis eastward at the reference point and the
/// second northward.
struct WcsLinearTransform
{
    /// Degrees of eastward plane offset per pixel of horizontal offset.
    double cd11 = 0.0;

    /// Degrees of eastward plane offset per pixel of vertical offset.
    double cd12 = 0.0;

    /// Degrees of northward plane offset per pixel of horizontal offset.
    double cd21 = 0.0;

    /// Degrees of northward plane offset per pixel of vertical offset.
    double cd22 = 0.0;

    /// The matrix determinant, in square degrees per square pixel.
    ///
    /// Its magnitude is the solid angle of one pixel and its sign is the frame's parity: positive
    /// means the sky is mirrored in the image.
    ///
    /// @return the determinant.
    [[nodiscard]] constexpr double determinant() const noexcept
    {
        return cd11 * cd22 - cd12 * cd21;
    }

    /// Whether two matrices are the same four numbers.
    ///
    /// @param left one matrix.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const WcsLinearTransform& left,
                                                   const WcsLinearTransform& right) noexcept = default;
};

/// How much sky a frame covers along each of its own axes.
struct WcsFieldOfView
{
    /// Extent along the image's horizontal axis, in arcminutes.
    double widthArcmin = 0.0;

    /// Extent along the image's vertical axis, in arcminutes.
    double heightArcmin = 0.0;

    /// Whether two extents are the same two numbers.
    ///
    /// @param left one extent.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const WcsFieldOfView& left,
                                                   const WcsFieldOfView& right) noexcept = default;
};

/// What a solver or a header reports about one frame, before any of it is checked.
///
/// A plain aggregate so a call site names each field rather than passing a reference pixel, a
/// direction and four matrix entries positionally.
struct WcsSolutionRequest
{
    /// The pixel the reference direction sits at, in engine pixel coordinates - the centre of the
    /// first pixel is (0.5, 0.5). May be fractional and may sit outside the image.
    PixelCoordinate referencePixel{};

    /// Where that pixel points.
    SkyCoordinate referencePoint{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};

    /// The linear pixel-to-plane matrix. Its determinant must not be zero.
    WcsLinearTransform linear{};

    /// Which projection carries the plane onto the sky.
    WcsProjection projection = WcsProjection::Gnomonic;

    /// Width of the frame this solution describes, in pixels. Must not be zero.
    std::uint32_t imageWidth = 0u;

    /// Height of the frame this solution describes, in pixels. Must not be zero.
    std::uint32_t imageHeight = 0u;

    /// The polynomial distortion, if any. A default-constructed one means a purely linear solution.
    SipDistortion distortion{};
};

/// Where one frame is pointing, in full.
///
/// Immutable and trivially copyable. There is no way to build one whose reference direction is not
/// on the sky, whose matrix cannot be inverted, or whose projection this build does not implement:
/// the constructor is private and `create` is the only way in.
///
/// Every transform is a new solution rather than an edit, so a solution that has been through a
/// crop is a different value from the one before it and a stale copy cannot look current.
class WcsSolution
{
public:
    /// A solution with these numbers, when they describe a frame on the sky.
    ///
    /// A distortion with a forward or inverse model must share the linear reference pixel;
    /// a mismatch returns `ErrorCode::IncompatibleArguments` before constructing the solution.
    /// @param request what the solver or the header reported.
    /// @return the solution; `ErrorCode::InvalidArgument` when the reference pixel is not finite,
    ///         the reference direction is not a finite in-range `SkyCoordinate`, a matrix entry is
    ///         not finite, or either image dimension is zero; `ErrorCode::Unsupported` when the
    ///         projection is not one this build implements; or `ErrorCode::InvalidData` when the
    ///         matrix determinant is zero or not finite, which is a solution that maps every pixel
    ///         to one direction.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<WcsSolution> create(const WcsSolutionRequest& request);

    /// The pixel the reference direction sits at.
    ///
    /// @return the reference pixel, in engine pixel coordinates.
    [[nodiscard]] constexpr PixelCoordinate referencePixel() const noexcept
    {
        return m_referencePixel;
    }

    /// Where the reference pixel points.
    ///
    /// @return the reference direction.
    [[nodiscard]] constexpr SkyCoordinate referencePoint() const noexcept
    {
        return m_referencePoint;
    }

    /// The linear pixel-to-plane matrix.
    ///
    /// @return the matrix.
    [[nodiscard]] constexpr WcsLinearTransform linear() const noexcept
    {
        return m_linear;
    }

    /// Which projection carries the plane onto the sky.
    ///
    /// @return the projection.
    [[nodiscard]] constexpr WcsProjection projection() const noexcept
    {
        return m_projection;
    }

    /// Width of the frame this solution describes.
    ///
    /// @return the width in pixels, which is never zero.
    [[nodiscard]] constexpr std::uint32_t imageWidth() const noexcept
    {
        return m_imageWidth;
    }

    /// Height of the frame this solution describes.
    ///
    /// @return the height in pixels, which is never zero.
    [[nodiscard]] constexpr std::uint32_t imageHeight() const noexcept
    {
        return m_imageHeight;
    }

    /// The polynomial distortion.
    ///
    /// @return the model, which corrects nothing when the solution is purely linear.
    [[nodiscard]] constexpr const SipDistortion& distortion() const noexcept
    {
        return m_distortion;
    }

    /// Every field, in a form `create` accepts.
    ///
    /// @return the request this solution was built from.
    [[nodiscard]] constexpr WcsSolutionRequest toRequest() const
    {
        return WcsSolutionRequest{.referencePixel = m_referencePixel,
                                  .referencePoint = m_referencePoint,
                                  .linear = m_linear,
                                  .projection = m_projection,
                                  .imageWidth = m_imageWidth,
                                  .imageHeight = m_imageHeight,
                                  .distortion = m_distortion};
    }

    /// The same solution with its polynomial distortion removed.
    ///
    /// The only way to drop a distortion model, and it is a caller's decision rather than something
    /// a transform does quietly. A frame whose distortion has been dropped is still solved to its
    /// linear accuracy, which is what a caller usually wants after an operation the polynomial
    /// cannot follow.
    ///
    /// @return the linear solution.
    [[nodiscard]] AFTERNIGHTENGINE_API WcsSolution withoutDistortion() const;

    /// Where one pixel points.
    ///
    /// Applies the forward distortion, the linear matrix and the projection, in that order.
    ///
    /// Scalar Float64 throughout, for the reason `applySipForward` states. Thread-safe, reentrant
    /// and allocation-free: a solution is immutable, so two threads may project through one.
    ///
    /// @param pixel the position, in engine pixel coordinates.
    /// @return the direction; `ErrorCode::InvalidArgument` when `pixel` is not finite; or
    ///         `ErrorCode::OutOfRange` when the distortion or the plane offset overflows.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCoordinate> pixelToWorld(const PixelCoordinate& pixel) const;

    /// Which pixel one direction falls on.
    ///
    /// Projects onto the plane, inverts the linear matrix and undoes the distortion, in that order.
    /// The distortion is undone by the model's inverse polynomials when it has them and by a Newton
    /// solve of its forward model when it does not.
    ///
    /// Scalar Float64 throughout. Thread-safe, reentrant and allocation-free.
    ///
    /// @param direction the direction to place.
    /// @return the position, in engine pixel coordinates, which may lie outside the frame;
    ///         `ErrorCode::InvalidArgument` when `direction` is not a finite in-range
    ///         `SkyCoordinate`; `ErrorCode::OutOfRange` when the direction is on or behind the
    ///         projection's horizon - ninety or more degrees from the reference point, to within
    ///         rounding - when it has no finite pixel position, or when the inverse distortion
    ///         polynomials overflow; or `ErrorCode::InvalidData` when a Newton solve of the
    ///         distortion does not converge there.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate> worldToPixel(const SkyCoordinate& direction) const;

    /// How much sky one pixel step along the image's horizontal axis covers.
    ///
    /// @return arcseconds per pixel, which is finite and positive.
    [[nodiscard]] AFTERNIGHTENGINE_API double pixelScaleXArcsecPerPixel() const noexcept;

    /// How much sky one pixel step along the image's vertical axis covers.
    ///
    /// @return arcseconds per pixel, which is finite and positive.
    [[nodiscard]] AFTERNIGHTENGINE_API double pixelScaleYArcsecPerPixel() const noexcept;

    /// The scale of a square pixel covering the same solid angle.
    ///
    /// The square root of the determinant's magnitude. Named for what it is rather than offered as
    /// "the" plate scale, because the two axis scales differ whenever a solution is anisotropic and
    /// a single number cannot be both.
    ///
    /// @return arcseconds per pixel, which is finite and positive.
    [[nodiscard]] AFTERNIGHTENGINE_API double meanPixelScaleArcsecPerPixel() const noexcept;

    /// Which way celestial north points in the image.
    ///
    /// The position angle of increasing declination at the reference pixel, measured from the
    /// direction of increasing y and turning toward decreasing x. Zero means north points toward
    /// increasing y - up the image when its first row is drawn at the bottom, as FITS draws it;
    /// ninety means north points toward decreasing x, to the left.
    ///
    /// @return degrees in the half-open range `[0, 360)`.
    [[nodiscard]] AFTERNIGHTENGINE_API double orientationDegrees() const noexcept;

    /// Whether the sky is mirrored in this image.
    ///
    /// True when the linear matrix has a positive determinant, which is the parity an odd number of
    /// reflections in the optical path produces.
    ///
    /// @return the parity.
    [[nodiscard]] AFTERNIGHTENGINE_API bool mirrored() const noexcept;

    /// How much sky the frame covers along each of its own axes.
    ///
    /// The axis scales multiplied by the frame's dimensions. A linear estimate: it ignores both the
    /// projection's own curvature and the polynomial distortion, which is what a catalogue query
    /// radius and a field summary want and is not a substitute for projecting the corners.
    ///
    /// @return the extents, in arcminutes.
    [[nodiscard]] AFTERNIGHTENGINE_API WcsFieldOfView fieldOfViewArcmin() const noexcept;

    /// Where the middle of the frame points.
    ///
    /// The direction at the frame's geometric centre, which in engine pixel coordinates is
    /// `(width / 2, height / 2)`.
    ///
    /// @return the direction, with the failures `pixelToWorld` returns.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCoordinate> fieldCenter() const;

    /// The radius of a cone that contains the whole frame.
    ///
    /// Half the field's diagonal plus the caller's margin. What a catalogue search needs in order
    /// not to miss a star near a corner.
    ///
    /// @param marginArcsec extra radius, in arcseconds - a match tolerance, usually. Must be finite
    ///        and must not be negative.
    /// @return the radius in degrees; `ErrorCode::InvalidArgument` when the margin is negative or
    ///         not finite; or `ErrorCode::OutOfRange` when the radius is not finite.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<double> queryRadiusDegrees(double marginArcsec) const;

    /// The summary of this solution that travels with an image.
    ///
    /// The one-way bridge to `Image/AstrometricSolution.h`. The summary carries the reference point
    /// and pixel, the square-pixel scale, the orientation, the parity, and whether a distortion
    /// model exists - not the matrix and not the polynomial, which is why an image's metadata
    /// cannot be turned back into a full solution.
    ///
    /// The reference pixel crosses in engine pixel coordinates, unchanged.
    ///
    /// @return the summary; `ErrorCode::OutOfRange` when the square-pixel scale is not positive and
    ///         finite; or whatever `AstrometricSolution::create` refused.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<AstrometricSolution> toAstrometricSolution() const;

    /// Whether two solutions say the same thing.
    ///
    /// @param left one solution.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend constexpr bool operator==(const WcsSolution& left, const WcsSolution& right) = default;

private:
    /// Builds from a request whose numbers have already been checked.
    ///
    /// @param request the validated solution.
    constexpr explicit WcsSolution(const WcsSolutionRequest& request)
        : m_referencePixel(request.referencePixel), m_referencePoint(request.referencePoint), m_linear(request.linear),
          m_distortion(request.distortion), m_imageWidth(request.imageWidth), m_imageHeight(request.imageHeight),
          m_projection(request.projection)
    {
    }

    /// The pixel the reference direction sits at.
    PixelCoordinate m_referencePixel{};

    /// Where that pixel points.
    SkyCoordinate m_referencePoint{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};

    /// The linear pixel-to-plane matrix.
    WcsLinearTransform m_linear{};

    /// The polynomial distortion.
    SipDistortion m_distortion{};

    /// Width of the frame, in pixels.
    std::uint32_t m_imageWidth = 0u;

    /// Height of the frame, in pixels.
    std::uint32_t m_imageHeight = 0u;

    /// Which projection carries the plane onto the sky.
    WcsProjection m_projection = WcsProjection::Gnomonic;
};

/// The solution of a frame after a rectangular region of it is kept.
///
/// A crop moves the origin and nothing else: the matrix, the projection and the polynomial are
/// unchanged, and the reference pixel and the distortion's reference pixel both move with the new
/// origin: each loses `(originX, originY)`, which carries the centre of the first kept pixel,
/// `(originX + 0.5, originY + 0.5)`, to the new frame's `(0.5, 0.5)`. The reference pixel routinely
/// ends up outside the kept region, which is correct and is why nothing here clamps it.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param solution the solution of the frame being cropped.
/// @param originX first kept column, in pixels from the original origin.
/// @param originY first kept row, in pixels from the original origin.
/// @param width width of the kept region, in pixels. Must not be zero.
/// @param height height of the kept region, in pixels. Must not be zero.
/// @return the cropped frame's solution; `ErrorCode::InvalidArgument` for a zero dimension; or
///         `ErrorCode::OutOfRange` when the kept region is not inside the original frame or a moved
///         reference pixel is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WcsSolution> cropWcsSolution(const WcsSolution& solution,
                                                                       std::uint32_t originX, std::uint32_t originY,
                                                                       std::uint32_t width, std::uint32_t height);

/// The solution of a frame whose contents have been moved within an unchanged canvas.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param solution the solution of the frame being translated.
/// @param dx how far the contents moved along the horizontal axis, in pixels.
/// @param dy how far the contents moved along the vertical axis, in pixels.
/// @return the translated frame's solution; `ErrorCode::InvalidArgument` when either offset is not
///         finite or the shifted reference pixel is not.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WcsSolution> translateWcsSolution(const WcsSolution& solution, double dx,
                                                                            double dy);

/// The solution of a frame that has been resampled to a different size.
///
/// The reference pixel moves with the sampling grid - multiplied by the scale, which keeps the
/// frame's origin edge fixed because pixel edges sit at whole numbers in this convention - the
/// matrix is divided by the scale, and the polynomial coefficients are rescaled term by term -
/// a coefficient of total degree `d` by the scale raised to `1 - d`, which is what keeps the
/// correction the same fraction of the new pixel.
///
/// That rescaling is exact only when both axes scale together, and a resize that keeps the aspect
/// ratio rarely does exactly: each side of its result is a whole number of pixels, so its two scales
/// differ by up to about one pixel's share of the result's shorter side. The polynomial is therefore
/// carried while the two scales agree within a thousandth of the larger - which every aspect-locked
/// resize to a thousand pixels or more on each side does - its coefficients rescaled by the mean of
/// the two scales and its reference pixel moved on each axis with the linear one. A resize whose
/// scales differ by more is refused rather than served with a polynomial that no longer describes the
/// frame; the caller drops the distortion with `WcsSolution::withoutDistortion` if the linear
/// solution is enough.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param solution the solution of the frame being resized.
/// @param scaleX how many new pixels one old pixel became along the horizontal axis. Must be finite
///        and positive.
/// @param scaleY the same along the vertical axis.
/// @param width width of the resized frame, in pixels. Must not be zero.
/// @param height height of the resized frame, in pixels. Must not be zero.
/// @return the resized frame's solution; `ErrorCode::InvalidArgument` for a scale that is not
///         finite and positive or a zero dimension; `ErrorCode::Unsupported` when the two scales
///         differ by more than a thousandth of the larger and the solution carries a polynomial
///         distortion; or `ErrorCode::OutOfRange` when a rescaled coefficient is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WcsSolution>
resizeWcsSolution(const WcsSolution& solution, double scaleX, double scaleY, std::uint32_t width, std::uint32_t height);

/// The solution of a frame that has been rotated about its centre onto a new canvas.
///
/// The matrix absorbs the rotation and the reference pixel moves with the canvas. A rotation is a
/// linear change of pixel basis, so a linear solution survives it exactly.
///
/// Carrying a polynomial distortion into the rotated basis is representable and not implemented, so
/// this operation refuses a distorted solution rather than dropping the model silently. The caller
/// either re-solves the rotated frame or asks for the linear solution explicitly with
/// `WcsSolution::withoutDistortion`.
///
/// Thread-safe, reentrant and allocation-free.
///
/// @param solution the solution of the frame being rotated.
/// @param angleDegrees how far the contents were turned about the old frame's centre, which lands
///        on the new canvas's centre. A positive angle turns the direction of increasing x toward
///        increasing y - counter-clockwise when the first row is drawn at the bottom. Must be
///        finite.
/// @param width width of the new canvas, in pixels. Must not be zero.
/// @param height height of the new canvas, in pixels. Must not be zero.
/// @return the rotated frame's solution; `ErrorCode::InvalidArgument` for an angle that is not
///         finite or a zero dimension; or `ErrorCode::Unsupported` when the solution carries a
///         polynomial distortion.
[[nodiscard]] AFTERNIGHTENGINE_API Result<WcsSolution>
rotateWcsSolution(const WcsSolution& solution, double angleDegrees, std::uint32_t width, std::uint32_t height);

} // namespace an
