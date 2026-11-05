// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the registration transform, its mapping, its inverse and its bounds.
//
// Positions are engine pixel coordinates, whose first pixel centre is (0.5, 0.5), so a frame's
// boundary - the corners and the edges the bounds sample - runs from 0 to its width and from 0 to
// its height: the outer edges of the edge pixels, not their centres. The per-position arithmetic
// is shared with the coordinate maps through RegistrationInternal.h; the entry points here add the
// argument checks and the typed failures around it.

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>

#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include "algorithms/registration/RegistrationInternal.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>

namespace an
{
namespace
{

constexpr const char* kCreateOperation = "an::RegistrationTransform::create";
constexpr const char* kMapOperation = "an::mapRegistrationPoint";
constexpr const char* kInverseOperation = "an::mapRegistrationPointInverse";
constexpr const char* kComposeOperation = "an::composeRegistrationTransforms";
constexpr const char* kInvertOperation = "an::invertRegistrationTransform";
constexpr const char* kBoundsOperation = "an::computeRegistrationBounds";
constexpr const char* kCornersOperation = "an::mapRegistrationFrameCorners";

// The arithmetic these entry points check for and wrap is shared with the coordinate map, which
// calls it once per pixel from another translation unit, so it is defined in the domain header.
using registration_internal::applyLinear;
using registration_internal::evaluatePolynomial;
using registration_internal::inverseNormalizationScale;
using registration_internal::invertLinear;
using registration_internal::isFinitePixel;
using registration_internal::PolynomialValue;
using registration_internal::solveNewton;

/// Builds a registration-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status transformFailure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Checks one polynomial component of a request.
///
/// @param polynomial the correction to check.
/// @return the failure, or success when it describes a correction or none at all.
[[nodiscard]] Status checkPolynomial(const RegistrationPolynomial& polynomial)
{
    if (polynomial.order != 0u && (polynomial.order < kMinimumRegistrationPolynomialOrder ||
                                   polynomial.order > kMaximumRegistrationPolynomialOrder))
    {
        return transformFailure(ErrorCode::InvalidArgument, "a polynomial correction is absent or of order two to four",
                                kCreateOperation);
    }
    if (!isFinitePixel(polynomial.normalizationOrigin) || !isFinitePixel(polynomial.normalizationScale))
    {
        return transformFailure(ErrorCode::InvalidArgument, "a polynomial's normalization is not finite",
                                kCreateOperation);
    }
    if (polynomial.normalizationScale.x == 0.0 || polynomial.normalizationScale.y == 0.0)
    {
        return transformFailure(ErrorCode::InvalidArgument, "a polynomial's normalization scale divides by zero",
                                kCreateOperation);
    }
    const std::size_t active = static_cast<std::size_t>(registrationPolynomialTermCount(polynomial.order));
    for (std::size_t index = 0u; index < kRegistrationPolynomialTerms; ++index)
    {
        if (!std::isfinite(polynomial.x[index]) || !std::isfinite(polynomial.y[index]))
        {
            return transformFailure(ErrorCode::InvalidArgument, "a polynomial coefficient is not finite",
                                    kCreateOperation);
        }
        if (polynomial.order != 0u && index >= active && (polynomial.x[index] != 0.0 || polynomial.y[index] != 0.0))
        {
            return transformFailure(ErrorCode::InvalidArgument,
                                    "a polynomial carries a nonzero coefficient of a degree its order excludes",
                                    kCreateOperation);
        }
    }
    if (polynomial.order == 0u)
    {
        for (std::size_t index = 0u; index < kRegistrationPolynomialTerms; ++index)
        {
            if (polynomial.x[index] != 0.0 || polynomial.y[index] != 0.0)
            {
                return transformFailure(ErrorCode::InvalidArgument,
                                        "an absent polynomial carries a nonzero coefficient", kCreateOperation);
            }
        }
    }
    return Status::success();
}

/// The more general of two linear kinds.
///
/// @param left one kind.
/// @param right the other.
/// @return projective when either is, affine when either is, and similarity otherwise.
[[nodiscard]] RegistrationTransformKind moreGeneralKind(const RegistrationTransformKind left,
                                                        const RegistrationTransformKind right) noexcept
{
    if (left == RegistrationTransformKind::Projective || right == RegistrationTransformKind::Projective)
    {
        return RegistrationTransformKind::Projective;
    }
    if (left == RegistrationTransformKind::Affine || right == RegistrationTransformKind::Affine)
    {
        return RegistrationTransformKind::Affine;
    }
    return RegistrationTransformKind::Similarity;
}

/// Maps one frame-boundary position, correcting a source distortion first.
///
/// @param transform the model.
/// @param distortion the frame's plate distortion, or null.
/// @param position the boundary position, in engine pixel coordinates.
/// @param postTranslation the offset added to the result.
/// @param out the mapped position.
/// @return false when the distortion or the transform does not map the position.
[[nodiscard]] bool mapBoundaryPosition(const RegistrationTransform& transform, const SipDistortion* distortion,
                                       const PixelCoordinate& position, const PixelCoordinate& postTranslation,
                                       PixelCoordinate& out)
{
    PixelCoordinate corrected = position;
    if (distortion != nullptr)
    {
        const Result<PixelCoordinate> forward = applySipForward(*distortion, position);
        if (!forward.hasValue())
        {
            return false;
        }
        corrected = *forward;
    }
    const Result<PixelCoordinate> mapped = mapRegistrationPoint(transform, corrected, postTranslation);
    if (!mapped.hasValue())
    {
        return false;
    }
    out = *mapped;
    return true;
}

} // namespace

const char* registrationTransformKindName(const RegistrationTransformKind kind) noexcept
{
    switch (kind)
    {
    case RegistrationTransformKind::Similarity:
        return "Similarity";
    case RegistrationTransformKind::Affine:
        return "Affine";
    case RegistrationTransformKind::Projective:
        return "Projective";
    case RegistrationTransformKind::Polynomial2:
        return "Polynomial2";
    case RegistrationTransformKind::Polynomial3:
        return "Polynomial3";
    case RegistrationTransformKind::Polynomial4:
        return "Polynomial4";
    }
    return "UnknownRegistrationTransformKind";
}

const char* registrationInverseSeedName(const RegistrationInverseSeed seed) noexcept
{
    switch (seed)
    {
    case RegistrationInverseSeed::AuthoritativeSeed:
        return "AuthoritativeSeed";
    case RegistrationInverseSeed::RowNeighbourWarmStart:
        return "RowNeighbourWarmStart";
    }
    return "UnknownRegistrationInverseSeed";
}

Result<RegistrationTransform> RegistrationTransform::create(const RegistrationTransformRequest& request)
{
    if (!isLinearRegistrationTransformKind(request.linearKind))
    {
        return transformFailure(ErrorCode::InvalidArgument,
                                "a transform's linear kind is one of similarity, affine or projective",
                                kCreateOperation);
    }
    for (const double coefficient : request.sourceToReference)
    {
        if (!std::isfinite(coefficient))
        {
            return transformFailure(ErrorCode::InvalidArgument, "a transform matrix coefficient is not finite",
                                    kCreateOperation);
        }
    }
    if (request.linearKind != RegistrationTransformKind::Projective &&
        (request.sourceToReference[6] != 0.0 || request.sourceToReference[7] != 0.0 ||
         request.sourceToReference[8] != 1.0))
    {
        return transformFailure(ErrorCode::InvalidArgument,
                                "a similarity or affine transform's last matrix row is exactly zero, zero, one",
                                kCreateOperation);
    }
    if (const Status checked = checkPolynomial(request.polynomial); !checked.ok())
    {
        return checked;
    }

    const double determinant = request.sourceToReference[0] * request.sourceToReference[4] -
                               request.sourceToReference[1] * request.sourceToReference[3];
    if (!std::isfinite(determinant) || std::abs(determinant) < kRegistrationSingularDeterminant)
    {
        return transformFailure(ErrorCode::InvalidData,
                                "a transform whose linear determinant vanishes maps the plane onto a line",
                                kCreateOperation);
    }
    return RegistrationTransform{request};
}

double RegistrationTransform::scaleX() const noexcept
{
    return std::hypot(m_matrix[0], m_matrix[3]);
}

double RegistrationTransform::scaleY() const noexcept
{
    return std::hypot(m_matrix[1], m_matrix[4]);
}

double RegistrationTransform::scale() const noexcept
{
    return (scaleX() + scaleY()) / 2.0;
}

double RegistrationTransform::rotationRadians() const noexcept
{
    return std::atan2(m_matrix[3], m_matrix[0]);
}

double RegistrationTransform::rotationDegrees() const noexcept
{
    return rotationRadians() * (180.0 / std::numbers::pi);
}

Result<PixelCoordinate> mapRegistrationPoint(const RegistrationTransform& transform, const PixelCoordinate& pixel,
                                             const PixelCoordinate& postTranslation)
{
    if (!isFinitePixel(pixel) || !isFinitePixel(postTranslation))
    {
        return transformFailure(ErrorCode::InvalidArgument, "a mapped position and its translation are finite",
                                kMapOperation);
    }
    PixelCoordinate mapped{};
    if (!applyLinear(transform.matrix(), transform.linearKind() == RegistrationTransformKind::Projective, pixel,
                     mapped))
    {
        return transformFailure(ErrorCode::InvalidData, "this transform does not map that position", kMapOperation);
    }
    if (!transform.isLinear())
    {
        PolynomialValue value;
        if (!evaluatePolynomial(transform.polynomial(), mapped, false, value))
        {
            return transformFailure(ErrorCode::InvalidData, "the polynomial correction overflows at that position",
                                    kMapOperation);
        }
        mapped = value.pixel;
    }
    mapped.x += postTranslation.x;
    mapped.y += postTranslation.y;
    if (!isFinitePixel(mapped))
    {
        return transformFailure(ErrorCode::InvalidData, "the translated position is not finite", kMapOperation);
    }
    return mapped;
}

std::uint64_t mapRegistrationPoints(const RegistrationTransform& transform, const PixelCoordinate& postTranslation,
                                    std::span<PixelCoordinate> positions) noexcept
{
    constexpr double kHole = std::numeric_limits<double>::quiet_NaN();
    if (!isFinitePixel(postTranslation))
    {
        std::fill(positions.begin(), positions.end(), PixelCoordinate{.x = kHole, .y = kHole});
        return positions.size();
    }
    const std::array<double, 9>& matrix = transform.matrix();
    const bool projective = transform.linearKind() == RegistrationTransformKind::Projective;
    const bool linear = transform.isLinear();
    const RegistrationPolynomial& polynomial = transform.polynomial();
    const PixelCoordinate inverseScale = linear ? PixelCoordinate{} : inverseNormalizationScale(polynomial);
    std::uint64_t unmapped = 0u;
    for (PixelCoordinate& position : positions)
    {
        // The checks and the arithmetic of `mapRegistrationPoint`, in its order.
        PixelCoordinate mapped{};
        bool valid = isFinitePixel(position) && applyLinear(matrix, projective, position, mapped);
        if (valid && !linear)
        {
            PolynomialValue value;
            valid = evaluatePolynomial(polynomial, inverseScale, mapped, false, value);
            mapped = value.pixel;
        }
        mapped.x += postTranslation.x;
        mapped.y += postTranslation.y;
        valid = valid && isFinitePixel(mapped);
        position = valid ? mapped : PixelCoordinate{.x = kHole, .y = kHole};
        unmapped += valid ? 0u : 1u;
    }
    return unmapped;
}

Result<RegistrationInverseResult> mapRegistrationPointInverse(const RegistrationTransform& transform,
                                                              const PixelCoordinate& pixel,
                                                              const PixelCoordinate& postTranslation,
                                                              const RegistrationInverseSeed seed,
                                                              const PixelCoordinate* warmSeed)
{
    if (!isRegistrationInverseSeed(seed))
    {
        return transformFailure(ErrorCode::InvalidArgument, "an inverse solve names a seed policy this build has",
                                kInverseOperation);
    }
    if (!isFinitePixel(pixel) || !isFinitePixel(postTranslation) || (warmSeed != nullptr && !isFinitePixel(*warmSeed)))
    {
        return transformFailure(ErrorCode::InvalidArgument,
                                "an inverted position, its translation and any warm seed are finite",
                                kInverseOperation);
    }

    const registration_internal::PreparedRegistrationInverse prepared =
        registration_internal::prepareRegistrationInverse(transform);
    if (!prepared.usable)
    {
        return transformFailure(ErrorCode::InvalidData, "this transform's linear part has no inverse",
                                kInverseOperation);
    }
    RegistrationInverseResult result;
    if (!registration_internal::invertWithPreparedInverse(transform, prepared, pixel, postTranslation,
                                                          seed == RegistrationInverseSeed::RowNeighbourWarmStart,
                                                          warmSeed, result))
    {
        return transformFailure(ErrorCode::InvalidData, "this transform does not invert at that position",
                                kInverseOperation);
    }
    return result;
}

Result<RegistrationTransform> composeRegistrationTransforms(const RegistrationTransform& first,
                                                            const RegistrationTransform& second)
{
    if (!first.isLinear() || !second.isLinear())
    {
        return transformFailure(ErrorCode::Unsupported,
                                "composing a polynomial correction with another transform is not a polynomial "
                                "correction this engine fits or evaluates",
                                kComposeOperation);
    }
    RegistrationTransformRequest request;
    request.linearKind = moreGeneralKind(first.linearKind(), second.linearKind());
    for (std::size_t row = 0u; row < 3u; ++row)
    {
        for (std::size_t column = 0u; column < 3u; ++column)
        {
            double sum = 0.0;
            for (std::size_t inner = 0u; inner < 3u; ++inner)
            {
                sum += second.matrix()[row * 3u + inner] * first.matrix()[inner * 3u + column];
            }
            request.sourceToReference[row * 3u + column] = sum;
        }
    }
    if (request.linearKind != RegistrationTransformKind::Projective)
    {
        request.sourceToReference[6] = 0.0;
        request.sourceToReference[7] = 0.0;
        request.sourceToReference[8] = 1.0;
    }
    return RegistrationTransform::create(request);
}

Result<RegistrationTransform> registrationTransformOf(const StarTransform& transform)
{
    if (!isStarTransformModel(transform.model))
    {
        return Status::failure(ErrorCode::InvalidArgument,
                               StatusDetail{.message = "a star-field match names a model this build does not know",
                                            .operation = "an::registrationTransformOf",
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofText(ContextKey::Parameter, "model")}});
    }
    RegistrationTransformRequest request;
    switch (transform.model)
    {
    case StarTransformModel::Similarity:
        request.linearKind = RegistrationTransformKind::Similarity;
        break;
    case StarTransformModel::Affine:
        request.linearKind = RegistrationTransformKind::Affine;
        break;
    case StarTransformModel::Projective:
        request.linearKind = RegistrationTransformKind::Projective;
        break;
    }
    request.sourceToReference = transform.sourceToReference;
    return RegistrationTransform::create(request);
}

