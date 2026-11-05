// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Deterministic star-field matching and robust transform estimation.
///
/// Alignment is a model, not a side effect on an image. This surface returns one owned result:
/// source/reference indices, a source-to-reference matrix, and the quality needed to decide whether
/// to use it. Every ordering and tie break is part of the contract, and every random sample comes
/// from the request's operation-local seed.
///
/// Triangle invariants remain the matching primitive because they are invariant to translation,
/// rotation and uniform scale. They are implementation details rather than a second public
/// algorithm. A prepared reference owns their reusable form; the direct overload is the explicit
/// composition of preparation and matching.
///
/// Positions are engine pixel coordinates, the first pixel's centre at (0.5, 0.5), as a
/// `DetectedStar` reports them. Nothing here shifts a coordinate, so the fitted matrix maps in the
/// convention of the points it was fitted from, and `registrationTransformOf` hands it to the
/// registration warp unchanged.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/OperationEffect.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace an
{

class ExecutionContext;

/// The geometric model estimated from source coordinates to reference coordinates.
enum class StarTransformModel : std::uint8_t
{
    /// Uniform scale, rotation and translation, without shear or reflection.
    Similarity = 0u,

    /// A full two-dimensional affine transform.
    Affine,

    /// A full projective homography.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Projective
};

/// Whether a value names a transform model this build estimates.
///
/// @param model the value to inspect.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isStarTransformModel(StarTransformModel model) noexcept
{
    return static_cast<std::uint8_t>(model) <= static_cast<std::uint8_t>(StarTransformModel::Projective);
}

/// The transform model's own name.
///
/// @param model the value to name.
/// @return the enumerator spelling, or `"UnknownStarTransformModel"` for anything else. The
///         returned literal outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starTransformModelName(StarTransformModel model) noexcept;

/// How the bounded triangle catalogue is selected.
enum class StarTrianglePolicy : std::uint8_t
{
    /// Exhaust fields of at most sixty points and use bounded canonical sampling above that.
    Automatic = 0u,

    /// Examine every triangle in each deterministic rank/spatial seed group before keeping the
    /// largest requested set.
    Exhaustive,

    /// Sample triangle ordinals evenly inside each deterministic seed group, never examining more
    /// than the requested catalogue budget.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Bounded
};

/// Whether a value names a triangle policy this build implements.
///
/// @param policy the value to inspect.
/// @return true for every enumerator.
[[nodiscard]] constexpr bool isStarTrianglePolicy(StarTrianglePolicy policy) noexcept
{
    return static_cast<std::uint8_t>(policy) <= static_cast<std::uint8_t>(StarTrianglePolicy::Bounded);
}

/// The triangle policy's own name.
///
/// @param policy the value to name.
/// @return the enumerator spelling, or `"UnknownStarTrianglePolicy"` for anything else. The
///         returned literal outlives every caller.
[[nodiscard]] AFTERNIGHTENGINE_API const char* starTrianglePolicyName(StarTrianglePolicy policy) noexcept;

/// One fitted star position, in image pixels.
///
/// Coordinates follow `DetectedStar`: the first pixel centre is (0.5, 0.5). Ordering conveys the
/// detector or caller's brightness rank and is used by the bounded triangle policy; it does not
/// imply correspondence between two fields.
struct StarAlignmentPoint
{
    /// Horizontal coordinate in pixels.
    double x = 0.0;

    /// Vertical coordinate in pixels.
    double y = 0.0;

    /// Whether two points carry exactly the same coordinates.
    ///
    /// @param left one point.
    /// @param right the other.
    /// @return true when both coordinates are equal.
    [[nodiscard]] friend constexpr bool operator==(const StarAlignmentPoint& left,
                                                   const StarAlignmentPoint& right) noexcept = default;
};

/// Every decision made while matching and robustly fitting one field.
struct StarAlignmentRequest
{
    /// Which source-to-reference model to estimate.
    StarTransformModel model = StarTransformModel::Affine;

    /// How triangle candidates are selected.
    StarTrianglePolicy trianglePolicy = StarTrianglePolicy::Automatic;

    /// Maximum retained triangles on each side, in [1, 100000].
    std::uint32_t maximumTriangles = 30000u;

    /// Smallest retained triangle side in pixels, finite and positive.
    double minimumTriangleSide = 10.0;

    /// Euclidean search radius in the two independent normalized-side coordinates, in
    /// [1e-6, 0.25].
    ///
    /// Each source triangle searches with this times the mean of its two shorter sides over its
    /// longest, that mean clamped to the two scales below.
    double triangleRatioTolerance = 0.03;

    /// Smallest scale applied to `triangleRatioTolerance`, finite in [0.25, 1].
    double triangleToleranceMinimumScale = 0.5;

    /// Largest tolerance scale, finite in [minimum, 1].
    double triangleToleranceMaximumScale = 1.0;

    /// Nearest triangle neighbours allowed to vote, in [1, 16].
    std::uint32_t maximumTriangleNeighbours = 5u;

    /// Votes a point pairing needs before geometric fitting, in [1, 64].
    std::uint32_t minimumVotes = 3u;

    /// Inlier point pairs required. Zero selects six for a similarity or affine model and eight for
    /// a projective one; a stated value may be neither below the model's mathematical minimum -
    /// three, or four for a projective model - nor above `kMaxStarsPerFrame`.
    std::uint32_t minimumMatches = 0u;

    /// Largest residual an inlier may have, in reference-frame pixels; finite and positive. A
    /// residual is the distance between a mapped source point and its reference point.
    double inlierThreshold = 3.0;

    /// Minimum accepted inlier fraction among candidate point pairs, finite in (0, 1].
    double minimumInlierFraction = 0.5;

    /// Deterministic minimal-set attempts, in [1, 100000].
    std::uint32_t ransacIterations = 512u;

    /// Operation-local deterministic sampler seed. It never touches process-wide random state. The
    /// sampler is a xorshift generator, which has no zero state, so zero seeds it with one.
    std::uint64_t seed = 0x6a09e667f3bcc909ull;

    /// Whether two requests state every matching and fitting decision identically.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarAlignmentRequest& left,
                                                   const StarAlignmentRequest& right) noexcept = default;
};

