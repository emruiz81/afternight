// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Staged, deterministic catalog plate solving.
///
/// A plate solve is deliberately exposed as the composition it really is: validate the search,
/// select image stars, query and project a catalog, establish correspondences, fit a WCS, and refine
/// its tangent point.  The complete entry point composes those stages, while the stage results make
/// a failure or a performance regression attributable without parsing a log line.
///
/// Every image position here is an engine pixel coordinate - the first pixel's centre is
/// `(0.5, 0.5)` - and the returned `WcsSolution` carries its reference pixel in that convention;
/// `WcsCards.h` converts it once when it is written to a header.

#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// An immutable catalog of sky positions. Defined in `Algorithms/Catalog/SkyCatalog.h`.
class SkyCatalog;

/// The arithmetic scale used when the caller has no useful estimate, in arcseconds per pixel.
inline constexpr double kDefaultPlateSolveScaleArcsecPerPixel = 2.0;

/// The smallest catalog cone used for one trial centre, in degrees.
inline constexpr double kMinimumPlateSolveCatalogRadiusDegrees = 0.05;

/// The highest HEALPix order used for a blind sequence, whose grid is 49,152 trial centres.
inline constexpr std::uint8_t kMaximumPlateSolveBlindHealpixOrder = 6u;

/// One detected image star.
///
/// The input order is brightness order, brightest first. Plate solving preserves that rank while
/// selecting a spatially balanced subset; brightness itself is not inferred from the coordinates.
struct PlateSolveStar
{
    /// Position in engine pixel coordinates, where the first pixel centre is `(0.5, 0.5)`.
    PixelCoordinate position{};
};

/// Every caller choice for a catalog-backed plate solve.
struct PlateSolveRequest
{
    /// Width of the image in pixels. Must not be zero.
    std::uint32_t imageWidth = 0u;

    /// Height of the image in pixels. Must not be zero.
    std::uint32_t imageHeight = 0u;

    /// Approximate field centre, or absent for a blind full-sky search.
    std::optional<SkyCoordinate> initialCenter{};

    /// Approximate image scale in arcseconds per pixel, finite and positive when present.
    ///
    /// Absence selects `kDefaultPlateSolveScaleArcsecPerPixel`; it is not inferred from the sample
    /// type, metadata, or an earlier operation.
    std::optional<double> approximateScaleArcsecPerPixel{};

    /// Largest displacement of a hinted trial centre, in degrees and in `[0, 180]`.
    ///
    /// This controls placement of trial centres. It never enlarges the catalog cone queried at one
    /// centre, so a broad positional uncertainty does not turn each query into a broad sky scan.
    ///
    /// Ring `k` of `nearSearchRings` lies at an angular distance of
    /// `searchRadiusDegrees * k / nearSearchRings` from the hint on the celestial sphere. Samples
    /// start due east and turn toward north. A 180-degree ring reaches the antipode.
    double searchRadiusDegrees = 2.0;

    /// Faintest admitted catalog magnitude. A limit also refuses every entry with no measured
    /// magnitude; absence admits every entry, measured or not.
    std::optional<double> magnitudeLimit = 14.0;

    /// Most refinement iterations before the final recentered fit, in `[1, 32]`.
    std::uint32_t maximumRefinementIterations = 5u;

    /// Most image stars retained for matching, in `[3, 4096]`; `PlateSolveInput::stars` states which.
    std::uint32_t maximumImageStars = 80u;

    /// Most projected catalog stars retained for matching, in `[3, 4096]`.
    std::uint32_t maximumCatalogStars = 160u;

    /// Most catalog entries one trial-centre query may return, in `[maximumCatalogStars, 1000000]`.
    std::uint32_t maximumCatalogCandidates = 32768u;

    /// Concentric rings searched around a hint, in `[0, 64]`. Zero rings, or a zero search radius,
    /// tries the hint alone.
    std::uint32_t nearSearchRings = 3u;

