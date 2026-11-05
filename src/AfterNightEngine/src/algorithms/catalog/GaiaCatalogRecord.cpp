// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - decoding Gaia records and turning them into shared catalog entries.

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"
#include "algorithms/catalog/GaiaBinary.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;
using catalog_detail::readFloat32LittleEndian;
using catalog_detail::readFloat64LittleEndian;
using catalog_detail::readInt16LittleEndian;
using catalog_detail::readUint16LittleEndian;
using catalog_detail::readUint64LittleEndian;

constexpr const char* kDecodeOperation = "an::decodeGaiaRecords";
constexpr const char* kEntryOperation = "an::buildGaiaCatalogEntries";
constexpr std::uint32_t kCancellationStride = 4096u;
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

/// How many milliarcseconds one degree holds, which is what a proper motion is divided by.
constexpr double kMasPerDegree = 3600.0 * 1000.0;

/// How small the cosine of a declination may be before a right-ascension correction is skipped.
///
/// Past it the division that turns a great-circle proper motion into a right-ascension step has lost
/// its meaning, and the position is left where the catalog put it.
constexpr double kMinimumCosineDeclination = 1.0e-9;

/// The value a temperature field holds when the record carries no measurement.
constexpr std::uint16_t kAbsentTemperature = 0u;

/// The value an extinction field holds when the record carries no measurement.
constexpr std::uint16_t kAbsentExtinction = 65535u;

/// Where each field of the record layouts begins, in bytes from the start of a record.
constexpr std::size_t kRightAscensionOffset = 0u;
constexpr std::size_t kDeclinationOffset = 8u;
constexpr std::size_t kMagnitudeGOffset = 16u;
constexpr std::size_t kMagnitudeBpOffset = 20u;
constexpr std::size_t kMagnitudeRpOffset = 24u;
constexpr std::size_t kProperMotionRightAscensionOffset = 28u;
constexpr std::size_t kProperMotionDeclinationOffset = 32u;
constexpr std::size_t kSourceIdentifierOffset = 36u;
constexpr std::size_t kTemperatureOffset = 44u;
constexpr std::size_t kSurfaceGravityOffset = 46u;
constexpr std::size_t kMetallicityOffset = 48u;
constexpr std::size_t kExtinctionOffset = 50u;
constexpr std::size_t kReddeningOffset = 52u;

/// A measured value, or nothing when the field holds one that is not.
///
/// @param value the field as it was read.
/// @return the value as a double, or absent when it is not finite.
[[nodiscard]] std::optional<double> measured(const float value) noexcept
{
    const double widened = static_cast<double>(value);
    return std::isfinite(widened) ? std::optional<double>{widened} : std::nullopt;
}

/// A quantized parameter, or nothing when the field holds its absent code.
///
/// The scale is applied in double, because dividing a float by a float rounds the stored integer
/// twice on its way to the value it stands for.
///
/// @param stored the field as it was read.
/// @param absentCode the value that means "not measured".
/// @param scale what the stored integer is divided by.
/// @return the parameter, or absent.
[[nodiscard]] std::optional<double> quantized(const std::int16_t stored, const std::int16_t absentCode,
                                              const double scale) noexcept
{
    return stored == absentCode ? std::nullopt : std::optional<double>{static_cast<double>(stored) / scale};
}

