// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CombinationCases.h"

#include <tuple>
#include <utility>

namespace an::tests::fixtures
{

namespace
{

constexpr const char* kBlendSource = "src/AfterNightProcesses/processes/ImageBlendProcess.cpp:463";
constexpr const char* kLrgbSource = "src/AfterNightProcesses/processes/LRGBCombinationProcess.cpp:285";
constexpr const char* kRecombinationSource = "src/AfterNightProcesses/processes/StarmaskRecombinationProcess.cpp:373";
constexpr const char* kProfiledSource =
    "src/AfterNightProcesses/processes/ProfiledStarmaskRecombinationProcess.cpp:753";
constexpr const char* kStarnetSource = "src/AfterNightProcesses/processes/StarnetProcess.cpp:373";
constexpr const char* kStarMaskSource = "src/AfterNightProcesses/processes/StarnetStarMaskProcess.cpp:386";

CombinationCase blendCase(std::string operation, std::string input, std::string second, BlendCaseSettings settings,
                          std::string text, ExpectationClass classification, double absolute, std::string note,
                          std::string acceptedReason = {}, double acceptedBound = 0.0)
{
    CombinationCase item;
    item.operation = std::move(operation);
    item.input = std::move(input);
    item.second = std::move(second);
    item.kind = CombinationCaseKind::Blend;
    item.blend = std::move(settings);
    item.text = std::move(text);
    item.source = kBlendSource;
    item.classification = classification;
    if (classification == ExpectationClass::Tolerance)
    {
        item.absoluteTolerance = absolute;
    }
    item.acceptedReason = std::move(acceptedReason);
    item.acceptedBound = acceptedBound;
    item.note = std::move(note);
    return item;
}

std::vector<CombinationCase> build()
{
    std::vector<CombinationCase> cases;
    constexpr auto kTolerance = ExpectationClass::Tolerance;
    constexpr auto kAccepted = ExpectationClass::Accepted;

    // ---- The image blend: what the corpus's default-setting records do not reach.
    BlendCaseSettings transfers;
    transfers.mode = "Multiply";
    transfers.targetBlackPoint = 0.02;
    transfers.targetHighlights = 0.95;
    transfers.targetMidtones = 0.3;
    transfers.blendHighlights = 0.9;
    transfers.blendMidtones = 0.6;
    cases.push_back(blendCase("blend.multiply-transfers", "scene-rgb-float32", "scene-rgb-float32-second", transfers,
                              "the Multiply mode over the second scene, the target stretched by shadows 0.02, "
                              "highlights 0.95 and midtones 0.3 and the blend image by highlights 0.9 and midtones 0.6",
                              kTolerance, 1.0e-6,
                              "both transfers are the same midtones function, the record's in Float32 and the "
                              "engine's in double; they agree on this scene, and the tolerance is the corpus records' "
                              "own"));

    BlendCaseSettings gaussian;
    gaussian.filter = "Gaussian Blur";
    gaussian.filterRadius = 1.5;
    cases.push_back(blendCase("blend.replace-gaussian-1.5", "scene-rgb-float32", "scene-rgb-float32-second", gaussian,
                              "the Replace mode over the second scene blurred at radius 1.5", kAccepted, 0.0,
                              "the bound is the three box averages' distance from the Gaussian they approximate",
                              "the record approximates the blur with three box averages and the engine runs the "
                              "exact Gaussian",
                              0.006));

    BlendCaseSettings narrow;
    narrow.filter = "Gaussian Blur";
    narrow.filterRadius = 0.4;
    cases.push_back(blendCase("blend.replace-gaussian-0.4", "scene-rgb-float32", "scene-rgb-float32-second", narrow,
                              "the Replace mode over the second scene blurred at radius 0.4", kAccepted, 0.0,
                              "the record's three boxes all have radius zero below about half a pixel, so it is "
                              "not blurred, and the engine blurs",
                              "the record is not blurred below a radius of about half a pixel and the engine runs "
                              "the exact Gaussian",
                              0.02));

    BlendCaseSettings highPass;
    highPass.mode = "Overlay";
    highPass.filter = "High Pass";
    highPass.filterRadius = 2.0;
    cases.push_back(blendCase("blend.overlay-high-pass-2", "scene-rgb-float32", "scene-rgb-float32-second", highPass,
                              "the Overlay mode over the second scene high-passed at radius 2", kAccepted, 0.0,
                              "the bound is the box blur's distance from the Gaussian, through the high pass and the "
                              "overlay",
                              "the record's high pass subtracts three box averages and the engine's the exact "
                              "Gaussian",
                              0.008));

    BlendCaseSettings hue;
    hue.mode = "Screen";
    hue.filter = "Hue";
    hue.filterHue = 200.0;
    hue.filterSaturation = 0.5;
    cases.push_back(blendCase("blend.screen-hue-filter", "scene-rgb-float32", "scene-rgb-float32-second", hue,
                              "the Screen mode over the second scene's lightness colourized at hue 200 and saturation "
                              "0.5",
                              kTolerance, 1.0e-6,
                              "the colourized lightness comes out the same in the record and the engine on this "
                              "scene; the tolerance is the corpus records' own"));

    BlendCaseSettings color;
    color.mode = "Lighten/Max";
    color.filter = "Color";
    color.filterHue = 30.0;
    color.filterSaturation = 0.8;
    cases.push_back(blendCase("blend.lighten-color-filter", "scene-rgb-float32", "scene-rgb-float32-second", color,
                              "the Lighten mode over the second scene colourized channel by channel at hue 30 and "
                              "saturation 0.8",
                              kTolerance, 1.0e-6,
                              "the colourize is the same HSV arithmetic in the record and the engine"));

    BlendCaseSettings greyTarget;
    greyTarget.mode = "Multiply";
    cases.push_back(blendCase("blend.multiply-grey-target", "scene-mono-float32", "scene-rgb-float32-second",
                              greyTarget, "the Multiply mode of a grey target by a colour scene", kTolerance, 1.0e-6,
                              "the grey target is three equal channels in the record and the engine"));

    BlendCaseSettings greyLightness;
    greyLightness.mode = "Lightness";
    cases.push_back(blendCase("blend.lightness-grey-blend", "scene-rgb-float32", "scene-mono-float32-second",
                              greyLightness, "the Lightness mode with a grey blend image as the lightness itself",
                              kTolerance, 5.0e-5,
                              "the lightness round trip goes through an XYZ matrix and its inverse, the record's "
                              "inverse a printed approximation; measured at 3.4e-5"));

    BlendCaseSettings greyBlend;
    greyBlend.mode = "Screen";
    cases.push_back(blendCase("blend.screen-grey-blend", "scene-rgb-float32", "scene-mono-float32-second", greyBlend,
                              "the Screen mode over a grey blend image as three equal channels", kTolerance, 1.0e-6,
                              "the grey blend image is three equal channels in the record and the engine"));

    BlendCaseSettings sixteen;
    sixteen.mode = "Difference";
    cases.push_back(blendCase("blend.difference-uint16-target", "scene-rgb-uint16", "scene-rgb-float32-second", sixteen,
                              "the Difference mode of a sixteen-bit target from a Float32 scene", kTolerance, 1.0e-6,
                              "the sixteen-bit target is read as its fraction of full scale in the record and the "
                              "engine"));

    BlendCaseSettings kept;
    kept.mode = "Soft Light";
    kept.targetMidtones = 0.4;
    kept.preserveLightness = true;
    kept.opacity = 0.6;
    cases.push_back(blendCase("blend.soft-light-kept-lightness-opacity", "scene-rgb-float32",
                              "scene-rgb-float32-second", kept,
                              "the Soft Light mode over the second scene, the target stretched by midtones 0.4, its "
                              "lightness kept, at opacity 0.6",
                              kTolerance, 5.0e-5,
                              "the kept lightness goes through an XYZ matrix and its inverse, the record's inverse "
                              "a printed approximation; measured at 1.5e-5"));

    // ---- The LRGB combination: two settings beyond the corpus's.
    for (const auto& [operation, text, settings] :
         {std::tuple{"lrgb.chroma-midtones-0.35", "the mono scene as luminance, saturation midtones 0.35 alone",
                     LrgbCaseSettings{.saturationMidtones = 0.35}},
          std::tuple{"lrgb.lightness-weight-0.25-midtones-0.6-blue-0.7",
                     "the mono scene as luminance at lightness weight 0.25, lightness midtones 0.6, blue weight 0.7",
                     LrgbCaseSettings{.lightnessWeight = 0.25, .blueWeight = 0.7, .lightnessMidtones = 0.6}}})
    {
        CombinationCase item;
        item.operation = operation;
        item.input = "scene-rgb-float32";
        item.second = "scene-mono-float32";
        item.kind = CombinationCaseKind::Lrgb;
        item.lrgb = settings;
        item.text = text;
        item.source = kLrgbSource;
        item.classification = kTolerance;
        item.absoluteTolerance = 5.0e-5;
        item.note = "the record inverts its XYZ matrix through a printed approximation and the engine inverts the "
                    "matrix it used; measured at up to 2.9e-5";
        cases.push_back(std::move(item));
    }

    // ---- The recombinations: a mask, an inverted mask, and the profiled process's edge tightening.
    for (const auto& [operation, text, settings, source] :
         {std::tuple{"recombination.masked", "the starless background plus the star image through a grey mask",
                     RecombinationCaseSettings{.mask = "stars-mono-float32"}, kRecombinationSource},
          std::tuple{"recombination.masked-inverted",
                     "the starless background plus the star image through an inverted grey mask",
                     RecombinationCaseSettings{.mask = "stars-mono-float32", .invertMask = true}, kRecombinationSource},
          std::tuple{"recombination.tightened",
                     "the starless background plus the star image with its edges tightened by 0.5",
                     RecombinationCaseSettings{.tightenEdges = 0.5}, kProfiledSource},
          std::tuple{"recombination.tightened-masked",
                     "the starless background plus the star image with its edges tightened by 0.8, through a grey mask",
                     RecombinationCaseSettings{.mask = "stars-mono-float32", .tightenEdges = 0.8}, kProfiledSource}})
    {
        CombinationCase item;
        item.operation = operation;
        item.input = "stars-rgb-float32";
        item.second = "stars-rgb-float32-starless";
        item.kind = CombinationCaseKind::Recombination;
        item.recombination = settings;
        item.text = text;
        item.source = source;
        if (settings.tightenEdges > 0.0)
        {
            item.classification = kAccepted;
            item.acceptedReason = "the record finds the stars' edges over a linear sample index, so a row's end "
                                  "reads the next row's start; the engine's stencil reads a zero border there";
            item.acceptedBound = settings.mask.empty() ? 0.03 : 0.006;
            item.note = "the bound is measured at the row ends; every other pixel agrees within the rounding, which "
                        "the expectation test holds separately";
        }
        else
        {
            item.classification = kTolerance;
            item.absoluteTolerance = 1.0e-6;
            item.note = "the sum and the blend are the same arithmetic, the record rounding the sum to Float32 first";
        }
        cases.push_back(std::move(item));
    }

    // ---- The external-tool hand-off: the image handed, and the image a tool that changes nothing brings back.
    struct Handoff
    {
        const char* operation;
        const char* input;
        const char* stretch;
        HandoffCaseOutput output;
        const char* text;
        const char* source;
    };
    for (const Handoff& handoff :
         {Handoff{"handoff.handed", "scene-rgb-float32", "none", HandoffCaseOutput::Handed,
                  "the sixteen-bit image StarNet is handed, unstretched", kStarnetSource},
          Handoff{"handoff.restored", "scene-rgb-float32", "none", HandoffCaseOutput::RestoredImage,
                  "the handed image brought back as StarNet's starless answer, unstretched", kStarnetSource},
          Handoff{"handoff.handed", "scene-rgb-float32-unbounded", "none", HandoffCaseOutput::Handed,
                  "the sixteen-bit image StarNet is handed, mapped into the unit interval", kStarnetSource},
          Handoff{"handoff.restored", "scene-rgb-float32-unbounded", "none", HandoffCaseOutput::RestoredImage,
                  "the handed image brought back as StarNet's starless answer, mapped back", kStarnetSource},
          Handoff{"handoff.restored-stars", "scene-rgb-float32-unbounded", "none", HandoffCaseOutput::RestoredStars,
                  "the handed image brought back as StarNet's star layer, mapped back by the span", kStarnetSource},
          Handoff{"handoff.handed-unlinked", "stars-rgb-float32", "unlinked", HandoffCaseOutput::Handed,
                  "the sixteen-bit image StarNet is handed, stretched channel by channel", kStarnetSource},
          Handoff{"handoff.restored-unlinked", "stars-rgb-float32", "unlinked", HandoffCaseOutput::RestoredImage,
                  "the handed image brought back as StarNet's starless answer through the inverse stretch",
                  kStarnetSource},
          Handoff{"handoff.handed-linked", "stars-rgb-float32", "linked", HandoffCaseOutput::Handed,
                  "the sixteen-bit image the StarNet star mask is handed, stretched linked", kStarMaskSource},
          Handoff{"handoff.handed-unlinked", "scene-rgb-uint16", "unlinked", HandoffCaseOutput::Handed,
                  "the sixteen-bit image StarNet is handed from sixteen-bit codes, stretched channel by channel",
                  kStarnetSource},
          Handoff{"handoff.restored-unlinked", "scene-rgb-uint16", "unlinked", HandoffCaseOutput::RestoredImage,
                  "the handed image brought back as sixteen-bit codes through the inverse stretch", kStarnetSource}})
    {
        CombinationCase item;
        item.operation = handoff.operation;
        item.input = handoff.input;
        item.kind = CombinationCaseKind::Handoff;
        item.handoff = HandoffCaseSettings{.stretch = handoff.stretch, .output = handoff.output};
        item.text = handoff.text;
        item.source = handoff.source;
        const bool stretched = item.handoff.stretch != "none";
        const bool handed = item.handoff.output == HandoffCaseOutput::Handed;
        const bool integer = item.input == "scene-rgb-uint16";
        if (stretched && handed)
        {
            item.classification = kAccepted;
            item.acceptedReason = "the record derives its stretch from histogram estimates of the median and "
                                  "deviation and truncates to its codes; the engine derives it from exact statistics "
                                  "and rounds to nearest";
            item.acceptedBound = item.handoff.stretch == "linked" ? 48.0 : (integer ? 200.0 : 170.0);
            item.note = "the bound is measured; the curve is steep at the background, where a small move of its "
                        "shadow point moves many codes";
        }
        else if (handed)
        {
            item.absoluteTolerance = 1.0;
            item.note = "the record truncates to its sixteen-bit codes and the engine rounds to nearest, one code "
                        "apart at most";
        }
        else if (stretched)
        {
            item.absoluteTolerance = integer ? 5.0 : 1.0e-4;
            item.note = "the record and the engine each bring their own stretch back, the record through sixteen-bit "
                        "codes it requantizes the inverse to; measured at 4 codes and 6.2e-5";
        }
        else
        {
            item.absoluteTolerance = item.input == "scene-rgb-float32" ? 1.6e-5 : 3.5e-5;
            item.note = "one sixteen-bit code, over the range's span for a mapped image: the record truncates the "
                        "handed codes and the engine rounds them";
        }
        cases.push_back(std::move(item));
    }
    return cases;
}

} // namespace

const std::vector<CombinationCase>& combinationCases()
{
    static const std::vector<CombinationCase> cases = build();
    return cases;
}

const CombinationCase* combinationCase(std::string_view id)
{
    for (const CombinationCase& item : combinationCases())
    {
        if (item.id() == id)
        {
            return &item;
        }
    }
    return nullptr;
}

const char* combinationExpectationFileOf(const CombinationCase& item)
{
    return item.kind == CombinationCaseKind::Handoff ? kHandoffExpectationFile : kCombinationExpectationFile;
}

} // namespace an::tests::fixtures
