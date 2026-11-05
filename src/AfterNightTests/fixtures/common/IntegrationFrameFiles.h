// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Acquisition frames as capture programs write them, for the integration workflow's suites: written and read without
// the engine.
//
// The workflow sorts its inputs by what each file says about itself - the kind of frame, the exposure, the sensor
// temperature, the gain and offset, the filter, the target, the camera and when it was taken - and groups and matches
// them on those facts. A frame here is a neutral description (`FixtureImage`) whose cards state exactly those facts,
// spelled as the programs spell them, and a file is that description written by the neutral FITS writer, so the
// engine reads a file it did not write. A case states the facts it is about and leaves out the rest: a fact left
// empty writes no card, which is how a file that says nothing is described.

#include "FixtureImage.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::fixtures
{

/// What a capture program states about one frame. Every field left empty writes no card.
struct AcquisitionFacts
{
    /// The keyword that states the frame's kind - `IMAGETYP`, or INDI's `FRAME` - and its value as the program spells
    /// it (`Light Frame`, `Dark`, `Bias`, `Flat Field`).
    std::string kindKeyword = "IMAGETYP";
    std::string kind;

    /// The exposure in seconds, and the keyword it is written under (`EXPTIME` or `EXPOSURE`).
    std::optional<double> exposureSeconds;
    std::string exposureKeyword = "EXPTIME";

    /// `CCD-TEMP`, in degrees Celsius.
    std::optional<double> sensorTemperatureCelsius;

    /// `GAIN` and `ISOSPEED`, the camera's settings in its own units, and `OFFSET`.
    std::optional<std::int64_t> gain;
    std::optional<std::int64_t> isoSpeed;
    std::optional<std::int64_t> offset;

    /// `FILTER`, `OBJECT` and `INSTRUME`, as the program writes them - a sentinel such as `N/A` included.
    std::string filter;
    std::string object;
    std::string instrument;

    /// `DATE-OBS`, the exposure start in UTC, and `DATE`, when the file was written, in the FITS spelling
    /// `YYYY-MM-DDThh:mm:ss`.
    std::string exposureStart;
    std::string fileDate;
};

/// The cards a capture program writes for these facts, in the order above.
[[nodiscard]] std::vector<FixtureCard> acquisitionCards(const AcquisitionFacts& facts);

/// A small grey frame, unsigned sixteen-bit at a constant level, carrying these facts' cards.
///
/// @param name the description's name.
/// @param facts what the frame states.
/// @param width the frame's width in pixels.
/// @param height the frame's height in pixels.
/// @return the description; a case may change its samples' pattern before writing it.
[[nodiscard]] FixtureImage acquisitionFrame(std::string name, const AcquisitionFacts& facts, std::uint32_t width = 16u,
                                            std::uint32_t height = 12u);

/// Writes a frame as the neutral FITS writer writes any description, creating the directories it is in.
///
/// @return false, with the reason in `error`, when the description is refused or the file cannot be written.
[[nodiscard]] bool writeAcquisitionFrame(const FixtureImage& frame, const std::filesystem::path& path,
                                         std::string& error);

} // namespace an::tests::fixtures
