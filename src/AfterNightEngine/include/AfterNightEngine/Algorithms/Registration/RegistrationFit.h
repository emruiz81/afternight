// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Turning corresponding positions into a registration transform.
///
/// Matching two star fields is `Algorithms/Stars/StarAlignment.h`'s work and produces a linear
/// model. This header is the step after it and the step beside it: it fits the linear model to
/// correspondences that are already known to correspond, and it adds the polynomial correction that
/// a linear model cannot express - the field curvature left over once translation, rotation, scale
/// and perspective have been removed.
///
/// The two entry points are separate because their inputs are. A control-point fit is handed pairs
/// a person curated, so every pair is an inlier and the answer is a plain least-squares solve; a
/// polynomial refinement is handed a linear model and the same pairs and re-weights them itself,
/// because a polynomial with fifteen coefficients per axis will otherwise interpolate a mismatched
/// pair rather than reject it.
///
/// Positions are engine pixel coordinates, as everywhere in this domain: the centre of the first
/// pixel is (0.5, 0.5). The fitted transform therefore maps in that convention too, and
/// `StarAlignmentPoint` positions need no conversion to be fitted here.

#pragma once

#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>

namespace an
{

class ExecutionContext;

/// Whether and how a polynomial correction is added to the linear fit.
enum class RegistrationPolynomialPolicy : std::uint8_t
{
    /// Fit the linear model and stop.
    LinearOnly = 0u,

    /// Fit the linear model, then fit a polynomial of the requested order on top of it.
    ///
    /// The polynomial is kept when it can be fitted at all. A caller asking for an order has
    /// decided the field needs it.
    StatedOrder,

    /// Try increasing orders and keep the first that earns its place.
    ///
    /// A polynomial is attempted only when the linear residual is above
    /// `automaticLinearResidualPixels`, only at an order the correspondence count supports, and is
    /// kept only when its residual is at most `automaticImprovementFactor` of the linear one and its
    /// trimming left at least `automaticRegistrationPolynomialMinimumPoints` pairs. A
    /// correction that does not measurably improve the fit is a correction that is describing
    /// noise.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AutomaticOrder
};

/// Whether a value names a polynomial policy this build implements.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param policy the value to test.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isRegistrationPolynomialPolicy(RegistrationPolynomialPolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(RegistrationPolynomialPolicy::AutomaticOrder);
}

/// The policy's own name.
///
/// Thread-safe and allocation-free.
///
/// @param policy the policy to name. A value outside the enumeration is named
///        `"UnknownRegistrationPolynomialPolicy"`.
/// @return a null-terminated literal compiled into this library, which outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
registrationPolynomialPolicyName(RegistrationPolynomialPolicy policy) noexcept;

/// The fewest correspondences a linear model may be fitted from.
///
/// Above the mathematical minimum on purpose: a similarity needs two pairs and an affine three, but
/// a fit from exactly as many pairs as unknowns passes through every one of them and reports a
/// residual of zero whatever the pairs are worth.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the linear kind. A polynomial kind reports the count its linear stage needs, and an
///        unknown value reports zero.
/// @return the required correspondence count.
[[nodiscard]] constexpr std::uint32_t minimumRegistrationControlPoints(const RegistrationTransformKind kind) noexcept
{
    switch (kind)
    {
    case RegistrationTransformKind::Similarity:
    case RegistrationTransformKind::Affine:
    case RegistrationTransformKind::Polynomial2:
    case RegistrationTransformKind::Polynomial3:
    case RegistrationTransformKind::Polynomial4:
        return 3u;
    case RegistrationTransformKind::Projective:
        return 4u;
    }
    return 0u;
}

/// The fewest correspondences an automatically selected polynomial order may be chosen from.
///
/// Ten pairs for a second-order correction, sixteen for a third and twenty-five for a fourth,
/// against term counts of six, ten and fifteen. The margin is what stops the automatic route from
/// adopting an order that merely has enough unknowns to reproduce its own input.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param order the polynomial order.
/// @return the required count, or zero for an order outside
///         `[kMinimumRegistrationPolynomialOrder, kMaximumRegistrationPolynomialOrder]`.
[[nodiscard]] constexpr std::uint32_t automaticRegistrationPolynomialMinimumPoints(const std::uint32_t order) noexcept
{
    switch (order)
    {
    case 2u:
        return 10u;
    case 3u:
        return 16u;
    case 4u:
        return 25u;
    default:
        return 0u;
    }
}

