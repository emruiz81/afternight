// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The registry every process is launched through, as the processes library registers it: every action of the stated
/// table, under its id, category and launch mode, reaching its member and through it its window or non-interactive
/// process - with each process's descriptor stating the name the process attaches to a view under, whether it needs the
/// view to itself and whether it refuses a sequence, which the launcher's guards and the menu's action state read. The
/// registry's launches reach the launcher's entry points and the channel extractor, which this suite's stand-ins
/// record.

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"
#include "fixtures/harness/QObjectCensus.h"

#include "app/MenuActionManager.h"
#include "app/ProcessRegistry.h"

#include <QAction>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::processes::Channels;
using an::tests::processes::InstalledContext;
using an::tests::processes::makeDocument;
using an::tests::processes::makeTargetView;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;
using an::tests::processes::shared::closeOpenedProcessWindows;
using an::tests::processes::shared::openedProcessWindows;
using an::tests::processes::shared::registered;
using an::tests::processes::shared::RegistrySnapshot;
using Mode = an::ui::process_registry::ProcessLaunchMode;

/// What reaches an action's work: the launcher, the channel extractor or the image operations coordinator.
enum class Kind
{
    Launcher,
    Channel,
    ImageOperation
};

/// One action as the registry holds it - its category, action, launch mode and the member it reaches - with what that
/// member opens and, for a process, the name and the exclusive access its class states and whether it refuses a
/// sequence.
struct Registration
{
    const char* category;
    const char* action;
    Mode mode;
    Kind kind;
    const char* handler;
    const char* opens;
    const char* processName;
    bool exclusive;
    bool stillImage;
};

