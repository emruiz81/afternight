// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "actions/ActionBindings.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"

#include "app/ImageOperationsCoordinator.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/ProcessRegistry.h"

#include <QAction>
#include <QApplication>
#include <QObject>
#include <QVariant>

#include <utility>

namespace an::ui::shell
{

namespace
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

/// A process action of the catalogue and the registry category its descriptor is registered under.
struct ProcessBinding
{
    std::string_view action;
    const char* category = "";
};

/// Every process action of the catalogue, in the order they are bound.
constexpr ProcessBinding kProcessBindings[] = {
    {ids::kExtractFrame, kCategoryNonInteractive},
    {ids::kExtractSequence, kCategoryMultiImageStacking},
    {ids::kExtractRedChannel, kCategoryNonInteractive},
    {ids::kExtractGreenChannel, kCategoryNonInteractive},
    {ids::kExtractBlueChannel, kCategoryNonInteractive},
    {ids::kExtractRGBChannels, kCategoryNonInteractive},
    {ids::kExtractCIEL, kCategoryNonInteractive},
    {ids::kExtractCIEA, kCategoryNonInteractive},
    {ids::kExtractCIEB, kCategoryNonInteractive},
    {ids::kExtractCIEC, kCategoryNonInteractive},
    {ids::kExtractCIEH, kCategoryNonInteractive},
    {ids::kExtractCIELCH, kCategoryNonInteractive},
    {ids::kExtractCIEAB, kCategoryNonInteractive},
    {ids::kExtractCIELAB, kCategoryNonInteractive},
    {ids::kExtractCIEX, kCategoryNonInteractive},
    {ids::kExtractCIEY, kCategoryNonInteractive},
    {ids::kExtractCIEZ, kCategoryNonInteractive},
    {ids::kExtractCIEXYZ, kCategoryNonInteractive},
    {ids::kExtractHSLHue, kCategoryNonInteractive},
    {ids::kExtractHSLSaturation, kCategoryNonInteractive},
    {ids::kExtractHSLLightness, kCategoryNonInteractive},
    {ids::kExtractHSL, kCategoryNonInteractive},
    {ids::kExtractHSVHue, kCategoryNonInteractive},
    {ids::kExtractHSVSaturation, kCategoryNonInteractive},
    {ids::kExtractHSVValue, kCategoryNonInteractive},
    {ids::kExtractHSV, kCategoryNonInteractive},
    {ids::kExtractHSIHue, kCategoryNonInteractive},
    {ids::kExtractHSISaturation, kCategoryNonInteractive},
    {ids::kExtractHSIIntensity, kCategoryNonInteractive},
    {ids::kExtractHSI, kCategoryNonInteractive},
    {ids::kExtractYCbCrLuminance, kCategoryNonInteractive},
    {ids::kExtractYCbCrBlueChrominance, kCategoryNonInteractive},
    {ids::kExtractYCbCrRedChrominance, kCategoryNonInteractive},
    {ids::kExtractYCbCr, kCategoryNonInteractive},
    {ids::kConvertTo8Bit, kCategoryNonInteractive},
    {ids::kConvertToSigned16Bit, kCategoryNonInteractive},
    {ids::kConvertToUnsigned16Bit, kCategoryNonInteractive},
    {ids::kConvertToSigned32Bit, kCategoryNonInteractive},
    {ids::kConvertToUnsigned32Bit, kCategoryNonInteractive},
    {ids::kConvertTo32BitFloat, kCategoryNonInteractive},
    {ids::kConvertTo64BitFloat, kCategoryNonInteractive},
    {ids::kConvertToSER, kCategoryNonInteractive},
    {ids::kConvertToImageList, kCategoryNonInteractive},
    {ids::kExport, kCategoryNonInteractive},
    {ids::kConvertToFITS, kCategoryNonInteractive},
    {ids::kAdjustBrightnessContrast, kCategoryToneBrightness},
    {ids::kGammaCorrection, kCategoryToneBrightness},
    {ids::kExposure, kCategoryToneBrightness},
    {ids::kAdjustHSL, kCategoryToneBrightness},
    {ids::kAdjustLightnessChromaHue, kCategoryToneBrightness},
    {ids::kArcSinhStretch, kCategoryToneBrightness},
    {ids::kHistogramTransformation, kCategoryToneBrightness},
    {ids::kGeneralizedHyperbolicTransformation, kCategoryTransforms},
    {ids::kCurvesTransformation, kCategoryTransforms},
    {ids::kStarnet, kCategoryStarObject},
    {ids::kStarReduction, kCategoryStarObject},
    {ids::kStarmaskRecombination, kCategoryMultiImageStacking},
    {ids::kProfiledStarmaskRecombination, kCategoryMultiImageStacking},
    {ids::kSCNR, kCategoryColor},
    {ids::kMedianFilter, kCategoryFilters},
    {ids::kDeepSNR, kCategoryDenoising},
    {ids::kNonLocalBayesFilter, kCategoryDenoising},
    {ids::kWaveletDenoiseFilter, kCategoryDenoising},
    {ids::kPatchPCAFilter, kCategoryDenoising},
    {ids::kLocalHistogramEqualization, kCategorySharpening},
    {ids::kLocalContrastEnhancement, kCategorySharpening},
    {ids::kDarkStructureEnhance, kCategorySharpening},
    {ids::kMultiscaleAlphaSharpen, kCategorySharpening},
    {ids::kGaussianBlur, kCategoryFilters},
    {ids::kHighPass, kCategorySharpening},
    {ids::kChannelComposition, kCategoryColor},
    {ids::kDebayer, kCategorySensorCalibration},
    {ids::kSplitCFA, kCategorySensorCalibration},
    {ids::kMergeCFA, kCategorySensorCalibration},
    {ids::kResize, kCategoryGeometry},
    {ids::kIntegerResample, kCategoryGeometry},
    {ids::kCFAIntegerResample, kCategoryGeometry},
    {ids::kCrop, kCategoryGeometry},
    {ids::kRotate, kCategoryGeometry},
    {ids::kMaskDrawing, kCategoryMaskGeneration},
    {ids::kRangeSelection, kCategorySensorCalibration},
    {ids::kSelectMask, kCategoryNonInteractive},
    {ids::kRemoveMask, kCategoryNonInteractive},
    {ids::kStarMask, kCategoryStarObject},
    {ids::kStarnetStarMask, kCategoryStarObject},
    {ids::kCLAHistogramEqualization, kCategorySharpening},
    {ids::kColorSaturation, kCategoryColor},
    {ids::kColorCalibration2, kCategoryColor},
    {ids::kPhotometricColorCalibration, kCategoryColor},
    {ids::kSpectrophotometricColorCalibration, kCategoryColor},
    {ids::kBackgroundNeutralization, kCategoryColor},
    {ids::kBackgroundExtraction, kCategoryBackgroundExtraction},
    {ids::kAutoGradientRemoval, kCategoryBackgroundExtraction},
    {ids::kMultiscaleLinearTransform, kCategoryTransforms},
    {ids::kMorphologicalTransformation, kCategoryFilters},
    {ids::kTightenEdges, kCategorySharpening},
    {ids::kGRAYCstorationFilter, kCategoryDenoising},
    {ids::kCosmeticCorrectionFilter, kCategoryFilters},
    {ids::kBandingReductionFilter, kCategoryFilters},
    {ids::kBilateralFilter, kCategoryFilters},
    {ids::kStarProfilerV1, kCategoryStarObject},
    {ids::kStarProfilerV2, kCategoryStarObject},
    {ids::kStatisticalStretch, kCategoryToneBrightness},
    {ids::kUnsharpMask, kCategorySharpening},
    {ids::kNoiseGenerator, kCategoryFilters},
    {ids::kTotalVariationDenoiseFilter, kCategoryDenoising},
    {ids::kImageCompare, kCategoryMultiImageStacking},
    {ids::kImageBlend, kCategoryMultiImageStacking},
    {ids::kImageListAnalyzer, kCategoryMultiImageStacking},
    {ids::kLRGBCombination, kCategoryColor},
    {ids::kNBRGBCombination, kCategoryColor},
    {ids::kNBColorMapper, kCategoryColor},
    {ids::kNarrowbandPaletteComposer, kCategoryColor},
    {ids::kColorize, kCategoryColor},
    {ids::kImageCalibration, kCategorySensorCalibration},
    {ids::kImageStacking, kCategoryMultiImageStacking},
    {ids::kImageRegistration, kCategoryMultiImageStacking},
    {ids::kImageIntegration, kCategoryMultiImageStacking},
    {ids::kImageIntegrationLite, kCategoryMultiImageStacking},
    {ids::kLinearMatch, kCategoryColor},
    {ids::kPixelMath2, kCategoryTransforms},
    {ids::kBinarize, kCategorySensorCalibration},
};

/// An action of the image operations and the operation it runs.
struct ImageOperationBinding
{
    std::string_view action;
    void (an::ui::ImageOperationsCoordinator::*operation)() const = nullptr;
};

using Operations = an::ui::ImageOperationsCoordinator;

// The operations are the Controls library's: their addresses are known when it is loaded, not when this one is
// compiled.
const ImageOperationBinding kImageOperationBindings[] = {
    {ids::kClose, &Operations::closeFile},
    {ids::kClose2, &Operations::closeFile},
    {ids::kCloseAll, &Operations::closeAllFiles},
    {ids::kCloseAllOthers, &Operations::closeAllOtherFiles},
    {ids::kDuplicate, &Operations::duplicateImage},
    {ids::kRename, &Operations::renameImage},
    {ids::kUndo, &Operations::undo},
    {ids::kRedo, &Operations::redo},
    {ids::kImageHistory, &Operations::imageHistory},
    {ids::kShowInFileBrowser, &Operations::showInFileBrowser},
    {ids::kHorizontalMirror, &Operations::horizontalMirror},
    {ids::kVerticalMirror, &Operations::verticalMirror},
    {ids::kRotate180, &Operations::rotate180Degree},
    {ids::kRotate90Left, &Operations::rotate90DegreeLeft},
    {ids::kRotate90Right, &Operations::rotate90DegreeRight},
    {ids::kNegativeTransformation, &Operations::negativeTransformation},
    {ids::kConvertToGrayscale, &Operations::convertToGrayscale},
    {ids::kConvertToRGB, &Operations::convertToRGB},
    {ids::kZoomIn, &Operations::zoomIn},
    {ids::kZoomOut, &Operations::zoomOut},
    {ids::kZoom100, &Operations::zoom100},
    {ids::kFitToWindow, &Operations::fitToWindow},
    {ids::kMaximizeView, &Operations::switchMaximizeView},
    {ids::kStatistics, &Operations::statistics},
    {ids::kHistogram, &Operations::histogram},
    {ids::kFITSHeader, &Operations::fitsHeaderViewer},
    {ids::kAstrometryInfo, &Operations::imageAstrometryInfo},
    {ids::kImageEXIFViewer, &Operations::exifViewer},
    {ids::kPlateSolve, &Operations::plateSolve},
    {ids::kAnnotateImage, &Operations::annotateImage},
    {ids::kSummary, &Operations::imageSummary},
    {ids::kFitImagesToWindow, &Operations::fitImageAllWindows},
};

/// An action of the mask overlay and what it sets.
struct MaskBinding
{
    std::string_view action;
    void (an::ui::MaskOverlayCoordinator::*handler)(bool) const = nullptr;
};

using Masks = an::ui::MaskOverlayCoordinator;

const MaskBinding kMaskBindings[] = {
    {ids::kInvertMask, &Masks::onInvertMaskClicked},
    {ids::kEnableMask, &Masks::onEnableMaskClicked},
    {ids::kShowMask, &Masks::onShowMaskClicked},
    {ids::kOverlayRed, &Masks::onOverlayRedClicked},
    {ids::kOverlayGreen, &Masks::onOverlayGreenClicked},
    {ids::kOverlayBlue, &Masks::onOverlayBlueClicked},
    {ids::kOverlayYellow, &Masks::onOverlayYellowClicked},
    {ids::kOverlayCyan, &Masks::onOverlayCyanClicked},
    {ids::kOverlayMagenta, &Masks::onOverlayMagentaClicked},
    {ids::kMultiply, &Masks::onOverlayMultiplyClicked},
    {ids::kReplace, &Masks::onOverlayReplaceClicked},
};

/// Calls a target when it is given.
template <typename CallableT, typename... ArgumentsT> void call(const CallableT& target, ArgumentsT&&... arguments)
{
    if (target)
    {
        target(std::forward<ArgumentsT>(arguments)...);
    }
}

} // namespace

