// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The coordinate model one frame is registered to another by.
///
/// A registration transform is a linear map, optionally followed by a polynomial in the linear
/// map's own output space. That composition is what a plate with real field curvature needs. The two
/// parts are one immutable value with one kind, and every operation on it either answers or refuses,
/// so no composition silently loses a polynomial, no homography is inverted as if it were affine and
/// no inverse is labelled with a kind it does not have.
///
/// **One pixel convention, stated here.** Every position is an engine pixel coordinate: the centre
/// of the first pixel is (0.5, 0.5), which is what `PixelCoordinate`, `DetectedStar` and
/// `StarAlignmentPoint` already mean, so a detected star or a distortion-corrected position needs
/// no conversion before it is mapped. The sampling kernels put pixel centres at integers instead,
/// and that half pixel is added in one place - the warp in `Algorithms/Registration/RegistrationPlan.h`
/// - rather than at every call site.
///
/// **The polynomial is not a second transform.** It is evaluated on what the linear map produced,
/// in coordinates normalized by its own origin and scale, and its inverse is a Newton solve of that
/// composition rather than of either half. So a transform's kind is one value: three linear kinds,
/// or the polynomial order that follows the linear part.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <cstdint>
#include <span>

namespace an
{

/// What one registration transform is, in one value.
///
/// The three linear kinds are the models a star field is fitted to. The three polynomial kinds are
/// a linear model of some kind followed by a polynomial of that order, which is why they do not
/// name the linear part: the polynomial absorbs whatever the linear map left behind, and a
/// consumer that needs the linear half asks for the matrix.
enum class RegistrationTransformKind : std::uint8_t
{
    /// Uniform scale, rotation and translation, with no shear and no reflection.
    Similarity = 0u,

    /// A full two-dimensional affine map.
    Affine,

    /// A full projective homography.
    Projective,

    /// A linear map followed by a second-order polynomial correction.
    Polynomial2,

    /// A linear map followed by a third-order polynomial correction.
    Polynomial3,

    /// A linear map followed by a fourth-order polynomial correction.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Polynomial4
};

/// Whether a value names a transform kind this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the value to test.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isRegistrationTransformKind(RegistrationTransformKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RegistrationTransformKind::Polynomial4);
}

/// Whether a kind is a bare matrix with no polynomial after it.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the kind to test.
/// @return true for the three linear kinds and false for the three polynomial ones.
[[nodiscard]] constexpr bool isLinearRegistrationTransformKind(RegistrationTransformKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(RegistrationTransformKind::Projective);
}

/// The kind's own name, for a diagnostic field, a report or a log line.
///
/// Thread-safe and allocation-free.
///
/// @param kind the kind to name. A value outside the enumeration is named
///        `"UnknownRegistrationTransformKind"`.
/// @return a null-terminated literal compiled into this library, which outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* registrationTransformKindName(RegistrationTransformKind kind) noexcept;

/// The lowest polynomial order that corrects anything.
///
/// Orders zero and one are already expressed by the linear matrix, so a polynomial below two is a
/// correction that is not one.
inline constexpr std::uint32_t kMinimumRegistrationPolynomialOrder = 2u;

/// The highest polynomial order this engine fits or evaluates.
///
/// Above every order a star field of realistic density supports: a fourth-order fit already asks for
/// fifteen coefficients per axis.
inline constexpr std::uint32_t kMaximumRegistrationPolynomialOrder = 4u;

/// How many coefficients a polynomial of one order uses.
///
/// The terms of total degree at most `order`, which is the triangular count. Thread-safe,
/// allocation-free, and usable in a constant expression.
///
/// @param order the polynomial order.
/// @return the term count, or zero for an order this engine does not evaluate.
[[nodiscard]] constexpr std::uint32_t registrationPolynomialTermCount(const std::uint32_t order) noexcept
{
    return order > kMaximumRegistrationPolynomialOrder ? 0u : ((order + 1u) * (order + 2u)) / 2u;
}

