// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the sampled-spectrum catalog: manifest, chunk container, owned table, region
// and identifier lookups, chunk selection, readiness and the recorded-match join.
//
// One chunk record is `kXpChunkRecordBytes` long and little-endian. Its 62-byte head holds the
// source identifier (8 bytes), right ascension and declination (8 each), the two proper-motion
// components, the reference epoch and the G, BP and RP magnitudes (4-byte floats), the flux and
// error quantization scales (4-byte floats), the quality flags and the valid-sample count (2 bytes
// each) and the source kind (1 byte). Then come the grid's count of signed 16-bit flux codes and
// as many unsigned 16-bit error codes, each multiplied by its scale; the rest of the record is not
// read.
//
// The owned table keeps every spectrum in two contiguous record-major arrays, an index of record
// positions sorted by identifier for the join, and one sorted by declination with the declinations
// beside it for the region query. All of it is allocated through one `CatalogMemory`, so it is
// counted against the building engine context and keeps that context leased.

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"
#include "algorithms/catalog/CatalogJson.h"
#include "algorithms/catalog/GaiaBinary.h"
#include "algorithms/catalog/CatalogMemory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{

using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;
using catalog_detail::findJsonMember;
using catalog_detail::jsonIsArray;
using catalog_detail::JsonLimits;
using catalog_detail::JsonMember;
using catalog_detail::jsonNumber;
using catalog_detail::JsonScanStatus;
using catalog_detail::jsonString;
using catalog_detail::jsonUnsigned;
using catalog_detail::readFloat32LittleEndian;
using catalog_detail::readFloat64LittleEndian;
using catalog_detail::readInt16LittleEndian;
using catalog_detail::readUint16LittleEndian;
using catalog_detail::readUint32LittleEndian;
using catalog_detail::readUint64LittleEndian;
using catalog_detail::scanJsonArray;
using catalog_detail::scanJsonObject;

using catalog_detail::CatalogAllocationFailure;
using catalog_detail::CatalogAllocator;
using catalog_detail::CatalogMemory;

/// The shared immutable table behind every handle.
struct SpectrumCatalog::State
{
    explicit State(std::pmr::memory_resource* memory)
        : records(0u, memory), flux(0u, memory), fluxError(0u, memory), loadedChunks(0u, memory),
          byIdentifier(0u, memory), byDeclination(0u, memory), sortedDeclinations(0u, memory)
    {
    }
    /// The manifest the catalog was built under.
    SpectrumCatalogManifest manifest{};

    /// The grid every spectrum is on.
    SpectralGrid grid{};

    /// The records, chunk order then container order.
    std::pmr::vector<SpectrumRecord> records;

    /// Record-major flux samples.
    std::pmr::vector<float> flux;

    /// Record-major flux uncertainties.
    std::pmr::vector<float> fluxError;

    /// Which chunks are held, ascending.
    std::pmr::vector<std::int32_t> loadedChunks;

    /// Record positions sorted by identifier, for the binary search.
    std::pmr::vector<std::uint32_t> byIdentifier;

    /// Record positions sorted by declination, for the region window.
    std::pmr::vector<std::uint32_t> byDeclination;

    /// The declinations in the same order as `byDeclination`, so a bound is one comparison.
    std::pmr::vector<double> sortedDeclinations;
};

namespace
{

constexpr const char* kManifestOperation = "an::parseSpectrumCatalogManifest";
constexpr const char* kChunkOperation = "an::decodeSpectrumChunk";
constexpr const char* kBuildOperation = "an::buildSpectrumCatalog";
constexpr const char* kQueryOperation = "an::querySpectrumCatalogRegion";
constexpr const char* kRegionChunksOperation = "an::selectSpectrumChunksForRegion";
constexpr const char* kSourceChunksOperation = "an::selectSpectrumChunksForSources";
constexpr const char* kResolveOperation = "an::resolveMatchedSources";
constexpr std::uint32_t kCancellationStride = 1024u;

/// How many tiles the sky has at a HEALPix order; zero above `kMaximumHealpixOrder`.
[[nodiscard]] constexpr std::uint32_t tileCountForOrder(const std::uint8_t order) noexcept
{
    return healpixPixelCount(HealpixGrid{.order = order, .ordering = HealpixOrdering::Nested});
}

/// A member's string value, or a stated default when the member is absent.
///
/// @param members the scanned members.
/// @param name the member.
/// @param fallback what an absent member means.
/// @param out receives the value.
/// @return false when the member is present and not a string.
[[nodiscard]] bool stringMember(const std::span<const JsonMember> members, const std::string_view name,
                                const std::string_view fallback, std::string& out)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        out = std::string(fallback);
        return true;
    }
    std::optional<std::string> parsed = jsonString(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    out = std::move(*parsed);
    return true;
}

/// A member's unsigned value, or a stated default when the member is absent.
///
/// @param members the scanned members.
/// @param name the member.
/// @param fallback what an absent member means.
/// @param out receives the value.
/// @return false when the member is present and not a non-negative whole number.
[[nodiscard]] bool unsignedMember(const std::span<const JsonMember> members, const std::string_view name,
                                  const std::uint64_t fallback, std::uint64_t& out)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        out = fallback;
        return true;
    }
    const std::optional<std::uint64_t> parsed = jsonUnsigned(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    out = *parsed;
    return true;
}

/// A member's number, or not-a-number when the member is absent.
///
/// @param members the scanned members.
/// @param name the member.
/// @param out receives the value.
/// @return false when the member is present and not a number.
[[nodiscard]] bool numberMember(const std::span<const JsonMember> members, const std::string_view name, double& out)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    const std::optional<double> parsed = jsonNumber(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    out = *parsed;
    return true;
}

/// The members of a nested object member, or none when it is absent.
///
/// @param members the scanned members.
/// @param name the member.
/// @param limits how much to admit.
/// @param out receives the nested members, empty when absent.
/// @return false when the member is present and not an object.
[[nodiscard]] bool objectMember(const std::span<const JsonMember> members, const std::string_view name,
                                const JsonLimits& limits, std::vector<JsonMember>& out)
{
    out.clear();
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        return true;
    }
    return scanJsonObject(member->value, limits, out) == JsonScanStatus::Ok;
}

/// A manifest failure naming the member at fault.
///
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status manifestFailure(const char* const message)
{
    return catalogFailure(ErrorCode::InvalidData, message, kManifestOperation);
}

/// The access profile a manifest spelling names.
///
/// @param text the manifest's spelling.
/// @return the profile, or absent for a spelling this build does not read.
[[nodiscard]] std::optional<SpectrumCatalogAccessProfile> accessProfileOf(const std::string_view text) noexcept
{
    if (text == "local-full" || text == "local-full-install")
    {
        return SpectrumCatalogAccessProfile::LocalFullInstall;
    }
    if (text == "installed-regional-cache" || text == "regional-cache")
    {
        return SpectrumCatalogAccessProfile::InstalledRegionalCache;
    }
    if (text == "managed-remote")
    {
        return SpectrumCatalogAccessProfile::ManagedRemote;
    }
    if (text == "blocked")
    {
        return SpectrumCatalogAccessProfile::Blocked;
    }
    return std::nullopt;
}

