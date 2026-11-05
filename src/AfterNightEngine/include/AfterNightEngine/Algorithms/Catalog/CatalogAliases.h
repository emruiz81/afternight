// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Collapsing the entries one provider's table lists twice under two names.
///
/// One deep-sky table carries some objects twice - a Caldwell number and the survey number the
/// object is normally called by - as two rows with the same common name and nearly the same
/// position. The relation between them is a property of that table, so it is built beside a catalog
/// rather than inside the queries every catalog shares: `buildCatalogAliasIndex` reads a built
/// `SkyCatalog` through its public surface and returns a `CatalogAliasIndex`, and
/// `collapseCatalogAliases` applies that relation to any answer a query produced. A catalog whose
/// rows are distinct objects never builds one.
///
/// The index is positions into one catalog's `SkyCatalog::entries()`; applied to another catalog's
/// answer it is refused rather than misread.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// How far apart two rows may be and still be the same object, in degrees.
///
/// Generous, because the two rows come from two surveys that measured different things - a nebula's
/// catalogued position is not a measurement of a point - and because the common name has already had
/// to match exactly.
inline constexpr double kDefaultCatalogAliasSeparationDegrees = 3.0;

/// Which of two rows naming the same object a query should answer with.
struct CatalogAliasRequest
{
    /// How far apart two rows may be and still be the same object, in degrees. Finite and at least
    /// zero.
    double maximumSeparationDegrees = kDefaultCatalogAliasSeparationDegrees;
};

/// Which entry each entry of a catalog is answered by.
///
/// Built once for a catalog and then applied to any number of query answers, because the relation
/// is a property of the table rather than of a question asked about it.
struct CatalogAliasIndex
{
    /// One position per catalog entry, holding the entry that answers for it.
    ///
    /// An entry with no alias holds its own position, so applying this to an answer is a lookup
    /// rather than a branch.
    std::vector<std::uint32_t> canonicalEntryIndices{};

    /// How many entries are answered for by a different entry.
    std::uint32_t aliasCount = 0u;

    /// How many pairs of entries the build compared.
    ///
    /// Smaller than the square of the table, because candidates are reached through the shared
    /// common name rather than by comparing every alias against every entry.
    std::uint64_t examinedPairCount = 0u;
};

/// Builds the alias relation of a catalog whose table lists some objects twice.
///
/// One provider's deep-sky table carries the same object under two designations - a Caldwell number
/// and the survey number the object is normally called by - as two rows with the same common name
/// and nearly the same position. A region query then answers with both, and a person reading the
/// answer sees one nebula annotated twice. This is where that is decided, once, for a table that
/// has it, rather than inside a query every provider shares: a catalog whose rows are distinct
/// objects never builds one.
///
/// A row whose identifier canonicalizes to the Caldwell prefix is an alias. It is answered for by a
/// row within the stated separation that shares its common name - compared after trimming and
/// ignoring ASCII case - and is not itself a Caldwell row: the one whose designation ranks highest,
/// the survey designations first in the order this provider's table prefers them, then a Messier
/// number, then anything else; among equal ranks the nearest. A row with no common name is never
/// an alias and never answers for one.
///
/// **The choice is a total order** over rank, separation and identifier, so which of two equally
/// distant rows answers never depends on the order they were examined in.
///
/// Runs on the calling thread, checks cancellation at bounded entry boundaries, and allocates the
/// index it returns plus a transient grouping by folded common name and a list of the alias rows.
///
/// @param execution the operation policy and engine-context lease.
/// @param catalog the catalog whose table may list objects twice.
/// @param request how far apart two rows may be and still be one object.
/// @return the relation; `ErrorCode::NotInitialized` when `execution` holds nothing or the catalog
///         is empty; `ErrorCode::InvalidArgument` for a separation that is not finite and at least
///         zero; or cancellation. A table with no aliases in it is a success in which every entry
///         answers for itself.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CatalogAliasIndex>
buildCatalogAliasIndex(const ExecutionContext& execution, const SkyCatalog& catalog,
                       const CatalogAliasRequest& request = {});

/// Removes from a query answer the entries another entry already answers for.
///
/// The order is the answer's own: each entry keeps the position its first appearance took. Where an
/// alias and the entry that answers for it are both in the answer, the position is the earlier of
/// the two and the entry at it is the one that answers - so a caller reading the answer in order
/// sees each object once, at the place the ordering put it. An alias whose answering entry is not
/// in the answer stays, and a position repeated in the answer is kept once.
///
/// Runs on the calling thread, checks cancellation at bounded boundaries, and allocates the answer
/// and one position per catalog entry.
///
/// @param execution the operation policy and engine-context lease.
/// @param index the relation built for the catalog the answer came from.
/// @param entryIndices the answer to collapse, which is only read.
/// @return the collapsed answer; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidData` for an index that does not describe one catalog;
///         `ErrorCode::OutOfRange` for an answer holding a position the relation does not cover,
///         which is what applying one catalog's relation to another catalog's answer looks like; or
///         cancellation. An empty answer is a success.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<std::uint32_t>>
collapseCatalogAliases(const ExecutionContext& execution, const CatalogAliasIndex& index,
                       std::span<const std::uint32_t> entryIndices);

} // namespace an