/// Every registration the registry holds, in its order, and each process's name and exclusive access as the class's
/// own static members state them.
constexpr std::array<Registration, 122u> kRegistrations{{
    {"non_interactive", "actionExtract_Frame", Mode::NonInteractive, Kind::Launcher, "executeExtractFrame",
     "NonInteractiveProcesses::extractFrame", "", false, false},
    {"multi_image_stacking", "actionExtract_Sequence", Mode::SingleImage, Kind::Launcher, "extractSequence",
     "ExtractSequenceProcess", "Extract Sequence", false, false},
    {"non_interactive", "actionExtract_Red_Channel", Mode::NonInteractive, Kind::Channel, "extractRedChannel", "", "",
     false, false},
    {"non_interactive", "actionExtract_Green_Channel", Mode::NonInteractive, Kind::Channel, "extractGreenChannel", "",
     "", false, false},
    {"non_interactive", "actionExtract_Blue_Channel", Mode::NonInteractive, Kind::Channel, "extractBlueChannel", "", "",
     false, false},
    {"non_interactive", "actionExtract_RGB_Channels", Mode::NonInteractive, Kind::Channel, "extractRGBChannels", "", "",
     false, false},
    {"non_interactive", "actionExtract_CIE_L", Mode::NonInteractive, Kind::Channel, "extractCIEL", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_a", Mode::NonInteractive, Kind::Channel, "extractCIEA", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_b", Mode::NonInteractive, Kind::Channel, "extractCIEB", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_c", Mode::NonInteractive, Kind::Channel, "extractCIEC", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_h", Mode::NonInteractive, Kind::Channel, "extractCIEH", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_L_c_h", Mode::NonInteractive, Kind::Channel, "extractCIELCH", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_a_b", Mode::NonInteractive, Kind::Channel, "extractCIEAB", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_L_a_b", Mode::NonInteractive, Kind::Channel, "extractCIELAB", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_X", Mode::NonInteractive, Kind::Channel, "extractCIEX", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_Y", Mode::NonInteractive, Kind::Channel, "extractCIEY", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_Z", Mode::NonInteractive, Kind::Channel, "extractCIEZ", "", "", false,
     false},
    {"non_interactive", "actionExtract_CIE_X_Y_Z", Mode::NonInteractive, Kind::Channel, "extractCIEXYZ", "", "", false,
     false},
    {"non_interactive", "actionExtract_HSL_Hue", Mode::NonInteractive, Kind::Channel, "extractHSL_H", "", "", false,
     false},
    {"non_interactive", "actionExtract_HSL_Saturation", Mode::NonInteractive, Kind::Channel, "extractHSL_S", "", "",
     false, false},
    {"non_interactive", "actionExtract_HSL_Lightness", Mode::NonInteractive, Kind::Channel, "extractHSL_L", "", "",
     false, false},
    {"non_interactive", "actionExtract_HSL", Mode::NonInteractive, Kind::Channel, "extractHSL", "", "", false, false},
    {"non_interactive", "actionExtract_HSV_Hue", Mode::NonInteractive, Kind::Channel, "extractHSV_H", "", "", false,
     false},
    {"non_interactive", "actionExtract_HSV_Saturation", Mode::NonInteractive, Kind::Channel, "extractHSV_S", "", "",
     false, false},
    {"non_interactive", "actionExtract_HSV_Value", Mode::NonInteractive, Kind::Channel, "extractHSV_V", "", "", false,
     false},
    {"non_interactive", "actionExtract_HSV", Mode::NonInteractive, Kind::Channel, "extractHSV", "", "", false, false},
    {"non_interactive", "actionExtract_HSI_Hue", Mode::NonInteractive, Kind::Channel, "extractHSI_H", "", "", false,
     false},
    {"non_interactive", "actionExtract_HSI_Saturation", Mode::NonInteractive, Kind::Channel, "extractHSI_S", "", "",
     false, false},
    {"non_interactive", "actionExtract_HSI_Intensity", Mode::NonInteractive, Kind::Channel, "extractHSI_I", "", "",
     false, false},
    {"non_interactive", "actionExtract_HSI", Mode::NonInteractive, Kind::Channel, "extractHSI", "", "", false, false},
    {"non_interactive", "actionExtract_YCbCr_Luminance", Mode::NonInteractive, Kind::Channel, "extractYCbCr_Y", "", "",
     false, false},
    {"non_interactive", "actionExtract_YCbCr_Blue_Chrominance", Mode::NonInteractive, Kind::Channel, "extractYCbCr_Cb",
     "", "", false, false},
    {"non_interactive", "actionExtract_YCbCr_Red_Chrominance", Mode::NonInteractive, Kind::Channel, "extractYCbCr_Cr",
     "", "", false, false},
    {"non_interactive", "actionExtract_YCbCr", Mode::NonInteractive, Kind::Channel, "extractYCbCr", "", "", false,
     false},
    {"non_interactive", "actionConvert_to_8_bit", Mode::NonInteractive, Kind::Channel, "convertTo8Bit", "", "", false,
     false},
    {"non_interactive", "actionConvert_to_signed_16_bit", Mode::NonInteractive, Kind::Channel, "convertToSigned16Bit",
     "", "", false, false},
    {"non_interactive", "actionConvert_to_unsigned_16_bit", Mode::NonInteractive, Kind::Channel,
     "convertToUnsigned16Bit", "", "", false, false},
    {"non_interactive", "actionConvert_to_signed_32_bit", Mode::NonInteractive, Kind::Channel, "convertToSigned32Bit",
     "", "", false, false},
    {"non_interactive", "actionConvert_to_unsigned_32_bit", Mode::NonInteractive, Kind::Channel,
     "convertToUnsigned32Bit", "", "", false, false},
    {"non_interactive", "actionConvert_to_32_bit_float", Mode::NonInteractive, Kind::Channel, "convertToFloat32", "",
     "", false, false},
    {"non_interactive", "actionConvert_to_64_bit_float", Mode::NonInteractive, Kind::Channel, "convertToFloat64", "",
     "", false, false},
    {"non_interactive", "actionConvert_to_SER", Mode::NonInteractive, Kind::ImageOperation, "convertToSERSeq", "", "",
     false, false},
    {"non_interactive", "actionConvert_to_Image_List", Mode::NonInteractive, Kind::ImageOperation,
     "convertToImageListSeq", "", "", false, false},
    {"non_interactive", "actionExport", Mode::NonInteractive, Kind::ImageOperation, "exportSeq", "", "", false, false},
    {"non_interactive", "actionConvert_to_FITS", Mode::NonInteractive, Kind::ImageOperation, "convertToFITSSeq", "", "",
     false, false},
    {"tone_brightness", "actionAdjustBrightness_Contrast", Mode::SingleImage, Kind::Launcher,
     "adjustBrightnessAndContrast", "AdjustBrightnessAndContrastProcess", "Adjust Brightness And Contrast", false,
     false},
    {"tone_brightness", "actionGamma_Correction", Mode::SingleImage, Kind::Launcher, "gammaCorrection",
     "GammaCorrectionProcess", "Gamma Correction", false, false},
    {"tone_brightness", "actionExposure", Mode::SingleImage, Kind::Launcher, "exposureCorrection",
     "ExposureCorrectionProcess", "Exposure Correction", false, false},
    {"tone_brightness", "actionAdjustHSL", Mode::SingleImage, Kind::Launcher, "adjustHSL", "AdjustHSLProcess",
     "Adjust Hue/Saturation/Lightness", false, false},
    {"tone_brightness", "actionAdjustLightness_Chroma_Hue", Mode::SingleImage, Kind::Launcher, "adjustLCh",
     "AdjustLChProcess", "Adjust Lightness/Chroma/Hue", false, false},
    {"tone_brightness", "actionArcSinh_Stretch", Mode::SingleImage, Kind::Launcher, "arcSinhStretch",
     "ArcSinhStretchProcess", "ArcSinhStretch", false, false},
    {"tone_brightness", "actionHistogram_Transformation", Mode::SingleImage, Kind::Launcher, "histogramTransformation",
     "HistogramTransformationProcess", "Histogram Transformation", false, false},
    {"transforms", "actionGeneralized_Hyperbolic_Transformation", Mode::SingleImage, Kind::Launcher,
     "generalizedHyperbolicTransformation", "GHTransformationProcess", "Generalized Hyperbolic Transformation", false,
     false},
    {"transforms", "actionCurves_Transformation", Mode::SingleImage, Kind::Launcher, "curvesTransformation",
     "CurvesTransformationProcess", "Curves Transformation", false, false},
    {"star_object", "actionStarnet", Mode::SingleImage, Kind::Launcher, "starnet", "StarnetProcess", "Starnet++", false,
     false},
    {"multi_image_stacking", "actionStarmask_Recombination", Mode::TopLevel, Kind::Launcher, "starmaskRecombination",
     "StarmaskRecombinationProcess", "Starmask Recombination", false, false},
    {"multi_image_stacking", "actionProfiled_Starmask_Recombination", Mode::TopLevel, Kind::Launcher,
     "profiledStarmaskRecombination", "ProfiledStarmaskRecombinationProcess", "Profiled Starmask Recombination", false,
     false},
    {"star_object", "actionStar_Reduction", Mode::SingleImage, Kind::Launcher, "executeStarReduction",
     "StarReductionProcess", "Star Reduction", false, false},
    {"color", "actionSCNR", Mode::SingleImage, Kind::Launcher, "executeSCNR", "SCNRProcess", "SCNR", false, false},
    {"filters", "actionMedian_Filter", Mode::SingleImage, Kind::Launcher, "executeMedianFilter", "MedianFilterProcess",
     "Median Filter", false, false},
    {"denoising", "actionDeepSNR", Mode::SingleImage, Kind::Launcher, "executeDeepSNR", "DeepSNRProcess", "DeepSNR",
     false, false},
    {"denoising", "actionNon_Local_Bayes_Filter", Mode::SingleImage, Kind::Launcher, "executeNLBayesFilter",
     "NLBayesProcess", "Non-Local Bayes", false, false},
    {"denoising", "actionWavelet_Denoise_Filter", Mode::SingleImage, Kind::Launcher, "executeWaveletDenoiseFilter",
     "WaveletDenoiseProcess", "Wavelet Denoise", false, false},
    {"denoising", "actionPatch_PCA_Filter", Mode::SingleImage, Kind::Launcher, "executePatchPCAFilter",
     "PatchPCADenoiseProcess", "Patch PCA Denoise", false, false},
    {"sharpening_enhancement", "actionLocal_Histogram_Equalization", Mode::SingleImage, Kind::Launcher,
     "executeLocalHistogramEqualizationFilter", "LocalHistogramEqualizationProcess", "Local Histogram Equalization",
     false, false},
    {"sharpening_enhancement", "actionLocal_Contrast_Enhancement", Mode::SingleImage, Kind::Launcher,
     "executeLocalContrastEnhancementFilter", "LocalContrastEnhancementProcess", "Local Contrast Enhancement", false,
     false},
    {"sharpening_enhancement", "actionDark_Structure_Enhance", Mode::SingleImage, Kind::Launcher,
     "executeDarkStructureEnhance", "DarkStructureEnhanceProcess", "DarkStructureEnhance", false, false},
    {"sharpening_enhancement", "actionMultiscale_Alpha_Sharpen", Mode::SingleImage, Kind::Launcher,
     "executeSharpenAlphaFilter", "SharpenAlphaProcess", "Multiscale Sharpen Alpha", false, false},
    {"filters", "actionGaussian_Blur", Mode::SingleImage, Kind::Launcher, "executeGaussianBlurFilter",
     "GaussianBlurProcess", "Gaussian Blur", false, false},
    {"sharpening_enhancement", "actionHigh_Pass", Mode::SingleImage, Kind::Launcher, "executeHighPassFilter",
     "HighPassFilterProcess", "High Pass Filter", false, false},
    {"color", "actionChannelComposition", Mode::TopLevel, Kind::Launcher, "executeChannelComposition",
     "ChannelCompositionProcess", "Channel Composition", false, false},
    {"sensor_calibration", "actionDebayer", Mode::SingleImage, Kind::Launcher, "executeImageDebayer", "DebayerProcess",
     "Debayer", false, false},
    {"sensor_calibration", "actionSplit_CFA", Mode::SingleImage, Kind::Launcher, "executeSplitCFA", "SplitCFAProcess",
     "Split CFA", false, false},
    {"sensor_calibration", "actionMerge_CFA", Mode::TopLevel, Kind::Launcher, "executeMergeCFA", "MergeCFAProcess",
     "Merge CFA", false, false},
    {"geometry", "actionResize", Mode::SingleImage, Kind::Launcher, "executeResize", "ResizeProcess", "Resize", false,
     false},
    {"geometry", "actionIntegerResample", Mode::SingleImage, Kind::Launcher, "executeIntegerResample",
     "IntegerResampleProcess", "Integer Resample", false, false},
    {"geometry", "actionCFA_Integer_Resample", Mode::SingleImage, Kind::Launcher, "executeCFAIntegerResample",
     "CFAIntegerResampleProcess", "CFA Integer Resample", false, false},
    {"geometry", "actionCrop", Mode::SingleImage, Kind::Launcher, "executeCrop", "CropProcess", "Crop", true, false},
    {"geometry", "actionRotate", Mode::SingleImage, Kind::Launcher, "executeRotate", "RotateProcess", "Rotate", true,
     false},
    {"mask_generation", "actionMask_Drawing", Mode::SingleImage, Kind::Launcher, "executeMaskDrawing",
     "MaskDrawingProcess", "Mask Drawing", false, false},
    {"sensor_calibration", "actionRange_Selection", Mode::SingleImage, Kind::Launcher, "executeRangeSelection",
     "RangeSelectionProcess", "Range Selection", false, false},
    {"non_interactive", "actionSelect_Mask", Mode::SingleImage, Kind::Launcher, "executeSelectMask",
     "ImageMaskSelectorControl", "", false, false},
    {"non_interactive", "actionRemove_Mask", Mode::SingleImage, Kind::Launcher, "executeRemoveMask", "", "", false,
     false},
    {"star_object", "actionStar_Mask", Mode::SingleImage, Kind::Launcher, "executeStarMask", "StarMaskProcess",
     "Star Mask", false, false},
    {"star_object", "actionStarnet_Star_Mask", Mode::SingleImage, Kind::Launcher, "executeStarnetStarMask",
     "StarnetStarMaskProcess", "Starnet++ Star Mask", false, false},
    {"sharpening_enhancement", "actionCLA_Histogram_Equalization", Mode::SingleImage, Kind::Launcher, "executeCLAHE",
     "CLAHEProcess", "CLAHE", false, false},
    {"color", "actionColor_Saturation", Mode::SingleImage, Kind::Launcher, "executeColorSaturation",
     "ColorSaturationProcess", "Color Saturation", false, false},
    {"color", "actionColor_Calibration_2", Mode::SingleImage, Kind::Launcher, "executeColorCalibration",
     "ColorCalibrationProcess", "ColorCalibration", true, false},
    {"color", "actionPhotometric_Color_Calibration", Mode::SingleImage, Kind::Launcher,
     "executePhotometricColorCalibration", "PhotometricColorCalibrationProcess", "Photometric Color Calibration", true,
     false},
    {"color", "actionSpectrophotometric_Color_Calibration", Mode::SingleImage, Kind::Launcher,
     "executeSpectrophotometricColorCalibration", "SpectrophotometricColorCalibrationProcess",
     "Spectrophotometric Color Calibration", true, false},
    {"color", "actionBackground_Neutralization", Mode::SingleImage, Kind::Launcher, "executeBackgroundNeutralization",
     "BackgroundNeutralizationProcess", "Background Neutralization", true, false},
    {"background_extraction", "actionBackground_Extraction", Mode::SingleImage, Kind::Launcher,
     "executeBackgroundExtraction", "BackgroundExtractionProcess", "Background Extraction", true, false},
    {"background_extraction", "actionAuto_Gradient_Removal", Mode::SingleImage, Kind::Launcher,
     "executeAutoGradientRemoval", "AutoGradientRemovalProcess", "Auto Gradient Removal", true, true},
    {"transforms", "actionMultiscale_Linear_Transform", Mode::SingleImage, Kind::Launcher, "executeMultiscaleTransform",
     "MultiscaleLinearProcess", "Multiscale Linear Transform", false, false},
    {"filters", "actionMorphological_Transformation", Mode::SingleImage, Kind::Launcher,
     "executeMorphologicalTransformation", "MorphologicalTransformationProcess", "Morphological Transformation", false,
     false},
    {"sharpening_enhancement", "actionTighten_Edges", Mode::SingleImage, Kind::Launcher, "executeTightenEdges",
     "TightenEdgesProcess", "Tighten Edges", false, false},
    {"denoising", "actionGRAYCstoration_Filter", Mode::SingleImage, Kind::Launcher, "executeGRAYCstoration",
     "GRAYCstorationProcess", "GRAYCstoration", false, false},
    {"filters", "actionCosmetic_Correction_Filter", Mode::SingleImage, Kind::Launcher, "executeCosmeticCorrection",
     "CosmeticCorrectionProcess", "Cosmetic Correction", false, false},
    {"filters", "actionBanding_Reduction_Filter", Mode::SingleImage, Kind::Launcher, "executeBandingReduction",
     "BandingReductionProcess", "Banding Reduction", false, false},
    {"filters", "actionBilateral_Filter", Mode::SingleImage, Kind::Launcher, "executeBilateralFilter",
     "BilateralFilterProcess", "Bilateral Filter", false, false},
    {"star_object", "actionStar_Profiler_v1", Mode::SingleImage, Kind::Launcher, "executeStarProfilerV1",
     "StarProfilerProcessV1", "StarProfilerV1", true, false},
    {"star_object", "actionStar_Profiler_v2", Mode::SingleImage, Kind::Launcher, "executeStarProfilerV2",
     "StarProfilerProcessV2", "StarProfiler", true, false},
    {"tone_brightness", "actionStatistical_Stretch", Mode::SingleImage, Kind::Launcher, "executeStatisticalStretch",
     "StatisticalStretch", "Statistical Stretch", false, false},
    {"sharpening_enhancement", "actionUnsharp_Mask", Mode::SingleImage, Kind::Launcher, "executeUnsharpMask",
     "UnsharpMaskProcess", "Unsharp Mask", false, false},
    {"filters", "actionNoise_Generator", Mode::SingleImage, Kind::Launcher, "executeNoiseGenerator",
     "NoiseGeneratorProcess", "Noise Generator", false, false},
    {"denoising", "actionTotal_Variation_Denoise_Filter", Mode::SingleImage, Kind::Launcher, "executeTotalVariation",
     "TotalVariationDenoiseProcess", "Total Variation Denoise", false, false},
    {"multi_image_stacking", "actionImage_Compare", Mode::SingleImage, Kind::Launcher, "executeImageCompare",
     "ImageCompareProcess", "Image Compare", false, false},
    {"multi_image_stacking", "actionImage_Blend", Mode::SingleImage, Kind::Launcher, "executeImageBlend",
     "ImageBlendProcess", "Image Blend", false, false},
    {"multi_image_stacking", "actionImage_List_Analyzer", Mode::TopLevel, Kind::Launcher, "executeImageListAnalyzer",
     "ImageListAnalyzerProcess", "Image List Analyzer", false, false},
    {"color", "actionLRGB_Combination", Mode::SingleImage, Kind::Launcher, "executeLRGBCombination",
     "LRGBCombinationProcess", "LRGB Combination", false, false},
    {"color", "actionNBRGB_Combination", Mode::SingleImage, Kind::Launcher, "executeNBRGBCombination",
     "NBRGBCombinationProcess", "NBRGB Combination", false, false},
    {"color", "actionNBColorMapper", Mode::SingleImage, Kind::Launcher, "executeNBColorMapper", "NBColorMapperProcess",
     "NB Color Mapper", false, false},
    {"color", "actionNarrowbandPaletteComposer", Mode::TopLevel, Kind::Launcher, "executeNarrowbandPaletteComposer",
     "NarrowbandPaletteComposerProcess", "Narrowband Palette Composer", false, false},
    {"color", "actionColorize", Mode::SingleImage, Kind::Launcher, "executeColorize", "ColorizeProcess", "Colorize",
     false, false},
    {"sensor_calibration", "actionImage_Calibration", Mode::TopLevel, Kind::Launcher, "executeImageCalibration",
     "ImageCalibrationProcess", "Image Calibration", false, false},
    {"multi_image_stacking", "actionImage_Stacking", Mode::TopLevel, Kind::Launcher, "executeImageStacking",
     "ImageStackingProcess", "Image Stacking", false, false},
    {"multi_image_stacking", "actionImage_Registration", Mode::TopLevel, Kind::Launcher, "executeImageRegistration",
     "ImageRegistrationProcess", "Image Registration", false, false},
    {"multi_image_stacking", "actionImage_Integration", Mode::TopLevel, Kind::Launcher, "executeImageIntegration",
     "ImageIntegrationProcess", "Image Integration", false, false},
    {"multi_image_stacking", "actionImage_Integration_Lite", Mode::TopLevel, Kind::Launcher,
     "executeImageIntegrationLite", "ImageIntegrationLiteProcess", "Image Integration Lite", false, false},
    {"color", "actionLinear_Match", Mode::SingleImage, Kind::Launcher, "executeLinearMatch", "LinearMatchProcess",
     "Linear Match", false, false},
    {"transforms", "actionPixel_Math_2", Mode::SingleImage, Kind::Launcher, "executePixelMath", "PixelMathProcess",
     "PixelMath", false, false},
    {"sensor_calibration", "actionBinarize", Mode::SingleImage, Kind::Launcher, "executeBinarize", "BinarizeProcess",
     "Binarize", false, false},
}};