Result<RegistrationTransform> invertRegistrationTransform(const RegistrationTransform& transform)
{
    if (!transform.isLinear())
    {
        return transformFailure(ErrorCode::Unsupported,
                                "a polynomial correction has no closed-form inverse; invert it position by position",
                                kInvertOperation);
    }
    RegistrationTransformRequest request;
    request.linearKind = transform.linearKind();
    if (!invertLinear(transform, request.sourceToReference))
    {
        return transformFailure(ErrorCode::InvalidData, "this transform's matrix has no inverse", kInvertOperation);
    }
    return RegistrationTransform::create(request);
}

Result<RegistrationBounds> computeRegistrationBounds(const RegistrationTransform& transform, const std::uint32_t width,
                                                     const std::uint32_t height, const PixelCoordinate& postTranslation,
                                                     const SipDistortion* sourceDistortion,
                                                     const std::uint32_t boundarySamples)
{
    if (width == 0u || height == 0u)
    {
        return transformFailure(ErrorCode::InvalidArgument, "a frame with no extent occupies no rectangle",
                                kBoundsOperation);
    }
    if (boundarySamples < 2u || boundarySamples > kMaximumRegistrationBoundarySamples)
    {
        return transformFailure(ErrorCode::InvalidArgument,
                                "an edge is sampled at between two and the stated maximum positions", kBoundsOperation);
    }
    if (!isFinitePixel(postTranslation))
    {
        return transformFailure(ErrorCode::InvalidArgument, "a bounds translation is finite", kBoundsOperation);
    }

    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    const auto accumulate = [&](const PixelCoordinate& position) -> bool {
        PixelCoordinate mapped{};
        if (!mapBoundaryPosition(transform, sourceDistortion, position, postTranslation, mapped))
        {
            return false;
        }
        minimumX = (std::min)(minimumX, mapped.x);
        minimumY = (std::min)(minimumY, mapped.y);
        maximumX = (std::max)(maximumX, mapped.x);
        maximumY = (std::max)(maximumY, mapped.y);
        return true;
    };

    const double lastSample = static_cast<double>(boundarySamples - 1u);
    for (std::uint32_t sample = 0u; sample < boundarySamples; ++sample)
    {
        const double fraction = static_cast<double>(sample) / lastSample;
        const double alongX = fraction * static_cast<double>(width);
        const double alongY = fraction * static_cast<double>(height);
        if (!accumulate({.x = alongX, .y = 0.0}) || !accumulate({.x = alongX, .y = static_cast<double>(height)}) ||
            !accumulate({.x = 0.0, .y = alongY}) || !accumulate({.x = static_cast<double>(width), .y = alongY}))
        {
            return transformFailure(ErrorCode::InvalidData,
                                    "a position on this frame's boundary does not map, so its rectangle would omit "
                                    "part of the frame",
                                    kBoundsOperation);
        }
    }

    return RegistrationBounds{.minimumX = minimumX, .minimumY = minimumY, .maximumX = maximumX, .maximumY = maximumY};
}