    /// Equally spaced trial centres on each non-empty ring, in `[1, 1024]`.
    std::uint32_t nearSearchSamplesPerRing = 12u;

    /// Angular movement at or below which refinement has converged, in arcseconds and finite
    /// positive.
    double convergenceToleranceArcsec = 0.05;

    /// Matches required in an accepted solution, in `[3, min(maximumImageStars,
    /// maximumCatalogStars)]`.
    std::uint32_t minimumMatchedStars = 8u;

    /// Largest accepted final root-mean-square residual, in pixels and finite positive. It is also
    /// the inlier threshold of the matching and fitting stages' robust fits.
    double maximumRmsResidualPixels = 2.0;

    /// Most retained triangles on either side of matching, in `[1, 100000]`.
    std::uint32_t maximumTriangles = 40000u;

    /// Smallest triangle side retained by matching, in pixels and finite positive.
    double minimumTriangleSidePixels = 8.0;

    /// Matching tolerance in normalized triangle-side space, finite and in `(0, 0.25]`.
    double triangleRatioTolerance = 0.035;

    /// Deterministic robust-fit attempts, in `[1, 100000]`.
    std::uint32_t ransacIterations = 512u;

    /// Operation-local deterministic sampler seed. It never touches process-wide random state.
    std::uint64_t deterministicSeed = 0x510e527fade682d1ull;

    /// Whether fitting may add a SIP distortion polynomial. `fitPlateSolveModel` states when one is
    /// fitted and when it is retained.
    bool fitSipDistortion = true;
};

/// A checked plate-solve request and its derived search geometry.
///
/// Immutable. A default object is invalid and is accepted by no operation; `planPlateSolve` is the
/// only way to obtain a valid one. Planning is allocation-free and needs no engine context.
class PlateSolvePlan final
{
public:
    /// Builds an invalid placeholder.
    PlateSolvePlan() noexcept = default;

    /// Whether this object came from successful planning.
    ///
    /// @return true for a checked plan.
    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return m_valid;
    }

    /// The checked caller choices.
    ///
    /// @return the request copied into this plan.
    [[nodiscard]] constexpr const PlateSolveRequest& request() const noexcept
    {
        return m_request;
    }

    /// Scale used to map projected degrees into matching pixels.
    ///
    /// @return the stated scale, or the documented default, in arcseconds per pixel.
    [[nodiscard]] constexpr double effectiveScaleArcsecPerPixel() const noexcept
    {
        return m_effectiveScaleArcsecPerPixel;
    }

    /// Radius of each catalog query.
    ///
    /// @return half the scaled image diagonal with its safety margin, in degrees, and never less
    ///         than `kMinimumPlateSolveCatalogRadiusDegrees`.
    [[nodiscard]] constexpr double catalogQueryRadiusDegrees() const noexcept
    {
        return m_catalogQueryRadiusDegrees;
    }

    /// HEALPix order used when no centre hint exists.
    ///
    /// The coarsest order whose largest pixel radius is within `catalogQueryRadiusDegrees`, so the
    /// query cones about the grid's pixel centres cover the sky. A field small enough to need a
    /// finer grid than `kMaximumPlateSolveBlindHealpixOrder` gets that order instead, and its cones
    /// then leave gaps between them.
    ///
    /// @return an order in `[0, kMaximumPlateSolveBlindHealpixOrder]`; zero for a hinted plan.
    [[nodiscard]] constexpr std::uint8_t blindHealpixOrder() const noexcept
    {
        return m_blindHealpixOrder;
    }

    /// Number of deterministic trial centres in the complete sequence.
    ///
    /// @return one plus `nearSearchRings * nearSearchSamplesPerRing` for a hinted solve with a
    ///         positive search radius, one for any other hinted solve, or every pixel in the
    ///         selected blind HEALPix grid.
    [[nodiscard]] constexpr std::uint32_t trialCenterCount() const noexcept
    {
        return m_trialCenterCount;
    }

