// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a spectrophotometric colour calibration was made against, for the
/// record: the recipe's name and the six identity tokens a calibrated image's lineage carries.
///
/// A header of its own, including nothing but `<string>`, because two headers need it and neither
/// may inherit the other's closure: the calibration plan carries the identity, and the transition
/// builder that records it reaches the whole metadata model, which the plan must not.

#pragma once

#include <string>

namespace an
{

/// The stable name of the spectrophotometric colour-calibration recipe this build implements.
///
/// Sampled Gaia XP spectra, the synthetic photometry `kSyntheticPhotometryAlgorithmVersion` names,
/// the repeated-median fit anchored on a white reference and a shared-pedestal sky, under this
/// engine's conventions: positions with the first pixel centre at one half, a sky window that is not
/// clamped to the unit interval, and a floating apply. Named in the lineage entry
/// `spectrophotometricCalibrationTransition` builds, which the caller applies to the calibrated
/// image; the calibration itself writes no metadata.
inline constexpr const char* kSpectrophotometricColorCalibrationAlgorithmVersion =
    "spcc-sampled-xp-color-calibration-v3";

/// What a calibration was made against, for the record.
///
/// Identity tokens compared byte for byte by whoever reads the lineage; never parsed here.
struct SpectrophotometricCalibrationIdentity
{
    /// The response profile's identity.
    std::string responseProfileId{};

    /// The response profile's version.
    std::string responseProfileVersion{};

    /// The white reference's identity.
    std::string whiteReferenceId{};

    /// The white reference's version.
    std::string whiteReferenceVersion{};

    /// The spectrum catalog's family.
    std::string catalogFamilyId{};

    /// The spectrum catalog's manifest identity.
    std::string catalogManifestId{};
};

} // namespace an