/// Reads one record out of a block.
///
/// @param recordBytes the record's own bytes, exactly as many as its layout occupies.
/// @param layout which layout the bytes hold.
/// @return the record.
[[nodiscard]] GaiaStarRecord decodeOneRecord(const std::span<const std::byte> recordBytes,
                                             const GaiaRecordLayout layout) noexcept
{
    GaiaStarRecord record;
    record.coordinate.rightAscensionDegrees = readFloat64LittleEndian(recordBytes, kRightAscensionOffset);
    record.coordinate.declinationDegrees = readFloat64LittleEndian(recordBytes, kDeclinationOffset);
    record.magnitudeG = measured(readFloat32LittleEndian(recordBytes, kMagnitudeGOffset));
    record.magnitudeBp = measured(readFloat32LittleEndian(recordBytes, kMagnitudeBpOffset));
    record.magnitudeRp = measured(readFloat32LittleEndian(recordBytes, kMagnitudeRpOffset));
    record.properMotionRightAscensionMasPerYear =
        measured(readFloat32LittleEndian(recordBytes, kProperMotionRightAscensionOffset));
    record.properMotionDeclinationMasPerYear =
        measured(readFloat32LittleEndian(recordBytes, kProperMotionDeclinationOffset));

    if (layout == GaiaRecordLayout::Version1)
    {
        return record;
    }
    record.sourceIdentifier = readUint64LittleEndian(recordBytes, kSourceIdentifierOffset);
    if (layout == GaiaRecordLayout::Version2)
    {
        return record;
    }

    const std::uint16_t temperature = readUint16LittleEndian(recordBytes, kTemperatureOffset);
    record.effectiveTemperatureKelvin =
        temperature == kAbsentTemperature ? std::nullopt : std::optional<double>{static_cast<double>(temperature)};
    constexpr std::int16_t kAbsentSigned = -32768;
    record.surfaceGravityLog =
        quantized(readInt16LittleEndian(recordBytes, kSurfaceGravityOffset), kAbsentSigned, 100.0);
    record.metallicity = quantized(readInt16LittleEndian(recordBytes, kMetallicityOffset), kAbsentSigned, 100.0);
    const std::uint16_t extinction = readUint16LittleEndian(recordBytes, kExtinctionOffset);
    record.extinctionG = extinction == kAbsentExtinction
                             ? std::nullopt
                             : std::optional<double>{static_cast<double>(extinction) / 10000.0};
    record.reddeningBpMinusRp = quantized(readInt16LittleEndian(recordBytes, kReddeningOffset), kAbsentSigned, 10000.0);
    return record;
}

} // namespace

const char* gaiaRecordLayoutName(const GaiaRecordLayout layout) noexcept
{
    switch (layout)
    {
    case GaiaRecordLayout::Version1:
        return "Version1";
    case GaiaRecordLayout::Version2:
        return "Version2";
    case GaiaRecordLayout::Version3:
        return "Version3";
    }
    return "UnknownGaiaRecordLayout";
}

