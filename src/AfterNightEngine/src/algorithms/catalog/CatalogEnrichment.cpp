// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the one-to-one assignment of measured sources to catalog entries.

#include <AfterNightEngine/Algorithms/Catalog/CatalogEnrichment.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"
#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;

constexpr const char* kMatchOperation = "an::matchCatalogSources";
constexpr const char* kSortOperation = "an::sortCatalogMatches";
constexpr const char* kProgressStage = "matching sources to a catalog";
constexpr std::uint32_t kCancellationStride = 256u;
constexpr double kArcsecondsPerDegree = 3600.0;

/// One source-to-entry pair inside the match radius, before selection decides which pairs win.
struct Candidate
{
    /// How far apart the two are, in arcseconds. The first key of the selection order.
    double separationArcseconds = 0.0;

    /// The source's position in the measurement list.
    std::uint32_t sourceIndex = 0u;

    /// The entry's position in the catalog.
    std::uint32_t entryIndex = 0u;
};

/// Whether one candidate is taken before another.
///
/// A total order: separation, then source position, then entry position. Separations are compared
/// exactly, because a comparison through a tolerance is not transitive and therefore not a valid
/// strict weak ordering for a standard sort.
///
/// @param left one candidate.
/// @param right the other.
/// @return true when `left` is taken first.
[[nodiscard]] bool candidateLess(const Candidate& left, const Candidate& right) noexcept
{
    if (left.separationArcseconds != right.separationArcseconds)
    {
        return left.separationArcseconds < right.separationArcseconds;
    }
    if (left.sourceIndex != right.sourceIndex)
    {
        return left.sourceIndex < right.sourceIndex;
    }
    return left.entryIndex < right.entryIndex;
}

/// The catalog positions inside one source's match radius, ordered by position.
///
/// The catalog's region query is what keeps this proportional to the field's density: it reaches its
/// candidates through the declination index rather than by scanning the whole table once per source.
///
/// @param execution the running operation.
/// @param catalog the catalog to search.
/// @param source the measured source.
/// @param radiusDegrees the match radius in degrees.
/// @param request the match policy, for the magnitude rule.
/// @param found receives the positions.
/// @return success, or the query's own failure.
[[nodiscard]] Status neighboursOf(const ExecutionContext& execution, const SkyCatalog& catalog,
                                  const CatalogSourceMeasurement& source, double radiusDegrees,
                                  const CatalogMatchRequest& request, std::vector<std::uint32_t>& found)
{
    CatalogRegionQuery query;
    query.center = source.coordinate;
    query.radiusDegrees = radiusDegrees;
    query.magnitudeLimit = request.magnitudeLimit;
    query.admitNamedEntriesWithoutMagnitude = request.admitNamedEntriesWithoutMagnitude;
    Result<CatalogQueryResult> answered = queryCatalogRegion(execution, catalog, query);
    if (!answered.hasValue())
    {
        return answered.status();
    }
    found = std::move(answered->entryIndices);
    return Status::success();
}

/// Whether the request's own terms are usable.
///
/// @param request the policy to check.
/// @return success, or the first term that is not.
[[nodiscard]] Status checkRequest(const CatalogMatchRequest& request)
{
    if (!std::isfinite(request.matchRadiusArcseconds) || request.matchRadiusArcseconds <= 0.0)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the match radius must be finite and greater than zero",
                              kMatchOperation);
    }
    if (request.magnitudeLimit.has_value() && !std::isfinite(*request.magnitudeLimit))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a stated magnitude limit must be finite", kMatchOperation);
    }
    if (request.maximumCandidates == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a match considers at least one candidate pair",
                              kMatchOperation);
    }
    return Status::success();
}

/// The entry one correspondence names, or null when the catalog does not hold it.
///
/// @param catalog the catalog the positions refer to.
/// @param match the correspondence.
/// @return the entry, or null.
[[nodiscard]] const CatalogEntry* entryOf(const SkyCatalog& catalog, const CatalogMatch& match) noexcept
{
    return catalog.entry(match.entryIndex);
}

} // namespace

const char* catalogMatchSortKeyName(const CatalogMatchSortKey key) noexcept
{
    switch (key)
    {
    case CatalogMatchSortKey::SourceIndex:
        return "SourceIndex";
    case CatalogMatchSortKey::EntryIndex:
        return "EntryIndex";
    case CatalogMatchSortKey::SeparationArcseconds:
        return "SeparationArcseconds";
    case CatalogMatchSortKey::CatalogIdentifier:
        return "CatalogIdentifier";
    case CatalogMatchSortKey::CatalogCommonName:
        return "CatalogCommonName";
    case CatalogMatchSortKey::CatalogMagnitude:
        return "CatalogMagnitude";
    }
    return "UnknownCatalogMatchSortKey";
}

