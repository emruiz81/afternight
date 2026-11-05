// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - fitting a registration transform to corresponding positions.
//
// The fit is two stages. The linear stage is the plain least-squares answer over every pair,
// reached through the star domain's robust estimator configured to reject nothing. The polynomial
// stage maps the linear stage's output to the reference positions with a full polynomial in
// normalized coordinates, solved through normal equations in extended precision, and over up to
// `kRegistrationPolynomialRefinementPasses` passes trims the pairs whose residual exceeds three
// upper-quartile residuals or a quarter pixel, whichever is larger. The automatic policy tries each
// order from the lowest and keeps the first that improves the linear residual enough on enough
// pairs.

#include <AfterNightEngine/Algorithms/Registration/RegistrationFit.h>

#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "algorithms/registration/RegistrationInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory_resource>
#include <numeric>
#include <string>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kPolynomialOperation = "an::fitRegistrationPolynomial";
constexpr const char* kFitOperation = "an::fitRegistrationTransform";

/// The residual threshold a trimming pass never goes below, in pixels.
///
/// Three times the upper-quartile residual of a fit that already describes the field is a
/// fraction of a pixel, and trimming at that distance rejects pairs for being slightly
/// better measured than their neighbours rather than for being wrong.
constexpr double kMinimumTrimThresholdPixels = 0.25;

/// The multiple of the upper-quartile residual a trimming pass keeps.
constexpr double kTrimSigmaMultiple = 3.0;

/// How many pairs above the term count a fit stops trimming at.
///
/// A system with barely more equations than unknowns has nothing left to reject: every remaining
/// residual is small because the polynomial passes through the pairs, not because the pairs agree.
constexpr std::size_t kTrimMargin = 2u;

/// The smallest pivot, relative to the system's largest entry, the solver will divide by.
constexpr long double kRelativePivotFloor = 1.0e-16L;

/// The squared distance, in square pixels, above which two positions count as distinct.
constexpr double kDistinctDistanceSquared = 1.0e-8;

/// The smallest doubled triangle area that counts as three positions not on one line.
constexpr double kNonCollinearArea = 1.0e-6;

