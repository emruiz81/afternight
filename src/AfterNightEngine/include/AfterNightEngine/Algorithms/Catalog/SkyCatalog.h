// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// An owned catalog and the region and name lookups it answers.
///
/// `buildSkyCatalog` is the one door into a catalog: a provider - a parsed text, a Gaia tile, a
/// network answer - produces `CatalogEntry` values and hands them here, and the resulting
/// `SkyCatalog` is an immutable table with a declination index and, when asked for, two exact-name
/// indices. Handles share the table, so copying one is a reference count, and the table's memory
/// is counted against the engine context it was built under for as long as any handle lives.
///
/// Queries answer with positions into `SkyCatalog::entries()` rather than pointers.
/// `queryCatalogRegion` reaches a cone's candidates through the declination index and a
/// right-ascension window and decides each by the exact great-circle separation;
/// `queryCatalogByName` looks a text up by exact name, canonical designation and substring. Both
/// orders are total, so an answer never depends on how the table happened to be laid out.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most entries one query returns unless the caller asks for fewer.
inline constexpr std::uint32_t kDefaultMaxCatalogQueryResults = 1000000u;

/// What a catalog is built to be.
struct SkyCatalogRequest
{
    /// The photometric band every entry's magnitude is measured in, such as `"V"` or `"G"`.
    ///
    /// Stated once for the whole table rather than repeated per entry, because a catalog whose rows
    /// disagree about their band is a mixture rather than a catalog. Free text: it is carried, shown
    /// and handed to a photometric fit, never switched on. Empty says the provider did not state
    /// one, which a consumer that needs a band must treat as unknown rather than assume.
    std::string photometricBand{};

    /// Whether to build the exact-name index that `queryCatalogByName` needs.
    ///
    /// A catalog searched only by region - a dense reference catalog behind a plate solve - pays
    /// neither the build time nor the memory for a name index it never reads. With this false a
    /// name query is refused by name rather than answered slowly.
    bool buildNameIndex = true;
};

/// A cone on the sky, and what may come back from inside it.
struct CatalogRegionQuery
{
    /// The centre of the cone.
    SkyCoordinate center{};

    /// The cone's angular radius, in degrees. Finite and greater than zero.
    double radiusDegrees = 1.0;

    /// The faintest magnitude admitted, or absent to admit every entry regardless of magnitude.
    ///
    /// A magnitude is smaller when brighter, so this is an upper bound.
    std::optional<double> magnitudeLimit{};

    /// Whether an entry with a common name but no measured magnitude is admitted anyway. Read only
    /// when `magnitudeLimit` is present; with no limit every entry is admitted.
    ///
    /// The difference between annotating a named nebula nobody has photometry for and inventing a
    /// bright star out of a missing measurement. Both are supported, and which one applies is the
    /// caller's stated choice.
    bool admitNamedEntriesWithoutMagnitude = false;

    /// The most positions the answer may hold. When more entries qualify, the ordering keeps the
    /// brightest of them and the result says it was truncated.
    std::uint32_t maximumResults = kDefaultMaxCatalogQueryResults;
};

/// A name or designation to look up, and how hard to look.
struct CatalogNameQuery
{
    /// The text to look for. Leading and trailing space is ignored; it need not outlive the call.
    std::string_view text{};

    /// Whether matching ignores case. ASCII case only, so a byte above 127 is never folded and a
    /// UTF-8 designation cannot be corrupted by the process locale.
    bool caseInsensitive = true;

    /// Whether to fall back to a substring scan when nothing matches exactly.
    bool matchSubstring = true;

    /// Whether to retry with `normalizeCatalogDesignation` applied to the text.
    ///
    /// This is what makes `m83` find `M 83`; it is one stated choice for one catalog surface.
    bool normalizeDesignation = true;

    /// The most positions the answer may hold. When more entries match, the lowest positions are
    /// kept and the result says it was truncated.
    std::uint32_t maximumResults = kDefaultMaxCatalogQueryResults;
};

