// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - robust WCS fitting and centre refinement for plate solving.
//
// The fixed-centre fit is the star domain's robust affine fit from tangent-plane degrees to image
// pixels, inverted into the WCS matrix, plus an optional forward SIP polynomial fitted to what the
// affine model leaves. The polynomial is a least-squares fit of the residual vectors against the
// terms of degree two and up in the offset from the reference pixel, normalised by the larger image
// dimension so the normal equations stay conditioned, and solved in extended precision. Refinement
// repeats search, match and fit while the solved field centre moves, and always ends with one fit
// at the centre it arrived at.

#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "algorithms/astrometry/solver/PlateSolverInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using plate_solver_detail::failure;

/// Terms of a fourth-order SIP polynomial of degree two and up: three, four and five per degree.
constexpr std::size_t kMaximumSipTerms = 12u;

/// How much a distortion polynomial must lower the RMS residual, in pixels, to be retained.
constexpr double kMinimumSipImprovementPixels = 1.0e-3;

/// The magnitude at or below which every coefficient counts as no distortion at all.
constexpr double kMinimumSipCoefficient = 1.0e-12;

/// The engine context's refusal, carried out of an allocator that cannot return one.
///
/// Derived from `std::bad_alloc` so a standard container passes it through; the fit catches it
/// ahead of any other `std::bad_alloc` and returns the refusal it carries.
class ContextAllocationFailure final : public std::bad_alloc
{
public:
    /// Wraps a refusal.
    ///
    /// @param status the failure the memory resource returned.
    explicit ContextAllocationFailure(Status status) noexcept : m_status(std::move(status)) {}

    /// The refusal this failure carries.
    ///
    /// @return the original failure, with its budget context intact.
    [[nodiscard]] const Status& status() const noexcept
    {
        return m_status;
    }

private:
    /// The refusal.
    Status m_status;
};

/// A standard allocator over the execution's engine-context memory that counts its peak.
class ExecutionMemory final : public std::pmr::memory_resource
{
public:
    /// Binds to a context's memory resource, which must outlive this adapter.
    ///
    /// @param memory the resource.
    explicit ExecutionMemory(MemoryResource& memory) noexcept : m_memory(memory) {}

    /// The most bytes this adapter held at once.
    ///
    /// @return the peak, in bytes.
    [[nodiscard]] std::uint64_t peakBytes() const noexcept
    {
        return m_peakBytes;
    }

    /// The bytes this adapter holds now.
    ///
    /// @return the live count, in bytes.
    [[nodiscard]] std::uint64_t liveBytes() const noexcept
    {
        return m_liveBytes;
    }

private:
    /// Takes bytes from the engine context, throwing `ContextAllocationFailure` on a refusal.
    ///
    /// @param bytes how many bytes are wanted.
    /// @param alignment the required alignment.
    /// @return the memory.
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        Result<void*> memory = m_memory.allocate(bytes, alignment);
        if (!memory.hasValue())
        {
            throw ContextAllocationFailure(memory.status());
        }
        m_liveBytes += bytes;
        m_peakBytes = (std::max)(m_peakBytes, m_liveBytes);
        return *memory;
    }

    /// Returns bytes to the engine context.
    ///
    /// @param pointer the memory to release.
    /// @param bytes how many bytes it holds.
    /// @param alignment the alignment it was taken with.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_memory.deallocate(pointer, bytes, alignment);
        m_liveBytes -= bytes;
    }

    /// Whether another resource is this one.
    ///
    /// @param other the resource to compare against.
    /// @return true only for this object.
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    /// The engine context's resource.
    MemoryResource& m_memory;

    /// Bytes currently held.
    std::uint64_t m_liveBytes = 0u;

    /// The most bytes held at once.
    std::uint64_t m_peakBytes = 0u;
};

/// One monomial of a distortion polynomial, `u^xPower * v^yPower`.
struct SipTerm
{
    /// The power of the horizontal offset.
    std::uint32_t xPower = 0u;

    /// The power of the vertical offset.
    std::uint32_t yPower = 0u;
};

/// The distortion a fit settled on, which is no distortion when it did not help.
struct SipCandidate
{
    /// The model; default-constructed, correcting nothing, when none was retained.
    SipDistortion distortion{};

    /// The RMS residual with this model, in pixels; the linear fit's when none was retained.
    double rmsResidualPixels = 0.0;

