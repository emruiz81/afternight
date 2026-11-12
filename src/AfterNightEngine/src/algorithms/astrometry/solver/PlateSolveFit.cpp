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

#include <AfterNightEngine/Algorithms/Astrometry/GnomonicProjection.h>
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
#include <numeric>
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
template <std::size_t Terms>
[[nodiscard]] bool solveNormalEquations(std::array<std::array<long double, Terms + 1u>, Terms>& augmented,
                                        std::size_t count, std::array<double, Terms>& solution) noexcept
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

/// The most terms of the polynomial the linear model is refined with: every monomial of degree zero through four.
constexpr std::size_t kMaximumJointTerms = 15u;

/// How often the linear model is refined; the correction of the second round is already far below a pixel.
constexpr std::uint32_t kLinearRefinementRounds = 3u;

/// Refines the linear model under the distortion its correspondences show.
///
/// The robust fit finds the straight model that best fits the positions as they are, and a distortion that grows
/// with the distance from the centre - any lens's - is partly taken into that model as a change of scale. The
/// distortion polynomial, which has no constant and no linear term, cannot give that part back, so fitted after
/// the fact it leaves a residual that grows with the area the stars cover. Here the two are fitted together: what
/// the linear model leaves is fitted by a polynomial of every degree from zero to the distortion's order, and its
/// constant and linear terms - the part of the residual a linear model can take - are folded into the linear
/// model. With `t` a star's tangent coordinates, `p(t) = S t + r` the linear model, `u` the star's offset from the
/// reference pixel `r`, and the fitted residual `p(t) - (r + u) = c + C u + A(u)`, the refined model is
/// `S' = (I + C)^-1 S` and `r' = r - (I + C)^-1 c`. The offsets are then measured from a reference pixel that moved
/// by a fraction of a pixel, so the fit is repeated from the refined model; the distortion itself is fitted
/// afterwards, by `fitSip`, against the model this returns.
///
/// @param execution the operation, for its cancellation.
/// @param transform the robust linear fit, tangent degrees to pixels.
/// @param matches the inliers.
/// @param plan the checked plan, for the image dimensions.
/// @return the refined model; the model as it stood when a round's system is singular or its correction is not
///         finite; or the cancellation.
[[nodiscard]] Result<StarTransform> refineLinearUnderDistortion(const ExecutionContext& execution,
                                                                StarTransform transform,
                                                                std::span<const PlateSolveMatch> matches,
                                                                const PlateSolvePlan& plan)
{
    const std::uint32_t order = chooseSipOrder(matches.size());
    if (order == 0u)
    {
        return transform;
    }
    std::array<SipTerm, kMaximumJointTerms> terms{};
    std::size_t termCount = 0u;
    for (std::uint32_t degree = 0u; degree <= order; ++degree)
    {
        for (std::uint32_t yPower = 0u; yPower <= degree; ++yPower)
        {
            terms[termCount++] = SipTerm{.xPower = degree - yPower, .yPower = yPower};
        }
    }
    const double normalization =
        (std::max)(static_cast<double>(plan.request().imageWidth), static_cast<double>(plan.request().imageHeight));

    for (std::uint32_t round = 0u; round < kLinearRefinementRounds; ++round)
    {
        std::array<std::array<long double, kMaximumJointTerms + 1u>, kMaximumJointTerms> systemX{};
        std::array<std::array<long double, kMaximumJointTerms + 1u>, kMaximumJointTerms> systemY{};
        const double a = transform.sourceToReference[0u];
        const double b = transform.sourceToReference[1u];
        const double c = transform.sourceToReference[3u];
        const double d = transform.sourceToReference[4u];
        const double referenceX = transform.sourceToReference[2u];
        const double referenceY = transform.sourceToReference[5u];
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
            const double targetX =
                a * match.tangentXDegrees + b * match.tangentYDegrees + referenceX - match.imagePosition.x;
            const double targetY =
                c * match.tangentXDegrees + d * match.tangentYDegrees + referenceY - match.imagePosition.y;
            const double u = (match.imagePosition.x - referenceX) / normalization;
            const double v = (match.imagePosition.y - referenceY) / normalization;
            std::array<long double, kMaximumJointTerms> design{};
            for (std::size_t column = 0u; column < termCount; ++column)
            {
                design[column] = static_cast<long double>(integerPower(u, terms[column].xPower) *
                                                          integerPower(v, terms[column].yPower));
            }
            for (std::size_t left = 0u; left < termCount; ++left)
            {
                systemX[left][termCount] += design[left] * static_cast<long double>(targetX);
                systemY[left][termCount] += design[left] * static_cast<long double>(targetY);
                for (std::size_t right = 0u; right < termCount; ++right)
                {
                    systemX[left][right] += design[left] * design[right];
                    systemY[left][right] += design[left] * design[right];
                }
            }
        }
        std::array<double, kMaximumJointTerms> fittedX{};
        std::array<double, kMaximumJointTerms> fittedY{};
        if (!solveNormalEquations(systemX, termCount, fittedX) || !solveNormalEquations(systemY, termCount, fittedY))
        {
            return transform;
        }
        // The terms are in ascending degree: the constant, then the horizontal and the vertical linear term, whose
        // coefficients are of the offsets divided by the normalization.
        const double constantX = fittedX[0u];
        const double constantY = fittedY[0u];
        const double m11 = 1.0 + fittedX[1u] / normalization;
        const double m12 = fittedX[2u] / normalization;
        const double m21 = fittedY[1u] / normalization;
        const double m22 = 1.0 + fittedY[2u] / normalization;
        const double determinant = m11 * m22 - m12 * m21;
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-12)
        {
            return transform;
        }
        const double i11 = m22 / determinant;
        const double i12 = -m12 / determinant;
        const double i21 = -m21 / determinant;
        const double i22 = m11 / determinant;
        StarTransform refined = transform;
        refined.sourceToReference[0u] = i11 * a + i12 * c;
        refined.sourceToReference[1u] = i11 * b + i12 * d;
        refined.sourceToReference[3u] = i21 * a + i22 * c;
        refined.sourceToReference[4u] = i21 * b + i22 * d;
        refined.sourceToReference[2u] = referenceX - (i11 * constantX + i12 * constantY);
        refined.sourceToReference[5u] = referenceY - (i21 * constantX + i22 * constantY);
        if (!std::all_of(refined.sourceToReference.begin(), refined.sourceToReference.end(),
                         [](double entry) { return std::isfinite(entry); }))
        {
            return transform;
        }
        transform = refined;
    }
    return transform;
}

