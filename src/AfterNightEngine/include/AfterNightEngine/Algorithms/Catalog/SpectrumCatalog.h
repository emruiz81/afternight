// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sampled-spectrum catalog: its manifest, its chunk container, the owned
/// table of spectra, and the joins a spectrophotometric calibration makes against it.
///
/// **The engine parses bytes it is handed and opens no file.** `parseSpectrumCatalogManifest` reads
/// the manifest text, `decodeSpectrumChunk` reads one chunk container's bytes, and
/// `buildSpectrumCatalog` owns what was decoded. Who read the bytes, from where, and whether a chunk
/// was fetched into a regional cache first are the host's decisions, as they are for the Gaia
/// provider next door.
///
/// **The format is stated once, as constants, and the manifest is checked against them.** The
/// wavelength grid, the record size, the HEALPix orders and the chunk versions are facts of the
/// `.anxp` format; a manifest that disagrees with them is refused by name rather than validated
/// against a copy of itself.
///
/// **A spectrum is a span into one table, not a vector per star.** Three hundred and forty-three
/// flux samples and their errors per record are the bulk of a catalog's memory; holding them
/// contiguous is what lets a region's spectra be handed to an integration without a copy.
///
/// **It is a catalog provider, so it lives beside the other one.** It sits under `Catalog` rather
/// than beside the spectrophotometry it feeds because it is a provider of sky records with the same
/// joins, the same tile arithmetic and the same bounded manifest scan as the Gaia provider, and a
/// domain may not reach another domain's private helpers to share them.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <cstddef>
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

/// How many wavelength samples one sampled Gaia XP spectrum carries.
inline constexpr std::uint32_t kXpSampledWavelengthCount = 343u;

/// The first sampled wavelength, in nanometres.
inline constexpr double kXpSampledWavelengthStartNm = 336.0;

/// The sampling step, in nanometres.
inline constexpr double kXpSampledWavelengthStepNm = 2.0;

/// The last sampled wavelength, in nanometres.
inline constexpr double kXpSampledWavelengthEndNm = 1020.0;

/// How many bytes one record of the chunk container occupies.
inline constexpr std::uint32_t kXpChunkRecordBytes = 1536u;

/// How many bytes of each record precede its quantized samples.
inline constexpr std::uint32_t kXpChunkRecordHeaderBytes = 62u;

/// How many bytes the fixed preamble of a chunk container occupies.
inline constexpr std::uint32_t kXpChunkPreambleBytes = 16u;

/// The four bytes a chunk container begins with.
inline constexpr const char* kXpChunkMagic = "ANXP";

/// The container format version this build reads.
inline constexpr std::uint16_t kXpChunkFormatVersion = 1u;

/// The record layout version this build reads.
inline constexpr std::uint16_t kXpChunkRecordVersion = 1u;

/// The HEALPix order the catalog's tiles are addressed at, in the nested scheme.
inline constexpr std::uint8_t kXpCatalogHealpixOrder = 6u;

/// The HEALPix order one chunk covers: the parent tile whose children a chunk holds.
inline constexpr std::uint8_t kXpChunkParentHealpixOrder = 3u;

/// The HEALPix order encoded in the top bits of a Gaia source identifier.
inline constexpr std::uint8_t kGaiaSourceIdentifierHealpixOrder = 12u;

/// The divisor that turns a Gaia source identifier into its order-twelve nested tile.
inline constexpr std::uint64_t kGaiaSourceIdentifierTileDivisor = std::uint64_t{1} << 35u;

/// The catalog family every manifest this build reads must name.
inline constexpr const char* kXpCatalogFamilyId = "spcc_gaia_dr3_xp_sampled";

/// The manifest schema this build reads.
inline constexpr const char* kXpCatalogManifestSchema = "spcc-catalog-meta-v1";

/// The most bytes of manifest text one parse reads.
inline constexpr std::uint32_t kDefaultMaxSpectrumManifestBytes = 1u * 1024u * 1024u;

/// The most bytes of a chunk container's own header text one decode reads.
inline constexpr std::uint32_t kDefaultMaxSpectrumChunkHeaderBytes = 1u * 1024u * 1024u;

/// The most records one owned spectrum catalog holds.
///
/// Two million records is a little under three gigabytes of spectra. The published table is
/// seventeen times that; nothing loads it whole, and a request that would is refused with the count.
inline constexpr std::uint32_t kMaxSpectrumCatalogRecords = 2000000u;

/// The regular wavelength grid a set of spectra is sampled on.
struct SpectralGrid
{
    /// How many samples.
    std::uint32_t sampleCount = kXpSampledWavelengthCount;

    /// The first wavelength, in nanometres.
    double startNm = kXpSampledWavelengthStartNm;

    /// The step between samples, in nanometres.
    double stepNm = kXpSampledWavelengthStepNm;

    /// The last wavelength, in nanometres.
    double endNm = kXpSampledWavelengthEndNm;