/// The source kind a manifest spelling names.
///
/// @param text the manifest's spelling.
/// @return the kind, or absent for a spelling this build does not read.
[[nodiscard]] std::optional<SpectrumSourceKind> sourceKindOf(const std::string_view text) noexcept
{
    if (text == "gaia_dr3_xp_sampled")
    {
        return SpectrumSourceKind::GaiaDr3XpSampled;
    }
    if (text == "gaia_dr3_xp_continuous_experimental")
    {
        return SpectrumSourceKind::GaiaDr3XpContinuousExperimental;
    }
    if (text == "synthetic_fixture")
    {
        return SpectrumSourceKind::SyntheticFixture;
    }
    return std::nullopt;
}

/// Whether a manifest path is relative and stays inside the manifest's directory.
///
/// @param path the path as the manifest spells it.
/// @return true for a non-empty path that is not absolute and has no parent step.
[[nodiscard]] bool isContainedRelativePath(const std::string_view path) noexcept
{
    if (path.empty() || path.front() == '/' || path.front() == '\\')
    {
        return false;
    }
    if (path.size() >= 2u && path[1] == ':')
    {
        return false;
    }
    std::size_t begin = 0u;
    while (begin <= path.size())
    {
        std::size_t end = begin;
        while (end < path.size() && path[end] != '/' && path[end] != '\\')
        {
            ++end;
        }
        if (path.substr(begin, end - begin) == "..")
        {
            return false;
        }
        begin = end + 1u;
    }
    return true;
}

/// Reads one chunk descriptor from its object text.
///
/// @param text the chunk's object.
/// @param limits how much to admit.
/// @param out receives the descriptor.
/// @return success, or the member that refused.
[[nodiscard]] Status parseChunkDescriptor(const std::string_view text, const JsonLimits& limits,
                                          SpectrumChunkDescriptor& out)
{
    std::vector<JsonMember> members;
    if (scanJsonObject(text, limits, members) != JsonScanStatus::Ok)
    {
        return manifestFailure("a chunk entry is not an object");
    }
    std::uint64_t chunkIndex = 0u;
    std::uint64_t firstTile = 0u;
    std::uint64_t tileCount = 0u;
    if (!unsignedMember(members, "chunk_index", 0u, chunkIndex) ||
        !unsignedMember(members, "first_tile", 0u, firstTile) ||
        !unsignedMember(members, "tile_count", 0u, tileCount) ||
        !unsignedMember(members, "record_count", 0u, out.recordCount) ||
        !unsignedMember(members, "size_bytes", 0u, out.sizeBytes) || !stringMember(members, "file", "", out.file) ||
        !stringMember(members, "sha256", "", out.sha256))
    {
        return manifestFailure("a chunk entry carries a member of the wrong type");
    }
    if (chunkIndex > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
        firstTile > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
        tileCount > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return manifestFailure("a chunk entry's index or tile range is out of range");
    }
    out.chunkIndex = static_cast<std::int32_t>(chunkIndex);
    out.firstTile = static_cast<std::int32_t>(firstTile);
    out.tileCount = static_cast<std::int32_t>(tileCount);
    return Status::success();
}

