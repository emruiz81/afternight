// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the real-time preview of every stretch-based dialog, and of brightness and contrast with and without a mask,
/// shows, held exactly to the references under `src/AfterNightTests/fixtures/references/realtime-preview/`.
///
/// The cases are those the recorded behaviour expectations hold for the histogram transformation and its automatic
/// stretches, the generalized hyperbolic transformation and its screen transfer, the arcsinh and statistical stretches,
/// the curves, the gamma correction and brightness and contrast - each mode over each scene it was recorded on - and
/// brightness and contrast through three masks. Each opens its dialog over the scene, sets the mode, turns the preview
/// on and puts the preview view in autostretch, and once everything has landed reads three things: the preview's
/// samples, the statistics the preview view presented them with, and the counts of every histogram the dialog draws.
/// Each is compared exactly with its reference, and so are the case's description and its scene's digests.
///
/// A build other than the one the references were captured on reads its own reference where one stands beside them,
/// under `<compiler>-<platform>/`, as the recorded behaviour expectations do.
///
/// With AFTERNIGHT_FIXTURE_CAPTURE naming a directory, the cases write fresh references below it instead, under
/// `references/realtime-preview/` - and, on a build other than the one the checked-in references came from, its own
/// references for the cases it does not reproduce under `references/realtime-preview/<compiler>-<platform>/` - to be
/// reviewed and checked in; AFTERNIGHT_FIXTURE_CAPTURE_COMMIT names the commit the libraries were built from.

#include "Test_ImageRTPreviewControl/ProcessDialogSupport.h"

#include "processes/AdjustBrightnessAndContrastProcess.h"
#include "processes/ArcSinhStretchProcess.h"
#include "processes/CurvesTransformationProcess.h"
#include "processes/GHTransformationProcess.h"
#include "processes/GammaCorrectionProcess.h"
#include "processes/HistogramTransformationProcess.h"
#include "processes/StatisticalStretch.h"
#include "widgets/CurvesWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ToneCurveModel.h"

#include "fixtures/common/FixtureExpectation.h"

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>

#include <QCheckBox>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::dialogs;

/// The fields of a statistics reference, in order, for each colour channel the analysis measured.
constexpr const char* kStatisticsFields =
    "median, median absolute deviation, samples measured, neutralization offset, calibration gain, calibration offset, "
    "measured, neutralized, calibrated, promotion, taken from subsampled samples";
constexpr std::uint32_t kStatisticsFieldCount = 11u;

/// One dialog the references cover: the corpus file its cases come from, which also names its references' file, what
/// it is called, how it is opened, and how it is set to one of the corpus's modes.
struct ReferenceDialog
{
    const char* records;
    const char* title;
    DialogHandle (*open)(ImageViewControl& target);
    void (*configure)(ImageRTPreviewControl& dialog, std::string_view mode);
};

/// One case: a dialog's file, its mode as the corpus names it, the scene it runs over, and the scene the target's
/// mask is made from, if it has one.
struct ReferenceCase
{
    const char* records;
    const char* mode;
    const char* scene;
    const char* mask = nullptr;
};

void failUnknownMode(const char* dialog, std::string_view mode)
{
    ADD_FAILURE() << dialog << " has no mode " << std::string(mode);
}

// ---- How each dialog is set to the corpus's modes, as a person sets its widgets.

void configureHistogramTransformation(ImageRTPreviewControl& dialog, std::string_view mode)
{
    struct Setting
    {
        const char* mode;
        double shadows = 0.0;
        double midtones = 0.5;
        double highlights = 1.0;
        const char* channel = "RGB";
        const char* automatic = nullptr;
    };
    static constexpr Setting kSettings[] = {
        {.mode = "default"},
        {.mode = "shadows-0.02-midtones-0.25-highlights-0.95", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95},
        {.mode = "red-only", .shadows = 0.02, .midtones = 0.25, .highlights = 0.95, .channel = "Red"},
        {.mode = "autostretch-linked", .automatic = "autostretchLinked"},
        {.mode = "autostretch-unlinked", .automatic = "autostretchUnlinked"}};
    for (const Setting& setting : kSettings)
    {
        if (mode != setting.mode)
        {
            continue;
        }
        if (setting.automatic != nullptr)
        {
            // The automatic stretches are offered while the preview is shown.
            setChecked(dialog, "preview", true);
            EXPECT_TRUE(settleDialog(dialog));
            if (auto* button = child<QAbstractButton>(dialog, setting.automatic))
            {
                EXPECT_TRUE(button->isEnabled()) << setting.automatic;
                button->click();
            }
            return;
        }
        if (offersChoice(dialog, "channelSelector2"))
        {
            choose(dialog, "channelSelector2", QString::fromLatin1(setting.channel));
        }
        setReal(dialog, "shadowsValue", setting.shadows);
        setReal(dialog, "midtonesValue", setting.midtones);
        setReal(dialog, "highlightsValue", setting.highlights);
        return;
    }
    failUnknownMode("the histogram transformation", mode);
}