Result<std::array<PixelCoordinate, kRegistrationFrameCornerCount>>
mapRegistrationFrameCorners(const RegistrationTransform& transform, const std::uint32_t width,
                            const std::uint32_t height, const PixelCoordinate& postTranslation,
                            const SipDistortion* sourceDistortion)
{
    if (width == 0u || height == 0u)
    {
        return transformFailure(ErrorCode::InvalidArgument, "a frame with no extent has no corners", kCornersOperation);
    }
    if (!isFinitePixel(postTranslation))
    {
        return transformFailure(ErrorCode::InvalidArgument, "a corner translation is finite", kCornersOperation);
    }

    const double right = static_cast<double>(width);
    const double bottom = static_cast<double>(height);
    const std::array<PixelCoordinate, kRegistrationFrameCornerCount> source{
        PixelCoordinate{.x = 0.0, .y = 0.0}, PixelCoordinate{.x = right, .y = 0.0},
        PixelCoordinate{.x = right, .y = bottom}, PixelCoordinate{.x = 0.0, .y = bottom}};
    std::array<PixelCoordinate, kRegistrationFrameCornerCount> mapped{};
    for (std::size_t index = 0u; index < kRegistrationFrameCornerCount; ++index)
    {
        if (!mapBoundaryPosition(transform, sourceDistortion, source[index], postTranslation, mapped[index]))
        {
            return transformFailure(ErrorCode::InvalidData, "a corner of this frame does not map", kCornersOperation);
        }
    }
    return mapped;
}

} // namespace an
