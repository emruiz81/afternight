// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Matching measured sources to catalog entries, one to one.
///
/// `matchCatalogSources` takes sources whose world positions the caller already has - from a plate
/// solution, a header or an earlier alignment - and pairs each with at most one `SkyCatalog` entry
/// inside a match radius, closest pairs first. It needs no astrometric solver and no frame, which is
/// what lets one implementation serve every source of world coordinates.
///
/// A correspondence is two positions and a separation, never a copy of the entry: identity,
/// magnitude and colour stay in the catalog, and `sortCatalogMatches` orders a list of
/// correspondences by one of their fields or by a field of the entry they name. The candidate
/// selection order is total, so the answer does not depend on the order anything was examined in.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most measured sources one match accepts.
inline constexpr std::uint32_t kMaxCatalogMatchSources = 1000000u;

/// The most source-to-entry pairs one match holds before it decides which of them win.
inline constexpr std::uint32_t kDefaultMaxCatalogMatchCandidates = 4000000u;

/// One thing this engine measured in a frame, placed on the sky.
///
/// Where the world position came from is the caller's business: a plate solution, a header, a
/// previous alignment. This surface takes the position rather than a frame and a coordinate model,
/// so attaching catalog identity does not require the astrometric solver to exist first, and one
/// implementation serves every source of world coordinates.
struct CatalogSourceMeasurement
{
    /// Where the source is on the sky.
    SkyCoordinate coordinate{};

    /// Instrumental magnitude, or absent when the source has no usable flux.
    ///
    /// Carried through untouched. It is what a later photometric fit regresses against the matched
    /// entry's catalog magnitude; nothing here uses it to decide a match, because a match is a
    /// question about positions.
    std::optional<double> instrumentalMagnitude{};

    /// Whether the source has a usable world position at all.
    ///
    /// A frame's star list normally holds stars the coordinate model could not place. Saying so
    /// explicitly keeps the two lists the same length and index-aligned, so a match's source
    /// position is a position in the caller's own list.
    bool hasCoordinate = true;
};

/// What one match is asked to do.
struct CatalogMatchRequest
{
    /// The largest separation at which a source and an entry may be the same object, in arcseconds.
    ///
    /// Finite and greater than zero.
    double matchRadiusArcseconds = 5.0;

    /// The faintest catalog magnitude admitted, or absent to admit every entry.
    std::optional<double> magnitudeLimit{};

    /// Whether a catalog entry with a common name but no measured magnitude is admitted anyway.
    ///
    /// The same stated choice `CatalogRegionQuery` carries, and false for the same reason: matching
    /// against an entry nobody has photometry for produces an identity but no measurement.
    bool admitNamedEntriesWithoutMagnitude = false;

    /// The most source-to-entry pairs the operation may consider.
    ///
    /// Pairs are what a dense field costs: doubling the match radius roughly quadruples them. A
    /// request that would exceed this is refused with the count rather than allowed to consume the
    /// engine's whole memory budget.
    std::uint32_t maximumCandidates = kDefaultMaxCatalogMatchCandidates;
};

/// One accepted source-to-entry correspondence.
struct CatalogMatch
{
    /// The source's position in the measurement list.
    std::uint32_t sourceIndex = 0u;

    /// The entry's position in the catalog.
    std::uint32_t entryIndex = 0u;

    /// How far apart they are, in arcseconds.
    double separationArcseconds = 0.0;

    /// Whether two correspondences are the same three values.
    ///
    /// @param left one correspondence.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const CatalogMatch& left,
                                                   const CatalogMatch& right) noexcept = default;
};

/// Everything one completed match produced.
///
/// The matches carry positions rather than copies of the catalog's own fields. An entry's
/// identifier, magnitude and colour live in the catalog, and a record that repeated them here would
/// be a second copy of a fact with nothing keeping the two in step.
struct CatalogMatchResult
{
    /// The accepted correspondences, ordered by source position and then entry position, whatever
    /// the admitted thread count.
    std::vector<CatalogMatch> matches{};

    /// How many measurements were supplied.
    std::uint32_t sourceCount = 0u;

    /// How many of them carried a usable world position.
    std::uint32_t positionedSourceCount = 0u;

    /// How many source-to-entry pairs were inside the match radius and passed the magnitude rule,
    /// before one-to-one selection.
    std::uint32_t candidateCount = 0u;

    /// The faintest catalog magnitude among the matched entries, or absent when none carried one.
    ///
    /// Not the frame's limiting magnitude: it is a property of what was matched, not of the
    /// instrument, and it is absent rather than zero when nothing was matched.
    std::optional<double> faintestMatchedMagnitude{};

    /// The brightest catalog magnitude among the matched entries, or absent under the same rule.
    std::optional<double> brightestMatchedMagnitude{};

    /// Peak engine-context bytes held for candidate pairs and selection state.
    std::uint64_t workingBytes = 0u;
};

