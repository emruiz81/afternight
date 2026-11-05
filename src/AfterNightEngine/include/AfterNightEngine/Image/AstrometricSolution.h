// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where an image is pointing, when something has worked that out.
///
/// This is the typed answer rather than a set of header cards. Reading `CRVAL`, `CRPIX`, `CD1_1` and the rest out of
/// a card bag whenever they are needed means every reader re-derives the same numbers, a card that disagrees with
/// another card is discovered late, and an image whose solution a crop has invalidated still carries cards that say
/// otherwise.
///
/// **Absent is a state, not a set of zeroes.** Most images have no solution. `present()` answers it
/// once, and every accessor of a solution that is absent returns the same nothing rather than a
/// plausible zero that a later computation would use.
///
/// **Scale and orientation are carried, not the matrix.** What a solved frame reports about itself -
/// where its reference pixel points, how many arcseconds a pixel covers, which way north is, and
/// whether the sky is mirrored - is enough to place, compare, and sanity-check a frame, and it is
/// the part that stays true when the full model behind it changes. The world-coordinate matrix and
/// the polynomial distortion model live in `Algorithms/Astrometry/WcsSolution.h`, which is what a
/// plate solve produces and what projects a pixel; `hasDistortion` records that such a model exists
/// for this frame without describing it, so an image that has one does not silently look like an
/// image that does not. The bridge runs one way: a full solution summarises itself into this one,
/// and this one cannot be turned back into a full solution.
///
/// **It is semantic.** A result computed under one solution and read under another is wrong, so this
/// object and `Image/ImageSemantics.h` are the two whose change moves an image's semantic
/// generation.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <optional>

namespace an
{

/// What a plate solve reports about a frame, before any of it has been checked.
///
/// A plain aggregate so a call site names each field rather than passing seven numbers positionally
/// and getting the reference pixel the wrong way round.
struct AstrometricSolutionRequest
{
    /// Where the reference pixel points. The right ascension must be finite and in the half-open
    /// range [0, 360) degrees and the declination finite and in [-90, 90]: an angle outside them is
    /// refused rather than normalised, because wrapping it would hide a solver's mistake.
    SkyCoordinate referencePoint;

    /// The column the reference point sits at, in pixels from the image origin. Must be finite. May
    /// be fractional and may sit outside the image, which is what a solution of a cropped frame
    /// looks like.
    double referencePixelX;

    /// The row the reference point sits at, in pixels from the image origin. Must be finite, and
    /// may sit outside the image for the same reason the column may.
    double referencePixelY;

    /// How much sky one pixel covers, in arcseconds. Must be positive and finite.
    double pixelScaleArcsecPerPixel;

    /// The angle from the image's vertical axis to celestial north, in degrees, measured
    /// counter-clockwise. Must be finite and in the half-open range [0, 360).
    double orientationDegrees;

    /// Whether the sky is mirrored in the image - an odd number of reflections in the optical path,
    /// or a flip applied without updating the solution. It is carried rather than folded into the
    /// orientation because the two are independent and a solver reports them separately.
    bool mirrored;

    /// Whether a polynomial distortion model was fitted for this frame. The model itself is not
    /// carried here; this says one exists, so nothing treats the frame as if the linear terms were
    /// the whole solution.
    bool hasDistortion;
};

/// Where an image is pointing, when something has worked that out.
///
/// Two states: absent, which is what almost every image has, and present, which carries a solution
/// whose numbers have been checked. There is no way to build a present one whose numbers are
/// impossible: the constructor is private and `create` is the only way in.
///
/// Trivially copyable and usable in a constant expression. Immutable: a solution is replaced, never
/// patched, so a change to it is visible as a change.
class AstrometricSolution
{
public:
    /// No solution.
    ///
    /// The default, and the correct state for an image nothing has solved.
    constexpr AstrometricSolution() noexcept = default;

    /// A solution with these numbers, when they describe a direction and a scale.
    ///
    /// @param request what the solver reported.
    /// @return the solution, or `ErrorCode::InvalidArgument` naming the first field whose value is
    ///         not a position on the sky, a scale, or an angle.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<AstrometricSolution>
    create(const AstrometricSolutionRequest& request);

    /// Whether this image has a solution at all.
    ///
    /// @return false for the default-constructed state.
    [[nodiscard]] constexpr bool present() const noexcept
    {
        return m_present;
    }

    /// Where the reference pixel points.
    ///
    /// @return the direction, or an empty optional when no solution is present.
    [[nodiscard]] constexpr std::optional<SkyCoordinate> referencePoint() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_referencePoint;
    }