/// A fitted source-to-reference coordinate transform.
///
/// It maps positions in the convention of the points it was fitted from - engine pixel
/// coordinates, whose first pixel centre is (0.5, 0.5), for points taken from `DetectedStar` - and
/// `registrationTransformOf` turns it into the `RegistrationTransform` the warp and the drizzle
/// take without changing a coefficient.
struct StarTransform
{
    /// Kind of transform represented by the matrix.
    StarTransformModel model = StarTransformModel::Affine;

    /// Row-major homogeneous matrix. Affine and similarity matrices have `[6,7,8] == [0,0,1]`.
    std::array<double, 9> sourceToReference{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    /// Whether two transforms carry exactly the same model and coefficients.
    ///
    /// @param left one transform.
    /// @param right the other.
    /// @return true when all fields are equal.
    [[nodiscard]] friend constexpr bool operator==(const StarTransform& left,
                                                   const StarTransform& right) noexcept = default;
};

/// Applies one fitted transform to one coordinate.
///
/// @param transform the checked transform returned by this surface.
/// @param source source-frame coordinate.
/// @return the finite reference coordinate; `ErrorCode::InvalidArgument` for an unknown model, a
///         non-finite coordinate or a non-finite coefficient; or `ErrorCode::InvalidData` when the
///         projective denominator is within 1e-12 of zero - a point on the horizon - or the mapped
///         coordinate is not finite.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarAlignmentPoint> transformStarPoint(const StarTransform& transform,
                                                                                 const StarAlignmentPoint& source);

/// One geometrically accepted source/reference correspondence.
struct StarMatch
{
    /// Index in the source span passed to alignment.
    std::uint32_t sourceIndex = 0u;