/// The chunk whose declared range holds a tile.
///
/// @param manifest the manifest.
/// @param tile the order-six nested tile.
/// @return the chunk's position, or absent when no chunk declares the tile.
[[nodiscard]] std::optional<std::uint32_t> chunkForTile(const SpectrumCatalogManifest& manifest,
                                                        const std::int64_t tile) noexcept
{
    for (std::size_t index = 0u; index < manifest.chunks.size(); ++index)
    {
        const SpectrumChunkDescriptor& chunk = manifest.chunks[index];
        if (chunk.tileCount > 0 && tile >= chunk.firstTile &&
            tile < static_cast<std::int64_t>(chunk.firstTile) + chunk.tileCount)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

/// Whether a manifest is one undivided chunk with no tile range.
///
/// @param manifest the manifest.
/// @return true for exactly one chunk declaring no tiles.
[[nodiscard]] bool isUndividedManifest(const SpectrumCatalogManifest& manifest) noexcept
{
    return manifest.chunks.size() == 1u && manifest.chunks.front().tileCount <= 0;
}

/// A finite float read as an optional double; not-a-number is absent.
///
/// @param value the stored value.
/// @return the value, or absent when it is not finite.
[[nodiscard]] std::optional<double> optionalOf(const float value) noexcept
{
    return std::isfinite(value) ? std::optional<double>{static_cast<double>(value)} : std::nullopt;
}

/// Orders records brightest first by G, unmeasured last, then by identifier.
///
/// A total order: a measured magnitude before an unmeasured one, then the magnitudes compared
/// exactly, then the identifier. Magnitudes are finite whenever they are present, so the exact
/// comparison is a strict weak ordering, which the standard sort that uses this requires. A
/// tolerance would call two magnitudes a step apart equal and two steps apart different - an
/// intransitive comparison, on which a sort's behaviour is undefined and a region's order would
/// depend on the order its records were found in.
///
/// @param left one record.
/// @param right the other.
/// @return true when `left` sorts first.
[[nodiscard]] bool brighterRecord(const SpectrumRecord& left, const SpectrumRecord& right) noexcept
{
    if (left.magnitudeG.has_value() != right.magnitudeG.has_value())
    {
        return left.magnitudeG.has_value();
    }
    if (left.magnitudeG.has_value() && *left.magnitudeG != *right.magnitudeG)
    {
        return *left.magnitudeG < *right.magnitudeG;
    }
    return left.sourceIdentifier < right.sourceIdentifier;
}

/// Whether a record's magnitude admits it under a query's rule.
///
/// @param record the record.
/// @param query the rule.
/// @return true when admitted.
[[nodiscard]] bool magnitudeAdmits(const SpectrumRecord& record, const CatalogRegionQuery& query) noexcept
{
    if (!query.magnitudeLimit.has_value())
    {
        return true;
    }
    if (!record.magnitudeG.has_value())
    {
        return query.admitNamedEntriesWithoutMagnitude;
    }
    return *record.magnitudeG <= *query.magnitudeLimit;
}

/// Whether a chunk decode or build has been given records the table can hold.
///
/// @param count how many records are proposed.
/// @param limit the ceiling.
/// @param operation the entry point speaking.
/// @return success, or the ceiling failure.
[[nodiscard]] Status checkRecordCeiling(const std::uint64_t count, const std::uint64_t limit,
                                        const char* const operation)
{
    if (count > limit)
    {
        return catalogLimitFailure("the record count exceeds the stated ceiling", operation, count, limit);
    }
    return Status::success();
}

} // namespace

const char* spectrumSourceKindName(const SpectrumSourceKind kind) noexcept
{
    switch (kind)
    {
    case SpectrumSourceKind::GaiaDr3XpSampled:
        return "GaiaDr3XpSampled";
    case SpectrumSourceKind::GaiaDr3XpContinuousExperimental:
        return "GaiaDr3XpContinuousExperimental";
    case SpectrumSourceKind::SyntheticFixture:
        return "SyntheticFixture";
    }
    return "UnknownSpectrumSourceKind";
}

const char* spectrumCatalogAccessProfileName(const SpectrumCatalogAccessProfile profile) noexcept
{
    switch (profile)
    {
    case SpectrumCatalogAccessProfile::LocalFullInstall:
        return "local-full-install";
    case SpectrumCatalogAccessProfile::InstalledRegionalCache:
        return "installed-regional-cache";
    case SpectrumCatalogAccessProfile::ManagedRemote:
        return "managed-remote";
    case SpectrumCatalogAccessProfile::Blocked:
        return "blocked";
    }
    return "UnknownSpectrumCatalogAccessProfile";
}

const char* spectrumCatalogReadinessErrorName(const SpectrumCatalogReadinessError error) noexcept
{
    switch (error)
    {
    case SpectrumCatalogReadinessError::None:
        return "None";
    case SpectrumCatalogReadinessError::AccessProfileNotLocal:
        return "AccessProfileNotLocal";
    case SpectrumCatalogReadinessError::MissingIdentity:
        return "MissingIdentity";
    case SpectrumCatalogReadinessError::ChecksumInvalidOrStale:
        return "ChecksumInvalidOrStale";
    case SpectrumCatalogReadinessError::RightsReviewNotApproved:
        return "RightsReviewNotApproved";
    case SpectrumCatalogReadinessError::RegionNotCovered:
        return "RegionNotCovered";
    }
    return "UnknownSpectrumCatalogReadinessError";
}

const char* matchedSourceLookupStatusName(const MatchedSourceLookupStatus status) noexcept
{
    switch (status)
    {
    case MatchedSourceLookupStatus::Satisfied:
        return "Satisfied";
    case MatchedSourceLookupStatus::InvalidSourceIdentifier:
        return "InvalidSourceIdentifier";
    case MatchedSourceLookupStatus::MissingSource:
        return "MissingSource";
    case MatchedSourceLookupStatus::MissingChunkOrCache:
        return "MissingChunkOrCache";
    case MatchedSourceLookupStatus::ManifestMismatch:
        return "ManifestMismatch";
    case MatchedSourceLookupStatus::StaleMatch:
        return "StaleMatch";
    case MatchedSourceLookupStatus::RejectedQualityFlags:
        return "RejectedQualityFlags";
    }
    return "UnknownMatchedSourceLookupStatus";
}

Result<SpectrumCatalogManifest> parseSpectrumCatalogManifest(const ExecutionContext& execution,
                                                             const std::string_view text,
                                                             const SpectrumManifestLimits& limits)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a manifest parse needs an execution context",
                              kManifestOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (text.size() > limits.maximumBytes)
    {
        return catalogLimitFailure("the manifest text exceeds the stated ceiling", kManifestOperation, text.size(),
                                   limits.maximumBytes);
    }

    const JsonLimits jsonLimits{.maximumEntries = 4096u, .maximumDepth = 16u};
    std::vector<JsonMember> members;
    if (scanJsonObject(text, jsonLimits, members) != JsonScanStatus::Ok)
    {
        return manifestFailure("the manifest text is not one JSON object");
    }

    SpectrumCatalogManifest manifest;
    std::string accessProfileText;
    std::string sourceKindText;
    std::uint64_t recordSizeBytes = 0u;
    std::uint64_t healpixOrder = 0u;
    std::uint64_t chunkParentOrder = 0u;
    std::vector<JsonMember> nested;
    if (!stringMember(members, "schema_version", "", manifest.schemaVersion) ||
        !stringMember(members, "catalog_family_id", "", manifest.catalogFamilyId) ||
        !stringMember(members, "manifest_id", "", manifest.manifestId) ||
        !stringMember(members, "access_profile", "", accessProfileText) ||
        !stringMember(members, "source_kind", "gaia_dr3_xp_sampled", sourceKindText) ||
        !stringMember(members, "payload_kind", "", manifest.payloadKind) ||
        !unsignedMember(members, "total_records", 0u, manifest.totalRecords) ||
        !stringMember(members, "flux_unit", "", manifest.fluxUnit))
    {
        return manifestFailure("a top-level manifest member has the wrong type");
    }
    if (!objectMember(members, "rights", jsonLimits, nested) ||
        !stringMember(nested, "review_status", "", manifest.rightsReviewStatus))
    {
        return manifestFailure("the rights member is not an object of strings");
    }
    if (!objectMember(members, "files", jsonLimits, nested) ||
        !stringMember(nested, "chunk_manifest", "", manifest.chunkManifestFile) ||
        !stringMember(nested, "source_id_index", "", manifest.sourceIdentifierIndexFile) ||
        !stringMember(nested, "coverage_summary", "", manifest.coverageSummaryFile))
    {
        return manifestFailure("the files member is not an object of strings");
    }

    if (manifest.schemaVersion != kXpCatalogManifestSchema)
    {
        return manifestFailure("the manifest schema is not one this build reads");
    }
    if (manifest.catalogFamilyId != kXpCatalogFamilyId)
    {
        return manifestFailure("the manifest names a catalog family this build does not read");
    }
    if (manifest.manifestId.empty() || manifest.manifestId == manifest.catalogFamilyId)
    {
        return manifestFailure("the manifest identity is empty or repeats the family");
    }
    const std::optional<SpectrumCatalogAccessProfile> profile = accessProfileOf(accessProfileText);
    if (!profile.has_value())
    {
        return catalogFailure(ErrorCode::Unsupported, "the manifest names an access profile this build does not read",
                              kManifestOperation);
    }
    manifest.accessProfile = *profile;
    const std::optional<SpectrumSourceKind> kind = sourceKindOf(sourceKindText);
    if (!kind.has_value())
    {
        return catalogFailure(ErrorCode::Unsupported, "the manifest names a spectrum source this build does not read",
                              kManifestOperation);
    }
    manifest.sourceKind = *kind;

    std::string magic;
    std::string byteOrder;
    std::uint64_t formatVersion = 0u;
    std::uint64_t recordVersion = 0u;
    if (!objectMember(members, "format", jsonLimits, nested) || !stringMember(nested, "magic", "", magic) ||
        !unsignedMember(nested, "format_version", 0u, formatVersion) ||
        !unsignedMember(nested, "record_version", 0u, recordVersion) ||
        !stringMember(nested, "byte_order", "little-endian", byteOrder) ||
        !unsignedMember(nested, "record_size_bytes", 0u, recordSizeBytes) ||
        !unsignedMember(nested, "healpix_order", 0u, healpixOrder) ||
        !unsignedMember(nested, "chunk_parent_healpix_order", 0u, chunkParentOrder))
    {
        return manifestFailure("the format member is not an object of the expected members");
    }
    if (magic != kXpChunkMagic || formatVersion != kXpChunkFormatVersion || recordVersion != kXpChunkRecordVersion)
    {
        return manifestFailure("the manifest does not describe version-one ANXP chunks");
    }
    if (byteOrder != "little-endian")
    {
        return manifestFailure("the manifest names a byte order this build does not read");
    }
    if (recordSizeBytes != kXpChunkRecordBytes)
    {
        return manifestFailure("the manifest record size is not the format's");
    }
    if (healpixOrder != kXpCatalogHealpixOrder || chunkParentOrder != kXpChunkParentHealpixOrder)
    {
        return manifestFailure("the manifest tile orders are not the format's");
    }
    manifest.recordSizeBytes = kXpChunkRecordBytes;
    manifest.healpixOrder = kXpCatalogHealpixOrder;
    manifest.chunkParentHealpixOrder = kXpChunkParentHealpixOrder;

    std::uint64_t sampleCount = 0u;
    if (!objectMember(members, "wavelength_grid", jsonLimits, nested) ||
        !unsignedMember(nested, "sample_count", 0u, sampleCount) ||
        !numberMember(nested, "start_nm", manifest.grid.startNm) ||
        !numberMember(nested, "step_nm", manifest.grid.stepNm) || !numberMember(nested, "end_nm", manifest.grid.endNm))
    {
        return manifestFailure("the wavelength grid is not an object of numbers");
    }
    manifest.grid.sampleCount =
        sampleCount > std::numeric_limits<std::uint32_t>::max() ? 0u : static_cast<std::uint32_t>(sampleCount);
    if (!isXpSampledGrid(manifest.grid))
    {
        return manifestFailure("the wavelength grid is not the sampled Gaia XP grid");
    }

    const JsonMember* const chunksMember = findJsonMember(members, "chunks");
    if (chunksMember == nullptr || !jsonIsArray(chunksMember->value))
    {
        return manifestFailure("the manifest has no chunk array");
    }
    std::vector<std::string_view> elements;
    const auto scanned = scanJsonArray(
        chunksMember->value, JsonLimits{.maximumEntries = limits.maximumChunks, .maximumDepth = 16u}, elements);
    if (scanned == JsonScanStatus::TooManyEntries)
        return catalogLimitFailure("the manifest lists more chunks than the stated ceiling", kManifestOperation,
                                   static_cast<std::uint64_t>(limits.maximumChunks) + 1u, limits.maximumChunks);
    if (scanned != JsonScanStatus::Ok)
        return manifestFailure("the chunk array is malformed or exceeds the supported nesting depth");
    if (elements.empty())
    {
        return manifestFailure("the manifest lists no chunks");
    }
    manifest.chunks.reserve(elements.size());
    const std::uint32_t tileCount = tileCountForOrder(kXpCatalogHealpixOrder);
    std::vector<std::uint8_t> claimed(tileCount, 0u);
    std::uint64_t declaredRecords = 0u;
    for (std::size_t index = 0u; index < elements.size(); ++index)
    {
        SpectrumChunkDescriptor chunk;
        if (const Status parsed = parseChunkDescriptor(elements[index], jsonLimits, chunk); parsed.failed())
        {
            return parsed;
        }
        if (chunk.chunkIndex != static_cast<std::int32_t>(index))
        {
            return manifestFailure("chunk indices are contiguous from zero");
        }
        if (!isContainedRelativePath(chunk.file))
        {
            return manifestFailure("a chunk path is not a contained relative path");
        }
        for (const SpectrumChunkDescriptor& earlier : manifest.chunks)
        {
            if (earlier.file == chunk.file)
            {
                return manifestFailure("two chunks name the same file");
            }
        }
        if (!(elements.size() == 1u && chunk.tileCount == 0))
        {
            if (chunk.tileCount <= 0 || chunk.firstTile < 0 ||
                static_cast<std::uint64_t>(chunk.firstTile) + static_cast<std::uint64_t>(chunk.tileCount) > tileCount)
            {
                return manifestFailure("a chunk tile range lies outside the order-six sky");
            }
            for (std::int32_t tile = chunk.firstTile; tile < chunk.firstTile + chunk.tileCount; ++tile)
            {
                if (claimed[static_cast<std::size_t>(tile)] != 0u)
                {
                    return manifestFailure("two chunks declare the same tile");
                }
                claimed[static_cast<std::size_t>(tile)] = 1u;
            }
        }
        declaredRecords += chunk.recordCount;
        manifest.chunks.push_back(std::move(chunk));
    }
    if (declaredRecords != manifest.totalRecords)
    {
        return manifestFailure("the total record count is not the sum of the chunks");
    }
    return manifest;
}