/// The coefficient table's fixed length: the terms a fourth-order polynomial uses.
inline constexpr std::size_t kRegistrationPolynomialTerms =
    static_cast<std::size_t>(registrationPolynomialTermCount(kMaximumRegistrationPolynomialOrder));

/// The polynomial correction applied to a linear map's output.
///
/// A plain aggregate. Coefficients are ordered by ascending total degree and, within one degree, by
/// ascending vertical degree: `1, x, y, x^2, xy, y^2, ...`. Entries at or beyond the order's term
/// count take no part, and `RegistrationTransform::create` refuses a nonzero one there rather than
/// dropping it silently.
///
/// The polynomial is evaluated in normalized coordinates. A position is shifted by
/// `normalizationOrigin` and divided by `normalizationScale` before the terms are formed, and the
/// result is scaled and shifted back. That is what keeps a fourth power of a coordinate in the
/// thousands from dominating the conditioning of the fit that produced it.
struct RegistrationPolynomial
{
    /// The order: zero when there is no polynomial, otherwise in
    /// `[kMinimumRegistrationPolynomialOrder, kMaximumRegistrationPolynomialOrder]`.
    std::uint32_t order = 0u;

    /// The horizontal component's coefficients, in term order.
    std::array<double, kRegistrationPolynomialTerms> x{};

    /// The vertical component's coefficients, in term order.
    std::array<double, kRegistrationPolynomialTerms> y{};

    /// The position the normalization measures offsets from.
    PixelCoordinate normalizationOrigin{};

    /// The divisor each axis is normalized by. Both components are finite and nonzero.
    PixelCoordinate normalizationScale{1.0, 1.0};

    /// Whether two polynomials are the same order and the same numbers.
    ///
    /// Exact equality of every entry, including the ones the order excludes, which is what a
    /// round-trip comparison of a parsed or serialized model wants.
    ///
    /// @param left one polynomial.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationPolynomial& left,
                                                   const RegistrationPolynomial& right) = default;
};

/// The smallest linear determinant a transform may have.
///
/// Below it the matrix maps the plane onto a line to within double precision, so it has no inverse
/// and the coordinates it produces are not positions.
inline constexpr double kRegistrationSingularDeterminant = 1.0e-12;

/// What a solver, a fit or a stored record reports, before any of it is checked.
///
/// A plain aggregate so a call site names each part rather than passing a matrix and a polynomial
/// positionally and getting the direction wrong.
struct RegistrationTransformRequest
{
    /// Which linear model the matrix is meant to be. A polynomial does not change it: the kind a
    /// checked transform reports is derived from this and the polynomial's order together.
    RegistrationTransformKind linearKind = RegistrationTransformKind::Similarity;

    /// The row-major homogeneous matrix mapping a source position to a reference position.
    ///
    /// A similarity or affine matrix has `[6, 7, 8] == [0, 0, 1]`, which `create` requires rather
    /// than assumes, so a matrix carrying a projective row cannot be labelled affine and then be
    /// inverted as one.
    std::array<double, 9> sourceToReference{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// The polynomial correction, whose order is zero when there is none.
    RegistrationPolynomial polynomial{};
};

/// The checked coordinate model one frame is registered to another by.
///
/// Immutable and trivially copyable, so two threads map positions through one transform with no
/// lock and no lazily built state. The constructor is private and `create` is the only way in:
/// there is no way to hold one whose matrix is singular, whose coefficients are not finite, or
/// whose declared linear kind disagrees with the matrix it carries.
///
/// A default-constructed transform is the identity with no polynomial, which is the honest state
/// for the reference frame of a registration pass.
class RegistrationTransform
{
public:
    /// The identity: the transform of a frame registered to itself.
    constexpr RegistrationTransform() noexcept = default;