/// The id an action is registered under.
[[nodiscard]] std::string idOf(const Registration& registration)
{
    return std::string(registration.category) + "." + registration.action;
}

/// A view of a colour Float32 image of eight by six, made the context's active view.
[[nodiscard]] ViewHandle activeColourView(ProcessTestContext& context)
{
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context.engineSession(), 8u, 6u, Channels::Rgb,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return static_cast<float>(x + y + c) / 32.0f; });
    EXPECT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("target", std::move(*document));
    context.addViewControl(view.get());
    context.setActiveImageViewControl(view.get());
    return view;
}

/// Runs a descriptor's launch the way the application does for its launch mode, on a view.
void launch(const an::ui::process_registry::ProcessDescriptor& descriptor, ImageViewControl* view)
{
    switch (descriptor.launch_mode)
    {
    case Mode::SingleImage:
        ASSERT_TRUE(static_cast<bool>(descriptor.launch_single_image)) << descriptor.id;
        descriptor.launch_single_image(view);
        break;
    case Mode::TopLevel:
        ASSERT_TRUE(static_cast<bool>(descriptor.launch_top_level)) << descriptor.id;
        descriptor.launch_top_level();
        break;
    case Mode::NonInteractive:
        ASSERT_TRUE(static_cast<bool>(descriptor.launch_non_interactive)) << descriptor.id;
        descriptor.launch_non_interactive();
        break;
    }
}

} // namespace

