// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deterministic triangle matching and robust coordinate-model fitting.
//
// Matching runs in four steps. Each field's triangle catalogue is built from at most three seed
// groups of sixty points - the brightest sixty, the sixty from rank forty, and a spatial spread
// that takes the brightest point of each cell of an eight-by-eight grid first - either exhaustively
// or by sampling triangle ordinals evenly. A triangle is described by its two shorter sides over its
// longest, which is invariant to translation, rotation and uniform scale; one whose shortest side is
// under a tenth of its longest, or whose area is under a hundredth of its squared perimeter, is too
// thin to be described reliably and is dropped. A prepared reference places its triangles in a grid
// over that two-ratio plane so a source triangle's nearest neighbours are found in the cells around
// it; `ComputeBackend::Reference` searches every reference triangle instead, which is the answer the
// grid has to reproduce. Each triangle pair whose vertex pairing agrees votes for three point pairs,
// the pairs with enough votes are taken one-to-one by descending vote count, and `robustFit` - a
// seeded RANSAC over minimal sets followed by least-squares refits - decides the inliers and the
// model.
//
// Determinism is structural: parallel stages write pre-sized slots indexed by their input, every
// sort ends in an index comparison, and the only randomness is the request's own xorshift seed.
// Transient vectors take their memory from the execution's engine context through `ExecutionMemory`,
// which throws on a refusal so that a budget failure unwinds through the standard containers and is
// returned as the context's own status at the public boundary.

#include <AfterNightEngine/Algorithms/Stars/StarAlignment.h>

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include "algorithms/stars/StarsInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <numeric>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace an
{

namespace
{

using stars_detail::failure;
using stars_detail::parameterFailure;
using stars_detail::rangeFailure;

constexpr const char* kPrepareOperation = "an::prepareStarAlignmentReference";
constexpr const char* kFitOperation = "an::fitStarTransform";
constexpr const char* kAlignOperation = "an::alignStarFields";
constexpr const char* kMapOperation = "an::transformStarPoint";

constexpr std::uint32_t kMaximumTriangles = 100000u;
constexpr std::uint32_t kMaximumTriangleNeighbours = 16u;
constexpr std::uint32_t kMaximumVotes = 64u;
constexpr std::uint32_t kMaximumRansacIterations = 100000u;
constexpr std::uint32_t kTriangleSeedGroupSize = 60u;
constexpr std::uint32_t kSecondTriangleWindowStart = 40u;
constexpr std::uint32_t kSpatialGridSide = 8u;
constexpr double kMinimumRatioTolerance = 1.0e-6;
constexpr double kMinimumToleranceScale = 0.25;
constexpr double kCoordinateEpsilon = 1.0e-12;
constexpr double kTriangleSideRatioFloor = 0.10;
constexpr double kTriangleAreaFloor = 0.01;
constexpr std::uint64_t kPreparationSchema = 2u;

/// The engine context's refusal, carried through a standard container as the exception it can raise.
///
/// Derived from `std::bad_alloc`, which is what a memory resource's allocation is expected to
/// throw; each public entry point catches it before `std::bad_alloc` and returns its status.
class ContextAllocationFailure final : public std::bad_alloc
{
public:
    /// Carries one refusal.
    explicit ContextAllocationFailure(Status status) noexcept : m_status(std::move(status)) {}

    /// The context's own failure, with its requested, available and limit bytes.
    [[nodiscard]] const Status& status() const noexcept
    {
        return m_status;
    }

private:
    /// The refusal being carried.
    Status m_status;
};

/// The engine context's memory resource, offered to standard containers and counting its peak.
///
/// One per public call, so `peakBytes` is that call's own working set.
class ExecutionMemory final : public std::pmr::memory_resource
{
public:
    /// Borrows the context's resource for the call.
    explicit ExecutionMemory(MemoryResource& memory) noexcept : m_memory(memory) {}

    /// The most bytes held at once through this resource.
    [[nodiscard]] std::uint64_t peakBytes() const noexcept
    {
        return m_peakBytes;
    }

private:
    [[nodiscard]] void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        Result<void*> taken = m_memory.allocate(bytes, alignment);
        if (!taken.hasValue())
        {
            throw ContextAllocationFailure(taken.status());
        }
        m_liveBytes += bytes;
        m_peakBytes = (std::max)(m_peakBytes, m_liveBytes);
        return *taken;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_memory.deallocate(pointer, bytes, alignment);
        m_liveBytes -= bytes;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    MemoryResource& m_memory;
    std::uint64_t m_liveBytes = 0u;
    std::uint64_t m_peakBytes = 0u;
};

/// One triangle of a field and its similarity-invariant description.
struct Triangle
{
    /// The three points, as indices into the field, in the order the triangle was formed.
    std::array<std::uint32_t, 3> indices{};

    /// The three sides over the longest, ascending, so the last is one; all zero for a degenerate
    /// or too thin triangle, which is never retained.
    std::array<double, 3> ratios{};

    /// For each side rank, shortest first, the place in `indices` of the vertex opposite it. Two
    /// triangles are paired vertex by vertex through this.
    std::array<std::uint32_t, 3> canonicalVertices{};

    /// The longest side, in pixels.
    double largestSide = 0.0;
};

/// One field's triangle catalogue and how it was built.
struct TriangleBuild
{
    /// Starts an empty catalogue in the execution's memory.
    explicit TriangleBuild(std::pmr::memory_resource* memory) : triangles(memory) {}

    /// The retained triangles.
    std::pmr::vector<Triangle> triangles;

    /// How many candidate triangles were formed and tested.
    std::uint64_t candidatesExamined = 0u;

    /// The policy that ran, with `Automatic` resolved.
    StarTrianglePolicy executedPolicy = StarTrianglePolicy::Automatic;
};

/// A prepared reference's triangles placed by their two smallest side ratios in the square cells of a
/// dense grid, row by row, so that a run of cells along one row is one run of entries.
struct TriangleGrid
{
    /// One triangle's two smallest side ratios, copied here so that a search compares against the
    /// entries it walks rather than reaching into the triangle list, which is ordered differently.
    struct Entry
    {
        double first = 0.0;
        double second = 0.0;
        std::uint32_t triangleIndex = 0u;
    };

    double cellSize = 1.0;
    std::int64_t firstColumn = 0;
    std::int64_t firstRow = 0;
    std::int64_t columns = 0;
    std::int64_t rows = 0;

    /// Where each cell's entries begin, row after row, and one more for where the last one ends.
    std::vector<std::uint32_t> starts{};
    std::vector<Entry> entries{};
};

/// A source triangle and a reference triangle whose ratios lie within the search radius.
struct TrianglePair
{
    /// Index into the source catalogue.
    std::uint32_t source = 0u;

    /// Index into the reference catalogue.
    std::uint32_t reference = 0u;
};

/// A source point and a reference point, with the triangle votes that paired them.
struct CandidateMatch
{
    /// Index into the source field.
    std::uint32_t source = 0u;

    /// Index into the reference field.
    std::uint32_t reference = 0u;

    /// How many triangle pairs voted for this pairing.
    std::uint32_t votes = 0u;
};

/// One triangle pair's vote for one point pairing, before votes are counted.
struct Vote
{
    /// Index into the source field.
    std::uint32_t source = 0u;

    /// Index into the reference field.
    std::uint32_t reference = 0u;
};

/// One reference triangle offered to a query, with its squared ratio distance.
struct Neighbour
{
    /// Squared distance between the two triangles' two smaller ratios.
    double distanceSquared = 0.0;

    /// Index into the reference catalogue.
    std::uint32_t triangleIndex = 0u;
};

/// What the robust fit decided, index by index over the correspondences it was given.
struct FitSummary
{
    /// Starts an empty summary in the execution's memory.
    explicit FitSummary(std::pmr::memory_resource* memory) : inliers(memory), residuals(memory) {}

    /// The final least-squares model.
    StarTransform transform{};

    /// Whether each correspondence is inside the inlier threshold under that model.
    std::pmr::vector<bool> inliers;

    /// Each correspondence's residual under that model, in pixels.
    std::pmr::vector<double> residuals;

    /// Minimal sets whose model could not be solved.
    std::uint32_t degenerateSamples = 0u;

    /// Minimal sets tried.
    std::uint32_t attempts = 0u;
};

/// The fewest point pairs that determine a model: three for a similarity or affine map, four for a
/// projective one.
[[nodiscard]] std::uint32_t modelMinimum(StarTransformModel model) noexcept
{
    switch (model)
    {
    case StarTransformModel::Similarity:
    case StarTransformModel::Affine:
        return 3u;
    case StarTransformModel::Projective:
        return 4u;
    }
    return 0u;
}

[[nodiscard]] std::uint32_t requiredMatches(const StarAlignmentRequest& request) noexcept
{
    if (request.minimumMatches != 0u)
    {
        return request.minimumMatches;
    }
    return request.model == StarTransformModel::Projective ? 8u : 6u;
}

[[nodiscard]] Status checkRequest(const StarAlignmentRequest& request, const char* operation)
{
    if (!isStarTransformModel(request.model))
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "the transform model is not one this build estimates", "request.model");
    }
    if (!isStarTrianglePolicy(request.trianglePolicy))
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "the triangle policy is not one this build implements", "request.trianglePolicy");
    }
    if (request.maximumTriangles == 0u || request.maximumTriangles > kMaximumTriangles)
    {
        return rangeFailure(operation, "the triangle catalogue is bounded at one hundred thousand entries",
                            "request.maximumTriangles", request.maximumTriangles, kMaximumTriangles);
    }
    if (!std::isfinite(request.minimumTriangleSide) || request.minimumTriangleSide <= 0.0)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument,
                                "the minimum triangle side is finite and positive", "request.minimumTriangleSide");
    }
    if (!std::isfinite(request.triangleRatioTolerance) || request.triangleRatioTolerance < kMinimumRatioTolerance ||
        request.triangleRatioTolerance > 0.25)
    {
        return parameterFailure(operation, ErrorCode::OutOfRange,
                                "the triangle ratio tolerance is between 1e-6 and one quarter",
                                "request.triangleRatioTolerance");
    }
    if (!std::isfinite(request.triangleToleranceMinimumScale) ||
        request.triangleToleranceMinimumScale < kMinimumToleranceScale || request.triangleToleranceMinimumScale > 1.0)
    {
        return parameterFailure(operation, ErrorCode::OutOfRange,
                                "the minimum triangle tolerance scale is between one quarter and one",
                                "request.triangleToleranceMinimumScale");
    }
    if (!std::isfinite(request.triangleToleranceMaximumScale) ||
        request.triangleToleranceMaximumScale < request.triangleToleranceMinimumScale ||
        request.triangleToleranceMaximumScale > 1.0)
    {
        return parameterFailure(operation, ErrorCode::OutOfRange,
                                "the maximum triangle tolerance scale is ordered and no greater than one",
                                "request.triangleToleranceMaximumScale");
    }
    if (request.maximumTriangleNeighbours == 0u || request.maximumTriangleNeighbours > kMaximumTriangleNeighbours)
    {
        return rangeFailure(operation, "at most sixteen triangle neighbours may vote",
                            "request.maximumTriangleNeighbours", request.maximumTriangleNeighbours,
                            kMaximumTriangleNeighbours);
    }
    if (request.minimumVotes == 0u || request.minimumVotes > kMaximumVotes)
    {
        return rangeFailure(operation, "a correspondence asks for between one and sixty-four votes",
                            "request.minimumVotes", request.minimumVotes, kMaximumVotes);
    }
    const std::uint32_t mathematicalMinimum = modelMinimum(request.model);
    if (request.minimumMatches != 0u && request.minimumMatches < mathematicalMinimum)
    {
        return parameterFailure(operation, ErrorCode::OutOfRange,
                                "the requested match count is below the model's mathematical minimum",
                                "request.minimumMatches");
    }
    if (request.minimumMatches > kMaxStarsPerFrame)
    {
        return rangeFailure(operation, "the requested match count exceeds one frame's star ceiling",
                            "request.minimumMatches", request.minimumMatches, kMaxStarsPerFrame);
    }
    if (!std::isfinite(request.inlierThreshold) || request.inlierThreshold <= 0.0)
    {
        return parameterFailure(operation, ErrorCode::InvalidArgument, "the inlier threshold is finite and positive",
                                "request.inlierThreshold");
    }
    if (!std::isfinite(request.minimumInlierFraction) || request.minimumInlierFraction <= 0.0 ||
        request.minimumInlierFraction > 1.0)
    {
        return parameterFailure(operation, ErrorCode::OutOfRange,
                                "the minimum inlier fraction is above zero and at most one",
                                "request.minimumInlierFraction");
    }
    if (request.ransacIterations == 0u || request.ransacIterations > kMaximumRansacIterations)
    {
        return rangeFailure(operation, "the robust fit performs at most one hundred thousand attempts",
                            "request.ransacIterations", request.ransacIterations, kMaximumRansacIterations);
    }
    return Status::success();
}