    /// The model's order, or zero when none was retained.
    std::uint32_t order = 0u;
};

/// The polynomial order a number of inliers can support.
///
/// @param count the inliers the polynomial is fitted to.
/// @return four from 24 inliers, three from 10, two from 6, and zero below that.
[[nodiscard]] std::uint32_t chooseSipOrder(std::size_t count) noexcept
{
    if (count >= 24u)
    {
        return 4u;
    }
    if (count >= 10u)
    {
        return 3u;
    }
    if (count >= 6u)
    {
        return 2u;
    }
    return 0u;
}

/// Lists the monomials of degree two through `order`, in ascending degree and, within one degree,
/// descending horizontal power.
///
/// @param order the polynomial order, at most four.
/// @param terms the list to fill.
/// @return how many entries were filled.
[[nodiscard]] std::size_t buildSipTerms(std::uint32_t order, std::array<SipTerm, kMaximumSipTerms>& terms) noexcept
{
    std::size_t count = 0u;
    for (std::uint32_t degree = 2u; degree <= order; ++degree)
    {
        for (std::uint32_t yPower = 0u; yPower <= degree; ++yPower)
        {
            terms[count++] = SipTerm{.xPower = degree - yPower, .yPower = yPower};
        }
    }
    return count;
}

/// Raises a value to a small whole power by repeated multiplication.
///
/// @param value the base.
/// @param power the exponent.
/// @return the power; one for an exponent of zero.
[[nodiscard]] double integerPower(double value, std::uint32_t power) noexcept
{
    double answer = 1.0;
    for (std::uint32_t index = 0u; index < power; ++index)
    {
        answer *= value;
    }
    return answer;
}

/// Solves one augmented normal-equation system by Gauss-Jordan elimination with partial pivoting.
///
/// @param augmented the system, each row its coefficients followed by its right-hand side;
///        overwritten.
/// @param count how many leading rows and columns participate.
/// @param solution the answer.
/// @return false when a pivot is at most 1e-24 in magnitude or the answer is not finite.
[[nodiscard]] bool
solveNormalEquations(std::array<std::array<long double, kMaximumSipTerms + 1u>, kMaximumSipTerms>& augmented,
                     std::size_t count, std::array<double, kMaximumSipTerms>& solution) noexcept
{
    constexpr long double kPivotFloor = 1.0e-24L;
    for (std::size_t column = 0u; column < count; ++column)
    {
        std::size_t pivot = column;
        for (std::size_t row = column + 1u; row < count; ++row)
        {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
            {
                pivot = row;
            }
        }
        if (std::abs(augmented[pivot][column]) <= kPivotFloor)
        {
            return false;
        }
        if (pivot != column)
        {
            std::swap(augmented[pivot], augmented[column]);
        }

        const long double divisor = augmented[column][column];
        for (std::size_t entry = column; entry <= count; ++entry)
        {
            augmented[column][entry] /= divisor;
        }
        for (std::size_t row = 0u; row < count; ++row)
        {
            if (row == column)
            {
                continue;
            }
            const long double factor = augmented[row][column];
            for (std::size_t entry = column; entry <= count; ++entry)
            {
                augmented[row][entry] -= factor * augmented[column][entry];
            }
        }
    }

    for (std::size_t index = 0u; index < count; ++index)
    {
        solution[index] = static_cast<double>(augmented[index][count]);
        if (!std::isfinite(solution[index]))
        {
            return false;
        }
    }
    return true;
}

