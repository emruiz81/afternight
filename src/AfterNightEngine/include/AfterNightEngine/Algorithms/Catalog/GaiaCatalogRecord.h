// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One Gaia survey record, its binary layouts, and the entries it becomes.
///
/// `GaiaStarRecord` is the provider's own record: three photometric bands, a proper motion and five
/// astrophysical parameters that no other catalog carries, each an absent optional when the file
/// holds its "not measured" code. `decodeGaiaRecords` reads a block in one of three fixed-size
/// little-endian layouts, field by field; `applyGaiaEpochCorrection` moves a position to an
/// observation epoch; and `buildGaiaCatalogEntries` turns records into the `CatalogEntry` values
/// `buildSkyCatalog` takes, handing up only a designation, a direction and the G magnitude.
///
/// Everything particular to Gaia stays in the records the caller holds, so no query below the
/// catalog learns what Gaia is.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The epoch the shipping Gaia tables state their positions at, in Julian years.
///
/// A default rather than a constant of the format: a table states its own epoch in its manifest,
/// and an epoch correction is given both years rather than assuming this one, so a table at any other
/// epoch is corrected from its own.
inline constexpr double kGaiaDefaultEpochYear = 2016.0;

/// The photometric band a Gaia record's `magnitudeG` is measured in.
///
/// The value a catalog built from these records states once, so that nothing downstream has to
/// guess which of the three Gaia bands an entry's magnitude came from.
inline constexpr const char* kGaiaPhotometricBand = "G";

/// Which fixed-size record layout a block of Gaia bytes holds.
///
/// A closed set: these are the three layouts this engine's own Gaia tables have been written in,
/// each identified by its own size, and a fourth would be a format change rather than a parameter.
/// The names are the format's own `record_version` numbers, which is what a manifest states.
enum class GaiaRecordLayout : std::uint8_t
{
    /// Thirty-six bytes: position, three magnitudes and two proper-motion components. Carries no
    /// source identifier, so entries built from it are named by their position in their tile.
    Version1 = 0u,

    /// Forty-four bytes: `Version1` followed by the Gaia source identifier.
    Version2 = 1u,

    /// Sixty-four bytes: position, three magnitudes, two proper-motion components, the source
    /// identifier, and five quantized astrophysical parameters - the temperature in whole kelvin,
    /// surface gravity and metallicity in hundredths, extinction and reddening in ten-thousandths.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Version3 = 2u
};

/// Whether a value names a layout this build decodes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param layout the value to inspect.
/// @return true for one of the three declared layouts; false for an out-of-range cast.
[[nodiscard]] constexpr bool isGaiaRecordLayout(GaiaRecordLayout layout) noexcept
{
    return layout == GaiaRecordLayout::Version1 || layout == GaiaRecordLayout::Version2 ||
           layout == GaiaRecordLayout::Version3;
}

/// How many bytes one record of a layout occupies on disk.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param layout the layout to measure.
/// @return 36, 44 or 64; zero for an out-of-range cast, which is what makes a size check refuse it.
[[nodiscard]] constexpr std::uint32_t gaiaRecordSizeBytes(GaiaRecordLayout layout) noexcept
{
    switch (layout)
    {
    case GaiaRecordLayout::Version1:
        return 36u;
    case GaiaRecordLayout::Version2:
        return 44u;
    case GaiaRecordLayout::Version3:
        return 64u;
    }
    return 0u;
}

/// Which layout a stated record size names.
///
/// The closed table read backwards, so a manifest's `record_size_bytes` becomes a layout without a
/// call site restating the three sizes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param recordSizeBytes the size a manifest or a chunk header stated.
/// @return the layout, or absent when no layout has that size.
[[nodiscard]] constexpr std::optional<GaiaRecordLayout> gaiaRecordLayoutForSize(std::uint64_t recordSizeBytes) noexcept
{
    if (recordSizeBytes == gaiaRecordSizeBytes(GaiaRecordLayout::Version1))
    {
        return GaiaRecordLayout::Version1;
    }
    if (recordSizeBytes == gaiaRecordSizeBytes(GaiaRecordLayout::Version2))
    {
        return GaiaRecordLayout::Version2;
    }
    if (recordSizeBytes == gaiaRecordSizeBytes(GaiaRecordLayout::Version3))
    {
        return GaiaRecordLayout::Version3;
    }
    return std::nullopt;
}

/// The layout's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param layout the layout to name.
/// @return a null-terminated string literal compiled into this library, which outlives every
///         caller; `"UnknownGaiaRecordLayout"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* gaiaRecordLayoutName(GaiaRecordLayout layout) noexcept;