/// Matches measured sources to catalog entries, one source to at most one entry.
///
/// Every source-to-entry pair inside the match radius is a candidate. Candidates are then taken in
/// order of increasing separation, and a pair is accepted when neither its source nor its entry has
/// already been taken. That rule is what makes a crowded field resolve to the closest consistent
/// assignment rather than to whichever source was looked at first.
///
/// The order candidates are taken in is total: separation, then source position, then entry
/// position. Separations are compared exactly - a comparison through a tolerance is not a strict
/// weak ordering and therefore not a valid comparator for a standard sort - and only two equal
/// separations fall through to the positions.
///
/// Candidates are found through `queryCatalogRegion` once to count them and once to collect them,
/// so a field above the candidate ceiling is refused before any pair is held. Candidate pairs and
/// selection state come from the engine context's memory resource and are released before this
/// returns. The catalog's declination index is what keeps candidate generation proportional to the
/// field's density rather than to the size of the catalog.
///
/// Thread-safe with respect to `execution`, `sources` and `catalog`, all of which are only read.
/// Runs on the calling thread, checks cancellation at bounded source and candidate boundaries, and
/// reports one progress step, on completion.
///
/// @param execution the operation policy and engine-context lease.
/// @param sources the measured sources, at most `kMaxCatalogMatchSources` of them.
/// @param catalog the catalog to match against.
/// @param request the match radius, the magnitude rule and the candidate ceiling.
/// @return the correspondences and counts; `ErrorCode::NotInitialized` when `execution` holds
///         nothing or the catalog is empty; `ErrorCode::InvalidArgument` for a radius that is not
///         finite and positive, a non-finite magnitude limit or a zero candidate ceiling;
///         `ErrorCode::EmptySelection` for no sources; `ErrorCode::LimitExceeded` above the source
///         or candidate ceiling, carrying the requested and permitted counts;
///         `ErrorCode::InvalidData` for a positioned source whose coordinate is not a direction,
///         carrying its position; a memory-budget refusal; or cancellation. A field that matches
///         nothing is a success with no matches.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CatalogMatchResult>
matchCatalogSources(const ExecutionContext& execution, std::span<const CatalogSourceMeasurement> sources,
                    const SkyCatalog& catalog, const CatalogMatchRequest& request = {});

/// The field a list of correspondences is ordered by.
enum class CatalogMatchSortKey : std::uint8_t
{
    /// `CatalogMatch::sourceIndex`.
    SourceIndex = 0u,

    /// `CatalogMatch::entryIndex`.
    EntryIndex,

    /// `CatalogMatch::separationArcseconds`.
    SeparationArcseconds,

    /// The matched entry's `CatalogEntry::identifier`, compared byte by byte.
    ///
    /// This is what ordering a star list by its catalog name means once identity lives beside the
    /// star rather than inside it.
    CatalogIdentifier,

    /// The matched entry's `CatalogEntry::commonName`, compared byte by byte.
    CatalogCommonName,

    /// The matched entry's `CatalogEntry::magnitude`. An entry with no measured magnitude sorts
    /// after every entry that has one ascending, and before every one of them descending.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    CatalogMagnitude
};

/// Whether a value names a key this build orders correspondences by.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param key the value to inspect.
/// @return true for a declared key; false for an out-of-range cast.
[[nodiscard]] constexpr bool isCatalogMatchSortKey(CatalogMatchSortKey key) noexcept
{
    return static_cast<std::uint8_t>(key) <= static_cast<std::uint8_t>(CatalogMatchSortKey::CatalogMagnitude);
}

/// The key's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param key the key to name.
/// @return a null-terminated string literal compiled into this library, which outlives every
///         caller; `"UnknownCatalogMatchSortKey"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* catalogMatchSortKeyName(CatalogMatchSortKey key) noexcept;

/// Orders correspondences by one field, in place and deterministically.
///
/// The sort is stable: correspondences that compare equal on the key keep the relative order they
/// had, so two calls on the same list produce the same list and a sort by one key after another is
/// a composed order rather than an arbitrary one.
///
/// Runs on the calling thread. The stable sort may take a temporary buffer from the standard
/// library, outside the engine context's memory accounting.
///
/// @param matches the correspondences to order, which are rearranged in place.
/// @param catalog the catalog their entry positions refer to. Needed only for the three keys that
///        read an entry, and read for those.
/// @param key which field to order by.
/// @param direction which way.
/// @return success; `ErrorCode::InvalidArgument` for an out-of-range key or direction;
///         `ErrorCode::OutOfRange` when a correspondence names an entry the catalog does not hold.
///         The list is untouched on any failure.
[[nodiscard]] AFTERNIGHTENGINE_API Status
sortCatalogMatches(std::span<CatalogMatch> matches, const SkyCatalog& catalog, CatalogMatchSortKey key,
                   CatalogSortDirection direction = CatalogSortDirection::Ascending);

} // namespace an