void configureGeneralizedHyperbolic(ImageRTPreviewControl& dialog, std::string_view mode)
{
    struct Setting
    {
        const char* mode;
        const char* transformation = "Generalized Hyperbolic";
        bool invert = false;
        const char* channel = "RGB";
        double stretchFactor = 2.0;
        double localIntensity = 1.5;
        double symmetryPoint = 0.1;
        double blackPoint = 0.0;
        bool linkedStf = false;
    };
    static constexpr Setting kSettings[] = {
        {.mode = "default", .stretchFactor = 0.0, .localIntensity = 0.0, .symmetryPoint = 0.0},
        {.mode = "hyperbolic-factor-2-intensity-1.5-symmetry-0.1"},
        {.mode = "hyperbolic-inverse", .invert = true},
        {.mode = "lightness", .channel = "Lightness"},
        {.mode = "saturation", .channel = "Saturation"},
        {.mode = "colour", .channel = "Colour"},
        {.mode = "red-only", .channel = "Red"},
        {.mode = "arcsinh", .transformation = "Arcsinh"},
        {.mode = "histogram", .transformation = "Histogram Transformation"},
        {.mode = "linear", .transformation = "Linear Stretch", .blackPoint = 0.05},
        {.mode = "inversion", .transformation = "Image Inversion"},
        {.mode = "stf-linked",
         .transformation = "STF Transformation",
         .stretchFactor = 0.0,
         .localIntensity = 0.0,
         .symmetryPoint = 0.0,
         .linkedStf = true}};
    for (const Setting& setting : kSettings)
    {
        if (mode != setting.mode)
        {
            continue;
        }
        choose(dialog, "transformationTypeSelector", QString::fromLatin1(setting.transformation));
        if (offersChoice(dialog, "channelSelector2"))
        {
            choose(dialog, "channelSelector2", QString::fromLatin1(setting.channel));
        }
        setReal(dialog, "stretchFactorValue", setting.stretchFactor);
        setReal(dialog, "localIntensityValue", setting.localIntensity);
        setReal(dialog, "protectShadowsValue", 0.0);
        setReal(dialog, "protectHighlightsValue", 1.0);
        setReal(dialog, "symmetryPointValue", setting.symmetryPoint);
        setReal(dialog, "blackPointValue", setting.blackPoint);
        setReal(dialog, "whitePointValue", 1.0);
        setChecked(dialog, "invert", setting.invert);
        setChecked(dialog, "linkedStf", setting.linkedStf);
        return;
    }
    failUnknownMode("the generalized hyperbolic transformation", mode);
}

void configureArcsinh(ImageRTPreviewControl& dialog, std::string_view mode)
{
    struct Setting
    {
        const char* mode;
        double stretchFactor;
        double blackPoint;
        bool humanWeighted;
    };
    static constexpr Setting kSettings[] = {{"stretch-0-black-point-0.001", 0.0, 0.001, true},
                                            {"stretch-50", 50.0, 0.001, true},
                                            {"stretch-50-even-luminance", 50.0, 0.001, false}};
    for (const Setting& setting : kSettings)
    {
        if (mode == setting.mode)
        {
            setReal(dialog, "stretchFactor", setting.stretchFactor);
            setReal(dialog, "blackPoint", setting.blackPoint);
            setChecked(dialog, "humanWeightedLuminance", setting.humanWeighted);
            return;
        }
    }
    failUnknownMode("the arcsinh stretch", mode);
}

void configureStatisticalStretch(ImageRTPreviewControl& dialog, std::string_view mode)
{
    struct Setting
    {
        const char* mode;
        bool linked = false;
        bool normalize = false;
        bool applyCurves = false;
        bool noBlackClip = false;
    };
    static constexpr Setting kSettings[] = {{.mode = "default"},
                                            {.mode = "linked", .linked = true},
                                            {.mode = "normalize-curves", .normalize = true, .applyCurves = true},
                                            {.mode = "no-black-clip", .noBlackClip = true}};
    for (const Setting& setting : kSettings)
    {
        if (mode == setting.mode)
        {
            setReal(dialog, "targetMedianValue", 0.25);
            setReal(dialog, "blackPointSigmaValue", 5.0);
            setReal(dialog, "curvesBoostValue", 0.2);
            setChecked(dialog, "linkedStretchCheck", setting.linked);
            setChecked(dialog, "normalizeImageCheck", setting.normalize);
            setChecked(dialog, "applyCurvesCheck", setting.applyCurves);
            setChecked(dialog, "noBlackClipCheck", setting.noBlackClip);
            return;
        }
    }
    failUnknownMode("the statistical stretch", mode);
}

/// A dialog's curve editor; its class declares no meta-object of its own, so it is found by its type.
[[nodiscard]] CurvesWidget* curvesWidgetOf(const QWidget& dialog)
{
    for (QWidget* widget : dialog.findChildren<QWidget*>())
    {
        if (auto* curves = dynamic_cast<CurvesWidget*>(widget))
        {
            return curves;
        }
    }
    return nullptr;
}

