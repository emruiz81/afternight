// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the owned catalog, its declination and name indices, and its two lookups.
//
// The shared state, the entries, the declination index and both name maps are allocated through
// one `CatalogMemory`, so every byte is counted against the building engine context and the state
// keeps that context leased until its last handle goes. A budget refusal anywhere in the build
// arrives as `CatalogAllocationFailure` and is returned with its typed status. The name maps hold
// each distinct trimmed name once, exact and ASCII-folded, with the ascending positions that carry
// it.

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"
#include "algorithms/catalog/CatalogMemory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <new>
#include <functional>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::CatalogAllocationFailure;
using catalog_detail::CatalogAllocator;
using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;
using catalog_detail::CatalogMemory;
using catalog_detail::containsText;
using catalog_detail::foldedCopy;
using catalog_detail::trimAscii;

constexpr const char* kBuildOperation = "an::buildSkyCatalog";
constexpr const char* kRegionOperation = "an::queryCatalogRegion";
constexpr const char* kNameOperation = "an::queryCatalogByName";
constexpr std::uint32_t kCancellationStride = 4096u;
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

/// How far the right-ascension window is widened past the angle its own bound implies.
///
/// A relative nanodegree: far above the rounding of one inverse sine and far below anything that
/// changes how many entries the window admits.
constexpr double kWindowSlack = 1.0e-9;

/// One entry's declination and where it sits in the table.
///
/// Sorted by declination, then by position, which makes the order total and therefore the band a
/// query walks the same band on every run.
struct DeclinationIndexEntry
{
    /// The entry's declination, in degrees.
    double declinationDegrees = 0.0;

    /// The entry's position in the table.
    std::uint32_t entryIndex = 0u;
};

/// Looks up borrowed names without allocating a key through a different allocator.
struct NameHash
{
    using is_transparent = void; // NOLINT(readability-identifier-naming): heterogeneous lookup protocol.
    std::size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};
struct NameEqual
{
    using is_transparent = void; // NOLINT(readability-identifier-naming): heterogeneous lookup protocol.
    bool operator()(std::string_view left, std::string_view right) const noexcept
    {
        return left == right;
    }
};
using NameIndex = std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::uint32_t>, NameHash, NameEqual>;

/// Adds one position under one key, keeping the positions ascending and free of repeats.
///
/// The key is borrowed and is only copied into the table's own memory when it turns out to be a
/// key the table does not hold, because the names of one catalog repeat: the identifier, common
/// name and designation of sixteen thousand entries are far fewer than forty-eight thousand
/// distinct strings, and materializing every one of them before the lookup pays for the copy on
/// the path that then discards it.
///
/// @param index the map to add to.
/// @param key the lookup key; an empty key is not indexed.
/// @param entryIndex the position to record.
/// @param memory where a key this table does not yet hold is copied to.
void addNameKey(NameIndex& index, std::string_view key, std::uint32_t entryIndex, std::pmr::memory_resource* memory)
{
    if (key.empty())
    {
        return;
    }
    auto position = index.find(key);
    if (position == index.end())
    {
        position = index.try_emplace(std::pmr::string(key, memory), 0u).first;
    }
    auto& positions = position->second;
    if (positions.empty() || positions.back() != entryIndex)
    {
        positions.push_back(entryIndex);
    }
}

/// Whether one entry passes the magnitude rule a query stated.
///
/// @param entry the entry to test.
/// @param magnitudeLimit the faintest magnitude admitted, or absent for no limit.
/// @param admitNamedWithoutMagnitude whether a named entry with no measurement is admitted anyway.
/// @return true when the entry is admitted.
[[nodiscard]] bool passesMagnitudeRule(const CatalogEntry& entry, const std::optional<double>& magnitudeLimit,
                                       const bool admitNamedWithoutMagnitude) noexcept
{
    if (!entry.magnitude.has_value())
    {
        return !magnitudeLimit.has_value() || (admitNamedWithoutMagnitude && !entry.commonName.empty());
    }
    return !magnitudeLimit.has_value() || *entry.magnitude <= *magnitudeLimit;
}

