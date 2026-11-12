// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - private coordination shared by plate-solver stages.
//
// The solver's public stages live in three translation units: planning, input, catalog search and
// matching in PlateSolveStages.cpp, fitting and refinement in PlateSolveFit.cpp, and the complete
// solve in PlateSolver.cpp. What they share is here: the operation names every failure carries, the
// failure builders, and the refinement entry point the complete solve calls with a counter record
// so its diagnostics can report the work every trial centre did.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/PlateSolver.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

/// The AfterNight engine namespace; `Core/LibraryInfo.h` states what it holds.
namespace an
{

class ExecutionContext;
class SkyCatalog;

/// The plate solver's private helpers, shared by its three translation units.
namespace plate_solver_detail
{

/// The operation name `planPlateSolve` and `estimatePlateSolveCatalogQueryRadius` failures carry.
inline constexpr const char* kPlanOperation = "an::planPlateSolve";

/// The operation name `preparePlateSolveInput` failures carry.
inline constexpr const char* kInputOperation = "an::preparePlateSolveInput";

/// The operation name `searchPlateSolveCatalog` failures carry.
inline constexpr const char* kSearchOperation = "an::searchPlateSolveCatalog";

/// The operation name `matchPlateSolveStars` failures carry.
inline constexpr const char* kMatchOperation = "an::matchPlateSolveStars";

/// The operation name `fitPlateSolveModel` failures carry.
inline constexpr const char* kFitOperation = "an::fitPlateSolveModel";

/// The operation name refinement failures carry, whichever entry point called it.
inline constexpr const char* kRefineOperation = "an::refinePlateSolveAtCenter";

/// The operation name `solvePlate`'s own failures carry.
inline constexpr const char* kSolveOperation = "an::solvePlate";

/// Accumulated stage work used by the complete solve's diagnostics.
///
/// Every trial centre's refinement adds to one record, so the totals cover the whole search and
/// not only the selected answer.
struct RunCounters
{
    /// Catalog searches started, failed ones included.
    std::uint32_t catalogQueries = 0u;

    /// Catalog candidates the successful searches' queries examined.
    std::uint64_t examinedCatalogEntries = 0u;

    /// Catalog entries the successful searches' queries admitted.
    std::uint64_t admittedCatalogEntries = 0u;

    /// Matching stages started, which is one per successful search.
    std::uint32_t matchingAttempts = 0u;

    /// Fitting stages started, which is one per successful match.
    std::uint32_t fittingAttempts = 0u;

    /// The largest working-byte peak a successful matching or fitting stage reported.
    std::uint64_t peakWorkingBytes = 0u;
};

/// Which part of the deterministic centre sequence supplied a candidate.
enum class TrialRoute : std::uint8_t
{
    /// The first trial of a hinted solve, at the hint itself.
    ExactHint,

    /// A trial on one of the rings around the hint.
    NearSearch,

    /// A pixel centre of the blind HEALPix grid, when there is no hint.
    BlindSearch
};

/// The radius of the matching disc, in pixels: the largest circle about the image centre that lies inside the frame.
///
/// @param request the checked request, for the image dimensions.
/// @return half the shorter side.
[[nodiscard]] inline double matchingDiscRadius(const PlateSolveRequest& request) noexcept
{
    return 0.5 * static_cast<double>((std::min)(request.imageWidth, request.imageHeight));
}

/// Whether an image position lies inside the matching disc.
///
/// @param position the position, in engine pixel coordinates.
/// @param request the checked request, for the image dimensions.
/// @return true when it is no farther from the image centre than the disc's radius.
[[nodiscard]] inline bool insideMatchingDisc(const PixelCoordinate& position, const PlateSolveRequest& request) noexcept
{
    return std::hypot(position.x - 0.5 * static_cast<double>(request.imageWidth),
                      position.y - 0.5 * static_cast<double>(request.imageHeight)) <= matchingDiscRadius(request);
}

/// Whether the image's stars were kept over the matching disc: none of them lies outside it.
///
/// That is the case of a crowded frame, whose input keeps the disc's stars alone, and of a frame whose every star
/// happens to lie there. Either way the image has nothing outside the disc to set beside a catalog star, so the
/// catalog keeps the disc too; a frame with a star outside it keeps its whole extent, and so does the catalog.
///
/// @param input the prepared image stars.
/// @param plan the checked plan, for the image dimensions.
/// @return true when every prepared star lies inside the disc.
[[nodiscard]] inline bool keptOverMatchingDisc(const PlateSolveInput& input, const PlateSolvePlan& plan) noexcept
{
    return std::all_of(input.stars.begin(), input.stars.end(), [&plan](const PlateSolveStar& star) {
        return insideMatchingDisc(star.position, plan.request());
    });
}

/// An accepted refinement, refitted over the whole frame where that was possible.
struct FrameExtension
{
    /// The refinement: as it was given, or with its fit replaced by the one over the frame.
    PlateSolveRefinementResult refinement;