ActionBindings::ActionBindings(ActionCatalogue& catalogue, QObject& signalContext)
    : m_catalogue(catalogue), m_signalContext(signalContext)
{
}

void ActionBindings::setTargets(Targets targets)
{
    m_targets = std::move(targets);
}

void ActionBindings::markRegistryActionPending(QAction* action)
{
    if (!action)
    {
        return;
    }

    const QString pendingReason = QStringLiteral("This extension action is still loading and cannot be launched yet.");
    action->setEnabled(false);
    action->setToolTip(pendingReason);
    action->setStatusTip(pendingReason);
    action->setWhatsThis(pendingReason);
}

void ActionBindings::bindRegistryAction(QAction* action, const std::string_view category,
                                        const std::string_view descriptorId) const
{
    if (!action)
    {
        return;
    }
    const bool alreadyBound = action->property("anRegistryBound").toBool();

    using namespace an::ui::process_registry;

    std::string processId;
    if (!descriptorId.empty())
    {
        processId = std::string(descriptorId);
    }
    else
    {
        const QString propertyDescriptorId = action->property("anRegistryDescriptorId").toString().trimmed();
        if (!propertyDescriptorId.isEmpty())
        {
            processId = propertyDescriptorId.toStdString();
        }
    }

    if (processId.empty())
    {
        processId = category.empty() ? "process" : std::string(category);
        processId.push_back('.');
        processId += action->objectName().toStdString();
    }

    auto& registry = ProcessRegistry::instance();
    ProcessDescriptor* descriptor = registry.findMutable(processId);
    if (!descriptor)
    {
        markRegistryActionPending(action);

        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Registry descriptor not registered for action='{}', id='{}'",
                        action->objectName().toStdString(), processId);
        }
        return;
    }

    QString displayName = action->text();
    displayName.remove('&');
    if (!displayName.isEmpty())
    {
        const std::string previousDisplayName = descriptor->display_name;
        const std::string previousProcessName = descriptor->process_name;
        descriptor->display_name = displayName.toStdString();

        if (previousProcessName.empty() || previousProcessName == descriptor->action_object_name ||
            previousProcessName == previousDisplayName)
        {
            descriptor->process_name = descriptor->display_name;
        }
    }

    // An extension's process whose environment is still to be prepared stays launchable: its launch prepares it.
    const bool available = descriptor->is_available() || preparesBeforeLaunch(*descriptor);
    action->setEnabled(available);

    if (!descriptor->availability_reason.empty())
    {
        const QString reason = QString::fromUtf8(descriptor->availability_reason.c_str());
        action->setToolTip(reason);
        action->setStatusTip(reason);
        action->setWhatsThis(reason);
    }
    else
    {
        action->setToolTip({});
        action->setStatusTip({});
        action->setWhatsThis({});
    }

    if (alreadyBound)
    {
        return;
    }

    QObject::connect(action, &QAction::triggered, &m_signalContext,
                     [this, id = std::move(processId)]() { launch(id); });

    action->setProperty("anRegistryBound", true);
}