/// The angular area an entry covers, for the ordering's second key.
///
/// @param entry the entry to measure.
/// @return the product of the two axes in square arcminutes; zero for a point source.
[[nodiscard]] double extentArea(const CatalogEntry& entry) noexcept
{
    return entry.extent.has_value() ? entry.extent->majorAxisArcminutes * entry.extent->minorAxisArcminutes : 0.0;
}

/// Whether one entry sorts before another in a region answer.
///
/// Ascending magnitude, then descending area, then ascending identifier. An entry with no measured
/// magnitude sorts after every entry that has one.
///
/// @param left one entry.
/// @param right the other.
/// @return true when `left` comes first.
[[nodiscard]] bool regionOrderLess(const CatalogEntry& left, const CatalogEntry& right) noexcept
{
    if (left.magnitude.has_value() != right.magnitude.has_value())
    {
        return left.magnitude.has_value();
    }
    if (left.magnitude.has_value() && *left.magnitude != *right.magnitude)
    {
        return *left.magnitude < *right.magnitude;
    }
    const double leftArea = extentArea(left);
    const double rightArea = extentArea(right);
    if (leftArea != rightArea)
    {
        return leftArea > rightArea;
    }
    return left.identifier < right.identifier;
}

} // namespace

/// The immutable table one or more handles share.
struct SkyCatalog::State
{
    explicit State(std::pmr::memory_resource* memory)
        : entries(0u, memory), byDeclination(0u, memory), exactNames(0u, memory), foldedNames(0u, memory),
          photometricBand(std::string_view{}, memory)
    {
    }
    /// The entries, in the order they were supplied.
    std::pmr::vector<CatalogEntry> entries;

    /// Every entry's declination, ascending, with its position.
    std::pmr::vector<DeclinationIndexEntry> byDeclination;

    /// Identifiers, common names and designations exactly as written.
    NameIndex exactNames;

    /// The same keys folded to ASCII lower case.
    NameIndex foldedNames;

    /// The band every magnitude in this table is measured in.
    std::pmr::string photometricBand;

    /// Whether the two name indices were built.
    bool hasNameIndex = false;
};

SkyCatalog::SkyCatalog() noexcept = default;

SkyCatalog::SkyCatalog(const SkyCatalog& other) noexcept = default;

SkyCatalog::SkyCatalog(SkyCatalog&& other) noexcept = default;

SkyCatalog& SkyCatalog::operator=(const SkyCatalog& other) noexcept = default;

SkyCatalog& SkyCatalog::operator=(SkyCatalog&& other) noexcept = default;

SkyCatalog::~SkyCatalog() = default;

SkyCatalog::SkyCatalog(std::shared_ptr<const State> state) noexcept : m_state(std::move(state)) {}

bool SkyCatalog::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint32_t SkyCatalog::entryCount() const noexcept
{
    return m_state == nullptr ? 0u : static_cast<std::uint32_t>(m_state->entries.size());
}

std::span<const CatalogEntry> SkyCatalog::entries() const noexcept
{
    return m_state == nullptr ? std::span<const CatalogEntry>{} : std::span<const CatalogEntry>(m_state->entries);
}

const CatalogEntry* SkyCatalog::entry(const std::uint32_t index) const noexcept
{
    if (m_state == nullptr || index >= m_state->entries.size())
    {
        return nullptr;
    }
    return &m_state->entries[index];
}

std::string_view SkyCatalog::photometricBand() const noexcept
{
    return m_state == nullptr ? std::string_view{} : std::string_view(m_state->photometricBand);
}

bool SkyCatalog::hasNameIndex() const noexcept
{
    return m_state != nullptr && m_state->hasNameIndex;
}

Result<SkyCatalog> buildSkyCatalog(const ExecutionContext& execution, std::vector<CatalogEntry> entries,
                                   const SkyCatalogRequest& request)
