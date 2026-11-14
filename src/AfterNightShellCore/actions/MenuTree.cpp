// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "actions/MenuTree.h"

#include "actions/ActionIds.h"

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
#include <QMenuBar>

namespace an::ui::shell
{

namespace
{

/// The context the menus' titles are translated under: the one the classic window's titles always had.
constexpr const char* kTranslationContext = "AfterNightAppClass";

// clang-format off
constexpr MenuItem kFileItems[] = {
    {MenuItemKind::Action, ids::kOpen},
    {MenuItemKind::Menu, "menuOpen_Recent"},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kNewSequence},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kSaveAs},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kClose},
    {MenuItemKind::Action, ids::kCloseAll},
    {MenuItemKind::Action, ids::kCloseAllOthers},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExit},
};

constexpr MenuItem kEditItems[] = {
    {MenuItemKind::Action, ids::kUndo},
    {MenuItemKind::Action, ids::kRedo},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kImageHistory},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kSettings},
};

constexpr MenuItem kView2Items[] = {
    {MenuItemKind::Action, ids::kLogViewer},
};

constexpr MenuItem kImageItems[] = {
    {MenuItemKind::Menu, "menuActions"},
    {MenuItemKind::Menu, "menuConvert_Format"},
    {MenuItemKind::Menu, "menuExtract_Channel"},
    {MenuItemKind::Menu, "menuGeometry"},
    {MenuItemKind::Menu, "menuInformation"},
    {MenuItemKind::Menu, "menuMask"},
    {MenuItemKind::Menu, "menuSequence"},
    {MenuItemKind::Menu, "menuView"},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kShowInFileBrowser},
};

constexpr MenuItem kActionsItems[] = {
    {MenuItemKind::Action, ids::kDuplicate},
    {MenuItemKind::Action, ids::kRename},
    {MenuItemKind::Action, ids::kClose2},
    {MenuItemKind::Action, ids::kSaveFile},
};

constexpr MenuItem kConvertFormatItems[] = {
    {MenuItemKind::Action, ids::kConvertTo8Bit},
    {MenuItemKind::Action, ids::kConvertToSigned16Bit},
    {MenuItemKind::Action, ids::kConvertToUnsigned16Bit},
    {MenuItemKind::Action, ids::kConvertToSigned32Bit},
    {MenuItemKind::Action, ids::kConvertToUnsigned32Bit},
    {MenuItemKind::Action, ids::kConvertTo32BitFloat},
    {MenuItemKind::Action, ids::kConvertTo64BitFloat},
};

constexpr MenuItem kExtractChannelItems[] = {
    {MenuItemKind::Action, ids::kExtractRedChannel},
    {MenuItemKind::Action, ids::kExtractGreenChannel},
    {MenuItemKind::Action, ids::kExtractBlueChannel},
    {MenuItemKind::Action, ids::kExtractRGBChannels},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExtractCIEL},
    {MenuItemKind::Action, ids::kExtractCIEA},
    {MenuItemKind::Action, ids::kExtractCIEB},
    {MenuItemKind::Action, ids::kExtractCIEC},
    {MenuItemKind::Action, ids::kExtractCIEH},
    {MenuItemKind::Action, ids::kExtractCIEAB},
    {MenuItemKind::Action, ids::kExtractCIELAB},
    {MenuItemKind::Action, ids::kExtractCIELCH},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExtractCIEX},
    {MenuItemKind::Action, ids::kExtractCIEY},
    {MenuItemKind::Action, ids::kExtractCIEZ},
    {MenuItemKind::Action, ids::kExtractCIEXYZ},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExtractHSLHue},
    {MenuItemKind::Action, ids::kExtractHSLSaturation},
    {MenuItemKind::Action, ids::kExtractHSLLightness},
    {MenuItemKind::Action, ids::kExtractHSL},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExtractHSVHue},
    {MenuItemKind::Action, ids::kExtractHSVSaturation},
    {MenuItemKind::Action, ids::kExtractHSVValue},
    {MenuItemKind::Action, ids::kExtractHSV},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExtractHSIHue},
    {MenuItemKind::Action, ids::kExtractHSISaturation},
    {MenuItemKind::Action, ids::kExtractHSIIntensity},
    {MenuItemKind::Action, ids::kExtractHSI},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kExtractYCbCrLuminance},
    {MenuItemKind::Action, ids::kExtractYCbCrBlueChrominance},
    {MenuItemKind::Action, ids::kExtractYCbCrRedChrominance},
    {MenuItemKind::Action, ids::kExtractYCbCr},
};