Result<SpectrumChunk> decodeSpectrumChunk(const ExecutionContext& execution, const std::span<const std::byte> bytes,
                                          const SpectrumCatalogManifest& manifest, const std::uint32_t chunkIndex,
                                          const SpectrumChunkLimits& limits)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a chunk decode needs an execution context", kChunkOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (chunkIndex >= manifest.chunks.size())
    {
        return catalogFailure(ErrorCode::OutOfRange, "the manifest lists no such chunk", kChunkOperation);
    }
    const SpectrumChunkDescriptor& descriptor = manifest.chunks[chunkIndex];
    const auto chunkFailure = [&](const char* const message) {
        return Status::failure(
            ErrorCode::InvalidData,
            StatusDetail{.message = message,
                         .operation = kChunkOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofText(ContextKey::Path, descriptor.file)}});
    };

    if (bytes.size() < kXpChunkPreambleBytes)
    {
        return chunkFailure("the chunk is shorter than its preamble");
    }
    for (std::size_t index = 0u; index < 4u; ++index)
    {
        if (static_cast<char>(bytes[index]) != kXpChunkMagic[index])
        {
            return chunkFailure("the chunk does not begin with the ANXP magic");
        }
    }
    if (readUint16LittleEndian(bytes, 4u) != kXpChunkFormatVersion ||
        readUint16LittleEndian(bytes, 6u) != kXpChunkRecordVersion)
    {
        return chunkFailure("the chunk versions are not the ones this build reads");
    }
    const std::uint32_t headerBytes = readUint32LittleEndian(bytes, 8u);
    const std::uint32_t recordCount = readUint32LittleEndian(bytes, 12u);
    if (headerBytes == 0u)
    {
        return chunkFailure("the chunk header length is zero");
    }
    if (headerBytes > limits.maximumHeaderBytes)
    {
        return catalogLimitFailure("the chunk header exceeds the stated ceiling", kChunkOperation, headerBytes,
                                   limits.maximumHeaderBytes);
    }
    if (recordCount != descriptor.recordCount)
    {
        return chunkFailure("the chunk record count disagrees with the manifest");
    }
    if (const Status ceiling = checkRecordCeiling(recordCount, limits.maximumRecords, kChunkOperation);
        ceiling.failed())
    {
        return ceiling;
    }
    const std::uint64_t expectedBytes = static_cast<std::uint64_t>(kXpChunkPreambleBytes) + headerBytes +
                                        static_cast<std::uint64_t>(recordCount) * kXpChunkRecordBytes;
    if (bytes.size() != expectedBytes)
    {
        return chunkFailure("the chunk size disagrees with its preamble");
    }
    if (descriptor.sizeBytes != 0u && descriptor.sizeBytes != bytes.size())
    {
        return chunkFailure("the chunk size disagrees with the manifest");
    }

    const std::string_view headerText{reinterpret_cast<const char*>(bytes.data() + kXpChunkPreambleBytes), headerBytes};
    std::vector<JsonMember> members;
    if (scanJsonObject(headerText, JsonLimits{.maximumEntries = 4096u, .maximumDepth = 16u}, members) !=
        JsonScanStatus::Ok)
    {
        return chunkFailure("the chunk header is not one JSON object");
    }
    std::string family;
    std::string manifestId;
    std::uint64_t headerChunkIndex = 0u;
    std::uint64_t headerRecordCount = 0u;
    std::uint64_t headerRecordBytes = 0u;
    std::uint64_t headerFirstTile = static_cast<std::uint64_t>((std::max)(descriptor.firstTile, 0));
    std::uint64_t headerTileCount = static_cast<std::uint64_t>((std::max)(descriptor.tileCount, 0));
    if (!stringMember(members, "catalog_family_id", "", family) ||
        !stringMember(members, "manifest_id", "", manifestId) ||
        !unsignedMember(members, "chunk_index", 0u, headerChunkIndex) ||
        !unsignedMember(members, "record_count", 0u, headerRecordCount) ||
        !unsignedMember(members, "record_size_bytes", 0u, headerRecordBytes) ||
        !unsignedMember(members, "first_tile", headerFirstTile, headerFirstTile) ||
        !unsignedMember(members, "tile_count", headerTileCount, headerTileCount))
    {
        return chunkFailure("a chunk header member has the wrong type");
    }
    if (family != manifest.catalogFamilyId || manifestId != manifest.manifestId)
    {
        return chunkFailure("the chunk header names another catalog");
    }
    if (headerChunkIndex != chunkIndex || headerRecordCount != recordCount || headerRecordBytes != kXpChunkRecordBytes)
    {
        return chunkFailure("the chunk header layout disagrees with the manifest");
    }
    if (headerFirstTile != static_cast<std::uint64_t>((std::max)(descriptor.firstTile, 0)) ||
        headerTileCount != static_cast<std::uint64_t>((std::max)(descriptor.tileCount, 0)))
    {
        return chunkFailure("the chunk header tile range disagrees with the manifest");
    }

    SpectrumChunk chunk;
    chunk.chunkIndex = static_cast<std::int32_t>(chunkIndex);
    chunk.grid = manifest.grid;
    chunk.records.reserve(recordCount);
    const std::size_t sampleCount = manifest.grid.sampleCount;
    chunk.flux.resize(static_cast<std::size_t>(recordCount) * sampleCount);
    chunk.fluxError.resize(static_cast<std::size_t>(recordCount) * sampleCount);
    std::size_t offset = kXpChunkPreambleBytes + headerBytes;
    for (std::uint32_t recordIndex = 0u; recordIndex < recordCount; ++recordIndex, offset += kXpChunkRecordBytes)
    {
        if ((recordIndex % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const auto recordFailure = [&](const char* const message) {
            return Status::failure(
                ErrorCode::InvalidData,
                StatusDetail{.message = message,
                             .operation = kChunkOperation,
                             .domain = ErrorDomain::Algorithms,
                             .context = {StatusContextField::ofText(ContextKey::Path, descriptor.file),
                                         StatusContextField::ofCount(ContextKey::Completed, recordIndex)}});
        };
        SpectrumRecord record;
        record.sourceIdentifier = readUint64LittleEndian(bytes, offset);
        record.coordinate = {.rightAscensionDegrees = readFloat64LittleEndian(bytes, offset + 8u),
                             .declinationDegrees = readFloat64LittleEndian(bytes, offset + 16u)};
        record.properMotionRightAscensionMasPerYear = optionalOf(readFloat32LittleEndian(bytes, offset + 24u));
        record.properMotionDeclinationMasPerYear = optionalOf(readFloat32LittleEndian(bytes, offset + 28u));
        const float referenceEpoch = readFloat32LittleEndian(bytes, offset + 32u);
        record.referenceEpochYear = std::isfinite(referenceEpoch) ? static_cast<double>(referenceEpoch) : 2016.0;
        record.magnitudeG = optionalOf(readFloat32LittleEndian(bytes, offset + 36u));
        record.magnitudeBp = optionalOf(readFloat32LittleEndian(bytes, offset + 40u));
        record.magnitudeRp = optionalOf(readFloat32LittleEndian(bytes, offset + 44u));
        const float fluxScale = readFloat32LittleEndian(bytes, offset + 48u);
        const float errorScale = readFloat32LittleEndian(bytes, offset + 52u);
        record.qualityFlags = readUint16LittleEndian(bytes, offset + 56u);
        const std::uint16_t validSamples = readUint16LittleEndian(bytes, offset + 58u);
        const auto kindValue = static_cast<std::uint8_t>(bytes[offset + 60u]);
        record.chunkIndex = chunk.chunkIndex;
        record.recordIndex = recordIndex;

        if (record.sourceIdentifier == 0u)
        {
            return recordFailure("a record carries no source identifier");
        }
        if (!isSkyCoordinate(record.coordinate))
        {
            return recordFailure("a record's position is not a direction on the sky");
        }
        if (validSamples != sampleCount)
        {
            return recordFailure("a record's sample count is not the grid's");
        }
        if (!std::isfinite(fluxScale) || !std::isfinite(errorScale) || fluxScale <= 0.0f || errorScale <= 0.0f)
        {
            return recordFailure("a record's quantization scales are not finite and positive");
        }
        if (!isSpectrumSourceKind(static_cast<SpectrumSourceKind>(kindValue)) ||
            static_cast<SpectrumSourceKind>(kindValue) != manifest.sourceKind)
        {
            return recordFailure("a record's spectrum source is not the manifest's");
        }
        record.sourceKind = manifest.sourceKind;

        float* const flux = chunk.flux.data() + static_cast<std::size_t>(recordIndex) * sampleCount;
        float* const fluxError = chunk.fluxError.data() + static_cast<std::size_t>(recordIndex) * sampleCount;
        std::size_t sampleOffset = offset + kXpChunkRecordHeaderBytes;
        for (std::size_t sample = 0u; sample < sampleCount; ++sample, sampleOffset += 2u)
        {
            flux[sample] = static_cast<float>(readInt16LittleEndian(bytes, sampleOffset)) * fluxScale;
            if (!std::isfinite(flux[sample]))
            {
                return recordFailure("a record carries a non-finite flux");
            }
        }
        for (std::size_t sample = 0u; sample < sampleCount; ++sample, sampleOffset += 2u)
        {
            fluxError[sample] = static_cast<float>(readUint16LittleEndian(bytes, sampleOffset)) * errorScale;
            if (!std::isfinite(fluxError[sample]) || fluxError[sample] < 0.0f)
            {
                return recordFailure("a record carries an invalid flux error");
            }
        }
        chunk.records.push_back(record);
    }
    return chunk;
}

SpectrumCatalog::SpectrumCatalog() noexcept = default;
SpectrumCatalog::SpectrumCatalog(const SpectrumCatalog& other) noexcept = default;
SpectrumCatalog::SpectrumCatalog(SpectrumCatalog&& other) noexcept = default;
SpectrumCatalog& SpectrumCatalog::operator=(const SpectrumCatalog& other) noexcept = default;
SpectrumCatalog& SpectrumCatalog::operator=(SpectrumCatalog&& other) noexcept = default;
SpectrumCatalog::~SpectrumCatalog() = default;

SpectrumCatalog::SpectrumCatalog(std::shared_ptr<const State> state) noexcept : m_state(std::move(state)) {}

bool SpectrumCatalog::valid() const noexcept
{
    return m_state != nullptr;
}

std::uint32_t SpectrumCatalog::recordCount() const noexcept
{
    return m_state == nullptr ? 0u : static_cast<std::uint32_t>(m_state->records.size());
}

std::span<const SpectrumRecord> SpectrumCatalog::records() const noexcept
{
    return m_state == nullptr ? std::span<const SpectrumRecord>{} : std::span<const SpectrumRecord>{m_state->records};
}

const SpectrumRecord* SpectrumCatalog::record(const std::uint32_t index) const noexcept
{
    return m_state == nullptr || index >= m_state->records.size() ? nullptr : &m_state->records[index];
}

std::span<const float> SpectrumCatalog::flux(const std::uint32_t index) const noexcept
{
    if (m_state == nullptr || index >= m_state->records.size())
    {
        return {};
    }
    const std::size_t count = m_state->grid.sampleCount;
    return std::span<const float>{m_state->flux.data() + static_cast<std::size_t>(index) * count, count};
}

std::span<const float> SpectrumCatalog::fluxError(const std::uint32_t index) const noexcept
{
    if (m_state == nullptr || index >= m_state->records.size())
    {
        return {};
    }
    const std::size_t count = m_state->grid.sampleCount;
    return std::span<const float>{m_state->fluxError.data() + static_cast<std::size_t>(index) * count, count};
}

const SpectralGrid& SpectrumCatalog::grid() const noexcept
{
    return m_state == nullptr ? kXpSampledGrid : m_state->grid;
}

const SpectrumCatalogManifest& SpectrumCatalog::manifest() const noexcept
{
    static const SpectrumCatalogManifest kEmptyManifest{};
    return m_state == nullptr ? kEmptyManifest : m_state->manifest;
}

std::span<const std::int32_t> SpectrumCatalog::loadedChunkIndices() const noexcept
{
    return m_state == nullptr ? std::span<const std::int32_t>{} : std::span<const std::int32_t>{m_state->loadedChunks};
}

std::optional<std::uint32_t> SpectrumCatalog::findSourceIdentifier(const std::uint64_t sourceIdentifier) const noexcept
{
    if (m_state == nullptr || sourceIdentifier == 0u)
    {
        return std::nullopt;
    }
    const auto& records = m_state->records;
    const auto at = std::lower_bound(m_state->byIdentifier.begin(), m_state->byIdentifier.end(), sourceIdentifier,
                                     [&records](const std::uint32_t index, const std::uint64_t value) {
                                         return records[index].sourceIdentifier < value;
                                     });
    if (at == m_state->byIdentifier.end() || records[*at].sourceIdentifier != sourceIdentifier)
    {
        return std::nullopt;
    }
    return *at;
}

Result<SpectrumCatalog> buildSpectrumCatalog(const ExecutionContext& execution, std::vector<SpectrumChunk> chunks,
                                             const SpectrumCatalogManifest& manifest)
try
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a catalog build needs an execution context", kBuildOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }

    const auto memory = std::make_shared<CatalogMemory>(execution);
    std::uint64_t total = 0u;
    // The count constructor may throw. MSVC's allocator-only constructor is noexcept even though
    // iterator-debug builds allocate a proxy, so a budget refusal there would terminate the process.
    std::pmr::vector<std::int32_t> loaded(0u, memory.get());
    loaded.reserve(chunks.size());
    for (const SpectrumChunk& chunk : chunks)
    {
        if (chunk.chunkIndex < 0 || static_cast<std::size_t>(chunk.chunkIndex) >= manifest.chunks.size())
        {
            return catalogFailure(ErrorCode::OutOfRange, "a chunk is not one the manifest lists", kBuildOperation);
        }
        if (std::find(loaded.begin(), loaded.end(), chunk.chunkIndex) != loaded.end())
        {
            return catalogFailure(ErrorCode::InvalidData, "a chunk was supplied twice", kBuildOperation);
        }
        if (chunk.grid != manifest.grid)
        {
            return catalogFailure(ErrorCode::InvalidData, "a chunk's grid is not the manifest's", kBuildOperation);
        }
        const std::size_t expected = chunk.records.size() * static_cast<std::size_t>(manifest.grid.sampleCount);
        if (chunk.flux.size() != expected || chunk.fluxError.size() != expected)
        {
            return catalogFailure(ErrorCode::InvalidData, "a chunk's sample tables are not the size its records need",
                                  kBuildOperation);
        }
        loaded.push_back(chunk.chunkIndex);
        total += chunk.records.size();
    }
    if (const Status ceiling = checkRecordCeiling(total, kMaxSpectrumCatalogRecords, kBuildOperation); ceiling.failed())
    {
        return ceiling;
    }
    std::sort(loaded.begin(), loaded.end());

    auto state =
        std::allocate_shared<SpectrumCatalog::State>(CatalogAllocator<SpectrumCatalog::State>(memory), memory.get());
    state->manifest = manifest;
    state->grid = manifest.grid;
    state->loadedChunks = std::move(loaded);
    state->records.reserve(static_cast<std::size_t>(total));
    state->flux.reserve(static_cast<std::size_t>(total) * manifest.grid.sampleCount);
    state->fluxError.reserve(static_cast<std::size_t>(total) * manifest.grid.sampleCount);
    for (SpectrumChunk& chunk : chunks)
    {
        for (std::size_t index = 0u; index < chunk.records.size(); ++index)
        {
            if ((index % kCancellationStride) == 0u)
            {
                if (const Status stopped = execution.checkCancellation(); stopped.failed())
                {
                    return stopped;
                }
            }
            const SpectrumRecord& record = chunk.records[index];
            if (record.sourceIdentifier == 0u || !isSkyCoordinate(record.coordinate))
            {
                return Status::failure(ErrorCode::InvalidData,
                                       StatusDetail{.message = "a record has no identifier or is not a direction",
                                                    .operation = kBuildOperation,
                                                    .domain = ErrorDomain::Algorithms,
                                                    .context = {StatusContextField::ofCount(ContextKey::Completed,
                                                                                            state->records.size())}});
            }
            state->records.push_back(record);
        }
        state->flux.insert(state->flux.end(), chunk.flux.begin(), chunk.flux.end());
        state->fluxError.insert(state->fluxError.end(), chunk.fluxError.begin(), chunk.fluxError.end());
        // clear() retains the capacity; release each consumed input chunk before copying the next.
        chunk = {};
    }

    const std::uint32_t count = static_cast<std::uint32_t>(state->records.size());
    state->byIdentifier.resize(count);
    state->byDeclination.resize(count);
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        state->byIdentifier[index] = index;
        state->byDeclination[index] = index;
    }
    const auto& records = state->records;
    std::sort(state->byIdentifier.begin(), state->byIdentifier.end(),
              [&records](const std::uint32_t left, const std::uint32_t right) {
                  return records[left].sourceIdentifier < records[right].sourceIdentifier ||
                         (records[left].sourceIdentifier == records[right].sourceIdentifier && left < right);
              });
    for (std::uint32_t position = 1u; position < count; ++position)
    {
        if (records[state->byIdentifier[position - 1u]].sourceIdentifier ==
            records[state->byIdentifier[position]].sourceIdentifier)
        {
            return catalogFailure(ErrorCode::InvalidData, "two records carry the same source identifier",
                                  kBuildOperation);
        }
    }
    std::sort(state->byDeclination.begin(), state->byDeclination.end(),
              [&records](const std::uint32_t left, const std::uint32_t right) {
                  const double a = records[left].coordinate.declinationDegrees;
                  const double b = records[right].coordinate.declinationDegrees;
                  return a < b || (a == b && left < right);
              });
    state->sortedDeclinations.resize(count);
    for (std::uint32_t position = 0u; position < count; ++position)
    {
        state->sortedDeclinations[position] = records[state->byDeclination[position]].coordinate.declinationDegrees;
    }
    return SpectrumCatalog{std::shared_ptr<const SpectrumCatalog::State>(std::move(state))};
}
catch (const CatalogAllocationFailure& refused)
{
    return refused.status;
}
catch (const std::bad_alloc&)
{
    return Status::failure(ErrorCode::AllocationFailed);
}