    /// Whether two grids are the same four numbers.
    ///
    /// @param left one grid.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend constexpr bool operator==(const SpectralGrid& left,
                                                   const SpectralGrid& right) noexcept = default;
};

/// The grid every sampled Gaia XP spectrum is on.
inline constexpr SpectralGrid kXpSampledGrid{};

/// Whether a grid is the sampled Gaia XP grid this build's chunk format carries.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param grid the grid to inspect.
/// @return true when every member equals the format's.
[[nodiscard]] constexpr bool isXpSampledGrid(const SpectralGrid& grid) noexcept
{
    return grid == kXpSampledGrid;
}

/// The wavelength of one sample of a grid.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param grid the grid.
/// @param index the sample, below the grid's count for a meaningful answer.
/// @return `startNm + index * stepNm`.
[[nodiscard]] constexpr double spectralGridWavelengthNm(const SpectralGrid& grid, const std::uint32_t index) noexcept
{
    return grid.startNm + static_cast<double>(index) * grid.stepNm;
}

/// Where a spectrum came from.
enum class SpectrumSourceKind : std::uint8_t
{
    /// The Gaia DR3 sampled mean spectra, which is the production source.
    GaiaDr3XpSampled = 0u,

    /// Spectra reconstructed from the Gaia DR3 continuous representation. Carried so a chunk that
    /// says so is not mistaken for the production source; no production catalog is built from it.
    GaiaDr3XpContinuousExperimental = 1u,

    /// A spectrum a test wrote.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    SyntheticFixture = 2u
};

/// Whether a value names a source this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param kind the value to inspect.
/// @return true for one of the three declared values.
[[nodiscard]] constexpr bool isSpectrumSourceKind(SpectrumSourceKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind) <= static_cast<std::uint8_t>(SpectrumSourceKind::SyntheticFixture);
}

/// The kind's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param kind the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSpectrumSourceKind"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* spectrumSourceKindName(SpectrumSourceKind kind) noexcept;

/// How a catalog was made available to the host that read it.
enum class SpectrumCatalogAccessProfile : std::uint8_t
{
    /// The whole catalog is installed locally.
    LocalFullInstall = 0u,

    /// A regional subset of chunks is installed locally; a source outside it is a cache miss rather
    /// than a missing star.
    InstalledRegionalCache = 1u,

    /// Chunks are fetched from a managed remote on demand. Named so a manifest can say it; no
    /// runtime path in this build fetches anything.
    ManagedRemote = 2u,

    /// The manifest says the catalog may not be used.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Blocked = 3u
};

/// Whether a value names an access profile this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param profile the value to inspect.
/// @return true for one of the four declared values.
[[nodiscard]] constexpr bool isSpectrumCatalogAccessProfile(SpectrumCatalogAccessProfile profile) noexcept
{
    return static_cast<std::uint8_t>(profile) <= static_cast<std::uint8_t>(SpectrumCatalogAccessProfile::Blocked);
}

/// The profile's own name, which is also the spelling a manifest uses.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param profile the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSpectrumCatalogAccessProfile"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
spectrumCatalogAccessProfileName(SpectrumCatalogAccessProfile profile) noexcept;

/// One chunk as the manifest describes it.
struct SpectrumChunkDescriptor
{
    /// The chunk's position in the manifest's list, which is also the index its records carry.
    std::int32_t chunkIndex = 0;

    /// The chunk's file, relative to the manifest, as the manifest spells it. Checked to be a
    /// non-empty relative path with no `..` segment, and distinct from every other chunk's; never
    /// opened here.
    std::string file{};

    /// The first order-six nested tile this chunk holds.
    std::int32_t firstTile = 0;

    /// How many consecutive tiles it holds. Zero only for the single chunk of an undivided
    /// catalog, which holds the whole sky.
    std::int32_t tileCount = 0;

    /// How many records it holds.
    std::uint64_t recordCount = 0u;

    /// How many bytes the file is, or zero when the manifest did not say.
    std::uint64_t sizeBytes = 0u;

    /// The file's SHA-256, as hexadecimal text, or empty when the manifest did not say. Carried,
    /// never verified here: the engine does not read the file.
    std::string sha256{};
};

/// Everything a manifest states about a catalog.
struct SpectrumCatalogManifest
{
    /// The schema the text was written to. Always `kXpCatalogManifestSchema` after a parse.
    std::string schemaVersion{};

    /// The catalog family. Always `kXpCatalogFamilyId` after a parse.
    std::string catalogFamilyId{};

    /// The immutable identity of this build of the catalog. Never empty and never equal to the
    /// family after a parse.
    std::string manifestId{};

    /// How the catalog reached the host.
    SpectrumCatalogAccessProfile accessProfile = SpectrumCatalogAccessProfile::LocalFullInstall;

    /// Where the spectra came from. `GaiaDr3XpSampled` when the manifest names none.
    SpectrumSourceKind sourceKind = SpectrumSourceKind::GaiaDr3XpSampled;