    /// A transform with this matrix and polynomial, when they describe one.
    ///
    /// @param request what the fit, the solver or the stored record reported.
    /// @return the model; `ErrorCode::InvalidArgument` for an unknown linear kind, a non-finite
    ///         matrix entry, coefficient, normalization origin or scale, a zero normalization
    ///         scale, a polynomial order that is neither zero nor within
    ///         `[kMinimumRegistrationPolynomialOrder, kMaximumRegistrationPolynomialOrder]`, a
    ///         nonzero coefficient beyond the order's term count, or a non-projective kind whose
    ///         matrix carries a projective row; or `ErrorCode::InvalidData` when the linear
    ///         determinant is not finite or is below `kRegistrationSingularDeterminant`.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<RegistrationTransform>
    create(const RegistrationTransformRequest& request);

    /// What this transform is, linear part and polynomial order together.
    ///
    /// @return the linear kind when there is no polynomial, otherwise the polynomial kind of its
    ///         order.
    [[nodiscard]] constexpr RegistrationTransformKind kind() const noexcept
    {
        if (m_polynomial.order == 0u)
        {
            return m_linearKind;
        }
        return static_cast<RegistrationTransformKind>(static_cast<std::uint8_t>(RegistrationTransformKind::Projective) +
                                                      static_cast<std::uint8_t>(m_polynomial.order) -
                                                      static_cast<std::uint8_t>(kMinimumRegistrationPolynomialOrder) +
                                                      1u);
    }

    /// Which linear model the matrix is, whatever follows it.
    ///
    /// @return the declared linear kind, which is always one of the three linear enumerators.
    [[nodiscard]] constexpr RegistrationTransformKind linearKind() const noexcept
    {
        return m_linearKind;
    }

    /// Whether this transform is a bare matrix.
    ///
    /// @return true when no polynomial follows the linear map.
    [[nodiscard]] constexpr bool isLinear() const noexcept
    {
        return m_polynomial.order == 0u;
    }

    /// The row-major homogeneous source-to-reference matrix.
    ///
    /// @return the nine coefficients, whose last row is `[0, 0, 1]` unless the kind is projective.
    [[nodiscard]] constexpr const std::array<double, 9>& matrix() const noexcept
    {
        return m_matrix;
    }

    /// The polynomial correction.
    ///
    /// @return the polynomial, whose order is zero when there is none.
    [[nodiscard]] constexpr const RegistrationPolynomial& polynomial() const noexcept
    {
        return m_polynomial;
    }

    /// The length the unit horizontal vector is mapped to.
    ///
    /// The norm of the matrix's first column, which is where the horizontal axis goes. The first
    /// *row* would be the gradient of the output's horizontal component rather than the image of an
    /// axis; the two agree for a similarity and disagree for anything sheared.
    ///
    /// @return the scale factor, ignoring any polynomial.
    [[nodiscard]] AFTERNIGHTENGINE_API double scaleX() const noexcept;

    /// The length the unit vertical vector is mapped to.
    ///
    /// The norm of the matrix's second column, for the reason `scaleX` states.
    ///
    /// @return the scale factor, ignoring any polynomial.
    [[nodiscard]] AFTERNIGHTENGINE_API double scaleY() const noexcept;

    /// The mean of the two axis scales, for a caller that wants one number.
    ///
    /// @return the arithmetic mean of `scaleX` and `scaleY`.
    [[nodiscard]] AFTERNIGHTENGINE_API double scale() const noexcept;

    /// Where the horizontal axis points after the map, clockwise from the axis itself.
    ///
    /// @return the angle in radians, in `(-pi, pi]`, ignoring any polynomial.
    [[nodiscard]] AFTERNIGHTENGINE_API double rotationRadians() const noexcept;

    /// The same angle in degrees.
    ///
    /// @return the angle in `(-180, 180]`.
    [[nodiscard]] AFTERNIGHTENGINE_API double rotationDegrees() const noexcept;

    /// Where the source origin lands, in reference pixels.
    ///
    /// @return the matrix's translation column, ignoring any polynomial.
    [[nodiscard]] constexpr PixelCoordinate translation() const noexcept
    {
        return PixelCoordinate{.x = m_matrix[2], .y = m_matrix[5]};
    }