void ActionBindings::launch(const std::string& descriptorId) const
{
    if (!m_targets.launch || !m_targets.launch(descriptorId))
    {
        if (an::platform::isLoggingInitialized())
        {
            LOG_WARNING("Registry launch failed for descriptor id={}", descriptorId);
        }
    }
}

void ActionBindings::connectActions()
{
    if (m_actionsConnected)
    {
        return;
    }
    m_actionsConnected = true;

    const auto connect = [this](const std::string_view id, auto handler) {
        if (QAction* const action = m_catalogue.action(id))
        {
            QObject::connect(action, &QAction::triggered, &m_signalContext, std::move(handler));
        }
    };

    // The files.
    connect(ids::kOpen, [this]() { call(m_targets.openFile); });
    connect(ids::kNewSequence, [this]() { call(m_targets.newSequence); });
    connect(ids::kSaveAs, [this]() { call(m_targets.saveFile); });
    connect(ids::kSaveFile, [this]() { call(m_targets.saveFile); });
    connect(ids::kExit, [this]() { call(m_targets.exit); });

    // The image operations.
    for (const ImageOperationBinding& binding : kImageOperationBindings)
    {
        connect(binding.action, [this, operation = binding.operation]() {
            if (m_targets.imageOperations)
            {
                if (const auto* operations = m_targets.imageOperations())
                {
                    (operations->*operation)();
                }
            }
        });
    }

    // The mask overlay.
    for (const MaskBinding& binding : kMaskBindings)
    {
        connect(binding.action, [this, handler = binding.handler](const bool checked) {
            if (m_targets.maskOverlay)
            {
                if (const auto* masks = m_targets.maskOverlay())
                {
                    (masks->*handler)(checked);
                }
            }
        });
    }

    // The Session Console, help, the settings, the extensions and the window's views.
    connect(ids::kLogViewer, [this](const bool checked) { call(m_targets.sessionConsole, checked); });
    connect(ids::kUserGuide, [this]() { call(m_targets.userGuide); });
    connect(ids::kAboutQt, []() { QApplication::aboutQt(); });
    connect(ids::kAboutAfterNight, [this]() { call(m_targets.about); });
    connect(ids::kSystemInformation, [this]() { call(m_targets.systemInformation); });
    connect(ids::kSettings, [this]() { call(m_targets.settings); });
    connect(ids::kManageExtensions, [this]() { call(m_targets.extensionManager); });
    connect(ids::kLockLayout, [this]() { call(m_targets.lockLayout); });
    connect(ids::kUnlockLayout, [this]() { call(m_targets.unlockLayout); });
    connect(ids::kGroupAllImages, [this]() { call(m_targets.groupAll); });
    connect(ids::kSplitAllImages, [this]() { call(m_targets.splitAll); });
}

void ActionBindings::connectProcessActions()
{
    if (m_processActionsConnected)
    {
        return;
    }
    m_processActionsConnected = true;

    for (const ProcessBinding& binding : kProcessBindings)
    {
        bindRegistryAction(m_catalogue.action(binding.action), binding.category);
    }
}

void ActionBindings::setActionIcons(const std::function<QIcon(const QString& key)>& icons) const
{
    if (!icons)
    {
        return;
    }
    for (const ActionSpec& spec : ActionCatalogue::specs())
    {
        QAction* const action = m_catalogue.action(spec.id);
        if (action == nullptr)
        {
            continue;
        }
        action->setIcon(spec.iconKey[0] != '\0' ? icons(QString::fromLatin1(spec.iconKey)) : QIcon());
    }
}

} // namespace an::ui::shell