Result<CatalogQueryResult> querySpectrumCatalogRegion(const ExecutionContext& execution, const SpectrumCatalog& catalog,
                                                      const CatalogRegionQuery& query)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a region query needs an execution context", kQueryOperation);
    }
    if (!catalog.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a region query needs a built catalog", kQueryOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!isSkyCoordinate(query.center))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the cone's centre is a direction on the sky",
                              kQueryOperation);
    }
    if (!std::isfinite(query.radiusDegrees) || query.radiusDegrees <= 0.0)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the cone's radius is finite and positive", kQueryOperation);
    }
    if (query.magnitudeLimit.has_value() && !std::isfinite(*query.magnitudeLimit))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the magnitude limit is finite", kQueryOperation);
    }
    if (query.maximumResults == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the result bound is at least one", kQueryOperation);
    }

    const SpectrumCatalog::State& state = *catalog.m_state;
    const auto& records = state.records;
    CatalogQueryResult result{};
    const double lowest = query.center.declinationDegrees - query.radiusDegrees;
    const double highest = query.center.declinationDegrees + query.radiusDegrees;
    const auto begin = std::lower_bound(state.sortedDeclinations.begin(), state.sortedDeclinations.end(), lowest);
    const auto end = std::upper_bound(state.sortedDeclinations.begin(), state.sortedDeclinations.end(), highest);
    std::vector<std::uint32_t> admitted;
    std::size_t visited = 0u;
    for (auto at = begin; at != end; ++at, ++visited)
    {
        if ((visited % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const std::uint32_t index =
            state.byDeclination[static_cast<std::size_t>(at - state.sortedDeclinations.begin())];
        const SpectrumRecord& record = records[index];
        ++result.examinedCount;
        if (!magnitudeAdmits(record, query))
        {
            continue;
        }
        if (angularSeparationDegrees(query.center, record.coordinate) <= query.radiusDegrees)
        {
            admitted.push_back(index);
        }
    }
    std::sort(admitted.begin(), admitted.end(), [&records](const std::uint32_t left, const std::uint32_t right) {
        return brighterRecord(records[left], records[right]);
    });
    result.admittedCount = static_cast<std::uint32_t>(admitted.size());
    if (admitted.size() > query.maximumResults)
    {
        admitted.resize(query.maximumResults);
        result.truncated = true;
    }
    result.entryIndices = std::move(admitted);
    return result;
}

Result<SpectrumChunkSelection> selectSpectrumChunksForRegion(const ExecutionContext& execution,
                                                             const SpectrumCatalogManifest& manifest,
                                                             const SkyCoordinate& center, const double radiusDegrees)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a chunk selection needs an execution context",
                              kRegionChunksOperation);
    }
    if (!isSkyCoordinate(center))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the cone's centre is a direction on the sky",
                              kRegionChunksOperation);
    }
    if (!std::isfinite(radiusDegrees) || radiusDegrees <= 0.0)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the cone's radius is finite and positive",
                              kRegionChunksOperation);
    }
    SpectrumChunkSelection selection{};
    if (manifest.chunks.empty())
    {
        return selection;
    }
    if (isUndividedManifest(manifest))
    {
        selection.chunkIndices.push_back(0u);
        selection.tilesExamined = 1u;
        return selection;
    }
    const HealpixGrid grid{.order = manifest.healpixOrder, .ordering = HealpixOrdering::Nested};
    const Result<HealpixDiscResult> disc =
        queryHealpixDisc(execution, grid,
                         HealpixDiscQuery{.center = center,
                                          .radiusDegrees = radiusDegrees,
                                          .coverage = HealpixDiscCoverage::ConservativeOverlap,
                                          .maximumPixels = tileCountForOrder(manifest.healpixOrder)});
    if (!disc.hasValue())
    {
        return disc.status();
    }
    for (const HealpixPixelIndex& pixel : disc->pixels)
    {
        ++selection.tilesExamined;
        const std::optional<std::uint32_t> chunk = chunkForTile(manifest, pixel.value);
        if (!chunk.has_value())
        {
            ++selection.undeclaredTiles;
            continue;
        }
        if (std::find(selection.chunkIndices.begin(), selection.chunkIndices.end(), *chunk) ==
            selection.chunkIndices.end())
        {
            selection.chunkIndices.push_back(*chunk);
        }
    }
    std::sort(selection.chunkIndices.begin(), selection.chunkIndices.end());
    return selection;
}