try
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kBuildOperation);
    }
    if (entries.empty())
    {
        return catalogFailure(ErrorCode::EmptySelection, "a catalog holds at least one entry", kBuildOperation);
    }
    if (entries.size() > kMaxCatalogEntries)
    {
        return catalogLimitFailure("the catalog holds more entries than this engine keeps in memory", kBuildOperation,
                                   entries.size(), kMaxCatalogEntries);
    }

    const auto entryFailure = [](const char* message, std::size_t position) {
        return Status::failure(ErrorCode::InvalidData,
                               StatusDetail{.message = message,
                                            .operation = kBuildOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofCount(ContextKey::Completed, position)}});
    };

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
        if (entry.identifier.empty())
        {
            return entryFailure("a catalog entry has no identifier", position);
        }
        if (!isSkyCoordinate(entry.coordinate))
        {
            return entryFailure("a catalog entry's coordinate is not a direction on the sky", position);
        }
        if (entry.magnitude.has_value() && !std::isfinite(*entry.magnitude))
        {
            return entryFailure("a catalog entry's magnitude is present and not finite", position);
        }
        if (entry.extent.has_value() &&
            (!std::isfinite(entry.extent->majorAxisArcminutes) || !std::isfinite(entry.extent->minorAxisArcminutes) ||
             !std::isfinite(entry.extent->positionAngleDegrees) || entry.extent->majorAxisArcminutes < 0.0 ||
             entry.extent->minorAxisArcminutes < 0.0))
        {
            return entryFailure("a catalog entry's extent is present and is not two non-negative finite axes",
                                position);
        }
    }

    const auto memory = std::make_shared<CatalogMemory>(execution);
    auto state = std::allocate_shared<SkyCatalog::State>(CatalogAllocator<SkyCatalog::State>(memory), memory.get());
    state->entries.reserve(entries.size());
    for (auto& entry : entries)
    {
        if ((state->entries.size() % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        state->entries.push_back(std::move(entry));
    }
    std::vector<CatalogEntry>{}.swap(entries);
    state->photometricBand = request.photometricBand;
    state->hasNameIndex = request.buildNameIndex;

    state->byDeclination.resize(state->entries.size());
    for (std::size_t position = 0u; position < state->entries.size(); ++position)
    {
        state->byDeclination[position] =
            DeclinationIndexEntry{.declinationDegrees = state->entries[position].coordinate.declinationDegrees,
                                  .entryIndex = static_cast<std::uint32_t>(position)};
    }
    std::sort(state->byDeclination.begin(), state->byDeclination.end(),
              [](const DeclinationIndexEntry& left, const DeclinationIndexEntry& right) {
                  return left.declinationDegrees < right.declinationDegrees ||
                         (left.declinationDegrees == right.declinationDegrees && left.entryIndex < right.entryIndex);
              });

    if (state->hasNameIndex)
    {
        // Both tables are sized once for the three names an entry carries. Growing them a rehash at
        // a time reallocates the bucket array and rewires every node already in it, which is work
        // proportional to the table rather than to the entry being added.
        const std::size_t keyCeiling = state->entries.size() * 3u;
        state->exactNames.reserve(keyCeiling);
        state->foldedNames.reserve(keyCeiling);

        // One buffer folds every name. It is the table's own memory, so the fold is accounted like
        // the rest of the catalog, and it is reused rather than reallocated for each name.
        std::pmr::string folded(memory.get());
        for (std::size_t position = 0u; position < state->entries.size(); ++position)
        {
            if ((position % kCancellationStride) == 0u)
            {
                if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
                {
                    return cancelled;
                }
            }
            const CatalogEntry& entry = state->entries[position];
            const std::uint32_t index = static_cast<std::uint32_t>(position);
            for (const std::string_view name : {std::string_view(entry.identifier), std::string_view(entry.commonName),
                                                std::string_view(entry.designation)})
            {
                const std::string_view trimmed = trimAscii(name);
                addNameKey(state->exactNames, trimmed, index, memory.get());
                folded.assign(trimmed);
                for (char& byte : folded)
                {
                    byte = catalog_detail::foldAscii(byte);
                }
                addNameKey(state->foldedNames, folded, index, memory.get());
            }
        }
    }

    return SkyCatalog(std::move(state));
}
catch (const CatalogAllocationFailure& refused)
{
    return refused.status;
}
catch (const std::bad_alloc&)
{
    return catalogFailure(ErrorCode::AllocationFailed, "the platform refused catalog storage", kBuildOperation);
}

Result<CatalogQueryResult> queryCatalogRegion(const ExecutionContext& execution, const SkyCatalog& catalog,
                                              const CatalogRegionQuery& query)
{
    if (!execution.valid() || !catalog.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context or no catalog",
                              kRegionOperation);
    }
    if (!isSkyCoordinate(query.center))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the region centre is not a direction on the sky",
                              kRegionOperation);
    }
    if (!std::isfinite(query.radiusDegrees) || query.radiusDegrees <= 0.0)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the region radius must be finite and greater than zero",
                              kRegionOperation);
    }
    if (query.magnitudeLimit.has_value() && !std::isfinite(*query.magnitudeLimit))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a stated magnitude limit must be finite", kRegionOperation);
    }
    if (query.maximumResults == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a query returns at least one entry or none at all",
                              kRegionOperation);
    }

    const std::span<const CatalogEntry> entries = catalog.entries();
    const auto& byDeclination = catalog.m_state->byDeclination;

    // The declination band the cone spans. Everything outside it is further away than the radius in
    // declination alone, so the index seeks past it rather than measuring it.
    const double lowestDeclination = query.center.declinationDegrees - query.radiusDegrees;
    const double highestDeclination = query.center.declinationDegrees + query.radiusDegrees;
    const auto first = std::lower_bound(
        byDeclination.begin(), byDeclination.end(), lowestDeclination,
        [](const DeclinationIndexEntry& entry, const double value) { return entry.declinationDegrees < value; });
    const auto last = std::upper_bound(
        byDeclination.begin(), byDeclination.end(), highestDeclination,
        [](const double value, const DeclinationIndexEntry& entry) { return value < entry.declinationDegrees; });

    // The right-ascension window that band admits. Two directions separated by at most the radius
    // satisfy hav(dRa) <= hav(radius) / (cos d1 cos d2), and the cosine is smallest at the largest
    // declination the band reaches, so that bound is safe for every candidate in it. Near a pole the
    // bound covers the whole turn and the window is not applied at all.
    const double centreDeclination = std::abs(query.center.declinationDegrees);
    const double bandDeclination =
        (std::max)(centreDeclination, (std::max)(std::abs(lowestDeclination), std::abs(highestDeclination)));
    const double cosineFloor = std::cos((std::min)(bandDeclination, 90.0) * kRadiansPerDegree);
    const double halfRadius = std::sin(query.radiusDegrees * 0.5 * kRadiansPerDegree);
    const double radiusHaversine = halfRadius * halfRadius;
    bool useRightAscensionWindow = false;
    double rightAscensionWindowDegrees = 180.0;
    if (cosineFloor > 0.0)
    {
        const double bound = radiusHaversine / (cosineFloor * cosineFloor);
        if (bound < 1.0)
        {
            // The bound is exact, but the trigonometry that turns it into an angle is not, and a
            // window rounded one unit in the last place too narrow would drop an entry sitting on
            // the cone edge. Widening it by a nanodegree costs nothing and keeps the decision where
            // it belongs: with the exact separation below.
            rightAscensionWindowDegrees = 2.0 * std::asin(std::sqrt(bound)) / kRadiansPerDegree * (1.0 + kWindowSlack);
            useRightAscensionWindow = rightAscensionWindowDegrees < 180.0;
        }
    }

    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }

    CatalogQueryResult result;
    std::vector<std::uint32_t> admitted;
    std::uint32_t visited = 0u;
    for (auto cursor = first; cursor != last; ++cursor)
    {
        if ((visited % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        ++visited;

        const CatalogEntry& entry = entries[cursor->entryIndex];
        if (useRightAscensionWindow)
        {
            double difference = std::abs(entry.coordinate.rightAscensionDegrees - query.center.rightAscensionDegrees);
            if (difference > 180.0)
            {
                difference = 360.0 - difference;
            }
            if (difference > rightAscensionWindowDegrees)
            {
                continue;
            }
        }
        ++result.examinedCount;
        if (!passesMagnitudeRule(entry, query.magnitudeLimit, query.admitNamedEntriesWithoutMagnitude))
        {
            continue;
        }
        if (angularSeparationDegrees(query.center, entry.coordinate) > query.radiusDegrees)
        {
            continue;
        }
        admitted.push_back(cursor->entryIndex);
    }

    result.admittedCount = static_cast<std::uint32_t>(admitted.size());
    std::sort(admitted.begin(), admitted.end(), [entries](const std::uint32_t left, const std::uint32_t right) {
        if (regionOrderLess(entries[left], entries[right]))
        {
            return true;
        }
        if (regionOrderLess(entries[right], entries[left]))
        {
            return false;
        }
        return left < right;
    });
    if (admitted.size() > query.maximumResults)
    {
        admitted.resize(query.maximumResults);
        result.truncated = true;
    }
    result.entryIndices = std::move(admitted);
    return result;
}

Result<CatalogQueryResult> queryCatalogByName(const ExecutionContext& execution, const SkyCatalog& catalog,
                                              const CatalogNameQuery& query)
{
    if (!execution.valid() || !catalog.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context or no catalog",
                              kNameOperation);
    }
    if (!catalog.hasNameIndex())
    {
        return catalogFailure(ErrorCode::Unsupported, "this catalog was built without a name index", kNameOperation);
    }
    if (query.maximumResults == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a query returns at least one entry or none at all",
                              kNameOperation);
    }
    const std::string_view text = trimAscii(query.text);
    if (text.empty())
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a name query needs something to look for", kNameOperation);
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }

    const std::span<const CatalogEntry> entries = catalog.entries();
    const NameIndex& index = query.caseInsensitive ? catalog.m_state->foldedNames : catalog.m_state->exactNames;
    const std::string exactKey = query.caseInsensitive ? foldedCopy(text) : std::string(text);
    const std::string normalized = query.normalizeDesignation ? normalizeCatalogDesignation(text) : std::string(text);

    CatalogQueryResult result;
    std::vector<std::uint32_t> found;

    const auto lookup = [&index, &found](const std::string& key) {
        const auto position = index.find(key);
        if (position != index.end())
        {
            found.assign(position->second.begin(), position->second.end());
        }
    };

    lookup(exactKey);
    if (found.empty() && normalized != text)
    {
        lookup(query.caseInsensitive ? foldedCopy(normalized) : normalized);
    }

    if (found.empty() && query.matchSubstring)
    {
        // The literal text first, then its canonical designation when that is a different text.
        std::vector<std::string_view> needles;
        needles.push_back(text);
        if (normalized != text)
        {
            needles.push_back(normalized);
        }
        for (const std::string_view needle : needles)
        {
            if (!found.empty())
            {
                break;
            }
            for (std::size_t position = 0u; position < entries.size(); ++position)
            {
                if ((position % kCancellationStride) == 0u)
                {
                    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
                    {
                        return cancelled;
                    }
                }
                ++result.examinedCount;
                const CatalogEntry& entry = entries[position];
                if (containsText(entry.identifier, needle, query.caseInsensitive) ||
                    containsText(entry.commonName, needle, query.caseInsensitive) ||
                    containsText(entry.designation, needle, query.caseInsensitive))
                {
                    found.push_back(static_cast<std::uint32_t>(position));
                }
            }
        }
    }

    result.admittedCount = static_cast<std::uint32_t>(found.size());
    if (found.size() > query.maximumResults)
    {
        found.resize(query.maximumResults);
        result.truncated = true;
    }
    result.entryIndices = std::move(found);
    return result;
}

} // namespace an
