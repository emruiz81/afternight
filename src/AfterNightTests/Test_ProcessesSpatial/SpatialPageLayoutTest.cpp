// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The spatial and noise processes as pages: each in its column at 600 px of height, in both densities, with every
/// control in the column and reached by Tab and nothing clipped sideways where text is measured; and each giving
/// through its page the result its window gives for the same parameters.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;

/// A view of the colour star field the family's cases open on, its history begun as the application begins it.
[[nodiscard]] ViewHandle starsTarget(ProcessTestContext& context, const std::string& name)
{
    std::optional<an::ui::ImageDocument> document = fixtureDocument(context.engineSession(), "stars-rgb-float32");
    return document.has_value() ? makeTargetView(name, std::move(*document)) : nullptr;
}

/// The page of a process held to its column; reported skipped where text is not measured as the interface measures it.
void fitsItsColumn(const std::string& descriptorId)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = starsTarget(*context, "page target");
    ASSERT_NE(target, nullptr);
    context->addViewControl(target.get());
    context->setActiveImageViewControl(target.get());
    const bool measured = shared::expectPageFitsItsColumn(descriptorId, *target, *context);
    context->removeViewControl(target.get(), false);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// The page of a process gives the window's result, its parameters set as the family's cases set them.
void appliesAsInAWindow(std::string_view action, const std::string& descriptorId, const bool drawsAtRandom = false)
{
    const SpatialProcessSpec* spec = spatialProcess(action);
    ASSERT_NE(spec, nullptr);
    if (!runsHere(*spec))
    {
        GTEST_SKIP() << "this build carries no G'MIC";
    }
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    shared::PageApply how;
    how.configure = [spec](ImageRTPreviewControl& process) { spec->configure(process); };
    how.drawsAtRandom = drawsAtRandom;
    shared::expectPageAppliesAsTheWindow(
        descriptorId, *context, [&context]() { return starsTarget(*context, "applied target"); }, how);
}

} // namespace

#define AN_SPATIAL_PAGE_CASES(Name, Category, Action)    \
    TEST(PageLayout, Name##FitsItsColumn)                \
    {                                                    \
        fitsItsColumn(Category "." Action);              \
    }                                                    \
    TEST(PageLayout, Name##AppliesAsInAWindow)           \
    {                                                    \
        appliesAsInAWindow(Action, Category "." Action); \
    }

AN_SPATIAL_PAGE_CASES(GaussianBlur, "filters", "actionGaussian_Blur")
AN_SPATIAL_PAGE_CASES(MedianFilter, "filters", "actionMedian_Filter")
AN_SPATIAL_PAGE_CASES(BilateralFilter, "filters", "actionBilateral_Filter")
AN_SPATIAL_PAGE_CASES(BandingReduction, "filters", "actionBanding_Reduction_Filter")
AN_SPATIAL_PAGE_CASES(NonLocalBayes, "denoising", "actionNon_Local_Bayes_Filter")
AN_SPATIAL_PAGE_CASES(PatchPcaDenoise, "denoising", "actionPatch_PCA_Filter")
AN_SPATIAL_PAGE_CASES(WaveletDenoise, "denoising", "actionWavelet_Denoise_Filter")
AN_SPATIAL_PAGE_CASES(TotalVariationDenoise, "denoising", "actionTotal_Variation_Denoise_Filter")
AN_SPATIAL_PAGE_CASES(UnsharpMask, "sharpening_enhancement", "actionUnsharp_Mask")
AN_SPATIAL_PAGE_CASES(GrayCstoration, "denoising", "actionGRAYCstoration_Filter")
AN_SPATIAL_PAGE_CASES(HighPass, "sharpening_enhancement", "actionHigh_Pass")
AN_SPATIAL_PAGE_CASES(MultiscaleAlphaSharpen, "sharpening_enhancement", "actionMultiscale_Alpha_Sharpen")
AN_SPATIAL_PAGE_CASES(TightenEdges, "sharpening_enhancement", "actionTighten_Edges")
AN_SPATIAL_PAGE_CASES(LocalContrastEnhancement, "sharpening_enhancement", "actionLocal_Contrast_Enhancement")
AN_SPATIAL_PAGE_CASES(LocalHistogramEqualization, "sharpening_enhancement", "actionLocal_Histogram_Equalization")
AN_SPATIAL_PAGE_CASES(ClaHistogramEqualization, "sharpening_enhancement", "actionCLA_Histogram_Equalization")
AN_SPATIAL_PAGE_CASES(MorphologicalTransformation, "filters", "actionMorphological_Transformation")
AN_SPATIAL_PAGE_CASES(CosmeticCorrection, "filters", "actionCosmetic_Correction_Filter")

// Noise Generator draws its noise from a seed each dialog draws for itself: its page is held to applying noise as its
// window does, not to the window's very samples.
TEST(PageLayout, NoiseGeneratorFitsItsColumn)
{
    fitsItsColumn("filters.actionNoise_Generator");
}

TEST(PageLayout, NoiseGeneratorAppliesAsInAWindow)
{
    appliesAsInAWindow("actionNoise_Generator", "filters.actionNoise_Generator", true);
}
