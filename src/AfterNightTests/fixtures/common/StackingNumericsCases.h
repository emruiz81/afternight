// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's stacking and drizzle numerics, as neutral cases: the comet composite's split, mask and blend, what the
// workflow counted from a stack's rejection maps, the drizzle canvas the three workflow stages computed, and the
// exposure and signal-to-noise weights the standalone stacking derived.
//
// None of it is a registered action, so the behavior corpus holds none of it. A case states one computation over
// neutral inputs, and its record is the reference result; the engine fixture executable runs the engine's operations
// and compares with the record by its classification.
//
// The inputs are this table's own: comet frames and stacks, rejection count maps, and frames that only state an
// extent, none of them a catalogue fixture. A record's id is `<operation>/<input>`. A comet case records an image; a
// map, canvas or weighting case records its numbers as one row of Float64 samples, in the order its kind states. The
// records live in `comet-composite.expectations` and `stacking-numerics.expectations`.

#include "FixtureExpectation.h"
#include "FixtureImage.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// Which computation a case runs.
enum class StackingNumericsCaseKind : std::uint8_t
{
    /// One frame split at a comet's aperture; the record is the star layer or the comet layer.
    CometSplit,
    /// The comet mask over every frame's projected aperture; the record is the mask.
    CometMask,
    /// A comet stack blended into a star stack through the mask; the record is the composite.
    CometBlend,
    /// The workflow's counts over a low and a high rejection map; the record is `lowElements`, `highElements`,
    /// `eitherElements`, `eitherFraction`, the clusters found on both maps, the largest cluster's pixels and its
    /// fraction of the map.
    RejectionMaps,
    /// The drizzle canvas; the record is the canvas's corner, width and height before the scale, the drizzled width
    /// and height, the edge guard the stack crops, and the analysis plane's width and height.
    DrizzleCanvas,
    /// The standalone stacking's weights; the record is one weight per frame.
    Weighting
};

/// One frame's comet aperture: where it is on its source frame and how the frame's star registration moves it.
struct CometCaseAperture
{
    /// The centre on the source frame, in engine pixel coordinates (the first pixel's centre at one half).
    double x = 0.0;
    double y = 0.0;
    /// The radius in source pixels.
    double radius = 0.0;
    /// The frame's star registration, a translation onto the star stack's plane.
    double dx = 0.0;
    double dy = 0.0;
};

/// One frame's registration in a canvas case: a translation, or a rotation about the origin and a translation.
struct CanvasCaseFrame
{
    double dx = 0.0;
    double dy = 0.0;
    double degrees = 0.0;
};

/// One computation over neutral inputs.
struct StackingNumericsCase
{
    /// The operation key, `<kind>.<what>`; the id is `<operation>/<input>`.
    std::string operation;
    /// The frame, the star stack, the low map or the frame stating an extent, which the record names as its input.
    std::string input;
    /// The comet stack or the high map; empty otherwise.
    std::string second;
    StackingNumericsCaseKind kind = StackingNumericsCaseKind::CometSplit;

    /// The split's aperture, or every frame's aperture for a mask or a blend.
    std::vector<CometCaseAperture> apertures;
    /// Whether a split records its comet layer rather than its star layer.
    bool cometLayer = false;
    /// The drizzle scale of a mask, a blend or a canvas.
    double scale = 1.0;
    /// The reference frame's comet registration in a blend, a translation onto the comet stack's plane.
    double cometDx = 0.0;
    double cometDy = 0.0;

    /// The stack's frame count, by which the product divided a map's counts, and the fewest rejections counted.
    std::uint32_t frames = 0u;
    std::uint32_t minimum = 1u;

    /// A canvas case's alignment mode - `standard`, `mosaic` or `intersection` - its drop fraction, its frames'
    /// registrations, its reference frame, and the plane a comet-aligned canvas is drawn on instead.
    std::string mode = "standard";
    double pixelFraction = 1.0;
    std::vector<CanvasCaseFrame> canvasFrames;
    std::uint32_t referenceIndex = 0u;
    std::optional<std::array<double, 4>> plane;

    /// A weighting case's mode - `exposure` or `snr` - and every frame's figure.
    std::string weighting;
    std::vector<double> figures;

    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose calls the case follows, as `<library path>:<line>`.
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

/// The record files the two families live in.
inline constexpr const char* kCometCompositeExpectationFile = "comet-composite.expectations";
inline constexpr const char* kStackingNumericsExpectationFile = "stacking-numerics.expectations";

/// The neutral inputs the cases name, in a stable order; names are unique and none is a catalogue fixture's.
[[nodiscard]] const std::vector<FixtureImage>& stackingNumericsInputs();

/// The input of this name, or null.
[[nodiscard]] const FixtureImage* stackingNumericsInput(std::string_view name);

/// Every case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<StackingNumericsCase>& stackingNumericsCases();

/// The case of this id, or null.
[[nodiscard]] const StackingNumericsCase* stackingNumericsCase(std::string_view id);

/// The record file a case's record lives in.
[[nodiscard]] const char* stackingNumericsExpectationFileOf(const StackingNumericsCase& item);

/// Whether a canvas case's alignment mode is one the product names.
[[nodiscard]] bool stackingNumericsCanvasModeKnown(std::string_view mode) noexcept;

} // namespace an::tests::fixtures