/// One star as the Gaia tables carry it.
///
/// The provider's own record, and the reason it exists: it carries three photometric bands, a
/// proper motion and five astrophysical parameters that no other catalog this engine reads has, and
/// none of them belongs in `CatalogEntry`, which every provider shares. Everything a query needs is
/// in the entry this record becomes; everything particular to Gaia stays here, where a photometric
/// or spectrophotometric fit that wants it asks the provider for it by name.
///
/// Every unmeasured quantity is an absent optional. On disk "not measured" is spelled four ways - a
/// non-finite float for a magnitude or a proper-motion component, zero for the temperature, the
/// smallest signed 16-bit code for surface gravity, metallicity and reddening, and the largest
/// unsigned 16-bit code for extinction - and reading them as numbers is how an unmeasured surface
/// gravity becomes -327.68 in an average.
struct GaiaStarRecord
{
    /// Where the star is at the catalog's own epoch, in degrees.
    SkyCoordinate coordinate{};

    /// The Gaia source identifier, or zero for a layout that carries none.
    ///
    /// Zero is the format's own "absent" rather than a sentinel this engine chose: the thirty-six
    /// byte layout has no field for it, and an entry built from such a record is named by its
    /// position in its tile instead.
    std::uint64_t sourceIdentifier = 0u;

    /// The broad-band G magnitude, or absent when the record carries no measurement.
    std::optional<double> magnitudeG{};

    /// The blue-photometer BP magnitude, or absent when unmeasured.
    std::optional<double> magnitudeBp{};

    /// The red-photometer RP magnitude, or absent when unmeasured.
    std::optional<double> magnitudeRp{};

    /// Proper motion in right ascension, in milliarcseconds per year, already multiplied by the
    /// cosine of the declination as Gaia publishes it. Absent when unmeasured.
    std::optional<double> properMotionRightAscensionMasPerYear{};

    /// Proper motion in declination, in milliarcseconds per year. Absent when unmeasured.
    std::optional<double> properMotionDeclinationMasPerYear{};

    /// Effective temperature, in kelvin, or absent when the record carries none.
    std::optional<double> effectiveTemperatureKelvin{};

    /// Base-ten logarithm of the surface gravity in centimetres per second squared, or absent.
    std::optional<double> surfaceGravityLog{};

    /// Metallicity as the base-ten logarithm of the iron-to-hydrogen ratio relative to the Sun, or
    /// absent.
    std::optional<double> metallicity{};

    /// Line-of-sight extinction in the G band, in magnitudes, or absent.
    std::optional<double> extinctionG{};

    /// Reddening of the BP-minus-RP colour, in magnitudes, or absent.
    std::optional<double> reddeningBpMinusRp{};

    /// Whether two records carry the same facts.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const GaiaStarRecord& left, const GaiaStarRecord& right) = default;
};

/// How much one decode is allowed to read.
struct GaiaRecordLimits
{
    /// The most records one decode produces.
    ///
    /// A refusal rather than a truncation, carrying the requested and permitted counts: a caller
    /// that silently received the first half of a tile would answer questions about a sky it only
    /// partly knows.
    std::uint32_t maximumRecords = kMaxCatalogEntries;
};

/// Decodes a block of fixed-size Gaia records.
///
/// The bytes are the provider's own little-endian layout, read field by field rather than
/// reinterpreted, so the decode does not depend on this build's structure padding or byte order.
/// The block must hold a whole number of records: a trailing partial record is a truncated file
/// rather than a record to guess at.
///
/// Runs on the calling thread, checks cancellation at bounded record boundaries, and allocates only
/// the records it returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param recordBytes the block to decode, which is only read and need not outlive the call.
/// @param layout which fixed-size layout the block holds.
/// @param limits how many records the decode may produce.
/// @return the records, in the order the block lists them; `ErrorCode::NotInitialized` when
///         `execution` holds nothing; `ErrorCode::InvalidArgument` for an out-of-range layout or a
///         zero limit; `ErrorCode::InvalidData` when the block is not a whole number of records;
///         `ErrorCode::LimitExceeded` above the record limit, carrying the requested and permitted
///         counts; or cancellation. An empty block is a success with no records.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<GaiaStarRecord>>
decodeGaiaRecords(const ExecutionContext& execution, std::span<const std::byte> recordBytes, GaiaRecordLayout layout,
                  const GaiaRecordLimits& limits = {});