// The registry holds every action of the table and nothing else, in its order, each under its id, category, action
// and launch mode, with the one launch the mode calls for. The ids the library registered are recorded beside the
// result, in order, so a reader of the results sees the registry the library builds rather than its source.
TEST(ProcessRegistryTest, TheRegistryHoldsEveryStatedActionAndNothingElse)
{
    const std::vector<an::ui::process_registry::ProcessDescriptor>& descriptors =
        an::ui::process_registry::ProcessRegistry::instance().all();
    std::string ids;
    for (const an::ui::process_registry::ProcessDescriptor& descriptor : descriptors)
    {
        ids += (ids.empty() ? "" : " ") + descriptor.id;
    }
    RecordProperty("registeredDescriptors", ids);
    ASSERT_EQ(descriptors.size(), kRegistrations.size());
    for (std::size_t index = 0u; index < kRegistrations.size(); ++index)
    {
        const Registration& expected = kRegistrations[index];
        const an::ui::process_registry::ProcessDescriptor& descriptor = descriptors[index];
        EXPECT_EQ(descriptor.id, idOf(expected)) << index;
        EXPECT_EQ(descriptor.category, expected.category) << descriptor.id;
        EXPECT_EQ(descriptor.action_object_name, expected.action) << descriptor.id;
        EXPECT_EQ(descriptor.launch_mode, expected.mode) << descriptor.id;
        EXPECT_EQ(static_cast<bool>(descriptor.launch_single_image), expected.mode == Mode::SingleImage)
            << descriptor.id;
        EXPECT_EQ(static_cast<bool>(descriptor.launch_top_level), expected.mode == Mode::TopLevel) << descriptor.id;
        EXPECT_EQ(static_cast<bool>(descriptor.launch_non_interactive), expected.mode == Mode::NonInteractive)
            << descriptor.id;
        EXPECT_TRUE(descriptor.is_available()) << descriptor.id;
        EXPECT_FALSE(descriptor.is_extension_process) << descriptor.id;
    }
    EXPECT_TRUE(an::ui::process_registry::ProcessRegistry::instance().validateUniqueDescriptorIds());
}

