// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The launcher's entry point into each process: the member that opens a process's window on the active view, or
/// a top-level window, and the non-interactive extraction of a sequence frame.
///
/// Each needs its process's header, so this unit compiles only with every process - with the processes library
/// whole. The launcher's guards, which read the descriptor a launcher acts for, are `app/ProcessLauncher.cpp`.
#include "stdafx.hpp"

#include "app/ProcessLauncher.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"

#include "processes/ProcessCommonUtils.h"

#include "processes/AdjustBrightnessAndContrastProcess.h"
#include "processes/AdjustHSLProcess.h"
#include "processes/AdjustLChProcess.h"
#include "processes/ArcSinhStretchProcess.h"
#include "processes/AutoGradientRemovalProcess.h"
#include "processes/BandingReductionProcess.h"
#include "processes/BackgroundExtractionProcess.h"
#include "processes/BackgroundNeutralizationProcess.h"
#include "processes/BilateralFilterProcess.h"
#include "processes/BinarizeProcess.h"
#include "processes/CFAIntegerResampleProcess.h"
#include "processes/ChannelCompositionProcess.h"
#include "processes/CLAHEProcess.h"
#include "processes/ColorCalibrationProcess.h"
#include "processes/ColorizeProcess.h"
#include "processes/ColorSaturationProcess.h"
#include "processes/CosmeticCorrectionProcess.h"
#include "processes/CropProcess.h"
#include "processes/CurvesTransformationProcess.h"
#include "processes/DarkStructureEnhanceProcess.h"
#include "processes/DeepSNRProcess.h"
#include "processes/DebayerProcess.h"
#include "processes/ExposureCorrectionProcess.h"
#include "processes/ExtractSequenceProcess.h"
#include "processes/GammaCorrectionProcess.h"
#include "processes/GaussianBlurProcess.h"
#include "processes/GHTransformationProcess.h"
#include "processes/GRAYCstorationProcess.h"
#include "processes/HighPassFilterProcess.h"
#include "processes/HistogramTransformationProcess.h"
#include "processes/ImageBlendProcess.h"
#include "processes/ImageCalibrationProcess.h"
#include "processes/ImageCompareProcess.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegrationLiteProcess.h"
#include "processes/ImageListAnalyzerProcess.h"
#include "processes/ImageRegistrationProcess.h"
#include "processes/ImageStackingProcess.h"
#include "processes/IntegerResampleProcess.h"
#include "processes/LinearMatchProcess.h"
#include "processes/LocalContrastEnhancementProcess.h"
#include "processes/LocalHistogramEqualizationProcess.h"
#include "processes/LRGBCombinationProcess.h"
#include "processes/MaskDrawingProcess.h"
#include "processes/MedianFilterProcess.h"
#include "processes/MergeCFAProcess.h"
#include "processes/MorphologicalTransformationProcess.h"
#include "processes/MultiscaleLinearProcess.h"
#include "processes/NLBayesProcess.h"
#include "processes/NBColorMapperProcess.h"
#include "processes/NBRGBCombinationProcess.h"
#include "processes/NoiseGeneratorProcess.h"
#include "processes/NonInteractiveProcesses.h"
#include "processes/PatchPCADenoiseProcess.h"
#include "processes/NarrowbandPaletteComposerProcess.h"
#include "processes/PixelMathProcess.h"
#include "processes/ProfiledStarmaskRecombinationProcess.h"
#include "processes/PhotometricColorCalibration/PhotometricColorCalibrationProcess.h"
#include "processes/RangeSelectionProcess.h"
#include "processes/ResizeProcess.h"
#include "processes/RotateProcess.h"
#include "processes/SCNRProcess.h"
#include "processes/SharpenAlphaProcess.h"
#include "processes/SplitCFAProcess.h"
#include "processes/StarReductionProcess.h"
#include "processes/StarMaskProcess.h"
#include "processes/StarProfilerProcessV1.h"
#include "processes/StarProfilerProcessV2.h"
#include "processes/SpectrophotometricColorCalibration/SpectrophotometricColorCalibrationProcess.h"
#include "processes/StarnetProcess.h"
#include "processes/StarnetStarMaskProcess.h"
#include "processes/StarmaskRecombinationProcess.h"
#include "processes/StatisticalStretch.h"
#include "processes/TightenEdgesProcess.h"
#include "processes/TotalVariationDenoiseProcess.h"
#include "processes/UnsharpMaskProcess.h"
#include "processes/WaveletDenoiseProcess.h"