[[nodiscard]] Status checkPoints(std::span<const StarAlignmentPoint> points, const char* operation,
                                 const char* parameter)
{
    if (points.size() > kMaxStarsPerFrame)
    {
        return Status::failure(
            ErrorCode::LimitExceeded,
            StatusDetail{.message = "a star field exceeds the per-frame point ceiling",
                         .operation = operation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Parameter, parameter),
                                     StatusContextField::ofCount(ContextKey::Requested, points.size()),
                                     StatusContextField::ofCount(ContextKey::Limit, kMaxStarsPerFrame)}});
    }
    for (const StarAlignmentPoint& point : points)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
        {
            return parameterFailure(operation, ErrorCode::InvalidData,
                                    "every star coordinate is a finite image position", parameter);
        }
    }
    return Status::success();
}

[[nodiscard]] double distance(const StarAlignmentPoint& first, const StarAlignmentPoint& second) noexcept
{
    return std::hypot(first.x - second.x, first.y - second.y);
}

/// Every distance between two points of one seed group, measured once in each direction exactly as a
/// triangle would measure it, so that each of the group's triangles reads its sides. `seedGroups` never
/// forms a group of more than `kTriangleSeedGroupSize` points.
class GroupDistances
{
public:
    GroupDistances(std::span<const StarAlignmentPoint> points, std::span<const std::uint32_t> group) noexcept
    {
        for (std::size_t from = 0u; from < group.size(); ++from)
        {
            for (std::size_t to = 0u; to < group.size(); ++to)
            {
                if (from != to)
                {
                    m_distances[from * kTriangleSeedGroupSize + to] = distance(points[group[from]], points[group[to]]);
                }
            }
        }
    }

    [[nodiscard]] double operator()(std::uint32_t from, std::uint32_t to) const noexcept
    {
        return m_distances[static_cast<std::size_t>(from) * kTriangleSeedGroupSize + to];
    }

private:
    std::array<double, static_cast<std::size_t>(kTriangleSeedGroupSize) * kTriangleSeedGroupSize> m_distances{};
};

/// The triangle on three of a group's points, named by their places in the group.
[[nodiscard]] Triangle triangleOf(std::span<const StarAlignmentPoint> points, std::span<const std::uint32_t> group,
                                  const GroupDistances& distances, std::uint32_t firstPlace, std::uint32_t secondPlace,
                                  std::uint32_t thirdPlace) noexcept
{
    const std::uint32_t first = group[firstPlace];
    const std::uint32_t second = group[secondPlace];
    const std::uint32_t third = group[thirdPlace];
    Triangle triangle;
    triangle.indices = {first, second, third};
    const double d01 = distances(firstPlace, secondPlace);
    const double d12 = distances(secondPlace, thirdPlace);
    const double d20 = distances(thirdPlace, firstPlace);
    triangle.largestSide = (std::max)({d01, d12, d20});
    if (triangle.largestSide <= kCoordinateEpsilon)
    {
        return triangle;
    }
    const double smallest = (std::min)({d01, d12, d20});
    if (smallest / triangle.largestSide < kTriangleSideRatioFloor)
    {
        return triangle;
    }
    const StarAlignmentPoint& p0 = points[first];
    const StarAlignmentPoint& p1 = points[second];
    const StarAlignmentPoint& p2 = points[third];
    const double twiceArea = std::abs((p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x));
    const double perimeter = d01 + d12 + d20;
    if (0.5 * twiceArea / (perimeter * perimeter) < kTriangleAreaFloor)
    {
        return triangle;
    }
    triangle.ratios = {d01 / triangle.largestSide, d12 / triangle.largestSide, d20 / triangle.largestSide};
    std::sort(triangle.ratios.begin(), triangle.ratios.end());
    std::array<std::pair<double, std::uint32_t>, 3> opposite{{{d12, 0u}, {d20, 1u}, {d01, 2u}}};
    std::sort(opposite.begin(), opposite.end(), [](const auto& left, const auto& right) {
        return std::tie(left.first, left.second) < std::tie(right.first, right.second);
    });
    for (std::uint32_t rank = 0u; rank < 3u; ++rank)
    {
        triangle.canonicalVertices[rank] = opposite[rank].second;
    }
    return triangle;
}

[[nodiscard]] bool usableTriangle(const Triangle& triangle, double minimumSide) noexcept
{
    return triangle.ratios[2] > 0.0 && triangle.largestSide >= minimumSide;
}

/// The consecutive brightness ranks `[start, start + wanted)`, cut to the field.
[[nodiscard]] std::pmr::vector<std::uint32_t> rankWindow(std::size_t count, std::uint32_t start, std::uint32_t wanted,
                                                         std::pmr::memory_resource* memory)
{
    std::pmr::vector<std::uint32_t> indices(memory);
    if (start >= count)
    {
        return indices;
    }
    const std::size_t available = count - start;
    const std::uint32_t taken = static_cast<std::uint32_t>((std::min)(available, static_cast<std::size_t>(wanted)));
    indices.reserve(taken);
    for (std::uint32_t index = 0u; index < taken; ++index)
    {
        indices.push_back(start + index);
    }
    return indices;
}

/// A seed group spread over the field: the brightest point of each occupied cell of an
/// eight-by-eight grid over the points' bounding box, then the brightest points not yet taken.
///
/// The rank windows alone would draw every triangle from wherever the brightest stars cluster; this
/// group gives a field whose bright stars sit in one corner triangles that span the frame.
[[nodiscard]] std::pmr::vector<std::uint32_t> spatialSeeds(std::span<const StarAlignmentPoint> points,
                                                           std::uint32_t wanted, std::pmr::memory_resource* memory)
{
    if (points.size() <= wanted)
    {
        return rankWindow(points.size(), 0u, static_cast<std::uint32_t>(points.size()), memory);
    }
    double minimumX = points.front().x;
    double maximumX = points.front().x;
    double minimumY = points.front().y;
    double maximumY = points.front().y;
    for (const StarAlignmentPoint& point : points)
    {
        minimumX = (std::min)(minimumX, point.x);
        maximumX = (std::max)(maximumX, point.x);
        minimumY = (std::min)(minimumY, point.y);
        maximumY = (std::max)(maximumY, point.y);
    }
    const double width = (std::max)(maximumX - minimumX, kCoordinateEpsilon);
    const double height = (std::max)(maximumY - minimumY, kCoordinateEpsilon);
    std::array<bool, kSpatialGridSide * kSpatialGridSide> occupied{};
    std::pmr::vector<bool> selected(points.size(), false, memory);
    std::pmr::vector<std::uint32_t> indices(memory);
    indices.reserve(wanted);
    for (std::uint32_t index = 0u; index < points.size() && indices.size() < wanted; ++index)
    {
        const StarAlignmentPoint& point = points[index];
        const std::uint32_t x = (std::min)(kSpatialGridSide - 1u,
                                           static_cast<std::uint32_t>((point.x - minimumX) / width * kSpatialGridSide));
        const std::uint32_t y =
            (std::min)(kSpatialGridSide - 1u,
                       static_cast<std::uint32_t>((point.y - minimumY) / height * kSpatialGridSide));
        const std::uint32_t cell = y * kSpatialGridSide + x;
        if (!occupied[cell])
        {
            occupied[cell] = true;
            selected[index] = true;
            indices.push_back(index);
        }
    }
    for (std::uint32_t index = 0u; index < points.size() && indices.size() < wanted; ++index)
    {
        if (!selected[index])
        {
            indices.push_back(index);
        }
    }
    return indices;
}

