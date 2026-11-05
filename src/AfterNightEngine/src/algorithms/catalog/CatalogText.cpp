// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a delimited catalog text into owned entries, within stated bounds.

#include <AfterNightEngine/Algorithms/Catalog/CatalogText.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;
using catalog_detail::equalsFolded;
using catalog_detail::trimAscii;

constexpr const char* kOperation = "an::parseCatalogText";
constexpr std::uint32_t kCancellationStride = 4096u;
constexpr std::size_t kMaxColumns = 9u;

/// The name of every column of one schema, in the order the text writes them.
///
/// The names are the ones a failure quotes, so a person reading one is looking at the same word the
/// header row of their file carries.
struct SchemaColumns
{
    /// How many columns a row of this schema has.
    std::size_t count;

    /// The column names, of which the first `count` are meaningful.
    std::array<const char*, kMaxColumns> names;
};

constexpr SchemaColumns kExtendedObjectColumns{.count = 9u,
                                               .names = {"id", "common_name", "ra_deg", "dec_deg", "major_axis_arcmin",
                                                         "minor_axis_arcmin", "position_angle_deg", "object_type",
                                                         "magnitude"}};

constexpr SchemaColumns kStarColumns{.count = 7u,
                                     .names = {"id", "common_name", "bayer_designation", "ra_deg", "dec_deg",
                                               "magnitude", "spectral_type", nullptr, nullptr}};

/// The columns of one schema.
///
/// @param schema which layout.
/// @return its column table.
[[nodiscard]] const SchemaColumns& columnsOf(CatalogTextSchema schema) noexcept
{
    return schema == CatalogTextSchema::Star ? kStarColumns : kExtendedObjectColumns;
}

/// One row's fields, already split and unquoted.
///
/// The strings are owned because unquoting a field that holds a doubled quote produces text that is
/// in no buffer the caller supplied. The list is reused across rows, so its own storage is taken
/// once; a field longer than a string's inline capacity still allocates on every row.
using RowFields = std::vector<std::string>;

/// Why one row could not be read.
struct RowProblem
{
    /// The column whose value was refused, or the row itself when `column` is null.
    const char* column = nullptr;

    /// What a person reads.
    const char* message = nullptr;
};

/// Splits one line into fields, honouring quoted fields and doubled quotes.
///
/// @param line the line, already trimmed of surrounding space.
/// @param maximumFieldBytes the most bytes one field may hold before unquoting.
/// @param fields receives the fields, replacing whatever it held.
/// @param widestField receives the size of the field that exceeded the limit, when one did.
/// @return true when every field is inside its limit.
[[nodiscard]] bool splitRow(std::string_view line, std::uint32_t maximumFieldBytes, RowFields& fields,
                            std::uint64_t& widestField)
{
    fields.clear();
    std::string current;
    bool insideQuotes = false;
    std::size_t rawFieldBytes = 0u;

    const auto finishField = [&]() {
        fields.push_back(current);
        current.clear();
        rawFieldBytes = 0u;
    };

    for (std::size_t index = 0u; index < line.size(); ++index)
    {
        const char value = line[index];
        ++rawFieldBytes;
        if (rawFieldBytes > maximumFieldBytes)
        {
            widestField = rawFieldBytes;
            return false;
        }
        if (value == '"')
        {
            if (insideQuotes && (index + 1u) < line.size() && line[index + 1u] == '"')
            {
                current.push_back('"');
                ++index;
                ++rawFieldBytes;
                continue;
            }
            insideQuotes = !insideQuotes;
            continue;
        }
        if (value == ',' && !insideQuotes)
        {
            finishField();
            continue;
        }
        current.push_back(value);
    }
    finishField();
    return true;
}

/// Reads one field as a complete finite number.
///
/// @param text the field, which may carry surrounding space.
/// @param value receives the number when the field holds one.
/// @return true when the field is empty, in which case `value` is untouched, or holds a complete
///         finite number.
[[nodiscard]] bool readNumber(std::string_view text, double& value) noexcept
{
    const std::string_view trimmed = trimAscii(text);
    if (trimmed.empty())
    {
        return true;
    }
    double parsed = 0.0;
    const char* const begin = trimmed.data();
    const char* const end = begin + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed))
    {
        return false;
    }
    value = parsed;
    return true;
}

/// Whether a field was left empty.
///
/// @param text the field.
/// @return true when it holds nothing but space.
[[nodiscard]] bool isBlankField(std::string_view text) noexcept
{
    return trimAscii(text).empty();
}

/// Turns a magnitude field into a measurement or into the absence of one.
///
/// @param text the field.
/// @param sentinel the value at or above which the provider means "not measured".
/// @param magnitude receives the measurement, or is left absent.
/// @param unknown set when the sentinel rule removed a number.
/// @return true when the field is readable.
[[nodiscard]] bool readMagnitude(std::string_view text, const std::optional<double>& sentinel,
                                 std::optional<double>& magnitude, bool& unknown) noexcept
{
    magnitude.reset();
    unknown = false;
    if (isBlankField(text))
    {
        return true;
    }
    double value = 0.0;
    if (!readNumber(text, value))
    {
        return false;
    }
    if (sentinel.has_value() && value >= *sentinel)
    {
        unknown = true;
        return true;
    }
    magnitude = value;
    return true;
}