    /// Index in the reference span used to prepare the matcher.
    std::uint32_t referenceIndex = 0u;

    /// Triangle-consensus votes. Direct correspondence fitting reports one.
    std::uint32_t votes = 0u;

    /// Votes divided by the largest vote count of the candidate set, in (0, 1].
    double confidence = 0.0;

    /// Euclidean residual under the final source-to-reference transform, in pixels.
    double residual = 0.0;

    /// Whether two records carry exactly the same correspondence and measures.
    ///
    /// @param left one match.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarMatch& left, const StarMatch& right) noexcept = default;
};

/// Quality and work facts for one completed alignment.
struct StarAlignmentQuality
{
    /// Source points supplied.
    std::uint32_t sourcePoints = 0u;

    /// Reference points supplied.
    std::uint32_t referencePoints = 0u;

    /// Source triangles retained.
    std::uint32_t sourceTriangles = 0u;

    /// Reference triangles retained.
    std::uint32_t referenceTriangles = 0u;

    /// One-to-one consensus candidates presented to robust fitting.
    std::uint32_t candidateMatches = 0u;

    /// Matches inside the final residual threshold.
    std::uint32_t inlierMatches = 0u;

    /// Candidate matches outside that threshold.
    std::uint32_t rejectedOutliers = 0u;

    /// Degenerate minimal sets refused while fitting.
    std::uint32_t degenerateSamples = 0u;

    /// Minimal-set attempts actually evaluated.
    std::uint32_t ransacAttempts = 0u;

    /// Inliers divided by candidates, in [0, 1].
    double inlierFraction = 0.0;

    /// Root mean square of final inlier residuals, in pixels.
    double rootMeanSquareResidual = 0.0;

    /// Median final inlier residual, in pixels.
    double medianResidual = 0.0;

    /// Largest final inlier residual, in pixels.
    double maximumResidual = 0.0;

    /// Context-owned temporary bytes held at once. Owned result and prepared-reference vectors are
    /// excluded because they leave the operation with the caller.
    std::uint64_t workingBytes = 0u;

    /// Workers the operation was admitted. Ordering and results never depend on this value.
    std::uint32_t admittedConcurrency = 0u;

    /// Whether two records contain exactly the same figures.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend constexpr bool operator==(const StarAlignmentQuality& left,
                                                   const StarAlignmentQuality& right) noexcept = default;
};

/// The complete answer from matching or direct robust fitting.
struct StarAlignmentResult
{
    /// Fitted source-to-reference transform.
    StarTransform transform{};

    /// Accepted inliers, sorted by source index and then reference index whatever the thread count.
    std::vector<StarMatch> matches{};

    /// Quality and execution facts used to accept the model.
    StarAlignmentQuality quality{};

    /// Whether two answers are bit-for-bit identical.
    ///
    /// @param left one answer.
    /// @param right the other.
    /// @return true when transform, ordered matches and quality all agree.
    [[nodiscard]] friend bool operator==(const StarAlignmentResult& left, const StarAlignmentResult& right) = default;
};

/// Immutable reusable analysis of one reference field.
///
/// Copies share immutable state. Preparation binds ordered coordinates, every request field and
/// the triangle-selection schema into a stable fingerprint. A default object holds nothing; all
/// accessors remain safe and return zero/default facts for it.
class PreparedStarAlignmentReference final
{
public:
    /// Builds an empty reference.
    AFTERNIGHTENGINE_API PreparedStarAlignmentReference() noexcept;

    /// Shares an immutable reference.
    ///
    /// @param other reference to share.
    AFTERNIGHTENGINE_API PreparedStarAlignmentReference(const PreparedStarAlignmentReference& other) noexcept;

    /// Moves an immutable reference.
    ///
    /// @param other reference to take.
    AFTERNIGHTENGINE_API PreparedStarAlignmentReference(PreparedStarAlignmentReference&& other) noexcept;