constexpr MenuItem kGeometryItems[] = {
    {MenuItemKind::Action, ids::kHorizontalMirror},
    {MenuItemKind::Action, ids::kVerticalMirror},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kRotate180},
    {MenuItemKind::Action, ids::kRotate90Left},
    {MenuItemKind::Action, ids::kRotate90Right},
};

constexpr MenuItem kInformationItems[] = {
    {MenuItemKind::Action, ids::kStatistics},
    {MenuItemKind::Action, ids::kHistogram},
    {MenuItemKind::Action, ids::kImageEXIFViewer},
    {MenuItemKind::Action, ids::kFITSHeader},
    {MenuItemKind::Action, ids::kAstrometryInfo},
    {MenuItemKind::Action, ids::kPlateSolve},
    {MenuItemKind::Action, ids::kAnnotateImage},
    {MenuItemKind::Action, ids::kSummary},
};

constexpr MenuItem kMaskItems[] = {
    {MenuItemKind::Action, ids::kSelectMask},
    {MenuItemKind::Action, ids::kRemoveMask},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kInvertMask},
    {MenuItemKind::Action, ids::kEnableMask},
    {MenuItemKind::Action, ids::kShowMask},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Menu, "menuRendering_Mode"},
};

constexpr MenuItem kRenderingModeItems[] = {
    {MenuItemKind::Action, ids::kOverlayRed},
    {MenuItemKind::Action, ids::kOverlayGreen},
    {MenuItemKind::Action, ids::kOverlayBlue},
    {MenuItemKind::Action, ids::kOverlayYellow},
    {MenuItemKind::Action, ids::kOverlayMagenta},
    {MenuItemKind::Action, ids::kOverlayCyan},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kReplace},
    {MenuItemKind::Action, ids::kMultiply},
};

constexpr MenuItem kSequenceItems[] = {
    {MenuItemKind::Menu, "menuConvert"},
    {MenuItemKind::Action, ids::kExtractFrame},
    {MenuItemKind::Action, ids::kExtractSequence},
    {MenuItemKind::Action, ids::kExport},
};

constexpr MenuItem kConvertItems[] = {
    {MenuItemKind::Action, ids::kConvertToSER},
    {MenuItemKind::Action, ids::kConvertToImageList},
    {MenuItemKind::Action, ids::kConvertToFITS},
};

constexpr MenuItem kViewItems[] = {
    {MenuItemKind::Action, ids::kZoomIn},
    {MenuItemKind::Action, ids::kZoomOut},
    {MenuItemKind::Action, ids::kZoom100},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kFitToWindow},
    {MenuItemKind::Action, ids::kMaximizeView},
};

constexpr MenuItem kProcessItems[] = {
    {MenuItemKind::Menu, "menuFavorite_Processes"},
    {MenuItemKind::Menu, "menuRecent_Processes"},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Menu, "menuAdjustments"},
    {MenuItemKind::Menu, "menuBackground_Modelization"},
    {MenuItemKind::Menu, "menuColor_Calibration"},
    {MenuItemKind::Menu, "menuColor_Spaces"},
    {MenuItemKind::Menu, "menuDetails"},
    {MenuItemKind::Menu, "menuFilters"},
    {MenuItemKind::Menu, "menuGeometry_2"},
    {MenuItemKind::Menu, "menuIntensity_Transformations"},
    {MenuItemKind::Menu, "menuMask_Generation"},
    {MenuItemKind::Menu, "menuMorphology"},
    {MenuItemKind::Menu, "menuMultiscale"},
    {MenuItemKind::Menu, "menuNoise_Reduction"},
    {MenuItemKind::Menu, "menuPixel_Math"},
    {MenuItemKind::Menu, "menuPreprocess"},
    {MenuItemKind::Menu, "menuStar_Processing"},
    {MenuItemKind::Menu, "menuUtilities"},
};