void configureCurves(ImageRTPreviewControl& dialog, std::string_view mode)
{
    static const std::vector<an::CurveControlPoint> kThreeNodes{{0.0, 0.0}, {0.3, 0.5}, {1.0, 1.0}};
    static const std::vector<an::CurveControlPoint> kFiveNodes{
        {0.0, 0.0}, {0.2, 0.3}, {0.4, 0.55}, {0.7, 0.8}, {1.0, 1.0}};
    struct Setting
    {
        const char* mode;
        const std::vector<an::CurveControlPoint>* points;
        const char* interpolation;
        const char* channel = "RGB";
    };
    const Setting settings[] = {
        {.mode = "default", .points = nullptr, .interpolation = "Cubic"},
        {.mode = "cubic-three-nodes", .points = &kThreeNodes, .interpolation = "Cubic"},
        {.mode = "linear-three-nodes", .points = &kThreeNodes, .interpolation = "Linear"},
        {.mode = "akima-five-nodes", .points = &kFiveNodes, .interpolation = "Akima"},
        {.mode = "steffen-five-nodes", .points = &kFiveNodes, .interpolation = "Steffen"},
        {.mode = "cubic-three-nodes-red", .points = &kThreeNodes, .interpolation = "Cubic", .channel = "Red"},
        {.mode = "cubic-three-nodes-cie-l",
         .points = &kThreeNodes,
         .interpolation = "Cubic",
         .channel = "CIE L* (Lightness)"},
        {.mode = "cubic-three-nodes-hsv-value",
         .points = &kThreeNodes,
         .interpolation = "Cubic",
         .channel = "HSV Value"}};
    for (const Setting& setting : settings)
    {
        if (mode != setting.mode)
        {
            continue;
        }
        choose(dialog, "interpolationSelector", QString::fromLatin1(setting.interpolation));
        if (offersChoice(dialog, "channelSelector2"))
        {
            choose(dialog, "channelSelector2", QString::fromLatin1(setting.channel));
        }
        CurvesWidget* widget = curvesWidgetOf(dialog);
        EXPECT_NE(widget, nullptr) << "the curves dialog has no curve editor";
        if (widget != nullptr && setting.points != nullptr)
        {
            widget->setCurve(an::ui::ToneCurveModel(*setting.points, widget->getCurve().interpolation()));
        }
        return;
    }
    failUnknownMode("the curves transformation", mode);
}

void configureGamma(ImageRTPreviewControl& dialog, std::string_view mode)
{
    struct ChannelMode
    {
        const char* mode;
        an::ChannelScope scope;
    };
    static constexpr ChannelMode kChannelModes[] = {
        {"channel-rgb", an::ChannelScope::Whole},
        {"channel-grayscale", an::ChannelScope::Whole},
        {"channel-red", an::ChannelScope::Red},
        {"channel-green", an::ChannelScope::Green},
        {"channel-blue", an::ChannelScope::Blue},
        {"channel-cie-l", an::ChannelScope::CieLightness},
        {"channel-cie-a", an::ChannelScope::CieA},
        {"channel-cie-b", an::ChannelScope::CieB},
        {"channel-cie-c", an::ChannelScope::CieChroma},
        {"channel-cie-h", an::ChannelScope::CieHue},
        {"channel-cie-x", an::ChannelScope::CieX},
        {"channel-cie-y", an::ChannelScope::CieY},
        {"channel-cie-z", an::ChannelScope::CieZ},
        {"channel-hsl-hue", an::ChannelScope::HslHue},
        {"channel-hsl-saturation", an::ChannelScope::HslSaturation},
        {"channel-hsl-lightness", an::ChannelScope::HslLightness},
        {"channel-hsv-hue", an::ChannelScope::HsvHue},
        {"channel-hsv-saturation", an::ChannelScope::HsvSaturation},
        {"channel-hsv-value", an::ChannelScope::HsvValue},
        {"channel-ycbcr-luma", an::ChannelScope::YCbCrLuma},
        {"channel-ycbcr-blue-chroma", an::ChannelScope::YCbCrBlueChroma},
        {"channel-ycbcr-red-chroma", an::ChannelScope::YCbCrRedChroma},
    };
    if (mode == "default" || mode == "gamma-2.2" || mode == "gamma-0.5")
    {
        setReal(dialog, "gammaValue", mode == "default" ? 1.0 : (mode == "gamma-2.2" ? 2.2 : 0.5));
        return;
    }
    for (const ChannelMode& channel : kChannelModes)
    {
        if (mode == channel.mode)
        {
            setReal(dialog, "gammaValue", 2.2);
            if (offersChoice(dialog, "channelSelector"))
            {
                choose(dialog, "channelSelector", an::ui::processChannelLabel(channel.scope));
            }
            return;
        }
    }
    failUnknownMode("the gamma correction", mode);
}

void configureBrightnessContrast(ImageRTPreviewControl& dialog, std::string_view mode)
{
    if (mode == "default")
    {
        setInteger(dialog, "brightnessValue", 0);
        setInteger(dialog, "contrastValue", 0);
    }
    else if (mode == "brightness-40-contrast-25")
    {
        setInteger(dialog, "brightnessValue", 40);
        setInteger(dialog, "contrastValue", 25);
    }
    else if (mode == "brightness-minus-60-contrast-minus-30")
    {
        setInteger(dialog, "brightnessValue", -60);
        setInteger(dialog, "contrastValue", -30);
    }
    else
    {
        failUnknownMode("brightness and contrast", mode);
    }
}

