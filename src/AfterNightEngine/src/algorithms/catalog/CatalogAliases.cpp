// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deciding which of two rows naming one object a query answers with.

#include <AfterNightEngine/Algorithms/Catalog/CatalogAliases.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::catalogFailure;
using catalog_detail::foldedCopy;
using catalog_detail::startsWithFolded;
using catalog_detail::trimAscii;

constexpr const char* kBuildOperation = "an::buildCatalogAliasIndex";
constexpr const char* kCollapseOperation = "an::collapseCatalogAliases";
constexpr std::uint32_t kCancellationStride = 1024u;

/// The canonical prefix of the designation whose rows are the aliases.
constexpr std::string_view kAliasPrefix = "C ";

/// A rank no candidate reaches, so the first candidate examined always replaces it.
constexpr int kNoRank = std::numeric_limits<int>::max();

/// Which entry answers for an entry that has no alias.
constexpr std::uint32_t kNoCanonicalEntry = std::numeric_limits<std::uint32_t>::max();

/// Whether an entry is one of the rows that another row answers for.
///
/// @param entry the entry to inspect.
/// @return true when its identifier canonicalizes to the alias catalog's prefix.
[[nodiscard]] bool isAliasEntry(const CatalogEntry& entry)
{
    return startsWithFolded(normalizeCatalogDesignation(entry.identifier), kAliasPrefix);
}

/// How strongly this provider's table prefers a row as the answer for an alias.
///
/// The two survey catalogues name the object as an object, a Messier number names it as something to
/// look at, and anything else is whatever the table happened to carry. A row carrying an alias
/// designation is excluded before it is ranked.
///
/// @param entry the candidate to rank.
/// @return zero for the first survey, one for the second, two for a Messier number, three
///         otherwise. Smaller is preferred.
[[nodiscard]] int candidateRank(const CatalogEntry& entry)
{
    const std::string canonical = normalizeCatalogDesignation(entry.identifier);
    if (startsWithFolded(canonical, "NGC "))
    {
        return 0;
    }
    if (startsWithFolded(canonical, "IC "))
    {
        return 1;
    }
    if (startsWithFolded(canonical, "M "))
    {
        return 2;
    }
    return 3;
}

/// Whether one candidate is a better answer for an alias than another.
///
/// A total order over the rank, the separation and the identifier, so the answer does not depend on
/// the order the candidates were examined in. Separations are compared exactly in every clause, because
/// a tolerance in one of them would not be a strict weak ordering.
///
/// @param rank the candidate's rank.
/// @param separationDegrees how far it is from the alias.
/// @param identifier its designation.
/// @param bestRank the incumbent's rank.
/// @param bestSeparationDegrees the incumbent's separation.
/// @param bestIdentifier the incumbent's designation.
/// @return true when the candidate should replace the incumbent.
[[nodiscard]] bool isBetterCandidate(const int rank, const double separationDegrees, const std::string& identifier,
                                     const int bestRank, const double bestSeparationDegrees,
                                     const std::string& bestIdentifier) noexcept
{
    if (rank != bestRank)
    {
        return rank < bestRank;
    }
    if (separationDegrees != bestSeparationDegrees)
    {
        return separationDegrees < bestSeparationDegrees;
    }
    return identifier < bestIdentifier;
}

} // namespace