private:
    /// Builds the checked representation.
    ///
    /// @param request checked caller choices.
    /// @param effectiveScaleArcsecPerPixel resolved scale.
    /// @param catalogQueryRadiusDegrees resolved per-centre catalog cone.
    /// @param blindHealpixOrder order for a blind sequence, or zero for a hinted one.
    /// @param trialCenterCount complete sequence length.
    constexpr PlateSolvePlan(PlateSolveRequest request, double effectiveScaleArcsecPerPixel,
                             double catalogQueryRadiusDegrees, std::uint8_t blindHealpixOrder,
                             std::uint32_t trialCenterCount) noexcept
        : m_request(request), m_effectiveScaleArcsecPerPixel(effectiveScaleArcsecPerPixel),
          m_catalogQueryRadiusDegrees(catalogQueryRadiusDegrees), m_blindHealpixOrder(blindHealpixOrder),
          m_trialCenterCount(trialCenterCount), m_valid(true)
    {
    }

    /// Checked caller choices.
    PlateSolveRequest m_request{};

    /// Resolved scale in arcseconds per pixel.
    double m_effectiveScaleArcsecPerPixel = 0.0;

    /// Per-centre catalog cone radius in degrees.
    double m_catalogQueryRadiusDegrees = 0.0;

    /// Blind HEALPix order.
    std::uint8_t m_blindHealpixOrder = 0u;

    /// Complete number of trial centres.
    std::uint32_t m_trialCenterCount = 0u;

    /// Whether planning succeeded.
    bool m_valid = false;

    friend AFTERNIGHTENGINE_API Result<PlateSolvePlan> planPlateSolve(const PlateSolveRequest&);
};

/// The image-star input used by every trial centre.
///
/// The matcher forms its triangles from each side's brightest points, so the image and the catalog
/// must rank the same stars first. A frame with at least sixty stars inside the matching disc - the
/// largest circle about the image centre that lies inside the frame, half the shorter side in
/// radius, the one region the field's unknown rotation cannot change - therefore keeps the disc's
/// stars alone, and the catalog search keeps the region the image's stars were kept over: the same
/// circle about the trial centre, at the plan's effective scale, whenever no prepared star lies
/// outside the disc, and the frame otherwise. The two sides never differ in it.
struct PlateSolveInput
{
    /// Selected stars. When at least sixty supplied stars lie inside the matching disc, they are the
    /// first `maximumImageStars` of those, in supplied order, and no star outside it. Otherwise, up
    /// to `maximumImageStars` they are every supplied star in supplied order; above it they are
    /// chosen in rounds over a ten-by-eight grid of the frame - the brightest star of every occupied
    /// cell, then the second brightest of each, and so on - each round in brightness order.
    std::vector<PlateSolveStar> stars{};

    /// Stars supplied before selection.
    std::uint32_t suppliedStarCount = 0u;
};

/// One catalog point projected for a particular trial centre.
struct PlateSolveCatalogStar
{
    /// Position in `SkyCatalog::entries()`.
    std::uint32_t catalogIndex = 0u;

    /// Catalog direction retained verbatim.
    SkyCoordinate coordinate{};

    /// Eastward tangent-plane coordinate relative to the trial centre, in degrees.
    double tangentXDegrees = 0.0;

    /// Northward tangent-plane coordinate relative to the trial centre, in degrees.
    double tangentYDegrees = 0.0;

    /// Eastward tangent coordinate mapped by the approximate scale, in pseudo pixels.
    double matchingX = 0.0;

    /// Northward tangent coordinate mapped by the approximate scale, in pseudo pixels.
    double matchingY = 0.0;
};

/// The catalog-search stage's owned output and work facts.
struct PlateSolveCatalogSearchResult
{
    /// Centre against which every tangent coordinate was projected.
    SkyCoordinate trialCenter{};

    /// Per-centre cone radius used, in degrees.
    double queryRadiusDegrees = 0.0;

    /// Point sources retained for matching, in the order `searchPlateSolveCatalog` describes.
    std::vector<PlateSolveCatalogStar> stars{};