/// Reads one row of the extended-object schema.
///
/// @param fields the row's fields.
/// @param request the parse policy.
/// @param entry receives the entry.
/// @param report the running counts, updated for repairs this row needed.
/// @return no problem, or the column that refused the row.
[[nodiscard]] RowProblem readExtendedObject(const RowFields& fields, const CatalogTextRequest& request,
                                            CatalogEntry& entry, CatalogTextReport& report)
{
    double rightAscension = 0.0;
    double declination = 0.0;
    double majorAxis = 0.0;
    double minorAxis = 0.0;
    double positionAngle = 0.0;

    if (!readNumber(fields[2], rightAscension))
    {
        return RowProblem{.column = kExtendedObjectColumns.names[2], .message = "the right ascension is not a number"};
    }
    if (!readNumber(fields[3], declination))
    {
        return RowProblem{.column = kExtendedObjectColumns.names[3], .message = "the declination is not a number"};
    }
    if (!readNumber(fields[4], majorAxis))
    {
        return RowProblem{.column = kExtendedObjectColumns.names[4], .message = "the major axis is not a number"};
    }
    minorAxis = majorAxis;
    if (!readNumber(fields[5], minorAxis))
    {
        return RowProblem{.column = kExtendedObjectColumns.names[5], .message = "the minor axis is not a number"};
    }
    if (!readNumber(fields[6], positionAngle))
    {
        return RowProblem{.column = kExtendedObjectColumns.names[6], .message = "the position angle is not a number"};
    }

    bool unknownMagnitude = false;
    if (!readMagnitude(fields[8], request.unknownMagnitudeAtOrAbove, entry.magnitude, unknownMagnitude))
    {
        return RowProblem{.column = kExtendedObjectColumns.names[8], .message = "the magnitude is not a number"};
    }
    report.unknownMagnitudeCount += unknownMagnitude ? 1u : 0u;

    entry.commonName = fields[1];
    entry.objectType = fields[7];

    const double normalized = normalizeRightAscensionDegrees(rightAscension);
    report.normalizedRightAscensionCount += (normalized != rightAscension) ? 1u : 0u;
    const double clamped = std::clamp(declination, -90.0, 90.0);
    report.clampedDeclinationCount += (clamped != declination) ? 1u : 0u;
    entry.coordinate = SkyCoordinate{.rightAscensionDegrees = normalized, .declinationDegrees = clamped};
    entry.extent = CatalogExtent{.majorAxisArcminutes = (std::max)(0.0, majorAxis),
                                 .minorAxisArcminutes = (std::max)(0.0, minorAxis),
                                 .positionAngleDegrees = positionAngle};
    return RowProblem{};
}

/// Reads one row of the star schema.
///
/// @param fields the row's fields.
/// @param request the parse policy.
/// @param entry receives the entry.
/// @param report the running counts, updated for repairs this row needed.
/// @return no problem, or the column that refused the row.
[[nodiscard]] RowProblem readStar(const RowFields& fields, const CatalogTextRequest& request, CatalogEntry& entry,
                                  CatalogTextReport& report)
{
    double rightAscension = 0.0;
    double declination = 0.0;

    if (!readNumber(fields[3], rightAscension))
    {
        return RowProblem{.column = kStarColumns.names[3], .message = "the right ascension is not a number"};
    }
    if (!readNumber(fields[4], declination))
    {
        return RowProblem{.column = kStarColumns.names[4], .message = "the declination is not a number"};
    }

    bool unknownMagnitude = false;
    if (!readMagnitude(fields[5], request.unknownMagnitudeAtOrAbove, entry.magnitude, unknownMagnitude))
    {
        return RowProblem{.column = kStarColumns.names[5], .message = "the magnitude is not a number"};
    }
    report.unknownMagnitudeCount += unknownMagnitude ? 1u : 0u;

    entry.commonName = fields[1];
    entry.designation = fields[2];
    entry.spectralType = fields[6];

    const double normalized = normalizeRightAscensionDegrees(rightAscension);
    report.normalizedRightAscensionCount += (normalized != rightAscension) ? 1u : 0u;
    const double clamped = std::clamp(declination, -90.0, 90.0);
    report.clampedDeclinationCount += (clamped != declination) ? 1u : 0u;
    entry.coordinate = SkyCoordinate{.rightAscensionDegrees = normalized, .declinationDegrees = clamped};
    return RowProblem{};
}

/// Builds the failure a malformed row produces.
///
/// @param problem what the row got wrong.
/// @param linesCompleted how many lines were read before the one that failed.
/// @return the failure.
[[nodiscard]] Status rowFailure(const RowProblem& problem, std::uint64_t linesCompleted)
{
    std::vector<StatusContextField> context;
    if (problem.column != nullptr)
    {
        context.push_back(StatusContextField::ofText(ContextKey::Parameter, problem.column));
    }
    context.push_back(StatusContextField::ofCount(ContextKey::Completed, linesCompleted));
    return Status::failure(ErrorCode::InvalidData, StatusDetail{.message = problem.message,
                                                                .operation = kOperation,
                                                                .domain = ErrorDomain::Algorithms,
                                                                .context = std::move(context)});
}

