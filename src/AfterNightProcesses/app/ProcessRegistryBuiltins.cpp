// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ProcessRegistry.h"

#include "app/ChannelExtractor.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/ProcessLauncher.h"

#include "controls/ExportSequenceControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"

#include <AfterNightPlatform/Logging.h>

#include <string>
#include <string_view>

namespace
{

using an::ui::ChannelExtractor;
using an::ui::ImageOperationsCoordinator;
using an::ui::ProcessLauncher;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessLaunchMode;
using an::ui::process_registry::ProcessRegistry;

using ProcessLauncherHandler = void (ProcessLauncher::*)() const;
using ChannelExtractorHandler = void (ChannelExtractor::*)() const;
using ImageOperationsHandler = void (ImageOperationsCoordinator::*)() const;

/// What a registered process states about itself, which its descriptor carries for the guards to read: the name it
/// attaches to a view under, whether it needs the view to itself, and whether it refuses a sequence.
struct ProcessFacts
{
    /// The name the process attaches to a view under; empty for an action that opens no process, whose descriptor is
    /// named by the menu.
    const char* processName = "";

    /// Whether the process needs the view to itself.
    bool requiresExclusiveAccess = false;

    /// Whether the process refuses a sequence.
    bool requiresStillImage = false;
};

/// A process that shares its view with other processes.
constexpr ProcessFacts shared(const char* processName)
{
    return ProcessFacts{processName, false, false};
}

/// A process that needs its view to itself.
constexpr ProcessFacts exclusive(const char* processName)
{
    return ProcessFacts{processName, true, false};
}

/// A process that needs its view to itself and refuses a sequence.
constexpr ProcessFacts exclusiveStillImage(const char* processName)
{
    return ProcessFacts{processName, true, true};
}

/// An action that opens no process of its own.
constexpr ProcessFacts noProcess()
{
    return ProcessFacts{};
}

/// The user guide's page of a registered action.
struct HelpPage
{
    /// The action's object name.
    std::string_view actionObjectName;