template <typename ProcessT> DialogHandle openReferenceDialog(ImageViewControl& target)
{
    return openDialog<ProcessT>(target);
}

const ReferenceDialog kDialogs[] = {
    {"histogram-transformation", "Histogram Transformation", &openReferenceDialog<HistogramTransformationProcess>,
     &configureHistogramTransformation},
    {"gh-transformation", "Generalized Hyperbolic Transformation", &openReferenceDialog<GHTransformationProcess>,
     &configureGeneralizedHyperbolic},
    {"arcsinh-stretch", "Arcsinh Stretch", &openReferenceDialog<ArcSinhStretchProcess>, &configureArcsinh},
    {"statistical-stretch", "Statistical Stretch", &openReferenceDialog<StatisticalStretch>,
     &configureStatisticalStretch},
    {"curves-transformation", "Curves Transformation", &openReferenceDialog<CurvesTransformationProcess>,
     &configureCurves},
    {"gamma-correction", "Gamma Correction", &openReferenceDialog<GammaCorrectionProcess>, &configureGamma},
    {"brightness-and-contrast", "Brightness and Contrast", &openReferenceDialog<AdjustBrightnessAndContrastProcess>,
     &configureBrightnessContrast},
};

const ReferenceCase kCases[] = {
    {"histogram-transformation", "default", "scene-rgb-float32"},
    {"histogram-transformation", "shadows-0.02-midtones-0.25-highlights-0.95", "scene-rgb-float32"},
    {"histogram-transformation", "shadows-0.02-midtones-0.25-highlights-0.95", "scene-rgb-uint16"},
    {"histogram-transformation", "shadows-0.02-midtones-0.25-highlights-0.95", "scene-mono-float32"},
    {"histogram-transformation", "red-only", "scene-rgb-float32"},
    {"histogram-transformation", "autostretch-linked", "scene-rgb-float32"},
    {"histogram-transformation", "autostretch-linked", "scene-rgb-uint16"},
    {"histogram-transformation", "autostretch-unlinked", "scene-rgb-float32"},
    {"gh-transformation", "default", "scene-rgb-float32"},
    {"gh-transformation", "hyperbolic-factor-2-intensity-1.5-symmetry-0.1", "scene-rgb-float32"},
    {"gh-transformation", "hyperbolic-factor-2-intensity-1.5-symmetry-0.1", "scene-rgb-uint16"},
    {"gh-transformation", "hyperbolic-factor-2-intensity-1.5-symmetry-0.1", "scene-mono-float32"},
    {"gh-transformation", "hyperbolic-inverse", "scene-rgb-float32"},
    {"gh-transformation", "lightness", "scene-rgb-float32"},
    {"gh-transformation", "saturation", "scene-rgb-float32"},
    {"gh-transformation", "colour", "scene-rgb-float32"},
    {"gh-transformation", "red-only", "scene-rgb-float32"},
    {"gh-transformation", "arcsinh", "scene-rgb-float32"},
    {"gh-transformation", "histogram", "scene-rgb-float32"},
    {"gh-transformation", "linear", "scene-rgb-float32"},
    {"gh-transformation", "inversion", "scene-rgb-float32"},
    {"gh-transformation", "stf-linked", "scene-rgb-float32"},
    {"arcsinh-stretch", "stretch-0-black-point-0.001", "scene-rgb-float32"},
    {"arcsinh-stretch", "stretch-50", "scene-rgb-float32"},
    {"arcsinh-stretch", "stretch-50", "scene-rgb-uint16"},
    {"arcsinh-stretch", "stretch-50", "scene-mono-float32"},
    {"arcsinh-stretch", "stretch-50-even-luminance", "scene-rgb-float32"},
    {"statistical-stretch", "default", "scene-rgb-float32"},
    {"statistical-stretch", "default", "scene-rgb-uint16"},
    {"statistical-stretch", "default", "scene-mono-float32"},
    {"statistical-stretch", "linked", "scene-rgb-float32"},
    {"statistical-stretch", "normalize-curves", "scene-rgb-float32"},
    {"statistical-stretch", "no-black-clip", "scene-rgb-float32"},
    {"curves-transformation", "default", "scene-rgb-float32"},
    {"curves-transformation", "cubic-three-nodes", "scene-rgb-float32"},
    {"curves-transformation", "cubic-three-nodes", "scene-rgb-uint16"},
    {"curves-transformation", "cubic-three-nodes", "scene-mono-float32"},
    {"curves-transformation", "linear-three-nodes", "scene-rgb-float32"},
    {"curves-transformation", "akima-five-nodes", "scene-rgb-float32"},
    {"curves-transformation", "steffen-five-nodes", "scene-rgb-float32"},
    {"curves-transformation", "cubic-three-nodes-red", "scene-rgb-float32"},
    {"curves-transformation", "cubic-three-nodes-cie-l", "scene-rgb-float32"},
    {"curves-transformation", "cubic-three-nodes-hsv-value", "scene-rgb-float32"},
    {"gamma-correction", "channel-rgb", "scene-rgb-float32"},
    {"gamma-correction", "channel-red", "scene-rgb-float32"},
    {"gamma-correction", "channel-green", "scene-rgb-float32"},
    {"gamma-correction", "channel-blue", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-l", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-a", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-b", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-c", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-h", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-x", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-y", "scene-rgb-float32"},
    {"gamma-correction", "channel-cie-z", "scene-rgb-float32"},
    {"gamma-correction", "channel-hsl-hue", "scene-rgb-float32"},
    {"gamma-correction", "channel-hsl-saturation", "scene-rgb-float32"},
    {"gamma-correction", "channel-hsl-lightness", "scene-rgb-float32"},
    {"gamma-correction", "channel-hsv-hue", "scene-rgb-float32"},
    {"gamma-correction", "channel-hsv-saturation", "scene-rgb-float32"},
    {"gamma-correction", "channel-hsv-value", "scene-rgb-float32"},
    {"gamma-correction", "channel-ycbcr-luma", "scene-rgb-float32"},
    {"gamma-correction", "channel-ycbcr-blue-chroma", "scene-rgb-float32"},
    {"gamma-correction", "channel-ycbcr-red-chroma", "scene-rgb-float32"},
    {"gamma-correction", "channel-grayscale", "scene-mono-float32"},
    {"gamma-correction", "default", "scene-rgb-float32"},
    {"gamma-correction", "gamma-2.2", "scene-rgb-uint16"},
    {"gamma-correction", "gamma-2.2", "scene-mono-float32"},
    {"gamma-correction", "gamma-2.2", "scene-mono-uint8"},
    {"gamma-correction", "gamma-2.2", "scene-rgb-float32-unbounded"},
    {"gamma-correction", "gamma-0.5", "scene-rgb-float32"},
    {"brightness-and-contrast", "default", "scene-rgb-float32"},
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-rgb-float32"},
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-rgb-uint16"},
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-mono-int16"},
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-rgb-float32-unbounded"},
    {"brightness-and-contrast", "brightness-minus-60-contrast-minus-30", "scene-rgb-float32"},
    // Through a mask: an integer image's blend and a floating one's, and a mask of another representation than the
    // image.
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-rgb-uint16", "scene-mono-float32"},
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-rgb-float32", "scene-mono-float32"},
    {"brightness-and-contrast", "brightness-40-contrast-25", "scene-rgb-uint16", "scene-mono-uint8"},
};

/// The key of a case's references: its file, its mode and the mask it is seen through.
[[nodiscard]] std::string caseKey(const ReferenceCase& reference)
{
    std::string key = std::string(reference.records) + "." + reference.mode;
    if (reference.mask != nullptr)
    {
        key += std::string("-masked-by-") + reference.mask;
    }
    return key;
}

/// A build's name among the references' per-build directories: its compiler and its platform, as the recorded
/// behaviour expectations name them.
[[nodiscard]] std::string buildNameOf(const fixtures::ExpectationProvenance& provenance)
{
    return provenance.compiler.substr(0u, provenance.compiler.find(' ')) + "-" + provenance.platform;
}

/// The analysis a presentation was made through, as one row per colour channel of the fields `kStatisticsFields`
/// names, in Float64, which holds every one of them exactly.
[[nodiscard]] fixtures::FixtureSamples statisticsSamples(const an::DisplayPresentationAnalysis& analysis)
{
    const std::uint32_t channels = std::clamp(analysis.colorChannels, 1u, 3u);
    fixtures::FixtureSamples samples(kStatisticsFieldCount, channels, 1u, fixtures::FixtureSampleType::Float64);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const double fields[kStatisticsFieldCount] = {analysis.medians[channel],
                                                      analysis.medianAbsoluteDeviations[channel],
                                                      static_cast<double>(analysis.measuredSamples[channel]),
                                                      analysis.neutralizationOffsets[channel],
                                                      analysis.calibrationGains[channel],
                                                      analysis.calibrationOffsets[channel],
                                                      analysis.measured ? 1.0 : 0.0,
                                                      analysis.neutralized ? 1.0 : 0.0,
                                                      analysis.calibrated ? 1.0 : 0.0,
                                                      static_cast<double>(analysis.promotion),
                                                      analysis.realtimePreview ? 1.0 : 0.0};
        for (std::uint32_t field = 0u; field < kStatisticsFieldCount; ++field)
        {
            samples.setValue(field, channel, 0u, fields[field]);
        }
    }
    return samples;
}