/// Builds a registration-domain failure.
///
/// @param code the typed reason.
/// @param message the explanation for a person.
/// @param operation the public operation that failed.
/// @return the complete failure.
[[nodiscard]] Status fitFailure(const ErrorCode code, std::string message, const char* const operation)
{
    return Status::failure(
        code, StatusDetail{.message = std::move(message), .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Whether a position holds two finite numbers.
///
/// @param pixel the position to inspect.
/// @return true when both components are finite.
[[nodiscard]] bool isFinitePixel(const PixelCoordinate& pixel) noexcept
{
    return std::isfinite(pixel.x) && std::isfinite(pixel.y);
}

/// Checks the parts of a request both entry points share.
///
/// @param request the request to check.
/// @param operation the public operation being validated.
/// @return the failure, or success.
[[nodiscard]] Status checkSharedRequest(const RegistrationFitRequest& request, const char* const operation)
{
    if (!isRegistrationPolynomialPolicy(request.polynomialPolicy))
    {
        return fitFailure(ErrorCode::InvalidArgument, "a fit names a polynomial policy this build implements",
                          operation);
    }
    if (request.polynomialOrder < kMinimumRegistrationPolynomialOrder ||
        request.polynomialOrder > kMaximumRegistrationPolynomialOrder)
    {
        return fitFailure(ErrorCode::InvalidArgument, "a stated polynomial order is two, three or four", operation);
    }
    if (!isFinitePixel(request.normalizationOrigin) || !isFinitePixel(request.normalizationScale) ||
        request.normalizationScale.x == 0.0 || request.normalizationScale.y == 0.0)
    {
        return fitFailure(ErrorCode::InvalidArgument,
                          "a polynomial's normalization is finite and divides by a nonzero scale", operation);
    }
    if (!std::isfinite(request.automaticLinearResidualPixels) || request.automaticLinearResidualPixels < 0.0)
    {
        return fitFailure(ErrorCode::InvalidArgument,
                          "the automatic policy's residual threshold is finite and "
                          "not negative",
                          operation);
    }
    if (!std::isfinite(request.automaticImprovementFactor) || request.automaticImprovementFactor <= 0.0 ||
        request.automaticImprovementFactor > 1.0)
    {
        return fitFailure(ErrorCode::InvalidArgument,
                          "the automatic policy's improvement factor is above zero and at most one", operation);
    }
    return Status::success();
}

/// Checks the two correspondence spans.
///
/// @param source source-frame positions.
/// @param reference reference-frame positions.
/// @param minimum the fewest pairs the model needs.
/// @param operation the public operation being validated.
/// @return the failure, or success.
[[nodiscard]] Status checkCorrespondences(std::span<const PixelCoordinate> source,
                                          std::span<const PixelCoordinate> reference, const std::size_t minimum,
                                          const char* const operation)
{
    if (source.size() != reference.size())
    {
        return fitFailure(ErrorCode::IncompatibleArguments,
                          "a fit takes one reference position for every source position", operation);
    }
    if (source.size() > kMaximumRegistrationCorrespondences)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{
                .message = "a correspondence set exceeds this domain's ceiling",
                .operation = operation,
                .domain = ErrorDomain::Algorithms,
                .context = {StatusContextField::ofCount(ContextKey::Requested, source.size()),
                            StatusContextField::ofCount(ContextKey::Limit, kMaximumRegistrationCorrespondences)}});
    }
    if (source.size() < minimum)
    {
        return fitFailure(ErrorCode::EmptySelection, "a fit has fewer correspondences than its model needs", operation);
    }
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        if (!isFinitePixel(source[index]) || !isFinitePixel(reference[index]))
        {
            return fitFailure(ErrorCode::InvalidData, "a correspondence carries a position that is not finite",
                              operation);
        }
    }
    return Status::success();
}

/// Whether one side of a correspondence set can determine the requested model.
///
/// Linear in the point count: a first distinct pair fixes a line, and one position off that line is
/// what a model above a similarity needs. A search of every pair and every triple would be
/// affordable for a handful of curated control points and not for a matched star field.
///
/// @param points one side of the set.
/// @param kind the linear model being fitted.
/// @return true when the positions cannot determine the model.
[[nodiscard]] bool isDegenerate(std::span<const PixelCoordinate> points, const RegistrationTransformKind kind)
{
    if (points.empty())
    {
        return true;
    }
    const PixelCoordinate first = points.front();
    std::size_t secondIndex = points.size();
    for (std::size_t index = 1u; index < points.size(); ++index)
    {
        const double dx = points[index].x - first.x;
        const double dy = points[index].y - first.y;
        if ((dx * dx + dy * dy) > kDistinctDistanceSquared)
        {
            secondIndex = index;
            break;
        }
    }
    if (secondIndex == points.size())
    {
        return true;
    }
    if (kind == RegistrationTransformKind::Similarity)
    {
        return false;
    }
    const double edgeX = points[secondIndex].x - first.x;
    const double edgeY = points[secondIndex].y - first.y;
    for (std::size_t index = 1u; index < points.size(); ++index)
    {
        const double otherX = points[index].x - first.x;
        const double otherY = points[index].y - first.y;
        if (std::abs(edgeX * otherY - edgeY * otherX) > kNonCollinearArea)
        {
            return false;
        }
    }
    return true;
}

/// The star-domain model that corresponds to a linear registration kind.
///
/// @param kind the linear kind.
/// @return the matching model.
[[nodiscard]] StarTransformModel starModelOf(const RegistrationTransformKind kind) noexcept
{
    switch (kind)
    {
    case RegistrationTransformKind::Similarity:
        return StarTransformModel::Similarity;
    case RegistrationTransformKind::Projective:
        return StarTransformModel::Projective;
    default:
        return StarTransformModel::Affine;
    }
}

