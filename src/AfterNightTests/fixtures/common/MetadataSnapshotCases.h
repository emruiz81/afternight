// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's frame-kind reading, its combined-image metadata merge and its history steps, as neutral cases.
//
// The behavior corpus reaches none of the three: the workflow's frame detection runs while files are imported, the
// merge after a combination process's own engine calls, and a history step's snapshot beside a process rather than
// in one. A case states one computation over neutral inputs, and its record is the reference result; the engine
// fixture executable runs the engine's operations and compares with the record by its classification.
//
// A frame-kind case's input carries the cards a file states and its record is one code. A combination case names its
// first source as its input and the others beside it, every source a header's cards over a small frame; its record is
// what the combined image's header states, read back as a header is read, in the order
// `kCombinationKeywords` gives. A history case's input is an image and its record is the image restored from the
// history step. The records live in `metadata-and-snapshots.expectations`.

#include "FixtureExpectation.h"
#include "FixtureImage.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// Which computation a case runs.
enum class MetadataSnapshotCaseKind : std::uint8_t
{
    /// A frame's kind from the cards it states; the record is one sample, bias 0, dark 1, flat 2, light 3, or -1 when
    /// no card states a kind.
    FrameKind,
    /// The metadata of an image combined from the input and the other sources; the record is the exposure the
    /// combined header states as read back, not-a-number when none, and then, for each of `kCombinationKeywords`, one
    /// when the header states the keyword and zero when it does not, and the number its value states, not-a-number
    /// when it states none.
    Combination,
    /// An image kept as a history step and restored from it; the record is the restored image.
    HistoryRoundTrip
};

/// The keywords a combination case records, in order.
inline constexpr std::string_view kCombinationKeywords[] = {"OBJECT",   "INSTRUME", "TELESCOP", "FILTER",  "CCD-TEMP",
                                                            "IMAGETYP", "GAIN",     "OFFSET",   "SITELAT", "FOCALLEN",
                                                            "DATE-OBS", "AIRMASS",  "EXPTIME",  "ANLIN",   "CRVAL1"};

/// One computation over neutral inputs.
struct MetadataSnapshotCase
{
    /// The operation key, `<family>.<what>`; the id is `<operation>/<input>`.
    std::string operation;
    /// The input's name: the frame stating its kind, the combination's first source, or the image kept.
    std::string input;
    /// A combination's other sources, in order.
    std::vector<std::string> others;
    MetadataSnapshotCaseKind kind = MetadataSnapshotCaseKind::FrameKind;

    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose code the case follows, as `<library path>:<line>`.
    std::string source;
    /// How the engine's answer is held to the record.
    ExpectationClass classification = ExpectationClass::Exact;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    /// Why the answers differ, for an accepted case.
    std::string acceptedReason;
    /// For an accepted case, the largest difference the comparison allows, in the record's units.
    double acceptedBound = 0.0;
    /// For an accepted case, every sample of the record the engine's answer differs in, in order; a sample is the
    /// same when both are not-a-number.
    std::vector<std::uint32_t> differing;
    /// Why a bound is what it is.
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + input;
    }
};

/// The record file every case's record lives in.
inline constexpr const char* kMetadataSnapshotExpectationFile = "metadata-and-snapshots.expectations";

/// The neutral inputs the cases name, in a stable order; names are unique and none is a catalogue fixture's.
[[nodiscard]] const std::vector<FixtureImage>& metadataSnapshotInputs();

/// The input of this name, or null.
[[nodiscard]] const FixtureImage* metadataSnapshotInput(std::string_view name);

/// Every case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<MetadataSnapshotCase>& metadataSnapshotCases();

/// The case of this id, or null.
[[nodiscard]] const MetadataSnapshotCase* metadataSnapshotCase(std::string_view id);

/// A card's value as a header carries it: a text card as a FITS string - quoted, an inner quote doubled - and any
/// other card as its value.
[[nodiscard]] std::string metadataCardValue(const FixtureCard& card);

/// The number a header card's value states: surrounding blanks and one pair of quotes removed, then exactly one
/// decimal number; nothing otherwise.
[[nodiscard]] std::optional<double> metadataCardNumber(std::string_view value) noexcept;

/// The code a frame-kind record holds for the product's kind spelling - `BIAS`, `DARK`, `FLAT` or `LIGHT` - and -1 for
/// any other text, the empty one included.
[[nodiscard]] double frameKindCode(std::string_view kind) noexcept;

/// A row of Float64 samples holding the numbers, which a frame-kind and a combination case record.
[[nodiscard]] FixtureSamples metadataRow(const std::vector<double>& values);

} // namespace an::tests::fixtures