// A process's descriptor states the name its window attaches to a view under, whether it needs the view to itself and
// whether it refuses a sequence - what its class says of itself - so the menu's action state and the launcher read one
// fact. An action that opens no process of its own keeps the name the menu gives it.
TEST(ProcessRegistryTest, EveryProcessDescriptorStatesWhatItsProcessSaysOfItself)
{
    for (const Registration& expected : kRegistrations)
    {
        const an::ui::process_registry::ProcessDescriptor* descriptor = registered(idOf(expected));
        ASSERT_NE(descriptor, nullptr) << idOf(expected);
        if (std::string(expected.processName).empty())
        {
            EXPECT_EQ(descriptor->process_name, descriptor->action_object_name) << descriptor->id;
        }
        else
        {
            EXPECT_EQ(descriptor->process_name, expected.processName) << descriptor->id;
        }
        EXPECT_EQ(descriptor->requires_exclusive_access, expected.exclusive) << descriptor->id;
        const bool stillByRole = expected.action == std::string("actionNBRGB_Combination") ||
                                 expected.action == std::string("actionNBColorMapper");
        EXPECT_EQ(descriptor->requires_still_image, expected.stillImage || stillByRole) << descriptor->id;
    }
}

// The menu names each descriptor by its action's text when it binds the action, and leaves the name the process
// attaches to a view under, which the guards and an attached window's exclusive access are read by; an action that
// opens no process of its own is named by the menu throughout.
TEST(ProcessRegistryTest, TheMenuNamesADescriptorWithoutRenamingItsProcess)
{
    const RegistrySnapshot snapshot;
    QObject signalContext;
    an::ui::MenuActionManager::Dependencies dependencies;
    dependencies.signalContext = &signalContext;
    const an::ui::MenuActionManager menu(dependencies);

    for (const Registration& expected : kRegistrations)
    {
        QAction action(QStringLiteral("&Menu text of %1...").arg(QString::fromLatin1(expected.action)));
        action.setObjectName(QString::fromLatin1(expected.action));
        menu.bindRegistryAction(&action, expected.category);

        const auto* descriptor = registered(idOf(expected));
        ASSERT_NE(descriptor, nullptr) << idOf(expected);
        EXPECT_EQ(descriptor->display_name, "Menu text of " + std::string(expected.action) + "...") << descriptor->id;
        if (std::string(expected.processName).empty())
        {
            EXPECT_EQ(descriptor->process_name, descriptor->display_name) << descriptor->id;
        }
        else
        {
            EXPECT_EQ(descriptor->process_name, expected.processName) << descriptor->id;
        }
    }
}