Result<std::vector<std::uint32_t>>
selectSpectrumChunksForSources(const ExecutionContext& execution, const SpectrumCatalogManifest& manifest,
                               const std::span<const std::uint64_t> sourceIdentifiers)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a chunk selection needs an execution context",
                              kSourceChunksOperation);
    }
    std::vector<std::uint32_t> chunks;
    if (manifest.chunks.empty())
    {
        return chunks;
    }
    if (isUndividedManifest(manifest))
    {
        chunks.push_back(0u);
        return chunks;
    }
    const std::uint64_t orderTwelveTiles = tileCountForOrder(kGaiaSourceIdentifierHealpixOrder);
    const std::uint64_t divisor = std::uint64_t{1}
                                  << (2u * (kGaiaSourceIdentifierHealpixOrder - manifest.healpixOrder));
    for (std::size_t index = 0u; index < sourceIdentifiers.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const std::uint64_t identifier = sourceIdentifiers[index];
        if (identifier == 0u)
        {
            continue;
        }
        const std::uint64_t tileTwelve = identifier / kGaiaSourceIdentifierTileDivisor;
        if (tileTwelve >= orderTwelveTiles)
        {
            continue;
        }
        const std::optional<std::uint32_t> chunk =
            chunkForTile(manifest, static_cast<std::int64_t>(tileTwelve / divisor));
        if (chunk.has_value() && std::find(chunks.begin(), chunks.end(), *chunk) == chunks.end())
        {
            chunks.push_back(*chunk);
        }
    }
    std::sort(chunks.begin(), chunks.end());
    return chunks;
}