    /// The payload kind the manifest names, as text. Carried for identity; not switched on.
    std::string payloadKind{};

    /// How many records all the chunks hold together; a parse requires it to equal the sum of the
    /// chunks' counts.
    std::uint64_t totalRecords = 0u;

    /// The bytes per record. Always `kXpChunkRecordBytes` after a parse.
    std::uint32_t recordSizeBytes = kXpChunkRecordBytes;

    /// The tile order. Always `kXpCatalogHealpixOrder` after a parse.
    std::uint8_t healpixOrder = kXpCatalogHealpixOrder;

    /// The chunk order. Always `kXpChunkParentHealpixOrder` after a parse.
    std::uint8_t chunkParentHealpixOrder = kXpChunkParentHealpixOrder;

    /// The wavelength grid. Always the sampled XP grid after a parse.
    SpectralGrid grid{};

    /// The flux unit the manifest names, as text.
    std::string fluxUnit{};

    /// The rights review status the manifest names, as text.
    std::string rightsReviewStatus{};

    /// The chunk-manifest sidecar the manifest names, relative to itself, or empty. Never opened
    /// here, and carried as written: unlike a chunk path, a sidecar path is not checked to stay
    /// inside the manifest's directory.
    std::string chunkManifestFile{};

    /// The source-identifier index sidecar, or empty. Carried as written, like the one above.
    std::string sourceIdentifierIndexFile{};

    /// The coverage summary sidecar, or empty. Carried as written, like the one above.
    std::string coverageSummaryFile{};

    /// The chunks, in chunk-index order.
    std::vector<SpectrumChunkDescriptor> chunks{};
};

/// How much of a manifest one parse admits.
struct SpectrumManifestLimits
{
    /// The most bytes of text.
    std::uint32_t maximumBytes = kDefaultMaxSpectrumManifestBytes;

    /// The most chunks. The format has at most one chunk per order-three tile, which is 768.
    std::uint32_t maximumChunks = 768u;
};

/// Reads a catalog manifest from its text.
///
/// The text is one JSON object read by a bounded scan. Every format fact is checked against the
/// constants above: the schema, the family, the chunk magic and versions, the byte order, the record
/// size, the two orders and the grid. The manifest identity must be non-empty and differ from the
/// family. The chunk list must be non-empty and contiguous from zero, every file a contained
/// relative path named by one chunk only, every tile range inside the order-six sky, no two ranges
/// overlapping, and the stated total the sum of the chunks' counts; a list of one chunk with no
/// tile range is an undivided catalog. A manifest that names a chunk manifest, a source index or a
/// coverage summary carries those names forward for the host to open; nothing here does.
///
/// Runs on the calling thread, checks cancellation once before it begins, and allocates the
/// returned manifest and transient scan state.
///
/// @param execution the operation policy and engine-context lease.
/// @param text the manifest, which is only read and need not outlive the call.
/// @param limits how much text and how many chunks to admit.
/// @return the manifest; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::LimitExceeded` when the text exceeds its byte limit, or when the chunk array
///         cannot be scanned - too many chunks and a malformed array alike, with a requested count
///         of zero; `ErrorCode::InvalidData` for text that is not one object, a missing or mistyped
///         member, or a fact that disagrees with the format; `ErrorCode::Unsupported` for an access
///         profile - an absent one included - or a source kind this build does not read; or
///         cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrumCatalogManifest>
parseSpectrumCatalogManifest(const ExecutionContext& execution, std::string_view text,
                             const SpectrumManifestLimits& limits = {});

/// One star of the catalog, without its spectrum.
///
/// The spectrum is reached through the catalog that owns the record, as a span into one table. Every
/// unmeasured quantity is an absent optional; the container stores a not-a-number for it, and any
/// non-finite stored value is read as absent.
struct SpectrumRecord
{
    /// The Gaia source identifier. Never zero in a decoded record.
    std::uint64_t sourceIdentifier = 0u;

    /// Where the star is at `referenceEpochYear`.
    SkyCoordinate coordinate{};

    /// Proper motion in right ascension, in milliarcseconds per year, cosine of declination
    /// included, or absent.
    std::optional<double> properMotionRightAscensionMasPerYear{};

    /// Proper motion in declination, in milliarcseconds per year, or absent.
    std::optional<double> properMotionDeclinationMasPerYear{};

    /// The epoch the position is stated at, in Julian years; 2016.0 when the container stores a
    /// value that is not finite.
    double referenceEpochYear = 2016.0;

    /// The G magnitude, or absent.
    std::optional<double> magnitudeG{};

    /// The BP magnitude, or absent.
    std::optional<double> magnitudeBp{};

    /// The RP magnitude, or absent.
    std::optional<double> magnitudeRp{};

    /// The provider's quality flags, as written. Zero is clean.
    std::uint16_t qualityFlags = 0u;