namespace an::ui
{

#define LAUNCH_SINGLE_IMAGE_PROCESS(PROCESS_TYPE)            \
    launchSingleImageProcess([](ImageViewControl* control) { \
        auto* process = new PROCESS_TYPE(control);           \
        process->show();                                     \
    })

#define LAUNCH_TOP_LEVEL_WITH_APP(PROCESS_TYPE)                               \
    launchTopLevelProcess([]() {                                              \
        auto* process = new PROCESS_TYPE(process_utils::currentMainWindow()); \
        process->show();                                                      \
    })

void ProcessLauncher::extractSequence() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ExtractSequenceProcess);
}

void ProcessLauncher::executeExtractFrame() const
{
    auto* control = process_utils::currentActiveViewControl();
    if (!control)
    {
        if (auto* dialogService = process_utils::currentDialogService())
        {
            dialogService->notifyErrorMessage(process_utils::currentMainWindow(), QStringLiteral("Extract frame error"),
                                              QStringLiteral("No selected sequence"));
        }
        return;
    }

    NonInteractiveProcesses::extractFrame(control);
}

void ProcessLauncher::adjustBrightnessAndContrast() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(AdjustBrightnessAndContrastProcess);
}

void ProcessLauncher::gammaCorrection() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(GammaCorrectionProcess);
}

void ProcessLauncher::exposureCorrection() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ExposureCorrectionProcess);
}

void ProcessLauncher::adjustHSL() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(AdjustHSLProcess);
}

void ProcessLauncher::adjustLCh() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(AdjustLChProcess);
}

void ProcessLauncher::arcSinhStretch() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ArcSinhStretchProcess);
}

void ProcessLauncher::histogramTransformation() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(HistogramTransformationProcess);
}

void ProcessLauncher::generalizedHyperbolicTransformation() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(GHTransformationProcess);
}

void ProcessLauncher::curvesTransformation() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(CurvesTransformationProcess);
}

void ProcessLauncher::starnet() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StarnetProcess);
}

void ProcessLauncher::openIntegratedStarlessGeneration(ImageViewControl* target,
                                                       std::function<void(ImageViewControl*)> onStarless)
{
    if (target == nullptr)
    {
        return;
    }
    auto* process = new StarnetProcess(target);
    process->configureIntegratedStarlessGeneration(std::move(onStarless));
    process->show();
}

void ProcessLauncher::starmaskRecombination() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(StarmaskRecombinationProcess);
}

void ProcessLauncher::profiledStarmaskRecombination() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ProfiledStarmaskRecombinationProcess);
}

void ProcessLauncher::executeStarReduction() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StarReductionProcess);
}

void ProcessLauncher::executeStarMask() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StarMaskProcess);
}

void ProcessLauncher::executeStarnetStarMask() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StarnetStarMaskProcess);
}

void ProcessLauncher::executeSCNR() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(SCNRProcess);
}

void ProcessLauncher::executeMedianFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(MedianFilterProcess);
}

void ProcessLauncher::executeDeepSNR() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(DeepSNRProcess);
}

void ProcessLauncher::executeNLBayesFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(NLBayesProcess);
}

void ProcessLauncher::executeWaveletDenoiseFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(WaveletDenoiseProcess);
}

void ProcessLauncher::executePatchPCAFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(PatchPCADenoiseProcess);
}

void ProcessLauncher::executeStatisticalStretch() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StatisticalStretch);
}

void ProcessLauncher::executeLocalHistogramEqualizationFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(LocalHistogramEqualizationProcess);
}

void ProcessLauncher::executeLocalContrastEnhancementFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(LocalContrastEnhancementProcess);
}

void ProcessLauncher::executeDarkStructureEnhance() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(DarkStructureEnhanceProcess);
}

void ProcessLauncher::executeSharpenAlphaFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(SharpenAlphaProcess);
}

void ProcessLauncher::executeGaussianBlurFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(GaussianBlurProcess);
}

void ProcessLauncher::executeHighPassFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(HighPassFilterProcess);
}

void ProcessLauncher::executeChannelComposition() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ChannelCompositionProcess);
}

void ProcessLauncher::executeImageDebayer() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(DebayerProcess);
}

void ProcessLauncher::executeMergeCFA() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(MergeCFAProcess);
}