/// Fits the forward distortion polynomials to what the linear fit leaves.
///
/// The target at each inlier is the affine prediction minus the observed position, so the fitted
/// polynomial moves an observed position onto the linear model. Its offsets are measured from the
/// reference pixel - where the affine model puts the tangent point - and divided by the larger image
/// dimension, and the coefficients are scaled back to pixel offsets after the solve. Anything that
/// does not produce a clear improvement returns no distortion rather than a failure.
///
/// @param execution the operation, for cancellation every 64 inliers.
/// @param transform the robust affine fit from tangent-plane degrees to image pixels.
/// @param matches the inliers.
/// @param plan the checked plan, for the fit switch and the image dimensions.
/// @param linearRms the affine fit's RMS residual, in pixels.
/// @return the retained model with its order and RMS, the linear RMS with no model, the
///         cancellation, or a `SipDistortion::create` refusal.
[[nodiscard]] Result<SipCandidate> fitSip(const ExecutionContext& execution, const StarTransform& transform,
                                          std::span<const PlateSolveMatch> matches, const PlateSolvePlan& plan,
                                          double linearRms)
{
    SipCandidate absent{.rmsResidualPixels = linearRms};
    if (!plan.request().fitSipDistortion)
    {
        return absent;
    }
    const std::uint32_t order = chooseSipOrder(matches.size());
    if (order == 0u)
    {
        return absent;
    }

    std::array<SipTerm, kMaximumSipTerms> terms{};
    const std::size_t termCount = buildSipTerms(order, terms);
    std::array<std::array<long double, kMaximumSipTerms>, kMaximumSipTerms> normal{};
    std::array<long double, kMaximumSipTerms> rhsX{};
    std::array<long double, kMaximumSipTerms> rhsY{};
    const double referenceX = transform.sourceToReference[2u];
    const double referenceY = transform.sourceToReference[5u];
    const double normalization =
        (std::max)(static_cast<double>(plan.request().imageWidth), static_cast<double>(plan.request().imageHeight));

    for (std::size_t row = 0u; row < matches.size(); ++row)
    {
        if ((row & 63u) == 0u)
        {
            const Status cancellation = execution.checkCancellation();
            if (cancellation.failed())
            {
                return plate_solver_detail::cancelledFailure(plate_solver_detail::kFitOperation, cancellation);
            }
        }
        const PlateSolveMatch& match = matches[row];
        const double predictedX = transform.sourceToReference[0u] * match.tangentXDegrees +
                                  transform.sourceToReference[1u] * match.tangentYDegrees + referenceX;
        const double predictedY = transform.sourceToReference[3u] * match.tangentXDegrees +
                                  transform.sourceToReference[4u] * match.tangentYDegrees + referenceY;
        const double targetX = predictedX - match.imagePosition.x;
        const double targetY = predictedY - match.imagePosition.y;
        const double u = (match.imagePosition.x - referenceX) / normalization;
        const double v = (match.imagePosition.y - referenceY) / normalization;
        std::array<long double, kMaximumSipTerms> design{};
        for (std::size_t column = 0u; column < termCount; ++column)
        {
            design[column] =
                static_cast<long double>(integerPower(u, terms[column].xPower) * integerPower(v, terms[column].yPower));
        }
        for (std::size_t left = 0u; left < termCount; ++left)
        {
            rhsX[left] += design[left] * static_cast<long double>(targetX);
            rhsY[left] += design[left] * static_cast<long double>(targetY);
            for (std::size_t right = 0u; right < termCount; ++right)
            {
                normal[left][right] += design[left] * design[right];
            }
        }
    }

    std::array<std::array<long double, kMaximumSipTerms + 1u>, kMaximumSipTerms> augmentedX{};
    std::array<std::array<long double, kMaximumSipTerms + 1u>, kMaximumSipTerms> augmentedY{};
    for (std::size_t row = 0u; row < termCount; ++row)
    {
        for (std::size_t column = 0u; column < termCount; ++column)
        {
            augmentedX[row][column] = normal[row][column];
            augmentedY[row][column] = normal[row][column];
        }
        augmentedX[row][termCount] = rhsX[row];
        augmentedY[row][termCount] = rhsY[row];
    }
    std::array<double, kMaximumSipTerms> normalizedX{};
    std::array<double, kMaximumSipTerms> normalizedY{};
    if (!solveNormalEquations(augmentedX, termCount, normalizedX) ||
        !solveNormalEquations(augmentedY, termCount, normalizedY))
    {
        return absent;
    }

    SipPolynomial polynomialX{.order = order};
    SipPolynomial polynomialY{.order = order};
    double maximumCoefficient = 0.0;
    for (std::size_t index = 0u; index < termCount; ++index)
    {
        const SipTerm term = terms[index];
        const double divisor = integerPower(normalization, term.xPower + term.yPower);
        const double coefficientX = normalizedX[index] / divisor;
        const double coefficientY = normalizedY[index] / divisor;
        polynomialX.coefficients[term.xPower][term.yPower] = coefficientX;
        polynomialY.coefficients[term.xPower][term.yPower] = coefficientY;
        maximumCoefficient = (std::max)({maximumCoefficient, std::abs(coefficientX), std::abs(coefficientY)});
    }
    if (maximumCoefficient <= kMinimumSipCoefficient)
    {
        return absent;
    }

    const Result<SipDistortion> distortion =
        SipDistortion::create(SipDistortionRequest{.referencePixel = PixelCoordinate{.x = referenceX, .y = referenceY},
                                                   .forwardX = polynomialX,
                                                   .forwardY = polynomialY});
    if (!distortion.hasValue())
    {
        return distortion.status();
    }

    long double squaredError = 0.0L;
    for (const PlateSolveMatch& match : matches)
    {
        const double predictedX = transform.sourceToReference[0u] * match.tangentXDegrees +
                                  transform.sourceToReference[1u] * match.tangentYDegrees + referenceX;
        const double predictedY = transform.sourceToReference[3u] * match.tangentXDegrees +
                                  transform.sourceToReference[4u] * match.tangentYDegrees + referenceY;
        const Result<PixelCoordinate> corrected = applySipForward(*distortion, match.imagePosition);
        if (!corrected.hasValue())
        {
            return absent;
        }
        const double dx = corrected->x - predictedX;
        const double dy = corrected->y - predictedY;
        squaredError += static_cast<long double>(dx) * dx + static_cast<long double>(dy) * dy;
    }
    const double rms = std::sqrt(static_cast<double>(squaredError / matches.size()));
    if (!std::isfinite(rms) || linearRms - rms <= kMinimumSipImprovementPixels)
    {
        return absent;
    }
    return SipCandidate{.distortion = *distortion, .rmsResidualPixels = rms, .order = order};
}