/// Fits the linear stage by least squares over every correspondence.
///
/// The estimator is the star domain's robust fit, configured so that nothing is rejected: the
/// residual threshold admits every pair, so the first non-degenerate minimal set classifies the
/// whole set as inliers and the deterministic all-point refit that follows is the plain
/// least-squares answer.
///
/// @param execution operation policy and engine-context lease.
/// @param source source-frame positions.
/// @param reference reference-frame positions.
/// @param kind the linear model.
/// @return the fitted transform, or the star domain's own failure.
[[nodiscard]] Result<RegistrationTransform> fitLinearStage(const ExecutionContext& execution,
                                                           std::span<const PixelCoordinate> source,
                                                           std::span<const PixelCoordinate> reference,
                                                           const RegistrationTransformKind kind)
{
    registration_internal::ExecutionMemory memory(*execution.memoryResource());
    std::pmr::vector<StarAlignmentPoint> sourcePoints(&memory);
    std::pmr::vector<StarAlignmentPoint> referencePoints(&memory);
    sourcePoints.reserve(source.size());
    referencePoints.reserve(reference.size());
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        sourcePoints.push_back({.x = source[index].x, .y = source[index].y});
        referencePoints.push_back({.x = reference[index].x, .y = reference[index].y});
    }

    StarAlignmentRequest request;
    request.model = starModelOf(kind);
    request.minimumMatches = minimumRegistrationControlPoints(kind);
    // Every pair is an inlier and every pair must remain one, so the model returned is the
    // least-squares fit of the whole set rather than of a subset this stage chose.
    request.inlierThreshold = 1.0e9;
    request.minimumInlierFraction = 1.0;
    request.ransacIterations = 256u;

    const Result<StarAlignmentResult> fitted = fitStarTransform(execution, sourcePoints, referencePoints, request);
    if (!fitted.hasValue())
    {
        return fitted.status();
    }
    return RegistrationTransform::create(
        {.linearKind = kind, .sourceToReference = fitted->transform.sourceToReference});
}

/// The largest and root-mean-square residual of a transform over a selection of correspondences.
struct ResidualSummary
{
    /// Root mean square of the selected residuals, in pixels.
    double rootMeanSquare = 0.0;

    /// The largest selected residual, in pixels.
    double maximum = 0.0;
};

/// Measures a transform over a selection of correspondences.
///
/// @param transform the model to measure.
/// @param source source-frame positions.
/// @param reference reference-frame positions.
/// @param selection indices to measure, or an empty span for every pair.
/// @param summary the measures, whose members are infinite when a position does not map.
/// @return false when a selected position does not map.
[[nodiscard]] bool measureResiduals(const RegistrationTransform& transform, std::span<const PixelCoordinate> source,
                                    std::span<const PixelCoordinate> reference,
                                    std::span<const std::uint32_t> selection, ResidualSummary& summary)
{
    const std::size_t count = selection.empty() ? source.size() : selection.size();
    if (count == 0u)
    {
        summary = {};
        return true;
    }
    double sumOfSquares = 0.0;
    double worst = 0.0;
    for (std::size_t position = 0u; position < count; ++position)
    {
        const std::size_t index = selection.empty() ? position : static_cast<std::size_t>(selection[position]);
        const Result<PixelCoordinate> mapped = mapRegistrationPoint(transform, source[index]);
        if (!mapped.hasValue())
        {
            summary.rootMeanSquare = std::numeric_limits<double>::infinity();
            summary.maximum = std::numeric_limits<double>::infinity();
            return false;
        }
        const double dx = mapped->x - reference[index].x;
        const double dy = mapped->y - reference[index].y;
        sumOfSquares += dx * dx + dy * dy;
        worst = (std::max)(worst, std::hypot(dx, dy));
    }
    summary.rootMeanSquare = std::sqrt(sumOfSquares / static_cast<double>(count));
    summary.maximum = worst;
    return std::isfinite(summary.rootMeanSquare);
}