    /// Where the spectrum came from; a decode requires it to be the manifest's.
    SpectrumSourceKind sourceKind = SpectrumSourceKind::GaiaDr3XpSampled;

    /// Which chunk the record was decoded from.
    std::int32_t chunkIndex = 0;

    /// The record's position inside that chunk.
    std::uint32_t recordIndex = 0u;
};

/// How much of a chunk one decode admits.
struct SpectrumChunkLimits
{
    /// The most bytes of the chunk's own header text.
    std::uint32_t maximumHeaderBytes = kDefaultMaxSpectrumChunkHeaderBytes;

    /// The most records.
    std::uint32_t maximumRecords = kMaxSpectrumCatalogRecords;
};

/// One decoded chunk: its records and their spectra, contiguous.
struct SpectrumChunk
{
    /// Which chunk of the manifest this is.
    std::int32_t chunkIndex = 0;

    /// The grid every spectrum here is on.
    SpectralGrid grid{};

    /// The records, in container order.
    std::vector<SpectrumRecord> records{};

    /// `records.size() * grid.sampleCount` flux samples, record-major, in the manifest's flux unit.
    std::vector<float> flux{};

    /// The same number of flux uncertainties, record-major, never negative.
    std::vector<float> fluxError{};
};

/// Decodes one chunk container.
///
/// The bytes are the format's own little-endian layout: a sixteen-byte preamble - the magic, the
/// format and record versions, the header length and the record count - then a JSON header whose
/// identity, chunk index, record count, record size and tile range must agree with the manifest,
/// and then the records, each `kXpChunkRecordBytes` long. The container's size must be exactly what
/// the preamble implies, and the manifest's when it states one. Each record's identifier must be
/// non-zero, its position a direction, its sample count the grid's, its two quantization scales
/// finite and positive, and its source kind the manifest's; each quantized sample is scaled back to
/// a finite flux and a non-negative error.
///
/// Runs on the calling thread, checks cancellation at bounded record boundaries, and allocates the
/// returned chunk and the transiently scanned header members.
///
/// @param execution the operation policy and engine-context lease.
/// @param bytes the whole container, which is only read and need not outlive the call.
/// @param manifest the manifest the chunk belongs to.
/// @param chunkIndex which of the manifest's chunks these bytes are.
/// @param limits how much header text and how many records to admit.
/// @return the chunk; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::OutOfRange` for a chunk index the manifest does not list;
///         `ErrorCode::LimitExceeded` above either limit, carrying the requested and permitted
///         values; `ErrorCode::InvalidData` for a container whose preamble, header, size or a record
///         disagrees with the format or the manifest, carrying the chunk's path and, for a record,
///         its position; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrumChunk> decodeSpectrumChunk(const ExecutionContext& execution,
                                                                             std::span<const std::byte> bytes,
                                                                             const SpectrumCatalogManifest& manifest,
                                                                             std::uint32_t chunkIndex,
                                                                             const SpectrumChunkLimits& limits = {});

/// An owned, immutable table of spectra with its lookup indices.
///
/// Record, spectral-sample and index storage is charged to the building execution's memory
/// resource. A retained context lease outlives every shared handle and keeps that resource alive.
///
/// Copies share immutable state, so handing one to another thread costs a reference count. A default
/// object holds nothing; every accessor stays safe on one and reports an empty catalog. Queries
/// answer with positions into `records()`, not with pointers.
class SpectrumCatalog final
{
public:
    /// Builds an empty catalog.
    AFTERNIGHTENGINE_API SpectrumCatalog() noexcept;

    /// Shares an immutable catalog.
    ///
    /// @param other the catalog to share.
    AFTERNIGHTENGINE_API SpectrumCatalog(const SpectrumCatalog& other) noexcept;

    /// Takes another catalog's state, leaving it empty.
    ///
    /// @param other the catalog to take from.
    AFTERNIGHTENGINE_API SpectrumCatalog(SpectrumCatalog&& other) noexcept;

    /// Shares another immutable catalog.
    ///
    /// @param other the catalog to share.
    /// @return this catalog.
    AFTERNIGHTENGINE_API SpectrumCatalog& operator=(const SpectrumCatalog& other) noexcept;

    /// Takes another catalog's state, leaving it empty.
    ///
    /// @param other the catalog to take from.
    /// @return this catalog.
    AFTERNIGHTENGINE_API SpectrumCatalog& operator=(SpectrumCatalog&& other) noexcept;

    /// Releases this handle; the shared table lives until its last handle goes.
    AFTERNIGHTENGINE_API ~SpectrumCatalog();

    /// Whether this handle came from a successful build.
    ///
    /// @return true when it owns a table.
    [[nodiscard]] AFTERNIGHTENGINE_API bool valid() const noexcept;

    /// How many records the catalog holds.
    ///
    /// @return the count, or zero for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t recordCount() const noexcept;