/// The groups triangles are formed inside: the whole field when it has at most sixty points,
/// otherwise the sixty brightest, the sixty from rank forty and the spatial spread, with a group
/// that repeats an earlier one dropped.
[[nodiscard]] std::pmr::vector<std::pmr::vector<std::uint32_t>> seedGroups(std::span<const StarAlignmentPoint> points,
                                                                           std::pmr::memory_resource* memory)
{
    std::pmr::vector<std::pmr::vector<std::uint32_t>> groups(memory);
    if (points.size() <= kTriangleSeedGroupSize)
    {
        groups.push_back(rankWindow(points.size(), 0u, static_cast<std::uint32_t>(points.size()), memory));
        return groups;
    }
    auto add = [&](std::pmr::vector<std::uint32_t> group) {
        if (group.size() >= 3u && std::find(groups.begin(), groups.end(), group) == groups.end())
        {
            groups.push_back(std::move(group));
        }
    };
    add(rankWindow(points.size(), 0u, kTriangleSeedGroupSize, memory));
    add(rankWindow(points.size(), kSecondTriangleWindowStart, kTriangleSeedGroupSize, memory));
    add(spatialSeeds(points, kTriangleSeedGroupSize, memory));
    return groups;
}

/// How many triangles `count` points form.
[[nodiscard]] std::uint64_t combinationCount(std::uint32_t count) noexcept
{
    return count < 3u ? 0u : static_cast<std::uint64_t>(count) * (count - 1u) * (count - 2u) / 6u;
}

/// The three places in a group of `count` that the `ordinal`-th triangle joins, counting the
/// triangles `first < second < third` in lexicographic order - the order the exhaustive loops visit
/// them in - so bounded sampling can name a triangle without enumerating the ones before it.
[[nodiscard]] std::array<std::uint32_t, 3> decodeOrdinal(std::uint64_t ordinal, std::uint32_t count) noexcept
{
    for (std::uint32_t first = 0u; first + 2u < count; ++first)
    {
        const std::uint64_t remaining = count - first - 1u;
        const std::uint64_t block = remaining * (remaining - 1u) / 2u;
        if (ordinal >= block)
        {
            ordinal -= block;
            continue;
        }
        for (std::uint32_t second = first + 1u; second + 1u < count; ++second)
        {
            const std::uint64_t secondBlock = count - second - 1u;
            if (ordinal >= secondBlock)
            {
                ordinal -= secondBlock;
                continue;
            }
            return {first, second, static_cast<std::uint32_t>(second + 1u + ordinal)};
        }
    }
    return {0u, 1u, 2u};
}

/// Builds a field's triangle catalogue. A prepared reference's catalogue is ordered longest first,
/// because its search breaks a tie by a triangle's place in it; a source's order reaches nothing
/// downstream, so it is ordered only when it has to be cut to the requested size.
[[nodiscard]] Result<TriangleBuild> buildTriangles(const ExecutionContext& execution,
                                                   std::span<const StarAlignmentPoint> points,
                                                   const StarAlignmentRequest& request, bool longestFirst,
                                                   std::pmr::memory_resource* memory)
{
    TriangleBuild build(memory);
    build.executedPolicy =
        request.trianglePolicy == StarTrianglePolicy::Automatic
            ? (points.size() <= kTriangleSeedGroupSize ? StarTrianglePolicy::Exhaustive : StarTrianglePolicy::Bounded)
            : request.trianglePolicy;
    std::pmr::vector<std::pmr::vector<std::uint32_t>> groups = seedGroups(points, memory);
    if (groups.empty())
    {
        return build;
    }
    if (build.executedPolicy == StarTrianglePolicy::Exhaustive)
    {
        const std::uint32_t perGroup =
            (std::max)(1u, request.maximumTriangles / static_cast<std::uint32_t>(groups.size()));
        for (const std::pmr::vector<std::uint32_t>& group : groups)
        {
            const std::uint64_t candidateCount = combinationCount(static_cast<std::uint32_t>(group.size()));
            build.candidatesExamined += candidateCount;
            const std::uint64_t firstCount = group.size() - 2u;
            const TaskCost triangleCost{.bytesPerItem = static_cast<std::uint64_t>(group.size()) * sizeof(Triangle),
                                        .operationsPerItem = 20000u};
            std::pmr::vector<Triangle> local(static_cast<std::size_t>(candidateCount), memory);
            std::pmr::vector<std::uint8_t> kept(static_cast<std::size_t>(candidateCount), 0u, memory);
            const GroupDistances distances(points, group);
            const Status generated = parallelFor(execution, firstCount, triangleCost, [&](TaskRange range) {
                for (std::uint32_t first = static_cast<std::uint32_t>(range.begin); first < range.end; ++first)
                {
                    const Status running = execution.checkCancellation();
                    if (running.failed())
                    {
                        return running;
                    }
                    std::uint64_t ordinal =
                        candidateCount - combinationCount(static_cast<std::uint32_t>(group.size()) - first);
                    for (std::uint32_t second = first + 1u; second + 1u < group.size(); ++second)
                    {
                        for (std::uint32_t third = second + 1u; third < group.size(); ++third)
                        {
                            Triangle triangle = triangleOf(points, group, distances, first, second, third);
                            if (usableTriangle(triangle, request.minimumTriangleSide))
                            {
                                local[static_cast<std::size_t>(ordinal)] = std::move(triangle);
                                kept[static_cast<std::size_t>(ordinal)] = 1u;
                            }
                            ++ordinal;
                        }
                    }
                }
                return Status::success();
            });
            if (generated.failed())
            {
                return generated;
            }
            std::size_t retained = 0u;
            for (std::size_t candidate = 0u; candidate < local.size(); ++candidate)
            {
                if (kept[candidate] != 0u)
                {
                    local[retained++] = std::move(local[candidate]);
                }
            }
            local.resize(retained);
            if (local.size() > perGroup)
            {
                std::partial_sort(local.begin(), local.begin() + perGroup, local.end(),
                                  [](const Triangle& left, const Triangle& right) {
                                      if (left.largestSide != right.largestSide)
                                      {
                                          return left.largestSide > right.largestSide;
                                      }
                                      return left.indices < right.indices;
                                  });
                local.resize(perGroup);
            }
            build.triangles.insert(build.triangles.end(), std::make_move_iterator(local.begin()),
                                   std::make_move_iterator(local.end()));
        }
    }
    else
    {
        const std::uint32_t base = request.maximumTriangles / static_cast<std::uint32_t>(groups.size());
        const std::uint32_t remainder = request.maximumTriangles % static_cast<std::uint32_t>(groups.size());
        for (std::uint32_t groupIndex = 0u; groupIndex < groups.size(); ++groupIndex)
        {
            const std::pmr::vector<std::uint32_t>& group = groups[groupIndex];
            const std::uint64_t available = combinationCount(static_cast<std::uint32_t>(group.size()));
            const std::uint64_t wanted =
                (std::min)(available, static_cast<std::uint64_t>(base + (groupIndex < remainder ? 1u : 0u)));
            build.candidatesExamined += wanted;
            std::pmr::vector<Triangle> local(static_cast<std::size_t>(wanted), memory);
            std::pmr::vector<std::uint8_t> kept(static_cast<std::size_t>(wanted), 0u, memory);
            const TaskCost triangleCost{.bytesPerItem = sizeof(Triangle), .operationsPerItem = 1000u};
            const GroupDistances distances(points, group);
            const Status generated = parallelFor(execution, wanted, triangleCost, [&](TaskRange range) {
                for (std::uint64_t sample = range.begin; sample < range.end; ++sample)
                {
                    const std::uint64_t ordinal =
                        wanted == 1u ? available / 2u : sample * (available - 1u) / (wanted - 1u);
                    const std::array<std::uint32_t, 3> indices =
                        decodeOrdinal(ordinal, static_cast<std::uint32_t>(group.size()));
                    Triangle triangle = triangleOf(points, group, distances, indices[0], indices[1], indices[2]);
                    if (usableTriangle(triangle, request.minimumTriangleSide))
                    {
                        local[static_cast<std::size_t>(sample)] = std::move(triangle);
                        kept[static_cast<std::size_t>(sample)] = 1u;
                    }
                }
                return Status::success();
            });
            if (generated.failed())
            {
                return generated;
            }
            std::size_t retained = 0u;
            for (std::size_t candidate = 0u; candidate < local.size(); ++candidate)
            {
                if (kept[candidate] != 0u)
                {
                    local[retained++] = std::move(local[candidate]);
                }
            }
            local.resize(retained);
            build.triangles.insert(build.triangles.end(), std::make_move_iterator(local.begin()),
                                   std::make_move_iterator(local.end()));
        }
    }
    if (groups.size() > 1u)
    {
        std::sort(build.triangles.begin(), build.triangles.end(), [](const Triangle& left, const Triangle& right) {
            if (left.indices != right.indices)
            {
                return left.indices < right.indices;
            }
            return left.largestSide > right.largestSide;
        });
        build.triangles.erase(
            std::unique(build.triangles.begin(), build.triangles.end(),
                        [](const Triangle& left, const Triangle& right) { return left.indices == right.indices; }),
            build.triangles.end());
    }
    if (!longestFirst && build.triangles.size() <= request.maximumTriangles)
    {
        return build;
    }
    // Longest first, then by vertex indices. The indices are unique among the triangles here, so the
    // order is total and its result is the one sorted sequence whatever sorts it; the keys are sorted
    // instead of the triangles, which are several times larger, and each triangle is moved once.
    struct OrderKey
    {
        double largestSide = 0.0;
        std::array<std::uint32_t, 3> indices{};
        std::uint32_t position = 0u;
    };
    std::pmr::vector<OrderKey> keys(build.triangles.size(), memory);
    for (std::size_t position = 0u; position < build.triangles.size(); ++position)
    {
        keys[position] = {.largestSide = build.triangles[position].largestSide,
                          .indices = build.triangles[position].indices,
                          .position = static_cast<std::uint32_t>(position)};
    }
    std::sort(keys.begin(), keys.end(), [](const OrderKey& left, const OrderKey& right) {
        if (left.largestSide != right.largestSide)
        {
            return left.largestSide > right.largestSide;
        }
        return left.indices < right.indices;
    });
    const std::size_t kept = (std::min)(keys.size(), static_cast<std::size_t>(request.maximumTriangles));
    std::pmr::vector<Triangle> ordered(memory);
    ordered.reserve(kept);
    for (std::size_t rank = 0u; rank < kept; ++rank)
    {
        ordered.push_back(std::move(build.triangles[keys[rank].position]));
    }
    build.triangles = std::move(ordered);
    return build;
}

