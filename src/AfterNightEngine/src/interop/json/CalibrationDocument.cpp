// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the colour-calibration document read and written through nlohmann-json, and
// that library's version report.
//
// A read compares the `schema` member before it reads any field, so a document of another schema -
// or one naming none - is refused whole rather than half-read. Every exception the library throws
// is caught here: a parse or type error on read is `ErrorCode::InvalidData`, a type error on write
// can only be text that is not UTF-8 and is `ErrorCode::InvalidArgument`, and anything else crosses
// the engine's exception boundary. The writer builds an ordinary `nlohmann::json` object, which holds
// its members sorted by key, and dumps it compactly; that is where the member order and the number
// spelling the header promises come from.

#include "interop/json/CalibrationDocument.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Status.h>

#include "runtime/DependencyVersions.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <exception>
#include <string>

namespace an
{
namespace
{
constexpr ExternalBoundary kBoundary{.library = "nlohmann-json", .operation = "calibration document"};

std::string textMember(const nlohmann::json& document, const char* key)
{
    return document.value(key, std::string());
}

std::array<double, 3u> numberMembers(const nlohmann::json& document, const char* key, std::array<double, 3u> values)
{
    const auto found = document.find(key);
    if (found == document.end() || !found->is_array())
        return values;
    for (std::size_t i = 0u; i < values.size() && i < found->size(); ++i)
        if ((*found)[i].is_number())
            values[i] = (*found)[i].get<double>();
    return values;
}
} // namespace

Result<CalibrationDocument> parseCalibrationDocument(std::string_view text)
{
    try
    {
        const nlohmann::json document = nlohmann::json::parse(text.begin(), text.end());
        if (textMember(document, "schema") != kCalibrationDocumentSchema)
            return Status::failure(ErrorCode::Unsupported,
                                   {.message = "the document names a schema other than the calibration record's",
                                    .operation = "an::parseCalibrationDocument",
                                    .domain = ErrorDomain::Interop});
        CalibrationDocument fields;
        fields.calibrationState = textMember(document, "calibration_state");
        fields.gains = numberMembers(document, "gains", fields.gains);
        fields.offsets = numberMembers(document, "offsets", fields.offsets);
        fields.responseProfileId = textMember(document, "response_profile_id");
        fields.responseProfileVersion = textMember(document, "response_profile_version");
        fields.whiteReferenceId = textMember(document, "white_reference_id");
        fields.whiteReferenceVersion = textMember(document, "white_reference_version");
        fields.catalogFamilyId = textMember(document, "catalog_family_id");
        fields.catalogManifestId = textMember(document, "catalog_manifest_id");
        fields.catalogAccessProfileId = textMember(document, "catalog_access_profile_id");
        fields.algorithmVersion = textMember(document, "algorithm_version");
        fields.inputLinearity = textMember(document, "input_linearity");
        fields.inputChannelBasis = textMember(document, "input_channel_basis");
        return fields;
    }
    catch (const nlohmann::json::exception&)
    {
        // Parse and type errors are what a malformed document is; the library's text for them says where,
        // which a person reading a header cannot act on, so the code alone travels.
        return Status::failure(ErrorCode::InvalidData,
                               {.message = "the text is not a JSON object whose members have the calibration "
                                           "schema's types",
                                .operation = "an::parseCalibrationDocument",
                                .domain = ErrorDomain::Interop});
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

Result<std::string> writeCalibrationDocument(const CalibrationDocument& fields)
{
    try
    {
        nlohmann::json document;
        document["schema"] = std::string(kCalibrationDocumentSchema);
        document["calibration_state"] = fields.calibrationState;
        document["gains"] = {fields.gains[0], fields.gains[1], fields.gains[2]};
        document["offsets"] = {fields.offsets[0], fields.offsets[1], fields.offsets[2]};
        document["response_profile_id"] = fields.responseProfileId;
        document["response_profile_version"] = fields.responseProfileVersion;
        document["white_reference_id"] = fields.whiteReferenceId;
        document["white_reference_version"] = fields.whiteReferenceVersion;
        document["catalog_family_id"] = fields.catalogFamilyId;
        document["catalog_manifest_id"] = fields.catalogManifestId;
        document["catalog_access_profile_id"] = fields.catalogAccessProfileId;
        document["algorithm_version"] = fields.algorithmVersion;
        document["input_linearity"] = fields.inputLinearity;
        document["input_channel_basis"] = fields.inputChannelBasis;
        return document.dump();
    }
    catch (const nlohmann::json::type_error&)
    {
        // The only type error serialising strings and numbers can raise is text that is not UTF-8.
        return Status::failure(ErrorCode::InvalidArgument, {.message = "a calibration record's text is not UTF-8",
                                                            .operation = "an::writeCalibrationDocument",
                                                            .domain = ErrorDomain::Interop});
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

namespace dependency_detail
{

void reportNlohmannJson(DependencyVersion& entry) noexcept
{
    entry.name = "nlohmann-json";
    // Header-only, so there is no second copy that could differ: what the headers declared is what
    // is inside this binary, and there is nothing loaded to ask.
    entry.linkage = DependencyLinkage::HeaderOnly;
    setVersionTextFromParts(entry.compiled, NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
                            NLOHMANN_JSON_VERSION_PATCH);
}

} // namespace dependency_detail

} // namespace an