/// The positions one query returned, and what it had to look at to return them.
struct CatalogQueryResult
{
    /// Positions into `SkyCatalog::entries()`, in the order the query defines.
    std::vector<std::uint32_t> entryIndices{};

    /// How many entries the query compared against its own condition.
    ///
    /// Smaller than the catalog for a region query, which reaches its candidates through the
    /// declination index rather than by scanning, and zero for a name query the exact-name index
    /// answered on its own.
    std::uint32_t examinedCount = 0u;

    /// How many entries satisfied the query, which is larger than the answer when it was truncated.
    std::uint32_t admittedCount = 0u;

    /// Whether the answer was cut down to `maximumResults`.
    bool truncated = false;
};

/// An owned, immutable catalog with its lookup indices.
///
/// Copies share immutable state, so handing one to another thread costs a reference count rather
/// than a copy of the table. A default object holds nothing; every accessor stays safe on one and
/// reports an empty catalog. Queries answer with positions into `entries()`, not with pointers,
/// so an answer cannot outlive its meaning without also outliving the handle it came from.
class SkyCatalog final
{
public:
    /// Builds an empty catalog.
    AFTERNIGHTENGINE_API SkyCatalog() noexcept;

    /// Shares an immutable catalog.
    ///
    /// @param other the catalog to share.
    AFTERNIGHTENGINE_API SkyCatalog(const SkyCatalog& other) noexcept;

    /// Takes another catalog's state, leaving it empty.
    ///
    /// @param other the catalog to take from.
    AFTERNIGHTENGINE_API SkyCatalog(SkyCatalog&& other) noexcept;

    /// Shares another immutable catalog.
    ///
    /// @param other the catalog to share.
    /// @return this catalog.
    AFTERNIGHTENGINE_API SkyCatalog& operator=(const SkyCatalog& other) noexcept;

    /// Takes another catalog's state, leaving it empty.
    ///
    /// @param other the catalog to take from.
    /// @return this catalog.
    AFTERNIGHTENGINE_API SkyCatalog& operator=(SkyCatalog&& other) noexcept;

    /// Releases this handle; the shared table lives until its last handle goes.
    AFTERNIGHTENGINE_API ~SkyCatalog();

    /// Whether this handle came from a successful build.
    ///
    /// @return true when it owns a table.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// How many entries the catalog holds.
    ///
    /// @return the count, or zero for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t entryCount() const noexcept;

    /// The entries, in the order they were supplied.
    ///
    /// @return a span valid for the life of this handle, empty for an empty one.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const CatalogEntry> entries() const noexcept;

    /// One entry by position.
    ///
    /// @param index a position below `entryCount()`.
    /// @return the entry, or null when the index is out of range or the handle is empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const CatalogEntry* entry(std::uint32_t index) const noexcept;

    /// The photometric band every magnitude in this catalog is measured in.
    ///
    /// @return the band stated at build time, valid for the life of this handle; empty when the
    ///         provider stated none.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view photometricBand() const noexcept;

    /// Whether this catalog carries the exact-name index a name query needs.
    ///
    /// @return true when the build was asked for one.
    [[nodiscard]] AFTERNIGHTENGINE_API bool hasNameIndex() const noexcept;

private:
    struct State;

    /// Builds a public handle around checked immutable state.
    ///
    /// @param state the state to own.
    explicit SkyCatalog(std::shared_ptr<const State> state) noexcept;

    /// The shared immutable table and its indices.
    std::shared_ptr<const State> m_state{};

    friend AFTERNIGHTENGINE_API Result<SkyCatalog> buildSkyCatalog(const ExecutionContext&, std::vector<CatalogEntry>,
                                                                   const SkyCatalogRequest&);
    friend AFTERNIGHTENGINE_API Result<CatalogQueryResult>
    queryCatalogRegion(const ExecutionContext&, const SkyCatalog&, const CatalogRegionQuery&);
    friend AFTERNIGHTENGINE_API Result<CatalogQueryResult>
    queryCatalogByName(const ExecutionContext&, const SkyCatalog&, const CatalogNameQuery&);
};

