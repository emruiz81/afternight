// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's image combinations and its external-tool hand-off, as neutral cases: what the image blend, the LRGB
// combination and the two star-mask recombinations make of two or three images, and what StarNet and DeepSNR are
// handed and bring back.
//
// The behavior corpus already records the twenty-seven blend modes at their dialog's defaults, two LRGB settings and
// the unmasked recombination. These cases are what it does not hold: the blend's transfers, filters, grey promotion
// and kept lightness, two more LRGB settings, a masked and an edge-tightened recombination, and the hand-off, which
// the corpus cannot reach because it runs an external program. A case states one combination over neutral inputs,
// and its record is the reference result; the engine fixture executable runs the engine's operations and compares
// with the record by its classification.
//
// A record's id is `<operation>/<input>`. A hand-off case records either the image the tool is handed or the image a
// tool that changes nothing brings back. The records live in `combination.expectations` and `handoff.expectations`.

#include "FixtureExpectation.h"
#include "FixtureImage.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// Which combination a case runs.
enum class CombinationCaseKind : std::uint8_t
{
    Blend,
    Lrgb,
    Recombination,
    Handoff
};

/// The image blend's settings, as its dialog states them.
struct BlendCaseSettings
{
    /// The mode, by the dialog's label.
    std::string mode = "Replace";
    /// The target's and the blend image's midtones transfers.
    double targetBlackPoint = 0.0;
    double targetHighlights = 1.0;
    double targetMidtones = 0.5;
    double blendBlackPoint = 0.0;
    double blendHighlights = 1.0;
    double blendMidtones = 0.5;
    /// The filter, by the dialog's label, and its radius, hue and saturation.
    std::string filter = "None";
    double filterRadius = 10.0;
    double filterHue = 0.0;
    double filterSaturation = 1.0;
    bool preserveLightness = false;
    double opacity = 1.0;
};

/// The LRGB combination's settings, as its dialog states them.
struct LrgbCaseSettings
{
    double lightnessWeight = 1.0;
    double redWeight = 1.0;
    double greenWeight = 1.0;
    double blueWeight = 1.0;
    double lightnessMidtones = 0.5;
    double saturationMidtones = 0.5;
};

/// A star-mask recombination's settings.
struct RecombinationCaseSettings
{
    /// The mask's fixture; empty for none.
    std::string mask;
    bool invertMask = false;
    /// The profiled process's edge tightening; zero for none.
    double tightenEdges = 0.0;
};

/// What a hand-off case records.
enum class HandoffCaseOutput : std::uint8_t
{
    /// The sixteen-bit image the tool is handed.
    Handed,
    /// The image a tool that changes nothing brings back, in the source's representation and range.
    RestoredImage,
    /// The same answer brought back as a star layer.
    RestoredStars
};

/// A hand-off's settings.
struct HandoffCaseSettings
{
    /// `none`, `unlinked` - StarNet's and DeepSNR's stretch - or `linked`, the star mask's.
    std::string stretch = "none";
    HandoffCaseOutput output = HandoffCaseOutput::Handed;
};

/// One combination over neutral inputs.
struct CombinationCase
{
    /// The operation key, `<kind>.<what>`; the id is `<operation>/<input>`.
    std::string operation;
    /// The target, the colour image, the star image or the hand-off's source, which the record names as its input.
    std::string input;
    /// The blend image, the luminance image or the starless image; empty for a hand-off.
    std::string second;
    CombinationCaseKind kind = CombinationCaseKind::Blend;
    BlendCaseSettings blend;
    LrgbCaseSettings lrgb;
    RecombinationCaseSettings recombination;
    HandoffCaseSettings handoff;
    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose calls the case follows, as `<library path>:<line>`.
    std::string source;
    /// How the engine's answer is held to the record.
    ExpectationClass classification = ExpectationClass::Tolerance;
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

/// The record files the two families live in.
inline constexpr const char* kCombinationExpectationFile = "combination.expectations";
inline constexpr const char* kHandoffExpectationFile = "handoff.expectations";

/// Every combination case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<CombinationCase>& combinationCases();

/// The case of this id, or null.
[[nodiscard]] const CombinationCase* combinationCase(std::string_view id);

/// The record file a case's record lives in.
[[nodiscard]] const char* combinationExpectationFileOf(const CombinationCase& item);

} // namespace an::tests::fixtures
