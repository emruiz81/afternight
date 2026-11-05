// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process windows' layouts as the processes library builds them, each builder run over a window of the case's
/// own: margins and spacing are the design tokens' steps, headings and captions the tokens' faces, no widget sets a
/// font of its own size, and a window that resizes lays out everything it shows rather than placing it at a fixed
/// geometry.

#include "Test_ProcessFramework/WindowLayoutChecks.h"

#include "processes/AdjustBrightnessAndContrastProcessUiBuilder.h"
#include "processes/AdjustHSLProcessUiBuilder.h"
#include "processes/AdjustLChProcessUiBuilder.h"
#include "processes/ArcSinhStretchProcessUiBuilder.h"
#include "processes/BackgroundExtractionProcessUiBuilder.h"
#include "processes/BackgroundNeutralizationProcessUiBuilder.h"
#include "processes/BandingReductionProcessUiBuilder.h"
#include "processes/BilateralFilterProcessUiBuilder.h"
#include "processes/BinarizeProcessUiBuilder.h"
#include "processes/CFAIntegerResampleProcessUiBuilder.h"
#include "processes/CLAHEProcessUiBuilder.h"
#include "processes/ChannelCompositionProcessUiBuilder.h"
#include "processes/ColorCalibrationProcessUiBuilder.h"
#include "processes/ColorSaturationProcessUiBuilder.h"
#include "processes/ColorizeProcessUiBuilder.h"
#include "processes/CosmeticCorrectionProcessUiBuilder.h"
#include "processes/CropProcessUiBuilder.h"
#include "processes/CurvesTransformationProcessUiBuilder.h"
#include "processes/DebayerProcessUiBuilder.h"
#include "processes/ExposureCorrectionProcessUiBuilder.h"
#include "processes/ExtractSequenceProcessUiBuilder.h"
#include "processes/GHTransformationProcessUiBuilder.h"
#include "processes/GRAYCstorationProcessUiBuilder.h"
#include "processes/GammaCorrectionProcessUiBuilder.h"
#include "processes/GaussianBlurProcessUiBuilder.h"
#include "processes/HighPassFilterProcessUiBuilder.h"
#include "processes/HistogramTransformationProcessUiBuilder.h"
#include "processes/ImageBlendProcessUiBuilder.h"
#include "processes/ImageCalibrationProcessUiBuilder.h"
#include "processes/ImageCompareProcessUiBuilder.h"
#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/controls/StackingSettingsDialogUiBuilder.h"
#include "processes/ImageIntegration/tabs/CalibrationTabUiBuilder.h"
#include "processes/ImageIntegration/tabs/RegistrationTabUiBuilder.h"
#include "processes/ImageIntegration/tabs/StackingTabUiBuilder.h"
#include "processes/ImageIntegration/tabs/SummaryTabUiBuilder.h"
#include "processes/ImageListAnalyzerProcessUiBuilder.h"
#include "processes/ImageRegistrationProcessUiBuilder.h"
#include "processes/ImageStackingProcessUiBuilder.h"
#include "processes/IntegerResampleProcessUiBuilder.h"
#include "processes/LRGBCombinationProcessUiBuilder.h"
#include "processes/LinearMatchProcessUiBuilder.h"
#include "processes/LocalContrastEnhancementProcessUiBuilder.h"
#include "processes/LocalHistogramEqualizationProcessUiBuilder.h"
#include "processes/MedianFilterProcessUiBuilder.h"
#include "processes/MergeCFAProcessUiBuilder.h"
#include "processes/MorphologicalTransformationProcessUiBuilder.h"
#include "processes/MultiscaleLinearProcessUiBuilder.h"
#include "processes/NLBayesProcessUiBuilder.h"
#include "processes/NoiseGeneratorProcessUiBuilder.h"
#include "processes/PatchPCADenoiseProcessUiBuilder.h"
#include "processes/PixelMathProcessUiBuilder.h"
#include "processes/ProfiledStarmaskRecombinationProcessUiBuilder.h"
#include "processes/RangeSelectionProcessUiBuilder.h"
#include "processes/ResizeProcessUiBuilder.h"
#include "processes/RotateProcessUiBuilder.h"
#include "processes/SCNRProcessUiBuilder.h"
#include "processes/SharpenAlphaProcessUiBuilder.h"
#include "processes/SplitCFAProcessUiBuilder.h"
#include "processes/StarMaskProcessUiBuilder.h"
#include "processes/StarProfilerProcessV1UiBuilder.h"
#include "processes/StarProfilerProcessV2UiBuilder.h"
#include "processes/StarmaskRecombinationProcessUiBuilder.h"
#include "processes/StarnetProcessUiBuilder.h"
#include "processes/StarnetStarMaskProcessUiBuilder.h"
#include "processes/StatisticalStretchUiBuilder.h"
#include "processes/TightenEdgesProcessUiBuilder.h"
#include "processes/TotalVariationDenoiseProcessUiBuilder.h"
#include "processes/UnsharpMaskProcessUiBuilder.h"
#include "processes/WaveletDenoiseProcessUiBuilder.h"
#include "theme/AnDesignTokens.h"

#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QStringList>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

