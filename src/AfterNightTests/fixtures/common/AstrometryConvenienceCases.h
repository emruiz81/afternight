// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's astrometry conveniences, as neutral cases: reading and writing sexagesimal angles, the plate solve's
// pointing and scale hints from a header, the focal length a solved scale implies, a mosaic's mean pointing, the
// observation epoch, the cards of a solver's header-only output, and the overlay's star detection channel policy.
//
// None of it is a registered action, so the behavior corpus holds none of it. A case states one computation over a
// neutral input, and its record is the reference result; the engine fixture executable runs the engine's operations
// and compares with the record by its classification.
//
// An input is the computation's whole input, so its digest proves a record was captured from it: a text is a row of
// its bytes, numbers are a row of Float64 samples, header cards are a description's cards - a text card written as the
// FITS string a header carries - and a star field is an image. Every record is a row of Float64 samples in the order
// its kind states, except a written angle, which is a row of the text's bytes. The records live in
// `astrometry-conveniences.expectations`.

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
enum class AstrometryCaseKind : std::uint8_t
{
    /// A right ascension read from text; the record is `accepted` (one or zero) and the degrees.
    ParseRightAscension,
    /// A declination read from text; the record is `accepted` and the degrees.
    ParseDeclination,
    /// A right ascension written in hours, minutes and seconds to three decimals, as the mosaic writer wrote
    /// `OBJCTRA`; the record is the text's bytes.
    FormatRightAscension,
    /// A declination written signed in degrees, minutes and seconds to three decimals; the record is the text's bytes.
    FormatDeclination,
    /// The plate solve's position hint from the cards, a solution's reference first; the record is `found`, the
    /// right ascension, the declination and the pair that stated them - `CRVAL` 0, `RA` 1, `OBJCTRA` 2, `OBJRA` 3,
    /// `TELRA` 4, or -1.
    PlateSolveHint,
    /// A frame's observed pointing from the cards, which a mosaic centres on; recorded as the hint is.
    ObservedPointing,
    /// The plate solve's scale hint from the cards; the record is `found` and the arcseconds per pixel.
    ScaleHint,
    /// The focal length a solved scale implies through the cards' pixel size; the record is `found` and the
    /// millimetres.
    DerivedFocalLength,
    /// A mosaic's mean pointing over directions; the record is `found`, the right ascension and the declination.
    MosaicCentre,
    /// The observation epoch of a `DATE-OBS` text; the record is `found` and the Julian year.
    EpochYear,
    /// The cards of a header-only solver output; the record is the card count, or -1 for a refused header, and then
    /// the number each of `kHeaderCardKeywords` states, not-a-number where none does.
    HeaderCards,
    /// The overlay's detection channel policy over a star field; the record is the attempts made, the channel of
    /// each of the first two, -1 for none, and the channel kept.
    DetectionPolicy
};

/// The keywords whose numbers a header case records, in order.
inline constexpr std::string_view kHeaderCardKeywords[] = {"CRPIX1", "CRPIX2", "CRVAL1", "CRVAL2",  "CD1_1",
                                                           "CD1_2",  "CD2_1",  "CD2_2",  "A_ORDER", "B_ORDER",
                                                           "A_2_0",  "B_0_2",  "IMAGEW", "IMAGEH"};

/// One computation over a neutral input.
struct AstrometryCase
{
    /// The operation key, `<family>.<what>`; the id is `<operation>/<input>`.
    std::string operation;
    /// The input's name.
    std::string input;
    AstrometryCaseKind kind = AstrometryCaseKind::ParseRightAscension;
    /// A derived focal length's solved scale, in arcseconds per pixel.
    double solvedScale = 0.0;

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
    /// Why a tolerance or a bound is what it is.
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + input;
    }
};

/// The record file every case's record lives in.
inline constexpr const char* kAstrometryConvenienceExpectationFile = "astrometry-conveniences.expectations";

/// The neutral inputs the cases name, in a stable order; names are unique and none is a catalogue fixture's.
[[nodiscard]] const std::vector<FixtureImage>& astrometryConvenienceInputs();

/// The input of this name, or null.
[[nodiscard]] const FixtureImage* astrometryConvenienceInput(std::string_view name);

/// Every case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<AstrometryCase>& astrometryConvenienceCases();

/// The case of this id, or null.
[[nodiscard]] const AstrometryCase* astrometryConvenienceCase(std::string_view id);

/// The text a text input's samples hold: one byte per sample of a one-row UInt8 image.
[[nodiscard]] std::string astrometryInputText(const FixtureSamples& samples);

/// The numbers a numeric input's samples hold: one row of Float64 samples.
[[nodiscard]] std::vector<double> astrometryInputNumbers(const FixtureSamples& samples);

/// A card's value as a header carries it: a text card as a FITS string - quoted, an inner quote doubled - and any
/// other card as its value.
[[nodiscard]] std::string astrometryCardValue(const FixtureCard& card);

/// The number a header card's value states: surrounding blanks and one pair of quotes removed, then exactly one
/// decimal number; nothing otherwise.
[[nodiscard]] std::optional<double> astrometryCardNumber(std::string_view value) noexcept;

/// A row of Float64 samples holding the numbers, which every case but a written angle records.
[[nodiscard]] FixtureSamples astrometryRow(const std::vector<double>& values);

/// A row of UInt8 samples holding a text's bytes, which a written angle records.
[[nodiscard]] FixtureSamples astrometryTextRow(std::string_view text);

} // namespace an::tests::fixtures