    /// Catalog candidates tested by the indexed query.
    std::uint32_t examinedCatalogEntries = 0u;

    /// Entries admitted by the cone and magnitude rules before point-source projection.
    std::uint32_t admittedCatalogEntries = 0u;

    /// Extended entries deliberately excluded from a point-star match.
    std::uint32_t rejectedExtendedEntries = 0u;

    /// Directions the trial tangent plane could not represent.
    std::uint32_t rejectedProjectionEntries = 0u;

    /// Whether the catalog query reached its requested result limit.
    bool queryTruncated = false;
};

/// One accepted image-to-catalog correspondence.
struct PlateSolveMatch
{
    /// Position in `PlateSolveInput::stars`. In the matches of a complete solve (`solvePlate`), which
    /// prepares its own input, the star's position among the stars supplied to it.
    std::uint32_t imageStarIndex = 0u;

    /// Position in `SkyCatalog::entries()`.
    std::uint32_t catalogIndex = 0u;

    /// Observed image position in engine pixel coordinates.
    PixelCoordinate imagePosition{};

    /// Catalog direction.
    SkyCoordinate catalogCoordinate{};

    /// Eastward tangent-plane coordinate at the trial centre, in degrees.
    double tangentXDegrees = 0.0;

    /// Northward tangent-plane coordinate at the trial centre, in degrees.
    double tangentYDegrees = 0.0;

    /// Triangle-consensus votes.
    std::uint32_t votes = 0u;

    /// Votes relative to the strongest candidate, in `(0, 1]`.
    double confidence = 0.0;

    /// Residual in pixels. From the matching stage, under its affine pseudo-pixel transform; from
    /// fitting or a complete solve, in image pixels under the fitted model, after the distortion
    /// polynomial when one was retained.
    double residualPixels = 0.0;
};

/// The matching stage's owned output and work facts.
struct PlateSolveMatchResult
{
    /// Accepted correspondences, ordered by image-star index.
    std::vector<PlateSolveMatch> matches{};

    /// Image triangles retained by matching.
    std::uint32_t imageTriangles = 0u;

    /// Catalog triangles retained by matching.
    std::uint32_t catalogTriangles = 0u;

    /// One-to-one candidates presented to robust fitting.
    std::uint32_t candidateMatches = 0u;

    /// Candidate correspondences rejected as outliers.
    std::uint32_t rejectedOutliers = 0u;

    /// Context-owned transient bytes held at once by matching.
    std::uint64_t peakWorkingBytes = 0u;
};

/// A WCS fitted at one fixed trial centre.
struct PlateSolveFitResult
{
    /// Full immutable world-coordinate solution.
    WcsSolution solution;

    /// Inlier correspondences retained by the robust tangent-to-pixel fit.
    std::vector<PlateSolveMatch> matches{};

    /// Root-mean-square residual of the linear fit over its inliers, in pixels.
    double linearRmsResidualPixels = 0.0;

    /// Root-mean-square residual after any accepted distortion, in pixels.
    double finalRmsResidualPixels = 0.0;

    /// Candidate correspondences rejected by the robust fit.
    std::uint32_t rejectedOutliers = 0u;

    /// Accepted SIP polynomial order, or zero for a linear solution.
    std::uint32_t sipOrder = 0u;

    /// Context-owned transient bytes fitting needed: this stage's own peak plus the robust fit's,
    /// which bounds what was held at once from above.
    std::uint64_t peakWorkingBytes = 0u;
};

/// One centre's iterative refinement result.
struct PlateSolveRefinementResult
{
    /// Best fit, recentered once at its final field centre.
    PlateSolveFitResult fit;

    /// Trial centre supplied to the first iteration.
    SkyCoordinate initialCenter{};

    /// Centre used for the final fit.
    SkyCoordinate finalCenter{};

    /// Refinement iterations attempted before the final recentered fit.
    std::uint32_t iterations = 0u;

    /// Whether centre movement reached the requested tolerance.
    bool converged = false;
};