    /// The determinant of the linear part.
    ///
    /// @return the two-by-two determinant, whose sign is the parity of the map: negative means the
    ///         frame is mirrored relative to the reference.
    [[nodiscard]] constexpr double linearDeterminant() const noexcept
    {
        return m_matrix[0] * m_matrix[4] - m_matrix[1] * m_matrix[3];
    }

    /// Every field, in a form `create` accepts.
    ///
    /// @return the request this transform was built from.
    [[nodiscard]] constexpr RegistrationTransformRequest toRequest() const
    {
        return RegistrationTransformRequest{
            .linearKind = m_linearKind, .sourceToReference = m_matrix, .polynomial = m_polynomial};
    }

    /// Whether two transforms say the same thing.
    ///
    /// @param left one transform.
    /// @param right the other.
    /// @return true when the linear kind, every matrix entry and the whole polynomial are equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationTransform& left,
                                                   const RegistrationTransform& right) = default;

private:
    /// Builds from a request whose numbers have already been checked.
    ///
    /// @param request the validated model.
    constexpr explicit RegistrationTransform(const RegistrationTransformRequest& request)
        : m_linearKind(request.linearKind), m_matrix(request.sourceToReference), m_polynomial(request.polynomial)
    {
    }

    /// Which linear model the matrix is.
    RegistrationTransformKind m_linearKind = RegistrationTransformKind::Similarity;

    /// The row-major homogeneous matrix.
    std::array<double, 9> m_matrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// The polynomial correction, absent when its order is zero.
    RegistrationPolynomial m_polynomial{};
};

/// Maps one source position to its reference position.
///
/// The linear matrix first, then the polynomial on what the matrix produced, then the translation
/// the caller supplied. That order is the contract: `postTranslation` moves the result onto an
/// output canvas whose origin is not the reference frame's, so it is applied after everything that
/// describes the frames' relationship and never folded into the matrix, where it would change what
/// the polynomial is evaluated on.
///
/// The arithmetic is scalar Float64. A fourth-order polynomial in coordinates of a few thousand
/// pixels reaches magnitudes a Float32 accumulation cannot hold apart, and the answer conditions
/// sub-pixel resampling.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param pixel the source position, in engine pixel coordinates.
/// @param postTranslation the offset added to the result, in reference pixels.
/// @return the reference position; `ErrorCode::InvalidArgument` when `pixel` or `postTranslation`
///         is not finite; or `ErrorCode::InvalidData` when the projective denominator vanishes or
///         a polynomial term overflows, which is a position this transform does not map rather
///         than a malformed call.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PixelCoordinate>
mapRegistrationPoint(const RegistrationTransform& transform, const PixelCoordinate& pixel,
                     const PixelCoordinate& postTranslation = {});

/// Maps a run of source positions to their reference positions in place.
///
/// The form of `mapRegistrationPoint` for a caller that maps many positions through one transform,
/// such as a coordinate map: the translation is checked once and the transform's constant work is
/// taken once, and each position then takes the single position's arithmetic in its order, so a
/// position that maps is written with exactly the bits `mapRegistrationPoint` returns for it. A
/// position that does not map - one that is not finite, meets a vanishing projective denominator,
/// or whose correction or translated result is not finite - is written as two quiet NaNs and
/// counted, rather than failing the run.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param postTranslation the offset added to every result, in reference pixels.
/// @param positions the source positions, each replaced by its reference position.
/// @return how many positions did not map; every one of them when `postTranslation` is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t mapRegistrationPoints(const RegistrationTransform& transform,
                                                                       const PixelCoordinate& postTranslation,
                                                                       std::span<PixelCoordinate> positions) noexcept;

/// Where an inverse solve starts from.
///
/// Only a polynomial transform has anything to solve: the linear inverse is exact and needs no
/// seed. The choice matters for a coordinate map, where thousands of neighbouring positions are
/// inverted in a row and the previous answer is a better starting point than the linear inverse of
/// the current one - as long as the traversal that produced it is not allowed to change the answer.
enum class RegistrationInverseSeed : std::uint8_t
{
    /// Start every solve from the linear inverse of the position being solved.
    ///
    /// The answer then depends on nothing but the position, so two runs at different thread counts
    /// and two maps of different tile shapes agree exactly.
    AuthoritativeSeed = 0u,