constexpr MenuItem kAdjustmentsItems[] = {
    {MenuItemKind::Action, ids::kAdjustBrightnessContrast},
    {MenuItemKind::Action, ids::kGammaCorrection},
    {MenuItemKind::Action, ids::kExposure},
    {MenuItemKind::Action, ids::kAdjustHSL},
    {MenuItemKind::Action, ids::kAdjustLightnessChromaHue},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kNegativeTransformation},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kConvertToRGB},
    {MenuItemKind::Action, ids::kConvertToGrayscale},
};

constexpr MenuItem kBackgroundModelizationItems[] = {
    {MenuItemKind::Action, ids::kBackgroundExtraction},
    {MenuItemKind::Action, ids::kAutoGradientRemoval},
};

constexpr MenuItem kColorCalibrationItems[] = {
    {MenuItemKind::Action, ids::kBackgroundNeutralization},
    {MenuItemKind::Action, ids::kSCNR},
    {MenuItemKind::Action, ids::kColorCalibration2},
    {MenuItemKind::Action, ids::kPhotometricColorCalibration},
    {MenuItemKind::Action, ids::kSpectrophotometricColorCalibration},
    {MenuItemKind::Action, ids::kLinearMatch},
};

constexpr MenuItem kColorSpacesItems[] = {
    {MenuItemKind::Action, ids::kChannelComposition},
    {MenuItemKind::Action, ids::kColorize},
    {MenuItemKind::Action, ids::kDebayer},
    {MenuItemKind::Action, ids::kLRGBCombination},
    {MenuItemKind::Action, ids::kNBRGBCombination},
    {MenuItemKind::Action, ids::kNBColorMapper},
    {MenuItemKind::Action, ids::kNarrowbandPaletteComposer},
    {MenuItemKind::Action, ids::kMergeCFA},
    {MenuItemKind::Action, ids::kSplitCFA},
};

constexpr MenuItem kDetailsItems[] = {
    {MenuItemKind::Action, ids::kGaussianBlur},
    {MenuItemKind::Action, ids::kHighPass},
    {MenuItemKind::Action, ids::kLocalContrastEnhancement},
    {MenuItemKind::Action, ids::kDarkStructureEnhance},
    {MenuItemKind::Action, ids::kMultiscaleAlphaSharpen},
    {MenuItemKind::Action, ids::kUnsharpMask},
};

constexpr MenuItem kFiltersItems[] = {
    {MenuItemKind::Action, ids::kGaussianBlur},
    {MenuItemKind::Action, ids::kMedianFilter},
    {MenuItemKind::Action, ids::kBilateralFilter},
    {MenuItemKind::Action, ids::kBandingReductionFilter},
    {MenuItemKind::Action, ids::kCosmeticCorrectionFilter},
};

constexpr MenuItem kGeometry2Items[] = {
    {MenuItemKind::Action, ids::kResize},
    {MenuItemKind::Action, ids::kCrop},
    {MenuItemKind::Action, ids::kRotate},
    {MenuItemKind::Action, ids::kIntegerResample},
    {MenuItemKind::Action, ids::kCFAIntegerResample},
};

constexpr MenuItem kIntensityTransformationsItems[] = {
    {MenuItemKind::Action, ids::kArcSinhStretch},
    {MenuItemKind::Action, ids::kBinarize},
    {MenuItemKind::Action, ids::kCLAHistogramEqualization},
    {MenuItemKind::Action, ids::kColorSaturation},
    {MenuItemKind::Action, ids::kCurvesTransformation},
    {MenuItemKind::Action, ids::kGeneralizedHyperbolicTransformation},
    {MenuItemKind::Action, ids::kHistogramTransformation},
    {MenuItemKind::Action, ids::kLocalHistogramEqualization},
    {MenuItemKind::Action, ids::kStatisticalStretch},
};

constexpr MenuItem kMaskGenerationItems[] = {
    {MenuItemKind::Action, ids::kMaskDrawing},
    {MenuItemKind::Action, ids::kRangeSelection},
    {MenuItemKind::Action, ids::kStarMask},
    {MenuItemKind::Action, ids::kStarnetStarMask},
};

constexpr MenuItem kMorphologyItems[] = {
    {MenuItemKind::Action, ids::kMorphologicalTransformation},
    {MenuItemKind::Action, ids::kTightenEdges},
};

constexpr MenuItem kMultiscaleItems[] = {
    {MenuItemKind::Action, ids::kMultiscaleLinearTransform},
};