/// How a fit treats the correspondences it is given.
struct FitChoices
{
    /// The distance a correspondence may lie from the robust linear fit and still be kept, in pixels; nothing for
    /// the request's residual limit.
    std::optional<double> linearReach{};

    /// Whether the linear model is refined under the distortion before the distortion is fitted
    /// (`refineLinearUnderDistortion`).
    bool refineLinear = false;
};

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

/// The fit behind `fitPlateSolveModel`, with the distance a correspondence may lie from the linear model and still
/// be kept stated apart from the request's residual limit.
///
/// The robust linear fit keeps the correspondences within `linearReach` of it, and the distortion is then fitted to
/// those. Correspondences a triangle match proposed are held to the residual limit itself, since a wrong one is
/// only told from a right one by it. Correspondences taken by position under a solution that already places the
/// catalog - the fit over the frame - are right to within a few pixels of the whole model, so the linear step may
/// keep the ones a real distortion carries farther from a straight fit than the limit, which is what lets the
/// distortion be fitted to them. The finished fit is held to the request's match and residual limits either way.
///
/// @param execution operation policy and engine-context lease.
/// @param matches the correspondences.
/// @param trialCenter the tangent point.
/// @param plan checked plate-solve plan.
/// @param choices the linear fit's inlier distance, and whether the linear model is refined under the distortion.
/// @return the fit, with the failures `fitPlateSolveModel` documents.
[[nodiscard]] Result<PlateSolveFitResult> fitModel(const ExecutionContext& execution,
                                                   const PlateSolveMatchResult& matches,
                                                   const SkyCoordinate& trialCenter, const PlateSolvePlan& plan,
                                                   const FitChoices& choices)
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
                                              .inlierThreshold =
                                                  choices.linearReach.value_or(plan.request().maximumRmsResidualPixels),
                                              .minimumInlierFraction = 0.25,
                                              .ransacIterations = plan.request().ransacIterations,
                                              .seed = plan.request().deterministicSeed};
        Result<StarAlignmentResult> fitted = fitStarTransform(execution, tangent, image, fitRequest);
        if (!fitted.hasValue())
        {
            return fitted.status();
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

        StarTransform transform = fitted->transform;
        if (choices.refineLinear)
        {
            Result<StarTransform> refined = refineLinearUnderDistortion(execution, transform, retained, plan);
            if (!refined.hasValue())
            {
                return refined.status();
            }
            transform = *refined;
        }
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

} // namespace

