// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's mask rendering and mask drawing, as neutral cases: what the viewer paints over an image to show
// its mask, and what the drawing tools do to a mask plane.
//
// The viewer's mask overlay is a mask made renderable, converted to bytes, inverted and multiplied; the drawing tools
// compute a brush tip, dab placement, dab blend, layer blend and shape feather. A case states one rendering or one
// drawing over neutral inputs, and its record is the reference result; the engine fixture executable runs the
// engine's operations and compares with the record by its classification.
//
// An overlay record's id is `<operation>/<mask>`; a drawing record's is `<operation>/<input>`, the input being the
// paint plane a stroke lands on or the coverage a feather blurs. They live in `mask-overlay.expectations` and
// `drawing.expectations`.

#include "FixtureExpectation.h"
#include "FixtureImage.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// What the viewer's mask overlay shows.
enum class MaskOverlayCaseMode : std::uint8_t
{
    /// A colour whose opacity is the mask's complement.
    Tint,
    /// The mask itself.
    Replace,
    /// The image multiplied by the mask.
    Multiply
};

/// One rendering of a mask.
struct MaskOverlayCase
{
    /// The operation key, `overlay.<mode>` with `-inverted` for an inverted mask; the id is `<operation>/<mask>`.
    std::string operation;
    /// The mask, which the record names as its input.
    std::string mask;
    /// The image a multiply shows; empty for the other modes.
    std::string image;
    MaskOverlayCaseMode mode = MaskOverlayCaseMode::Tint;
    bool invert = false;
    /// The tint's red, green and blue.
    std::uint8_t red = 255u;
    std::uint8_t green = 0u;
    std::uint8_t blue = 0u;
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
    /// For an accepted case, the largest difference the comparison allows once the record is restated in the
    /// operation's shape, in the record's units; the note says what the restatement is.
    double acceptedBound = 0.0;
    /// Why a tolerance or a bound is what it is.
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + mask;
    }
};

/// What a drawing case draws.
enum class DrawingCaseKind : std::uint8_t
{
    /// A brush stroke through the case's points, blended into the paint plane.
    Stroke,
    /// A shape's coverage feathered and screened over a paint plane.
    Feather
};

/// How a stroke combines with the paint plane: the drawing tools' five modes.
enum class DrawingBlendMode : std::uint8_t
{
    Normal,
    Multiply,
    Screen,
    Overlay,
    Erase
};

/// One point of a stroke, with the pressure its segment is stamped at.
struct DrawingStrokePoint
{
    double x = 0.0;
    double y = 0.0;
    float pressure = 1.0f;
};

/// One drawing over a plane.
struct DrawingCase
{
    /// The operation key, `drawing.<what>`; the id is `<operation>/<input>`.
    std::string operation;
    /// For a stroke, the paint plane the stroke lands on; for a feather, the image the shape's coverage is made
    /// from.
    std::string input;
    /// For a feather, the paint plane the feathered shape is screened over; empty for a stroke.
    std::string base;
    DrawingCaseKind kind = DrawingCaseKind::Stroke;
    /// The brush: diameter, hardness, a square tip or a round one, spacing and flow.
    float diameter = 20.0f;
    float hardness = 1.0f;
    bool square = false;
    float spacing = 0.25f;
    float flow = 1.0f;
    /// The stroke's points, in order.
    std::vector<DrawingStrokePoint> points;
    /// How the stroke lands, and at what opacity; a feathered shape's opacity.
    DrawingBlendMode mode = DrawingBlendMode::Normal;
    float opacity = 1.0f;
    /// The feather width in pixels.
    float feather = 0.0f;
    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose code the case follows, as `<library path>:<line>`.
    std::string source;
    ExpectationClass classification = ExpectationClass::Exact;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    std::string acceptedReason;
    /// For an accepted case, the largest difference the comparison allows; the note says why.
    double acceptedBound = 0.0;
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + input;
    }
};

/// The record files the two families live in.
inline constexpr const char* kMaskOverlayExpectationFile = "mask-overlay.expectations";
inline constexpr const char* kDrawingExpectationFile = "drawing.expectations";

/// Every overlay case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<MaskOverlayCase>& maskOverlayCases();

/// The overlay case of this id, or null.
[[nodiscard]] const MaskOverlayCase* maskOverlayCase(std::string_view id);

/// Every drawing case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<DrawingCase>& drawingCases();

/// The drawing case of this id, or null.
[[nodiscard]] const DrawingCase* drawingCase(std::string_view id);

/// A plane as the drawing document imports it: the image's first channel held to the unit interval, row-major and
/// tightly packed. A sample that is not a number stays one, as the product's clamp lets it through.
[[nodiscard]] std::vector<float> importedPlane(const FixtureSamples& samples);

/// A shape's coverage made from an image: the image imported as a plane and then quantized to bytes, as a shape the
/// product rasterizes reaches its layer through an eight-bit alpha; a sample that is not a number is no coverage.
[[nodiscard]] std::vector<float> shapeCoverage(const FixtureSamples& samples);

} // namespace an::tests::fixtures