/// Fills the polynomial terms of one normalized position, in the coefficient order.
///
/// @param nx the normalized horizontal offset.
/// @param ny the normalized vertical offset.
/// @param order the polynomial order.
/// @param terms the filled table; entries beyond the order's term count are untouched.
void fillTerms(const double nx, const double ny, const std::uint32_t order,
               std::array<long double, kRegistrationPolynomialTerms>& terms) noexcept
{
    std::array<long double, kMaximumRegistrationPolynomialOrder + 1u> powersX{};
    std::array<long double, kMaximumRegistrationPolynomialOrder + 1u> powersY{};
    powersX[0] = 1.0L;
    powersY[0] = 1.0L;
    for (std::size_t index = 1u; index <= static_cast<std::size_t>(order); ++index)
    {
        powersX[index] = powersX[index - 1u] * static_cast<long double>(nx);
        powersY[index] = powersY[index - 1u] * static_cast<long double>(ny);
    }
    std::size_t column = 0u;
    for (std::uint32_t degree = 0u; degree <= order; ++degree)
    {
        for (std::uint32_t verticalDegree = 0u; verticalDegree <= degree; ++verticalDegree)
        {
            terms[column++] = powersX[degree - verticalDegree] * powersY[verticalDegree];
        }
    }
}

/// Solves one symmetric system by Gauss-Jordan elimination with partial pivoting.
///
/// The accumulation is extended precision and the pivot floor is relative to the system's largest
/// entry, so a system whose scale is a coordinate in the thousands raised to the fourth power is
/// judged against itself rather than against an absolute number.
///
/// @param matrix the coefficient matrix, overwritten.
/// @param right the two right-hand sides, overwritten.
/// @param unknowns how many leading rows and columns participate.
/// @param answer the two solutions.
/// @return false when the system has no numerically usable solution.
[[nodiscard]] bool
solveSystem(std::array<long double, kRegistrationPolynomialTerms * kRegistrationPolynomialTerms>& matrix,
            std::array<std::array<long double, kRegistrationPolynomialTerms>, 2u>& right, const std::size_t unknowns,
            std::array<std::array<double, kRegistrationPolynomialTerms>, 2u>& answer) noexcept
{
    long double largest = 0.0L;
    for (std::size_t row = 0u; row < unknowns; ++row)
    {
        for (std::size_t column = 0u; column < unknowns; ++column)
        {
            largest = (std::max)(largest, std::abs(matrix[row * kRegistrationPolynomialTerms + column]));
        }
    }
    if (largest == 0.0L)
    {
        return false;
    }
    for (std::size_t pivot = 0u; pivot < unknowns; ++pivot)
    {
        std::size_t best = pivot;
        for (std::size_t row = pivot + 1u; row < unknowns; ++row)
        {
            if (std::abs(matrix[row * kRegistrationPolynomialTerms + pivot]) >
                std::abs(matrix[best * kRegistrationPolynomialTerms + pivot]))
            {
                best = row;
            }
        }
        if (std::abs(matrix[best * kRegistrationPolynomialTerms + pivot]) <= largest * kRelativePivotFloor)
        {
            return false;
        }
        if (best != pivot)
        {
            for (std::size_t column = pivot; column < unknowns; ++column)
            {
                std::swap(matrix[pivot * kRegistrationPolynomialTerms + column],
                          matrix[best * kRegistrationPolynomialTerms + column]);
            }
            std::swap(right[0][pivot], right[0][best]);
            std::swap(right[1][pivot], right[1][best]);
        }
        const long double divisor = matrix[pivot * kRegistrationPolynomialTerms + pivot];
        for (std::size_t column = pivot; column < unknowns; ++column)
        {
            matrix[pivot * kRegistrationPolynomialTerms + column] /= divisor;
        }
        right[0][pivot] /= divisor;
        right[1][pivot] /= divisor;
        for (std::size_t row = 0u; row < unknowns; ++row)
        {
            if (row == pivot)
            {
                continue;
            }
            const long double factor = matrix[row * kRegistrationPolynomialTerms + pivot];
            if (factor == 0.0L)
            {
                continue;
            }
            for (std::size_t column = pivot; column < unknowns; ++column)
            {
                matrix[row * kRegistrationPolynomialTerms + column] -=
                    factor * matrix[pivot * kRegistrationPolynomialTerms + column];
            }
            right[0][row] -= factor * right[0][pivot];
            right[1][row] -= factor * right[1][pivot];
        }
    }
    for (std::size_t index = 0u; index < unknowns; ++index)
    {
        answer[0][index] = static_cast<double>(right[0][index]);
        answer[1][index] = static_cast<double>(right[1][index]);
        if (!std::isfinite(answer[0][index]) || !std::isfinite(answer[1][index]))
        {
            return false;
        }
    }
    return true;
}