/// Where a star is at an observation epoch, given the epoch its catalog states.
///
/// Applies the record's proper motion over the elapsed years and folds the answer back into the
/// declared ranges. A component the record does not carry moves nothing, and near a pole, where the
/// cosine of the declination is too small to divide by, the right ascension is left alone rather
/// than sent anywhere by a division that has lost its meaning.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param record the star, at its catalog's epoch.
/// @param catalogEpochYear the epoch the catalog states its positions at, in Julian years.
/// @param observationEpochYear the epoch to move the position to, in Julian years.
/// @return the corrected direction, with right ascension folded into `[0, 360)` and declination
///         clamped into `[-90, 90]`. An elapsed time that is not finite - either epoch not finite -
///         or zero returns the folded and clamped catalog position.
[[nodiscard]] AFTERNIGHTENGINE_API SkyCoordinate applyGaiaEpochCorrection(const GaiaStarRecord& record,
                                                                          double catalogEpochYear,
                                                                          double observationEpochYear) noexcept;

/// The BP-minus-RP colour index of one record.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param record the star to measure.
/// @return the difference in magnitudes, or absent when either band is unmeasured.
[[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> gaiaColorIndexBpMinusRp(const GaiaStarRecord& record) noexcept;

/// The identifier a Gaia source is named by.
///
/// Thread-safe and reentrant. It allocates only the returned string.
///
/// @param sourceIdentifier the Gaia source identifier, which must not be zero for this spelling to
///        mean anything.
/// @return the designation, such as `"Gaia DR3 4295806720"`.
[[nodiscard]] AFTERNIGHTENGINE_API std::string makeGaiaSourceDesignation(std::uint64_t sourceIdentifier);

/// The identifier a Gaia record with no source identifier is named by.
///
/// Stable only within one build of one catalog: it names a position in a tile rather than a star,
/// and a table rebuilt with different tile contents reuses it for a different star. It exists
/// because the thirty-six byte layout carries no identifier and an entry still has to be named.
///
/// Thread-safe and reentrant. It allocates only the returned string.
///
/// @param tileIndex which tile the record came from.
/// @param tileRecordIndex the record's position within that tile.
/// @return the designation, such as `"Gaia DR3 tile 1234:7"`.
[[nodiscard]] AFTERNIGHTENGINE_API std::string makeGaiaTileDesignation(std::int32_t tileIndex,
                                                                       std::uint64_t tileRecordIndex);

/// What a block of Gaia records is turned into entries for.
struct GaiaEntryRequest
{
    /// Which tile the records came from, used to name a record that carries no source identifier.
    ///
    /// Negative says the records did not come from a tile, in which case a record with no source
    /// identifier is refused rather than named after a tile that does not exist.
    ///
    /// A record with no identifier is named by its position in the span handed to
    /// `buildGaiaCatalogEntries`, so that span must be the whole tile, in the tile's own order, for
    /// the name to be the record's position in its tile.
    std::int32_t tileIndex = -1;

    /// The epoch the records state their positions at, in Julian years.
    double catalogEpochYear = kGaiaDefaultEpochYear;

    /// The epoch to place the entries at, in Julian years.
    ///
    /// Equal to `catalogEpochYear` by default, which applies no proper motion at all.
    double observationEpochYear = kGaiaDefaultEpochYear;

    /// The most entries this call produces.
    std::uint32_t maximumEntries = kMaxCatalogEntries;
};

/// Turns Gaia records into the entries every catalog query is written against.
///
/// This is the whole of what the provider hands upwards: a designation, a direction at the
/// requested epoch, and the G magnitude. The other two bands, the proper motion and the
/// astrophysical parameters stay in the records the caller already holds, which is what keeps Gaia
/// vocabulary out of `SkyCatalog` and out of every query below it.
///
/// A record with no G magnitude produces an entry with no magnitude, which a magnitude-limited
/// query then refuses: admitting it would let a star with no photometry pass every photometric limit
/// it is compared against, which is the same mistake as inventing a bright star out of a missing
/// measurement.
///
/// Runs on the calling thread, checks cancellation at bounded record boundaries, and allocates only
/// the entries it returns.
///
/// @param execution the operation policy and engine-context lease.
/// @param records the records to convert, which are only read.
/// @param request the tile identity, the two epochs and the entry ceiling.
/// @return the entries, one per record and in the same order; `ErrorCode::NotInitialized` when
///         `execution` holds nothing; `ErrorCode::InvalidArgument` for a non-finite epoch or a zero
///         ceiling; `ErrorCode::LimitExceeded` above the ceiling, carrying the requested and
///         permitted counts; `ErrorCode::InvalidData` for a record with no source identifier that
///         came from no tile, or one whose corrected position is not a direction, carrying the
///         record's position; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::vector<CatalogEntry>>
buildGaiaCatalogEntries(const ExecutionContext& execution, std::span<const GaiaStarRecord> records,
                        const GaiaEntryRequest& request = {});

} // namespace an