/// The most correspondences one fit accepts.
///
/// A frame's detected star ceiling, which is the largest correspondence set any caller in this
/// engine produces.
inline constexpr std::uint32_t kMaximumRegistrationCorrespondences = 200000u;

/// How many times a polynomial fit is solved while outliers are trimmed.
///
/// Each pass solves on the retained pairs; every pass but the last then drops the pairs whose
/// residual exceeds three times the upper-quartile residual or a quarter of a pixel, whichever is
/// larger. The loop stops early when a pass drops nothing, when dropping would leave fewer pairs
/// than the order's term count, or when no more than two pairs above the term count remain.
inline constexpr std::uint32_t kRegistrationPolynomialRefinementPasses = 4u;

/// Every decision made while fitting one correspondence set.
struct RegistrationFitRequest
{
    /// Which linear model to fit. A polynomial kind is refused here: the order is stated by
    /// `polynomialPolicy` and `polynomialOrder`, so one request cannot say two different things.
    RegistrationTransformKind linearKind = RegistrationTransformKind::Affine;

    /// Whether a polynomial correction follows, and how its order is chosen.
    RegistrationPolynomialPolicy polynomialPolicy = RegistrationPolynomialPolicy::LinearOnly;

    /// The order used under `RegistrationPolynomialPolicy::StatedOrder`, in
    /// `[kMinimumRegistrationPolynomialOrder, kMaximumRegistrationPolynomialOrder]`. Checked under
    /// every policy and used by that one alone.
    std::uint32_t polynomialOrder = kMinimumRegistrationPolynomialOrder;

    /// The position the polynomial's normalization measures offsets from.
    PixelCoordinate normalizationOrigin{};

    /// The divisor the polynomial's coordinates are normalized by, finite and nonzero on both axes.
    ///
    /// The default performs no normalization, which is only adequate for a second-order fit over a
    /// small field. Pass the frame's extent: a fourth power of a coordinate in the thousands
    /// otherwise dominates the conditioning of the system that produced it.
    PixelCoordinate normalizationScale{1.0, 1.0};

    /// The linear residual, in pixels, above which the automatic policy tries a polynomial at all.
    /// Finite and not negative.
    double automaticLinearResidualPixels = 0.5;

    /// The share of the linear residual an automatic polynomial must reach to be kept, in (0, 1].
    double automaticImprovementFactor = 0.8;

    /// Whether two requests state every decision identically.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationFitRequest& left,
                                                   const RegistrationFitRequest& right) noexcept = default;
};

/// The fitted model and what the fit measured.
struct RegistrationFitResult
{
    /// The fitted source-to-reference transform.
    RegistrationTransform transform{};

    /// How many correspondences were supplied.
    std::uint32_t correspondenceCount = 0u;

    /// Root mean square residual of the linear stage over every correspondence, in pixels.
    ///
    /// Always measured, including when a polynomial is kept, because it is what says whether the
    /// polynomial was needed.
    double linearRootMeanSquareResidual = 0.0;

    /// Root mean square residual of the returned transform over the pairs it was fitted from.
    double rootMeanSquareResidual = 0.0;

    /// The largest residual among those pairs, in pixels.
    double maximumResidual = 0.0;

    /// The order of the kept polynomial, or zero when none was kept.
    std::uint32_t polynomialOrder = 0u;

    /// How many correspondences the kept polynomial was finally fitted from.
    ///
    /// Below `correspondenceCount` when the trimming passes dropped pairs. Zero when no polynomial
    /// was kept.
    std::uint32_t polynomialInliers = 0u;

    /// How many trimming passes the kept polynomial's fit ran.
    std::uint32_t refinementPasses = 0u;

    /// Polynomial orders the automatic policy considered and did not keep.
    ///
    /// Counts an order skipped for too few correspondences as well as one fitted and judged not to
    /// improve the residual enough, and is zero for the other policies. It is the difference
    /// between "the field is not curved" and "the field is curved and there were not enough stars
    /// to say so".
    std::uint32_t rejectedPolynomialOrders = 0u;