// An extension's action stays enabled while its environment can be prepared - a launch prepares it - and says why the
// environment is not ready yet; one whose environment cannot be prepared, an incompatible one here, is disabled.
TEST(ProcessRegistryTest, AnExtensionsActionStaysEnabledWhileItsEnvironmentCanBePrepared)
{
    using an::ui::process_registry::ProcessAvailability;

    const RegistrySnapshot snapshot;
    auto& registry = an::ui::process_registry::ProcessRegistry::instance();
    const auto registerExtensionProcess = [&registry](const std::string& name, const ProcessAvailability availability,
                                                      const std::string& reason) {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = "extensions." + name;
        descriptor.display_name = name;
        descriptor.category = "extensions";
        descriptor.action_object_name = "action_" + name;
        descriptor.process_name = name;
        descriptor.is_extension_process = true;
        descriptor.extension_id = name;
        descriptor.availability = availability;
        descriptor.availability_reason = reason;
        return registry.registerProcess(std::move(descriptor));
    };
    ASSERT_TRUE(registerExtensionProcess("preparable", ProcessAvailability::InstallNeeded,
                                         "Extension environment has not been provisioned yet."));
    ASSERT_TRUE(registerExtensionProcess("incompatible", ProcessAvailability::Incompatible,
                                         "Requires AfterNight 99.0 or newer."));

    QObject signalContext;
    an::ui::MenuActionManager::Dependencies dependencies;
    dependencies.signalContext = &signalContext;
    const an::ui::MenuActionManager menu(dependencies);

    QAction preparable(QStringLiteral("Preparable"));
    preparable.setObjectName(QStringLiteral("action_preparable"));
    menu.bindRegistryAction(&preparable, "extensions", "extensions.preparable");
    EXPECT_TRUE(preparable.isEnabled());
    EXPECT_EQ(preparable.toolTip(), QStringLiteral("Extension environment has not been provisioned yet."));

    QAction incompatible(QStringLiteral("Incompatible"));
    incompatible.setObjectName(QStringLiteral("action_incompatible"));
    menu.bindRegistryAction(&incompatible, "extensions", "extensions.incompatible");
    EXPECT_FALSE(incompatible.isEnabled());
}