    /// The records, in the order the chunks were supplied and then container order within each.
    ///
    /// @return a span valid for the life of this handle, empty for an empty one.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const SpectrumRecord> records() const noexcept;

    /// One record by position.
    ///
    /// @param index a position below `recordCount()`.
    /// @return the record, or null when the index is out of range or the handle is empty.
    [[nodiscard]] AFTERNIGHTENGINE_API const SpectrumRecord* record(std::uint32_t index) const noexcept;

    /// One record's flux samples.
    ///
    /// @param index a position below `recordCount()`.
    /// @return `grid().sampleCount` samples, valid for the life of this handle; empty out of range.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const float> flux(std::uint32_t index) const noexcept;

    /// One record's flux uncertainties.
    ///
    /// @param index a position below `recordCount()`.
    /// @return `grid().sampleCount` values, valid for the life of this handle; empty out of range.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const float> fluxError(std::uint32_t index) const noexcept;

    /// The grid every spectrum is on.
    ///
    /// @return the grid; the sampled XP grid for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API const SpectralGrid& grid() const noexcept;

    /// The manifest the catalog was built under.
    ///
    /// @return the manifest, valid for the life of this handle; a default one for an empty handle.
    [[nodiscard]] AFTERNIGHTENGINE_API const SpectrumCatalogManifest& manifest() const noexcept;

    /// Which of the manifest's chunks this catalog holds, ascending.
    ///
    /// @return the chunk indices, valid for the life of this handle.
    [[nodiscard]] AFTERNIGHTENGINE_API std::span<const std::int32_t> loadedChunkIndices() const noexcept;

    /// The position of the record with a source identifier.
    ///
    /// A binary search over an identifier-sorted index, so a join of many identifiers costs a
    /// logarithm each rather than a table of its own.
    ///
    /// @param sourceIdentifier the identifier to find.
    /// @return its position, or absent when no record carries it, the identifier is zero or the
    ///         handle is empty.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::uint32_t>
    findSourceIdentifier(std::uint64_t sourceIdentifier) const noexcept;

private:
    struct State;

    /// Builds a public handle around checked immutable state.
    ///
    /// @param state the state to own.
    explicit SpectrumCatalog(std::shared_ptr<const State> state) noexcept;

    /// The shared immutable table and its indices.
    std::shared_ptr<const State> m_state{};

    friend AFTERNIGHTENGINE_API Result<SpectrumCatalog>
    buildSpectrumCatalog(const ExecutionContext&, std::vector<SpectrumChunk>, const SpectrumCatalogManifest&);
    friend AFTERNIGHTENGINE_API Result<CatalogQueryResult>
    querySpectrumCatalogRegion(const ExecutionContext&, const SpectrumCatalog&, const CatalogRegionQuery&);
};

/// Builds an owned spectrum catalog from decoded chunks.
///
/// The one door into a catalog. Every chunk must be one of the manifest's, no chunk may appear twice,
/// every chunk's grid must be the manifest's, every chunk's sample tables must be the size its
/// records need, and no source identifier may appear twice across the chunks. Records are checked
/// again for a direction and a non-zero identifier, so a caller that assembled chunks by hand meets
/// the same rules a decode does.
///
/// Runs on the calling thread and checks cancellation at bounded record boundaries. The table it
/// owns may outlive the execution, so it is caller-lifetime storage rather than operation scratch.
/// The table is reserved in full first; each chunk's records and samples are then copied into it
/// and the chunk is released, so the input's memory falls as the table fills.
///
/// @param execution the operation policy and engine-context lease.
/// @param chunks the chunks to own, moved into the catalog. May be empty, which builds an empty but
///        valid catalog whose queries answer nothing.
/// @param manifest the manifest the chunks belong to, copied into the catalog.
/// @return the catalog; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::LimitExceeded` above `kMaxSpectrumCatalogRecords`; `ErrorCode::OutOfRange`
///         for a chunk the manifest does not list; `ErrorCode::InvalidData` for a repeated chunk, a
///         foreign grid, a mis-sized table, a duplicate identifier or a record with no identifier or
///         no direction; the engine context memory's own refusal, which is
///         `ErrorCode::MemoryBudgetExceeded` for the budget; `ErrorCode::AllocationFailed` when the
///         platform refuses; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrumCatalog>
buildSpectrumCatalog(const ExecutionContext& execution, std::vector<SpectrumChunk> chunks,
                     const SpectrumCatalogManifest& manifest);