constexpr MenuItem kNoiseReductionItems[] = {
    {MenuItemKind::Action, ids::kDeepSNR},
    {MenuItemKind::Action, ids::kGRAYCstorationFilter},
    {MenuItemKind::Action, ids::kNonLocalBayesFilter},
    {MenuItemKind::Action, ids::kPatchPCAFilter},
    {MenuItemKind::Action, ids::kTotalVariationDenoiseFilter},
    {MenuItemKind::Action, ids::kWaveletDenoiseFilter},
};

constexpr MenuItem kPixelMathItems[] = {
    {MenuItemKind::Action, ids::kPixelMath2},
};

constexpr MenuItem kPreprocessItems[] = {
    {MenuItemKind::Action, ids::kImageCalibration},
    {MenuItemKind::Action, ids::kImageRegistration},
    {MenuItemKind::Action, ids::kImageStacking},
    {MenuItemKind::Action, ids::kImageIntegration},
    {MenuItemKind::Action, ids::kImageIntegrationLite},
};

constexpr MenuItem kStarProcessingItems[] = {
    {MenuItemKind::Action, ids::kProfiledStarmaskRecombination},
    {MenuItemKind::Action, ids::kStarmaskRecombination},
    {MenuItemKind::Action, ids::kStarReduction},
    {MenuItemKind::Action, ids::kStarnet},
    {MenuItemKind::Action, ids::kStarProfilerV1},
    {MenuItemKind::Action, ids::kStarProfilerV2},
};

constexpr MenuItem kUtilitiesItems[] = {
    {MenuItemKind::Action, ids::kImageBlend},
    {MenuItemKind::Action, ids::kImageCompare},
    {MenuItemKind::Action, ids::kNoiseGenerator},
    {MenuItemKind::Action, ids::kImageListAnalyzer},
};

constexpr MenuItem kExtensionsItems[] = {
    {MenuItemKind::Action, ids::kManageExtensions},
};

constexpr MenuItem kWindowItems[] = {
    {MenuItemKind::Action, ids::kLockLayout},
    {MenuItemKind::Action, ids::kUnlockLayout},
    {MenuItemKind::Separator, {}},
    {MenuItemKind::Action, ids::kGroupAllImages},
    {MenuItemKind::Action, ids::kSplitAllImages},
    {MenuItemKind::Action, ids::kFitImagesToWindow},
};

constexpr MenuItem kHelpItems[] = {
    {MenuItemKind::Action, ids::kUserGuide},
};

constexpr MenuItem kAboutItems[] = {
    {MenuItemKind::Action, ids::kAboutQt},
    {MenuItemKind::Action, ids::kAboutAfterNight},
    {MenuItemKind::Action, ids::kSystemInformation},
};

constexpr MenuNode kMenus[] = {
    {"menuFile", "File", false, true, kFileItems},
    {"menuOpen_Recent", "Open Recent", true, false, {}},
    {"menuEdit", "Edit", false, true, kEditItems},
    {"menuView_2", "View", false, true, kView2Items},
    {"menuImage", "Image", false, true, kImageItems},
    {"menuActions", "Actions", false, true, kActionsItems},
    {"menuConvert_Format", "Convert Format", false, true, kConvertFormatItems},
    {"menuExtract_Channel", "Extract Channel", false, true, kExtractChannelItems},
    {"menuGeometry", "Fast Geometry", false, true, kGeometryItems},
    {"menuInformation", "Information", false, true, kInformationItems},
    {"menuMask", "Mask", false, true, kMaskItems},
    {"menuRendering_Mode", "Rendering Mode", false, true, kRenderingModeItems},
    {"menuSequence", "Sequence", false, true, kSequenceItems},
    {"menuConvert", "Convert", false, true, kConvertItems},
    {"menuView", "View", false, true, kViewItems},
    {"menuProcess", "Process", false, true, kProcessItems},
    {"menuFavorite_Processes", "Favorite Processes", true, true, {}},
    {"menuRecent_Processes", "Recent Processes", true, false, {}},
    {"menuAdjustments", "Adjustments", false, true, kAdjustmentsItems},
    {"menuBackground_Modelization", "Background Modelization", false, true, kBackgroundModelizationItems},
    {"menuColor_Calibration", "Color Calibration", false, true, kColorCalibrationItems},
    {"menuColor_Spaces", "Color Spaces", false, true, kColorSpacesItems},
    {"menuDetails", "Details", false, true, kDetailsItems},
    {"menuFilters", "Filters", false, true, kFiltersItems},
    {"menuGeometry_2", "Geometry", false, true, kGeometry2Items},
    {"menuIntensity_Transformations", "Intensity Transformations", false, true, kIntensityTransformationsItems},
    {"menuMask_Generation", "Mask Generation", false, true, kMaskGenerationItems},
    {"menuMorphology", "Morphology", false, true, kMorphologyItems},
    {"menuMultiscale", "Multiscale", false, true, kMultiscaleItems},
    {"menuNoise_Reduction", "Noise Reduction", false, true, kNoiseReductionItems},
    {"menuPixel_Math", "Pixel Math", false, true, kPixelMathItems},
    {"menuPreprocess", "Preprocess", false, true, kPreprocessItems},
    {"menuStar_Processing", "Star Processing", false, true, kStarProcessingItems},
    {"menuUtilities", "Utilities", false, true, kUtilitiesItems},
    {"menuExtensions", "Extensions", false, true, kExtensionsItems},
    {"menuWindow", "Window", false, false, kWindowItems},
    {"menuHelp", "Help", false, true, kHelpItems},
    {"menuAbout", "About", false, true, kAboutItems},
};