Result<PlateSolveFitResult> fitPlateSolveModel(const ExecutionContext& execution, const PlateSolveMatchResult& matches,
                                               const SkyCoordinate& trialCenter, const PlateSolvePlan& plan)
{
    return fitModel(execution, matches, trialCenter, plan, FitChoices{});
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

namespace
{

/// A catalog point source of the cone about the final centre, as the fit over the frame uses it.
struct FrameCatalogStar
{
    /// Position in `SkyCatalog::entries()`.
    std::uint32_t catalogIndex = 0u;

    /// Catalog direction.
    SkyCoordinate coordinate{};

    /// Tangent-plane coordinates at the final centre, in degrees.
    double tangentXDegrees = 0.0;
    double tangentYDegrees = 0.0;
};

/// The most supplied stars, and the most catalog point sources, the fit over the frame sets side by side: more than
/// any detector's list of one frame holds, and a bound on the work a list of a million would ask for.
constexpr std::size_t kFrameExtensionImageStars = 8192u;
constexpr std::size_t kFrameExtensionCatalogStars = 32768u;

/// The most passes the fit over the frame makes, each from the solution of the one before.
constexpr std::uint32_t kFrameExtensionPasses = 5u;

/// The side of a cell of the grid the image stars are found through, in pixels.
constexpr double kFrameExtensionCellPixels = 32.0;

/// How far from where the solution places a catalog star an image star may lie and still be taken for it, in pixels:
/// four times the fit's residual, and no less and no more than these.
constexpr double kFrameExtensionMinimumReach = 3.0;
constexpr double kFrameExtensionMaximumReach = 8.0;

/// No catalog star, in the list of the catalog star nearest each image star.
constexpr std::uint32_t kNoCatalogStar = (std::numeric_limits<std::uint32_t>::max)();

} // namespace

Result<FrameExtension> extendOverFrame(const ExecutionContext& execution, const SkyCatalog& catalog,
                                       std::span<const PlateSolveStar> stars, const PlateSolveInput& input,
                                       const PlateSolvePlan& plan, PlateSolveRefinementResult refinement,
                                       RunCounters* counters)
{
    FrameExtension outcome{.refinement = std::move(refinement), .extended = false};
    const PlateSolveRequest& request = plan.request();
    const std::size_t considered = (std::min)(stars.size(), kFrameExtensionImageStars);
    const std::span<const PlateSolveStar> candidates = stars.first(considered);
    // A frame whose stars were kept over its whole extent was fitted over it, and one that supplies no star outside
    // the disc has nothing more to be fitted to.
    if (!keptOverMatchingDisc(input, plan) ||
        std::all_of(candidates.begin(), candidates.end(),
                    [&request](const PlateSolveStar& star) { return insideMatchingDisc(star.position, request); }))
    {
        return outcome;
    }
    MemoryResource* const memoryResource = execution.memoryResource();
    if (memoryResource == nullptr)
    {
        return outcome;
    }

    const SkyCoordinate center = outcome.refinement.finalCenter;
    if (counters != nullptr)
    {
        ++counters->catalogQueries;
    }
    const Result<CatalogQueryResult> queried =
        queryCatalogRegion(execution, catalog,
                           CatalogRegionQuery{.center = center,
                                              .radiusDegrees = plan.catalogQueryRadiusDegrees(),
                                              .magnitudeLimit = request.magnitudeLimit,
                                              .admitNamedEntriesWithoutMagnitude = false,
                                              .maximumResults = request.maximumCatalogCandidates});
    if (!queried.hasValue())
    {
        // The accepted solution stands when its catalog cannot be read again, unless the operation was stopped.
        if (queried.code() == ErrorCode::Cancelled)
        {
            return queried.status();
        }
        return outcome;
    }
    if (counters != nullptr)
    {
        counters->examinedCatalogEntries += queried->examinedCount;
        counters->admittedCatalogEntries += queried->admittedCount;
    }

    try
    {
        ExecutionMemory memory(*memoryResource);
        const auto stopped = [&execution]() { return execution.checkCancellation(); };

        // The catalog's point sources, brightest first, each on the final centre's tangent plane.
        std::pmr::vector<FrameCatalogStar> sources(&memory);
        sources.reserve((std::min)(queried->entryIndices.size(), kFrameExtensionCatalogStars));
        for (std::size_t position = 0u;
             position < queried->entryIndices.size() && sources.size() < kFrameExtensionCatalogStars; ++position)
        {
            if ((position & 255u) == 0u)
            {
                if (const Status cancellation = stopped(); cancellation.failed())
                {
                    return cancelledFailure(kRefineOperation, cancellation);
                }
            }
            const std::uint32_t index = queried->entryIndices[position];
            const CatalogEntry* entry = catalog.entry(index);
            if (entry == nullptr || entry->extent.has_value())
            {
                continue;
            }
            const Result<GnomonicCoordinate> tangent = projectGnomonic(entry->coordinate, center);
            if (!tangent.hasValue())
            {
                continue;
            }
            sources.push_back(FrameCatalogStar{.catalogIndex = index,
                                               .coordinate = entry->coordinate,
                                               .tangentXDegrees = tangent->xDegrees,
                                               .tangentYDegrees = tangent->yDegrees});
        }

        // The supplied stars, found through a grid of the frame: the stars of each cell, cell after cell.
        const double width = static_cast<double>(request.imageWidth);
        const double height = static_cast<double>(request.imageHeight);
        const std::size_t columns =
            (std::max)(std::size_t{1u}, static_cast<std::size_t>(std::ceil(width / kFrameExtensionCellPixels)));
        const std::size_t rows =
            (std::max)(std::size_t{1u}, static_cast<std::size_t>(std::ceil(height / kFrameExtensionCellPixels)));
        const auto column = [columns](double x) {
            return static_cast<std::size_t>(
                std::clamp(std::floor(x / kFrameExtensionCellPixels), 0.0, static_cast<double>(columns - 1u)));
        };
        const auto row = [rows](double y) {
            return static_cast<std::size_t>(
                std::clamp(std::floor(y / kFrameExtensionCellPixels), 0.0, static_cast<double>(rows - 1u)));
        };
        const auto inFrame = [width, height](const PixelCoordinate& position) {
            return std::isfinite(position.x) && std::isfinite(position.y) && position.x >= 0.0 && position.y >= 0.0 &&
                   position.x <= width && position.y <= height;
        };
        std::pmr::vector<std::uint32_t> cellStart(columns * rows + 1u, 0u, &memory);
        for (const PlateSolveStar& star : candidates)
        {
            if (inFrame(star.position))
            {
                ++cellStart[row(star.position.y) * columns + column(star.position.x) + 1u];
            }
        }
        std::partial_sum(cellStart.begin(), cellStart.end(), cellStart.begin());
        std::pmr::vector<std::uint32_t> cellStars(cellStart.back(), 0u, &memory);
        std::pmr::vector<std::uint32_t> filled(cellStart.begin(), cellStart.end() - 1, &memory);
        for (std::size_t index = 0u; index < candidates.size(); ++index)
        {
            const PixelCoordinate& position = candidates[index].position;
            if (inFrame(position))
            {
                cellStars[filled[row(position.y) * columns + column(position.x)]++] = static_cast<std::uint32_t>(index);
            }
        }

        std::pmr::vector<std::uint32_t> nearestSource(&memory);
        std::pmr::vector<double> nearestDistance(&memory);
        for (std::uint32_t pass = 0u; pass < kFrameExtensionPasses; ++pass)
        {
            const PlateSolveFitResult& fit = outcome.refinement.fit;
            const double reach =
                std::clamp(4.0 * fit.finalRmsResidualPixels, kFrameExtensionMinimumReach, kFrameExtensionMaximumReach);
            // Each catalog star is placed by the solution and offered to the image star nearest to where it falls;
            // an image star two of them reach keeps the nearer.
            nearestSource.assign(candidates.size(), kNoCatalogStar);
            nearestDistance.assign(candidates.size(), (std::numeric_limits<double>::infinity)());
            for (std::size_t source = 0u; source < sources.size(); ++source)
            {
                if ((source & 255u) == 0u)
                {
                    if (const Status cancellation = stopped(); cancellation.failed())
                    {
                        return cancelledFailure(kRefineOperation, cancellation);
                    }
                }
                const Result<PixelCoordinate> placed = fit.solution.worldToPixel(sources[source].coordinate);
                if (!placed.hasValue() || !std::isfinite(placed->x) || !std::isfinite(placed->y) ||
                    placed->x < -reach || placed->y < -reach || placed->x > width + reach || placed->y > height + reach)
                {
                    continue;
                }
                std::uint32_t nearest = kNoCatalogStar;
                double distance = reach;
                for (std::size_t cellRow = row(placed->y - reach); cellRow <= row(placed->y + reach); ++cellRow)
                {
                    for (std::size_t cellColumn = column(placed->x - reach); cellColumn <= column(placed->x + reach);
                         ++cellColumn)
                    {
                        const std::size_t cell = cellRow * columns + cellColumn;
                        for (std::uint32_t at = cellStart[cell]; at < cellStart[cell + 1u]; ++at)
                        {
                            const PixelCoordinate& position = candidates[cellStars[at]].position;
                            const double apart = std::hypot(position.x - placed->x, position.y - placed->y);
                            if (apart <= distance)
                            {
                                distance = apart;
                                nearest = cellStars[at];
                            }
                        }
                    }
                }
                if (nearest != kNoCatalogStar && distance < nearestDistance[nearest])
                {
                    nearestDistance[nearest] = distance;
                    nearestSource[nearest] = static_cast<std::uint32_t>(source);
                }
            }

            PlateSolveMatchResult matched;
            for (std::size_t index = 0u; index < candidates.size(); ++index)
            {
                if (nearestSource[index] == kNoCatalogStar)
                {
                    continue;
                }
                const FrameCatalogStar& source = sources[nearestSource[index]];
                matched.matches.push_back(PlateSolveMatch{.imageStarIndex = static_cast<std::uint32_t>(index),
                                                          .catalogIndex = source.catalogIndex,
                                                          .imagePosition = candidates[index].position,
                                                          .catalogCoordinate = source.coordinate,
                                                          .tangentXDegrees = source.tangentXDegrees,
                                                          .tangentYDegrees = source.tangentYDegrees,
                                                          .votes = 1u,
                                                          .confidence = 1.0,
                                                          .residualPixels = nearestDistance[index]});
            }
            // A pass that sets no more stars beside the catalog than the fit already holds has nothing to add.
            if (matched.matches.size() <= fit.matches.size())
            {
                break;
            }
            matched.candidateMatches = static_cast<std::uint32_t>(matched.matches.size());
            if (counters != nullptr)
            {
                ++counters->fittingAttempts;
            }
            // These pairs were taken where the solution places the catalog, so the linear step keeps the ones a
            // distortion carries as far from a straight fit as the reach allows, and the linear model and the
            // distortion are fitted to them together.
            Result<PlateSolveFitResult> refit = fitModel(
                execution, matched, center, plan,
                FitChoices{.linearReach = (std::max)(request.maximumRmsResidualPixels, kFrameExtensionMaximumReach),
                           .refineLinear = true});
            if (!refit.hasValue())
            {
                if (refit.code() == ErrorCode::Cancelled)
                {
                    return refit.status();
                }
                break;
            }
            if (refit->matches.size() <= fit.matches.size())
            {
                break;
            }
            if (counters != nullptr)
            {
                counters->peakWorkingBytes =
                    (std::max)(counters->peakWorkingBytes, memory.peakBytes() + refit->peakWorkingBytes);
            }
            outcome.refinement.fit = std::move(*refit);
            outcome.extended = true;
        }
        return outcome;
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

} // namespace plate_solver_detail

Result<PlateSolveRefinementResult> refinePlateSolveAtCenter(const ExecutionContext& execution,
                                                            const SkyCatalog& catalog, const PlateSolveInput& input,
                                                            const PlateSolvePlan& plan,
                                                            const SkyCoordinate& initialCenter)
{
    return plate_solver_detail::refineAtCenter(execution, catalog, input, plan, initialCenter, nullptr);
}

} // namespace an