/// Returns the records inside a cone, brightest first.
///
/// Candidates are reached through a declination index alone - there is no right-ascension window,
/// so the work is proportional to the declination band the cone spans - and admitted by the exact
/// great-circle separation. The order is total: ascending G magnitude compared exactly, unmeasured
/// magnitudes last, then ascending source identifier, so the same catalog and query always give the
/// same answer whatever order the records were found in. The magnitude limit, when
/// present, refuses a record whose G is above it and admits one whose G is unmeasured only when
/// `admitNamedEntriesWithoutMagnitude` says so; a record needs no common name for that.
///
/// Runs on the calling thread and checks cancellation at bounded candidate boundaries. It allocates
/// the answer and nothing else.
///
/// @param execution the operation policy and engine-context lease.
/// @param catalog the catalog to search.
/// @param query the cone, the magnitude rule and the answer bound.
/// @return the positions and counts; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the catalog handle is empty; `ErrorCode::InvalidArgument` for a centre that is not a
///         direction, a radius that is not finite and positive, a non-finite magnitude limit or a
///         zero result bound; or cancellation. A cone containing nothing is a success with an empty
///         answer.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CatalogQueryResult>
querySpectrumCatalogRegion(const ExecutionContext& execution, const SpectrumCatalog& catalog,
                           const CatalogRegionQuery& query);

/// Which chunks a region needs, and whether the manifest declares all of them.
struct SpectrumChunkSelection
{
    /// The chunks whose declared tile ranges meet the region, ascending.
    std::vector<std::uint32_t> chunkIndices{};

    /// How many order-six tiles the region's disc covered.
    std::uint32_t tilesExamined = 0u;

    /// How many of those tiles no chunk declares.
    std::uint32_t undeclaredTiles = 0u;

    /// Whether every tile of the disc is declared by some chunk.
    ///
    /// @return true when `undeclaredTiles` is zero and at least one tile was examined.
    [[nodiscard]] bool declaredCoverageComplete() const noexcept
    {
        return tilesExamined > 0u && undeclaredTiles == 0u;
    }
};

/// The chunks a cone on the sky needs.
///
/// The disc is resolved to order-six nested tiles with conservative coverage, and each tile to the
/// chunk whose declared range holds it. A manifest with one chunk and no tile range is a single
/// undivided catalog and selects that chunk for every region, reporting one tile examined; a
/// manifest with no chunks selects nothing and examines no tile.
///
/// Runs on the calling thread and checks cancellation inside the disc query. Each tile is matched
/// against the chunk list in turn, so the cost is the tile count times the chunk count.
///
/// @param execution the operation policy and engine-context lease.
/// @param manifest the manifest whose chunks are being selected.
/// @param center the cone's centre.
/// @param radiusDegrees the cone's radius, finite and positive.
/// @return the selection; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a centre that is not a direction or a radius that is
///         not finite and positive; or whatever the disc query reports.
[[nodiscard]] AFTERNIGHTENGINE_API Result<SpectrumChunkSelection>
selectSpectrumChunksForRegion(const ExecutionContext& execution, const SpectrumCatalogManifest& manifest,
                              const SkyCoordinate& center, double radiusDegrees);

/// The chunks a set of Gaia source identifiers need.
///
/// A Gaia identifier carries its order-twelve nested tile in its top bits; that tile's ancestor at
/// the manifest's tile order - six for a parsed manifest - names the chunk. An identifier of zero,
/// one whose tile is outside the sky, and one whose tile no chunk declares select nothing, and
/// nothing reports them. An undivided manifest selects its one chunk whatever the identifiers.
///
/// Runs on the calling thread and checks cancellation at bounded identifier boundaries.
///
/// @param execution the operation policy and engine-context lease.
/// @param manifest the manifest whose chunks are being selected.
/// @param sourceIdentifiers the identifiers to resolve.
/// @return the chunk indices, ascending and unique; `ErrorCode::NotInitialized` when `execution`
///         holds nothing; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<std::uint32_t>>
selectSpectrumChunksForSources(const ExecutionContext& execution, const SpectrumCatalogManifest& manifest,
                               std::span<const std::uint64_t> sourceIdentifiers);

/// How much of the sky a manifest declares.
struct SpectrumCoverageSummary
{
    /// How many chunks the manifest lists.
    std::uint32_t declaredChunks = 0u;

    /// How many order-three tiles the sky has, which is how many chunks a full catalog has.
    std::uint32_t expectedParentChunks = 0u;

    /// How many order-six tiles the chunks declare between them.
    std::uint32_t declaredTiles = 0u;

    /// How many order-six tiles the sky has.
    std::uint32_t expectedTiles = 0u;

    /// Whether the declared tile count reaches the sky's. The count is a sum of range lengths, which
    /// a parsed manifest keeps free of overlaps.
    bool fullSkyCoverage = false;
};

/// What a manifest declares about its own coverage, from its tile ranges alone.
///
/// An undivided manifest declares no tile range, so it reports zero declared tiles and no full-sky
/// coverage.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param manifest the manifest to summarize.
/// @return the counts. The expected counts come from the manifest's own orders; an order above
///         `kMaximumHealpixOrder` reports zero expected tiles or chunks.
[[nodiscard]] AFTERNIGHTENGINE_API SpectrumCoverageSummary
summarizeSpectrumCatalogCoverage(const SpectrumCatalogManifest& manifest) noexcept;