SpectrumCoverageSummary summarizeSpectrumCatalogCoverage(const SpectrumCatalogManifest& manifest) noexcept
{
    SpectrumCoverageSummary summary{};
    summary.declaredChunks = static_cast<std::uint32_t>(manifest.chunks.size());
    summary.expectedParentChunks = tileCountForOrder(manifest.chunkParentHealpixOrder);
    summary.expectedTiles = tileCountForOrder(manifest.healpixOrder);
    for (const SpectrumChunkDescriptor& chunk : manifest.chunks)
    {
        if (chunk.tileCount > 0)
        {
            summary.declaredTiles += static_cast<std::uint32_t>(chunk.tileCount);
        }
    }
    summary.fullSkyCoverage = summary.expectedTiles > 0u && summary.declaredTiles >= summary.expectedTiles;
    return summary;
}

SpectrumCatalogReadiness makeSpectrumCatalogReadiness(const SpectrumCatalogManifest& manifest, const bool checksumValid,
                                                      const bool regionAvailable, const bool stale)
{
    SpectrumCatalogReadiness readiness;
    readiness.accessProfile = manifest.accessProfile;
    readiness.catalogFamilyId = manifest.catalogFamilyId;
    readiness.manifestId = manifest.manifestId;
    readiness.rightsReviewStatus = manifest.rightsReviewStatus;
    readiness.checksumValid = checksumValid;
    readiness.stale = stale;
    readiness.regionAvailable = regionAvailable;
    readiness.rightsReviewApproved = manifest.rightsReviewStatus == "approved-for-managed-download" ||
                                     manifest.rightsReviewStatus == "approved-for-bundling";
    return readiness;
}