    /// Shares another immutable reference.
    ///
    /// @param other reference to share.
    /// @return this object.
    AFTERNIGHTENGINE_API PreparedStarAlignmentReference&
    operator=(const PreparedStarAlignmentReference& other) noexcept;

    /// Takes another immutable reference.
    ///
    /// @param other reference to take.
    /// @return this object.
    AFTERNIGHTENGINE_API PreparedStarAlignmentReference& operator=(PreparedStarAlignmentReference&& other) noexcept;

    /// Releases this handle; shared immutable state lives until its last handle.
    AFTERNIGHTENGINE_API ~PreparedStarAlignmentReference();

    /// Whether this object came from successful preparation.
    ///
    /// @return true when it owns immutable state.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// Ordered reference-point count.
    ///
    /// @return the count, or zero for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::size_t pointCount() const noexcept;

    /// Retained reference-triangle count.
    ///
    /// @return the count, or zero for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::size_t triangleCount() const noexcept;

    /// Stable identity of the coordinates, request and preparation schema.
    ///
    /// @return the 64-bit fingerprint, or zero for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint64_t fingerprint() const noexcept;

    /// Matching and fitting choices embedded in this preparation.
    ///
    /// @return the choices, or a default request for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API StarAlignmentRequest request() const noexcept;

private:
    struct State;

    /// Builds a public handle around checked immutable state.
    ///
    /// @param state state to own.
    explicit PreparedStarAlignmentReference(std::shared_ptr<const State> state) noexcept;

    /// Shared immutable implementation.
    std::shared_ptr<const State> m_state{};

    friend AFTERNIGHTENGINE_API Result<PreparedStarAlignmentReference>
    prepareStarAlignmentReference(const ExecutionContext&, std::span<const StarAlignmentPoint>,
                                  const StarAlignmentRequest&);
    friend AFTERNIGHTENGINE_API Result<StarAlignmentResult> alignStarFields(const ExecutionContext&,
                                                                            std::span<const StarAlignmentPoint>,
                                                                            const PreparedStarAlignmentReference&);
};

/// Prepares the reference side of repeated star-field matching.
///
/// Coordinates are copied in their supplied brightness order and never mutated. Triangle
/// generation is deterministic and checks cancellation at group and bounded-sample boundaries.
/// The owned result may outlive the execution; its ordinary value storage is therefore caller
/// storage rather than operation scratch.
///
/// @param execution operation policy and engine-context lease.
/// @param reference finite reference-frame coordinates in brightness order.
/// @param request matching, fitting and deterministic sampling choices.
/// @return the immutable preparation; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for an unknown model or triangle policy, or a non-finite or
///         non-positive minimum triangle side or inlier threshold; `ErrorCode::OutOfRange` for any
///         other request field outside its stated range; `ErrorCode::LimitExceeded` above
///         `kMaxStarsPerFrame` points; `ErrorCode::InvalidData` for a non-finite coordinate;
///         `ErrorCode::EmptySelection` for fewer points than the model's minimum or no
///         non-degenerate triangle of the requested size; or a cancellation, memory-budget or
///         allocation failure. An allocation failure is translated at this boundary.
[[nodiscard]] AFTERNIGHTENGINE_API Result<PreparedStarAlignmentReference>
prepareStarAlignmentReference(const ExecutionContext& execution, std::span<const StarAlignmentPoint> reference,
                              const StarAlignmentRequest& request = {});