namespace
{

using an::tests::processes::framework::fontsOutsideTheTokens;
using an::tests::processes::framework::widgetsOutsideTheLayout;

/// A window the case owns with a builder's interface built over it, as the process window builds its own.
struct BuiltWindow
{
    const char* name;
    std::function<std::unique_ptr<QWidget>()> build;
};

/// Builds an interface over a plain window.
template <typename BuildT> [[nodiscard]] std::function<std::unique_ptr<QWidget>()> overAWindow(BuildT build)
{
    return [build]() {
        auto window = std::make_unique<QWidget>();
        (void)build(window.get());
        return window;
    };
}

/// Builds a dialog's interface over a dialog.
template <typename BuildT> [[nodiscard]] std::function<std::unique_ptr<QWidget>()> overADialog(BuildT build)
{
    return [build]() {
        auto dialog = std::make_unique<QDialog>();
        (void)build(dialog.get());
        return std::unique_ptr<QWidget>(dialog.release());
    };
}

/// Builds an interface whose root layout the process window installs itself, and installs it as the window does.
template <typename BuildT, typename RootT>
[[nodiscard]] std::function<std::unique_ptr<QWidget>()> withItsRootLayout(BuildT build, RootT root)
{
    return [build, root]() {
        auto window = std::make_unique<QWidget>();
        auto ui = build(window.get());
        if (window->layout() == nullptr)
        {
            window->setLayout(root(ui));
        }
        return window;
    };
}

namespace processes = an::ui::processes;
namespace integration = an::ui::imageintegration;

/// Every process window's interface the processes library builds with a builder of its own.
[[nodiscard]] std::vector<BuiltWindow> everyProcessWindow()
{
    return {
        {"SummaryTab", overAWindow(integration::buildSummaryTabUi)},
        {"RegistrationTab", overAWindow(integration::buildRegistrationTabUi)},
        {"StackingTab", overAWindow(integration::buildStackingTabUi)},
        {"CalibrationTab", overAWindow(integration::buildCalibrationTabUi)},
        {"RegistrationSettingsDialog", overADialog(integration::buildRegistrationSettingsDialogUi)},
        {"StackingSettingsDialog", overADialog(integration::buildStackingSettingsDialogUi)},
        {"LocalNormalizationSettingsDialog", overADialog(integration::buildLocalNormalizationSettingsDialogUi)},
        {"ColorCalibration", overAWindow(processes::buildColorCalibrationProcessUi)},
        {"BackgroundExtraction", overAWindow(processes::buildBackgroundExtractionProcessUi)},
        {"MultiscaleLinear", overAWindow(processes::buildMultiscaleLinearProcessUi)},
        {"ImageStacking", overAWindow(processes::buildImageStackingProcessUi)},
        {"GHTransformation", overAWindow(processes::buildGHTransformationProcessUi)},
        {"PixelMath", overAWindow(processes::buildPixelMathProcessUi)},
        {"ImageRegistration", overAWindow(processes::buildImageRegistrationProcessUi)},
        {"ImageBlend", overAWindow(processes::buildImageBlendProcessUi)},
        {"ImageCalibration", overAWindow(processes::buildImageCalibrationProcessUi)},
        {"BackgroundNeutralization", overAWindow(processes::buildBackgroundNeutralizationProcessUi)},
        {"LinearMatch", overAWindow(processes::buildLinearMatchProcessUi)},
        {"ChannelComposition", overAWindow(processes::buildChannelCompositionProcessUi)},
        {"SCNR", overAWindow(processes::buildSCNRProcessUi)},
        {"ColorSaturation", overAWindow(processes::buildColorSaturationProcessUi)},
        {"LRGBCombination", overAWindow(processes::buildLRGBCombinationProcessUi)},
        {"TotalVariationDenoise", overAWindow(processes::buildTotalVariationDenoiseProcessUi)},
        {"WaveletDenoise", overAWindow(processes::buildWaveletDenoiseProcessUi)},
        {"PatchPCADenoise", overAWindow(processes::buildPatchPCADenoiseProcessUi)},
        {"NLBayes", overAWindow(processes::buildNLBayesProcessUi)},
        {"MedianFilter", overAWindow(processes::buildMedianFilterProcessUi)},
        {"BilateralFilter", overAWindow(processes::buildBilateralFilterProcessUi)},
        {"GaussianBlur", overAWindow(processes::buildGaussianBlurProcessUi)},
        {"UnsharpMask", overAWindow(processes::buildUnsharpMaskProcessUi)},
        {"LocalHistogramEqualization", overAWindow(processes::buildLocalHistogramEqualizationProcessUi)},
        {"LocalContrastEnhancement", overAWindow(processes::buildLocalContrastEnhancementProcessUi)},
        {"HighPassFilter", overAWindow(processes::buildHighPassFilterProcessUi)},
        {"Starnet", overAWindow(processes::buildStarnetProcessUi)},
        {"StarnetStarMask", overAWindow(processes::buildStarnetStarMaskProcessUi)},
        {"StarProfilerV1",
         withItsRootLayout(processes::buildStarProfilerProcessV1Ui, [](auto& ui) { return ui.verticalLayout; })},
        {"StarProfilerV2",
         withItsRootLayout(processes::buildStarProfilerProcessV2Ui, [](auto& ui) { return ui.verticalLayout; })},
        {"ProfiledStarmaskRecombination", withItsRootLayout(processes::buildProfiledStarmaskRecombinationProcessUi,
                                                            [](auto& ui) { return ui.horizontalLayoutMain; })},
        {"StarmaskRecombination", withItsRootLayout(processes::buildStarmaskRecombinationProcessUi,
                                                    [](auto& ui) { return ui.horizontalLayoutMain; })},
        {"StarMask",
         withItsRootLayout(processes::buildStarMaskProcessUi, [](auto& ui) { return ui.horizontalLayoutMain; })},
        {"Crop", overAWindow(processes::buildCropProcessUi)},
        {"Rotate", overAWindow(processes::buildRotateProcessUi)},
        {"Resize", overAWindow(processes::buildResizeProcessUi)},
        {"IntegerResample", overAWindow(processes::buildIntegerResampleProcessUi)},
        {"CFAIntegerResample", overAWindow(processes::buildCFAIntegerResampleProcessUi)},
        {"Debayer", overAWindow(processes::buildDebayerProcessUi)},
        {"MergeCFA", overAWindow(processes::buildMergeCFAProcessUi)},
        {"SplitCFA", overAWindow(processes::buildSplitCFAProcessUi)},
        {"MorphologicalTransformation", overAWindow(processes::buildMorphologicalTransformationProcessUi)},
        {"CosmeticCorrection", overAWindow(processes::buildCosmeticCorrectionProcessUi)},
        {"HistogramTransformation", overAWindow(processes::buildHistogramTransformationProcessUi)},
        {"Binarize", overAWindow(processes::buildBinarizeProcessUi)},
        {"BandingReduction", overAWindow(processes::buildBandingReductionProcessUi)},
        {"RangeSelection", overAWindow(processes::buildRangeSelectionProcessUi)},
        {"CurvesTransformation", overAWindow(processes::buildCurvesTransformationProcessUi)},
        {"TightenEdges", overAWindow(processes::buildTightenEdgesProcessUi)},
        {"StatisticalStretch", overAWindow(processes::buildStatisticalStretchUi)},
        {"NoiseGenerator", overAWindow(processes::buildNoiseGeneratorProcessUi)},
        {"CLAHE", overAWindow(processes::buildCLAHEProcessUi)},
        {"ArcSinhStretch", overAWindow(processes::buildArcSinhStretchProcessUi)},
        {"AdjustLCh", overAWindow(processes::buildAdjustLChProcessUi)},
        {"SharpenAlpha", overAWindow(processes::buildSharpenAlphaProcessUi)},
        {"ImageListAnalyzer", overAWindow(processes::buildImageListAnalyzerProcessUi)},
        {"GRAYCstoration", overAWindow(processes::buildGRAYCstorationProcessUi)},
        {"ImageCompare", overAWindow(processes::buildImageCompareProcessUi)},
        {"GammaCorrection", overAWindow(processes::buildGammaCorrectionProcessUi)},
        {"ExtractSequence", overAWindow(processes::buildExtractSequenceProcessUi)},
        {"ExposureCorrection", overAWindow(processes::buildExposureCorrectionProcessUi)},
        {"Colorize", overAWindow(processes::buildColorizeProcessUi)},
        {"AdjustHSL", overAWindow(processes::buildAdjustHSLProcessUi)},
        {"AdjustBrightnessAndContrast", overAWindow(processes::buildAdjustBrightnessAndContrastProcessUi)},
    };
}

/// The process windows that resize with the person: each lays out everything it shows.
[[nodiscard]] bool resizesWithThePerson(const char* name)
{
    static const QStringList resizable{QStringLiteral("MultiscaleLinear"),
                                       QStringLiteral("GHTransformation"),
                                       QStringLiteral("PixelMath"),
                                       QStringLiteral("StarProfilerV1"),
                                       QStringLiteral("StarProfilerV2"),
                                       QStringLiteral("ProfiledStarmaskRecombination"),
                                       QStringLiteral("LRGBCombination"),
                                       QStringLiteral("TotalVariationDenoise"),
                                       QStringLiteral("WaveletDenoise"),
                                       QStringLiteral("PatchPCADenoise"),
                                       QStringLiteral("NLBayes"),
                                       QStringLiteral("UnsharpMask"),
                                       QStringLiteral("LocalHistogramEqualization"),
                                       QStringLiteral("LocalContrastEnhancement"),
                                       QStringLiteral("HighPassFilter"),
                                       QStringLiteral("StarnetStarMask"),
                                       QStringLiteral("StarmaskRecombination"),
                                       QStringLiteral("StarMask"),
                                       QStringLiteral("Binarize"),
                                       QStringLiteral("RangeSelection"),
                                       QStringLiteral("CurvesTransformation"),
                                       QStringLiteral("TightenEdges"),
                                       QStringLiteral("CLAHE"),
                                       QStringLiteral("SharpenAlpha"),
                                       QStringLiteral("GRAYCstoration"),
                                       QStringLiteral("Colorize"),
                                       QStringLiteral("AdjustBrightnessAndContrast")};
    return resizable.contains(QString::fromLatin1(name));
}

/// The resizable process windows that also leave their size policy free, so a parent layout or the person can grow
/// them in both directions.
[[nodiscard]] bool growsInBothDirections(const char* name)
{
    static const QStringList growable{QStringLiteral("GHTransformation"),
                                      QStringLiteral("PixelMath"),
                                      QStringLiteral("TotalVariationDenoise"),
                                      QStringLiteral("WaveletDenoise"),
                                      QStringLiteral("PatchPCADenoise"),
                                      QStringLiteral("UnsharpMask"),
                                      QStringLiteral("HighPassFilter"),
                                      QStringLiteral("LocalHistogramEqualization"),
                                      QStringLiteral("LocalContrastEnhancement"),
                                      QStringLiteral("Binarize"),
                                      QStringLiteral("RangeSelection"),
                                      QStringLiteral("TightenEdges"),
                                      QStringLiteral("CLAHE"),
                                      QStringLiteral("SharpenAlpha"),
                                      QStringLiteral("GRAYCstoration"),
                                      QStringLiteral("Colorize"),
                                      QStringLiteral("AdjustBrightnessAndContrast")};
    return growable.contains(QString::fromLatin1(name));
}

} // namespace