    /// Whether the fit was replaced. Its matches then name their stars among the stars supplied to the solve, not
    /// among the prepared ones.
    bool extended = false;
};

/// Refits an accepted solution over the whole frame when its image stars were kept over the matching disc alone.
///
/// A crowded frame is matched over its disc, so the fit a trial centre accepts is of the disc's stars, and the
/// distortion it retains is carried from there to the corners, well beyond the stars it was fitted to. Here the
/// catalog's point sources of the cone are placed by that solution, each is set beside the supplied star nearest
/// to where it falls - within a reach of a few pixels that follows the fit's residual - and the model is fitted
/// again to all of those pairs by `fitPlateSolveModel`. The fit is kept when it holds more stars than the one
/// before; a few passes carry it outward, each from the solution of the last, and the first that finds or keeps no
/// more ends them. A frame whose stars were kept over its whole extent, or that supplies none outside the disc, is
/// returned as it came.
///
/// @param execution operation policy and engine-context lease.
/// @param catalog immutable catalog to search.
/// @param stars the stars supplied to the solve, in brightness order.
/// @param input the prepared image stars the refinement was made from.
/// @param plan checked plate-solve plan.
/// @param refinement the accepted refinement.
/// @param counters the complete solve's running totals, or null.
/// @return the refinement and whether its fit was replaced; cancellation; or an allocation failure. A catalog
///         query or a fit that fails for any other reason leaves the accepted refinement as it was.
[[nodiscard]] Result<FrameExtension> extendOverFrame(const ExecutionContext& execution, const SkyCatalog& catalog,
                                                     std::span<const PlateSolveStar> stars,
                                                     const PlateSolveInput& input, const PlateSolvePlan& plan,
                                                     PlateSolveRefinementResult refinement, RunCounters* counters);

/// The refinement behind `refinePlateSolveAtCenter`, optionally publishing its work.
///
/// @param execution operation policy and engine-context lease.
/// @param catalog immutable catalog to search.
/// @param input prepared image stars.
/// @param plan checked plate-solve plan.
/// @param initialCenter first tangent point.
/// @param counters the complete solve's running totals, or null when the public stage is called on
///        its own.
/// @return the refinement, with the failures `refinePlateSolveAtCenter` documents.
[[nodiscard]] Result<PlateSolveRefinementResult>
refineAtCenter(const ExecutionContext& execution, const SkyCatalog& catalog, const PlateSolveInput& input,
               const PlateSolvePlan& plan, const SkyCoordinate& initialCenter, RunCounters* counters);

/// Builds an algorithm-domain failure.
///
/// @param code the typed reason.
/// @param operation the public operation that failed.
/// @param message the explanation for a person.
/// @return the complete failure.
[[nodiscard]] inline Status failure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Algorithms});
}

/// Builds an `ErrorCode::InvalidArgument` failure that names the refused field.
///
/// @param operation the public operation that failed.
/// @param parameter the request field or argument, recorded as the `Parameter` context.
/// @param message the explanation for a person.
/// @return the complete failure.
[[nodiscard]] inline Status parameterFailure(const char* operation, const char* parameter, const char* message)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

/// Restates a cancellation checkpoint's refusal under a plate-solver operation.
///
/// @param operation the public operation that stopped.
/// @param cancellation the checkpoint's failure, whose code - `Cancelled` or `DeadlineExceeded` -
///        is kept.
/// @return the failure.
[[nodiscard]] inline Status cancelledFailure(const char* operation, const Status& cancellation)
{
    return Status::failure(cancellation.code(),
                           StatusDetail{.message = "plate solving stopped at a cancellation checkpoint",
                                        .operation = operation,
                                        .domain = ErrorDomain::Algorithms});
}

} // namespace plate_solver_detail
} // namespace an