/// A histogram's counts as one row per channel: every bin, then the samples clipped low, clipped high, not a number
/// and admitted in all, in Float64, which holds every count exactly.
[[nodiscard]] fixtures::FixtureSamples histogramSamples(const an::ui::HistogramDisplayData& counts)
{
    const auto channels = static_cast<std::uint32_t>(counts.channels.size());
    const auto bins = static_cast<std::uint32_t>(counts.channels.front().bins.size());
    fixtures::FixtureSamples samples(bins + 4u, channels, 1u, fixtures::FixtureSampleType::Float64);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        const an::ui::HistogramDisplayChannel& counted = counts.channels[channel];
        EXPECT_EQ(counted.bins.size(), bins) << "every channel of a histogram has as many bins";
        for (std::uint32_t bin = 0u; bin < bins && bin < counted.bins.size(); ++bin)
        {
            samples.setValue(bin, channel, 0u, static_cast<double>(counted.bins[bin]));
        }
        samples.setValue(bins, channel, 0u, static_cast<double>(counted.clippedLow));
        samples.setValue(bins + 1u, channel, 0u, static_cast<double>(counted.clippedHigh));
        samples.setValue(bins + 2u, channel, 0u, static_cast<double>(counted.notANumber));
        samples.setValue(bins + 3u, channel, 0u, static_cast<double>(counted.samples));
    }
    return samples;
}