SpectrumCatalogReadinessError checkSpectrumCatalogReadiness(const SpectrumCatalogReadiness& readiness) noexcept
{
    if (!readiness.accessProfile.has_value() ||
        (*readiness.accessProfile != SpectrumCatalogAccessProfile::LocalFullInstall &&
         *readiness.accessProfile != SpectrumCatalogAccessProfile::InstalledRegionalCache))
    {
        return SpectrumCatalogReadinessError::AccessProfileNotLocal;
    }
    if (readiness.catalogFamilyId.empty() || readiness.manifestId.empty())
    {
        return SpectrumCatalogReadinessError::MissingIdentity;
    }
    if (!readiness.checksumValid || readiness.stale)
    {
        return SpectrumCatalogReadinessError::ChecksumInvalidOrStale;
    }
    if (!readiness.rightsReviewApproved)
    {
        return SpectrumCatalogReadinessError::RightsReviewNotApproved;
    }
    if (!readiness.regionAvailable)
    {
        return SpectrumCatalogReadinessError::RegionNotCovered;
    }
    return SpectrumCatalogReadinessError::None;
}

Result<MatchedSourceResolution> resolveMatchedSources(const ExecutionContext& execution,
                                                      const std::span<const MatchedSourceReference> references,
                                                      const SpectrumCatalog& catalog,
                                                      const MatchedSourceRequirements& requirements)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a match resolution needs an execution context",
                              kResolveOperation);
    }
    if (!catalog.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "a match resolution needs a built catalog", kResolveOperation);
    }
    if (const Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if ((requirements.maximumResidualPixels.has_value() && !std::isfinite(*requirements.maximumResidualPixels)) ||
        (requirements.maximumSeparationArcseconds.has_value() &&
         !std::isfinite(*requirements.maximumSeparationArcseconds)))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a residual or separation bound is finite",
                              kResolveOperation);
    }

    const SpectrumCatalogManifest& manifest = catalog.manifest();
    const std::string_view requiredFamily = requirements.requiredSpectrumCatalogFamilyId.empty()
                                                ? std::string_view{manifest.catalogFamilyId}
                                                : std::string_view{requirements.requiredSpectrumCatalogFamilyId};
    const std::string_view requiredManifest = requirements.requiredSpectrumManifestId.empty()
                                                  ? std::string_view{manifest.manifestId}
                                                  : std::string_view{requirements.requiredSpectrumManifestId};
    const bool regional = manifest.accessProfile == SpectrumCatalogAccessProfile::InstalledRegionalCache;

    MatchedSourceResolution resolution{};
    resolution.lookups.reserve(references.size());
    const auto mismatch = [](const std::string_view required, const std::string& actual) {
        return !required.empty() && actual != required;
    };
    const auto exceeds = [](const std::optional<double>& bound, const std::optional<double>& value) {
        return bound.has_value() && (!value.has_value() || !std::isfinite(*value) || *value > *bound);
    };
    for (std::size_t index = 0u; index < references.size(); ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
        }
        const MatchedSourceReference& reference = references[index];
        MatchedSourceLookup lookup{};
        const auto refuse = [&](const MatchedSourceLookupStatus status, std::uint32_t& counter) {
            lookup.status = status;
            ++counter;
            ++resolution.rejectedCount;
        };
        if (reference.sourceIdentifier == 0u)
        {
            refuse(MatchedSourceLookupStatus::InvalidSourceIdentifier, resolution.invalidSourceIdentifierCount);
        }
        else if (const std::optional<std::uint32_t> found = catalog.findSourceIdentifier(reference.sourceIdentifier);
                 !found.has_value())
        {
            if (regional)
            {
                refuse(MatchedSourceLookupStatus::MissingChunkOrCache, resolution.cacheMissCount);
            }
            else
            {
                refuse(MatchedSourceLookupStatus::MissingSource, resolution.missingSourceCount);
            }
        }
        else if (mismatch(requiredFamily, reference.spectrumCatalogFamilyId) ||
                 mismatch(requiredManifest, reference.spectrumCatalogManifestId))
        {
            refuse(MatchedSourceLookupStatus::ManifestMismatch, resolution.manifestMismatchCount);
        }
        else if (mismatch(requirements.requiredAstrometricCatalogFamilyId, reference.astrometricCatalogFamilyId) ||
                 mismatch(requirements.requiredAstrometricCatalogManifestId, reference.astrometricCatalogManifestId) ||
                 mismatch(requirements.requiredObservationEpoch, reference.observationEpoch) ||
                 mismatch(requirements.requiredProperMotionModelVersion, reference.properMotionModelVersion) ||
                 mismatch(requirements.requiredWcsSolutionId, reference.wcsSolutionId) ||
                 exceeds(requirements.maximumResidualPixels, reference.residualPixels) ||
                 exceeds(requirements.maximumSeparationArcseconds, reference.separationArcseconds))
        {
            refuse(MatchedSourceLookupStatus::StaleMatch, resolution.staleMatchCount);
        }
        else if (requirements.rejectNonZeroQualityFlags && catalog.record(*found)->qualityFlags != 0u)
        {
            refuse(MatchedSourceLookupStatus::RejectedQualityFlags, resolution.qualityRejectedCount);
        }
        else
        {
            lookup.status = MatchedSourceLookupStatus::Satisfied;
            lookup.recordIndex = found;
            ++resolution.acceptedCount;
        }
        resolution.lookups.push_back(lookup);
    }
    return resolution;
}

} // namespace an