/// Whether the request names a schema and holds no zero limit.
///
/// @param request the policy to check.
/// @return success, or the first term that is not usable.
[[nodiscard]] Status checkRequest(const CatalogTextRequest& request)
{
    if (!isCatalogTextSchema(request.schema))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the catalog text schema is not one this build reads",
                              kOperation);
    }
    if (request.limits.maximumBytes == 0u || request.limits.maximumEntries == 0u ||
        request.limits.maximumLineBytes == 0u || request.limits.maximumFieldBytes == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "every catalog parse limit must be greater than zero",
                              kOperation);
    }
    if (request.unknownMagnitudeAtOrAbove.has_value() && !std::isfinite(*request.unknownMagnitudeAtOrAbove))
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the unknown-magnitude sentinel must be finite", kOperation);
    }
    return Status::success();
}

} // namespace

const char* catalogTextSchemaName(const CatalogTextSchema schema) noexcept
{
    switch (schema)
    {
    case CatalogTextSchema::ExtendedObject:
        return "ExtendedObject";
    case CatalogTextSchema::Star:
        return "Star";
    }
    return "UnknownCatalogTextSchema";
}

Result<ParsedCatalogText> parseCatalogText(const ExecutionContext& execution, const std::string_view text,
                                           const CatalogTextRequest& request)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kOperation);
    }
    if (const Status checked = checkRequest(request); checked.failed())
    {
        return checked;
    }
    if (text.size() > request.limits.maximumBytes)
    {
        return catalogLimitFailure("the catalog text is larger than the stated limit", kOperation, text.size(),
                                   request.limits.maximumBytes);
    }

    const SchemaColumns& columns = columnsOf(request.schema);
    ParsedCatalogText parsed;
    parsed.report.byteCount = text.size();

    RowFields fields;
    fields.reserve(columns.count);

    // A text ending in a newline has as many lines as it has newlines. Walking to the byte after the
    // last one would count a line that is not there and report a blank the file does not hold.
    std::size_t lineBegin = 0u;
    while (lineBegin < text.size())
    {
        if ((parsed.report.lineCount % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }

        const std::size_t lineEnd = text.find('\n', lineBegin);
        const std::size_t lineLength =
            (lineEnd == std::string_view::npos) ? (text.size() - lineBegin) : (lineEnd - lineBegin);
        const std::string_view rawLine = text.substr(lineBegin, lineLength);
        ++parsed.report.lineCount;

        if (rawLine.size() > request.limits.maximumLineBytes)
        {
            return catalogLimitFailure("a catalog line is longer than the stated limit", kOperation, rawLine.size(),
                                       request.limits.maximumLineBytes);
        }

        const std::string_view line = trimAscii(rawLine);
        if (line.empty())
        {
            ++parsed.report.blankLineCount;
        }
        else if (line.front() == '#')
        {
            ++parsed.report.commentLineCount;
        }
        else
        {
            std::uint64_t widestField = 0u;
            if (!splitRow(line, request.limits.maximumFieldBytes, fields, widestField))
            {
                return catalogLimitFailure("a catalog field is longer than the stated limit", kOperation, widestField,
                                           request.limits.maximumFieldBytes);
            }

            if (!fields.empty() && equalsFolded(trimAscii(fields.front()), "id"))
            {
                ++parsed.report.headerLineCount;
            }
            else if (fields.size() != columns.count)
            {
                return rowFailure(RowProblem{.column = nullptr,
                                             .message = "the row does not have the column count its schema declares"},
                                  parsed.report.lineCount - 1u);
            }
            else
            {
                if (parsed.report.entryCount >= request.limits.maximumEntries)
                {
                    return catalogLimitFailure("the catalog text holds more entries than the stated limit", kOperation,
                                               static_cast<std::uint64_t>(request.limits.maximumEntries) + 1u,
                                               request.limits.maximumEntries);
                }

                CatalogEntry entry;
                entry.identifier = trimAscii(fields[0]);
                if (entry.identifier.empty())
                {
                    return rowFailure(RowProblem{.column = columns.names[0], .message = "the row has no identifier"},
                                      parsed.report.lineCount - 1u);
                }

                const RowProblem problem = (request.schema == CatalogTextSchema::Star)
                                               ? readStar(fields, request, entry, parsed.report)
                                               : readExtendedObject(fields, request, entry, parsed.report);
                if (problem.message != nullptr)
                {
                    return rowFailure(problem, parsed.report.lineCount - 1u);
                }

                parsed.entries.push_back(std::move(entry));
                ++parsed.report.entryCount;
            }
        }

        if (lineEnd == std::string_view::npos)
        {
            break;
        }
        lineBegin = lineEnd + 1u;
    }

    if (parsed.entries.empty())
    {
        return catalogFailure(ErrorCode::EmptySelection, "the catalog text holds no entry", kOperation);
    }
    return parsed;
}

} // namespace an