// The roles the registry states beside the facts: the starless and star-mask outputs of StarNet, the colour-only NBRGB
// combination, and the narrowband tools' still-image requirement.
TEST(ProcessRegistryTest, TheRegistryStatesTheRolesBesideTheFacts)
{
    const auto* starnet = registered("star_object.actionStarnet");
    ASSERT_NE(starnet, nullptr);
    EXPECT_TRUE(starnet->provides_starless_output);
    EXPECT_TRUE(starnet->provides_star_mask_output);
    const auto* starMask = registered("star_object.actionStarnet_Star_Mask");
    ASSERT_NE(starMask, nullptr);
    EXPECT_FALSE(starMask->provides_starless_output);
    EXPECT_TRUE(starMask->provides_star_mask_output);
    const auto* nbrgb = registered("color.actionNBRGB_Combination");
    ASSERT_NE(nbrgb, nullptr);
    EXPECT_EQ(nbrgb->source_channel_filter, std::vector<int>{3});
    EXPECT_TRUE(nbrgb->requires_still_image);
    const auto* palette = registered("color.actionNarrowbandPaletteComposer");
    ASSERT_NE(palette, nullptr);
    EXPECT_FALSE(palette->requires_still_image);
    for (const auto& descriptor : an::ui::process_registry::ProcessRegistry::instance().all())
    {
        if (descriptor.id != "color.actionNBRGB_Combination")
        {
            EXPECT_TRUE(descriptor.source_channel_filter.empty()) << descriptor.id;
        }
        if (descriptor.id != "star_object.actionStarnet" && descriptor.id != "star_object.actionStarnet_Star_Mask")
        {
            EXPECT_FALSE(descriptor.provides_star_mask_output) << descriptor.id;
        }
    }
}

// Every process action opens, through its launcher member and acting for its own descriptor, the window of the process
// that member opens - one window, shown, on the active view for a single-image process and
// over no view for a top-level one - and says nothing doing so. This launches every registered process window the
// library defines, so a registration whose entry point opens no window, the wrong one or one that cannot be built
// fails here.
TEST(ProcessRegistryTest, EveryProcessActionOpensItsProcessWindow)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeColourView(*context);
    closeOpenedProcessWindows();

    int windows = 0;
    for (const Registration& expected : kRegistrations)
    {
        const std::string handler = expected.handler;
        if (expected.kind != Kind::Launcher || handler == "executeSelectMask" || handler == "executeRemoveMask" ||
            handler == "executeExtractFrame")
        {
            continue;
        }
        const auto* descriptor = registered(idOf(expected));
        ASSERT_NE(descriptor, nullptr) << idOf(expected);
        launch(*descriptor, view.get());
        const std::vector<an::tests::processes::shared::OpenedProcessWindow> opened = openedProcessWindows();
        ASSERT_EQ(opened.size(), 1u) << descriptor->id;
        EXPECT_EQ(opened[0].className, expected.opens) << descriptor->id;
        EXPECT_TRUE(opened[0].window->isVisible()) << descriptor->id;
        EXPECT_EQ(opened[0].view, expected.mode == Mode::TopLevel ? nullptr : view.get()) << descriptor->id;
        ++windows;
        closeOpenedProcessWindows();
    }
    EXPECT_EQ(windows, 76);
    EXPECT_TRUE(context->messages.isEmpty()) << context->messages.join(QStringLiteral(", ")).toStdString();
}

// The census sets aside what a thread pool makes when it starts a thread - the thread and its event dispatcher - which
// the pool keeps until the thread has idled past its expiry, whoever's work started it; otherwise a thread the global
// pool starts while the image comparison is open would be counted as the window's.
TEST(ProcessRegistryTest, TheCensusSetsAsideTheThreadsAPoolStarts)
{
    QThreadPool pool;
    pool.setMaxThreadCount(1);
    std::vector<std::string> survivors;
    {
        an::tests::harness::QObjectCensus census;
        ASSERT_TRUE(census.usable());
        // The task's end, not the pool's: waiting for the pool to be done ends its threads.
        std::atomic<bool> ran{false};
        pool.start([&ran]() { ran = true; });
        ASSERT_TRUE(an::tests::processes::pumpUntil([&ran]() { return ran.load(); }));
        survivors = census.survivors();
    }
    EXPECT_TRUE(survivors.empty()) << an::tests::harness::listed(survivors);
}