/// Search, matching, fitting, execution, and outcome facts for a completed solve.
struct PlateSolveDiagnostics
{
    /// Image stars supplied by the caller.
    std::uint32_t suppliedImageStars = 0u;

    /// Image stars retained for matching.
    std::uint32_t selectedImageStars = 0u;

    /// Trial centres in the planned sequence.
    std::uint32_t plannedTrialCenters = 0u;

    /// Trial centres attempted, the one whose answer was selected included.
    std::uint32_t examinedTrialCenters = 0u;

    /// Catalog queries issued across every trial centre's refinement iterations and final fits,
    /// failed queries included.
    std::uint32_t catalogQueries = 0u;

    /// Catalog candidates examined by those queries.
    std::uint64_t examinedCatalogEntries = 0u;

    /// Catalog positions admitted by those queries.
    std::uint64_t admittedCatalogEntries = 0u;

    /// Matching stages attempted.
    std::uint32_t matchingAttempts = 0u;

    /// Fixed-centre fitting stages attempted.
    std::uint32_t fittingAttempts = 0u;

    /// Refinement iterations used by the selected answer.
    std::uint32_t refinementIterations = 0u;

    /// Correspondences retained by the selected answer.
    std::uint32_t matchedStars = 0u;

    /// Candidate correspondences rejected by its final robust fit.
    std::uint32_t rejectedOutliers = 0u;

    /// Largest context-owned transient-byte peak reported by a stage.
    std::uint64_t peakWorkingBytes = 0u;

    /// Workers granted to this operation.
    std::uint32_t admittedConcurrency = 0u;

    /// Whether the selected answer began at the exact supplied hint.
    bool usedExactHint = false;

    /// Whether the selected answer began at an offset near-search centre.
    bool usedNearSearch = false;

    /// Whether the selected answer began at a blind HEALPix centre.
    bool usedBlindSearch = false;

    /// Whether the selected refinement reached its angular tolerance.
    bool converged = false;

    /// Whether the selected WCS retained a SIP distortion model.
    bool fittedSipDistortion = false;
};

/// The complete plate-solve answer.
struct PlateSolveResult
{
    /// Full immutable world-coordinate solution.
    WcsSolution solution;

    /// Final robust correspondences.
    std::vector<PlateSolveMatch> matches{};

    /// Observable work and route facts.
    PlateSolveDiagnostics diagnostics{};
};

/// Computes the per-centre catalog cone independently of positional search uncertainty.
///
/// The radius is 1.25 times half the scaled image diagonal, with a 0.05-degree floor. The separate
/// `PlateSolveRequest::searchRadiusDegrees` controls trial-centre placement and is intentionally
/// absent from this function.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param imageWidth image width in pixels, not zero.
/// @param imageHeight image height in pixels, not zero.
/// @param scaleArcsecPerPixel finite positive approximate scale.
/// @return the cone radius in degrees; `ErrorCode::InvalidArgument` for a zero dimension or an
///         invalid scale; or `ErrorCode::OutOfRange` when the radius is not finite or exceeds 180
///         degrees.
[[nodiscard]] AFTERNIGHTENGINE_API Result<double>
estimatePlateSolveCatalogQueryRadius(std::uint32_t imageWidth, std::uint32_t imageHeight, double scaleArcsecPerPixel);

/// Validates every plate-solve choice and derives the deterministic search plan.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param request dimensions, hints, limits, matching policy, and fit policy.
/// @return the immutable plan; `ErrorCode::InvalidArgument`, naming the field in its `Parameter`
///         context, for a zero dimension, a centre hint that is not a sky direction, a stated
///         scale or tolerance that is not finite and positive, a magnitude limit that is not
///         finite, or a count or bound outside the range its field states;
///         `ErrorCode::OutOfRange` when the scaled field has no catalog cone of at most 180
///         degrees; or `ErrorCode::LimitExceeded` when the hinted trial sequence does not fit a
///         32-bit count.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolvePlan> planPlateSolve(const PlateSolveRequest& request);