    /// The column the reference point sits at.
    ///
    /// @return the pixel column, or an empty optional when no solution is present.
    [[nodiscard]] constexpr std::optional<double> referencePixelX() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_referencePixelX;
    }

    /// The row the reference point sits at.
    ///
    /// @return the pixel row, or an empty optional when no solution is present.
    [[nodiscard]] constexpr std::optional<double> referencePixelY() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_referencePixelY;
    }

    /// How much sky one pixel covers.
    ///
    /// @return arcseconds per pixel, or an empty optional when no solution is present.
    [[nodiscard]] constexpr std::optional<double> pixelScaleArcsecPerPixel() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_pixelScaleArcsecPerPixel;
    }

    /// Which way celestial north is.
    ///
    /// @return degrees counter-clockwise from the image's vertical axis, or an empty optional when
    ///         no solution is present.
    [[nodiscard]] constexpr std::optional<double> orientationDegrees() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_orientationDegrees;
    }

    /// Whether the sky is mirrored in this image.
    ///
    /// @return the parity, or an empty optional when no solution is present. An optional rather
    ///         than false, because "not mirrored" is a fact about a solved frame and an unsolved
    ///         one has no parity at all.
    [[nodiscard]] constexpr std::optional<bool> mirrored() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_mirrored;
    }

    /// Whether a polynomial distortion model was fitted for this frame.
    ///
    /// @return whether one exists, or an empty optional when no solution is present. The model
    ///         itself is not carried here.
    [[nodiscard]] constexpr std::optional<bool> hasDistortion() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return m_hasDistortion;
    }

    /// The request this solution was built from.
    ///
    /// @return every field, in a form `create` accepts, or an empty optional when no solution is
    ///         present.
    [[nodiscard]] constexpr std::optional<AstrometricSolutionRequest> toRequest() const noexcept
    {
        if (!m_present)
        {
            return std::nullopt;
        }
        return AstrometricSolutionRequest{.referencePoint = m_referencePoint,
                                          .referencePixelX = m_referencePixelX,
                                          .referencePixelY = m_referencePixelY,
                                          .pixelScaleArcsecPerPixel = m_pixelScaleArcsecPerPixel,
                                          .orientationDegrees = m_orientationDegrees,
                                          .mirrored = m_mirrored,
                                          .hasDistortion = m_hasDistortion};
    }

    /// Whether two solutions say the same thing.
    ///
    /// @param left one solution.
    /// @param right the other.
    /// @return true when both are absent, or both are present with every number equal.
    [[nodiscard]] friend constexpr bool operator==(const AstrometricSolution& left,
                                                   const AstrometricSolution& right) noexcept
    {
        if (left.m_present != right.m_present)
        {
            return false;
        }
        if (!left.m_present)
        {
            return true;
        }
        return left.m_referencePoint == right.m_referencePoint && left.m_referencePixelX == right.m_referencePixelX &&
               left.m_referencePixelY == right.m_referencePixelY &&
               left.m_pixelScaleArcsecPerPixel == right.m_pixelScaleArcsecPerPixel &&
               left.m_orientationDegrees == right.m_orientationDegrees && left.m_mirrored == right.m_mirrored &&
               left.m_hasDistortion == right.m_hasDistortion;
    }

private:
    /// Builds from a request whose numbers have already been checked.
    ///
    /// @param request the validated solution.
    constexpr explicit AstrometricSolution(const AstrometricSolutionRequest& request) noexcept
        : m_referencePoint(request.referencePoint), m_referencePixelX(request.referencePixelX),
          m_referencePixelY(request.referencePixelY), m_pixelScaleArcsecPerPixel(request.pixelScaleArcsecPerPixel),
          m_orientationDegrees(request.orientationDegrees), m_present(true), m_mirrored(request.mirrored),
          m_hasDistortion(request.hasDistortion)
    {
    }

    /// Where the reference pixel points. Meaningless when absent.
    SkyCoordinate m_referencePoint{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};

    /// The column the reference point sits at.
    double m_referencePixelX = 0.0;

    /// The row the reference point sits at.
    double m_referencePixelY = 0.0;

    /// Arcseconds of sky per pixel.
    double m_pixelScaleArcsecPerPixel = 0.0;

    /// Degrees counter-clockwise from the image's vertical axis to celestial north.
    double m_orientationDegrees = 0.0;

    /// Whether any of the numbers above mean anything.
    bool m_present = false;

    /// Whether the sky is mirrored in this image.
    bool m_mirrored = false;

    /// Whether a polynomial distortion model was fitted for this frame.
    bool m_hasDistortion = false;
};

} // namespace an