/// The upper-quartile value of a residual list.
///
/// @param residuals the residuals, reordered in place.
/// @return the residual at the three-quarter position of the sorted order.
[[nodiscard]] double upperQuartile(std::pmr::vector<double>& residuals)
{
    if (residuals.empty())
    {
        return 0.0;
    }
    const std::size_t position =
        static_cast<std::size_t>(std::floor(0.75 * static_cast<double>(residuals.size() - 1u)));
    std::nth_element(residuals.begin(), residuals.begin() + static_cast<std::ptrdiff_t>(position), residuals.end());
    return residuals[position];
}

} // namespace

const char* registrationPolynomialPolicyName(const RegistrationPolynomialPolicy policy) noexcept
{
    switch (policy)
    {
    case RegistrationPolynomialPolicy::LinearOnly:
        return "LinearOnly";
    case RegistrationPolynomialPolicy::StatedOrder:
        return "StatedOrder";
    case RegistrationPolynomialPolicy::AutomaticOrder:
        return "AutomaticOrder";
    }
    return "UnknownRegistrationPolynomialPolicy";
}

namespace
{

/// Fits a polynomial correction, letting an engine-context refusal escape as an exception.
///
/// @param execution operation policy and engine-context lease.
/// @param source source-frame positions.
/// @param reference reference-frame positions.
/// @param linear the linear seed.
/// @param request the order, normalization and thresholds.
/// @return the fitted transform and its measures, or a returned failure.
[[nodiscard]] Result<RegistrationFitResult> fitPolynomialUnguarded(const ExecutionContext& execution,
                                                                   std::span<const PixelCoordinate> source,
                                                                   std::span<const PixelCoordinate> reference,
                                                                   const RegistrationTransform& linear,
                                                                   const RegistrationFitRequest& request)
{
    if (!execution.valid())
    {
        return fitFailure(ErrorCode::NotInitialized,
                          "a fit needs an execution context that holds a lease on an engine context",
                          kPolynomialOperation);
    }
    if (const Status checked = checkSharedRequest(request, kPolynomialOperation); !checked.ok())
    {
        return checked;
    }
    if (!linear.isLinear())
    {
        return fitFailure(ErrorCode::InvalidArgument,
                          "a polynomial correction is fitted on a linear seed, not on another correction",
                          kPolynomialOperation);
    }
    const std::uint32_t order = request.polynomialOrder;
    const std::size_t terms = static_cast<std::size_t>(registrationPolynomialTermCount(order));
    if (const Status checked = checkCorrespondences(source, reference, terms, kPolynomialOperation); !checked.ok())
    {
        return checked;
    }
    if (const Status running = execution.checkCancellation(); running.failed())
    {
        return running;
    }

    registration_internal::ExecutionMemory memory(*execution.memoryResource());
    std::pmr::vector<PixelCoordinate> normalizedSource(&memory);
    std::pmr::vector<PixelCoordinate> normalizedTarget(&memory);
    normalizedSource.reserve(source.size());
    normalizedTarget.reserve(source.size());
    const double originX = request.normalizationOrigin.x;
    const double originY = request.normalizationOrigin.y;
    const double scaleX = request.normalizationScale.x;
    const double scaleY = request.normalizationScale.y;
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        const Result<PixelCoordinate> mapped = mapRegistrationPoint(linear, source[index]);
        if (!mapped.hasValue())
        {
            return fitFailure(ErrorCode::InvalidData, "the linear seed does not map one of the source positions",
                              kPolynomialOperation);
        }
        normalizedSource.push_back({.x = (mapped->x - originX) / scaleX, .y = (mapped->y - originY) / scaleY});
        normalizedTarget.push_back(
            {.x = (reference[index].x - originX) / scaleX, .y = (reference[index].y - originY) / scaleY});
    }