    /// Whether two results contain exactly the same figures.
    ///
    /// @param left one result.
    /// @param right the other.
    /// @return true when the transform and every measure are equal.
    [[nodiscard]] friend constexpr bool operator==(const RegistrationFitResult& left,
                                                   const RegistrationFitResult& right) noexcept = default;
};

/// Fits a polynomial correction on top of a linear model.
///
/// The linear model maps each source position, the polynomial is fitted from those linear outputs
/// to the reference positions in normalized coordinates, and up to
/// `kRegistrationPolynomialRefinementPasses` solves trim, between them, the pairs whose residual
/// exceeds three times the upper-quartile residual or a quarter of a pixel, whichever is larger. A
/// pass that would leave fewer pairs than the term count, or that drops nothing, ends the loop and
/// keeps the current fit. So does running out of passes: the last fit is kept, and
/// `polynomialInliers` reports how many pairs it describes.
///
/// The normal equations are accumulated in extended precision and solved by Gauss-Jordan
/// elimination with partial pivoting; the coordinate normalization is what keeps that system
/// conditioned, which is why it is stated rather than assumed.
///
/// Deterministic: the retained set, the coefficients and every residual depend only on the inputs,
/// not on the thread count.
///
/// @param execution operation policy and engine-context lease.
/// @param source source-frame positions.
/// @param reference reference-frame positions, corresponding by index and exactly as long.
/// @param linear the already-fitted linear model, which must carry no polynomial of its own.
/// @param request the order, normalization and thresholds; its linear kind is not used.
/// @return the fitted transform and its measures; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for an unknown policy, an order outside its range, a
///         non-finite or zero normalization, an automatic threshold outside its range, or a linear
///         seed carrying a polynomial; `ErrorCode::IncompatibleArguments` for spans of different
///         lengths; `ErrorCode::LimitExceeded` above `kMaximumRegistrationCorrespondences`;
///         `ErrorCode::EmptySelection` for fewer correspondences than the order's term count;
///         `ErrorCode::InvalidData` for a non-finite position, a position the linear model does not
///         map, or a system with no solution; or a memory-budget or cancellation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationFitResult>
fitRegistrationPolynomial(const ExecutionContext& execution, std::span<const PixelCoordinate> source,
                          std::span<const PixelCoordinate> reference, const RegistrationTransform& linear,
                          const RegistrationFitRequest& request);

/// Fits a registration transform to correspondences that already correspond.
///
/// The linear stage is a least-squares fit of the requested model over every pair - these are
/// curated control points or star matches something else already accepted, so nothing is rejected
/// here. The polynomial stage then runs according to the request's policy.
///
/// Degenerate inputs are refused by name rather than producing a matrix: a similarity needs two
/// distinct positions on each side and an affine or projective model needs three that are not
/// collinear, and a set that does not have them is `ErrorCode::InvalidData`.
///
/// Deterministic, and independent of the thread count.
///
/// @param execution operation policy and engine-context lease.
/// @param source source-frame positions.
/// @param reference reference-frame positions, corresponding by index and exactly as long.
/// @param request the model, polynomial policy, normalization and thresholds.
/// @return the fitted transform and its measures; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for a polynomial linear kind, an unknown kind or policy, an
///         order outside its range, a non-finite or zero normalization, or a threshold outside its
///         range; `ErrorCode::IncompatibleArguments` for spans of different lengths;
///         `ErrorCode::LimitExceeded` above `kMaximumRegistrationCorrespondences`;
///         `ErrorCode::EmptySelection` for fewer correspondences than
///         `minimumRegistrationControlPoints`; `ErrorCode::InvalidData` for a non-finite position,
///         a degenerate set, or a system with no solution; or a memory-budget or cancellation
///         failure. Under the automatic policy an order whose fit fails for any other reason is
///         counted in `rejectedPolynomialOrders` rather than returned.
[[nodiscard]] AFTERNIGHTENGINE_API Result<RegistrationFitResult>
fitRegistrationTransform(const ExecutionContext& execution, std::span<const PixelCoordinate> source,
                         std::span<const PixelCoordinate> reference, const RegistrationFitRequest& request = {});

} // namespace an