/// What a case's settled preview showed, as references: its samples, the statistics the preview view presented them
/// with and the counts of every histogram the dialog draws. Fails the case at the step that fails, returning what was
/// read until then.
[[nodiscard]] std::vector<fixtures::Expectation> referencesOf(ProcessTestContext& context,
                                                              const ReferenceDialog& dialog,
                                                              const ReferenceCase& reference,
                                                              const fixtures::ExpectationProvenance& provenance)
{
    std::vector<fixtures::Expectation> references;
    const std::string key = caseKey(reference);
    const fixtures::FixtureImage* scene = fixtures::fixtureNamed(reference.scene);
    EXPECT_NE(scene, nullptr) << key << ": no fixture scene " << reference.scene;
    if (scene == nullptr)
    {
        return references;
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> sceneSamples = fixtures::materializeFixture(*scene, error);
    EXPECT_TRUE(sceneSamples.has_value()) << key << ": " << error;
    if (!sceneSamples.has_value())
    {
        return references;
    }
    const an::consumer::EngineSession& session = context.engineSession();
    SceneView target = sceneView(session, *scene, *sceneSamples);
    if (!target)
    {
        return references;
    }
    EXPECT_TRUE(settleView(*target)) << key;

    std::string described = std::string(dialog.title) + ", mode " + reference.mode;
    SceneView mask;
    if (reference.mask != nullptr)
    {
        const fixtures::FixtureImage* maskScene = fixtures::fixtureNamed(reference.mask);
        EXPECT_NE(maskScene, nullptr) << key << ": no fixture scene " << reference.mask;
        if (maskScene == nullptr)
        {
            return references;
        }
        const std::optional<fixtures::FixtureSamples> maskSamples = fixtures::materializeFixture(*maskScene, error);
        EXPECT_TRUE(maskSamples.has_value()) << key << ": " << error;
        if (!maskSamples.has_value())
        {
            return references;
        }
        mask = sceneView(session, *maskScene, *maskSamples);
        if (!mask)
        {
            return references;
        }
        target->setCurrentMask(mask.get());
        EXPECT_TRUE(target->isMasked() && target->isMaskEnabled()) << key << ": the mask applies";
        EXPECT_TRUE(settleView(*target)) << key;
        described += ", through the mask " + maskScene->name + " description=" + fixtures::fixtureDigest(*maskScene) +
                     " samples=" + maskSamples->digest();
    }
    described += ", preview on, preview view in autostretch: ";

    const int errorsBefore = context.errorCount();
    {
        DialogHandle window = dialog.open(*target);
        dialog.configure(*window, reference.mode);
        if (auto* preview = child<QCheckBox>(*window, "preview"); preview != nullptr && !preview->isChecked())
        {
            preview->setChecked(true);
        }
        EXPECT_TRUE(settleDialog(*window)) << key;
        ImageViewControl* previewView = window->previewView();
        EXPECT_NE(previewView, nullptr) << key << ": the preview is shown";
        if (previewView != nullptr)
        {
            previewView->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
            EXPECT_TRUE(settleDialog(*window)) << key;
            EXPECT_TRUE(previewView->isAutoStretchEnabled()) << key;
            // A mode that changes nothing shows the target itself, running no body; its bytes are held all the same.
            const ProcessRunTelemetry shownFrames = window->runTelemetry();
            EXPECT_GE(shownFrames.previewsCommitted + shownFrames.identityPreviews, 1u) << key;
            EXPECT_EQ(shownFrames.previewsFailed, 0u) << key;

            const std::shared_ptr<const an::consumer::ImageAsset> shown = previewView->document().asset();
            EXPECT_NE(shown, nullptr) << key;
            std::optional<fixtures::FixtureSamples> previewSamples =
                shown != nullptr ? fixtures::engine::readAssetSamples(*shown, error) : std::nullopt;
            EXPECT_TRUE(previewSamples.has_value()) << key << ": " << error;
            if (previewSamples.has_value())
            {
                references.push_back(fixtures::makeExpectation(
                    key + ".preview", described + "the settled preview's samples", *scene, *sceneSamples,
                    std::move(*previewSamples), fixtures::ExpectationClass::Exact, provenance));
            }

            const std::optional<an::DisplayPresentationAnalysis> analysis =
                previewView->getViewWidget()->presentedAnalysis();
            EXPECT_TRUE(analysis.has_value()) << key << ": the preview view presented the settled preview";
            if (analysis.has_value())
            {
                references.push_back(fixtures::makeExpectation(
                    key + ".statistics",
                    described +
                        "the statistics the preview view presented the settled preview with, per colour "
                        "channel: " +
                        kStatisticsFields,
                    *scene, *sceneSamples, statisticsSamples(*analysis), fixtures::ExpectationClass::Exact,
                    provenance));
            }

            const std::vector<std::shared_ptr<const an::ui::HistogramDisplayData>> histograms =
                drawnHistograms(*window);
            EXPECT_FALSE(histograms.empty()) << key << ": the dialog draws a histogram";
            for (std::size_t index = 0u; index < histograms.size(); ++index)
            {
                const std::string ordinal = std::to_string(index);
                references.push_back(fixtures::makeExpectation(
                    key + ".histogram-" + ordinal,
                    described + "the counts of histogram " + ordinal +
                        " of the dialog, in its layout's order, per channel: every bin, then the samples clipped low, "
                        "clipped high, not a number and admitted in all",
                    *scene, *sceneSamples, histogramSamples(*histograms[index]), fixtures::ExpectationClass::Exact,
                    provenance));
            }
        }
    }
    EXPECT_EQ(context.errorCount(), errorsBefore) << key << ": nothing was reported as an error";
    if (mask)
    {
        target->removeCurrentMask();
        EXPECT_TRUE(settleView(*target)) << key;
        mask.reset();
    }
    target.reset();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    return references;
}

/// The checked-in references' directory, beside the fixture layers', or nothing with where it looked in `error`.
[[nodiscard]] std::optional<std::filesystem::path> referenceDirectory(std::string& error)
{
    const std::optional<std::filesystem::path> expectations = fixtures::expectationDirectory(error);
    if (!expectations.has_value())
    {
        return std::nullopt;
    }
    // The fixture layers' expectations are `fixtures/common/expectations`; the references stand beside the layers.
    std::filesystem::path directory = expectations->parent_path().parent_path() / "references" / "realtime-preview";
    std::error_code ignored;
    if (!std::filesystem::is_directory(directory, ignored))
    {
        error = "no references at " + directory.string();
        return std::nullopt;
    }
    return directory;
}

/// The checked-in references of a dialog, each replaced by this build's own where one stands beside them, keyed by id;
/// empty when there are none.
[[nodiscard]] std::map<std::string, fixtures::Expectation> recordedReferences(const std::string& records,
                                                                              std::string& error)
{
    std::map<std::string, fixtures::Expectation> recorded;
    const std::optional<std::filesystem::path> directory = referenceDirectory(error);
    if (!directory.has_value())
    {
        return recorded;
    }
    const std::string file = records + ".expectations";
    const std::filesystem::path common = *directory / file;
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(common, ignored))
    {
        error = "no references at " + common.string();
        return recorded;
    }
    std::optional<std::vector<fixtures::Expectation>> read = fixtures::readExpectationFile(common, error);
    if (!read.has_value())
    {
        return recorded;
    }
    for (fixtures::Expectation& reference : *read)
    {
        const std::string id = reference.id;
        recorded.insert_or_assign(id, std::move(reference));
    }
    const std::filesystem::path own = *directory / buildNameOf(fixtures::provenanceOfThisBuild({}, {}, {})) / file;
    if (std::filesystem::is_regular_file(own, ignored))
    {
        std::optional<std::vector<fixtures::Expectation>> ownRead = fixtures::readExpectationFile(own, error);
        for (fixtures::Expectation& reference : ownRead.value_or(std::vector<fixtures::Expectation>{}))
        {
            const std::string id = reference.id;
            EXPECT_TRUE(recorded.contains(id))
                << own.string() << " holds " << id << ", which no common reference names";
            recorded.insert_or_assign(id, std::move(reference));
        }
    }
    return recorded;
}