    RegistrationFitResult result;
    result.correspondenceCount = static_cast<std::uint32_t>(source.size());
    ResidualSummary linearSummary;
    if (!measureResiduals(linear, source, reference, {}, linearSummary))
    {
        return fitFailure(ErrorCode::InvalidData, "the linear seed leaves a residual that is not finite",
                          kPolynomialOperation);
    }
    result.linearRootMeanSquareResidual = linearSummary.rootMeanSquare;

    std::pmr::vector<std::uint32_t> active(&memory);
    active.resize(source.size());
    std::iota(active.begin(), active.end(), 0u);

    RegistrationTransform candidate = linear;
    for (std::uint32_t pass = 0u; pass < kRegistrationPolynomialRefinementPasses; ++pass)
    {
        if (const Status running = execution.checkCancellation(); running.failed())
        {
            return running;
        }
        ++result.refinementPasses;

        std::array<long double, kRegistrationPolynomialTerms * kRegistrationPolynomialTerms> normal{};
        std::array<std::array<long double, kRegistrationPolynomialTerms>, 2u> right{};
        std::array<long double, kRegistrationPolynomialTerms> termValues{};
        for (const std::uint32_t index : active)
        {
            fillTerms(normalizedSource[index].x, normalizedSource[index].y, order, termValues);
            for (std::size_t row = 0u; row < terms; ++row)
            {
                const long double termRow = termValues[row];
                for (std::size_t column = row; column < terms; ++column)
                {
                    normal[row * kRegistrationPolynomialTerms + column] += termRow * termValues[column];
                }
                right[0][row] += termRow * static_cast<long double>(normalizedTarget[index].x);
                right[1][row] += termRow * static_cast<long double>(normalizedTarget[index].y);
            }
        }
        for (std::size_t row = 0u; row < terms; ++row)
        {
            for (std::size_t column = 0u; column < row; ++column)
            {
                normal[row * kRegistrationPolynomialTerms + column] =
                    normal[column * kRegistrationPolynomialTerms + row];
            }
        }

        std::array<std::array<double, kRegistrationPolynomialTerms>, 2u> coefficients{};
        if (!solveSystem(normal, right, terms, coefficients))
        {
            return fitFailure(ErrorCode::InvalidData,
                              "the polynomial system has no numerically usable solution at this order",
                              kPolynomialOperation);
        }

        RegistrationTransformRequest candidateRequest = linear.toRequest();
        candidateRequest.polynomial.order = order;
        candidateRequest.polynomial.normalizationOrigin = request.normalizationOrigin;
        candidateRequest.polynomial.normalizationScale = request.normalizationScale;
        for (std::size_t index = 0u; index < terms; ++index)
        {
            candidateRequest.polynomial.x[index] = coefficients[0][index];
            candidateRequest.polynomial.y[index] = coefficients[1][index];
        }
        const Result<RegistrationTransform> made = RegistrationTransform::create(candidateRequest);
        if (!made.hasValue())
        {
            return made.status();
        }
        candidate = *made;

        const bool lastPass = pass + 1u == kRegistrationPolynomialRefinementPasses;
        if (active.size() <= terms + kTrimMargin || lastPass)
        {
            break;
        }

        std::pmr::vector<double> residuals(&memory);
        residuals.reserve(active.size());
        for (const std::uint32_t index : active)
        {
            const Result<PixelCoordinate> mapped = mapRegistrationPoint(candidate, source[index]);
            if (!mapped.hasValue())
            {
                return fitFailure(ErrorCode::InvalidData,
                                  "the fitted correction does not map one of the source positions",
                                  kPolynomialOperation);
            }
            residuals.push_back(std::hypot(mapped->x - reference[index].x, mapped->y - reference[index].y));
        }
        std::pmr::vector<double> ordered(residuals, &memory);
        const double threshold = (std::max)(kTrimSigmaMultiple * upperQuartile(ordered), kMinimumTrimThresholdPixels);

        std::pmr::vector<std::uint32_t> retained(&memory);
        retained.reserve(active.size());
        for (std::size_t position = 0u; position < active.size(); ++position)
        {
            if (residuals[position] <= threshold)
            {
                retained.push_back(active[position]);
            }
        }
        if (retained.size() < terms || retained.size() == active.size())
        {
            break;
        }
        active = std::move(retained);
    }