Result<CatalogAliasIndex> buildCatalogAliasIndex(const ExecutionContext& execution, const SkyCatalog& catalog,
                                                 const CatalogAliasRequest& request)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kBuildOperation);
    }
    if (!catalog.valid() || catalog.entryCount() == 0u)
    {
        return catalogFailure(ErrorCode::NotInitialized, "the catalog holds no entries", kBuildOperation);
    }
    if (!std::isfinite(request.maximumSeparationDegrees) || request.maximumSeparationDegrees < 0.0)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the separation is not a finite angle of at least zero",
                              kBuildOperation);
    }

    const std::span<const CatalogEntry> entries = catalog.entries();
    CatalogAliasIndex index;
    index.canonicalEntryIndices.resize(entries.size());
    for (std::size_t position = 0u; position < entries.size(); ++position)
    {
        index.canonicalEntryIndices[position] = static_cast<std::uint32_t>(position);
    }

    // The name each row is known by is what makes two rows candidates for each other, so grouping by
    // it once replaces a pass over every pair of rows with a pass over the rows that share a name; a
    // pair whose names differ is never a candidate.
    std::unordered_map<std::string, std::vector<std::uint32_t>> byCommonName;
    std::vector<std::uint32_t> aliasPositions;
    for (std::size_t position = 0u; position < entries.size(); ++position)
    {
        if ((position % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        const CatalogEntry& entry = entries[position];
        const std::string_view name = trimAscii(entry.commonName);
        if (name.empty())
        {
            continue;
        }
        if (isAliasEntry(entry))
        {
            aliasPositions.push_back(static_cast<std::uint32_t>(position));
        }
        else
        {
            byCommonName[foldedCopy(name)].push_back(static_cast<std::uint32_t>(position));
        }
    }

    for (std::size_t alias = 0u; alias < aliasPositions.size(); ++alias)
    {
        if ((alias % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }

        const std::uint32_t aliasPosition = aliasPositions[alias];
        const CatalogEntry& aliasEntry = entries[aliasPosition];
        const auto group = byCommonName.find(foldedCopy(trimAscii(aliasEntry.commonName)));
        if (group == byCommonName.end())
        {
            continue;
        }

        std::uint32_t bestPosition = kNoCanonicalEntry;
        int bestRank = kNoRank;
        double bestSeparation = std::numeric_limits<double>::infinity();
        for (const std::uint32_t candidatePosition : group->second)
        {
            const CatalogEntry& candidate = entries[candidatePosition];
            ++index.examinedPairCount;
            const double separation = angularSeparationDegrees(aliasEntry.coordinate, candidate.coordinate);
            if (!(separation <= request.maximumSeparationDegrees))
            {
                continue;
            }
            const int rank = candidateRank(candidate);
            if (bestPosition == kNoCanonicalEntry ||
                isBetterCandidate(rank, separation, candidate.identifier, bestRank, bestSeparation,
                                  entries[bestPosition].identifier))
            {
                bestPosition = candidatePosition;
                bestRank = rank;
                bestSeparation = separation;
            }
        }

        if (bestPosition != kNoCanonicalEntry)
        {
            index.canonicalEntryIndices[aliasPosition] = bestPosition;
            ++index.aliasCount;
        }
    }
    return index;
}

Result<std::vector<std::uint32_t>> collapseCatalogAliases(const ExecutionContext& execution,
                                                          const CatalogAliasIndex& index,
                                                          const std::span<const std::uint32_t> entryIndices)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kCollapseOperation);
    }
    if (index.canonicalEntryIndices.empty())
    {
        return catalogFailure(ErrorCode::InvalidData, "the relation covers no catalog", kCollapseOperation);
    }

    const std::size_t entryCount = index.canonicalEntryIndices.size();
    std::vector<std::uint32_t> collapsed;
    if (entryIndices.empty())
    {
        return collapsed;
    }
    collapsed.reserve(entryIndices.size());

    std::vector<std::uint32_t> takenPosition(entryCount, kNoCanonicalEntry);
    for (std::size_t position = 0u; position < entryIndices.size(); ++position)
    {
        if ((position % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }

        const std::uint32_t entryIndex = entryIndices[position];
        if (entryIndex >= entryCount)
        {
            return Status::failure(
                ErrorCode::OutOfRange,
                StatusDetail{.message = "the answer names an entry the relation does not cover",
                             .operation = kCollapseOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofCount(ContextKey::Requested, entryIndex),
                                         StatusContextField::ofCount(ContextKey::Total, entryCount)}});
        }
        const std::uint32_t canonical = index.canonicalEntryIndices[entryIndex];
        if (canonical >= entryCount)
        {
            return catalogFailure(ErrorCode::InvalidData, "the relation names an entry it does not cover",
                                  kCollapseOperation);
        }

        std::uint32_t& taken = takenPosition[canonical];
        if (taken == kNoCanonicalEntry)
        {
            taken = static_cast<std::uint32_t>(collapsed.size());
            collapsed.push_back(entryIndex);
            continue;
        }
        // The row that answers for the others takes the place the first of them claimed, so the
        // answer holds one entry per object at the position the ordering put that object.
        if (collapsed[taken] != canonical && entryIndex == canonical)
        {
            collapsed[taken] = entryIndex;
        }
    }
    return collapsed;
}

} // namespace an