Result<std::vector<GaiaStarRecord>> decodeGaiaRecords(const ExecutionContext& execution,
                                                      const std::span<const std::byte> recordBytes,
                                                      const GaiaRecordLayout layout, const GaiaRecordLimits& limits)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kDecodeOperation);
    }
    if (!isGaiaRecordLayout(layout))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the record layout is not one this build decodes",
                              kDecodeOperation);
    }
    if (limits.maximumRecords == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the record ceiling admits nothing", kDecodeOperation);
    }

    const std::size_t recordSize = gaiaRecordSizeBytes(layout);
    if ((recordBytes.size() % recordSize) != 0u)
    {
        return Status::failure(
            ErrorCode::InvalidData,
            StatusDetail{.message = "the block does not hold a whole number of records",
                         .operation = kDecodeOperation,
                         .domain = ErrorDomain::Algorithms,
                         .context = {StatusContextField::ofCount(ContextKey::Requested, recordBytes.size()),
                                     StatusContextField::ofText(ContextKey::Parameter, gaiaRecordLayoutName(layout))}});
    }

    const std::size_t recordCount = recordBytes.size() / recordSize;
    if (recordCount > limits.maximumRecords)
    {
        return catalogLimitFailure("the block holds more records than the ceiling admits", kDecodeOperation,
                                   recordCount, limits.maximumRecords);
    }

    std::vector<GaiaStarRecord> records;
    records.reserve(recordCount);
    for (std::size_t index = 0u; index < recordCount; ++index)
    {
        if ((index % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        records.push_back(decodeOneRecord(recordBytes.subspan(index * recordSize, recordSize), layout));
    }
    return records;
}

SkyCoordinate applyGaiaEpochCorrection(const GaiaStarRecord& record, const double catalogEpochYear,
                                       const double observationEpochYear) noexcept
{
    SkyCoordinate corrected{.rightAscensionDegrees =
                                normalizeRightAscensionDegrees(record.coordinate.rightAscensionDegrees),
                            .declinationDegrees = std::clamp(record.coordinate.declinationDegrees, -90.0, 90.0)};

    const double elapsedYears = observationEpochYear - catalogEpochYear;
    if (!std::isfinite(elapsedYears) || elapsedYears == 0.0)
    {
        return corrected;
    }

    const double cosineDeclination = std::cos(corrected.declinationDegrees * kRadiansPerDegree);
    if (record.properMotionRightAscensionMasPerYear.has_value() &&
        std::abs(cosineDeclination) > kMinimumCosineDeclination)
    {
        corrected.rightAscensionDegrees = normalizeRightAscensionDegrees(
            corrected.rightAscensionDegrees +
            ((*record.properMotionRightAscensionMasPerYear * elapsedYears) / (kMasPerDegree * cosineDeclination)));
    }
    if (record.properMotionDeclinationMasPerYear.has_value())
    {
        corrected.declinationDegrees = std::clamp(
            corrected.declinationDegrees + ((*record.properMotionDeclinationMasPerYear * elapsedYears) / kMasPerDegree),
            -90.0, 90.0);
    }
    return corrected;
}

std::optional<double> gaiaColorIndexBpMinusRp(const GaiaStarRecord& record) noexcept
{
    if (!record.magnitudeBp.has_value() || !record.magnitudeRp.has_value())
    {
        return std::nullopt;
    }
    return *record.magnitudeBp - *record.magnitudeRp;
}

std::string makeGaiaSourceDesignation(const std::uint64_t sourceIdentifier)
{
    return "Gaia DR3 " + std::to_string(sourceIdentifier);
}

std::string makeGaiaTileDesignation(const std::int32_t tileIndex, const std::uint64_t tileRecordIndex)
{
    return "Gaia DR3 tile " + std::to_string(tileIndex) + ":" + std::to_string(tileRecordIndex);
}

Result<std::vector<CatalogEntry>> buildGaiaCatalogEntries(const ExecutionContext& execution,
                                                          const std::span<const GaiaStarRecord> records,
                                                          const GaiaEntryRequest& request)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kEntryOperation);
    }
    if (!std::isfinite(request.catalogEpochYear) || !std::isfinite(request.observationEpochYear))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "an epoch is not a finite year", kEntryOperation);
    }
    if (request.maximumEntries == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the entry ceiling admits nothing", kEntryOperation);
    }
    if (records.size() > request.maximumEntries)
    {
        return catalogLimitFailure("the block holds more records than the entry ceiling admits", kEntryOperation,
                                   records.size(), request.maximumEntries);
    }

    const auto recordFailure = [](const char* message, std::size_t position) {
        return Status::failure(ErrorCode::InvalidData,
                               StatusDetail{.message = message,
                                            .operation = kEntryOperation,
                                            .domain = ErrorDomain::Algorithms,
                                            .context = {StatusContextField::ofCount(ContextKey::Completed, position)}});
    };

    std::vector<CatalogEntry> entries;
    entries.reserve(records.size());
    for (std::size_t position = 0u; position < records.size(); ++position)
    {
        if ((position % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }

        const GaiaStarRecord& record = records[position];
        CatalogEntry entry;
        if (record.sourceIdentifier != 0u)
        {
            entry.identifier = makeGaiaSourceDesignation(record.sourceIdentifier);
        }
        else if (request.tileIndex >= 0)
        {
            entry.identifier = makeGaiaTileDesignation(request.tileIndex, position);
        }
        else
        {
            return recordFailure("a record carries no source identifier and came from no tile, so nothing names it",
                                 position);
        }

        entry.coordinate = applyGaiaEpochCorrection(record, request.catalogEpochYear, request.observationEpochYear);
        if (!isSkyCoordinate(entry.coordinate))
        {
            return recordFailure("a record's corrected position is not a direction on the sky", position);
        }
        entry.magnitude = record.magnitudeG;
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace an