    /// Start from the previous position's answer along the same row, falling back to the
    /// authoritative seed when that solve does not converge.
    ///
    /// Cheaper per pixel and still deterministic, because a row is traversed in one direction by
    /// one worker and a warm solve is held to the same residual as a cold one before it is
    /// accepted.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RowNeighbourWarmStart
};

/// Whether a value names an inverse seed policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param seed the value to test.
/// @return true for both enumerators.
[[nodiscard]] constexpr bool isRegistrationInverseSeed(RegistrationInverseSeed seed) noexcept
{
    return static_cast<std::uint8_t>(seed) <= static_cast<std::uint8_t>(RegistrationInverseSeed::RowNeighbourWarmStart);
}

/// The seed policy's own name.
///
/// Thread-safe and allocation-free.
///
/// @param seed the policy to name. A value outside the enumeration is named
///        `"UnknownRegistrationInverseSeed"`.
/// @return a null-terminated literal compiled into this library, which outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* registrationInverseSeedName(RegistrationInverseSeed seed) noexcept;

/// The largest round-trip distance, in pixels, an accepted inverse solve may leave.
///
/// A hundredth of a hundredth of a pixel, far below any resampling this feeds and far above where a
/// double-precision polynomial stops improving.
inline constexpr double kRegistrationInverseTolerancePixels = 1.0e-4;

/// The most Newton steps one inverse solve takes before it is judged on its residual.
inline constexpr std::uint32_t kMaximumRegistrationNewtonIterations = 20u;

/// The round-trip distance a solve that ran out of steps may still be accepted at.
///
/// Five times the tolerance. A solve that reached the tolerance stops there; one that exhausted
/// its steps is judged on where it actually ended, because a position half a thousandth of a pixel
/// out is a usable answer and refusing it would leave a hole in a coordinate map for no reason a
/// reader could act on.
inline constexpr double kRegistrationInverseFinalTolerancePixels = 5.0 * kRegistrationInverseTolerancePixels;

/// One inverse solve and what it cost.
struct RegistrationInverseResult
{
    /// The source position, in engine pixel coordinates.
    PixelCoordinate pixel{};

    /// How many Newton steps ran. Zero for a linear transform, which is inverted exactly.
    std::uint32_t iterations = 0u;

    /// Whether a supplied warm seed produced the accepted answer.
    ///
    /// False for a linear transform, for a solve with no warm seed, and for one whose warm seed
    /// was abandoned for the authoritative one.
    bool warmSeedAccepted = false;

    /// The round-trip distance the forward map leaves at the answer, in pixels.
    ///
    /// Zero for a linear transform, whose inverse is exact rather than iterated.
    double residualPixels = 0.0;
};