// No process window sets a font of its own size: every widget that chooses its font takes one of the design tokens'
// faces, in points, so the windows follow the application's typography.
TEST(ProcessWindowLayoutTest, EveryProcessWindowTakesItsFontsFromTheTokens)
{
    for (const BuiltWindow& built : everyProcessWindow())
    {
        const std::unique_ptr<QWidget> window = built.build();
        ASSERT_NE(window, nullptr) << built.name;
        const QStringList outside = fontsOutsideTheTokens(*window);
        EXPECT_TRUE(outside.isEmpty()) << built.name << ": " << outside.join(QStringLiteral(", ")).toStdString();
    }
}

// A process window that resizes lays out everything it shows: no widget sits at a fixed geometry outside the window's
// layout, so the content grows and shrinks with the window.
TEST(ProcessWindowLayoutTest, EveryResizableProcessWindowLaysOutWhatItShows)
{
    int resizable = 0;
    for (const BuiltWindow& built : everyProcessWindow())
    {
        if (!resizesWithThePerson(built.name))
        {
            continue;
        }
        ++resizable;
        const std::unique_ptr<QWidget> window = built.build();
        ASSERT_NE(window, nullptr) << built.name;
        ASSERT_NE(window->layout(), nullptr) << built.name;
        const QStringList outside = widgetsOutsideTheLayout(*window);
        EXPECT_TRUE(outside.isEmpty()) << built.name << ": " << outside.join(QStringLiteral(", ")).toStdString();
        if (growsInBothDirections(built.name))
        {
            EXPECT_NE(window->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed) << built.name;
            EXPECT_NE(window->sizePolicy().verticalPolicy(), QSizePolicy::Fixed) << built.name;
        }
    }
    EXPECT_EQ(resizable, 27);
}