/// Robustly fits already corresponding points.
///
/// Source and reference spans correspond by index. A request-local deterministic RANSAC rejects
/// outliers, the selected inliers are refitted by least squares, and all quality values are
/// recomputed from that final model.
///
/// The first attempt fits the first minimal set in span order - for a triangle match, the pairs
/// with the most votes - and every later one is drawn from the request's seed; the attempts stop
/// early once one model makes every pair an inlier. The best model's inliers are then refitted by
/// least squares twice, reclassifying the inliers after each refit, and the required count and
/// fraction are checked again on the final set.
///
/// @param execution operation policy and engine-context lease.
/// @param source source-frame points.
/// @param reference corresponding reference-frame points, exactly as long as `source`.
/// @param request model, residual, inlier and deterministic sampler choices. Triangle fields are
///        validated but not used.
/// @return transform, source-ordered inliers and quality; `ErrorCode::NotInitialized` without an
///         execution; `ErrorCode::InvalidArgument` or `ErrorCode::OutOfRange` for a request field,
///         as `prepareStarAlignmentReference` reports it; `ErrorCode::IncompatibleArguments` for
///         unequal spans; `ErrorCode::LimitExceeded` above `kMaxStarsPerFrame` points;
///         `ErrorCode::InvalidData` for a non-finite coordinate, a set whose every minimal sample
///         is degenerate, or degenerate final inliers; `ErrorCode::EmptySelection` for fewer
///         correspondences than the required inlier count, or too few inliers or too small an
///         inlier fraction; or a cancellation, memory-budget or allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarAlignmentResult>
fitStarTransform(const ExecutionContext& execution, std::span<const StarAlignmentPoint> source,
                 std::span<const StarAlignmentPoint> reference, const StarAlignmentRequest& request = {});

/// Matches a source field against a prepared reference and estimates its transform.
///
/// Triangle candidates vote for point correspondences, candidates are selected one-to-one by vote
/// count with index tie breaks, and `fitStarTransform`'s deterministic robust model decides the
/// inliers. Triangle ratios are similarity invariants: affine and projective matching therefore
/// expects local shape distortion to remain within `triangleRatioTolerance`, after which the
/// requested model captures that distortion. The returned list is always sorted by source then
/// reference index.
///
/// Thread-safe with respect to the execution, source and prepared reference. Concurrent calls may
/// share one preparation; it contains no lazy mutation or process-wide random state.
///
/// @param execution operation policy and engine-context lease.
/// @param source finite source-frame coordinates in brightness order.
/// @param reference immutable prepared reference.
/// @return the complete alignment; `ErrorCode::NotInitialized` without an execution;
///         `ErrorCode::InvalidArgument` for an empty preparation; `ErrorCode::LimitExceeded` above
///         `kMaxStarsPerFrame` source points; `ErrorCode::InvalidData` for a non-finite source
///         coordinate or degenerate final geometry; `ErrorCode::EmptySelection` for fewer source
///         points than the model's minimum, no usable source triangle, no shared triangle
///         invariant, too few voted pairs, or too few inliers; or a cancellation, memory-budget or
///         allocation failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarAlignmentResult>
alignStarFields(const ExecutionContext& execution, std::span<const StarAlignmentPoint> source,
                const PreparedStarAlignmentReference& reference);

/// Prepares, matches and estimates a transform in one call.
///
/// This is an explicit convenience composition: repeated sources should call
/// `prepareStarAlignmentReference` once and use the prepared overload.
///
/// @param execution operation policy and engine-context lease.
/// @param source finite source-frame coordinates in brightness order.
/// @param reference finite reference-frame coordinates in brightness order.
/// @param request preparation, matching and fit choices.
/// @return the complete alignment, or any preparation or prepared-alignment failure.
[[nodiscard]] AFTERNIGHTENGINE_API Result<StarAlignmentResult>
alignStarFields(const ExecutionContext& execution, std::span<const StarAlignmentPoint> source,
                std::span<const StarAlignmentPoint> reference, const StarAlignmentRequest& request = {});

/// Alignment only reads coordinate records and produces no image or metadata transition.
///
/// In particular, estimating this model does not mark an image registered.
/// `Algorithms/Registration/RegistrationPlan.h`'s warp owns that semantic transition, because it is
/// the operation that actually applies a transform to samples.
inline constexpr OperationEffect kStarAlignmentEffect{.samples = SampleEffect::Reads};

} // namespace an