/// Whether a fresh reference matches a recorded one: the same description, the same scene and, by the recorded one's
/// classification, the same result. Says why not in `reason`.
[[nodiscard]] bool reproduces(const fixtures::Expectation& recorded, const fixtures::Expectation& fresh,
                              std::string& reason)
{
    if (recorded.operation != fresh.operation)
    {
        reason = "the case is described otherwise now:\n  recorded: " + recorded.operation +
                 "\n  now:      " + fresh.operation;
        return false;
    }
    if (recorded.inputDescription != fresh.inputDescription || recorded.inputSamples != fresh.inputSamples)
    {
        reason = "the scene " + fresh.inputName + " is not the one the reference was captured over";
        return false;
    }
    const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(recorded, fresh.result);
    if (!compared.matched)
    {
        reason = compared.reason;
        return false;
    }
    return true;
}

/// Writes references to a file with CRLF line endings, replacing it, under a header that says what they are.
[[nodiscard]] bool writeReferences(const std::filesystem::path& path,
                                   const std::vector<fixtures::Expectation>& references, std::string& error)
{
    std::string text =
        "# The real-time preview's references: what the settled preview of a process dialog showed - its "
        "samples, the\n# statistics its view presented them with and the histograms the dialog drew - "
        "when they were captured.\n# See the README.md of the references' directory.\n";
    for (const fixtures::Expectation& reference : references)
    {
        text += "\n" + fixtures::formatExpectation(reference);
    }
    std::string crlf;
    crlf.reserve(text.size() + text.size() / 40u);
    for (const char character : text)
    {
        if (character == '\n')
        {
            crlf += '\r';
        }
        crlf += character;
    }
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(crlf.data(), static_cast<std::streamsize>(crlf.size()));
    stream.close();
    if (!stream)
    {
        error = "could not write " + path.string();
        return false;
    }
    return true;
}

