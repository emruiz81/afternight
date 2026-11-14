// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "actions/ActionCatalogue.h"

#include "actions/ActionIds.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeySequence>
#include <QObject>

#include <algorithm>
#include <array>
#include <optional>
#include <string>

namespace an::ui::shell
{

namespace
{

/// The context the actions' texts are translated under: the one the classic window's texts always had.
constexpr const char* kTranslationContext = "AfterNightAppClass";

// clang-format off
/// Every action of the application, in the order the classic window declared them. An action a shell alone gives
/// something - a shortcut of its own, a place in a menu of its own - is not given it here.
constexpr ActionSpec kSpecs[] = {
    {.id = ids::kOpen, .text = "Open", .group = ActionGroup::File, .iconKey = "folder_open", .shortcut = "Ctrl+O"},
    {.id = ids::kSaveAs, .text = "Save as...", .group = ActionGroup::File, .iconKey = "file_save", .shortcut = "Ctrl+Shift+S"},
    {.id = ids::kExit, .text = "Exit", .group = ActionGroup::File, .iconKey = "exit"},
    {.id = ids::kAboutQt, .text = "About Qt", .group = ActionGroup::Help},
    {.id = ids::kAboutAfterNight, .text = "About AfterNight", .group = ActionGroup::Help},
    {.id = ids::kSystemInformation, .text = "System Information", .group = ActionGroup::Help},
    {.id = ids::kUserGuide, .text = "User Guide", .group = ActionGroup::Help, .iconKey = "help", .shortcut = "F1", .shortcutContext = Qt::ApplicationShortcut, .toolTip = "Open the AfterNight User Guide.", .statusTip = "Open the AfterNight User Guide.", .whatsThis = "Open the AfterNight User Guide."},
    {.id = ids::kUndo, .text = "Undo", .group = ActionGroup::Edit, .iconKey = "undo", .shortcut = "Ctrl+Z", .enabled = false},
    {.id = ids::kRedo, .text = "Redo", .group = ActionGroup::Edit, .iconKey = "redo", .shortcut = "Ctrl+Y", .enabled = false},
    {.id = ids::kClose, .text = "Close", .group = ActionGroup::File, .iconKey = "close"},
    {.id = ids::kCloseAll, .text = "Close All", .group = ActionGroup::File, .iconKey = "close"},
    {.id = ids::kDuplicate, .text = "Duplicate", .group = ActionGroup::Image, .iconKey = "copy"},
    {.id = ids::kRename, .text = "Rename", .group = ActionGroup::Image, .iconKey = "edit"},
    {.id = ids::kShowInFileBrowser, .text = "Show in File Browser", .group = ActionGroup::Image, .iconKey = "folder_open", .enabled = false},
    {.id = ids::kExtractRedChannel, .text = "Extract Red Channel", .group = ActionGroup::Image, .iconKey = "red_circle"},
    {.id = ids::kExtractGreenChannel, .text = "Extract Green Channel", .group = ActionGroup::Image, .iconKey = "green_circle"},
    {.id = ids::kExtractBlueChannel, .text = "Extract Blue Channel", .group = ActionGroup::Image, .iconKey = "blue_circle"},
    {.id = ids::kExtractRGBChannels, .text = "Extract RGB Channels", .group = ActionGroup::Image, .iconKey = "rgb"},
    {.id = ids::kConvertTo8Bit, .text = "Convert to 8 bit", .group = ActionGroup::Image},
    {.id = ids::kConvertToSigned16Bit, .text = "Convert to signed 16 bit", .group = ActionGroup::Image},
    {.id = ids::kConvertToUnsigned16Bit, .text = "Convert to unsigned 16 bit", .group = ActionGroup::Image},
    {.id = ids::kConvertToSigned32Bit, .text = "Convert to signed 32 bit", .group = ActionGroup::Image},
    {.id = ids::kConvertToUnsigned32Bit, .text = "Convert to unsigned 32 bit", .group = ActionGroup::Image},
    {.id = ids::kConvertTo32BitFloat, .text = "Convert to 32 bit float", .group = ActionGroup::Image},
    {.id = ids::kConvertTo64BitFloat, .text = "Convert to 64 bit float", .group = ActionGroup::Image},
    {.id = ids::kAction, .text = ".", .group = ActionGroup::None},
    {.id = ids::kHorizontalMirror, .text = "Horizontal Mirror", .group = ActionGroup::Image, .iconKey = "swap_horiz"},
    {.id = ids::kVerticalMirror, .text = "Vertical Mirror", .group = ActionGroup::Image, .iconKey = "swap_vert"},
    {.id = ids::kArcSinhStretch, .text = "ArcSinh Stretch", .group = ActionGroup::Process},
    {.id = ids::kRotate180, .text = "Rotate 180\302\272", .group = ActionGroup::Image, .iconKey = "rotate_180"},
    {.id = ids::kRotate90Left, .text = "Rotate 90\302\272 Left", .group = ActionGroup::Image, .iconKey = "rotate_90_ccw"},
    {.id = ids::kRotate90Right, .text = "Rotate 90\302\272 Right", .group = ActionGroup::Image, .iconKey = "rotate_90_cw"},
    {.id = ids::kStarnet, .text = "Starnet++", .group = ActionGroup::Process},
    {.id = ids::kStarReduction, .text = "Star Reduction", .group = ActionGroup::Process},
    {.id = ids::kAutomaticMTFTransform, .text = "Automatic MTF Transform", .group = ActionGroup::None},
    {.id = ids::kExtractCIEL, .text = "Extract CIE L*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEA, .text = "Extract CIE a*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEB, .text = "Extract CIE b*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEAB, .text = "Extract CIE a*=R / b*=G", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIELAB, .text = "Extract CIE L*a*b*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIELCH, .text = "Extract CIE L*c*h*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEC, .text = "Extract CIE c*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEH, .text = "Extract CIE h*", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEX, .text = "Extract CIE X", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEY, .text = "Extract CIE Y", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEZ, .text = "Extract CIE Z", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractCIEXYZ, .text = "Extract CIE XYZ", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractHSLHue, .text = "Extract HSL Hue", .group = ActionGroup::Image, .iconKey = "hsl"},
    {.id = ids::kExtractHSLSaturation, .text = "Extract HSL Saturation", .group = ActionGroup::Image, .iconKey = "hsl"},
    {.id = ids::kExtractHSLLightness, .text = "Extract HSL Lightness", .group = ActionGroup::Image, .iconKey = "hsl"},
    {.id = ids::kExtractHSL, .text = "Extract HSL", .group = ActionGroup::Image, .iconKey = "hsl"},
    {.id = ids::kExtractHSVHue, .text = "Extract HSV Hue", .group = ActionGroup::Image, .iconKey = "hsv"},
    {.id = ids::kExtractHSVSaturation, .text = "Extract HSV Saturation", .group = ActionGroup::Image, .iconKey = "hsv"},
    {.id = ids::kExtractHSVValue, .text = "Extract HSV Value", .group = ActionGroup::Image, .iconKey = "hsv"},
    {.id = ids::kExtractHSV, .text = "Extract HSV", .group = ActionGroup::Image, .iconKey = "hsv"},
    {.id = ids::kExtractHSIHue, .text = "Extract HSI Hue", .group = ActionGroup::Image},
    {.id = ids::kExtractHSISaturation, .text = "Extract HSI Saturation", .group = ActionGroup::Image},
    {.id = ids::kExtractHSIIntensity, .text = "Extract HSI Intensity", .group = ActionGroup::Image},
    {.id = ids::kExtractHSI, .text = "Extract HSI", .group = ActionGroup::Image},
    {.id = ids::kNegativeTransformation, .text = "Negative Transformation", .group = ActionGroup::Process, .iconKey = "film"},
    {.id = ids::kImageHistory, .text = "Image History", .group = ActionGroup::Edit, .iconKey = "history", .enabled = false},
    {.id = ids::kAsd, .text = "asd", .group = ActionGroup::None},
    {.id = ids::kAsd2, .text = "asd", .group = ActionGroup::None},
    {.id = ids::kStatistics, .text = "Statistics", .group = ActionGroup::Image, .iconKey = "stats", .enabled = false},
    {.id = ids::kHistogram, .text = "Histogram", .group = ActionGroup::Image, .iconKey = "histogram", .enabled = false},
    {.id = ids::kImageEXIFViewer, .text = "EXIF Viewer", .group = ActionGroup::Image, .iconKey = "camera", .enabled = false},
    {.id = ids::kFITSHeader, .text = "FITS Header", .group = ActionGroup::Image, .iconKey = "header", .enabled = false},
    {.id = ids::kAstrometryInfo, .text = "Astrometry Info", .group = ActionGroup::Image, .iconKey = "summary", .enabled = false},
    {.id = ids::kPlateSolve, .text = "Plate Solve", .group = ActionGroup::Image, .iconKey = "grid", .enabled = false},
    {.id = ids::kAnnotateImage, .text = "Annotate Image...", .group = ActionGroup::Image, .iconKey = "panorama", .enabled = false},
    {.id = ids::kAdjustBrightnessContrast, .text = "Brightness/Contrast", .group = ActionGroup::Process, .iconKey = "brightness"},
    {.id = ids::kGammaCorrection, .text = "Gamma Correction", .group = ActionGroup::Process, .iconKey = "gamma"},
    {.id = ids::kAdjustHSL, .text = "Hue/Saturation/Lightness", .group = ActionGroup::Process, .iconKey = "hsl"},
    {.id = ids::kStarmaskRecombination, .text = "Starmask Recombination", .group = ActionGroup::Process},
    {.id = ids::kSCNR, .text = "SCNR Filter", .group = ActionGroup::Process},
    {.id = ids::kMedianFilter, .text = "Median Filter", .group = ActionGroup::Process},
    {.id = ids::kClose2, .text = "Close", .group = ActionGroup::Image, .iconKey = "close"},
    {.id = ids::kZoomIn, .text = "Zoom In", .group = ActionGroup::Image, .iconKey = "zoom_in", .shortcut = "Ctrl++"},
    {.id = ids::kZoomOut, .text = "Zoom Out", .group = ActionGroup::Image, .iconKey = "zoom_out", .shortcut = "Ctrl+-"},
    {.id = ids::kZoom100, .text = "Zoom 100%", .group = ActionGroup::Image, .iconKey = "zoom_100", .shortcut = "Ctrl+0"},
    {.id = ids::kFitToWindow, .text = "Fit To Window", .group = ActionGroup::Image, .iconKey = "zoom_fit"},
    {.id = ids::kCloseAllOthers, .text = "Close All Others", .group = ActionGroup::File, .iconKey = "close"},
    {.id = ids::kChannelComposition, .text = "Channel Composition", .group = ActionGroup::Process},
    {.id = ids::kDebayer, .text = "Debayer", .group = ActionGroup::Process},
    {.id = ids::kConvertToGrayscale, .text = "Convert to Grayscale", .group = ActionGroup::Process, .iconKey = "grayscale"},
    {.id = ids::kConvertToRGB, .text = "Convert to RGB", .group = ActionGroup::Process, .iconKey = "rgb"},
    {.id = ids::kSummary, .text = "Summary", .group = ActionGroup::Image, .iconKey = "summary"},
    {.id = ids::kResize, .text = "Resize", .group = ActionGroup::Process, .iconKey = "zoom_100"},
    {.id = ids::kCrop, .text = "Crop", .group = ActionGroup::Process, .iconKey = "crop"},
    {.id = ids::kRotate, .text = "Rotate", .group = ActionGroup::Process, .iconKey = "crop_rotate"},
    {.id = ids::kHistogramTransformation, .text = "Histogram Transformation", .group = ActionGroup::Process},
    {.id = ids::kColorCalibration2, .text = "Color Calibration", .group = ActionGroup::Process},
    {.id = ids::kPhotometricColorCalibration, .text = "Photometric Color Calibration", .group = ActionGroup::Process},
    {.id = ids::kSpectrophotometricColorCalibration, .text = "Spectrophotometric Color Calibration", .group = ActionGroup::Process},
    {.id = ids::kBackgroundNeutralization, .text = "Background Neutralization", .group = ActionGroup::Process},
    {.id = ids::kMaskDrawing, .text = "Mask Drawing", .group = ActionGroup::Process},
    {.id = ids::kRangeSelection, .text = "Range Selection", .group = ActionGroup::Process},
    {.id = ids::kSelectMask, .text = "Select Mask", .group = ActionGroup::Image},
    {.id = ids::kRemoveMask, .text = "Remove Mask", .group = ActionGroup::Image},
    {.id = ids::kInvertMask, .text = "Invert Mask", .group = ActionGroup::Image, .checkable = true},
    {.id = ids::kEnableMask, .text = "Enable Mask", .group = ActionGroup::Image, .checkable = true},
    {.id = ids::kShowMask, .text = "Show Mask", .group = ActionGroup::Image, .checkable = true},
    {.id = ids::kOverlayRed, .text = "Overlay Red", .group = ActionGroup::Image, .iconKey = "red_circle", .checkable = true, .checked = true},
    {.id = ids::kOverlayGreen, .text = "Overlay Green", .group = ActionGroup::Image, .iconKey = "green_circle", .checkable = true},
    {.id = ids::kOverlayBlue, .text = "Overlay Blue", .group = ActionGroup::Image, .iconKey = "blue_circle", .checkable = true},
    {.id = ids::kOverlayYellow, .text = "Overlay Yellow", .group = ActionGroup::Image, .iconKey = "yellow_circle", .checkable = true},
    {.id = ids::kOverlayMagenta, .text = "Overlay Magenta", .group = ActionGroup::Image, .iconKey = "magenta_circle", .checkable = true},
    {.id = ids::kOverlayCyan, .text = "Overlay Cyan", .group = ActionGroup::Image, .iconKey = "cyan_circle", .checkable = true},
    {.id = ids::kReplace, .text = "Replace", .group = ActionGroup::Image, .checkable = true},
    {.id = ids::kMultiply, .text = "Multiply", .group = ActionGroup::Image, .checkable = true},
    {.id = ids::kIntegerResample, .text = "Integer Resample", .group = ActionGroup::Process, .iconKey = "grid_2"},
    {.id = ids::kCLAHistogramEqualization, .text = "CLA Histogram Equalization", .group = ActionGroup::Process},
    {.id = ids::kDeepSNR, .text = "DeepSNR", .group = ActionGroup::Process},
    {.id = ids::kNonLocalBayesFilter, .text = "Non-Local Bayes Filter", .group = ActionGroup::Process},
    {.id = ids::kBackgroundExtraction, .text = "Background Extraction", .group = ActionGroup::Process},
    {.id = ids::kAutoGradientRemoval, .text = "Auto Gradient Removal", .group = ActionGroup::Process},
    {.id = ids::kWaveletDenoiseFilter, .text = "Wavelet Denoise Filter", .group = ActionGroup::Process},
    {.id = ids::kPatchPCAFilter, .text = "Patch PCA Denoise Filter", .group = ActionGroup::Process},
    {.id = ids::kLocalHistogramEqualization, .text = "Local Histogram Equalization", .group = ActionGroup::Process},
    {.id = ids::kLocalContrastEnhancement, .text = "Local Contrast Enhancement", .group = ActionGroup::Process},
    {.id = ids::kDarkStructureEnhance, .text = "Dark Structure Enhance", .group = ActionGroup::Process},
    {.id = ids::kLinearMatch, .text = "Linear Match", .group = ActionGroup::Process},
    {.id = ids::kCurvesTransformation, .text = "Curves Transformation", .group = ActionGroup::Process},
    {.id = ids::kExtractYCbCrLuminance, .text = "Extract YCbCr Luma", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractYCbCrBlueChrominance, .text = "Extract YCbCr Blue Chroma", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractYCbCrRedChrominance, .text = "Extract YCbCr Red Chroma", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kExtractYCbCr, .text = "Extract YCbCr", .group = ActionGroup::Image, .iconKey = "cielab"},
    {.id = ids::kMultiscaleLinearTransform, .text = "Multiscale Linear Transform", .group = ActionGroup::Process},
    {.id = ids::kMultiscaleAlphaSharpen, .text = "Multiscale Alpha Sharpen", .group = ActionGroup::Process},
    {.id = ids::kStarMask, .text = "Star Mask", .group = ActionGroup::Process},
    {.id = ids::kStarnetStarMask, .text = "Starnet++ Star Mask", .group = ActionGroup::Process},
    {.id = ids::kGaussianBlur, .text = "Gaussian Blur", .group = ActionGroup::Process},
    {.id = ids::kHighPass, .text = "High Pass", .group = ActionGroup::Process},
    {.id = ids::kFilters, .text = "Filters", .group = ActionGroup::None},
    {.id = ids::kMorphologicalTransformation, .text = "Morphological Transformation", .group = ActionGroup::Process},
    {.id = ids::kGRAYCstorationFilter, .text = "GRAYCstoration Filter", .group = ActionGroup::Process},
    {.id = ids::kAdjustLightnessChromaHue, .text = "Lightness/Chroma/Hue", .group = ActionGroup::Process, .iconKey = "cielab"},
    {.id = ids::kExposure, .text = "Exposure", .group = ActionGroup::Process, .iconKey = "exposure"},
    {.id = ids::kCosmeticCorrectionFilter, .text = "Cosmetic Correction Filter", .group = ActionGroup::Process},
    {.id = ids::kBandingReductionFilter, .text = "Banding Reduction Filter", .group = ActionGroup::Process},
    {.id = ids::kStarProfilerV2, .text = "Star Profiler v2", .group = ActionGroup::Process},
    {.id = ids::kTightenEdges, .text = "Tighten Edges", .group = ActionGroup::Process},
    {.id = ids::kProfiledStarmaskRecombination, .text = "Profiled Starmask Recombination", .group = ActionGroup::Process},
    {.id = ids::kStarProfilerV1, .text = "Star Profiler v1", .group = ActionGroup::Process},
    {.id = ids::kLockLayout, .text = "Lock Layout", .group = ActionGroup::Window, .iconKey = "lock"},
    {.id = ids::kUnlockLayout, .text = "Unlock Layout", .group = ActionGroup::Window, .iconKey = "unlock"},
    {.id = ids::kFitImagesToWindow, .text = "Fit Images To Window", .group = ActionGroup::Window, .iconKey = "zoom_fit"},
    {.id = ids::kGroupAllImages, .text = "Group All Images", .group = ActionGroup::Window, .iconKey = "tab_group"},
    {.id = ids::kSplitAllImages, .text = "Split All Images", .group = ActionGroup::Window, .iconKey = "grid_3"},
    {.id = ids::kUnsharpMask, .text = "Unsharp Mask", .group = ActionGroup::Process},
    {.id = ids::kNoiseGenerator, .text = "Noise Generator", .group = ActionGroup::Process},
    {.id = ids::kTotalVariationDenoiseFilter, .text = "Total Variation Denoise Filter", .group = ActionGroup::Process},
    {.id = ids::kImageBlend, .text = "Image Blend", .group = ActionGroup::Process},
    {.id = ids::kImageCompare, .text = "Image Compare", .group = ActionGroup::Process},
    {.id = ids::kSplitCFA, .text = "Split CFA", .group = ActionGroup::Process},
    {.id = ids::kMergeCFA, .text = "Merge CFA", .group = ActionGroup::Process},
    {.id = ids::kGeneralizedHyperbolicTransformation, .text = "Generalized Hyperbolic Transformation", .group = ActionGroup::Process},
    {.id = ids::kColorSaturation, .text = "Color Saturation", .group = ActionGroup::Process},
    {.id = ids::kBilateralFilter, .text = "Bilateral Filter", .group = ActionGroup::Process},
    {.id = ids::kLogViewer, .text = "Session Console", .group = ActionGroup::View, .checkable = true},
    {.id = ids::kExtractFrame, .text = "Extract Frame", .group = ActionGroup::Image, .iconKey = "film"},
    {.id = ids::kExtractSequence, .text = "Extract Sequence", .group = ActionGroup::Image, .iconKey = "movie"},
    {.id = ids::kNewSequence, .text = "New Sequence...", .group = ActionGroup::File, .iconKey = "movie"},
    {.id = ids::kConvertToSER, .text = "Convert to SER", .group = ActionGroup::Image},
    {.id = ids::kConvertToImageList, .text = "Convert to Image List", .group = ActionGroup::Image},
    {.id = ids::kConvertToFITS, .text = "Convert to FITS", .group = ActionGroup::Image},
    {.id = ids::kCFAIntegerResample, .text = "CFA Integer Resample", .group = ActionGroup::Process, .iconKey = "grid_2"},
    {.id = ids::kMaximizeView, .text = "Maximize View", .group = ActionGroup::Image, .iconKey = "fullscreen"},
    {.id = ids::kPixelMath2, .text = "Pixel Math", .group = ActionGroup::Process},
    {.id = ids::kBinarize, .text = "Binarize", .group = ActionGroup::Process},
    {.id = ids::kImageCalibration, .text = "Image Calibration", .group = ActionGroup::Process},
    {.id = ids::kImageRegistration, .text = "Image Registration", .group = ActionGroup::Process},
    {.id = ids::kImageStacking, .text = "Image Stacking", .group = ActionGroup::Process},
    {.id = ids::kImageIntegration, .text = "Image Integration", .group = ActionGroup::Process},
    {.id = ids::kImageIntegrationLite, .text = "Image Integration Lite", .group = ActionGroup::Process},
    {.id = ids::kStatisticalStretch, .text = "Statistical Stretch", .group = ActionGroup::Process},
    {.id = ids::kLRGBCombination, .text = "LRGB Combination", .group = ActionGroup::Process},
    {.id = ids::kNBRGBCombination, .text = "NBRGB Combination", .group = ActionGroup::Process},
    {.id = ids::kNBColorMapper, .text = "NB Color Mapper", .group = ActionGroup::Process},
    {.id = ids::kNarrowbandPaletteComposer, .text = "Narrowband Palette Composer", .group = ActionGroup::Process},
    {.id = ids::kSaveFile, .text = "Save File...", .group = ActionGroup::Image, .iconKey = "file_save"},
    {.id = ids::kImageListAnalyzer, .text = "Image List Analyzer", .group = ActionGroup::Process},
    {.id = ids::kExport, .text = "Export...", .group = ActionGroup::Image, .iconKey = "movie"},
    {.id = ids::kColorize, .text = "Colorize", .group = ActionGroup::Process},
    {.id = ids::kSettings, .text = "Settings...", .group = ActionGroup::Edit, .iconKey = "settings", .shortcut = "Ctrl+,"},
    {.id = ids::kManageExtensions, .text = "Manage Extensions...", .group = ActionGroup::Extensions, .iconKey = "extension", .toolTip = "Install, update, remove, and configure extensions.", .statusTip = "Install, update, remove, and configure extensions.", .whatsThis = "Install, update, remove, and configure extensions."},
};
// clang-format on

/// An id and the row of the table it names.
struct IdRow
{
    std::string_view id;
    std::size_t row = 0u;
};

/// The table's rows in the order of their ids, made when the library is compiled.
constexpr std::array<IdRow, std::size(kSpecs)> sortedRows()
{
    std::array<IdRow, std::size(kSpecs)> rows{};
    for (std::size_t row = 0u; row < std::size(kSpecs); ++row)
    {
        rows[row] = IdRow{kSpecs[row].id, row};
    }
    std::sort(rows.begin(), rows.end(), [](const IdRow& left, const IdRow& right) { return left.id < right.id; });
    return rows;
}

constexpr std::array<IdRow, std::size(kSpecs)> kRowsById = sortedRows();

/// The row of the table an id names; none when it names no row.
std::optional<std::size_t> rowOf(const std::string_view id) noexcept
{
    const auto found =
        std::lower_bound(kRowsById.begin(), kRowsById.end(), id,
                         [](const IdRow& row, const std::string_view wanted) { return row.id < wanted; });
    if (found == kRowsById.end() || found->id != id)
    {
        return std::nullopt;
    }
    return found->row;
}

/// A text of the table as the application shows it.
QString translated(const char* text)
{
    return text[0] == '\0' ? QString() : QCoreApplication::translate(kTranslationContext, text);
}

} // namespace

ActionCatalogue::ActionCatalogue(QObject& owner) : m_owner(owner)
{
    m_actions.reserve(std::size(kSpecs));
    for (const ActionSpec& spec : kSpecs)
    {
        auto* action = new QAction(&m_owner);
        action->setObjectName(QString::fromLatin1(spec.id.data(), static_cast<qsizetype>(spec.id.size())));
        action->setText(translated(spec.text));
        if (spec.shortcut[0] != '\0')
        {
            action->setShortcut(QKeySequence(translated(spec.shortcut)));
        }
        action->setShortcutContext(spec.shortcutContext);
        if (spec.toolTip[0] != '\0')
        {
            action->setToolTip(translated(spec.toolTip));
        }
        if (spec.statusTip[0] != '\0')
        {
            action->setStatusTip(translated(spec.statusTip));
        }
        if (spec.whatsThis[0] != '\0')
        {
            action->setWhatsThis(translated(spec.whatsThis));
        }
        action->setCheckable(spec.checkable);
        action->setChecked(spec.checked);
        action->setEnabled(spec.enabled);
        m_actions.emplace_back(action);
    }
}

ActionCatalogue::~ActionCatalogue()
{
    // The actions are the owner's to delete: a hosting window that still lives gives them back first, so they go with
    // the owner and not with a window that may outlive the catalogue.
    release();
}

std::span<const ActionSpec> ActionCatalogue::specs() noexcept
{
    return {kSpecs, std::size(kSpecs)};
}

const ActionSpec* ActionCatalogue::specOf(const std::string_view id) noexcept
{
    const std::optional<std::size_t> row = rowOf(id);
    return row.has_value() ? &kSpecs[*row] : nullptr;
}

QAction* ActionCatalogue::action(const std::string_view id) const
{
    const std::optional<std::size_t> row = rowOf(id);
    return row.has_value() ? m_actions[*row].data() : nullptr;
}

std::vector<QAction*> ActionCatalogue::all() const
{
    std::vector<QAction*> actions;
    actions.reserve(m_actions.size());
    for (const QPointer<QAction>& action : m_actions)
    {
        if (!action.isNull())
        {
            actions.push_back(action.data());
        }
    }
    return actions;
}

std::vector<QAction*> ActionCatalogue::inGroup(const ActionGroup group) const
{
    std::vector<QAction*> actions;
    for (std::size_t row = 0u; row < m_actions.size(); ++row)
    {
        if (kSpecs[row].group == group && !m_actions[row].isNull())
        {
            actions.push_back(m_actions[row].data());
        }
    }
    return actions;
}

an::ui::ActionLookup ActionCatalogue::lookup() const
{
    return [this](const std::string_view id) { return action(id); };
}

void ActionCatalogue::host(QObject& window)
{
    m_window = &window;
    for (const QPointer<QAction>& action : m_actions)
    {
        if (!action.isNull())
        {
            action->setParent(&window);
        }
    }
}

void ActionCatalogue::release()
{
    if (m_window.isNull())
    {
        m_window = nullptr;
        return;
    }
    for (const QPointer<QAction>& action : m_actions)
    {
        if (!action.isNull())
        {
            action->setParent(&m_owner);
        }
    }
    m_window = nullptr;
}

QObject* ActionCatalogue::holder() const
{
    return m_window.isNull() ? &m_owner : m_window.data();
}

} // namespace an::ui::shell
