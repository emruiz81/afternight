// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Core/Result.h>

#include <array>
#include <string>
#include <string_view>

// AfterNightEngine - the colour-calibration record as a JSON document.
//
// A product's header carries its colour-calibration record twice: as cards, each of which holds no
// more than one card's worth of value, and as this document, hex-encoded across numbered cards so
// that a long identity survives. This adapter reads and writes the document and nothing else. Its fields stay
// text here - the adapter knows the document's shape and nothing of what a spelling means, so the
// domain that owns the record decides how each one reads - and the library's own value type and
// exceptions never leave this directory.

namespace an
{

/// The schema a calibration document names.
inline constexpr std::string_view kCalibrationDocumentSchema = "afternight.color_calibration_provenance.v1";

/// A calibration document's fields, as the document spells them.
struct CalibrationDocument
{
    /// `calibration_state`.
    std::string calibrationState{};

    /// `gains`: an entry that is absent or not a number keeps its default.
    std::array<double, 3u> gains{1.0, 1.0, 1.0};

    /// `offsets`: an entry that is absent or not a number keeps its default.
    std::array<double, 3u> offsets{0.0, 0.0, 0.0};

    /// `response_profile_id`.
    std::string responseProfileId{};

    /// `response_profile_version`.
    std::string responseProfileVersion{};

    /// `white_reference_id`.
    std::string whiteReferenceId{};

    /// `white_reference_version`.
    std::string whiteReferenceVersion{};

    /// `catalog_family_id`.
    std::string catalogFamilyId{};

    /// `catalog_manifest_id`.
    std::string catalogManifestId{};

    /// `catalog_access_profile_id`.
    std::string catalogAccessProfileId{};

    /// `algorithm_version`.
    std::string algorithmVersion{};

    /// `input_linearity`.
    std::string inputLinearity{};

    /// `input_channel_basis`.
    std::string inputChannelBasis{};
};

/// Reads a calibration document.
///
/// The text must be one JSON object. A member the schema reads as text must be a string when it is
/// present, and one it reads as numbers is used only when it is an array; a member that is absent
/// leaves its field's default.
///
/// @param text the document.
/// @return the fields; `ErrorCode::InvalidData` when the text is not a JSON object or a text member is
///         not a string; `ErrorCode::Unsupported` when the object names another schema or none; or
///         the translation of an allocation failure.
[[nodiscard]] Result<CalibrationDocument> parseCalibrationDocument(std::string_view text);

/// Writes a calibration document.
///
/// One compact JSON object naming the schema, its members in lexicographic order of their keys and its
/// numbers in their shortest round-trip form - the library's own serialisation, so one record always
/// writes the same text.
///
/// @param document the fields.
/// @return the text; `ErrorCode::InvalidArgument` when a text field is not UTF-8; or the translation of
///         an allocation failure.
[[nodiscard]] Result<std::string> writeCalibrationDocument(const CalibrationDocument& document);

} // namespace an