Result<CatalogMatchResult> matchCatalogSources(const ExecutionContext& execution,
                                               const std::span<const CatalogSourceMeasurement> sources,
                                               const SkyCatalog& catalog, const CatalogMatchRequest& request)
{
    if (!execution.valid() || !catalog.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context or no catalog",
                              kMatchOperation);
    }
    if (const Status checked = checkRequest(request); checked.failed())
    {
        return checked;
    }
    if (sources.empty())
    {
        return catalogFailure(ErrorCode::EmptySelection, "a match needs at least one measured source", kMatchOperation);
    }
    if (sources.size() > kMaxCatalogMatchSources)
    {
        return catalogLimitFailure("more sources were supplied than one match accepts", kMatchOperation, sources.size(),
                                   kMaxCatalogMatchSources);
    }

    CatalogMatchResult result;
    result.sourceCount = static_cast<std::uint32_t>(sources.size());
    const double radiusDegrees = request.matchRadiusArcseconds / kArcsecondsPerDegree;

    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        const CatalogSourceMeasurement& source = sources[index];
        if (!source.hasCoordinate)
        {
            continue;
        }
        if (!isSkyCoordinate(source.coordinate))
        {
            return Status::failure(
                ErrorCode::InvalidData,
                StatusDetail{.message = "a positioned source's coordinate is not a direction on the sky",
                             .operation = kMatchOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Completed, index)}});
        }
        ++result.positionedSourceCount;
    }

    // Counting the candidate pairs before holding any of them is what makes the memory this
    // operation takes exactly what its answer needs, and what lets a field too dense to match be
    // refused before a byte of it is allocated.
    std::vector<std::uint32_t> neighbours;
    std::uint64_t candidateCount = 0u;
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        if (!sources[index].hasCoordinate)
        {
            continue;
        }
        if (const Status found = neighboursOf(execution, catalog, sources[index], radiusDegrees, request, neighbours);
            found.failed())
        {
            return found;
        }
        candidateCount += neighbours.size();
        if (candidateCount > request.maximumCandidates)
        {
            return catalogLimitFailure("the field produces more candidate pairs than the request allows",
                                       kMatchOperation, candidateCount, request.maximumCandidates);
        }
    }
    result.candidateCount = static_cast<std::uint32_t>(candidateCount);

    const std::uint64_t candidateBytes = candidateCount * sizeof(Candidate);
    const std::uint64_t sourceFlagBytes = sources.size();
    const std::uint64_t entryFlagBytes = catalog.entryCount();
    Result<plane_detail::ScopedContextMemory> candidateMemory =
        plane_detail::ScopedContextMemory::take(execution, candidateBytes, alignof(Candidate), kMatchOperation);
    if (!candidateMemory.hasValue())
    {
        return candidateMemory.status();
    }
    Result<plane_detail::ScopedContextMemory> flagMemory = plane_detail::ScopedContextMemory::take(
        execution, sourceFlagBytes + entryFlagBytes, alignof(std::uint8_t), kMatchOperation);
    if (!flagMemory.hasValue())
    {
        return flagMemory.status();
    }
    result.workingBytes = candidateMemory->bytes() + flagMemory->bytes();

    Candidate* const candidates = static_cast<Candidate*>(candidateMemory->data());
    std::uint8_t* const takenSources = static_cast<std::uint8_t*>(flagMemory->data());
    std::uint8_t* const takenEntries = takenSources + sourceFlagBytes;
    std::fill(takenSources, takenSources + sourceFlagBytes + entryFlagBytes, static_cast<std::uint8_t>(0));

    std::size_t written = 0u;
    for (std::size_t index = 0u; index < sources.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        const CatalogSourceMeasurement& source = sources[index];
        if (!source.hasCoordinate)
        {
            continue;
        }
        if (const Status found = neighboursOf(execution, catalog, source, radiusDegrees, request, neighbours);
            found.failed())
        {
            return found;
        }
        for (const std::uint32_t entryIndex : neighbours)
        {
            if (written >= candidateCount)
            {
                return catalogFailure(ErrorCode::Internal, "the counted candidate pairs and the collected ones differ",
                                      kMatchOperation);
            }
            const CatalogEntry* const entry = catalog.entry(entryIndex);
            if (entry == nullptr)
            {
                return catalogFailure(ErrorCode::Internal, "a region answer named an entry the catalog does not hold",
                                      kMatchOperation);
            }
            candidates[written] =
                Candidate{.separationArcseconds =
                              angularSeparationDegrees(source.coordinate, entry->coordinate) * kArcsecondsPerDegree,
                          .sourceIndex = static_cast<std::uint32_t>(index),
                          .entryIndex = entryIndex};
            ++written;
        }
    }

    std::sort(candidates, candidates + written, candidateLess);

    result.matches.reserve((std::min)(static_cast<std::size_t>(result.positionedSourceCount), written));
    double faintest = 0.0;
    double brightest = 0.0;
    bool sawMagnitude = false;
    for (std::size_t position = 0u; position < written; ++position)
    {
        if ((position % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        const Candidate& candidate = candidates[position];
        if (takenSources[candidate.sourceIndex] != 0u || takenEntries[candidate.entryIndex] != 0u)
        {
            continue;
        }
        takenSources[candidate.sourceIndex] = 1u;
        takenEntries[candidate.entryIndex] = 1u;
        result.matches.push_back(CatalogMatch{.sourceIndex = candidate.sourceIndex,
                                              .entryIndex = candidate.entryIndex,
                                              .separationArcseconds = candidate.separationArcseconds});
        const CatalogEntry* const entry = catalog.entry(candidate.entryIndex);
        if (entry != nullptr && entry->magnitude.has_value())
        {
            faintest = sawMagnitude ? (std::max)(faintest, *entry->magnitude) : *entry->magnitude;
            brightest = sawMagnitude ? (std::min)(brightest, *entry->magnitude) : *entry->magnitude;
            sawMagnitude = true;
        }
    }

    // Selection walks the candidates in separation order, so the accepted correspondences come out
    // in that order. The answer is ordered by source instead, which is the order a caller reads its
    // own list in and is what makes two runs comparable element by element.
    std::sort(result.matches.begin(), result.matches.end(), [](const CatalogMatch& left, const CatalogMatch& right) {
        return left.sourceIndex < right.sourceIndex ||
               (left.sourceIndex == right.sourceIndex && left.entryIndex < right.entryIndex);
    });

    if (sawMagnitude)
    {
        result.faintestMatchedMagnitude = faintest;
        result.brightestMatchedMagnitude = brightest;
    }
    execution.reportProgress(result.sourceCount, result.sourceCount, kProgressStage);
    return result;
}

Status sortCatalogMatches(const std::span<CatalogMatch> matches, const SkyCatalog& catalog,
                          const CatalogMatchSortKey key, const CatalogSortDirection direction)
{
    if (!isCatalogMatchSortKey(key))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the catalog match sort key is not one this build orders by",
                              kSortOperation);
    }
    if (!isCatalogSortDirection(direction))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the sort direction is not one this build orders by",
                              kSortOperation);
    }

    const bool needsCatalog = key == CatalogMatchSortKey::CatalogIdentifier ||
                              key == CatalogMatchSortKey::CatalogCommonName ||
                              key == CatalogMatchSortKey::CatalogMagnitude;
    if (needsCatalog)
    {
        for (const CatalogMatch& match : matches)
        {
            if (entryOf(catalog, match) == nullptr)
            {
                return catalogFailure(ErrorCode::OutOfRange,
                                      "a correspondence names an entry this catalog does not hold", kSortOperation);
            }
        }
    }

    const auto ascendingLess = [&catalog, key](const CatalogMatch& left, const CatalogMatch& right) {
        switch (key)
        {
        case CatalogMatchSortKey::SourceIndex:
            return left.sourceIndex < right.sourceIndex;
        case CatalogMatchSortKey::EntryIndex:
            return left.entryIndex < right.entryIndex;
        case CatalogMatchSortKey::SeparationArcseconds:
            return left.separationArcseconds < right.separationArcseconds;
        case CatalogMatchSortKey::CatalogIdentifier:
            return entryOf(catalog, left)->identifier < entryOf(catalog, right)->identifier;
        case CatalogMatchSortKey::CatalogCommonName:
            return entryOf(catalog, left)->commonName < entryOf(catalog, right)->commonName;
        case CatalogMatchSortKey::CatalogMagnitude: {
            const std::optional<double>& leftMagnitude = entryOf(catalog, left)->magnitude;
            const std::optional<double>& rightMagnitude = entryOf(catalog, right)->magnitude;
            if (leftMagnitude.has_value() != rightMagnitude.has_value())
            {
                return leftMagnitude.has_value();
            }
            return leftMagnitude.has_value() && *leftMagnitude < *rightMagnitude;
        }
        }
        return false;
    };

    // One comparison, read in whichever direction was asked for. An absent value is ordered after
    // every present one ascending, so reversing the comparison puts it before every present one
    // descending - the same rule read backwards rather than a second rule.
    std::stable_sort(matches.begin(), matches.end(),
                     [&ascendingLess, direction](const CatalogMatch& left, const CatalogMatch& right) {
                         return direction == CatalogSortDirection::Ascending ? ascendingLess(left, right)
                                                                             : ascendingLess(right, left);
                     });
    return Status::success();
}

} // namespace an
