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

#include <cstdint>

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