void ProcessLauncher::executeSplitCFA() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(SplitCFAProcess);
}

void ProcessLauncher::executeResize() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ResizeProcess);
}

void ProcessLauncher::executeIntegerResample() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(IntegerResampleProcess);
}

void ProcessLauncher::executeCFAIntegerResample() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(CFAIntegerResampleProcess);
}

void ProcessLauncher::executeCrop() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(CropProcess);
}

void ProcessLauncher::executeRotate() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(RotateProcess);
}

void ProcessLauncher::executePixelMath() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(PixelMathProcess);
}

void ProcessLauncher::executeBinarize() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(BinarizeProcess);
}

void ProcessLauncher::executeMaskDrawing() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(MaskDrawingProcess);
}

void ProcessLauncher::executeRangeSelection() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(RangeSelectionProcess);
}

void ProcessLauncher::executeCLAHE() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(CLAHEProcess);
}

void ProcessLauncher::executeColorSaturation() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ColorSaturationProcess);
}

void ProcessLauncher::executeColorCalibration() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ColorCalibrationProcess);
}

void ProcessLauncher::executePhotometricColorCalibration() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(PhotometricColorCalibrationProcess);
}

void ProcessLauncher::executeSpectrophotometricColorCalibration() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(SpectrophotometricColorCalibrationProcess);
}

void ProcessLauncher::executeBackgroundNeutralization() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(BackgroundNeutralizationProcess);
}

void ProcessLauncher::executeLinearMatch() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(LinearMatchProcess);
}

void ProcessLauncher::executeBackgroundExtraction() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(BackgroundExtractionProcess);
}

void ProcessLauncher::executeAutoGradientRemoval() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(AutoGradientRemovalProcess);
}

void ProcessLauncher::executeMultiscaleTransform() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(MultiscaleLinearProcess);
}

void ProcessLauncher::executeMorphologicalTransformation() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(MorphologicalTransformationProcess);
}

void ProcessLauncher::executeTightenEdges() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(TightenEdgesProcess);
}

void ProcessLauncher::executeGRAYCstoration() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(GRAYCstorationProcess);
}

void ProcessLauncher::executeCosmeticCorrection() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(CosmeticCorrectionProcess);
}

void ProcessLauncher::executeBandingReduction() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(BandingReductionProcess);
}

void ProcessLauncher::executeBilateralFilter() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(BilateralFilterProcess);
}

void ProcessLauncher::executeStarProfilerV1() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StarProfilerProcessV1);
}

void ProcessLauncher::executeStarProfilerV2() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(StarProfilerProcessV2);
}

void ProcessLauncher::executeUnsharpMask() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(UnsharpMaskProcess);
}

void ProcessLauncher::executeNoiseGenerator() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(NoiseGeneratorProcess);
}

void ProcessLauncher::executeTotalVariation() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(TotalVariationDenoiseProcess);
}

void ProcessLauncher::executeImageCompare() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ImageCompareProcess);
}

void ProcessLauncher::executeImageBlend() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ImageBlendProcess);
}

void ProcessLauncher::executeImageListAnalyzer() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ImageListAnalyzerProcess);
}

void ProcessLauncher::executeLRGBCombination() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(LRGBCombinationProcess);
}

void ProcessLauncher::executeNBRGBCombination() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(NBRGBCombinationProcess);
}

void ProcessLauncher::executeNBColorMapper() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(NBColorMapperProcess);
}

void ProcessLauncher::executeNarrowbandPaletteComposer() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(NarrowbandPaletteComposerProcess);
}

void ProcessLauncher::executeColorize() const
{
    LAUNCH_SINGLE_IMAGE_PROCESS(ColorizeProcess);
}

void ProcessLauncher::executeImageCalibration() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ImageCalibrationProcess);
}

void ProcessLauncher::executeImageStacking() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ImageStackingProcess);
}

void ProcessLauncher::executeImageRegistration() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ImageRegistrationProcess);
}

void ProcessLauncher::executeImageIntegration() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ImageIntegrationProcess);
}

void ProcessLauncher::executeImageIntegrationLite() const
{
    LAUNCH_TOP_LEVEL_WITH_APP(ImageIntegrationLiteProcess);
}

#undef LAUNCH_TOP_LEVEL_WITH_APP
#undef LAUNCH_SINGLE_IMAGE_PROCESS

} // namespace an::ui