/// Whether a fit meets the request's match-count and residual limits.
///
/// @param fit the finished fit.
/// @param plan the checked plan.
/// @return true when it has at least `minimumMatchedStars` matches and a finite final RMS within
///         `maximumRmsResidualPixels`.
[[nodiscard]] bool acceptedFit(const PlateSolveFitResult& fit, const PlateSolvePlan& plan) noexcept
{
    return fit.matches.size() >= plan.request().minimumMatchedStars && std::isfinite(fit.finalRmsResidualPixels) &&
           fit.finalRmsResidualPixels <= plan.request().maximumRmsResidualPixels;
}

} // namespace

Result<PlateSolveFitResult> fitPlateSolveModel(const ExecutionContext& execution, const PlateSolveMatchResult& matches,
                                               const SkyCoordinate& trialCenter, const PlateSolvePlan& plan)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, plate_solver_detail::kFitOperation,
                       "plate-solve fitting requires a live execution context");
    }
    if (!plan.valid())
    {
        return failure(ErrorCode::NotInitialized, plate_solver_detail::kFitOperation,
                       "plate-solve fitting requires a checked plan");
    }
    if (!isSkyCoordinate(trialCenter))
    {
        return plate_solver_detail::parameterFailure(plate_solver_detail::kFitOperation, "trialCenter",
                                                     "the fit tangent point must be a finite sky direction");
    }
    if (matches.matches.size() < plan.request().minimumMatchedStars)
    {
        return failure(ErrorCode::EmptySelection, plate_solver_detail::kFitOperation,
                       "the fixed-centre fit has fewer correspondences than the requested minimum");
    }
    MemoryResource* const memoryResource = execution.memoryResource();
    if (memoryResource == nullptr)
    {
        return failure(ErrorCode::NotInitialized, plate_solver_detail::kFitOperation,
                       "the plate-solve execution has no memory resource");
    }

    try
    {
        ExecutionMemory memory(*memoryResource);
        std::pmr::vector<StarAlignmentPoint> tangent(&memory);
        std::pmr::vector<StarAlignmentPoint> image(&memory);
        tangent.reserve(matches.matches.size());
        image.reserve(matches.matches.size());
        for (const PlateSolveMatch& match : matches.matches)
        {
            if (!std::isfinite(match.tangentXDegrees) || !std::isfinite(match.tangentYDegrees) ||
                !std::isfinite(match.imagePosition.x) || !std::isfinite(match.imagePosition.y) ||
                !isSkyCoordinate(match.catalogCoordinate))
            {
                return failure(ErrorCode::InvalidData, plate_solver_detail::kFitOperation,
                               "a plate-solve correspondence carries a non-finite coordinate");
            }
            tangent.push_back(StarAlignmentPoint{.x = match.tangentXDegrees, .y = match.tangentYDegrees});
            image.push_back(StarAlignmentPoint{.x = match.imagePosition.x, .y = match.imagePosition.y});
        }

        const StarAlignmentRequest fitRequest{.model = StarTransformModel::Affine,
                                              .trianglePolicy = StarTrianglePolicy::Automatic,
                                              .maximumTriangles = plan.request().maximumTriangles,
                                              .minimumTriangleSide = plan.request().minimumTriangleSidePixels,
                                              .triangleRatioTolerance = plan.request().triangleRatioTolerance,
                                              .triangleToleranceMinimumScale = 0.5,
                                              .triangleToleranceMaximumScale = 1.0,
                                              .maximumTriangleNeighbours = 5u,
                                              .minimumVotes = 1u,
                                              .minimumMatches = plan.request().minimumMatchedStars,
                                              .inlierThreshold = plan.request().maximumRmsResidualPixels,
                                              .minimumInlierFraction = 0.25,
                                              .ransacIterations = plan.request().ransacIterations,
                                              .seed = plan.request().deterministicSeed};
        Result<StarAlignmentResult> fitted = fitStarTransform(execution, tangent, image, fitRequest);
        if (!fitted.hasValue())
        {
            return fitted.status();
        }
        const StarTransform& transform = fitted->transform;
        const double a = transform.sourceToReference[0u];
        const double b = transform.sourceToReference[1u];
        const double c = transform.sourceToReference[3u];
        const double d = transform.sourceToReference[4u];
        const double determinant = a * d - b * c;
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-18)
        {
            return failure(ErrorCode::InvalidData, plate_solver_detail::kFitOperation,
                           "the tangent-to-pixel fit is singular");
        }

        std::vector<PlateSolveMatch> retained;
        retained.reserve(fitted->matches.size());
        for (const StarMatch& inlier : fitted->matches)
        {
            if (inlier.sourceIndex >= matches.matches.size())
            {
                return failure(ErrorCode::Internal, plate_solver_detail::kFitOperation,
                               "robust fitting returned an inlier outside its input");
            }
            PlateSolveMatch retainedMatch = matches.matches[inlier.sourceIndex];
            retainedMatch.residualPixels = inlier.residual;
            retained.push_back(std::move(retainedMatch));
        }

        const Result<SipCandidate> sip =
            fitSip(execution, transform, retained, plan, fitted->quality.rootMeanSquareResidual);
        if (!sip.hasValue())
        {
            return sip.status();
        }
        if (sip->order != 0u)
        {
            for (PlateSolveMatch& match : retained)
            {
                const double predictedX =
                    a * match.tangentXDegrees + b * match.tangentYDegrees + transform.sourceToReference[2u];
                const double predictedY =
                    c * match.tangentXDegrees + d * match.tangentYDegrees + transform.sourceToReference[5u];
                const Result<PixelCoordinate> corrected = applySipForward(sip->distortion, match.imagePosition);
                if (corrected.hasValue())
                {
                    match.residualPixels = std::hypot(corrected->x - predictedX, corrected->y - predictedY);
                }
            }
        }

        const WcsLinearTransform linear{
            .cd11 = d / determinant, .cd12 = -b / determinant, .cd21 = -c / determinant, .cd22 = a / determinant};
        const Result<WcsSolution> solution = WcsSolution::create(
            WcsSolutionRequest{.referencePixel = PixelCoordinate{.x = transform.sourceToReference[2u],
                                                                 .y = transform.sourceToReference[5u]},
                               .referencePoint = trialCenter,
                               .linear = linear,
                               .projection = WcsProjection::Gnomonic,
                               .imageWidth = plan.request().imageWidth,
                               .imageHeight = plan.request().imageHeight,
                               .distortion = sip->distortion});
        if (!solution.hasValue())
        {
            return solution.status();
        }

        const std::uint64_t workingBytes = memory.peakBytes() + fitted->quality.workingBytes;
        PlateSolveFitResult result{.solution = *solution,
                                   .matches = std::move(retained),
                                   .linearRmsResidualPixels = fitted->quality.rootMeanSquareResidual,
                                   .finalRmsResidualPixels = sip->rmsResidualPixels,
                                   .rejectedOutliers = fitted->quality.rejectedOutliers,
                                   .sipOrder = sip->order,
                                   .peakWorkingBytes = workingBytes};
        if (!acceptedFit(result, plan))
        {
            return failure(ErrorCode::EmptySelection, plate_solver_detail::kFitOperation,
                           "the fitted plate solution does not meet the requested match and RMS limits");
        }
        return result;
    }
    catch (const ContextAllocationFailure& allocation)
    {
        return allocation.status();
    }
    catch (const std::bad_alloc&)
    {
        return Status::failure(ErrorCode::AllocationFailed);
    }
}

