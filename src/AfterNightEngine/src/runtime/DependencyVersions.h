// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the seam the dependency report is assembled through.
//
// One function per third-party library, each defined in the layer that already includes that
// library, because that is the only place the question can be answered honestly: a version macro
// and a run-time accessor are both spellings only the library's own header provides, and a central
// table that reproduced them would be a second copy of somebody else's constants, correct until
// the day it stopped being.
//
// So the direction is the one the layer graph already allows. The runtime declares what it needs
// and assembles the report; the codec, adapter and backend layers each answer for their own
// library. Nothing here reaches upwards.
//
// Every contributor is `noexcept` and allocation-free, and writes a whole entry - the name, how
// this binary reaches the library, and whichever of the two version texts the library can be asked
// for. What a contributor cannot answer it leaves empty, which is a statement rather than a gap:
// `Core/DependencyReport.h` says what an empty text means, and
// `build/scripts/tools/dependencies.txt` records why each empty one is empty.

#pragma once

#include <AfterNightEngine/Core/DependencyReport.h>

#include <cstdint>

/// The dependency report's contributors and the three text writers they share: the runtime
/// declares them and each library's own layer defines its contributor.
namespace an::dependency_detail
{

/// Copies a version string into a report entry's text, terminated inside its capacity.
///
/// @param text where the version is written; left empty when there is nothing to write.
/// @param value the version, or null.
void setVersionText(DependencyVersionText& text, const char* value) noexcept;

/// Reads a version out of a string that carries more than a version.
///
/// A bounded scan rather than a process-wide regular expression: libtiff answers with a multi-line
/// copyright banner and LibRaw with a hyphenated build string, and both are the library's own text
/// rather than a contract. The rule is the one both cases actually need and no more of one: skip
/// to the first digit at or after the marker, then take digits and dots until anything else, and
/// drop a trailing dot. It allocates nothing, cannot fail, and reads no further than the text it
/// was given.
///
/// @param text where the version is written; left empty when the marker or a digit is not found.
/// @param banner the library's text, or null.
/// @param marker the substring the version follows, or null to scan from the beginning.
void setVersionTextFromBanner(DependencyVersionText& text, const char* banner, const char* marker) noexcept;

/// Writes `major.minor.patch` into a report entry's text.
///
/// @param text where the version is written.
/// @param majorVersion the first component.
/// @param minorVersion the second component.
/// @param patchVersion the third component.
void setVersionTextFromParts(DependencyVersionText& text, std::uint32_t majorVersion, std::uint32_t minorVersion,
                             std::uint32_t patchVersion) noexcept;

/// Reports Exiv2, which the metadata adapter links.
///
/// @param entry the report entry to fill.
void reportExiv2(DependencyVersion& entry) noexcept;

/// Reports FFmpeg, which the video sequence source and sink link.
///
/// @param entry the report entry to fill.
void reportFfmpeg(DependencyVersion& entry) noexcept;

/// Reports FFTW's single-precision build, which the transform adapter links.
///
/// @param entry the report entry to fill.
void reportFftw(DependencyVersion& entry) noexcept;

/// Reports the GNU Scientific Library, which the distribution adapter links.
///
/// @param entry the report entry to fill.
void reportGsl(DependencyVersion& entry) noexcept;

/// Reports Highway, the vector abstraction the compiled kernels are written against.
///
/// @param entry the report entry to fill.
void reportHighway(DependencyVersion& entry) noexcept;

/// Reports libjpeg-turbo, which the JPEG codec links.
///
/// @param entry the report entry to fill.
void reportLibJpeg(DependencyVersion& entry) noexcept;

/// Reports libpng, which the PNG codec links.
///
/// @param entry the report entry to fill.
void reportLibPng(DependencyVersion& entry) noexcept;

/// Reports LibRaw, which the RAW codec links.
///
/// @param entry the report entry to fill.
void reportLibRaw(DependencyVersion& entry) noexcept;

/// Reports libtiff, which the TIFF codec links.
///
/// @param entry the report entry to fill.
void reportLibTiff(DependencyVersion& entry) noexcept;

/// Reports nlohmann-json, which the JSON document adapter compiles.
///
/// @param entry the report entry to fill.
void reportNlohmannJson(DependencyVersion& entry) noexcept;

/// Reports OpenFITS, the library the FITS codec links.
///
/// @param entry the report entry to fill.
void reportOpenFits(DependencyVersion& entry) noexcept;

/// Reports OpenXISF, the library the XISF codec links.
///
/// @param entry the report entry to fill.
void reportOpenXisf(DependencyVersion& entry) noexcept;

} // namespace an::dependency_detail