/// What a host knows about a catalog before any spectrum is read.
struct SpectrumCatalogReadiness
{
    /// How the catalog reached the host, or absent when no manifest was read.
    std::optional<SpectrumCatalogAccessProfile> accessProfile{};

    /// The manifest's family, or empty.
    std::string catalogFamilyId{};

    /// The manifest's identity, or empty.
    std::string manifestId{};

    /// The manifest's rights review status, as text.
    std::string rightsReviewStatus{};

    /// Whether the host verified the chunk checksums the manifest names.
    bool checksumValid = false;

    /// Whether the host knows the installed chunks lag the manifest.
    bool stale = false;

    /// Whether the chunks the region needs are installed.
    bool regionAvailable = false;

    /// Whether the rights review status is one that permits use.
    bool rightsReviewApproved = false;
};

/// The readiness a manifest implies, given what the host verified.
///
/// The rights review is approved for exactly the two statuses this engine ships under:
/// `approved-for-managed-download` and `approved-for-bundling`.
///
/// Thread-safe and reentrant. Allocates only the returned strings.
///
/// @param manifest the manifest the host read.
/// @param checksumValid whether the host verified the checksums.
/// @param regionAvailable whether the region's chunks are installed.
/// @param stale whether the installed chunks lag the manifest.
/// @return the readiness.
[[nodiscard]] AFTERNIGHTENGINE_API SpectrumCatalogReadiness makeSpectrumCatalogReadiness(
    const SpectrumCatalogManifest& manifest, bool checksumValid, bool regionAvailable = true, bool stale = false);

/// Why a catalog may not be used yet.
enum class SpectrumCatalogReadinessError : std::uint8_t
{
    /// The catalog may be used.
    None = 0u,

    /// The access profile is absent, remote or blocked; only a local full install or an installed
    /// regional cache is read.
    AccessProfileNotLocal = 1u,

    /// The family or the manifest identity is empty.
    MissingIdentity = 2u,

    /// The checksums were not verified, or the install lags the manifest.
    ChecksumInvalidOrStale = 3u,

    /// The rights review status does not permit use.
    RightsReviewNotApproved = 4u,

    /// The region's chunks are not installed.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RegionNotCovered = 5u
};

/// Whether a value names a readiness refusal this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param error the value to inspect.
/// @return true for every reason, `None` included.
[[nodiscard]] constexpr bool isSpectrumCatalogReadinessError(SpectrumCatalogReadinessError error) noexcept
{
    return static_cast<std::uint8_t>(error) <=
           static_cast<std::uint8_t>(SpectrumCatalogReadinessError::RegionNotCovered);
}

/// The reason's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param error the reason to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownSpectrumCatalogReadinessError"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
spectrumCatalogReadinessErrorName(SpectrumCatalogReadinessError error) noexcept;

/// Whether a catalog may be used, and if not, the first reason why not.
///
/// The order is a contract: profile, identity, checksums, rights, region.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param readiness what the host knows.
/// @return `SpectrumCatalogReadinessError::None`, or the first rule that refused.
[[nodiscard]] AFTERNIGHTENGINE_API SpectrumCatalogReadinessError
checkSpectrumCatalogReadiness(const SpectrumCatalogReadiness& readiness) noexcept;

/// One star a previous stage matched, as it was recorded then.
///
/// The strings are identity tokens compared byte for byte, never parsed: they say which catalogs,
/// which solution and which epoch produced the match, so a later stage can tell a match that still
/// describes its inputs from one that does not.
struct MatchedSourceReference
{
    /// The Gaia source identifier, or zero for a record that carried none.
    std::uint64_t sourceIdentifier = 0u;

    /// The astrometric catalog family the match was made against.
    std::string astrometricCatalogFamilyId{};

    /// That catalog's manifest identity.
    std::string astrometricCatalogManifestId{};

    /// The spectrum catalog family the match was recorded for.
    std::string spectrumCatalogFamilyId{};

    /// That catalog's manifest identity.
    std::string spectrumCatalogManifestId{};

    /// The world-coordinate solution the match was made under.
    std::string wcsSolutionId{};

    /// The observation epoch the positions were corrected to, as text.
    std::string observationEpoch{};

    /// The proper-motion model that corrected them, as text.
    std::string properMotionModelVersion{};

    /// How far the detection was from the catalog position, in arcseconds, or absent.
    std::optional<double> separationArcseconds{};

    /// The same distance in pixels, or absent.
    std::optional<double> residualPixels{};
};

/// What a recorded match must agree with to be reused.
///
/// An empty string requires nothing; an absent bound bounds nothing. The spectrum catalog family and
/// manifest default to the catalog being joined when left empty.
struct MatchedSourceRequirements
{
    /// The spectrum catalog family every reference must name, or empty for the catalog's own.
    std::string requiredSpectrumCatalogFamilyId{};

