// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deterministic complete plate-solve orchestration.
//
// The complete solve plans once, prepares the image stars once, and then walks the planned
// sequence of trial centres in order, refining each through the same stages a caller can run one
// at a time. The walk is serial and its order is fixed by the plan, so the answer depends on
// neither the thread count nor timing: any parallelism lives inside the stages. A centre that simply
// does not match is skipped; any failure that says something is wrong with the call rather than
// with that centre ends the solve.

#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>

#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/astrometry/solver/PlateSolverInternal.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>

namespace an
{
namespace
{

using plate_solver_detail::failure;
using plate_solver_detail::RunCounters;
using plate_solver_detail::TrialRoute;

/// One trial centre's accepted refinement and where in the sequence it came from.
struct Candidate
{
    /// The accepted refinement.
    PlateSolveRefinementResult refinement;

    /// Which part of the sequence supplied the centre.
    TrialRoute route = TrialRoute::ExactHint;

    /// The centre's position in the planned sequence, the last tie break.
    std::uint32_t ordinal = 0u;
};

/// The sky direction of one trial centre.
///
/// A blind plan's ordinal is a ring-ordered HEALPix pixel whose centre is the trial. A hinted
/// plan's ordinal zero is the hint; ordinal `n` above it is sample `(n - 1) % samples` of ring
/// `(n - 1) / samples + 1`, placed on the celestial sphere at that ring's angular share of the search
/// radius and at an angle turning from east toward north.
///
/// @param plan the checked plan.
/// @param ordinal the position in the sequence, below `plan.trialCenterCount()`.
/// @return the direction, or the HEALPix or inverse-projection failure.
[[nodiscard]] Result<SkyCoordinate> trialCenter(const PlateSolvePlan& plan, std::uint32_t ordinal)
{
    if (!plan.request().initialCenter.has_value())
    {
        return healpixPixelCenter(
            HealpixPixelIndex{.grid = HealpixGrid{.order = plan.blindHealpixOrder(), .ordering = HealpixOrdering::Ring},
                              .value = ordinal});
    }
    if (ordinal == 0u)
    {
        return *plan.request().initialCenter;
    }

    const std::uint32_t zeroBased = ordinal - 1u;
    const std::uint32_t ring = zeroBased / plan.request().nearSearchSamplesPerRing + 1u;
    const std::uint32_t sample = zeroBased % plan.request().nearSearchSamplesPerRing;
    const double radius = plan.request().searchRadiusDegrees * static_cast<double>(ring) /
                          static_cast<double>(plan.request().nearSearchRings);
    const double angle = 2.0 * std::numbers::pi * static_cast<double>(sample) /
                         static_cast<double>(plan.request().nearSearchSamplesPerRing);
    const SkyCoordinate hint = *plan.request().initialCenter;
    const double radians = std::numbers::pi / 180.0;
    const double distance = radius * radians;
    const double ra = hint.rightAscensionDegrees * radians;
    const double dec = hint.declinationDegrees * radians;
    const double east = std::sin(distance) * std::cos(angle);
    const double north = std::sin(distance) * std::sin(angle);
    const double radial = std::cos(distance);
    const double x = radial * std::cos(dec) * std::cos(ra) - east * std::sin(ra) - north * std::sin(dec) * std::cos(ra);
    const double y = radial * std::cos(dec) * std::sin(ra) + east * std::cos(ra) - north * std::sin(dec) * std::sin(ra);
    const double z = radial * std::sin(dec) + north * std::cos(dec);
    double longitude = std::atan2(y, x) / radians;
    if (longitude < 0.0)
        longitude += 360.0;
    return SkyCoordinate{.rightAscensionDegrees = longitude,
                         .declinationDegrees = std::atan2(z, std::hypot(x, y)) / radians};
}

/// Which part of the sequence one ordinal belongs to.
///
/// @param plan the checked plan.
/// @param ordinal the position in the sequence.
/// @return the route.
[[nodiscard]] TrialRoute routeFor(const PlateSolvePlan& plan, std::uint32_t ordinal) noexcept
{
    if (!plan.request().initialCenter.has_value())
    {
        return TrialRoute::BlindSearch;
    }
    return ordinal == 0u ? TrialRoute::ExactHint : TrialRoute::NearSearch;
}

/// Whether one accepted candidate outranks another.
///
/// More matches first, then the lower final residual, then the earlier trial, so the ranking is a
/// total order and the selected answer never depends on anything but the sequence.
///
/// @param candidate the new candidate.
/// @param incumbent the best so far.
/// @return true when `candidate` should replace `incumbent`.
[[nodiscard]] bool better(const Candidate& candidate, const Candidate& incumbent) noexcept
{
    if (candidate.refinement.fit.matches.size() != incumbent.refinement.fit.matches.size())
    {
        return candidate.refinement.fit.matches.size() > incumbent.refinement.fit.matches.size();
    }
    if (candidate.refinement.fit.finalRmsResidualPixels != incumbent.refinement.fit.finalRmsResidualPixels)
    {
        return candidate.refinement.fit.finalRmsResidualPixels < incumbent.refinement.fit.finalRmsResidualPixels;
    }
    return candidate.ordinal < incumbent.ordinal;
}

/// Whether the best candidate so far is strong enough to end the search early.
///
/// Twelve matches at a final residual of at most one pixel ends the walk without trying the rest
/// of the sequence, which for a blind search is most of the sky.
///
/// @param candidate the best candidate so far.
/// @return true when the search should stop here.
[[nodiscard]] bool fastAccept(const Candidate& candidate) noexcept
{
    return candidate.refinement.fit.matches.size() >= 12u && candidate.refinement.fit.finalRmsResidualPixels <= 1.0;
}

/// Builds the complete answer from the selected candidate and the run's totals.
///
/// @param candidate the selected candidate, whose matches are moved out.
/// @param input the prepared image stars.
/// @param plan the checked plan.
/// @param execution the operation, for its admitted concurrency.
/// @param counters the work every trial centre added.
/// @param examinedTrialCenters trial centres attempted, the selected one included.
/// @return the solution, its matches and its diagnostics.
[[nodiscard]] PlateSolveResult finish(Candidate candidate, const PlateSolveInput& input, const PlateSolvePlan& plan,
                                      const ExecutionContext& execution, const RunCounters& counters,
                                      std::uint32_t examinedTrialCenters)
{
    PlateSolveDiagnostics diagnostics{.suppliedImageStars = input.suppliedStarCount,
                                      .selectedImageStars = static_cast<std::uint32_t>(input.stars.size()),
                                      .plannedTrialCenters = plan.trialCenterCount(),
                                      .examinedTrialCenters = examinedTrialCenters,
                                      .catalogQueries = counters.catalogQueries,
                                      .examinedCatalogEntries = counters.examinedCatalogEntries,
                                      .admittedCatalogEntries = counters.admittedCatalogEntries,
                                      .matchingAttempts = counters.matchingAttempts,
                                      .fittingAttempts = counters.fittingAttempts,
                                      .refinementIterations = candidate.refinement.iterations,
                                      .matchedStars =
                                          static_cast<std::uint32_t>(candidate.refinement.fit.matches.size()),
                                      .rejectedOutliers = candidate.refinement.fit.rejectedOutliers,
                                      .peakWorkingBytes = counters.peakWorkingBytes,
                                      .admittedConcurrency = execution.admittedConcurrency(),
                                      .usedExactHint = candidate.route == TrialRoute::ExactHint,
                                      .usedNearSearch = candidate.route == TrialRoute::NearSearch,
                                      .usedBlindSearch = candidate.route == TrialRoute::BlindSearch,
                                      .converged = candidate.refinement.converged,
                                      .fittedSipDistortion = candidate.refinement.fit.sipOrder != 0u};
    return PlateSolveResult{.solution = candidate.refinement.fit.solution,
                            .matches = std::move(candidate.refinement.fit.matches),
                            .diagnostics = diagnostics};
}

/// Whether a trial centre's failure means only that this centre did not solve.
///
/// A centre that finds too few stars, singular geometry or a direction off its tangent plane is an
/// ordinary miss, and the search moves on. Every other code - cancellation, a budget, an invalid
/// argument, a catalog failure - says something about the call rather than this centre, so it ends
/// the solve.
///
/// @param code the trial's failure code.
/// @return true when the search should continue with the next centre.
[[nodiscard]] bool recoverableTrialFailure(ErrorCode code) noexcept
{
    return code == ErrorCode::EmptySelection || code == ErrorCode::InvalidData || code == ErrorCode::OutOfRange;
}

} // namespace

Result<PlateSolveResult> solvePlate(const ExecutionContext& execution, const SkyCatalog& catalog,
                                    std::span<const PlateSolveStar> stars, const PlateSolveRequest& request)
{
    if (!execution.valid())
    {
        return failure(ErrorCode::NotInitialized, plate_solver_detail::kSolveOperation,
                       "plate solving requires a live execution context");
    }
    if (!catalog.valid())
    {
        return failure(ErrorCode::NotInitialized, plate_solver_detail::kSolveOperation,
                       "plate solving requires a built sky catalog");
    }
    const Result<PlateSolvePlan> plan = planPlateSolve(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    Result<PlateSolveInput> input = preparePlateSolveInput(execution, stars, *plan);
    if (!input.hasValue())
    {
        return input.status();
    }

    RunCounters counters;
    std::optional<Candidate> best;
    std::uint32_t examined = 0u;
    execution.reportProgress(0u, plan->trialCenterCount(), "plate solve: trial centres");

    for (std::uint32_t ordinal = 0u; ordinal < plan->trialCenterCount(); ++ordinal)
    {
        const Status cancellation = execution.checkCancellation();
        if (cancellation.failed())
        {
            return plate_solver_detail::cancelledFailure(plate_solver_detail::kSolveOperation, cancellation);
        }
        const Result<SkyCoordinate> center = trialCenter(*plan, ordinal);
        ++examined;
        if (!center.hasValue())
        {
            if (!recoverableTrialFailure(center.code()))
            {
                return center.status();
            }
            execution.reportProgress(examined, plan->trialCenterCount(), "plate solve: trial centres");
            continue;
        }

        Result<PlateSolveRefinementResult> refined =
            plate_solver_detail::refineAtCenter(execution, catalog, *input, *plan, *center, &counters);
        execution.reportProgress(examined, plan->trialCenterCount(), "plate solve: trial centres");
        if (!refined.hasValue())
        {
            if (recoverableTrialFailure(refined.code()))
            {
                continue;
            }
            return refined.status();
        }

        Candidate candidate{.refinement = std::move(*refined), .route = routeFor(*plan, ordinal), .ordinal = ordinal};
        if (candidate.route == TrialRoute::ExactHint)
        {
            execution.reportDiagnostic(DiagnosticSeverity::Info, ErrorDomain::Algorithms,
                                       plate_solver_detail::kSolveOperation,
                                       "plate solving selected the exact supplied centre hint");
            return finish(std::move(candidate), *input, *plan, execution, counters, examined);
        }
        if (!best.has_value() || better(candidate, *best))
        {
            best = std::move(candidate);
        }
        if (best.has_value() && fastAccept(*best))
        {
            execution.reportDiagnostic(
                DiagnosticSeverity::Info, ErrorDomain::Algorithms, plate_solver_detail::kSolveOperation,
                best->route == TrialRoute::BlindSearch ? "plate solving selected a blind HEALPix centre"
                                                       : "plate solving selected an offset near-search centre");
            return finish(std::move(*best), *input, *plan, execution, counters, examined);
        }
    }

    if (!best.has_value())
    {
        execution.reportDiagnostic(DiagnosticSeverity::Debug, ErrorDomain::Algorithms,
                                   plate_solver_detail::kSolveOperation,
                                   "plate solving exhausted its deterministic trial-centre sequence");
        return Status::failure(
            ErrorCode::EmptySelection,
            StatusDetail{.message = "no trial centre produced a plate solution within the requested limits",
                         .operation = plate_solver_detail::kSolveOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Completed, examined),
                                     StatusContextField::ofCount(ContextKey::Total, plan->trialCenterCount())}});
    }

    execution.reportDiagnostic(DiagnosticSeverity::Info, ErrorDomain::Algorithms, plate_solver_detail::kSolveOperation,
                               best->route == TrialRoute::BlindSearch
                                   ? "plate solving selected a blind HEALPix centre"
                                   : "plate solving selected an offset near-search centre");
    return finish(std::move(*best), *input, *plan, execution, counters, examined);
}

} // namespace an