/// Maps one reference position back to its source position.
///
/// The exact inverse of `mapRegistrationPoint`: the caller's translation is removed, the linear
/// matrix is inverted, and a polynomial - which has no closed-form inverse - is solved by Newton
/// iteration on the whole composition with an analytic Jacobian.
///
/// A warm seed is a hint and never an authority. A warm solve that does not converge is discarded
/// and the position is solved again from the linear inverse, and a warm solve that does converge
/// takes one further step before the residual rule accepts it, so its answer agrees with the cold
/// route to within the tolerance rather than merely satisfying it from a different direction.
///
/// What a warm route is held to is the residual, not the branch. A polynomial correction of order
/// two or above is not injective over the whole plane, so a solve started far from the answer may
/// land on a different preimage - one that inverts the map exactly as well. That is why the policy
/// exists for a neighbouring position's answer, which is within a pixel of the one being solved,
/// and is not offered as a general hint; and it is why a coordinate map restarts its seed at the
/// beginning of every row rather than carrying one across a row boundary.
///
/// The arithmetic is scalar Float64, for the reason `mapRegistrationPoint` states. Thread-safe,
/// reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param pixel the reference position, in engine pixel coordinates.
/// @param postTranslation the offset that was added to the forward result.
/// @param seed where the solve starts; a `RowNeighbourWarmStart` request with no `warmSeed` behaves
///        as `AuthoritativeSeed`.
/// @param warmSeed the previous answer, or null.
/// @return the source position and what the solve cost; `ErrorCode::InvalidArgument` for a
///         non-finite position, translation or warm seed, or an unknown seed policy;
///         `ErrorCode::InvalidData` when the linear inverse is singular at this position, a
///         Jacobian is singular, or the solve exhausts `kMaximumRegistrationNewtonIterations`
///         without reaching the tolerance - a position this transform does not invert rather than
///         a malformed call.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationInverseResult>
mapRegistrationPointInverse(const RegistrationTransform& transform, const PixelCoordinate& pixel,
                            const PixelCoordinate& postTranslation = {},
                            RegistrationInverseSeed seed = RegistrationInverseSeed::AuthoritativeSeed,
                            const PixelCoordinate* warmSeed = nullptr);

/// Composes two linear transforms into the map that applies `second` after `first`.
///
/// Linear only, and deliberately so. The composition of two polynomial corrections is a polynomial
/// of the product of their orders, which this engine neither fits nor evaluates, so it is refused by
/// name rather than answered with the linear parts alone.
///
/// The result's linear kind is the more general of the two - similarity within affine within
/// projective - so composing never claims fewer degrees of freedom than the map has.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param first the transform applied first.
/// @param second the transform applied to its result.
/// @return the composed transform; `ErrorCode::Unsupported` when either operand carries a
///         polynomial; or any refusal `RegistrationTransform::create` makes about the product,
///         a singular product among them.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationTransform>
composeRegistrationTransforms(const RegistrationTransform& first, const RegistrationTransform& second);

/// What the star-field matcher reports. Defined in `Algorithms/Stars/StarAlignment.h`.
///
/// Named here rather than included: the conversion below reads one, and a caller holding one has
/// already included the matcher's header, while a caller that only resamples should not acquire it.
struct StarTransform;

/// The registration a star-field match describes.
///
/// The matcher and everything that resamples a frame name one model twice: `alignStarFields` reports
/// a `StarTransform`, and the warp, the canvas planner and the drizzle take a `RegistrationTransform`.
/// Both hold the row-major source-to-reference matrix in engine pixel coordinates, whose first pixel
/// centre is one half, and their three linear models correspond one to one, so the matrix is kept and
/// the model names the linear kind. The result is checked like any other transform.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the match.
/// @return the registration; `ErrorCode::InvalidArgument` for a model outside the enumeration, or for
///         any other malformed matrix `RegistrationTransform::create` refuses; or
///         `ErrorCode::InvalidData` for a singular matrix.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationTransform>
registrationTransformOf(const StarTransform& transform);

/// Inverts a linear transform exactly.
///
/// The full three-by-three inverse, projective row included, with the linear kind preserved: the
/// inverse of a homography is a homography, and the inverse of an affine map is affine.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the transform to invert.
/// @return the inverse; `ErrorCode::Unsupported` when the transform carries a polynomial, whose
///         inverse exists only position by position through `mapRegistrationPointInverse`; or
///         `ErrorCode::InvalidData` when the matrix is singular.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationTransform>
invertRegistrationTransform(const RegistrationTransform& transform);

/// The axis-aligned rectangle a mapped frame occupies, in reference pixels.
struct RegistrationBounds
{
    /// Smallest mapped horizontal coordinate.
    double minimumX = 0.0;

    /// Smallest mapped vertical coordinate.
    double minimumY = 0.0;

    /// Largest mapped horizontal coordinate.
    double maximumX = 0.0;

    /// Largest mapped vertical coordinate.
    double maximumY = 0.0;