/// Validates and selects image-star input once for all trial centres.
///
/// Runs on the calling thread, checks cancellation every 256 stars while validating them, and
/// produces ordinary caller-owned result storage. The selection is the one `PlateSolveInput::stars`
/// describes: the matching disc's stars for a crowded frame, the round selection for a sparser
/// one. A failure returns no partial input.
///
/// @param execution operation policy and engine-context lease.
/// @param stars detections in brightness order, brightest first. Each position must be finite and
///        lie within the span of pixel centres: x in `[0.5, width - 0.5]`, y in
///        `[0.5, height - 0.5]`.
/// @param plan checked plate-solve plan.
/// @return selected input; `ErrorCode::NotInitialized` for an empty execution or an invalid plan;
///         `ErrorCode::LimitExceeded` for more than a million stars or when the plan's dimensions
///         exceed the engine context's image limits; `ErrorCode::InvalidData`, with the star's
///         index as its `Completed` context, for a position that is not finite or outside that
///         span; `ErrorCode::EmptySelection` for fewer than three stars; the cancellation's own
///         code; or `ErrorCode::AllocationFailed`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolveInput> preparePlateSolveInput(const ExecutionContext& execution,
                                                                                  std::span<const PlateSolveStar> stars,
                                                                                  const PlateSolvePlan& plan);

/// Queries, filters, projects, and spatially balances catalog point sources for one trial centre.
///
/// The query is a cone of `plan.catalogQueryRadiusDegrees()` about the trial centre under the
/// request's magnitude limit, answered brightest first. Extended entries are dropped, and every
/// other entry is projected onto the trial centre's tangent plane and scaled by the plan's
/// effective scale into pseudo pixels, east along +x and north along +y. When no star of `input`
/// lies outside the matching disc `PlateSolveInput` describes - a frame matched over its disc, or
/// one whose every star happens to lie there - the entries outside that disc about the trial centre
/// are dropped, however few remain, and the first `maximumCatalogStars` kept, brightest first: a
/// catalog star outside the disc has no image star to be matched with. Otherwise, when at
/// least sixteen fall inside a rectangle 1.1 times the image's width and height about the centre -
/// aligned with east and north, since the field's rotation is not yet known - the rest are dropped,
/// and with fewer the ones inside are moved ahead of the rest; when more than `maximumCatalogStars`
/// then remain, the round selection `PlateSolveInput::stars` describes picks them over a square
/// 1.1 times the image diagonal wide. A query that reached its result bound is reported as a
/// warning diagnostic as well as in `queryTruncated`.
///
/// The catalog must already carry positions for the observation epoch; provider-specific epoch
/// correction belongs to catalog construction and is not silently repeated here.
///
/// @param execution operation policy and engine-context lease.
/// @param catalog immutable catalog to query.
/// @param input prepared image stars, at least three. Whether any of them lies outside the matching
///        disc decides the region the catalog keeps; the disc and the preferred rectangle come from
///        the plan's image dimensions.
/// @param plan checked plate-solve plan.
/// @param trialCenter finite sky direction used as the tangent point.
/// @return projected point sources and query facts; `ErrorCode::NotInitialized` for an empty
///         execution or catalog or an invalid plan; `ErrorCode::InvalidArgument` for a trial
///         centre that is not a sky direction; `ErrorCode::EmptySelection` when `input` holds
///         fewer than three stars, fewer than three point sources project, or fewer than three of
///         them lie in the matching disc a frame matched over it keeps; any failure of
///         `queryCatalogRegion`; cancellation; or `ErrorCode::AllocationFailed`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolveCatalogSearchResult>
searchPlateSolveCatalog(const ExecutionContext& execution, const SkyCatalog& catalog, const PlateSolveInput& input,
                        const PlateSolvePlan& plan, const SkyCoordinate& trialCenter);