// Every process window, opened as its action opens it and closed, leaves nothing it made behind - no layout taken out
// of its parent and kept by no one, no widget made without one: each is opened twice, the second time inside a census
// of the Qt objects made, so what a first opening makes for every later one is not counted.
TEST(ProcessRegistryTest, EveryProcessWindowClosedLeavesNothingItMadeBehind)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeColourView(*context);
    closeOpenedProcessWindows();

    int windows = 0;
    for (const Registration& expected : kRegistrations)
    {
        const std::string handler = expected.handler;
        if (expected.kind != Kind::Launcher || handler == "executeSelectMask" || handler == "executeRemoveMask" ||
            handler == "executeExtractFrame")
        {
            continue;
        }
        const auto* descriptor = registered(idOf(expected));
        ASSERT_NE(descriptor, nullptr) << idOf(expected);
        const std::vector<std::string> survivors = an::tests::harness::leftBehindBy([&descriptor, &view]() {
            launch(*descriptor, view.get());
            EXPECT_EQ(openedProcessWindows().size(), 1u) << descriptor->id;
            (void)an::tests::processes::pumpUntil(
                []() { return QThreadPool::globalInstance()->activeThreadCount() == 0; });
            closeOpenedProcessWindows();
            (void)an::tests::processes::pumpUntil(
                []() { return QThreadPool::globalInstance()->activeThreadCount() == 0; });
        });
        EXPECT_TRUE(survivors.empty()) << descriptor->id << " left behind:" << an::tests::harness::listed(survivors);
        ++windows;
    }
    EXPECT_EQ(windows, 76);
}

// Extract Frame runs the non-interactive frame extraction on the active view, which refuses a still image by saying
// there is no sequence, and opens no window.
TEST(ProcessRegistryTest, ExtractFrameOfAStillImageSaysThereIsNoSequence)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeColourView(*context);

    const auto* descriptor = registered("non_interactive.actionExtract_Frame");
    ASSERT_NE(descriptor, nullptr);
    launch(*descriptor, view.get());
    EXPECT_TRUE(openedProcessWindows().empty());
    EXPECT_TRUE(context->madeViews.empty());
    ASSERT_EQ(context->messages.size(), 1);
    EXPECT_TRUE(context->messages.front().startsWith(QStringLiteral("error:")))
        << context->messages.front().toStdString();
}

// Every channel extraction and conversion runs, through its extractor member, the non-interactive process of the same
// name on the active view, which opens what it makes as new views and says nothing; with no view it says so and runs
// nothing.
TEST(ProcessRegistryTest, EveryChannelActionReachesTheExtractionOfItsName)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeColourView(*context);

    int channels = 0;
    for (const Registration& expected : kRegistrations)
    {
        if (expected.kind != Kind::Channel)
        {
            continue;
        }
        ++channels;
        const auto* descriptor = registered(idOf(expected));
        ASSERT_NE(descriptor, nullptr) << idOf(expected);
        const std::size_t before = context->madeViews.size();
        launch(*descriptor, view.get());
        EXPECT_TRUE(an::tests::processes::pumpUntil([&] { return context->madeViews.size() > before; }))
            << descriptor->id;
        EXPECT_TRUE(openedProcessWindows().empty()) << descriptor->id;
        EXPECT_TRUE(context->messages.isEmpty())
            << descriptor->id << ": " << context->messages.join(QStringLiteral(", ")).toStdString();
        context->messages.clear();
    }
    EXPECT_EQ(channels, 39);

    context->setActiveImageViewControl(nullptr);
    const std::size_t before = context->madeViews.size();
    const auto* red = registered("non_interactive.actionExtract_Red_Channel");
    ASSERT_NE(red, nullptr);
    launch(*red, nullptr);
    an::tests::processes::pumpQuietly();
    EXPECT_EQ(context->madeViews.size(), before);
    EXPECT_EQ(context->messages, QStringList{QStringLiteral("error:Extract channel")});
}

// Mask Drawing is registered as the mask generation menu's single-image process, under its own name, and opens the
// mask drawing window on the active view through the launcher's mask drawing member.
TEST(ProcessRegistryTest, MaskDrawingIsRegisteredAndOpensTheMaskDrawingWindow)
{
    const RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::tests::processes::shared::ClosesOpenedWindows closesWindows;
    ViewHandle view = activeColourView(*context);

    const auto* descriptor = registered("mask_generation.actionMask_Drawing");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->category, "mask_generation");
    EXPECT_EQ(descriptor->launch_mode, Mode::SingleImage);
    EXPECT_EQ(descriptor->process_name, "Mask Drawing");
    closeOpenedProcessWindows();
    launch(*descriptor, view.get());
    const std::vector<an::tests::processes::shared::OpenedProcessWindow> opened = openedProcessWindows();
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0].className, "MaskDrawingProcess");
    EXPECT_EQ(opened[0].view, view.get());
    closeOpenedProcessWindows();
}
