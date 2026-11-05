// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The epoch a catalog's moving stars are carried to: the Julian epoch of the exposure's start.
///
/// The codecs project `DATE-OBS` into the acquisition record's exposure start, and the engine counts the Julian epoch
/// from J2000.0 at noon (`julianEpochYear`).
#pragma once

#include <AfterNightEngine/Algorithms/Astrometry/AstrometryHints.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <optional>

namespace an::ui
{

/// The Julian epoch of an image's exposure start.
///
/// @param acquisition the image's acquisition record.
/// @return the epoch in Julian years, or nothing when the record states no exposure start.
[[nodiscard]] inline std::optional<double> resolveObservationEpochYear(const an::AcquisitionMetadata& acquisition)
{
    const std::optional<an::UtcInstant> start = acquisition.exposureStart();
    if (!start.has_value())
    {
        return std::nullopt;
    }
    return an::julianEpochYear(*start);
}

} // namespace an::ui
