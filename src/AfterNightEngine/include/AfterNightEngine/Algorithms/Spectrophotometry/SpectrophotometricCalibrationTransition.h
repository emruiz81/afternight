// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The metadata transition a spectrophotometrically calibrated frame takes.
///
/// **The transition is built here and applied by the caller.** `Image/OperationTransition.h` turns an
/// operation's declared effect and its outcome into the one `MetadataTransition` an image accepts;
/// this header states that outcome for a spectrophotometric calibration - the colour-calibrated
/// meaning from `Algorithms/Photometry/ColorCalibrationApply.h` and a lineage entry naming the
/// recipe, the identities and the numbers - and hands the transition back for `Image::transition`.
///
/// **It is a header of its own so the calibration plan stays light.** Building a transition reaches
/// the whole metadata model; the plan and the operations that run it name the identity through
/// `SpectrophotometricCalibrationIdentity.h` and nothing more.

#pragma once

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectrophotometricCalibrationIdentity.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>

#include <array>
#include <string>

namespace an
{

/// The one-line text a calibration's lineage entry carries.
///
/// The recipe name, the six identity tokens and the six numbers under six labels in a fixed order -
/// recipe, response profile, white reference, catalog, gains, offsets - with every number spelled
/// with seventeen significant digits, enough to read back the same double. For a person and for a
/// byte-for-byte comparison; never parsed by this engine.
///
/// Thread-safe. Allocates the returned text.
///
/// @param identity what the calibration was made against.
/// @param gains the per-channel gains that were written, red, green and blue.
/// @param offsets the per-channel offsets that were written, red, green and blue.
/// @return the text, within `kMaxProvenanceTextBytes` for identities of ordinary length.
[[nodiscard]] AFTERNIGHTENGINE_API std::string
spectrophotometricCalibrationLineageDetail(const SpectrophotometricCalibrationIdentity& identity,
                                           const std::array<double, 3u>& gains, const std::array<double, 3u>& offsets);

/// The metadata transition a spectrophotometrically calibrated frame takes.
///
/// `operationTransition` over `kColorCalibrationApplyEffect` with the input's meaning restated by
/// `colorCalibratedSemantics` and a lineage entry naming `calibrateSpectrophotometricColor` and
/// carrying `spectrophotometricCalibrationLineageDetail`. The one door from a calibration result to
/// an image's metadata.
///
/// Thread-safe. Allocates the lineage text.
///
/// @param input what the source frame meant.
/// @param identity what the calibration was made against.
/// @param gains the per-channel gains that were written, red, green and blue.
/// @param offsets the per-channel offsets that were written, red, green and blue.
/// @return the transition, ready for `Image::transition`, or whatever `colorCalibratedSemantics` or
///         `operationTransition` refused.
[[nodiscard]] AFTERNIGHTENGINE_API Result<MetadataTransition>
spectrophotometricCalibrationTransition(const ImageSemantics& input,
                                        const SpectrophotometricCalibrationIdentity& identity,
                                        const std::array<double, 3u>& gains, const std::array<double, 3u>& offsets);

} // namespace an