    /// The rectangle's width.
    ///
    /// @return the horizontal extent, which is never negative for a checked bounds.
    [[nodiscard]] constexpr double width() const noexcept
    {
        return maximumX - minimumX;
    }

    /// The rectangle's height.
    ///
    /// @return the vertical extent, which is never negative for a checked bounds.
    [[nodiscard]] constexpr double height() const noexcept
    {
        return maximumY - minimumY;
    }

    /// Whether two rectangles are the same four numbers.
    ///
    /// @param left one rectangle.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationBounds& left,
                                                   const RegistrationBounds& right) noexcept = default;
};

/// How many positions each edge of a frame is sampled at when its bounds are computed.
///
/// Two would be enough for a linear transform, whose image of a rectangle is a quadrilateral with
/// the corners as extremes; a projective or polynomial map bows its edges, and the bound has to be
/// taken from the curve rather than from its endpoints.
inline constexpr std::uint32_t kDefaultRegistrationBoundarySamples = 50u;

/// The largest boundary sample count one bounds computation accepts.
inline constexpr std::uint32_t kMaximumRegistrationBoundarySamples = 4096u;

/// The rectangle a source frame occupies after this transform.
///
/// The frame's boundary - the rectangle `[0, width] x [0, height]`, which is what the pixels
/// actually cover when the first pixel centre is at one half - is sampled at `boundarySamples`
/// positions along each edge and every sample is mapped. A source distortion, when one is supplied,
/// is corrected first: the transform was fitted between undistorted positions, so the distorted
/// frame's own edge is what has to be mapped.
///
/// The whole computation is refused if any sample fails to map, rather than reporting a rectangle
/// derived from the ones that did. A bound that silently omits a corner is worse than no bound: it
/// is used to size a canvas.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param width the source frame's width in pixels, at least one.
/// @param height the source frame's height in pixels, at least one.
/// @param postTranslation the offset added to every mapped position.
/// @param sourceDistortion the frame's own plate distortion, or null when it has none.
/// @param boundarySamples positions per edge, at least two and at most
///        `kMaximumRegistrationBoundarySamples`.
/// @return the rectangle; `ErrorCode::InvalidArgument` for a zero extent, a sample count outside
///         its range, or a non-finite translation; or `ErrorCode::InvalidData` when a boundary
///         position does not map.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationBounds>
computeRegistrationBounds(const RegistrationTransform& transform, std::uint32_t width, std::uint32_t height,
                          const PixelCoordinate& postTranslation = {}, const SipDistortion* sourceDistortion = nullptr,
                          std::uint32_t boundarySamples = kDefaultRegistrationBoundarySamples);

/// How many corners a rectangular frame has.
inline constexpr std::size_t kRegistrationFrameCornerCount = 4u;

/// Where a source frame's four corners land, in reference pixels.
///
/// The corners of the rectangle `[0, width] x [0, height]` - the region the pixels actually cover
/// when the first pixel centre is at one half - in clockwise order from the origin: `(0, 0)`,
/// `(width, 0)`, `(width, height)`, `(0, height)`. That order is the contract, because a consumer
/// treats the four positions as a polygon.
///
/// A source distortion, when one is supplied, is corrected first, for the reason
/// `computeRegistrationBounds` states.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param transform the checked model.
/// @param width the source frame's width in pixels, at least one.
/// @param height the source frame's height in pixels, at least one.
/// @param postTranslation the offset added to every mapped corner.
/// @param sourceDistortion the frame's own plate distortion, or null when it has none.
/// @return the four positions; `ErrorCode::InvalidArgument` for a zero extent or a non-finite
///         translation; or `ErrorCode::InvalidData` when a corner does not map.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::array<PixelCoordinate, kRegistrationFrameCornerCount>>
mapRegistrationFrameCorners(const RegistrationTransform& transform, std::uint32_t width, std::uint32_t height,
                            const PixelCoordinate& postTranslation = {},
                            const SipDistortion* sourceDistortion = nullptr);

} // namespace an
