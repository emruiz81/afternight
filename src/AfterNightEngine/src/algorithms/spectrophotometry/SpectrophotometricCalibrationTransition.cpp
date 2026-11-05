// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the lineage text and the metadata transition of a spectrophotometric
// colour calibration.

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricCalibrationTransition.h>

#include <AfterNightEngine/Algorithms/Photometry/ColorCalibrationApply.h>
#include <AfterNightEngine/Image/OperationTransition.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

namespace an
{
namespace
{

/// The operation the lineage names: the whole calibration, whatever stage a caller ran on its own.
constexpr const char* kCalibrateOperation = "an::calibrateSpectrophotometricColor";

/// One number spelled at full precision.
///
/// @param value the number.
/// @return its text.
[[nodiscard]] std::string spellNumber(const double value)
{
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string("nan");
}

/// Three numbers spelled and comma-separated.
///
/// @param values the numbers.
/// @return their text.
[[nodiscard]] std::string spellTriple(const std::array<double, 3u>& values)
{
    return spellNumber(values[0]) + "," + spellNumber(values[1]) + "," + spellNumber(values[2]);
}

} // namespace

std::string spectrophotometricCalibrationLineageDetail(const SpectrophotometricCalibrationIdentity& identity,
                                                       const std::array<double, 3u>& gains,
                                                       const std::array<double, 3u>& offsets)
{
    std::string detail;
    detail += "recipe=";
    detail += kSpectrophotometricColorCalibrationAlgorithmVersion;
    detail += "; responseProfile=" + identity.responseProfileId + "@" + identity.responseProfileVersion;
    detail += "; whiteReference=" + identity.whiteReferenceId + "@" + identity.whiteReferenceVersion;
    detail += "; catalog=" + identity.catalogFamilyId + "/" + identity.catalogManifestId;
    detail += "; gains=" + spellTriple(gains);
    detail += "; offsets=" + spellTriple(offsets);
    return detail;
}

Result<MetadataTransition>
spectrophotometricCalibrationTransition(const ImageSemantics& input,
                                        const SpectrophotometricCalibrationIdentity& identity,
                                        const std::array<double, 3u>& gains, const std::array<double, 3u>& offsets)
{
    Result<ImageSemantics> semantics = colorCalibratedSemantics(input);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    return operationTransition(
        kColorCalibrationApplyEffect,
        OperationOutcome{.operation = kCalibrateOperation,
                         .detail = spectrophotometricCalibrationLineageDetail(identity, gains, offsets),
                         .semantics = std::move(*semantics)});
}

} // namespace an