/// Establishes deterministic image-to-catalog correspondences at one trial centre.
///
/// Image positions are measured from the image centre `(width / 2, height / 2)` and matched against
/// the catalog's pseudo-pixel positions by `alignStarFields` with an affine model, so the field's
/// rotation and parity need not be known. The request's triangle, match-count, iteration and seed
/// fields configure it, `maximumRmsResidualPixels` is its inlier threshold, and a correspondence
/// needs at least three triangle votes. Matching uses the operation-local seed; the image and
/// catalog order remain brightness ranks, and there is no process-wide random state.
///
/// @param execution operation policy and engine-context lease.
/// @param input prepared image stars.
/// @param catalogSearch projected catalog stars for one centre.
/// @param plan checked plate-solve plan.
/// @return correspondences ordered by image-star index, and work facts;
///         `ErrorCode::NotInitialized` for an empty execution or an invalid plan;
///         `ErrorCode::EmptySelection` when either side holds fewer than three points or no
///         reliable match exists; `ErrorCode::InvalidData` when `catalogSearch` carries an invalid
///         trial centre or the matched geometry is degenerate; or any other failure of
///         `alignStarFields`, cancellation, memory-budget and allocation failures included.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolveMatchResult>
matchPlateSolveStars(const ExecutionContext& execution, const PlateSolveInput& input,
                     const PlateSolveCatalogSearchResult& catalogSearch, const PlateSolvePlan& plan);

/// Fits a robust tangent-to-pixel model and builds a WCS at one fixed centre.
///
/// The matches' tangent-plane coordinates are fitted to their image positions by
/// `fitStarTransform` with an affine model, the request's residual, match-count, iteration and seed
/// fields, and at least a quarter of them as inliers. The fit is Float64 and deterministic. The
/// returned WCS's reference pixel is where the fitted model puts the trial centre, in engine pixel
/// coordinates, and its matrix is the inverse of the fit's linear part.
///
/// When `fitSipDistortion` is set, forward SIP polynomials are fitted to the inliers' residuals, in
/// offsets from that reference pixel divided by the larger image dimension, through normal
/// equations solved in extended precision. The order follows the inlier count - fourth from 24,
/// third from 10, second from 6, none below - and the polynomials are retained only when they
/// improve the RMS by more than 0.001 pixel and hold a coefficient above 1e-12 in magnitude; an
/// unsolvable system keeps the linear WCS. No inverse polynomials are fitted, so the WCS inverts
/// the distortion by Newton iteration.
///
/// @param execution operation policy and engine-context lease.
/// @param matches correspondences from the matching stage.
/// @param trialCenter the tangent point the matches' tangent coordinates were projected at. It is
///        not checked against them, and becomes the WCS's reference point.
/// @param plan checked plate-solve plan.
/// @return the WCS, final inliers and fit facts; `ErrorCode::NotInitialized` for an empty
///         execution, an invalid plan or an execution without a memory resource;
///         `ErrorCode::InvalidArgument` for a trial centre that is not a sky direction;
///         `ErrorCode::EmptySelection` when fewer than `minimumMatchedStars` matches are supplied
///         or retained, or the final RMS exceeds `maximumRmsResidualPixels`;
///         `ErrorCode::InvalidData` for a non-finite correspondence or a singular fit; any other
///         failure of `fitStarTransform`, `SipDistortion::create` or `WcsSolution::create`;
///         cancellation; the engine context's memory-budget refusal; or
///         `ErrorCode::AllocationFailed`.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolveFitResult> fitPlateSolveModel(const ExecutionContext& execution,
                                                                                  const PlateSolveMatchResult& matches,
                                                                                  const SkyCoordinate& trialCenter,
                                                                                  const PlateSolvePlan& plan);