    /// The spectrum manifest every reference must name, or empty for the catalog's own.
    std::string requiredSpectrumManifestId{};

    /// The astrometric catalog family every reference must name, or empty.
    std::string requiredAstrometricCatalogFamilyId{};

    /// The astrometric manifest every reference must name, or empty.
    std::string requiredAstrometricCatalogManifestId{};

    /// The observation epoch every reference must name, or empty.
    std::string requiredObservationEpoch{};

    /// The proper-motion model every reference must name, or empty.
    std::string requiredProperMotionModelVersion{};

    /// The solution every reference must name, or empty.
    std::string requiredWcsSolutionId{};

    /// The largest residual admitted, in pixels; a reference with none is refused when this is set.
    std::optional<double> maximumResidualPixels{};

    /// The largest separation admitted, in arcseconds; a reference with none is refused when set.
    std::optional<double> maximumSeparationArcseconds{};

    /// Whether a record with any quality flag set is refused.
    bool rejectNonZeroQualityFlags = true;
};

/// What became of one recorded match.
enum class MatchedSourceLookupStatus : std::uint8_t
{
    /// The record was found and every requirement held.
    Satisfied = 0u,

    /// The reference carries no identifier.
    InvalidSourceIdentifier = 1u,

    /// No record carries the identifier, in a catalog installed in full.
    MissingSource = 2u,

    /// No record carries the identifier, in a regional cache that may not hold its chunk.
    MissingChunkOrCache = 3u,

    /// The reference names another spectrum catalog family or manifest.
    ManifestMismatch = 4u,

    /// The reference names another astrometric catalog, epoch, model or solution, or its residual
    /// or separation is absent, not finite or above a stated bound.
    StaleMatch = 5u,

    /// The record carries a quality flag the requirements refuse.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    RejectedQualityFlags = 6u
};

/// Whether a value names a lookup status this build reports.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param status the value to inspect.
/// @return true for one of the seven declared values.
[[nodiscard]] constexpr bool isMatchedSourceLookupStatus(MatchedSourceLookupStatus status) noexcept
{
    return static_cast<std::uint8_t>(status) <=
           static_cast<std::uint8_t>(MatchedSourceLookupStatus::RejectedQualityFlags);
}

/// The status's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param status the value to name.
/// @return a null-terminated string literal compiled into this library;
///         `"UnknownMatchedSourceLookupStatus"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* matchedSourceLookupStatusName(MatchedSourceLookupStatus status) noexcept;

/// One reference's outcome.
struct MatchedSourceLookup
{
    /// What became of it.
    MatchedSourceLookupStatus status = MatchedSourceLookupStatus::MissingSource;

    /// The record's position in the catalog, present exactly when the status is `Satisfied`.
    std::optional<std::uint32_t> recordIndex{};
};

/// Everything one resolution produced.
struct MatchedSourceResolution
{
    /// One outcome per reference, in reference order.
    std::vector<MatchedSourceLookup> lookups{};

    /// How many references were satisfied.
    std::uint32_t acceptedCount = 0u;

    /// How many were refused for any reason.
    std::uint32_t rejectedCount = 0u;

    /// How many carried no identifier.
    std::uint32_t invalidSourceIdentifierCount = 0u;

    /// How many named a record the full catalog does not hold.
    std::uint32_t missingSourceCount = 0u;

    /// How many named a record the regional cache may not hold.
    std::uint32_t cacheMissCount = 0u;

    /// How many named another spectrum catalog.
    std::uint32_t manifestMismatchCount = 0u;

    /// How many were stale.
    std::uint32_t staleMatchCount = 0u;

    /// How many were refused for quality flags.
    std::uint32_t qualityRejectedCount = 0u;
};

/// Joins recorded matches against a spectrum catalog under stated requirements.
///
/// Each reference is looked up by identifier and then held to the requirements in a fixed order:
/// identifier, presence, spectrum catalog identity, astrometric identity, epoch, model, solution,
/// residual, separation, quality flags. The first rule that refuses names the outcome. A missing
/// record is `MissingChunkOrCache` when the catalog's manifest names an installed regional cache and
/// `MissingSource` otherwise.
///
/// Runs on the calling thread and checks cancellation at bounded reference boundaries. Allocates
/// only the returned outcomes.
///
/// @param execution the operation policy and engine-context lease.
/// @param references the recorded matches.
/// @param catalog the catalog to join against.
/// @param requirements what each reference must agree with.
/// @return the outcomes and counts; `ErrorCode::NotInitialized` when `execution` holds nothing or
///         the catalog handle is empty; `ErrorCode::InvalidArgument` for a non-finite bound; or
///         cancellation. References that all fail are a success with every outcome refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MatchedSourceResolution>
resolveMatchedSources(const ExecutionContext& execution, std::span<const MatchedSourceReference> references,
                      const SpectrumCatalog& catalog, const MatchedSourceRequirements& requirements = {});

} // namespace an