    /// The page, relative to the guide's root as the built guide names it.
    std::string_view page;
};

/// The page that describes the operations that open no window: the channel extractions, the format conversions and
/// the sequence conversions. An action of the non-interactive category with no row of its own in `kHelpPages` has it.
constexpr std::string_view kNonInteractiveHelpPage = "processes/utility/non-interactive-processes.html";

/// The user guide's page of every registered action that has one of its own: the page whose `menu` reaches the
/// action. Help opens it from the window the action opens. The guide's check reads these rows from this file and
/// refuses a path that names no page, so a page that moves is moved here with it.
constexpr HelpPage kHelpPages[] = {
    {"actionAdjustBrightness_Contrast", "processes/color-adjustment/adjust-brightness-contrast.html"},
    {"actionAdjustHSL", "processes/color-adjustment/adjust-hsl.html"},
    {"actionAdjustLightness_Chroma_Hue", "processes/color-adjustment/adjust-lch.html"},
    {"actionArcSinh_Stretch", "processes/stretching/arcsinh-stretch.html"},
    {"actionAuto_Gradient_Removal", "processes/background-extraction/auto-gradient-removal.html"},
    {"actionBackground_Extraction", "processes/background-extraction/background-extraction.html"},
    {"actionBackground_Neutralization", "processes/color-calibration/background-neutralization.html"},
    {"actionBanding_Reduction_Filter", "processes/noise-reduction/banding-reduction.html"},
    {"actionBilateral_Filter", "processes/noise-reduction/bilateral-filter.html"},
    {"actionBinarize", "processes/masking/binarize.html"},
    {"actionCFA_Integer_Resample", "processes/calibration/cfa-integer-resample.html"},
    {"actionCLA_Histogram_Equalization", "processes/sharpening/clahe.html"},
    {"actionChannelComposition", "processes/color-composition/channel-composition.html"},
    {"actionColor_Calibration_2", "processes/color-calibration/color-calibration.html"},
    {"actionColor_Saturation", "processes/color-adjustment/color-saturation.html"},
    {"actionColorize", "processes/color-composition/colorize.html"},
    {"actionCosmetic_Correction_Filter", "processes/calibration/cosmetic-correction.html"},
    {"actionCrop", "processes/geometry/crop.html"},
    {"actionCurves_Transformation", "processes/stretching/curves-transformation.html"},
    {"actionDark_Structure_Enhance", "processes/star-operations/dark-structure-enhance.html"},
    {"actionDebayer", "processes/calibration/debayer.html"},
    {"actionDeepSNR", "processes/noise-reduction/deepsnr.html"},
    {"actionExport", "interface/saving-and-exporting.html"},
    {"actionExposure", "processes/stretching/exposure-correction.html"},
    {"actionExtract_Sequence", "processes/utility/extract-sequence.html"},
    {"actionGRAYCstoration_Filter", "processes/noise-reduction/graycstoration.html"},
    {"actionGamma_Correction", "processes/stretching/gamma-correction.html"},
    {"actionGaussian_Blur", "processes/noise-reduction/gaussian-blur.html"},
    {"actionGeneralized_Hyperbolic_Transformation", "processes/stretching/gh-transformation.html"},
    {"actionHigh_Pass", "processes/sharpening/high-pass-filter.html"},
    {"actionHistogram_Transformation", "processes/stretching/histogram-transformation.html"},
    {"actionImage_Blend", "processes/analysis/image-blend.html"},
    {"actionImage_Calibration", "processes/calibration/image-calibration.html"},
    {"actionImage_Compare", "processes/analysis/image-compare.html"},
    {"actionImage_Integration", "processes/stacking-integration/image-integration.html"},
    {"actionImage_Integration_Lite", "processes/stacking-integration/image-integration-lite.html"},
    {"actionImage_List_Analyzer", "processes/stacking-integration/image-list-analyzer.html"},
    {"actionImage_Registration", "processes/registration-alignment/image-registration.html"},
    {"actionImage_Stacking", "processes/stacking-integration/image-stacking.html"},
    {"actionIntegerResample", "processes/geometry/integer-resample.html"},
    {"actionLRGB_Combination", "processes/color-composition/lrgb-combination.html"},
    {"actionLinear_Match", "processes/color-calibration/linear-match.html"},
    {"actionLocal_Contrast_Enhancement", "processes/sharpening/local-contrast-enhancement.html"},
    {"actionLocal_Histogram_Equalization", "processes/sharpening/local-histogram-equalization.html"},
    {"actionMask_Drawing", "processes/masking/mask-drawing.html"},
    {"actionMedian_Filter", "processes/noise-reduction/median-filter.html"},
    {"actionMerge_CFA", "processes/calibration/merge-cfa.html"},
    {"actionMorphological_Transformation", "processes/masking/morphological-transformation.html"},
    {"actionMultiscale_Alpha_Sharpen", "processes/sharpening/sharpen-alpha.html"},
    {"actionMultiscale_Linear_Transform", "processes/multiscale/multiscale-linear.html"},
    {"actionNBColorMapper", "processes/color-composition/nb-color-mapper.html"},
    {"actionNBRGB_Combination", "processes/color-composition/nbrgb-combination.html"},
    {"actionNarrowbandPaletteComposer", "processes/color-composition/narrowband-palette-composer.html"},
    {"actionNoise_Generator", "processes/utility/noise-generator.html"},
    {"actionNon_Local_Bayes_Filter", "processes/noise-reduction/nl-bayes.html"},
    {"actionPatch_PCA_Filter", "processes/noise-reduction/patch-pca-denoise.html"},
    {"actionPhotometric_Color_Calibration", "processes/color-calibration/photometric-cc.html"},
    {"actionPixel_Math_2", "processes/pixel-math/pixel-math.html"},
    {"actionProfiled_Starmask_Recombination", "processes/star-operations/profiled-starmask-recomb.html"},
    {"actionRange_Selection", "processes/analysis/range-selection.html"},
    {"actionRemove_Mask", "interface/masks.html"},
    {"actionResize", "processes/geometry/resize.html"},
    {"actionRotate", "processes/geometry/rotate.html"},
    {"actionSCNR", "processes/color-calibration/scnr.html"},
    {"actionSelect_Mask", "interface/masks.html"},
    {"actionSpectrophotometric_Color_Calibration", "processes/color-calibration/spectrophotometric-cc.html"},
    {"actionSplit_CFA", "processes/calibration/split-cfa.html"},
    {"actionStar_Mask", "processes/star-operations/star-mask.html"},
    {"actionStar_Profiler_v1", "processes/registration-alignment/star-profiler.html"},
    {"actionStar_Profiler_v2", "processes/registration-alignment/star-profiler.html"},
    {"actionStar_Reduction", "processes/star-operations/star-reduction.html"},
    {"actionStarmask_Recombination", "processes/star-operations/starmask-recombination.html"},
    {"actionStarnet", "processes/star-operations/starnet.html"},
    {"actionStarnet_Star_Mask", "processes/star-operations/starnet-star-mask.html"},
    {"actionStatistical_Stretch", "processes/stretching/statistical-stretch.html"},
    {"actionTighten_Edges", "processes/color-adjustment/tighten-edges.html"},
    {"actionTotal_Variation_Denoise_Filter", "processes/noise-reduction/total-variation-denoise.html"},
    {"actionUnsharp_Mask", "processes/sharpening/unsharp-mask.html"},
    {"actionWavelet_Denoise_Filter", "processes/noise-reduction/wavelet-denoise.html"},
};

/// The user guide's page of an action: its row of `kHelpPages`, the non-interactive operations' page for an action of
/// that category with no row, and none otherwise.
std::string helpPathOf(const std::string_view category, const std::string_view actionObjectName)
{
    for (const HelpPage& row : kHelpPages)
    {
        if (row.actionObjectName == actionObjectName)
        {
            return std::string(row.page);
        }
    }
    return category == "non_interactive" ? std::string(kNonInteractiveHelpPage) : std::string();
}

std::string buildProcessId(const std::string_view category, const std::string_view actionObjectName)
{
    std::string id;
    id.reserve(category.size() + actionObjectName.size() + 1u);
    id.append(category);
    id.push_back('.');
    id.append(actionObjectName);
    return id;
}

void registerDescriptor(ProcessDescriptor descriptor)
{
    const std::string descriptorId = descriptor.id;
    if (!ProcessRegistry::instance().registerProcess(std::move(descriptor)))
    {
        LOG_WARNING("ProcessRegistry self-registration skipped duplicate descriptor id={}", descriptorId);
    }
}

ProcessDescriptor makeDescriptor(const char* category, const char* actionObjectName, const ProcessLaunchMode launchMode)
{
    ProcessDescriptor descriptor;
    descriptor.category = category ? category : "process";
    descriptor.action_object_name = actionObjectName ? actionObjectName : "actionUnknown";
    descriptor.id = buildProcessId(descriptor.category, descriptor.action_object_name);

    // Names are hydrated from QAction text during MenuActionManager wiring.
    descriptor.display_name = descriptor.action_object_name;
    descriptor.process_name = descriptor.display_name;
    descriptor.launch_mode = launchMode;
    descriptor.helpPath = helpPathOf(descriptor.category, descriptor.action_object_name);
    return descriptor;
}

/// States a process's facts on its descriptor. A process name is kept when the menu names the descriptor, so the
/// guards find the process attached to a view under the name it attached with.
void applyProcessFacts(ProcessDescriptor& descriptor, const ProcessFacts& facts)
{
    if (facts.processName != nullptr && facts.processName[0] != '\0')
    {
        descriptor.process_name = facts.processName;
    }
    descriptor.requires_exclusive_access = facts.requiresExclusiveAccess;
    descriptor.requires_still_image = facts.requiresStillImage;
}

void applyBuiltinProcessRoles(ProcessDescriptor& descriptor)
{
    if (descriptor.category == "star_object" && descriptor.action_object_name == "actionStarnet")
    {
        descriptor.provides_starless_output = true;
        descriptor.provides_star_mask_output = true;
    }
    else if (descriptor.category == "star_object" && descriptor.action_object_name == "actionStarnet_Star_Mask")
    {
        descriptor.provides_star_mask_output = true;
    }
    else if (descriptor.category == "color" && descriptor.action_object_name == "actionNBRGB_Combination")
    {
        descriptor.source_channel_filter = {3};
        descriptor.requires_still_image = true;
    }
    else if (descriptor.category == "color" && descriptor.action_object_name == "actionNBColorMapper")
    {
        descriptor.requires_still_image = true;
    }
    else if (descriptor.category == "color" && descriptor.action_object_name == "actionNarrowbandPaletteComposer")
    {
        descriptor.requires_still_image = false;
    }
}

ImageOperationsCoordinator::Dependencies currentImageOperationsDependencies()
{
    ImageOperationsCoordinator::Dependencies dependencies;
    dependencies.activeViewControl = []() -> ImageViewControl* {
        if (auto* context = an::ui::appContext())
        {
            if (auto* viewRegistry = context->getViewRegistry())
            {
                return viewRegistry->getActiveImageViewControl();
            }
        }

        return nullptr;
    };
    dependencies.listViewControls = []() -> std::vector<ImageViewControl*> {
        if (auto* context = an::ui::appContext())
        {
            if (auto* viewControlManager = context->getViewControlManager())
            {
                return viewControlManager->getListOfViewControls();
            }
        }

        return {};
    };
    dependencies.messageParent = []() -> QWidget* {
        if (auto* context = an::ui::appContext())
        {
            return context->getMainWindow();
        }

        return nullptr;
    };
    dependencies.notifyErrorMessage = [](const QString& title, const QString& message) {
        if (auto* context = an::ui::appContext())
        {
            if (auto* dialogService = context->getDialogService())
            {
                dialogService->notifyErrorMessage(context->getMainWindow(), title, message);
            }
        }
    };
    dependencies.nonInteractive = an::ui::nonInteractiveImageOperations();
    // The image menu's Export opens the export window over the sequence the view shows.
    dependencies.exportSequence = [](ImageViewControl* control) {
        if (control != nullptr)
        {
            (void)ExportSequenceControl::openFor(*control);
        }
    };

    return dependencies;
}

void invokeProcessLauncher(const std::string& descriptorId, const ProcessLauncherHandler handler)
{
    ProcessLauncher launcher(descriptorId);
    (launcher.*handler)();
}

void invokeChannelExtractor(const ChannelExtractorHandler handler)
{
    ChannelExtractor extractor;
    (extractor.*handler)();
}

void invokeImageOperations(const ImageOperationsHandler handler)
{
    ImageOperationsCoordinator operations(currentImageOperationsDependencies());
    (operations.*handler)();
}

void registerProcessLauncherDescriptor(const char* category, const char* actionObjectName,
                                       const ProcessLaunchMode launchMode, const ProcessLauncherHandler handler,
                                       const ProcessFacts& facts)
{
    ProcessDescriptor descriptor = makeDescriptor(category, actionObjectName, launchMode);
    applyProcessFacts(descriptor, facts);
    applyBuiltinProcessRoles(descriptor);
    const std::string descriptorId = descriptor.id;
    switch (launchMode)
    {
    case ProcessLaunchMode::SingleImage:
        descriptor.launch_single_image = [descriptorId, handler](ImageViewControl*) {
            invokeProcessLauncher(descriptorId, handler);
        };
        break;
    case ProcessLaunchMode::TopLevel:
        descriptor.launch_top_level = [descriptorId, handler]() { invokeProcessLauncher(descriptorId, handler); };
        break;
    case ProcessLaunchMode::NonInteractive:
        descriptor.launch_non_interactive = [descriptorId, handler]() { invokeProcessLauncher(descriptorId, handler); };
        break;
    }

    registerDescriptor(std::move(descriptor));
}

void registerChannelExtractorDescriptor(const char* category, const char* actionObjectName,
                                        const ChannelExtractorHandler handler)
{
    ProcessDescriptor descriptor = makeDescriptor(category, actionObjectName, ProcessLaunchMode::NonInteractive);
    descriptor.launch_non_interactive = [handler]() { invokeChannelExtractor(handler); };
    registerDescriptor(std::move(descriptor));
}

void registerImageOperationDescriptor(const char* category, const char* actionObjectName,
                                      const ImageOperationsHandler handler)
{
    ProcessDescriptor descriptor = makeDescriptor(category, actionObjectName, ProcessLaunchMode::NonInteractive);
    descriptor.launch_non_interactive = [handler]() { invokeImageOperations(handler); };
    registerDescriptor(std::move(descriptor));
}

void registerBuiltinProcessDescriptors()
{
    constexpr const char* kCategoryToneBrightness = "tone_brightness";
    constexpr const char* kCategoryFilters = "filters";
    constexpr const char* kCategoryGeometry = "geometry";
    constexpr const char* kCategoryColor = "color";
    constexpr const char* kCategoryDenoising = "denoising";
    constexpr const char* kCategorySharpening = "sharpening_enhancement";
    constexpr const char* kCategoryStarObject = "star_object";
    constexpr const char* kCategoryBackgroundExtraction = "background_extraction";
    constexpr const char* kCategoryTransforms = "transforms";
    constexpr const char* kCategorySensorCalibration = "sensor_calibration";
    constexpr const char* kCategoryMaskGeneration = "mask_generation";
    constexpr const char* kCategoryMultiImageStacking = "multi_image_stacking";
    constexpr const char* kCategoryNonInteractive = "non_interactive";

#define AN_REGISTER_PROCESS_SINGLE(category, actionObjectName, handler, facts)                     \
    registerProcessLauncherDescriptor(category, #actionObjectName, ProcessLaunchMode::SingleImage, \
                                      &ProcessLauncher::handler, facts)
#define AN_REGISTER_PROCESS_TOPLEVEL(category, actionObjectName, handler, facts)                \
    registerProcessLauncherDescriptor(category, #actionObjectName, ProcessLaunchMode::TopLevel, \
                                      &ProcessLauncher::handler, facts)
#define AN_REGISTER_PROCESS_NON_INTERACTIVE(category, actionObjectName, handler)                      \
    registerProcessLauncherDescriptor(category, #actionObjectName, ProcessLaunchMode::NonInteractive, \
                                      &ProcessLauncher::handler, noProcess())
#define AN_REGISTER_CHANNEL(actionObjectName, handler) \
    registerChannelExtractorDescriptor(kCategoryNonInteractive, #actionObjectName, &ChannelExtractor::handler)
#define AN_REGISTER_IMAGE_OPERATION(actionObjectName, handler) \
    registerImageOperationDescriptor(kCategoryNonInteractive, #actionObjectName, &ImageOperationsCoordinator::handler)

    AN_REGISTER_PROCESS_NON_INTERACTIVE(kCategoryNonInteractive, actionExtract_Frame, executeExtractFrame);
    AN_REGISTER_PROCESS_SINGLE(kCategoryMultiImageStacking, actionExtract_Sequence, extractSequence,
                               shared("Extract Sequence"));

    AN_REGISTER_CHANNEL(actionExtract_Red_Channel, extractRedChannel);
    AN_REGISTER_CHANNEL(actionExtract_Green_Channel, extractGreenChannel);
    AN_REGISTER_CHANNEL(actionExtract_Blue_Channel, extractBlueChannel);
    AN_REGISTER_CHANNEL(actionExtract_RGB_Channels, extractRGBChannels);
    AN_REGISTER_CHANNEL(actionExtract_CIE_L, extractCIEL);
    AN_REGISTER_CHANNEL(actionExtract_CIE_a, extractCIEA);
    AN_REGISTER_CHANNEL(actionExtract_CIE_b, extractCIEB);
    AN_REGISTER_CHANNEL(actionExtract_CIE_c, extractCIEC);
    AN_REGISTER_CHANNEL(actionExtract_CIE_h, extractCIEH);
    AN_REGISTER_CHANNEL(actionExtract_CIE_L_c_h, extractCIELCH);
    AN_REGISTER_CHANNEL(actionExtract_CIE_a_b, extractCIEAB);
    AN_REGISTER_CHANNEL(actionExtract_CIE_L_a_b, extractCIELAB);
    AN_REGISTER_CHANNEL(actionExtract_CIE_X, extractCIEX);
    AN_REGISTER_CHANNEL(actionExtract_CIE_Y, extractCIEY);
    AN_REGISTER_CHANNEL(actionExtract_CIE_Z, extractCIEZ);
    AN_REGISTER_CHANNEL(actionExtract_CIE_X_Y_Z, extractCIEXYZ);
    AN_REGISTER_CHANNEL(actionExtract_HSL_Hue, extractHSL_H);
    AN_REGISTER_CHANNEL(actionExtract_HSL_Saturation, extractHSL_S);
    AN_REGISTER_CHANNEL(actionExtract_HSL_Lightness, extractHSL_L);
    AN_REGISTER_CHANNEL(actionExtract_HSL, extractHSL);
    AN_REGISTER_CHANNEL(actionExtract_HSV_Hue, extractHSV_H);
    AN_REGISTER_CHANNEL(actionExtract_HSV_Saturation, extractHSV_S);
    AN_REGISTER_CHANNEL(actionExtract_HSV_Value, extractHSV_V);
    AN_REGISTER_CHANNEL(actionExtract_HSV, extractHSV);
    AN_REGISTER_CHANNEL(actionExtract_HSI_Hue, extractHSI_H);
    AN_REGISTER_CHANNEL(actionExtract_HSI_Saturation, extractHSI_S);
    AN_REGISTER_CHANNEL(actionExtract_HSI_Intensity, extractHSI_I);
    AN_REGISTER_CHANNEL(actionExtract_HSI, extractHSI);
    AN_REGISTER_CHANNEL(actionExtract_YCbCr_Luminance, extractYCbCr_Y);
    AN_REGISTER_CHANNEL(actionExtract_YCbCr_Blue_Chrominance, extractYCbCr_Cb);
    AN_REGISTER_CHANNEL(actionExtract_YCbCr_Red_Chrominance, extractYCbCr_Cr);
    AN_REGISTER_CHANNEL(actionExtract_YCbCr, extractYCbCr);
    AN_REGISTER_CHANNEL(actionConvert_to_8_bit, convertTo8Bit);
    AN_REGISTER_CHANNEL(actionConvert_to_signed_16_bit, convertToSigned16Bit);
    AN_REGISTER_CHANNEL(actionConvert_to_unsigned_16_bit, convertToUnsigned16Bit);
    AN_REGISTER_CHANNEL(actionConvert_to_signed_32_bit, convertToSigned32Bit);
    AN_REGISTER_CHANNEL(actionConvert_to_unsigned_32_bit, convertToUnsigned32Bit);
    AN_REGISTER_CHANNEL(actionConvert_to_32_bit_float, convertToFloat32);
    AN_REGISTER_CHANNEL(actionConvert_to_64_bit_float, convertToFloat64);

    AN_REGISTER_IMAGE_OPERATION(actionConvert_to_SER, convertToSERSeq);
    AN_REGISTER_IMAGE_OPERATION(actionConvert_to_Image_List, convertToImageListSeq);
    AN_REGISTER_IMAGE_OPERATION(actionExport, exportSeq);
    AN_REGISTER_IMAGE_OPERATION(actionConvert_to_FITS, convertToFITSSeq);

    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionAdjustBrightness_Contrast, adjustBrightnessAndContrast,
                               shared("Adjust Brightness And Contrast"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionGamma_Correction, gammaCorrection,
                               shared("Gamma Correction"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionExposure, exposureCorrection,
                               shared("Exposure Correction"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionAdjustHSL, adjustHSL,
                               shared("Adjust Hue/Saturation/Lightness"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionAdjustLightness_Chroma_Hue, adjustLCh,
                               shared("Adjust Lightness/Chroma/Hue"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionArcSinh_Stretch, arcSinhStretch,
                               shared("ArcSinhStretch"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionHistogram_Transformation, histogramTransformation,
                               shared("Histogram Transformation"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryTransforms, actionGeneralized_Hyperbolic_Transformation,
                               generalizedHyperbolicTransformation, shared("Generalized Hyperbolic Transformation"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryTransforms, actionCurves_Transformation, curvesTransformation,
                               shared("Curves Transformation"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryStarObject, actionStarnet, starnet, shared("Starnet++"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionStarmask_Recombination, starmaskRecombination,
                                 shared("Starmask Recombination"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionProfiled_Starmask_Recombination,
                                 profiledStarmaskRecombination, shared("Profiled Starmask Recombination"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryStarObject, actionStar_Reduction, executeStarReduction,
                               shared("Star Reduction"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionSCNR, executeSCNR, shared("SCNR"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionMedian_Filter, executeMedianFilter, shared("Median Filter"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionDeepSNR, executeDeepSNR, shared("DeepSNR"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionNon_Local_Bayes_Filter, executeNLBayesFilter,
                               shared("Non-Local Bayes"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionWavelet_Denoise_Filter, executeWaveletDenoiseFilter,
                               shared("Wavelet Denoise"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionPatch_PCA_Filter, executePatchPCAFilter,
                               shared("Patch PCA Denoise"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionLocal_Histogram_Equalization,
                               executeLocalHistogramEqualizationFilter, shared("Local Histogram Equalization"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionLocal_Contrast_Enhancement,
                               executeLocalContrastEnhancementFilter, shared("Local Contrast Enhancement"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionDark_Structure_Enhance, executeDarkStructureEnhance,
                               shared("DarkStructureEnhance"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionMultiscale_Alpha_Sharpen, executeSharpenAlphaFilter,
                               shared("Multiscale Sharpen Alpha"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionGaussian_Blur, executeGaussianBlurFilter,
                               shared("Gaussian Blur"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionHigh_Pass, executeHighPassFilter, shared("High Pass Filter"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryColor, actionChannelComposition, executeChannelComposition,
                                 shared("Channel Composition"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySensorCalibration, actionDebayer, executeImageDebayer, shared("Debayer"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySensorCalibration, actionSplit_CFA, executeSplitCFA, shared("Split CFA"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategorySensorCalibration, actionMerge_CFA, executeMergeCFA, shared("Merge CFA"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryGeometry, actionResize, executeResize, shared("Resize"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryGeometry, actionIntegerResample, executeIntegerResample,
                               shared("Integer Resample"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryGeometry, actionCFA_Integer_Resample, executeCFAIntegerResample,
                               shared("CFA Integer Resample"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryGeometry, actionCrop, executeCrop, exclusive("Crop"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryGeometry, actionRotate, executeRotate, exclusive("Rotate"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryMaskGeneration, actionMask_Drawing, executeMaskDrawing, shared("Mask Drawing"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySensorCalibration, actionRange_Selection, executeRangeSelection,
                               shared("Range Selection"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryNonInteractive, actionSelect_Mask, executeSelectMask, noProcess());
    AN_REGISTER_PROCESS_SINGLE(kCategoryNonInteractive, actionRemove_Mask, executeRemoveMask, noProcess());
    AN_REGISTER_PROCESS_SINGLE(kCategoryStarObject, actionStar_Mask, executeStarMask, shared("Star Mask"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryStarObject, actionStarnet_Star_Mask, executeStarnetStarMask,
                               shared("Starnet++ Star Mask"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionCLA_Histogram_Equalization, executeCLAHE, shared("CLAHE"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionColor_Saturation, executeColorSaturation,
                               shared("Color Saturation"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionColor_Calibration_2, executeColorCalibration,
                               exclusive("ColorCalibration"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionPhotometric_Color_Calibration, executePhotometricColorCalibration,
                               exclusive("Photometric Color Calibration"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionSpectrophotometric_Color_Calibration,
                               executeSpectrophotometricColorCalibration,
                               exclusive("Spectrophotometric Color Calibration"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionBackground_Neutralization, executeBackgroundNeutralization,
                               exclusive("Background Neutralization"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryBackgroundExtraction, actionBackground_Extraction, executeBackgroundExtraction,
                               exclusive("Background Extraction"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryBackgroundExtraction, actionAuto_Gradient_Removal, executeAutoGradientRemoval,
                               exclusiveStillImage("Auto Gradient Removal"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryTransforms, actionMultiscale_Linear_Transform, executeMultiscaleTransform,
                               shared("Multiscale Linear Transform"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionMorphological_Transformation, executeMorphologicalTransformation,
                               shared("Morphological Transformation"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionTighten_Edges, executeTightenEdges, shared("Tighten Edges"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionGRAYCstoration_Filter, executeGRAYCstoration,
                               shared("GRAYCstoration"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionCosmetic_Correction_Filter, executeCosmeticCorrection,
                               shared("Cosmetic Correction"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionBanding_Reduction_Filter, executeBandingReduction,
                               shared("Banding Reduction"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionBilateral_Filter, executeBilateralFilter,
                               shared("Bilateral Filter"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryStarObject, actionStar_Profiler_v1, executeStarProfilerV1,
                               exclusive("StarProfilerV1"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryStarObject, actionStar_Profiler_v2, executeStarProfilerV2,
                               exclusive("StarProfiler"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryToneBrightness, actionStatistical_Stretch, executeStatisticalStretch,
                               shared("Statistical Stretch"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySharpening, actionUnsharp_Mask, executeUnsharpMask, shared("Unsharp Mask"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryFilters, actionNoise_Generator, executeNoiseGenerator,
                               shared("Noise Generator"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryDenoising, actionTotal_Variation_Denoise_Filter, executeTotalVariation,
                               shared("Total Variation Denoise"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryMultiImageStacking, actionImage_Compare, executeImageCompare,
                               shared("Image Compare"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryMultiImageStacking, actionImage_Blend, executeImageBlend,
                               shared("Image Blend"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionImage_List_Analyzer, executeImageListAnalyzer,
                                 shared("Image List Analyzer"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionLRGB_Combination, executeLRGBCombination,
                               shared("LRGB Combination"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionNBRGB_Combination, executeNBRGBCombination,
                               shared("NBRGB Combination"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionNBColorMapper, executeNBColorMapper, shared("NB Color Mapper"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryColor, actionNarrowbandPaletteComposer, executeNarrowbandPaletteComposer,
                                 shared("Narrowband Palette Composer"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionColorize, executeColorize, shared("Colorize"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategorySensorCalibration, actionImage_Calibration, executeImageCalibration,
                                 shared("Image Calibration"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionImage_Stacking, executeImageStacking,
                                 shared("Image Stacking"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionImage_Registration, executeImageRegistration,
                                 shared("Image Registration"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionImage_Integration, executeImageIntegration,
                                 shared("Image Integration"));
    AN_REGISTER_PROCESS_TOPLEVEL(kCategoryMultiImageStacking, actionImage_Integration_Lite, executeImageIntegrationLite,
                                 shared("Image Integration Lite"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryColor, actionLinear_Match, executeLinearMatch, shared("Linear Match"));
    AN_REGISTER_PROCESS_SINGLE(kCategoryTransforms, actionPixel_Math_2, executePixelMath, shared("PixelMath"));
    AN_REGISTER_PROCESS_SINGLE(kCategorySensorCalibration, actionBinarize, executeBinarize, shared("Binarize"));

#undef AN_REGISTER_PROCESS_SINGLE
#undef AN_REGISTER_PROCESS_TOPLEVEL
#undef AN_REGISTER_PROCESS_NON_INTERACTIVE
#undef AN_REGISTER_CHANNEL
#undef AN_REGISTER_IMAGE_OPERATION
}

const bool kBuiltinDescriptorsRegistered = []() {
    registerBuiltinProcessDescriptors();
    return true;
}();

} // anonymous namespace