[[nodiscard]] std::int64_t gridCellOf(double ratio, double cellSize) noexcept
{
    return static_cast<std::int64_t>(std::floor(ratio / cellSize));
}

/// The squared distance between a query's two smallest side ratios and a candidate's. The grid's
/// search and the exhaustive one both measure with it, so they agree on every tie.
[[nodiscard]] double ratioDistanceSquared(double queryFirst, double querySecond, double first, double second) noexcept
{
    const double firstDifference = queryFirst - first;
    const double secondDifference = querySecond - second;
    return firstDifference * firstDifference + secondDifference * secondDifference;
}

/// Places the triangles in cells half the smallest search radius wide, so that a query's nearest
/// neighbours are usually within a ring or two of its own cell, unless that would give the grid more
/// than about four cells per triangle, which a small tolerance would.
[[nodiscard]] TriangleGrid buildTriangleGrid(std::span<const Triangle> triangles, const StarAlignmentRequest& request)
{
    TriangleGrid grid;
    if (triangles.empty())
    {
        return grid;
    }
    std::array<double, 2> low{triangles[0].ratios[0], triangles[0].ratios[1]};
    std::array<double, 2> high = low;
    for (const Triangle& triangle : triangles)
    {
        for (std::size_t axis = 0u; axis < 2u; ++axis)
        {
            low[axis] = (std::min)(low[axis], triangle.ratios[axis]);
            high[axis] = (std::max)(high[axis], triangle.ratios[axis]);
        }
    }
    const double side = std::ceil(2.0 * std::sqrt(static_cast<double>(triangles.size())));
    grid.cellSize = (std::max)(0.5 * request.triangleRatioTolerance * request.triangleToleranceMinimumScale,
                               (std::max)(high[0] - low[0], high[1] - low[1]) / side);
    grid.firstColumn = gridCellOf(low[0], grid.cellSize);
    grid.firstRow = gridCellOf(low[1], grid.cellSize);
    grid.columns = gridCellOf(high[0], grid.cellSize) - grid.firstColumn + 1;
    grid.rows = gridCellOf(high[1], grid.cellSize) - grid.firstRow + 1;
    const auto cellOf = [&grid](const Triangle& triangle) {
        return static_cast<std::size_t>((gridCellOf(triangle.ratios[1], grid.cellSize) - grid.firstRow) * grid.columns +
                                        gridCellOf(triangle.ratios[0], grid.cellSize) - grid.firstColumn);
    };
    grid.starts.assign(static_cast<std::size_t>(grid.columns * grid.rows) + 1u, 0u);
    for (const Triangle& triangle : triangles)
    {
        ++grid.starts[cellOf(triangle) + 1u];
    }
    std::partial_sum(grid.starts.begin(), grid.starts.end(), grid.starts.begin());
    std::vector<std::uint32_t> next(grid.starts.begin(), grid.starts.end() - 1);
    grid.entries.resize(triangles.size());
    for (std::uint32_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
    {
        const Triangle& triangle = triangles[triangleIndex];
        grid.entries[next[cellOf(triangle)]++] = {
            .first = triangle.ratios[0], .second = triangle.ratios[1], .triangleIndex = triangleIndex};
    }
    return grid;
}

/// The nearest triangles offered to one query, at most `capacity` of them, ordered by squared ratio
/// distance and then by index, among those inside the search radius. What it keeps does not depend on
/// the order they are offered in.
class NearestTriangles
{
public:
    NearestTriangles(std::uint32_t capacity, double radiusSquared) noexcept
        : m_capacity(capacity), m_limit(radiusSquared)
    {
    }

    /// The squared distance past which nothing offered is kept: the radius's until the list is full,
    /// and its last entry's after that.
    [[nodiscard]] double limit() const noexcept
    {
        return m_limit;
    }

    [[nodiscard]] std::span<const Neighbour> kept() const noexcept
    {
        return {m_nearest.data(), m_count};
    }

    void offer(double distanceSquared, std::uint32_t triangleIndex) noexcept
    {
        if (distanceSquared > m_limit)
        {
            return;
        }
        const Neighbour neighbour{.distanceSquared = distanceSquared, .triangleIndex = triangleIndex};
        // A full list takes a neighbour only ahead of its last entry, which at an equal distance the
        // index decides.
        if (m_count == m_capacity &&
            !(std::tie(neighbour.distanceSquared, neighbour.triangleIndex) <
              std::tie(m_nearest[m_count - 1u].distanceSquared, m_nearest[m_count - 1u].triangleIndex)))
        {
            return;
        }
        auto position = m_nearest.begin();
        const auto end = m_nearest.begin() + m_count;
        while (position != end && std::tie(position->distanceSquared, position->triangleIndex) <=
                                      std::tie(neighbour.distanceSquared, neighbour.triangleIndex))
        {
            ++position;
        }
        if (m_count < m_capacity)
        {
            std::move_backward(position, end, end + 1);
            ++m_count;
        }
        else
        {
            std::move_backward(position, end - 1, end);
        }
        *position = neighbour;
        if (m_count == m_capacity)
        {
            m_limit = m_nearest[m_count - 1u].distanceSquared;
        }
    }

private:
    std::array<Neighbour, kMaximumTriangleNeighbours> m_nearest{};
    std::uint32_t m_count = 0u;
    std::uint32_t m_capacity = 0u;
    double m_limit = 0.0;
};

/// Offers a query the grid's triangles ring of cells by ring of cells around its own, and stops at the
/// first ring farther from it than anything the list would still keep.
void searchTriangleGrid(const TriangleGrid& grid, double queryFirst, double querySecond,
                        NearestTriangles& nearest) noexcept
{
    if (grid.entries.empty())
    {
        return;
    }
    const double cellSize = grid.cellSize;
    const std::int64_t column = gridCellOf(queryFirst, cellSize);
    const std::int64_t row = gridCellOf(querySecond, cellSize);
    const std::int64_t lastColumn = grid.firstColumn + grid.columns - 1;
    const std::int64_t lastRow = grid.firstRow + grid.rows - 1;
    // One row's cells from one column to another are one run of entries.
    const auto scan = [&](std::int64_t atRow, std::int64_t fromColumn, std::int64_t toColumn) {
        fromColumn = (std::max)(fromColumn, grid.firstColumn);
        toColumn = (std::min)(toColumn, lastColumn);
        if (atRow < grid.firstRow || atRow > lastRow || fromColumn > toColumn)
        {
            return;
        }
        const std::int64_t rowStart = (atRow - grid.firstRow) * grid.columns - grid.firstColumn;
        const std::uint32_t end = grid.starts[static_cast<std::size_t>(rowStart + toColumn) + 1u];
        for (std::uint32_t entry = grid.starts[static_cast<std::size_t>(rowStart + fromColumn)]; entry < end; ++entry)
        {
            const TriangleGrid::Entry& candidate = grid.entries[entry];
            nearest.offer(ratioDistanceSquared(queryFirst, querySecond, candidate.first, candidate.second),
                          candidate.triangleIndex);
        }
    };
    scan(row, column, column);
    for (std::int64_t ring = 1;; ++ring)
    {
        if (column - ring < grid.firstColumn && column + ring > lastColumn && row - ring < grid.firstRow &&
            row + ring > lastRow)
        {
            return;
        }
        // Everything in this ring and beyond it is at least `gap` from the query along one axis. The
        // slack is far above every rounding in placing a triangle or the query in its cell and in
        // squaring, so a ring is passed over only when nothing in it could be kept.
        const double gap = (std::min)({queryFirst - static_cast<double>(column - ring + 1) * cellSize,
                                       static_cast<double>(column + ring) * cellSize - queryFirst,
                                       querySecond - static_cast<double>(row - ring + 1) * cellSize,
                                       static_cast<double>(row + ring) * cellSize - querySecond});
        const double magnitude = std::abs(queryFirst) + std::abs(querySecond) +
                                 cellSize * static_cast<double>(std::abs(column) + std::abs(row) + 2 * ring + 2);
        const double clear = gap - 1.0e-9 * magnitude;
        if (clear > 0.0 && clear * clear > nearest.limit())
        {
            return;
        }
        scan(row - ring, column - ring, column + ring);
        scan(row + ring, column - ring, column + ring);
        for (std::int64_t atRow = (std::max)(row - ring + 1, grid.firstRow);
             atRow <= (std::min)(row + ring - 1, lastRow); ++atRow)
        {
            scan(atRow, column - ring, column - ring);
            scan(atRow, column + ring, column + ring);
        }
    }
}

/// Every source triangle's nearest reference triangles, in source order.
///
/// Each source triangle keeps at most `maximumTriangleNeighbours` reference triangles inside a
/// radius of `triangleRatioTolerance` scaled by its own shape, clamped to the request's two scales.
/// Workers write each source triangle's neighbours into its own slot, and the slots are compacted in
/// source order afterwards.
[[nodiscard]] Result<std::pmr::vector<TrianglePair>>
matchTriangles(const ExecutionContext& execution, std::span<const Triangle> source, std::span<const Triangle> reference,
               const TriangleGrid& grid, const StarAlignmentRequest& request, std::pmr::memory_resource* memory)
{
    std::pmr::vector<TrianglePair> matches(source.size() * request.maximumTriangleNeighbours, memory);
    std::pmr::vector<std::uint8_t> counts(source.size(), 0u, memory);
    // The reference backend offers every reference triangle to every query, which is the answer the
    // grid's search has to give; forcing it makes a comparison of the two.
    const bool exhaustive = execution.backend() == ComputeBackend::Reference;
    const TaskCost matchCost{
        .bytesPerItem = static_cast<std::uint64_t>(request.maximumTriangleNeighbours) * sizeof(TrianglePair),
        .operationsPerItem = exhaustive ? 8u * static_cast<std::uint64_t>(reference.size()) : 256u};
    const Status matched = parallelFor(execution, source.size(), matchCost, [&](TaskRange range) {
        for (std::uint32_t sourceIndex = static_cast<std::uint32_t>(range.begin); sourceIndex < range.end;
             ++sourceIndex)
        {
            const Triangle& triangle = source[sourceIndex];
            const double scale =
                (std::clamp)(0.5 * (triangle.ratios[0] + triangle.ratios[1]), request.triangleToleranceMinimumScale,
                             request.triangleToleranceMaximumScale);
            const double radius = request.triangleRatioTolerance * scale;
            NearestTriangles nearest(request.maximumTriangleNeighbours, radius * radius);
            if (exhaustive)
            {
                for (std::uint32_t referenceIndex = 0u; referenceIndex < reference.size(); ++referenceIndex)
                {
                    nearest.offer(ratioDistanceSquared(triangle.ratios[0], triangle.ratios[1],
                                                       reference[referenceIndex].ratios[0],
                                                       reference[referenceIndex].ratios[1]),
                                  referenceIndex);
                }
            }
            else
            {
                searchTriangleGrid(grid, triangle.ratios[0], triangle.ratios[1], nearest);
            }
            const std::span<const Neighbour> kept = nearest.kept();
            counts[sourceIndex] = static_cast<std::uint8_t>(kept.size());
            const std::size_t slot = static_cast<std::size_t>(sourceIndex) * request.maximumTriangleNeighbours;
            for (std::size_t neighbour = 0u; neighbour < kept.size(); ++neighbour)
            {
                matches[slot + neighbour] = {.source = sourceIndex, .reference = kept[neighbour].triangleIndex};
            }
        }
        return Status::success();
    });
    if (matched.failed())
    {
        return matched;
    }
    std::size_t written = 0u;
    for (std::size_t sourceIndex = 0u; sourceIndex < source.size(); ++sourceIndex)
    {
        const std::size_t slot = sourceIndex * request.maximumTriangleNeighbours;
        for (std::uint32_t neighbour = 0u; neighbour < counts[sourceIndex]; ++neighbour)
        {
            matches[written++] = matches[slot + neighbour];
        }
    }
    matches.resize(written);
    return matches;
}

/// The vertex opposite each side, with the sides in the order the three edges join vertices 0-1, 1-2
/// and 2-0.
constexpr std::array<std::uint32_t, 3> kOppositeVertices{2u, 0u, 1u};

/// Pairs two triangles' vertices by the rank of the side opposite each, and sums how far their sides
/// over their largest differ under that pairing, side by side in edge order. A side over the largest is
/// the ratio at the rank of the vertex opposite it - the ratios are those same quotients, sorted - and
/// the paired sides share that rank, so both are read rather than divided again.
[[nodiscard]] std::pair<std::array<std::uint32_t, 3>, double> bestPermutation(const Triangle& source,
                                                                              const Triangle& reference) noexcept
{
    std::array<std::uint32_t, 3> permutation{};
    std::array<std::uint32_t, 3> sourceRank{};
    for (std::uint32_t rank = 0u; rank < 3u; ++rank)
    {
        permutation[source.canonicalVertices[rank]] = reference.canonicalVertices[rank];
        sourceRank[source.canonicalVertices[rank]] = rank;
    }
    double error = 0.0;
    for (const std::uint32_t opposite : kOppositeVertices)
    {
        const std::uint32_t rank = sourceRank[opposite];
        error += std::abs(source.ratios[rank] - reference.ratios[rank]);
    }
    return {permutation, error};
}

/// Turns triangle pairs into one-to-one point pairs by voting.
///
/// A triangle pair whose paired sides differ by more than nine ratio tolerances in total casts no
/// vote; every other one votes for its three vertex pairings. Votes are counted in a dense table
/// when the two fields' sizes multiply to at most 1,048,576 cells and by sorting otherwise. Pairings
/// with at least `minimumVotes` votes are then taken greedily, most votes first and indices
/// breaking ties, skipping any whose source or reference point is already taken. The result is in
/// that order.
[[nodiscard]] Result<std::pmr::vector<CandidateMatch>>
buildConsensus(const ExecutionContext& execution, std::span<const TrianglePair> triangleMatches,
               std::span<const Triangle> sourceTriangles, std::span<const Triangle> referenceTriangles,
               std::uint32_t sourceCount, std::uint32_t referenceCount, const StarAlignmentRequest& request,
               std::pmr::memory_resource* memory)
{
    auto visitVotes = [&](auto&& visitor) -> Status {
        for (std::size_t matchIndex = 0u; matchIndex < triangleMatches.size(); ++matchIndex)
        {
            if ((matchIndex & 255u) == 0u)
            {
                const Status running = execution.checkCancellation();
                if (running.failed())
                {
                    return running;
                }
            }
            const Triangle& source = sourceTriangles[triangleMatches[matchIndex].source];
            const Triangle& reference = referenceTriangles[triangleMatches[matchIndex].reference];
            const auto [permutation, error] = bestPermutation(source, reference);
            if (error > request.triangleRatioTolerance * 9.0)
            {
                continue;
            }
            for (std::uint32_t vertex = 0u; vertex < 3u; ++vertex)
            {
                visitor(source.indices[vertex], reference.indices[permutation[vertex]]);
            }
        }
        return Status::success();
    };

    std::pmr::vector<CandidateMatch> candidates(memory);
    constexpr std::uint64_t kDenseVoteCellLimit = 1u << 20u;
    const std::uint64_t voteCells = static_cast<std::uint64_t>(sourceCount) * referenceCount;
    if (voteCells <= kDenseVoteCellLimit)
    {
        std::pmr::vector<std::uint32_t> counts(static_cast<std::size_t>(voteCells), 0u, memory);
        const Status visited = visitVotes([&](std::uint32_t source, std::uint32_t reference) {
            ++counts[static_cast<std::uint64_t>(source) * referenceCount + reference];
        });
        if (visited.failed())
        {
            return visited;
        }
        for (std::uint32_t source = 0u; source < sourceCount; ++source)
        {
            for (std::uint32_t reference = 0u; reference < referenceCount; ++reference)
            {
                const std::uint32_t count = counts[static_cast<std::uint64_t>(source) * referenceCount + reference];
                if (count >= request.minimumVotes)
                {
                    candidates.push_back({.source = source, .reference = reference, .votes = count});
                }
            }
        }
    }
    else
    {
        std::pmr::vector<Vote> votes(memory);
        votes.reserve(triangleMatches.size() * 3u);
        const Status visited = visitVotes([&](std::uint32_t source, std::uint32_t reference) {
            votes.push_back({.source = source, .reference = reference});
        });
        if (visited.failed())
        {
            return visited;
        }
        std::sort(votes.begin(), votes.end(), [](const Vote& left, const Vote& right) {
            return std::tie(left.source, left.reference) < std::tie(right.source, right.reference);
        });
        for (std::size_t first = 0u; first < votes.size();)
        {
            std::size_t last = first + 1u;
            while (last < votes.size() && votes[last].source == votes[first].source &&
                   votes[last].reference == votes[first].reference)
            {
                ++last;
            }
            const std::size_t count = last - first;
            if (count >= request.minimumVotes)
            {
                candidates.push_back({.source = votes[first].source,
                                      .reference = votes[first].reference,
                                      .votes = static_cast<std::uint32_t>(count)});
            }
            first = last;
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const CandidateMatch& left, const CandidateMatch& right) {
        if (left.votes != right.votes)
        {
            return left.votes > right.votes;
        }
        return std::tie(left.source, left.reference) < std::tie(right.source, right.reference);
    });
    std::pmr::vector<bool> sourceUsed(sourceCount, false, memory);
    std::pmr::vector<bool> referenceUsed(referenceCount, false, memory);
    std::pmr::vector<CandidateMatch> unique(memory);
    unique.reserve(candidates.size());
    for (const CandidateMatch& candidate : candidates)
    {
        if (!sourceUsed[candidate.source] && !referenceUsed[candidate.reference])
        {
            sourceUsed[candidate.source] = true;
            referenceUsed[candidate.reference] = true;
            unique.push_back(candidate);
        }
    }
    return unique;
}

/// Solves an `unknowns`-square system held in an eight-column table, by Gauss-Jordan elimination
/// with partial pivoting in extended precision.
///
/// @return false, leaving `answer` partly written, when a pivot falls to 1e-14 of the largest entry
///         or a solution component is not finite - which is how a degenerate minimal set shows up.
[[nodiscard]] bool solveNormalEquations(std::array<long double, 64>& matrix, std::array<long double, 8>& right,
                                        std::uint32_t unknowns, std::array<double, 8>& answer) noexcept
{
    long double scale = 0.0L;
    for (std::uint32_t row = 0u; row < unknowns; ++row)
    {
        for (std::uint32_t column = 0u; column < unknowns; ++column)
        {
            scale = (std::max)(scale, std::abs(matrix[row * 8u + column]));
        }
    }
    if (scale == 0.0L)
    {
        return false;
    }
    for (std::uint32_t pivot = 0u; pivot < unknowns; ++pivot)
    {
        std::uint32_t best = pivot;
        for (std::uint32_t row = pivot + 1u; row < unknowns; ++row)
        {
            if (std::abs(matrix[row * 8u + pivot]) > std::abs(matrix[best * 8u + pivot]))
            {
                best = row;
            }
        }
        if (std::abs(matrix[best * 8u + pivot]) <= scale * 1.0e-14L)
        {
            return false;
        }
        if (best != pivot)
        {
            for (std::uint32_t column = pivot; column < unknowns; ++column)
            {
                std::swap(matrix[pivot * 8u + column], matrix[best * 8u + column]);
            }
            std::swap(right[pivot], right[best]);
        }
        const long double divisor = matrix[pivot * 8u + pivot];
        for (std::uint32_t column = pivot; column < unknowns; ++column)
        {
            matrix[pivot * 8u + column] /= divisor;
        }
        right[pivot] /= divisor;
        for (std::uint32_t row = 0u; row < unknowns; ++row)
        {
            if (row == pivot)
            {
                continue;
            }
            const long double factor = matrix[row * 8u + pivot];
            for (std::uint32_t column = pivot; column < unknowns; ++column)
            {
                matrix[row * 8u + column] -= factor * matrix[pivot * 8u + column];
            }
            right[row] -= factor * right[pivot];
        }
    }
    for (std::uint32_t index = 0u; index < unknowns; ++index)
    {
        answer[index] = static_cast<double>(right[index]);
        if (!std::isfinite(answer[index]))
        {
            return false;
        }
    }
    return true;
}

/// Adds one linear equation `row . parameters = value` to the normal equations.
void accumulateEquation(std::array<long double, 64>& normal, std::array<long double, 8>& right,
                        const std::array<long double, 8>& row, long double value, std::uint32_t unknowns) noexcept
{
    for (std::uint32_t first = 0u; first < unknowns; ++first)
    {
        right[first] += row[first] * value;
        for (std::uint32_t second = 0u; second < unknowns; ++second)
        {
            normal[first * 8u + second] += row[first] * row[second];
        }
    }
}

/// Least-squares fit of one model to the selected pairs.
///
/// A similarity is `[a -b tx; b a ty]`, four unknowns; an affine map has six; a projective one has
/// eight with the last coefficient fixed at one, linearised by multiplying through by the
/// denominator - the direct linear transform, which minimises an algebraic rather than a geometric
/// residual. Each pair contributes one equation per axis.
///
/// @return false when the system has no usable solution, leaving `transform` unchanged.
[[nodiscard]] bool fitModel(std::span<const StarAlignmentPoint> source, std::span<const StarAlignmentPoint> reference,
                            std::span<const std::uint32_t> selected, StarTransformModel model,
                            StarTransform& transform) noexcept
{
    const std::uint32_t unknowns =
        model == StarTransformModel::Similarity ? 4u : (model == StarTransformModel::Affine ? 6u : 8u);
    std::array<long double, 64> normal{};
    std::array<long double, 8> right{};
    for (std::uint32_t index : selected)
    {
        const long double x = source[index].x;
        const long double y = source[index].y;
        const long double u = reference[index].x;
        const long double v = reference[index].y;
        if (model == StarTransformModel::Similarity)
        {
            accumulateEquation(normal, right, {x, -y, 1.0L, 0.0L}, u, unknowns);
            accumulateEquation(normal, right, {y, x, 0.0L, 1.0L}, v, unknowns);
        }
        else if (model == StarTransformModel::Affine)
        {
            accumulateEquation(normal, right, {x, y, 1.0L, 0.0L, 0.0L, 0.0L}, u, unknowns);
            accumulateEquation(normal, right, {0.0L, 0.0L, 0.0L, x, y, 1.0L}, v, unknowns);
        }
        else
        {
            accumulateEquation(normal, right, {x, y, 1.0L, 0.0L, 0.0L, 0.0L, -u * x, -u * y}, u, unknowns);
            accumulateEquation(normal, right, {0.0L, 0.0L, 0.0L, x, y, 1.0L, -v * x, -v * y}, v, unknowns);
        }
    }
    std::array<double, 8> parameters{};
    if (!solveNormalEquations(normal, right, unknowns, parameters))
    {
        return false;
    }
    transform.model = model;
    if (model == StarTransformModel::Similarity)
    {
        transform.sourceToReference = {
            parameters[0], -parameters[1], parameters[2], parameters[1], parameters[0], parameters[3], 0.0, 0.0, 1.0};
    }
    else if (model == StarTransformModel::Affine)
    {
        transform.sourceToReference = {
            parameters[0], parameters[1], parameters[2], parameters[3], parameters[4], parameters[5], 0.0, 0.0, 1.0};
    }
    else
    {
        transform.sourceToReference = {parameters[0], parameters[1], parameters[2],
                                       parameters[3], parameters[4], parameters[5],
                                       parameters[6], parameters[7], 1.0};
    }
    return true;
}

[[nodiscard]] bool mappedPoint(const StarTransform& transform, const StarAlignmentPoint& source,
                               StarAlignmentPoint& destination) noexcept
{
    const auto& matrix = transform.sourceToReference;
    const double denominator = matrix[6] * source.x + matrix[7] * source.y + matrix[8];
    if (!std::isfinite(denominator) || std::abs(denominator) <= kCoordinateEpsilon)
    {
        return false;
    }
    destination.x = (matrix[0] * source.x + matrix[1] * source.y + matrix[2]) / denominator;
    destination.y = (matrix[3] * source.x + matrix[4] * source.y + matrix[5]) / denominator;
    return std::isfinite(destination.x) && std::isfinite(destination.y);
}

[[nodiscard]] double residualOf(const StarTransform& transform, const StarAlignmentPoint& source,
                                const StarAlignmentPoint& reference) noexcept
{
    StarAlignmentPoint mapped{};
    if (!mappedPoint(transform, source, mapped))
    {
        return std::numeric_limits<double>::infinity();
    }
    return std::hypot(mapped.x - reference.x, mapped.y - reference.y);
}

/// Marsaglia's 64-bit xorshift generator with shifts 13, 7 and 17, seeded by the request.
///
/// It is fully specified by its state and three shifts, and indices are drawn from it by a plain
/// remainder, so one seed picks the same minimal sets on every platform and standard library - which
/// a standard distribution does not promise. Zero is its one fixed point, so a zero seed starts from
/// one.
class DeterministicSampler
{
public:
    /// Starts the sequence at `seed`, or at one when `seed` is zero.
    explicit DeterministicSampler(std::uint64_t seed) noexcept : m_state(seed == 0u ? 1u : seed) {}

    /// The next value of the sequence.
    [[nodiscard]] std::uint64_t next() noexcept
    {
        m_state ^= m_state << 13u;
        m_state ^= m_state >> 7u;
        m_state ^= m_state << 17u;
        return m_state;
    }

private:
    std::uint64_t m_state;
};

/// `wanted` distinct indices below `count`, drawn by rejection and returned ascending.
[[nodiscard]] std::pmr::vector<std::uint32_t> sampleIndices(DeterministicSampler& sampler, std::uint32_t count,
                                                            std::uint32_t wanted, std::pmr::memory_resource* memory)
{
    std::pmr::vector<std::uint32_t> selected(memory);
    selected.reserve(wanted);
    while (selected.size() < wanted)
    {
        const std::uint32_t candidate = static_cast<std::uint32_t>(sampler.next() % count);
        if (std::find(selected.begin(), selected.end(), candidate) == selected.end())
        {
            selected.push_back(candidate);
        }
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

/// Seeded RANSAC over corresponding points, then two least-squares refits on the inliers.
///
/// Attempt zero fits the first minimal set in input order; later attempts fit sampled ones. The
/// best attempt has the most inliers, then the smallest inlier residual sum, then the
/// lexicographically smallest sample, so a tie is decided by value rather than by which attempt
/// came first. The loop ends early once an attempt makes every pair an inlier. Serial, and checks
/// cancellation every sixty-four attempts.
[[nodiscard]] Result<FitSummary> robustFit(const ExecutionContext& execution,
                                           std::span<const StarAlignmentPoint> source,
                                           std::span<const StarAlignmentPoint> reference,
                                           const StarAlignmentRequest& request, std::pmr::memory_resource* memory)
{
    const std::uint32_t minimum = modelMinimum(request.model);
    DeterministicSampler sampler(request.seed);
    std::pmr::vector<bool> bestInliers(source.size(), false, memory);
    std::uint32_t bestCount = 0u;
    double bestSquared = std::numeric_limits<double>::infinity();
    std::pmr::vector<std::uint32_t> bestSample(memory);
    std::uint32_t degenerate = 0u;
    std::uint32_t attempts = 0u;
    for (std::uint32_t attempt = 0u; attempt < request.ransacIterations; ++attempt)
    {
        if ((attempt & 63u) == 0u)
        {
            const Status running = execution.checkCancellation();
            if (running.failed())
            {
                return running;
            }
        }
        std::pmr::vector<std::uint32_t> selected(memory);
        if (attempt == 0u)
        {
            selected.resize(minimum);
            std::iota(selected.begin(), selected.end(), 0u);
        }
        else
        {
            selected = sampleIndices(sampler, static_cast<std::uint32_t>(source.size()), minimum, memory);
        }
        ++attempts;
        StarTransform candidate;
        if (!fitModel(source, reference, selected, request.model, candidate))
        {
            ++degenerate;
            continue;
        }
        std::pmr::vector<bool> inliers(source.size(), false, memory);
        std::uint32_t count = 0u;
        double squared = 0.0;
        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            const double residual = residualOf(candidate, source[index], reference[index]);
            if (residual <= request.inlierThreshold)
            {
                inliers[index] = true;
                ++count;
                squared += residual * residual;
            }
        }
        if (count > bestCount ||
            (count == bestCount && (squared < bestSquared || (squared == bestSquared && selected < bestSample))))
        {
            bestCount = count;
            bestSquared = squared;
            bestSample = std::move(selected);
            bestInliers = std::move(inliers);
        }
        // Once every candidate is inside the residual threshold, no later minimal set can change
        // the inlier classification. The deterministic all-point refit below owns the coefficients,
        // so evaluating the remaining request-local samples would only repeat that answer.
        if (count == source.size())
        {
            break;
        }
    }
    if (bestCount < requiredMatches(request))
    {
        if (bestCount == 0u && degenerate == attempts)
        {
            return failure(kFitOperation, ErrorCode::InvalidData,
                           "every minimal set is degenerate for the requested transform model");
        }
        return failure(kFitOperation, ErrorCode::EmptySelection,
                       "the robust fit found fewer inliers than the request requires");
    }
    if (static_cast<double>(bestCount) / source.size() < request.minimumInlierFraction)
    {
        return failure(kFitOperation, ErrorCode::EmptySelection,
                       "the robust fit's inlier fraction is below the requested minimum");
    }
    StarTransform transform;
    for (std::uint32_t refinement = 0u; refinement < 2u; ++refinement)
    {
        std::pmr::vector<std::uint32_t> selected(memory);
        selected.reserve(bestCount);
        for (std::uint32_t index = 0u; index < bestInliers.size(); ++index)
        {
            if (bestInliers[index])
            {
                selected.push_back(index);
            }
        }
        if (!fitModel(source, reference, selected, request.model, transform))
        {
            return failure(kFitOperation, ErrorCode::InvalidData,
                           "the selected inliers are degenerate for the requested transform model");
        }
        bestCount = 0u;
        for (std::size_t index = 0u; index < source.size(); ++index)
        {
            bestInliers[index] = residualOf(transform, source[index], reference[index]) <= request.inlierThreshold;
            bestCount += bestInliers[index] ? 1u : 0u;
        }
    }
    if (bestCount < requiredMatches(request) ||
        static_cast<double>(bestCount) / source.size() < request.minimumInlierFraction)
    {
        return failure(kFitOperation, ErrorCode::EmptySelection,
                       "the final least-squares model does not retain the required inliers");
    }
    std::pmr::vector<double> residuals(source.size(), std::numeric_limits<double>::infinity(), memory);
    for (std::size_t index = 0u; index < source.size(); ++index)
    {
        residuals[index] = residualOf(transform, source[index], reference[index]);
    }
    FitSummary summary(memory);
    summary.transform = transform;
    summary.inliers = std::move(bestInliers);
    summary.residuals = std::move(residuals);
    summary.degenerateSamples = degenerate;
    summary.attempts = attempts;
    return summary;
}

/// Robustly fits the candidate pairs and assembles the public answer: the inlier matches sorted by
/// source then reference index, and every quality figure recomputed from the final model.
[[nodiscard]] Result<StarAlignmentResult>
fitCandidates(const ExecutionContext& execution, std::span<const StarAlignmentPoint> sourceField,
              std::span<const StarAlignmentPoint> referenceField, std::span<const CandidateMatch> candidates,
              const StarAlignmentRequest& request, std::uint32_t sourceTriangles, std::uint32_t referenceTriangles,
              ExecutionMemory& memory)
{
    if (candidates.size() < requiredMatches(request))
    {
        return failure(kFitOperation, ErrorCode::EmptySelection,
                       "fewer candidate correspondences than the request requires");
    }
    std::pmr::vector<StarAlignmentPoint> source(&memory);
    std::pmr::vector<StarAlignmentPoint> reference(&memory);
    source.reserve(candidates.size());
    reference.reserve(candidates.size());
    for (const CandidateMatch& candidate : candidates)
    {
        source.push_back(sourceField[candidate.source]);
        reference.push_back(referenceField[candidate.reference]);
    }
    Result<FitSummary> fitted = robustFit(execution, source, reference, request, &memory);
    if (!fitted.hasValue())
    {
        return fitted.status();
    }
    const std::uint32_t largestVote = std::max_element(candidates.begin(), candidates.end(),
                                                       [](const CandidateMatch& left, const CandidateMatch& right) {
                                                           return left.votes < right.votes;
                                                       })
                                          ->votes;
    StarAlignmentResult result;
    result.transform = fitted->transform;
    std::pmr::vector<double> inlierResiduals(&memory);
    inlierResiduals.reserve(candidates.size());
    double sumSquares = 0.0;
    double maximum = 0.0;
    for (std::size_t index = 0u; index < candidates.size(); ++index)
    {
        if (!fitted->inliers[index])
        {
            continue;
        }
        const CandidateMatch& candidate = candidates[index];
        const double residual = fitted->residuals[index];
        result.matches.push_back({.sourceIndex = candidate.source,
                                  .referenceIndex = candidate.reference,
                                  .votes = candidate.votes,
                                  .confidence = static_cast<double>(candidate.votes) / largestVote,
                                  .residual = residual});
        inlierResiduals.push_back(residual);
        sumSquares += residual * residual;
        maximum = (std::max)(maximum, residual);
    }
    std::sort(result.matches.begin(), result.matches.end(), [](const StarMatch& left, const StarMatch& right) {
        return std::tie(left.sourceIndex, left.referenceIndex) < std::tie(right.sourceIndex, right.referenceIndex);
    });
    std::sort(inlierResiduals.begin(), inlierResiduals.end());
    const std::size_t middle = inlierResiduals.size() / 2u;
    const double median = inlierResiduals.size() % 2u == 1u
                              ? inlierResiduals[middle]
                              : 0.5 * (inlierResiduals[middle - 1u] + inlierResiduals[middle]);
    result.quality =
        StarAlignmentQuality{.sourcePoints = static_cast<std::uint32_t>(sourceField.size()),
                             .referencePoints = static_cast<std::uint32_t>(referenceField.size()),
                             .sourceTriangles = sourceTriangles,
                             .referenceTriangles = referenceTriangles,
                             .candidateMatches = static_cast<std::uint32_t>(candidates.size()),
                             .inlierMatches = static_cast<std::uint32_t>(result.matches.size()),
                             .rejectedOutliers = static_cast<std::uint32_t>(candidates.size() - result.matches.size()),
                             .degenerateSamples = fitted->degenerateSamples,
                             .ransacAttempts = fitted->attempts,
                             .inlierFraction = static_cast<double>(result.matches.size()) / candidates.size(),
                             .rootMeanSquareResidual = std::sqrt(sumSquares / result.matches.size()),
                             .medianResidual = median,
                             .maximumResidual = maximum,
                             .workingBytes = memory.peakBytes(),
                             .admittedConcurrency = execution.admittedConcurrency()};
    return result;
}

/// A 64-bit FNV-1a hash over little-endian bytes, with zero reserved for an empty handle.
class FingerprintBuilder
{
public:
    /// Hashes the eight bytes of `value`, least significant first.
    void add(std::uint64_t value) noexcept
    {
        for (std::uint32_t byte = 0u; byte < 8u; ++byte)
        {
            m_value ^= (value >> (byte * 8u)) & 0xffu;
            m_value *= 1099511628211ull;
        }
    }

    void add(double value) noexcept
    {
        add(std::bit_cast<std::uint64_t>(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return m_value == 0u ? 1u : m_value;
    }

private:
    std::uint64_t m_value = 1469598103934665603ull;
};

/// A preparation's identity: the preparation schema, every request field and every coordinate in
/// order, so a changed policy, point or ordering is a different fingerprint.
[[nodiscard]] std::uint64_t fingerprintOf(std::span<const StarAlignmentPoint> points,
                                          const StarAlignmentRequest& request) noexcept
{
    FingerprintBuilder hash;
    hash.add(kPreparationSchema);
    hash.add(static_cast<std::uint64_t>(points.size()));
    hash.add(static_cast<std::uint64_t>(request.model));
    hash.add(static_cast<std::uint64_t>(request.trianglePolicy));
    hash.add(static_cast<std::uint64_t>(request.maximumTriangles));
    hash.add(request.minimumTriangleSide);
    hash.add(request.triangleRatioTolerance);
    hash.add(request.triangleToleranceMinimumScale);
    hash.add(request.triangleToleranceMaximumScale);
    hash.add(static_cast<std::uint64_t>(request.maximumTriangleNeighbours));
    hash.add(static_cast<std::uint64_t>(request.minimumVotes));
    hash.add(static_cast<std::uint64_t>(request.minimumMatches));
    hash.add(request.inlierThreshold);
    hash.add(request.minimumInlierFraction);
    hash.add(static_cast<std::uint64_t>(request.ransacIterations));
    hash.add(request.seed);
    for (const StarAlignmentPoint& point : points)
    {
        hash.add(point.x);
        hash.add(point.y);
    }
    return hash.value();
}

[[nodiscard]] Status allocationFailure(const char* operation)
{
    return failure(operation, ErrorCode::AllocationFailed,
                   "the platform could not hold the star-alignment value storage");
}

} // namespace

/// What a prepared reference shares between its copies, never modified after preparation.
///
/// Held in ordinary heap containers rather than the execution's memory, because it outlives the
/// execution that prepared it.
struct PreparedStarAlignmentReference::State
{
    std::vector<StarAlignmentPoint> points{};
    StarAlignmentRequest request{};
    std::vector<Triangle> triangles{};
    TriangleGrid grid{};
    std::uint64_t fingerprint = 0u;
};

const char* starTransformModelName(StarTransformModel model) noexcept
{
    switch (model)
    {
    case StarTransformModel::Similarity:
        return "Similarity";
    case StarTransformModel::Affine:
        return "Affine";
    case StarTransformModel::Projective:
        return "Projective";
    }
    return "UnknownStarTransformModel";
}

const char* starTrianglePolicyName(StarTrianglePolicy policy) noexcept
{
    switch (policy)
    {
    case StarTrianglePolicy::Automatic:
        return "Automatic";
    case StarTrianglePolicy::Exhaustive:
        return "Exhaustive";
    case StarTrianglePolicy::Bounded:
        return "Bounded";
    }
    return "UnknownStarTrianglePolicy";
}

Result<StarAlignmentPoint> transformStarPoint(const StarTransform& transform, const StarAlignmentPoint& source)
{
    if (!isStarTransformModel(transform.model))
    {
        return parameterFailure(kMapOperation, ErrorCode::InvalidArgument,
                                "the transform model is not one this build knows", "transform.model");
    }
    if (!std::isfinite(source.x) || !std::isfinite(source.y) ||
        !std::all_of(transform.sourceToReference.begin(), transform.sourceToReference.end(),
                     [](double value) { return std::isfinite(value); }))
    {
        return failure(kMapOperation, ErrorCode::InvalidArgument,
                       "the point and every transform coefficient must be finite");
    }
    StarAlignmentPoint destination{};
    if (!mappedPoint(transform, source, destination))
    {
        return failure(kMapOperation, ErrorCode::InvalidData,
                       "the projective denominator is zero or the mapped point is not finite");
    }
    return destination;
}

PreparedStarAlignmentReference::PreparedStarAlignmentReference() noexcept = default;
PreparedStarAlignmentReference::PreparedStarAlignmentReference(const PreparedStarAlignmentReference& other) noexcept =
    default;
PreparedStarAlignmentReference::PreparedStarAlignmentReference(PreparedStarAlignmentReference&& other) noexcept =
    default;
PreparedStarAlignmentReference&
PreparedStarAlignmentReference::operator=(const PreparedStarAlignmentReference& other) noexcept = default;
PreparedStarAlignmentReference&
PreparedStarAlignmentReference::operator=(PreparedStarAlignmentReference&& other) noexcept = default;
PreparedStarAlignmentReference::~PreparedStarAlignmentReference() = default;

PreparedStarAlignmentReference::PreparedStarAlignmentReference(std::shared_ptr<const State> state) noexcept
    : m_state(std::move(state))
{
}

bool PreparedStarAlignmentReference::valid() const noexcept
{
    return m_state != nullptr;
}

std::size_t PreparedStarAlignmentReference::pointCount() const noexcept
{
    return m_state != nullptr ? m_state->points.size() : 0u;
}

std::size_t PreparedStarAlignmentReference::triangleCount() const noexcept
{
    return m_state != nullptr ? m_state->triangles.size() : 0u;
}

std::uint64_t PreparedStarAlignmentReference::fingerprint() const noexcept
{
    return m_state != nullptr ? m_state->fingerprint : 0u;
}

StarAlignmentRequest PreparedStarAlignmentReference::request() const noexcept
{
    return m_state != nullptr ? m_state->request : StarAlignmentRequest{};
}

Result<PreparedStarAlignmentReference> prepareStarAlignmentReference(const ExecutionContext& execution,
                                                                     std::span<const StarAlignmentPoint> reference,
                                                                     const StarAlignmentRequest& request)
{
    if (!execution.valid())
    {
        return failure(kPrepareOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    const Status requestReady = checkRequest(request, kPrepareOperation);
    if (requestReady.failed())
    {
        return requestReady;
    }
    const Status pointsReady = checkPoints(reference, kPrepareOperation, "reference");
    if (pointsReady.failed())
    {
        return pointsReady;
    }
    if (reference.size() < modelMinimum(request.model))
    {
        return failure(kPrepareOperation, ErrorCode::EmptySelection,
                       "the reference has too few points for the requested transform model");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    ExecutionMemory memory(*execution.memoryResource());
    try
    {
        Result<TriangleBuild> built = buildTriangles(execution, reference, request, true, &memory);
        if (!built.hasValue())
        {
            return built.status();
        }
        if (built->triangles.empty())
        {
            return failure(kPrepareOperation, ErrorCode::EmptySelection,
                           "the reference contains no non-degenerate triangle of the requested size");
        }
        auto state = std::make_shared<PreparedStarAlignmentReference::State>();
        state->points.assign(reference.begin(), reference.end());
        state->request = request;
        state->triangles.assign(std::make_move_iterator(built->triangles.begin()),
                                std::make_move_iterator(built->triangles.end()));
        state->grid = buildTriangleGrid(state->triangles, request);
        state->fingerprint = fingerprintOf(state->points, request);
        return PreparedStarAlignmentReference{std::move(state)};
    }
    catch (const ContextAllocationFailure& refused)
    {
        return refused.status();
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure(kPrepareOperation);
    }
}

Result<StarAlignmentResult> fitStarTransform(const ExecutionContext& execution,
                                             std::span<const StarAlignmentPoint> source,
                                             std::span<const StarAlignmentPoint> reference,
                                             const StarAlignmentRequest& request)
{
    if (!execution.valid())
    {
        return failure(kFitOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    const Status requestReady = checkRequest(request, kFitOperation);
    if (requestReady.failed())
    {
        return requestReady;
    }
    if (source.size() != reference.size())
    {
        return failure(kFitOperation, ErrorCode::IncompatibleArguments,
                       "corresponding point spans must have the same length");
    }
    const Status sourceReady = checkPoints(source, kFitOperation, "source");
    if (sourceReady.failed())
    {
        return sourceReady;
    }
    const Status referenceReady = checkPoints(reference, kFitOperation, "reference");
    if (referenceReady.failed())
    {
        return referenceReady;
    }
    if (source.size() < requiredMatches(request))
    {
        return failure(kFitOperation, ErrorCode::EmptySelection,
                       "the correspondence set is smaller than the requested inlier count");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    ExecutionMemory memory(*execution.memoryResource());
    try
    {
        std::pmr::vector<CandidateMatch> candidates(&memory);
        candidates.reserve(source.size());
        for (std::uint32_t index = 0u; index < source.size(); ++index)
        {
            candidates.push_back({.source = index, .reference = index, .votes = 1u});
        }
        return fitCandidates(execution, source, reference, candidates, request, 0u, 0u, memory);
    }
    catch (const ContextAllocationFailure& refused)
    {
        return refused.status();
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure(kFitOperation);
    }
}

Result<StarAlignmentResult> alignStarFields(const ExecutionContext& execution,
                                            std::span<const StarAlignmentPoint> source,
                                            const PreparedStarAlignmentReference& reference)
{
    if (!execution.valid())
    {
        return failure(kAlignOperation, ErrorCode::NotInitialized, "the execution context holds nothing");
    }
    if (!reference.valid())
    {
        return failure(kAlignOperation, ErrorCode::InvalidArgument, "the prepared reference holds nothing");
    }
    const Status sourceReady = checkPoints(source, kAlignOperation, "source");
    if (sourceReady.failed())
    {
        return sourceReady;
    }
    const StarAlignmentRequest& request = reference.m_state->request;
    if (source.size() < modelMinimum(request.model))
    {
        return failure(kAlignOperation, ErrorCode::EmptySelection,
                       "the source has too few points for the requested transform model");
    }
    const Status running = execution.checkCancellation();
    if (running.failed())
    {
        return running;
    }
    ExecutionMemory memory(*execution.memoryResource());
    try
    {
        Result<TriangleBuild> sourceBuild = buildTriangles(execution, source, request, false, &memory);
        if (!sourceBuild.hasValue())
        {
            return sourceBuild.status();
        }
        if (sourceBuild->triangles.empty())
        {
            return failure(kAlignOperation, ErrorCode::EmptySelection,
                           "the source contains no non-degenerate triangle of the requested size");
        }
        Result<std::pmr::vector<TrianglePair>> triangleMatches = matchTriangles(
            execution, sourceBuild->triangles, reference.m_state->triangles, reference.m_state->grid, request, &memory);
        if (!triangleMatches.hasValue())
        {
            return triangleMatches.status();
        }
        if (triangleMatches->empty())
        {
            return failure(kAlignOperation, ErrorCode::EmptySelection,
                           "the source and reference share no triangle invariant");
        }
        Result<std::pmr::vector<CandidateMatch>> candidates =
            buildConsensus(execution, *triangleMatches, sourceBuild->triangles, reference.m_state->triangles,
                           static_cast<std::uint32_t>(source.size()),
                           static_cast<std::uint32_t>(reference.m_state->points.size()), request, &memory);
        if (!candidates.hasValue())
        {
            return candidates.status();
        }
        if (candidates->size() < requiredMatches(request))
        {
            return failure(kAlignOperation, ErrorCode::EmptySelection,
                           "triangle consensus produced fewer point pairs than the request requires");
        }
        return fitCandidates(execution, source, reference.m_state->points, *candidates, request,
                             static_cast<std::uint32_t>(sourceBuild->triangles.size()),
                             static_cast<std::uint32_t>(reference.m_state->triangles.size()), memory);
    }
    catch (const ContextAllocationFailure& refused)
    {
        return refused.status();
    }
    catch (const std::bad_alloc&)
    {
        return allocationFailure(kAlignOperation);
    }
}

Result<StarAlignmentResult> alignStarFields(const ExecutionContext& execution,
                                            std::span<const StarAlignmentPoint> source,
                                            std::span<const StarAlignmentPoint> reference,
                                            const StarAlignmentRequest& request)
{
    Result<PreparedStarAlignmentReference> prepared = prepareStarAlignmentReference(execution, reference, request);
    if (!prepared.hasValue())
    {
        return prepared.status();
    }
    return alignStarFields(execution, source, *prepared);
}

} // namespace an
