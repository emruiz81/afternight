// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "MaskRenderingCases.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace an::tests::fixtures
{

namespace
{

constexpr const char* kViewer = "src/AfterNightControls/controls/ImageViewControl.cpp";
constexpr const char* kBrush = "src/AfterNightControls/controls/drawing/BrushEngine.cpp";
constexpr const char* kShapes = "src/AfterNightControls/controls/drawing/ShapeEngine.cpp";

constexpr const char* kTruncation = "the record truncates a byte and the operation rounds it to nearest";

MaskOverlayCase overlay(std::string operation, std::string mask, MaskOverlayCaseMode mode, bool invert,
                        std::string text, int line)
{
    MaskOverlayCase made;
    made.operation = std::move(operation);
    made.mask = std::move(mask);
    made.mode = mode;
    made.invert = invert;
    made.text = std::move(text);
    made.source = std::string(kViewer) + ":" + std::to_string(line);
    made.classification = ExpectationClass::Tolerance;
    made.absoluteTolerance = 1.0;
    made.note = kTruncation;
    return made;
}

std::vector<MaskOverlayCase> buildOverlays()
{
    std::vector<MaskOverlayCase> cases;
    const std::string tint = "the viewer's tinted mask overlay: the colour, its alpha the mask's complement";
    const std::string tintInverted = "the viewer's tinted overlay of the inverted mask: the colour, its alpha the mask";
    const std::string replace = "the viewer's replace overlay: the mask as eight-bit samples";
    const std::string replaceInverted = "the viewer's replace overlay of the inverted mask: the negative of its bytes";
    const std::string multiply = "the viewer's multiply overlay: the image's samples times the mask's, as bytes";
    const std::string multiplyInverted =
        "the viewer's multiply overlay of the inverted mask: the image times the mask's negative, as bytes";

    cases.push_back(overlay("overlay.tint", "scene-mono-float32", MaskOverlayCaseMode::Tint, false, tint, 6121));
    cases.push_back(
        overlay("overlay.tint-inverted", "scene-mono-float32", MaskOverlayCaseMode::Tint, true, tintInverted, 6121));

    // An eight-bit mask: the record's alpha is the code's complement, and so is the operation's.
    MaskOverlayCase bytes = overlay("overlay.tint", "scene-mono-uint8", MaskOverlayCaseMode::Tint, false, tint, 6140);
    bytes.red = 255u;
    bytes.green = 255u;
    bytes.blue = 0u;
    bytes.classification = ExpectationClass::Exact;
    bytes.absoluteTolerance = 0.0;
    bytes.note.clear();
    cases.push_back(std::move(bytes));

    // Not-a-number and the infinities: the record and the operation hold them to the unit interval before the byte.
    cases.push_back(overlay("overlay.tint", "nonfinite-mono-float32", MaskOverlayCaseMode::Tint, false, tint, 6121));

    // A 32-bit mask, which the record divides by the largest code as a float, which is two to the 32nd.
    MaskOverlayCase wide =
        overlay("overlay.tint", "gradient-mono-uint32", MaskOverlayCaseMode::Tint, false, tint, 6121);
    wide.note = std::string(kTruncation) + ", and divides a 32-bit code by two to the 32nd rather than its largest";
    cases.push_back(std::move(wide));

    // A colour mask: the record holds one layer per channel stacked by the toolkit; the operation writes the stacked
    // opacity in one layer.
    MaskOverlayCase colour = overlay("overlay.tint", "scene-rgb-uint16", MaskOverlayCaseMode::Tint, false, tint, 6121);
    colour.classification = ExpectationClass::Accepted;
    colour.absoluteTolerance = 0.0;
    colour.acceptedReason =
        "the record writes one tinted layer per channel of a colour mask and the toolkit paints them stacked; the "
        "operation writes one layer whose opacity is the stack's, one minus the product of the channels";
    colour.acceptedBound = 2.0;
    colour.note = "the record's three layers stacked exactly, one minus the product of their alphas' complements, "
                  "against the operation's alpha: a truncation in each layer and a rounding in the operation";
    cases.push_back(std::move(colour));

    // A signed mask: the record reads a signed code through its offset, the operation as a signed fraction.
    MaskOverlayCase signedMask =
        overlay("overlay.tint", "scene-mono-int16", MaskOverlayCaseMode::Tint, false, tint, 6121);
    signedMask.classification = ExpectationClass::Accepted;
    signedMask.absoluteTolerance = 0.0;
    signedMask.acceptedReason =
        "the record reads a signed integer mask through its offset convention, so its lowest code is an empty "
        "mask; the operation reads it as a signed fraction of full scale, so every code below zero is an empty mask";
    signedMask.note = "the values are reported, not bounded";
    cases.push_back(std::move(signedMask));

    cases.push_back(
        overlay("overlay.replace", "scene-mono-float32", MaskOverlayCaseMode::Replace, false, replace, 6160));
    cases.push_back(overlay("overlay.replace-inverted", "scene-rgb-float32", MaskOverlayCaseMode::Replace, true,
                            replaceInverted, 6160));
    cases.push_back(
        overlay("overlay.replace", "scene-rgb-float32-unbounded", MaskOverlayCaseMode::Replace, false, replace, 6160));

    MaskOverlayCase product =
        overlay("overlay.multiply", "scene-mono-float32", MaskOverlayCaseMode::Multiply, false, multiply, 6219);
    product.image = "scene-rgb-float32";
    cases.push_back(std::move(product));
    MaskOverlayCase productInverted = overlay("overlay.multiply-inverted", "scene-mono-float32",
                                              MaskOverlayCaseMode::Multiply, true, multiplyInverted, 6219);
    productInverted.image = "scene-rgb-float32";
    cases.push_back(std::move(productInverted));
    MaskOverlayCase colourProduct =
        overlay("overlay.multiply", "scene-rgb-float32-second", MaskOverlayCaseMode::Multiply, false, multiply, 6219);
    colourProduct.image = "scene-rgb-float32";
    cases.push_back(std::move(colourProduct));

    // An eight-bit mask over a sixteen-bit image: the record promotes the mask, multiplies in sixteen bits and
    // truncates twice on the way to a byte.
    MaskOverlayCase integerProduct =
        overlay("overlay.multiply", "scene-mono-uint8", MaskOverlayCaseMode::Multiply, false, multiply, 6219);
    integerProduct.image = "scene-rgb-uint16";
    integerProduct.absoluteTolerance = 2.0;
    integerProduct.note = "the record truncates the sixteen-bit product and then its byte; the operation rounds "
                          "the byte of the exact product once";
    cases.push_back(std::move(integerProduct));
    return cases;
}

DrawingCase stroke(std::string operation, DrawingBlendMode mode, std::string text)
{
    DrawingCase made;
    made.operation = std::move(operation);
    made.input = "stars-mono-float32";
    made.kind = DrawingCaseKind::Stroke;
    made.diameter = 6.5f;
    made.hardness = 0.4f;
    made.spacing = 0.15f;
    made.flow = 0.6f;
    made.points = {{.x = 3.5, .y = 4.25, .pressure = 1.0f},
                   {.x = 10.75, .y = 6.5, .pressure = 0.8f},
                   {.x = 18.25, .y = 12.0, .pressure = 0.6f},
                   {.x = 26.5, .y = 9.75, .pressure = 1.0f},
                   {.x = 29.0, .y = 18.5, .pressure = 0.4f}};
    made.mode = mode;
    made.opacity = 0.8f;
    made.text = std::move(text);
    made.source = std::string(kBrush) + ":107";
    return made;
}

DrawingCase dab(std::string operation, double x, double y, float diameter, float hardness, bool square, float flow)
{
    DrawingCase made;
    made.operation = std::move(operation);
    made.input = "stars-mono-float32";
    made.kind = DrawingCaseKind::Stroke;
    made.diameter = diameter;
    made.hardness = hardness;
    made.square = square;
    made.flow = flow;
    made.points = {{.x = x, .y = y, .pressure = 1.0f}};
    made.text = "one dab of the brush tip at full opacity, which writes the tip times the flow over the paint";
    made.source = std::string(kBrush) + ":191";
    return made;
}

DrawingCase featherCase(std::string operation, float width, float opacity)
{
    DrawingCase made;
    made.operation = std::move(operation);
    made.input = "stars-mono-float32";
    made.base = "stars-mono-float32-starless";
    made.kind = DrawingCaseKind::Feather;
    made.feather = width;
    made.opacity = opacity;
    made.text = "a shape's eight-bit coverage feathered, scaled by the shape's opacity and screened over the paint";
    made.source = std::string(kShapes) + ":566";
    return made;
}

std::vector<DrawingCase> buildDrawings()
{
    std::vector<DrawingCase> cases;
    cases.push_back(dab("drawing.dab.round-hard", 12.4, 9.6, 7.0f, 1.0f, false, 1.0f));
    cases.push_back(dab("drawing.dab.round-soft", 15.5, 11.5, 11.0f, 0.3f, false, 0.8f));
    cases.push_back(dab("drawing.dab.square-soft-even", 20.75, 8.25, 8.0f, 0.5f, true, 1.0f));

    cases.push_back(stroke("drawing.stroke.normal", DrawingBlendMode::Normal,
                           "a five-point stroke dabbed along its Catmull-Rom segments and blended normally at 0.8"));
    cases.push_back(stroke("drawing.stroke.multiply", DrawingBlendMode::Multiply,
                           "a five-point stroke dabbed along its Catmull-Rom segments and multiplied in at 0.8"));
    cases.push_back(stroke("drawing.stroke.screen", DrawingBlendMode::Screen,
                           "a five-point stroke dabbed along its Catmull-Rom segments and screened in at 0.8"));
    cases.push_back(stroke("drawing.stroke.overlay", DrawingBlendMode::Overlay,
                           "a five-point stroke dabbed along its Catmull-Rom segments and overlaid at 0.8"));
    cases.push_back(stroke("drawing.stroke.erase", DrawingBlendMode::Erase,
                           "a five-point stroke dabbed along its Catmull-Rom segments and erased at 0.8"));

    // A point repeated and a release on the last point: each still closes a segment, and a degenerate one dabs once.
    DrawingCase repeated = stroke("drawing.stroke.repeated-points", DrawingBlendMode::Normal,
                                  "a stroke with a repeated point and a release on its last point, which still dab");
    repeated.points = {{.x = 6.0, .y = 6.0, .pressure = 1.0f},
                       {.x = 6.0, .y = 6.0, .pressure = 1.0f},
                       {.x = 14.5, .y = 15.25, .pressure = 0.7f},
                       {.x = 22.0, .y = 15.25, .pressure = 0.7f},
                       {.x = 22.0, .y = 15.25, .pressure = 0.7f}};
    repeated.diameter = 5.0f;
    repeated.hardness = 0.0f;
    repeated.square = true;
    repeated.spacing = 0.5f;
    repeated.flow = 0.9f;
    repeated.opacity = 1.0f;
    cases.push_back(std::move(repeated));

    // No feather: the coverage is screened over the paint at its opacity, which the record writes as one minus the
    // product of the complements and the operation as a screen moved by the opacity.
    DrawingCase unfeathered = featherCase("drawing.feather.none", 0.0f, 0.7f);
    unfeathered.classification = ExpectationClass::Tolerance;
    unfeathered.absoluteTolerance = 2.0e-7;
    unfeathered.note = "a screen of the coverage scaled by the opacity against a screen moved by the opacity: one "
                       "value by two orders of Float32 arithmetic";
    cases.push_back(std::move(unfeathered));

    const std::string boxes =
        "the record feathers with three box averages along each axis, which leave a shape unblurred below a "
        "width of about 1.15 pixels; the operation feathers with the Gaussian of half the width, never below half a "
        "pixel";
    DrawingCase thin = featherCase("drawing.feather.1", 1.0f, 1.0f);
    thin.classification = ExpectationClass::Accepted;
    thin.acceptedReason = boxes;
    thin.acceptedBound = 0.15;
    thin.note = "the record leaves the shape unblurred and the operation blurs it with a half-pixel Gaussian, "
                "which moves an edge sample by up to about a fifth of a step";
    cases.push_back(std::move(thin));
    DrawingCase medium = featherCase("drawing.feather.3", 3.0f, 0.7f);
    medium.classification = ExpectationClass::Accepted;
    medium.acceptedReason = boxes;
    medium.acceptedBound = 0.008;
    medium.note = "three unit boxes against the Gaussian of standard deviation 1.5 they approximate";
    cases.push_back(std::move(medium));
    DrawingCase broad = featherCase("drawing.feather.8", 8.0f, 1.0f);
    broad.classification = ExpectationClass::Accepted;
    broad.acceptedReason = boxes;
    broad.acceptedBound = 0.006;
    broad.note = "three boxes against the Gaussian of standard deviation four they approximate";
    cases.push_back(std::move(broad));
    return cases;
}

} // namespace

const std::vector<MaskOverlayCase>& maskOverlayCases()
{
    static const std::vector<MaskOverlayCase> cases = buildOverlays();
    return cases;
}

const MaskOverlayCase* maskOverlayCase(std::string_view id)
{
    for (const MaskOverlayCase& candidate : maskOverlayCases())
    {
        if (candidate.id() == id)
        {
            return &candidate;
        }
    }
    return nullptr;
}

const std::vector<DrawingCase>& drawingCases()
{
    static const std::vector<DrawingCase> cases = buildDrawings();
    return cases;
}

const DrawingCase* drawingCase(std::string_view id)
{
    for (const DrawingCase& candidate : drawingCases())
    {
        if (candidate.id() == id)
        {
            return &candidate;
        }
    }
    return nullptr;
}

std::vector<float> importedPlane(const FixtureSamples& samples)
{
    std::vector<float> plane(static_cast<std::size_t>(samples.width()) * samples.height(), 0.0f);
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            plane[static_cast<std::size_t>(y) * samples.width() + x] =
                static_cast<float>(std::clamp(samples.value(x, y, 0u), 0.0, 1.0));
        }
    }
    return plane;
}

std::vector<float> shapeCoverage(const FixtureSamples& samples)
{
    std::vector<float> coverage = importedPlane(samples);
    for (float& sample : coverage)
    {
        if (std::isnan(sample))
        {
            sample = 0.0f;
            continue;
        }
        const long alpha = std::lround(static_cast<double>(sample) * 255.0);
        sample = static_cast<float>(alpha) / 255.0f;
    }
    return coverage;
}

} // namespace an::tests::fixtures