class RealtimePreviewReferences : public ::testing::TestWithParam<const char*>
{
};

TEST_P(RealtimePreviewReferences, TheSettledPreviewShowsWhatItShowedWhenTheReferencesWereCaptured)
{
    const std::string records = GetParam();
    const ReferenceDialog* dialog = nullptr;
    for (const ReferenceDialog& candidate : kDialogs)
    {
        if (records == candidate.records)
        {
            dialog = &candidate;
        }
    }
    ASSERT_NE(dialog, nullptr) << records;

    const std::string captureDirectory = qEnvironmentVariable(fixtures::kCaptureDirectoryVariable).toStdString();
    const bool capturing = !captureDirectory.empty();
    const std::string captureCommit = qEnvironmentVariable(fixtures::kCaptureCommitVariable).toStdString();
    const fixtures::ExpectationProvenance provenance =
        fixtures::engine::engineProvenance(captureCommit.empty() ? std::string("-") : captureCommit);
    std::string error;
    const std::map<std::string, fixtures::Expectation> recorded = recordedReferences(records, error);
    if (!capturing)
    {
        ASSERT_FALSE(recorded.empty()) << error << "; capture the references first";
    }

    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::vector<fixtures::Expectation> fresh;
    std::vector<fixtures::Expectation> buildOwn;
    std::size_t cases = 0u;
    for (const ReferenceCase& reference : kCases)
    {
        if (records != reference.records)
        {
            continue;
        }
        ++cases;
        const std::string key = caseKey(reference);
        const std::vector<fixtures::Expectation> made = referencesOf(*context, *dialog, reference, provenance);
        EXPECT_FALSE(made.empty()) << key;
        // Every reference the case was captured with is made again, and no other.
        const std::string prefix = key + ".";
        const std::string suffix = std::string("/") + reference.scene;
        std::size_t recordedForCase = 0u;
        for (const auto& entry : recorded)
        {
            const std::string& id = entry.first;
            const bool ofCase =
                id.starts_with(prefix) && id.ends_with(suffix) && id.find('.', prefix.size()) == std::string::npos;
            recordedForCase += ofCase ? 1u : 0u;
        }
        for (const fixtures::Expectation& result : made)
        {
            const auto found = recorded.find(result.id);
            std::string reason;
            const bool matched = found != recorded.end() && reproduces(found->second, result, reason);
            if (capturing)
            {
                if (found != recorded.end() && !matched &&
                    buildNameOf(found->second.provenance) != buildNameOf(result.provenance))
                {
                    buildOwn.push_back(result);
                }
                continue;
            }
            if (found == recorded.end())
            {
                ADD_FAILURE() << result.id << ": no reference holds it; capture the references";
                continue;
            }
            EXPECT_TRUE(matched) << result.id << ": " << reason;
        }
        if (!capturing)
        {
            EXPECT_EQ(made.size(), recordedForCase)
                << key << " over " << reference.scene << ": the references hold " << recordedForCase
                << " results of the case and the preview showed " << made.size();
        }
        fresh.insert(fresh.end(), made.begin(), made.end());
    }
    EXPECT_GT(cases, 0u) << records;

    if (capturing)
    {
        const std::filesystem::path target =
            std::filesystem::path(captureDirectory) / "references" / "realtime-preview";
        ASSERT_TRUE(writeReferences(target / (records + ".expectations"), fresh, error)) << error;
        if (!buildOwn.empty())
        {
            ASSERT_TRUE(
                writeReferences(target / buildNameOf(provenance) / (records + ".expectations"), buildOwn, error))
                << error;
        }
        // A capture run writes and compares nothing, so it never passes: a shell that leaves the variable set cannot
        // run the suite green, and the records it wrote are read by the next run without it.
        ADD_FAILURE() << "captured " << fresh.size() << " references of " << records << " to " << target.string()
                      << (buildOwn.empty() ? std::string()
                                           : ", " + std::to_string(buildOwn.size()) + " of them this build's own")
                      << "; a capture run compares nothing, so it is reported as failed by design";
    }
}

[[nodiscard]] std::vector<const char*> dialogFiles()
{
    std::vector<const char*> files;
    for (const ReferenceDialog& dialog : kDialogs)
    {
        files.push_back(dialog.records);
    }
    return files;
}

INSTANTIATE_TEST_SUITE_P(Dialogs, RealtimePreviewReferences, ::testing::ValuesIn(dialogFiles()),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name;
                             bool upper = true;
                             for (const char* character = info.param; *character != '\0'; ++character)
                             {
                                 if (*character == '-')
                                 {
                                     upper = true;
                                     continue;
                                 }
                                 name += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(*character)))
                                               : *character;
                                 upper = false;
                             }
                             return name;
                         });

} // namespace