/// Repeats catalog search, matching, and fitting while the solved centre moves.
///
/// Each iteration searches, matches and fits at the current centre, then moves the centre to the
/// solution's `fieldCenter` - the sky direction at the image centre. It stops when that move is at
/// most `convergenceToleranceArcsec`, or after `maximumRefinementIterations`, which `converged`
/// reports rather than a failure. A final fit is made at the last solved field centre even when the
/// tolerance was already met, so catalog tangent coordinates and the returned WCS share one centre.
/// A failure of any stage, in any iteration or in the final fit, fails the refinement even when an
/// earlier iteration succeeded. All stages use the same execution context, cancellation source,
/// memory budget, scheduler grant, and callback order.
///
/// @param execution operation policy and engine-context lease.
/// @param catalog immutable catalog whose entries already have the intended epoch.
/// @param input prepared image stars.
/// @param plan checked plate-solve plan.
/// @param initialCenter first tangent point.
/// @return the recentered fit and iteration facts; `ErrorCode::NotInitialized` for an empty
///         execution or catalog or an invalid plan; `ErrorCode::InvalidArgument` for an initial
///         centre that is not a sky direction; or the first search, match, fit, field-centre or
///         cancellation failure. `ErrorCode::EmptySelection` means this centre produced no
///         accepted solution.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolveRefinementResult>
refinePlateSolveAtCenter(const ExecutionContext& execution, const SkyCatalog& catalog, const PlateSolveInput& input,
                         const PlateSolvePlan& plan, const SkyCoordinate& initialCenter);

/// Solves one image against an immutable catalog.
///
/// A hint is tried exactly first, followed by the concentric offsets
/// `PlateSolveRequest::searchRadiusDegrees` describes. Without a hint, the pixel centres of a
/// ring-numbered HEALPix grid at `PlateSolvePlan::blindHealpixOrder` are tried in ring order. Each
/// trial centre is refined by `refinePlateSolveAtCenter`. An accepted result at the exact hint
/// returns immediately. Otherwise candidates are ranked by match count, then lower final RMS, then
/// earlier trial; the search stops as soon as the best so far has at least twelve matches and a
/// final RMS of at most one pixel, and otherwise returns the best of the whole sequence. A trial
/// centre that fails with `ErrorCode::EmptySelection`, `ErrorCode::InvalidData` or
/// `ErrorCode::OutOfRange` is skipped; any other failure - cancellation, budget exhaustion, a
/// malformed input or a catalog failure - is returned immediately.
///
/// The solution of a frame matched over its disc is then fitted over the whole frame. The catalog's
/// point sources are placed by the accepted solution, each is set beside the supplied star nearest
/// to where it falls, within a few pixels, and the model is fitted again to all of those pairs,
/// under the request's own match and residual limits; a few passes carry the fit outward, and it is
/// kept while it holds more stars than the one before. The stars between the disc and the corners
/// then constrain the solution there, where a distortion fitted to the disc alone is carried well
/// beyond the stars it was fitted to. A frame whose stars were kept over its whole extent was
/// fitted over it already. Every match of the result names its star among the stars supplied.
///
/// Progress is reported as trial centres completed out of the planned count. The route that
/// supplied the answer is reported in the diagnostics and as an information diagnostic; a truncated
/// catalog query is a warning diagnostic from the search stage. Nothing is written to a stream or a
/// global log.
///
/// @param execution operation policy and engine-context lease.
/// @param catalog immutable catalog whose positions already represent the observation epoch.
/// @param stars finite image detections in brightness order, under the rules of
///        `preparePlateSolveInput`.
/// @param request dimensions, optional hints, limits, matching policy, and fit policy.
/// @return solution, matches, and diagnostics; `ErrorCode::NotInitialized` for an empty execution
///         or catalog; any `planPlateSolve` or `preparePlateSolveInput` failure;
///         `ErrorCode::EmptySelection`, with the trial centres examined and planned as its
///         `Completed` and `Total` context, when no trial centre yields an accepted solution; or
///         the first trial failure that is not skipped.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PlateSolveResult> solvePlate(const ExecutionContext& execution,
                                                                       const SkyCatalog& catalog,
                                                                       std::span<const PlateSolveStar> stars,
                                                                       const PlateSolveRequest& request);

} // namespace an