    ResidualSummary summary;
    if (!measureResiduals(candidate, source, reference, active, summary))
    {
        return fitFailure(ErrorCode::InvalidData, "the fitted correction leaves a residual that is not finite",
                          kPolynomialOperation);
    }
    result.transform = candidate;
    result.rootMeanSquareResidual = summary.rootMeanSquare;
    result.maximumResidual = summary.maximum;
    result.polynomialOrder = order;
    result.polynomialInliers = static_cast<std::uint32_t>(active.size());
    return result;
}

/// Fits a registration transform, letting an engine-context refusal escape as an exception.
///
/// @param execution operation policy and engine-context lease.
/// @param source source-frame positions.
/// @param reference reference-frame positions.
/// @param request the model, polynomial policy, normalization and thresholds.
/// @return the fitted transform and its measures, or a returned failure.
[[nodiscard]] Result<RegistrationFitResult> fitTransformUnguarded(const ExecutionContext& execution,
                                                                  std::span<const PixelCoordinate> source,
                                                                  std::span<const PixelCoordinate> reference,
                                                                  const RegistrationFitRequest& request)
{
    if (!execution.valid())
    {
        return fitFailure(ErrorCode::NotInitialized,
                          "a fit needs an execution context that holds a lease on an engine context", kFitOperation);
    }
    if (!isLinearRegistrationTransformKind(request.linearKind))
    {
        return fitFailure(ErrorCode::InvalidArgument,
                          "a fit states a linear model; the polynomial order is stated by the policy beside it",
                          kFitOperation);
    }
    if (const Status checked = checkSharedRequest(request, kFitOperation); !checked.ok())
    {
        return checked;
    }
    if (const Status checked = checkCorrespondences(
            source, reference, minimumRegistrationControlPoints(request.linearKind), kFitOperation);
        !checked.ok())
    {
        return checked;
    }
    if (isDegenerate(source, request.linearKind) || isDegenerate(reference, request.linearKind))
    {
        return fitFailure(ErrorCode::InvalidData,
                          "these correspondences cannot determine the requested model; they lie on one line or on "
                          "one position",
                          kFitOperation);
    }
    if (const Status running = execution.checkCancellation(); running.failed())
    {
        return running;
    }

    const Result<RegistrationTransform> linear = fitLinearStage(execution, source, reference, request.linearKind);
    if (!linear.hasValue())
    {
        return linear.status();
    }
    ResidualSummary linearSummary;
    if (!measureResiduals(*linear, source, reference, {}, linearSummary))
    {
        return fitFailure(ErrorCode::InvalidData, "the fitted linear model leaves a residual that is not finite",
                          kFitOperation);
    }

    RegistrationFitResult result;
    result.transform = *linear;
    result.correspondenceCount = static_cast<std::uint32_t>(source.size());
    result.linearRootMeanSquareResidual = linearSummary.rootMeanSquare;
    result.rootMeanSquareResidual = linearSummary.rootMeanSquare;
    result.maximumResidual = linearSummary.maximum;

    if (request.polynomialPolicy == RegistrationPolynomialPolicy::LinearOnly)
    {
        return result;
    }
    if (request.polynomialPolicy == RegistrationPolynomialPolicy::StatedOrder)
    {
        Result<RegistrationFitResult> corrected =
            fitPolynomialUnguarded(execution, source, reference, *linear, request);
        if (!corrected.hasValue())
        {
            return corrected.status();
        }
        corrected->linearRootMeanSquareResidual = linearSummary.rootMeanSquare;
        return corrected;
    }

    // The automatic policy keeps the linear model when it already fits, and otherwise the lowest
    // order that has enough pairs, keeps enough of them through trimming, and lowers the linear
    // residual by the stated factor. An order that fails for any reason but a stop or a memory
    // refusal is only rejected; the linear model stands when every order is.
    if (linearSummary.rootMeanSquare <= request.automaticLinearResidualPixels)
    {
        return result;
    }
    for (std::uint32_t order = kMinimumRegistrationPolynomialOrder; order <= kMaximumRegistrationPolynomialOrder;
         ++order)
    {
        if (const Status running = execution.checkCancellation(); running.failed())
        {
            return running;
        }
        const std::uint32_t needed = automaticRegistrationPolynomialMinimumPoints(order);
        if (source.size() < needed)
        {
            ++result.rejectedPolynomialOrders;
            continue;
        }
        RegistrationFitRequest attempt = request;
        attempt.polynomialOrder = order;
        Result<RegistrationFitResult> corrected =
            fitPolynomialUnguarded(execution, source, reference, *linear, attempt);
        if (!corrected.hasValue())
        {
            if (corrected.status().code() == ErrorCode::Cancelled ||
                corrected.status().code() == ErrorCode::DeadlineExceeded ||
                corrected.status().code() == ErrorCode::MemoryBudgetExceeded ||
                corrected.status().code() == ErrorCode::AllocationFailed)
            {
                return corrected.status();
            }
            ++result.rejectedPolynomialOrders;
            continue;
        }
        if (corrected->polynomialInliers < needed ||
            corrected->rootMeanSquareResidual > linearSummary.rootMeanSquare * request.automaticImprovementFactor)
        {
            ++result.rejectedPolynomialOrders;
            continue;
        }
        corrected->linearRootMeanSquareResidual = linearSummary.rootMeanSquare;
        corrected->rejectedPolynomialOrders = result.rejectedPolynomialOrders;
        return corrected;
    }
    return result;
}

} // namespace

Result<RegistrationFitResult> fitRegistrationPolynomial(const ExecutionContext& execution,
                                                        std::span<const PixelCoordinate> source,
                                                        std::span<const PixelCoordinate> reference,
                                                        const RegistrationTransform& linear,
                                                        const RegistrationFitRequest& request)
{
    try
    {
        return fitPolynomialUnguarded(execution, source, reference, linear, request);
    }
    catch (const registration_internal::ContextAllocationFailure& refused)
    {
        return refused.status();
    }
}

Result<RegistrationFitResult> fitRegistrationTransform(const ExecutionContext& execution,
                                                       std::span<const PixelCoordinate> source,
                                                       std::span<const PixelCoordinate> reference,
                                                       const RegistrationFitRequest& request)
{
    try
    {
        return fitTransformUnguarded(execution, source, reference, request);
    }
    catch (const registration_internal::ContextAllocationFailure& refused)
    {
        return refused.status();
    }
}

} // namespace an