namespace plate_solver_detail
{

Result<PlateSolveRefinementResult> refineAtCenter(const ExecutionContext& execution, const SkyCatalog& catalog,
                                                  const PlateSolveInput& input, const PlateSolvePlan& plan,
                                                  const SkyCoordinate& initialCenter, RunCounters* counters)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, kRefineOperation,
                       "plate-solve refinement requires a live execution context");
    }
    if (!plan.valid())
    {
        return failure(ErrorCode::NotInitialized, kRefineOperation, "plate-solve refinement requires a checked plan");
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, kRefineOperation,
                       "plate-solve refinement requires a built sky catalog");
    }
    if (!isSkyCoordinate(initialCenter))
    {
        return parameterFailure(kRefineOperation, "initialCenter",
                                "the refinement centre must be a finite sky direction");
    }

    SkyCoordinate currentCenter = initialCenter;
    std::optional<PlateSolveFitResult> best;
    std::uint32_t iterations = 0u;
    bool converged = false;

    const auto fitAtCurrent = [&]() -> Result<PlateSolveFitResult> {
        if (counters != nullptr)
        {
            ++counters->catalogQueries;
        }
        Result<PlateSolveCatalogSearchResult> search =
            searchPlateSolveCatalog(execution, catalog, input, plan, currentCenter);
        if (!search.hasValue())
        {
            return search.status();
        }
        if (counters != nullptr)
        {
            counters->examinedCatalogEntries += search->examinedCatalogEntries;
            counters->admittedCatalogEntries += search->admittedCatalogEntries;
            ++counters->matchingAttempts;
        }
        Result<PlateSolveMatchResult> matched = matchPlateSolveStars(execution, input, *search, plan);
        if (!matched.hasValue())
        {
            return matched.status();
        }
        if (counters != nullptr)
        {
            counters->peakWorkingBytes = (std::max)(counters->peakWorkingBytes, matched->peakWorkingBytes);
            ++counters->fittingAttempts;
        }
        Result<PlateSolveFitResult> fit = fitPlateSolveModel(execution, *matched, currentCenter, plan);
        if (fit.hasValue() && counters != nullptr)
        {
            counters->peakWorkingBytes = (std::max)(counters->peakWorkingBytes, fit->peakWorkingBytes);
        }
        return fit;
    };

    for (std::uint32_t iteration = 0u; iteration < plan.request().maximumRefinementIterations; ++iteration)
    {
        const Status cancellation = execution.checkCancellation();
        if (cancellation.failed())
        {
            return cancelledFailure(kRefineOperation, cancellation);
        }
        Result<PlateSolveFitResult> fit = fitAtCurrent();
        if (!fit.hasValue())
        {
            return fit.status();
        }
        ++iterations;
        const Result<SkyCoordinate> solvedCenter = fit->solution.fieldCenter();
        if (!solvedCenter.hasValue())
        {
            return solvedCenter.status();
        }
        const double movementArcsec = angularSeparationDegrees(currentCenter, *solvedCenter) * 3600.0;
        currentCenter = *solvedCenter;
        best = std::move(*fit);
        if (movementArcsec <= plan.request().convergenceToleranceArcsec)
        {
            converged = true;
            break;
        }
    }
    if (!best.has_value())
    {
        return failure(ErrorCode::EmptySelection, kRefineOperation, "the trial centre produced no plate-solve fit");
    }

    Result<PlateSolveFitResult> recentered = fitAtCurrent();
    if (!recentered.hasValue())
    {
        return recentered.status();
    }
    best = std::move(*recentered);
    return PlateSolveRefinementResult{.fit = std::move(*best),
                                      .initialCenter = initialCenter,
                                      .finalCenter = currentCenter,
                                      .iterations = iterations,
                                      .converged = converged};
}

} // namespace plate_solver_detail

Result<PlateSolveRefinementResult> refinePlateSolveAtCenter(const ExecutionContext& execution,
                                                            const SkyCatalog& catalog, const PlateSolveInput& input,
                                                            const PlateSolvePlan& plan,
                                                            const SkyCoordinate& initialCenter)
{
    return plate_solver_detail::refineAtCenter(execution, catalog, input, plan, initialCenter, nullptr);
}

} // namespace an