constexpr std::string_view kTopLevel[] = {"menuFile", "menuEdit", "menuView_2", "menuImage", "menuProcess", "menuExtensions", "menuWindow", "menuHelp", "menuAbout"};
// clang-format on

constexpr MenuTree kMenuBar(kMenus, kTopLevel);

} // namespace

const MenuTree& MenuTree::menuBar() noexcept
{
    return kMenuBar;
}

std::vector<std::string_view> MenuTree::topLevel() const
{
    return {m_topLevel.begin(), m_topLevel.end()};
}

const MenuNode* MenuTree::menu(const std::string_view id) const noexcept
{
    for (const MenuNode& node : m_menus)
    {
        if (node.id == id)
        {
            return &node;
        }
    }
    return nullptr;
}

std::vector<std::string> MenuTree::pathsOf(const std::string_view actionId) const
{
    std::vector<std::string> paths;
    for (const std::string_view top : m_topLevel)
    {
        collectPaths(top, std::string(), actionId, paths);
    }
    return paths;
}

void MenuTree::collectPaths(const std::string_view menuId, const std::string& above, const std::string_view actionId,
                            std::vector<std::string>& paths) const
{
    const MenuNode* const node = menu(menuId);
    if (node == nullptr || node->varies)
    {
        return;
    }
    const std::string path = above.empty() ? std::string(node->title) : above + " > " + node->title;
    for (const MenuItem& item : node->items)
    {
        if (item.kind == MenuItemKind::Menu)
        {
            collectPaths(item.id, path, actionId, paths);
        }
        else if (item.kind == MenuItemKind::Action && item.id == actionId &&
                 std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    }
}

void MenuTree::buildMenuBar(QMenuBar& bar, const an::ui::ActionLookup& actions) const
{
    for (const std::string_view top : m_topLevel)
    {
        auto* menu = new QMenu(&bar);
        if (buildMenu(*menu, top, actions))
        {
            bar.addAction(menu->menuAction());
        }
        else
        {
            delete menu;
        }
    }
}

bool MenuTree::buildMenu(QMenu& menu, const std::string_view id, const an::ui::ActionLookup& actions) const
{
    const MenuNode* const node = this->menu(id);
    if (node == nullptr)
    {
        return false;
    }
    menu.setObjectName(QString::fromLatin1(node->id.data(), static_cast<qsizetype>(node->id.size())));
    menu.setTitle(QCoreApplication::translate(kTranslationContext, node->title));
    menu.setEnabled(node->enabled);
    for (const MenuItem& item : node->items)
    {
        switch (item.kind)
        {
        case MenuItemKind::Separator:
            menu.addSeparator();
            break;
        case MenuItemKind::Action:
            if (QAction* const action = actions ? actions(item.id) : nullptr)
            {
                menu.addAction(action);
            }
            break;
        case MenuItemKind::Menu: {
            auto* submenu = new QMenu(&menu);
            if (buildMenu(*submenu, item.id, actions))
            {
                menu.addAction(submenu->menuAction());
            }
            else
            {
                delete submenu;
            }
            break;
        }
        }
    }
    return true;
}

} // namespace an::ui::shell