// The integration tabs set their margins and spacing in the tokens' steps.
TEST(ProcessWindowLayoutTest, IntegrationTabsTakeTheirMarginsFromTheTokens)
{
    const auto& t = an::ui::tokens();

    QWidget summaryHost;
    const auto summaryUi = an::ui::imageintegration::buildSummaryTabUi(&summaryHost);
    ASSERT_NE(summaryUi.mainLayout, nullptr);
    EXPECT_EQ(summaryUi.mainLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(summaryUi.mainLayout->spacing(), t.spacing.sm);

    QWidget registrationHost;
    const auto registrationUi = an::ui::imageintegration::buildRegistrationTabUi(&registrationHost);
    ASSERT_NE(registrationUi.verticalLayout, nullptr);
    ASSERT_NE(registrationUi.thresholdLayout, nullptr);
    EXPECT_EQ(registrationUi.verticalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(registrationUi.thresholdLayout->contentsMargins().top(), t.spacing.md);

    QWidget stackingHost;
    const auto stackingUi = an::ui::imageintegration::buildStackingTabUi(&stackingHost);
    ASSERT_NE(stackingUi.verticalLayout, nullptr);
    ASSERT_NE(stackingUi.verticalLayout_2, nullptr);
    ASSERT_NE(stackingUi.verticalLayout_3, nullptr);
    EXPECT_EQ(stackingUi.verticalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(stackingUi.verticalLayout_2->contentsMargins().left(), t.spacing.sm);
    EXPECT_EQ(stackingUi.verticalLayout_2->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(stackingUi.verticalLayout_3->contentsMargins().left(), t.spacing.sm);
    EXPECT_EQ(stackingUi.verticalLayout_3->contentsMargins().top(), t.spacing.md);

    QWidget calibrationHost;
    const auto calibrationUi = an::ui::imageintegration::buildCalibrationTabUi(&calibrationHost);
    ASSERT_NE(calibrationUi.horizontalLayout, nullptr);
    ASSERT_NE(calibrationUi.rightPanelLayout, nullptr);
    EXPECT_EQ(calibrationUi.horizontalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(calibrationUi.rightPanelLayout->contentsMargins().left(), t.spacing.sm);
    EXPECT_EQ(calibrationUi.rightPanelLayout->contentsMargins().top(), t.spacing.sm);
}

// The integration's registration, stacking and local normalization dialogs set their titles in the third heading
// face, their notes in the caption face, and their rows in the tokens' steps.
TEST(ProcessWindowLayoutTest, IntegrationSettingsDialogsTakeTheirFacesAndSpacingFromTheTokens)
{
    const auto& t = an::ui::tokens();

    QDialog registrationHost;
    const auto registrationUi = an::ui::imageintegration::buildRegistrationSettingsDialogUi(&registrationHost);
    ASSERT_NE(registrationUi.titleLabel, nullptr);
    ASSERT_NE(registrationUi.verticalLayout_advancedPanel, nullptr);
    ASSERT_NE(registrationUi.horizontalLayout_buttons, nullptr);
    EXPECT_EQ(registrationUi.titleLabel->font(), t.typography.heading3);
    EXPECT_EQ(registrationUi.verticalLayout_advancedPanel->spacing(), t.spacing.md);
    EXPECT_EQ(registrationUi.horizontalLayout_buttons->spacing(), t.spacing.sm);

    QDialog stackingHost;
    const auto stackingUi = an::ui::imageintegration::buildStackingSettingsDialogUi(&stackingHost);
    ASSERT_NE(stackingUi.titleLabel, nullptr);
    ASSERT_NE(stackingUi.localNormalizationPresetLabel, nullptr);
    ASSERT_NE(stackingUi.drizzleInfoLabel, nullptr);
    ASSERT_NE(stackingUi.horizontalLayout_stackMethod, nullptr);
    EXPECT_EQ(stackingUi.titleLabel->font(), t.typography.heading3);
    EXPECT_EQ(stackingUi.localNormalizationPresetLabel->font(), t.typography.caption);
    EXPECT_EQ(stackingUi.drizzleInfoLabel->font(), t.typography.caption);
    EXPECT_EQ(stackingUi.horizontalLayout_stackMethod->spacing(), t.spacing.sm);

    QDialog normalizationHost;
    const auto normalizationUi = an::ui::imageintegration::buildLocalNormalizationSettingsDialogUi(&normalizationHost);
    ASSERT_NE(normalizationUi.infoLabel, nullptr);
    ASSERT_NE(normalizationUi.referenceSettingsLabel, nullptr);
    ASSERT_NE(normalizationUi.horizontalLayout_buttons, nullptr);
    EXPECT_EQ(normalizationUi.infoLabel->font(), t.typography.caption);
    EXPECT_EQ(normalizationUi.referenceSettingsLabel->font(), t.typography.heading3);
    EXPECT_EQ(normalizationUi.horizontalLayout_buttons->spacing(), t.spacing.sm);
}

// The colour calibration and background extraction windows take their margins, spacing and heading from the tokens.
TEST(ProcessWindowLayoutTest, ColorCalibrationAndBackgroundExtractionUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget colorHost;
    const auto colorUi = an::ui::processes::buildColorCalibrationProcessUi(&colorHost);
    ASSERT_NE(colorUi.label, nullptr);
    ASSERT_NE(colorHost.layout(), nullptr);
    EXPECT_EQ(colorHost.layout()->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(colorHost.layout()->spacing(), t.spacing.sm);
    EXPECT_EQ(colorUi.label->font(), t.typography.heading3);

    QWidget backgroundHost;
    (void)an::ui::processes::buildBackgroundExtractionProcessUi(&backgroundHost);
    ASSERT_NE(backgroundHost.layout(), nullptr);
    EXPECT_EQ(backgroundHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(backgroundHost.layout()->spacing(), t.spacing.md);
}

// The multiscale and generalized hyperbolic windows space their panels in the tokens' steps, keep their scrolled
// settings at the size they were designed for, and grow with the window.
TEST(ProcessWindowLayoutTest, MultiscaleAndGeneralizedHyperbolicUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget multiscaleHost;
    const auto multiscaleUi = an::ui::processes::buildMultiscaleLinearProcessUi(&multiscaleHost);
    ASSERT_NE(multiscaleUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(multiscaleUi.mainSplitter, nullptr);
    ASSERT_NE(multiscaleUi.leftPanelLayout, nullptr);
    ASSERT_NE(multiscaleUi.globalSettingsScrollContent, nullptr);
    EXPECT_EQ(multiscaleUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(multiscaleUi.leftPanelLayout->spacing(), t.spacing.sm);
    EXPECT_EQ(multiscaleUi.globalSettingsScrollContent->minimumSize(), QSize(480, 623));
    EXPECT_EQ(multiscaleUi.mainSplitter->objectName(), QStringLiteral("mainSplitter"));

    QWidget ghHost;
    const auto ghUi = an::ui::processes::buildGHTransformationProcessUi(&ghHost);
    ASSERT_NE(ghHost.layout(), nullptr);
    ASSERT_NE(ghUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(ghUi.scrollAreaWidgetContents, nullptr);
    ASSERT_NE(ghUi.scrollAreaWidgetContents_4, nullptr);
    EXPECT_EQ(ghHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ghHost.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(ghUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(ghUi.scrollAreaWidgetContents->minimumSize(), QSize(508, 248));
    EXPECT_EQ(ghUi.scrollAreaWidgetContents_4->minimumSize(), QSize(508, 248));
    ghHost.resize(1180, 860);
    EXPECT_GE(ghHost.width(), 1180);
    EXPECT_GE(ghHost.height(), 860);
}

// The image registration and stacking windows set their margins and spacing in the tokens' steps.
TEST(ProcessWindowLayoutTest, ImageRegistrationAndStackingUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget registrationHost;
    const auto registrationUi = an::ui::processes::buildImageRegistrationProcessUi(&registrationHost);
    ASSERT_NE(registrationUi.verticalLayout_Main, nullptr);
    ASSERT_NE(registrationUi.verticalLayoutTab2_Main, nullptr);
    EXPECT_EQ(registrationUi.verticalLayout_Main->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(registrationUi.verticalLayout_Main->contentsMargins().top(), t.spacing.md);
    // The advanced card lays its body out at the card's own step, whatever the builder set before handing it over.
    EXPECT_EQ(registrationUi.verticalLayoutTab2_Main->spacing(), t.spacing.sm);

    QWidget stackingHost;
    const auto stackingUi = an::ui::processes::buildImageStackingProcessUi(&stackingHost);
    ASSERT_NE(stackingUi.verticalLayout_Main, nullptr);
    ASSERT_NE(stackingUi.frameGroupsCard, nullptr);
    EXPECT_EQ(stackingUi.verticalLayout_Main->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(stackingUi.verticalLayout_Main->spacing(), t.spacing.md);
    // The rejection settings fold away in a section of their own, and the window shows no selected-frame panel.
    EXPECT_NE(stackingUi.rejectionSection, nullptr);
    for (const QLabel* label : stackingHost.findChildren<QLabel*>())
    {
        EXPECT_NE(label->text(), QStringLiteral("Selected Frame"));
    }
    for (const QGroupBox* group : stackingHost.findChildren<QGroupBox*>())
    {
        EXPECT_NE(group->title(), QStringLiteral("Selected Frame"));
    }
}

// The PixelMath window lays its content in its root layout, both in the tokens' margins, and grows with the window.
TEST(ProcessWindowLayoutTest, PixelMathLaysItsContentInItsRootLayout)
{
    const auto& t = an::ui::tokens();

    QWidget pixelMathHost;
    const auto pixelMathUi = an::ui::processes::buildPixelMathProcessUi(&pixelMathHost);
    ASSERT_NE(pixelMathHost.layout(), nullptr);
    ASSERT_NE(pixelMathUi.verticalLayoutWidget, nullptr);
    ASSERT_NE(pixelMathUi.verticalLayout, nullptr);
    EXPECT_EQ(pixelMathHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(pixelMathHost.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(pixelMathHost.layout()->indexOf(pixelMathUi.verticalLayoutWidget), 0);
    EXPECT_EQ(pixelMathUi.verticalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(pixelMathUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    pixelMathHost.resize(1200, 900);
    EXPECT_GE(pixelMathHost.width(), 1200);
    EXPECT_GE(pixelMathHost.height(), 900);
}

// The star profilers and the profiled star mask recombination space their sections in the tokens' steps, and each
// profiler's Detect button is the window's primary action.
TEST(ProcessWindowLayoutTest, StarProfilersAndProfiledRecombinationUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget profilerHost;
    auto profilerUi = an::ui::processes::buildStarProfilerProcessV2Ui(&profilerHost);
    ASSERT_NE(profilerUi.verticalLayout, nullptr);
    ASSERT_NE(profilerUi.detectedStarsVerticalLayout, nullptr);
    EXPECT_EQ(profilerUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(profilerUi.detectedStarsVerticalLayout->contentsMargins().left(), t.spacing.md);
    ASSERT_NE(profilerUi.detectStarsButton, nullptr);
    EXPECT_EQ(profilerUi.detectStarsButton->property("anButtonVariant").toString(), QStringLiteral("primary"));

    QWidget firstProfilerHost;
    auto firstProfilerUi = an::ui::processes::buildStarProfilerProcessV1Ui(&firstProfilerHost);
    ASSERT_NE(firstProfilerUi.verticalLayout, nullptr);
    ASSERT_NE(firstProfilerUi.starDetectionVerticalLayout, nullptr);
    EXPECT_EQ(firstProfilerUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(firstProfilerUi.verticalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(firstProfilerUi.starDetectionVerticalLayout->spacing(), t.spacing.md);
    ASSERT_NE(firstProfilerUi.detectStarsButton, nullptr);
    EXPECT_EQ(firstProfilerUi.detectStarsButton->property("anButtonVariant").toString(), QStringLiteral("primary"));

    QWidget profiledHost;
    auto profiledUi = an::ui::processes::buildProfiledStarmaskRecombinationProcessUi(&profiledHost);
    ASSERT_NE(profiledUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(profiledUi.horizontalLayoutMain->spacing(), t.spacing.md);
    if (profiledHost.layout() == nullptr)
    {
        profiledHost.setLayout(profiledUi.horizontalLayoutMain);
    }
    EXPECT_NE(profiledHost.findChild<QSplitter*>(QStringLiteral("mainSplitter")), nullptr);
}

// The image blend and image calibration windows space their content in the tokens' steps.
TEST(ProcessWindowLayoutTest, ImageBlendAndCalibrationUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget blendHost;
    const auto blendUi = an::ui::processes::buildImageBlendProcessUi(&blendHost);
    ASSERT_NE(blendUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(blendUi.horizontalLayoutMain->spacing(), t.spacing.sm);
    EXPECT_EQ(blendUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.sm);

    QWidget calibrationHost;
    const auto calibrationUi = an::ui::processes::buildImageCalibrationProcessUi(&calibrationHost);
    ASSERT_NE(calibrationUi.verticalLayout_Main, nullptr);
    EXPECT_EQ(calibrationUi.verticalLayout_Main->spacing(), t.spacing.md);
    EXPECT_EQ(calibrationUi.verticalLayout_Main->contentsMargins().left(), t.spacing.md);
}

// The background neutralization, linear match and SCNR dialogs space their rows in the tokens' steps.
TEST(ProcessWindowLayoutTest, NeutralizationLinearMatchAndScnrUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget backgroundHost;
    const auto backgroundUi = an::ui::processes::buildBackgroundNeutralizationProcessUi(&backgroundHost);
    ASSERT_NE(backgroundHost.layout(), nullptr);
    ASSERT_NE(backgroundUi.horizontalLayout_4, nullptr);
    EXPECT_EQ(backgroundHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(backgroundHost.layout()->contentsMargins().left(), t.spacing.lg);
    EXPECT_EQ(backgroundUi.horizontalLayout_4->spacing(), t.spacing.md);

    QWidget linearHost;
    const auto linearUi = an::ui::processes::buildLinearMatchProcessUi(&linearHost);
    ASSERT_NE(linearUi.verticalLayout, nullptr);
    ASSERT_NE(linearUi.horizontalLayout, nullptr);
    ASSERT_NE(linearUi.horizontalLayout_2, nullptr);
    EXPECT_EQ(linearUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(linearUi.horizontalLayout->spacing(), t.spacing.sm);
    EXPECT_EQ(linearUi.horizontalLayout_2->contentsMargins().top(), t.spacing.lg);

    QWidget scnrHost;
    const auto scnrUi = an::ui::processes::buildSCNRProcessUi(&scnrHost);
    ASSERT_NE(scnrHost.layout(), nullptr);
    ASSERT_NE(scnrUi.amountValue, nullptr);
    EXPECT_EQ(scnrHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(scnrHost.layout()->contentsMargins().left(), t.spacing.md);
}

// The channel composition, colour saturation and LRGB windows space their content in the tokens' steps, and the LRGB
// chart keeps the size it was designed for while the window grows.
TEST(ProcessWindowLayoutTest, ChannelCompositionColorSaturationAndLrgbUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget channelHost;
    const auto channelUi = an::ui::processes::buildChannelCompositionProcessUi(&channelHost);
    ASSERT_NE(channelUi.verticalLayout, nullptr);
    EXPECT_EQ(channelUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(channelUi.verticalLayout->contentsMargins().left(), t.spacing.md);

    QWidget saturationHost;
    const auto saturationUi = an::ui::processes::buildColorSaturationProcessUi(&saturationHost);
    ASSERT_NE(saturationUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(saturationUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(saturationUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget lrgbHost;
    const auto lrgbUi = an::ui::processes::buildLRGBCombinationProcessUi(&lrgbHost);
    ASSERT_NE(lrgbHost.layout(), nullptr);
    ASSERT_NE(lrgbUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(lrgbUi.chartScrollArea, nullptr);
    EXPECT_EQ(lrgbUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(lrgbUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(lrgbHost.layout(), lrgbUi.horizontalLayoutMain);
    ASSERT_NE(lrgbUi.chartScrollArea->widget(), nullptr);
    EXPECT_EQ(lrgbUi.chartScrollArea->widget()->minimumSize(), QSize(378, 218));
    lrgbHost.resize(1120, 820);
    EXPECT_GE(lrgbHost.width(), 1120);
    EXPECT_GE(lrgbHost.height(), 820);
}

// The noise reduction windows lay their content in their root layout in the tokens' margins and space their settings
// in the tokens' steps.
TEST(ProcessWindowLayoutTest, NoiseReductionWindowsUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget totalHost;
    const auto totalUi = an::ui::processes::buildTotalVariationDenoiseProcessUi(&totalHost);
    ASSERT_NE(totalHost.layout(), nullptr);
    ASSERT_NE(totalUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(totalUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(totalUi.verticalLayout, nullptr);
    EXPECT_EQ(totalHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(totalHost.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(totalUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(totalUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(totalUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(totalHost.layout()->indexOf(totalUi.horizontalLayoutWidget_4), 0);

    QWidget waveletHost;
    const auto waveletUi = an::ui::processes::buildWaveletDenoiseProcessUi(&waveletHost);
    ASSERT_NE(waveletHost.layout(), nullptr);
    ASSERT_NE(waveletUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(waveletUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(waveletUi.gridLayout, nullptr);
    EXPECT_EQ(waveletHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(waveletUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(waveletUi.gridLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(waveletUi.gridLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(waveletHost.layout()->indexOf(waveletUi.horizontalLayoutWidget_4), 0);

    QWidget patchPcaHost;
    const auto patchPcaUi = an::ui::processes::buildPatchPCADenoiseProcessUi(&patchPcaHost);
    ASSERT_NE(patchPcaHost.layout(), nullptr);
    ASSERT_NE(patchPcaUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(patchPcaUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(patchPcaUi.gridLayout, nullptr);
    EXPECT_EQ(patchPcaHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(patchPcaUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(patchPcaUi.gridLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(patchPcaUi.gridLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(patchPcaHost.layout()->indexOf(patchPcaUi.horizontalLayoutWidget_4), 0);

    QWidget nlBayesHost;
    const auto nlBayesUi = an::ui::processes::buildNLBayesProcessUi(&nlBayesHost);
    ASSERT_NE(nlBayesHost.layout(), nullptr);
    ASSERT_NE(nlBayesUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(nlBayesUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(nlBayesUi.formLayout, nullptr);
    EXPECT_EQ(nlBayesHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(nlBayesUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(nlBayesUi.formLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(nlBayesUi.formLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(nlBayesHost.layout()->indexOf(nlBayesUi.horizontalLayoutWidget_4), 0);
}

// The median, bilateral, Gaussian, unsharp mask and high pass windows space their content in the tokens' steps, and
// the unsharp mask and high pass windows lay theirs in their root layout and are not capped in size.
TEST(ProcessWindowLayoutTest, FilterWindowsUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget medianHost;
    const auto medianUi = an::ui::processes::buildMedianFilterProcessUi(&medianHost);
    ASSERT_NE(medianUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(medianUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(medianUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget bilateralHost;
    const auto bilateralUi = an::ui::processes::buildBilateralFilterProcessUi(&bilateralHost);
    ASSERT_NE(bilateralUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(bilateralUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(bilateralUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget gaussianHost;
    const auto gaussianUi = an::ui::processes::buildGaussianBlurProcessUi(&gaussianHost);
    ASSERT_NE(gaussianUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(gaussianUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(gaussianUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget unsharpHost;
    const auto unsharpUi = an::ui::processes::buildUnsharpMaskProcessUi(&unsharpHost);
    ASSERT_NE(unsharpHost.layout(), nullptr);
    ASSERT_NE(unsharpUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(unsharpUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(unsharpUi.verticalLayout, nullptr);
    EXPECT_EQ(unsharpHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(unsharpHost.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(unsharpUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(unsharpUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(unsharpHost.layout()->indexOf(unsharpUi.horizontalLayoutWidget_4), 0);

    QWidget highPassHost;
    const auto highPassUi = an::ui::processes::buildHighPassFilterProcessUi(&highPassHost);
    ASSERT_NE(highPassHost.layout(), nullptr);
    ASSERT_NE(highPassUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(highPassUi.verticalLayout, nullptr);
    EXPECT_EQ(highPassHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(highPassHost.layout()->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(highPassUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(highPassUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_GT(highPassHost.maximumSize().width(), 10000);
    EXPECT_GT(highPassHost.maximumSize().height(), 10000);
}

// The local histogram equalization and local contrast enhancement windows space their content in the tokens' steps,
// keep their scrolled settings at their designed size and are not capped in size.
TEST(ProcessWindowLayoutTest, LocalContrastAndHistogramUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget histogramHost;
    const auto histogramUi = an::ui::processes::buildLocalHistogramEqualizationProcessUi(&histogramHost);
    ASSERT_NE(histogramHost.layout(), nullptr);
    ASSERT_NE(histogramUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(histogramUi.verticalLayout, nullptr);
    ASSERT_NE(histogramUi.scrollAreaWidgetContents, nullptr);
    EXPECT_EQ(histogramHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(histogramUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(histogramUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(histogramUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(histogramUi.scrollAreaWidgetContents->minimumSize(), QSize(417, 218));

    QWidget contrastHost;
    const auto contrastUi = an::ui::processes::buildLocalContrastEnhancementProcessUi(&contrastHost);
    ASSERT_NE(contrastHost.layout(), nullptr);
    ASSERT_NE(contrastUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(contrastUi.verticalLayout, nullptr);
    EXPECT_EQ(contrastHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(contrastUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(contrastUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(contrastUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_GT(contrastHost.maximumSize().width(), 10000);
    EXPECT_GT(contrastHost.maximumSize().height(), 10000);
}

// The star mask and the star mask recombination space their content in the tokens' steps and split controls from
// preview.
TEST(ProcessWindowLayoutTest, StarMaskAndRecombinationUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget recombinationHost;
    auto recombinationUi = an::ui::processes::buildStarmaskRecombinationProcessUi(&recombinationHost);
    ASSERT_NE(recombinationUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(recombinationUi.verticalLayout, nullptr);
    EXPECT_EQ(recombinationUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(recombinationUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(recombinationUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    if (recombinationHost.layout() == nullptr)
    {
        recombinationHost.setLayout(recombinationUi.horizontalLayoutMain);
    }
    EXPECT_NE(recombinationHost.findChild<QSplitter*>(QStringLiteral("mainSplitter")), nullptr);

    QWidget starMaskHost;
    auto starMaskUi = an::ui::processes::buildStarMaskProcessUi(&starMaskHost);
    ASSERT_NE(starMaskUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(starMaskUi.verticalLayout, nullptr);
    EXPECT_EQ(starMaskUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(starMaskUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(starMaskUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    if (starMaskHost.layout() == nullptr)
    {
        starMaskHost.setLayout(starMaskUi.horizontalLayoutMain);
    }
    EXPECT_NE(starMaskHost.findChild<QSplitter*>(QStringLiteral("mainSplitter")), nullptr);
}

// The StarNet and StarNet star mask windows space their content in the tokens' steps and head it in the third heading
// face.
TEST(ProcessWindowLayoutTest, StarnetWindowsUseTokenSpacingAndHeadings)
{
    const auto& t = an::ui::tokens();

    QWidget starnetHost;
    const auto starnetUi = an::ui::processes::buildStarnetProcessUi(&starnetHost);
    ASSERT_NE(starnetHost.layout(), nullptr);
    ASSERT_NE(starnetUi.label, nullptr);
    EXPECT_EQ(starnetHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(starnetHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(starnetUi.label->font(), t.typography.heading3);

    QWidget starnetMaskHost;
    const auto starnetMaskUi = an::ui::processes::buildStarnetStarMaskProcessUi(&starnetMaskHost);
    ASSERT_NE(starnetMaskHost.layout(), nullptr);
    ASSERT_NE(starnetMaskUi.label, nullptr);
    ASSERT_NE(starnetMaskUi.groupBox, nullptr);
    ASSERT_NE(starnetMaskUi.groupBox_2, nullptr);
    EXPECT_EQ(starnetMaskHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(starnetMaskHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(starnetMaskUi.label->font(), t.typography.heading3);
}

// The crop, rotate and resize windows space their forms in the tokens' steps.
TEST(ProcessWindowLayoutTest, CropRotateAndResizeUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget cropHost;
    const auto cropUi = an::ui::processes::buildCropProcessUi(&cropHost);
    ASSERT_NE(cropHost.layout(), nullptr);
    ASSERT_NE(cropUi.formLayout, nullptr);
    ASSERT_NE(cropUi.formLayout_2, nullptr);
    EXPECT_EQ(cropHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(cropHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(cropUi.formLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(cropUi.formLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(cropUi.formLayout_2->horizontalSpacing(), t.spacing.sm);

    QWidget rotateHost;
    const auto rotateUi = an::ui::processes::buildRotateProcessUi(&rotateHost);
    ASSERT_NE(rotateHost.layout(), nullptr);
    ASSERT_NE(rotateUi.groupBoxRot, nullptr);
    ASSERT_NE(rotateUi.groupBox, nullptr);
    EXPECT_EQ(rotateHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(rotateHost.layout()->contentsMargins().left(), t.spacing.md);
    const auto* rotationLayout = qobject_cast<QVBoxLayout*>(rotateUi.groupBoxRot->layout());
    ASSERT_NE(rotationLayout, nullptr);
    EXPECT_EQ(rotationLayout->spacing(), t.spacing.md);

    QWidget resizeHost;
    const auto resizeUi = an::ui::processes::buildResizeProcessUi(&resizeHost);
    ASSERT_NE(resizeHost.layout(), nullptr);
    ASSERT_NE(resizeUi.label_6, nullptr);
    EXPECT_EQ(resizeHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(resizeHost.layout()->contentsMargins().left(), t.spacing.md);
    const auto* sizeGrid = resizeHost.findChild<QGridLayout*>(QStringLiteral("gridLayout"));
    ASSERT_NE(sizeGrid, nullptr);
    EXPECT_EQ(sizeGrid->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(sizeGrid->verticalSpacing(), t.spacing.sm);
}

// The integer and filter-array integer resample windows space their groups in the tokens' steps.
TEST(ProcessWindowLayoutTest, IntegerResampleWindowsUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget integerHost;
    const auto integerUi = an::ui::processes::buildIntegerResampleProcessUi(&integerHost);
    ASSERT_NE(integerHost.layout(), nullptr);
    ASSERT_NE(integerUi.groupBox_2, nullptr);
    ASSERT_NE(integerUi.groupBox, nullptr);
    EXPECT_EQ(integerHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(integerHost.layout()->contentsMargins().left(), t.spacing.md);
    const auto* integerResampleLayout = qobject_cast<QVBoxLayout*>(integerUi.groupBox_2->layout());
    ASSERT_NE(integerResampleLayout, nullptr);
    EXPECT_EQ(integerResampleLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(integerResampleLayout->spacing(), t.spacing.md);
    const auto* integerSizeLayout = qobject_cast<QGridLayout*>(integerUi.groupBox->layout());
    ASSERT_NE(integerSizeLayout, nullptr);
    EXPECT_EQ(integerSizeLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(integerSizeLayout->verticalSpacing(), t.spacing.sm);

    QWidget cfaHost;
    const auto cfaUi = an::ui::processes::buildCFAIntegerResampleProcessUi(&cfaHost);
    ASSERT_NE(cfaHost.layout(), nullptr);
    ASSERT_NE(cfaUi.groupBox_2, nullptr);
    ASSERT_NE(cfaUi.groupBox, nullptr);
    EXPECT_EQ(cfaHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(cfaHost.layout()->contentsMargins().left(), t.spacing.md);
    const auto* cfaResampleLayout = qobject_cast<QVBoxLayout*>(cfaUi.groupBox_2->layout());
    ASSERT_NE(cfaResampleLayout, nullptr);
    EXPECT_EQ(cfaResampleLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(cfaResampleLayout->spacing(), t.spacing.md);
    const auto* cfaSizeLayout = qobject_cast<QGridLayout*>(cfaUi.groupBox->layout());
    ASSERT_NE(cfaSizeLayout, nullptr);
    EXPECT_EQ(cfaSizeLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(cfaSizeLayout->verticalSpacing(), t.spacing.sm);
}

// The debayer, merge and split filter-array windows space their rows in the tokens' steps.
TEST(ProcessWindowLayoutTest, DebayerAndFilterArrayWindowsUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget debayerHost;
    const auto debayerUi = an::ui::processes::buildDebayerProcessUi(&debayerHost);
    ASSERT_NE(debayerHost.layout(), nullptr);
    ASSERT_NE(debayerUi.buttonBox, nullptr);
    EXPECT_EQ(debayerHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(debayerHost.layout()->contentsMargins().left(), t.spacing.md);
    const auto* debayerForm = debayerHost.findChild<QFormLayout*>(QStringLiteral("formLayout"));
    ASSERT_NE(debayerForm, nullptr);
    EXPECT_EQ(debayerForm->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(debayerForm->verticalSpacing(), t.spacing.sm);

    QWidget mergeHost;
    const auto mergeUi = an::ui::processes::buildMergeCFAProcessUi(&mergeHost);
    ASSERT_NE(mergeHost.layout(), nullptr);
    ASSERT_NE(mergeUi.horizontalLayout, nullptr);
    ASSERT_NE(mergeUi.gridLayout, nullptr);
    EXPECT_EQ(mergeHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(mergeHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(mergeUi.horizontalLayout->spacing(), t.spacing.sm);
    EXPECT_EQ(mergeUi.gridLayout->spacing(), t.spacing.sm);

    QWidget splitHost;
    const auto splitUi = an::ui::processes::buildSplitCFAProcessUi(&splitHost);
    ASSERT_NE(splitHost.layout(), nullptr);
    ASSERT_NE(splitUi.gridLayout, nullptr);
    ASSERT_NE(splitUi.horizontalLayout, nullptr);
    EXPECT_EQ(splitHost.layout()->spacing(), t.spacing.md);
    EXPECT_EQ(splitHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(splitUi.gridLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(splitUi.gridLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(splitUi.horizontalLayout->spacing(), t.spacing.md);
}

// The morphological transformation and cosmetic correction windows space their groups and selectors in the tokens'
// steps.
TEST(ProcessWindowLayoutTest, MorphologyAndCosmeticCorrectionUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget morphHost;
    const auto morphUi = an::ui::processes::buildMorphologicalTransformationProcessUi(&morphHost);
    ASSERT_NE(morphUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(morphUi.groupBox, nullptr);
    ASSERT_NE(morphUi.groupBox_2, nullptr);
    EXPECT_EQ(morphUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(morphUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(morphUi.horizontalLayoutMain->contentsMargins().top(), t.spacing.md);
    const auto* morphGroupLayout = qobject_cast<QGridLayout*>(morphUi.groupBox->layout());
    ASSERT_NE(morphGroupLayout, nullptr);
    EXPECT_EQ(morphGroupLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(morphGroupLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(morphGroupLayout->contentsMargins().left(), t.spacing.md);
    const auto* morphSecondLayout = qobject_cast<QHBoxLayout*>(morphUi.groupBox_2->layout());
    ASSERT_NE(morphSecondLayout, nullptr);
    EXPECT_EQ(morphSecondLayout->spacing(), t.spacing.md);
    EXPECT_EQ(morphSecondLayout->contentsMargins().left(), t.spacing.md);
    const auto* morphSelector = morphHost.findChild<QFormLayout*>(QStringLiteral("formLayout"));
    ASSERT_NE(morphSelector, nullptr);
    EXPECT_EQ(morphSelector->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(morphSelector->horizontalSpacing(), t.spacing.md);

    QWidget cosmeticHost;
    const auto cosmeticUi = an::ui::processes::buildCosmeticCorrectionProcessUi(&cosmeticHost);
    ASSERT_NE(cosmeticUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(cosmeticUi.hotPixelsGroupBox, nullptr);
    ASSERT_NE(cosmeticUi.coldPixelsGroupBox, nullptr);
    ASSERT_NE(cosmeticUi.groupBox, nullptr);
    EXPECT_EQ(cosmeticUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(cosmeticUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    for (const QWidget* group : {static_cast<const QWidget*>(cosmeticUi.hotPixelsGroupBox),
                                 static_cast<const QWidget*>(cosmeticUi.coldPixelsGroupBox)})
    {
        const auto* pixelsLayout = qobject_cast<QHBoxLayout*>(group->layout());
        ASSERT_NE(pixelsLayout, nullptr) << group->objectName().toStdString();
        EXPECT_EQ(pixelsLayout->spacing(), t.spacing.md);
        EXPECT_EQ(pixelsLayout->contentsMargins().left(), t.spacing.md);
    }
    const auto* generalLayout = qobject_cast<QGridLayout*>(cosmeticUi.groupBox->layout());
    ASSERT_NE(generalLayout, nullptr);
    EXPECT_EQ(generalLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(generalLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(generalLayout->contentsMargins().left(), t.spacing.md);
    const auto* cosmeticSelector = cosmeticHost.findChild<QFormLayout*>(QStringLiteral("formLayout"));
    ASSERT_NE(cosmeticSelector, nullptr);
    EXPECT_EQ(cosmeticSelector->verticalSpacing(), t.spacing.sm);
    EXPECT_EQ(cosmeticSelector->horizontalSpacing(), t.spacing.md);
}

// The histogram transformation window spaces its content in the tokens' steps.
TEST(ProcessWindowLayoutTest, HistogramTransformationUsesTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = an::ui::processes::buildHistogramTransformationProcessUi(&host);
    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.targetImageSelector, nullptr);
    ASSERT_NE(ui.preview, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().top(), t.spacing.md);
}

// The binarize, banding reduction and range selection windows lay their content in their root layout and space it in
// the tokens' steps.
TEST(ProcessWindowLayoutTest, BinarizeBandingAndRangeSelectionUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget binarizeHost;
    const auto binarizeUi = an::ui::processes::buildBinarizeProcessUi(&binarizeHost);
    ASSERT_NE(binarizeHost.layout(), nullptr);
    ASSERT_NE(binarizeUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(binarizeUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(binarizeUi.verticalLayout, nullptr);
    ASSERT_NE(binarizeUi.horizontalLayout, nullptr);
    ASSERT_NE(binarizeUi.horizontalLayout_2, nullptr);
    EXPECT_EQ(binarizeHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(binarizeUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(binarizeUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(binarizeUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(binarizeUi.horizontalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(binarizeUi.horizontalLayout_2->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(binarizeHost.layout()->indexOf(binarizeUi.horizontalLayoutWidget_4), 0);

    QWidget bandingHost;
    const auto bandingUi = an::ui::processes::buildBandingReductionProcessUi(&bandingHost);
    ASSERT_NE(bandingUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(bandingUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(bandingUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    const auto* bandingGrid = bandingHost.findChild<QGridLayout*>(QStringLiteral("gridLayout"));
    ASSERT_NE(bandingGrid, nullptr);
    EXPECT_EQ(bandingGrid->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(bandingGrid->verticalSpacing(), t.spacing.sm);

    QWidget rangeHost;
    const auto rangeUi = an::ui::processes::buildRangeSelectionProcessUi(&rangeHost);
    ASSERT_NE(rangeHost.layout(), nullptr);
    ASSERT_NE(rangeUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(rangeUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(rangeUi.verticalLayout, nullptr);
    ASSERT_NE(rangeUi.horizontalLayout, nullptr);
    ASSERT_NE(rangeUi.horizontalLayout_2, nullptr);
    EXPECT_EQ(rangeHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(rangeUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(rangeUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(rangeUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(rangeUi.horizontalLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(rangeUi.horizontalLayout_2->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(rangeHost.layout()->indexOf(rangeUi.horizontalLayoutWidget_4), 0);
}

// The curves window spaces its histogram settings and channel selector in the tokens' steps and keeps its scrolled
// settings at their designed size.
TEST(ProcessWindowLayoutTest, CurvesUsesTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget host;
    const auto ui = an::ui::processes::buildCurvesTransformationProcessUi(&host);
    ASSERT_NE(ui.horizontalLayoutMain, nullptr);
    ASSERT_NE(ui.verticalLayout, nullptr);
    ASSERT_NE(ui.histogramSettingsLayout, nullptr);
    ASSERT_NE(ui.channelSelectorLayout, nullptr);
    ASSERT_NE(ui.scrollAreaWidgetContents, nullptr);
    EXPECT_EQ(ui.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(ui.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(ui.histogramSettingsLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.channelSelectorLayout->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(ui.scrollAreaWidgetContents->minimumSize(), QSize(348, 248));
}

// The tighten edges, statistical stretch and noise generator windows space their content in the tokens' steps, and
// the tighten edges window is not capped in size.
TEST(ProcessWindowLayoutTest, TightenEdgesStatisticalStretchAndNoiseUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget tightenHost;
    const auto tightenUi = an::ui::processes::buildTightenEdgesProcessUi(&tightenHost);
    ASSERT_NE(tightenHost.layout(), nullptr);
    ASSERT_NE(tightenUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(tightenUi.verticalLayout, nullptr);
    ASSERT_NE(tightenUi.gridLayout, nullptr);
    EXPECT_EQ(tightenHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(tightenUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(tightenUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(tightenUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(tightenUi.gridLayout->verticalSpacing(), t.spacing.md);
    EXPECT_GT(tightenHost.maximumSize().width(), 10000);
    EXPECT_GT(tightenHost.maximumSize().height(), 10000);

    QWidget statisticalHost;
    const auto statisticalUi = an::ui::processes::buildStatisticalStretchUi(&statisticalHost);
    ASSERT_NE(statisticalUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(statisticalUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(statisticalUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget noiseHost;
    const auto noiseUi = an::ui::processes::buildNoiseGeneratorProcessUi(&noiseHost);
    ASSERT_NE(noiseUi.gridLayout, nullptr);
    EXPECT_EQ(noiseUi.gridLayout->horizontalSpacing(), t.spacing.md);
    EXPECT_EQ(noiseUi.gridLayout->verticalSpacing(), t.spacing.sm);
}

// The CLAHE, arcsinh stretch and LCh adjustment windows space their content in the tokens' steps, and the CLAHE window
// is not capped in size.
TEST(ProcessWindowLayoutTest, ClaheArcSinhAndLchUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget claheHost;
    const auto claheUi = an::ui::processes::buildCLAHEProcessUi(&claheHost);
    ASSERT_NE(claheHost.layout(), nullptr);
    ASSERT_NE(claheUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(claheUi.verticalLayout, nullptr);
    ASSERT_NE(claheUi.gridLayout, nullptr);
    EXPECT_EQ(claheHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(claheUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(claheUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(claheUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(claheUi.gridLayout->verticalSpacing(), t.spacing.sm);
    EXPECT_GT(claheHost.maximumSize().width(), 10000);
    EXPECT_GT(claheHost.maximumSize().height(), 10000);

    QWidget arcHost;
    const auto arcUi = an::ui::processes::buildArcSinhStretchProcessUi(&arcHost);
    ASSERT_NE(arcUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(arcUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(arcUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget adjustHost;
    const auto adjustUi = an::ui::processes::buildAdjustLChProcessUi(&adjustHost);
    ASSERT_NE(adjustUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(adjustUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(adjustUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
}

// The sharpening, analysis, G'MIC, comparison and tone windows space their content in the tokens' steps; the ones the
// person resizes lay their content in their root layout and are not capped in size.
TEST(ProcessWindowLayoutTest, SharpeningAnalysisAndToneWindowsUseTokenSpacing)
{
    const auto& t = an::ui::tokens();

    QWidget sharpenHost;
    const auto sharpenUi = an::ui::processes::buildSharpenAlphaProcessUi(&sharpenHost);
    ASSERT_NE(sharpenHost.layout(), nullptr);
    ASSERT_NE(sharpenUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(sharpenUi.horizontalLayoutWidget_4, nullptr);
    ASSERT_NE(sharpenUi.verticalLayout, nullptr);
    EXPECT_EQ(sharpenHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(sharpenUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(sharpenUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(sharpenUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_EQ(sharpenHost.layout()->indexOf(sharpenUi.horizontalLayoutWidget_4), 0);

    QWidget analyzerHost;
    const auto analyzerUi = an::ui::processes::buildImageListAnalyzerProcessUi(&analyzerHost);
    ASSERT_NE(analyzerUi.verticalLayout_Main, nullptr);
    EXPECT_EQ(analyzerUi.verticalLayout_Main->spacing(), t.spacing.md);
    EXPECT_EQ(analyzerUi.verticalLayout_Main->contentsMargins().left(), t.spacing.md);

    QWidget grayHost;
    const auto grayUi = an::ui::processes::buildGRAYCstorationProcessUi(&grayHost);
    ASSERT_NE(grayHost.layout(), nullptr);
    ASSERT_NE(grayUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(grayUi.verticalLayout, nullptr);
    EXPECT_EQ(grayHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(grayUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(grayUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(grayUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_GT(grayHost.maximumSize().width(), 10000);
    EXPECT_GT(grayHost.maximumSize().height(), 10000);

    QWidget compareHost;
    const auto compareUi = an::ui::processes::buildImageCompareProcessUi(&compareHost);
    ASSERT_NE(compareUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(compareUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(compareUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget gammaHost;
    const auto gammaUi = an::ui::processes::buildGammaCorrectionProcessUi(&gammaHost);
    ASSERT_NE(gammaUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(gammaUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(gammaUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget extractHost;
    const auto extractUi = an::ui::processes::buildExtractSequenceProcessUi(&extractHost);
    ASSERT_NE(extractUi.verticalLayout, nullptr);
    EXPECT_EQ(extractUi.verticalLayout->spacing(), t.spacing.md);
    EXPECT_EQ(extractUi.verticalLayout->contentsMargins().left(), t.spacing.md);

    QWidget exposureHost;
    const auto exposureUi = an::ui::processes::buildExposureCorrectionProcessUi(&exposureHost);
    ASSERT_NE(exposureUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(exposureUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(exposureUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget colorizeHost;
    const auto colorizeUi = an::ui::processes::buildColorizeProcessUi(&colorizeHost);
    ASSERT_NE(colorizeHost.layout(), nullptr);
    ASSERT_NE(colorizeUi.horizontalLayoutMain, nullptr);
    ASSERT_NE(colorizeUi.verticalLayout, nullptr);
    EXPECT_EQ(colorizeHost.layout()->contentsMargins().left(), t.spacing.md);
    EXPECT_EQ(colorizeUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(colorizeUi.verticalLayout->contentsMargins().top(), t.spacing.md);
    EXPECT_GT(colorizeHost.maximumSize().width(), 10000);
    EXPECT_GT(colorizeHost.maximumSize().height(), 10000);

    QWidget hslHost;
    const auto hslUi = an::ui::processes::buildAdjustHSLProcessUi(&hslHost);
    ASSERT_NE(hslUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(hslUi.horizontalLayoutMain->spacing(), t.spacing.md);
    EXPECT_EQ(hslUi.horizontalLayoutMain->contentsMargins().left(), t.spacing.md);

    QWidget brightnessHost;
    const auto brightnessUi = an::ui::processes::buildAdjustBrightnessAndContrastProcessUi(&brightnessHost);
    ASSERT_NE(brightnessHost.layout(), nullptr);
    ASSERT_NE(brightnessUi.horizontalLayoutMain, nullptr);
    EXPECT_EQ(brightnessUi.horizontalLayoutMain->spacing(), t.spacing.md);
    brightnessHost.resize(900, 620);
    EXPECT_GE(brightnessHost.width(), 900);
    EXPECT_GE(brightnessHost.height(), 620);
}
