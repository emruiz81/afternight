// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// Neutral FITS input files: written and read without the engine.
//
// A consumer test's inputs are files as often as they are buffers, and a file written without the engine is an input
// whose bytes the engine did not choose. The writer here produces a single primary image from a description's
// samples, with the canonical FITS offsets for the unsigned sixteen- and thirty-two-bit and the signed eight-bit
// representations, a ROWORDER card saying which row is stored first - the top one unless the caller asks for a
// bottom-up file - `BAYERPAT` for a filter array, and the description's own cards. The reader reads a primary image
// the way the engine does - the same offsets, a bottom-up file turned top-down - so it can also check what the engine
// wrote, independently of the engine that wrote it.

#include "FixtureImage.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// The operation an engine performs on a neutral FITS input when it decodes it, as an expectation names it, and
/// the checked-in file holding those expectations.
inline constexpr const char* kFitsDecodeOperation = "fits-decode";
inline constexpr const char* kFitsDecodeOperationText = "decode the neutral FITS file written from the input";
inline constexpr const char* kFitsDecodeExpectationFile = "fits-decode.expectations";

/// One header card as a file holds it: the keyword, its value with a string's quotes removed and doubled
/// quotes undone, and its comment. A commentary card - COMMENT, HISTORY, a blank keyword - keeps its text as
/// the comment and has no value.
struct FitsHeaderCard
{
    std::string keyword;
    std::string value;
    std::string comment;
};

/// What `readFixtureFits` found in a file's primary image.
struct FixtureFitsFile
{
    std::vector<FitsHeaderCard> cards;
    /// The samples in top-down row order, in the representation the stored BITPIX and offset name.
    FixtureSamples samples;
    /// The filter array BAYERPAT names, or `FixtureCfa::None`.
    FixtureCfa cfa = FixtureCfa::None;

    /// The value of the first card with this keyword, or nothing.
    [[nodiscard]] std::optional<std::string> card(std::string_view keyword) const;
};

/// In which order a file stores the rows: the description's top row first, or its bottom row first, as many
/// capture programs write them. The file's ROWORDER card says which, so every reader turns it back.
enum class FixtureRowOrder : std::uint8_t
{
    TopDown,
    BottomUp
};

/// Writes a description's samples as a FITS file holding one primary image.
///
/// @return false, with the reason in `error`, when the samples are not the description's shape and
///         representation, the description is refused by `checkFixture`, or the file cannot be written.
[[nodiscard]] bool writeFixtureFits(const FixtureImage& image, const FixtureSamples& samples,
                                    const std::filesystem::path& path, std::string& error,
                                    FixtureRowOrder rowOrder = FixtureRowOrder::TopDown);

/// Reads a FITS file's primary image.
///
/// Refused, with the reason in `error`: a file that does not start with a FITS primary header or ends before
/// its header or data do; a BITPIX other than 8, 16, 32, -32 and -64; NAXIS other than 2, or 3 with one or three
/// planes; a BSCALE other than 1; a BZERO other than zero or the canonical offset of the stored width.
[[nodiscard]] std::optional<FixtureFitsFile> readFixtureFits(const std::filesystem::path& path, std::string& error);

} // namespace an::tests::fixtures