/// Builds an owned catalog and its lookup indices from entries the caller has already produced.
///
/// This is the one door into a catalog. A provider - a parsed text, a binary tile reader, a network
/// response - produces entries in its own way and hands them here, which is what keeps provider
/// vocabulary out of every query below.
///
/// Every entry is checked before any index is built: an identifier must be present, a coordinate
/// must be finite and inside its declared ranges, a magnitude that is present must be finite, and
/// an extent that is present must have finite, non-negative axes and a finite position angle. The
/// entries keep the order they were supplied, so a position returned by a query is a position in
/// the caller's own list.
///
/// Runs on the calling thread and checks cancellation at bounded entry boundaries. The table it
/// owns may outlive the execution. Its record storage, indexes and lookup keys are allocated from
/// the engine context and retain a thread-free lease until the last catalog handle is destroyed.
/// Entry strings are moved from the supplied records with their existing allocations.
///
/// @param execution the operation policy and engine-context lease.
/// @param entries the entries to own, moved into the catalog.
/// @param request the photometric band and whether to build the name index.
/// @return the catalog; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::EmptySelection` for no entries; `ErrorCode::LimitExceeded` above
///         `kMaxCatalogEntries`; `ErrorCode::InvalidData` for an entry with no identifier, a
///         coordinate that is not a direction, a non-finite magnitude or an extent that breaks its
///         rule, carrying the entry's position; the engine context memory's own refusal, which is
///         `ErrorCode::MemoryBudgetExceeded` for the budget; `ErrorCode::AllocationFailed` when the
///         platform refuses; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SkyCatalog> buildSkyCatalog(const ExecutionContext& execution,
                                                                      std::vector<CatalogEntry> entries,
                                                                      const SkyCatalogRequest& request = {});

/// Returns the entries inside a cone, brightest first.
///
/// Candidates are reached through the catalog's declination index and a right-ascension window
/// derived from the cone, so the work is proportional to what the cone contains rather than to the
/// size of the catalog. Every candidate is then admitted or refused by the exact great-circle
/// separation, so the index changes the cost and never the answer.
///
/// The order is total and deterministic: ascending magnitude, then descending angular area for an
/// extended object, then ascending identifier, then ascending position. An entry with no measured
/// magnitude sorts after every entry that has one.
///
/// Runs on the calling thread and checks cancellation at bounded candidate boundaries. It allocates
/// the answer and nothing else.
///
/// @param execution the operation policy and engine-context lease.
/// @param catalog the catalog to search.
/// @param query the cone, the magnitude rule and the answer bound.
/// @return the positions and counts; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the catalog is empty; `ErrorCode::InvalidArgument` for a centre that is not a direction,
///         a radius that is not finite and positive, a non-finite magnitude limit or a zero result
///         bound; or cancellation. A cone containing nothing is a success with an empty answer.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CatalogQueryResult>
queryCatalogRegion(const ExecutionContext& execution, const SkyCatalog& catalog, const CatalogRegionQuery& query);

/// Returns the entries whose identifier, common name or second designation matches a text.
///
/// Up to four passes, each tried only when the ones before it found nothing: the text as an exact
/// name, its canonical designation as an exact name, the text as a substring of any of the three
/// names, and its canonical designation as a substring of any of them. The two designation passes
/// run only when `normalizeDesignation` is set and the canonical spelling differs from the text,
/// and the two substring passes only when `matchSubstring` is set. Answers are in catalog order.
///
/// Runs on the calling thread, checks cancellation at bounded entry boundaries, and allocates the
/// answer and the folded and canonical spellings of the text.
///
/// @param execution the operation policy and engine-context lease.
/// @param catalog the catalog to search, which must have been built with a name index.
/// @param query the text and how hard to look.
/// @return the positions and counts; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the catalog is empty; `ErrorCode::Unsupported` when the catalog was built without a name
///         index; `ErrorCode::InvalidArgument` for empty text or a zero result bound; or
///         cancellation. Text that matches nothing is a success with an empty answer.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CatalogQueryResult>
queryCatalogByName(const ExecutionContext& execution, const SkyCatalog& catalog, const CatalogNameQuery& query);

} // namespace an
