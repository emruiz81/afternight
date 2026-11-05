// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/MenuActionManager.h"

#include "app/ImageOperationsCoordinator.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/ProcessRegistry.h"
#include "app/FileManager.h"
#include "app/AfterNightAppUiBuilder.h"

namespace an::ui
{

MenuActionManager::MenuActionManager(const Dependencies& dependencies)
    : m_dependencies(dependencies), m_signalContext(dependencies.signalContext), m_ui(dependencies.ui),
      m_imageSelector(dependencies.imageSelector)
{
}

void MenuActionManager::markRegistryActionPending(QAction* action) const
{
    if (!action)
    {
        return;
    }

    const QString pending_reason = QStringLiteral("This extension action is still loading and cannot be launched yet.");
    action->setEnabled(false);
    action->setToolTip(pending_reason);
    action->setStatusTip(pending_reason);
    action->setWhatsThis(pending_reason);
}

void MenuActionManager::bindRegistryAction(QAction* action, const std::string_view category,
                                           const std::string_view descriptor_id) const
{
    if (!action || !m_signalContext)
    {
        return;
    }
    const bool already_bound = action->property("anRegistryBound").toBool();

    using namespace an::ui::process_registry;

    std::string processId;
    if (!descriptor_id.empty())
    {
        processId = std::string(descriptor_id);
    }
    else
    {
        const QVariant property_descriptor_id = action->property("anRegistryDescriptorId");
        const QString property_descriptor_id_text = property_descriptor_id.toString().trimmed();
        if (!property_descriptor_id_text.isEmpty())
        {
            processId = property_descriptor_id_text.toStdString();
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
        const std::string previous_display_name = descriptor->display_name;
        const std::string previous_process_name = descriptor->process_name;
        descriptor->display_name = displayName.toStdString();

        if (previous_process_name.empty() || previous_process_name == descriptor->action_object_name ||
            previous_process_name == previous_display_name)
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

    if (already_bound)
    {
        return;
    }

    QObject::connect(action, &QAction::triggered, m_signalContext, [this, descriptorId = std::move(processId)]() {
        if (!m_dependencies.tryLaunchRegistryProcessById || !m_dependencies.tryLaunchRegistryProcessById(descriptorId))
        {
            if (an::platform::isLoggingInitialized())
            {
                LOG_WARNING("Registry launch failed for descriptor id={}", descriptorId);
            }
        }
    });

    action->setProperty("anRegistryBound", true);
}

void MenuActionManager::connectActions() const
{
    if (!m_signalContext || !m_ui)
    {
        return;
    }

    if (!shouldConnectMenuActions(m_nonProcessActionsConnected))
    {
        return;
    }

    if (m_imageSelector && m_dependencies.onImageSelectorChanged)
    {
        QObject::connect(m_imageSelector, &QComboBox::currentTextChanged, m_signalContext, [this](const QString& text) {
            if (m_dependencies.onImageSelectorChanged)
            {
                m_dependencies.onImageSelectorChanged(text);
            }
        });
    }

    connectNonProcessActions();
    m_nonProcessActionsConnected = true;
}

void MenuActionManager::connectNonProcessActions() const
{
    if (!m_signalContext)
    {
        return;
    }

    auto connectFileAction = [this](QAction* action, void (FileManager::*handler)() const) {
        QObject::connect(action, &QAction::triggered, m_signalContext, [this, handler]() {
            if (m_dependencies.fileManager)
            {
                if (auto* fileManager = m_dependencies.fileManager())
                {
                    (fileManager->*handler)();
                }
            }
        });
    };

    auto connectImageOp = [this](QAction* action, void (ImageOperationsCoordinator::*handler)() const) {
        QObject::connect(action, &QAction::triggered, m_signalContext, [this, handler]() {
            if (m_dependencies.imageOperations)
            {
                if (auto* ops = m_dependencies.imageOperations())
                {
                    (ops->*handler)();
                }
            }
        });
    };

    auto connectMaskAction = [this](QAction* action, void (MaskOverlayCoordinator::*handler)(bool) const) {
        QObject::connect(action, &QAction::triggered, m_signalContext, [this, handler](bool checked) {
            if (m_dependencies.maskOverlay)
            {
                if (auto* mask = m_dependencies.maskOverlay())
                {
                    (mask->*handler)(checked);
                }
            }
        });
    };

    // File actions
    connectFileAction(m_ui->actionOpen, &FileManager::openFile);
    QObject::connect(m_ui->actionNew_Sequence, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onNewSequenceRequested)
        {
            m_dependencies.onNewSequenceRequested();
        }
    });
    connectFileAction(m_ui->actionSave_as, &FileManager::saveFile);
    connectFileAction(m_ui->actionSave_File, &FileManager::saveFile);

    // Image operations
    connectImageOp(m_ui->actionClose, &ImageOperationsCoordinator::closeFile);
    connectImageOp(m_ui->actionClose_2, &ImageOperationsCoordinator::closeFile);
    connectImageOp(m_ui->actionClose_All, &ImageOperationsCoordinator::closeAllFiles);
    connectImageOp(m_ui->actionClose_All_Others, &ImageOperationsCoordinator::closeAllOtherFiles);
    connectImageOp(m_ui->actionDuplicate, &ImageOperationsCoordinator::duplicateImage);
    connectImageOp(m_ui->actionRename, &ImageOperationsCoordinator::renameImage);
    connectImageOp(m_ui->actionUndo, &ImageOperationsCoordinator::undo);
    connectImageOp(m_ui->actionRedo, &ImageOperationsCoordinator::redo);
    connectImageOp(m_ui->actionImage_History, &ImageOperationsCoordinator::imageHistory);

    connectImageOp(m_ui->actionHorizontal_Mirror, &ImageOperationsCoordinator::horizontalMirror);
    connectImageOp(m_ui->actionVertical_Mirror, &ImageOperationsCoordinator::verticalMirror);
    connectImageOp(m_ui->actionRotate_180, &ImageOperationsCoordinator::rotate180Degree);
    connectImageOp(m_ui->actionRotate_90_Left, &ImageOperationsCoordinator::rotate90DegreeLeft);
    connectImageOp(m_ui->actionRotate_90_Right, &ImageOperationsCoordinator::rotate90DegreeRight);
    connectImageOp(m_ui->actionNegative_Transformation, &ImageOperationsCoordinator::negativeTransformation);
    connectImageOp(m_ui->actionConvert_to_Grayscale, &ImageOperationsCoordinator::convertToGrayscale);
    connectImageOp(m_ui->actionConvert_to_RGB, &ImageOperationsCoordinator::convertToRGB);

    connectImageOp(m_ui->actionZoom_In, &ImageOperationsCoordinator::zoomIn);
    connectImageOp(m_ui->actionZoom_Out, &ImageOperationsCoordinator::zoomOut);
    connectImageOp(m_ui->actionZoom_100, &ImageOperationsCoordinator::zoom100);
    connectImageOp(m_ui->actionFit_To_Window, &ImageOperationsCoordinator::fitToWindow);
    connectImageOp(m_ui->actionMaximize_View, &ImageOperationsCoordinator::switchMaximizeView);

    connectImageOp(m_ui->actionStatistics, &ImageOperationsCoordinator::statistics);
    connectImageOp(m_ui->actionHistogram, &ImageOperationsCoordinator::histogram);
    connectImageOp(m_ui->actionFITSHeader, &ImageOperationsCoordinator::fitsHeaderViewer);
    connectImageOp(m_ui->actionAstrometryInfo, &ImageOperationsCoordinator::imageAstrometryInfo);
    connectImageOp(m_ui->actionImage_EXIFViewer, &ImageOperationsCoordinator::exifViewer);
    connectImageOp(m_ui->actionPlateSolve, &ImageOperationsCoordinator::plateSolve);
    connectImageOp(m_ui->actionSummary, &ImageOperationsCoordinator::imageSummary);
    connectImageOp(m_ui->actionFit_Images_To_Window, &ImageOperationsCoordinator::fitImageAllWindows);

    // Mask actions
    connectMaskAction(m_ui->actionInvert_Mask, &MaskOverlayCoordinator::onInvertMaskClicked);
    connectMaskAction(m_ui->actionEnable_Mask, &MaskOverlayCoordinator::onEnableMaskClicked);
    connectMaskAction(m_ui->actionShow_Mask, &MaskOverlayCoordinator::onShowMaskClicked);
    connectMaskAction(m_ui->actionOverlay_Red, &MaskOverlayCoordinator::onOverlayRedClicked);
    connectMaskAction(m_ui->actionOverlay_Green, &MaskOverlayCoordinator::onOverlayGreenClicked);
    connectMaskAction(m_ui->actionOverlay_Blue, &MaskOverlayCoordinator::onOverlayBlueClicked);
    connectMaskAction(m_ui->actionOverlay_Yellow, &MaskOverlayCoordinator::onOverlayYellowClicked);
    connectMaskAction(m_ui->actionOverlay_Cyan, &MaskOverlayCoordinator::onOverlayCyanClicked);
    connectMaskAction(m_ui->actionOverlay_Magenta, &MaskOverlayCoordinator::onOverlayMagentaClicked);
    connectMaskAction(m_ui->actionMultiply, &MaskOverlayCoordinator::onOverlayMultiplyClicked);
    connectMaskAction(m_ui->actionReplace, &MaskOverlayCoordinator::onOverlayReplaceClicked);

    // Layout and other actions
    QObject::connect(m_ui->actionLog_Viewer, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onLogViewerRequested)
        {
            m_dependencies.onLogViewerRequested();
        }
    });
    QObject::connect(m_ui->actionUser_Guide, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onOpenHelpRequested)
        {
            m_dependencies.onOpenHelpRequested();
        }
    });
    QObject::connect(m_ui->actionAbout_Qt, &QAction::triggered, m_signalContext, []() { QApplication::aboutQt(); });
    QObject::connect(m_ui->actionAbout_AfterNight, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onAboutRequested)
        {
            m_dependencies.onAboutRequested();
        }
    });
    QObject::connect(m_ui->actionSystem_Information, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onSystemInformationRequested)
        {
            m_dependencies.onSystemInformationRequested();
        }
    });
    QObject::connect(m_ui->actionSettings, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onOpenSettingsRequested)
        {
            m_dependencies.onOpenSettingsRequested();
        }
    });
    QObject::connect(m_ui->actionManage_Extensions, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onOpenExtensionManagerRequested)
        {
            m_dependencies.onOpenExtensionManagerRequested();
        }
    });

    QObject::connect(m_ui->actionLock_Layout, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onLockLayoutRequested)
        {
            m_dependencies.onLockLayoutRequested();
        }
    });
    QObject::connect(m_ui->actionUnlock_Layout, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onUnlockLayoutRequested)
        {
            m_dependencies.onUnlockLayoutRequested();
        }
    });
    QObject::connect(m_ui->actionGroup_All_Images, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onGroupAllImagesRequested)
        {
            m_dependencies.onGroupAllImagesRequested();
        }
    });
    QObject::connect(m_ui->actionSplit_All_Images, &QAction::triggered, m_signalContext, [this]() {
        if (m_dependencies.onSplitAllImagesRequested)
        {
            m_dependencies.onSplitAllImagesRequested();
        }
    });
}

void MenuActionManager::connectProcessActions() const
{
    if (!m_signalContext || !m_ui)
    {
        return;
    }

    const bool connectSignals = shouldConnectMenuActions(m_processActionsConnected);
    if (!connectSignals)
    {
        return;
    }

    using an::ui::process_registry::ProcessLaunchMode;

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

    auto connectRegistryProcessAction = [&](QAction* action, const char* category, const char* handler,
                                            const ProcessLaunchMode launchMode = ProcessLaunchMode::SingleImage) {
        (void)handler;
        (void)launchMode;
        bindRegistryAction(action, category ? category : "");
    };

    auto connectRegistryChannelAction = [&](QAction* action, const char* handler) {
        (void)handler;
        bindRegistryAction(action, kCategoryNonInteractive);
    };

    connectRegistryProcessAction(m_ui->actionExtract_Frame, kCategoryNonInteractive, "executeExtractFrame",
                                 ProcessLaunchMode::NonInteractive);
    connectRegistryProcessAction(m_ui->actionExtract_Sequence, kCategoryMultiImageStacking, "extractSequence");

    connectRegistryChannelAction(m_ui->actionExtract_Red_Channel, "extractRedChannel");
    connectRegistryChannelAction(m_ui->actionExtract_Green_Channel, "extractGreenChannel");
    connectRegistryChannelAction(m_ui->actionExtract_Blue_Channel, "extractBlueChannel");
    connectRegistryChannelAction(m_ui->actionExtract_RGB_Channels, "extractRGBChannels");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_L, "extractCIEL");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_a, "extractCIEA");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_b, "extractCIEB");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_c, "extractCIEC");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_h, "extractCIEH");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_L_c_h, "extractCIELCH");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_a_b, "extractCIEAB");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_L_a_b, "extractCIELAB");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_X, "extractCIEX");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_Y, "extractCIEY");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_Z, "extractCIEZ");
    connectRegistryChannelAction(m_ui->actionExtract_CIE_X_Y_Z, "extractCIEXYZ");
    connectRegistryChannelAction(m_ui->actionExtract_HSL_Hue, "extractHSL_H");
    connectRegistryChannelAction(m_ui->actionExtract_HSL_Saturation, "extractHSL_S");
    connectRegistryChannelAction(m_ui->actionExtract_HSL_Lightness, "extractHSL_L");
    connectRegistryChannelAction(m_ui->actionExtract_HSL, "extractHSL");
    connectRegistryChannelAction(m_ui->actionExtract_HSV_Hue, "extractHSV_H");
    connectRegistryChannelAction(m_ui->actionExtract_HSV_Saturation, "extractHSV_S");
    connectRegistryChannelAction(m_ui->actionExtract_HSV_Value, "extractHSV_V");
    connectRegistryChannelAction(m_ui->actionExtract_HSV, "extractHSV");
    connectRegistryChannelAction(m_ui->actionExtract_HSI_Hue, "extractHSI_H");
    connectRegistryChannelAction(m_ui->actionExtract_HSI_Saturation, "extractHSI_S");
    connectRegistryChannelAction(m_ui->actionExtract_HSI_Intensity, "extractHSI_I");
    connectRegistryChannelAction(m_ui->actionExtract_HSI, "extractHSI");
    connectRegistryChannelAction(m_ui->actionExtract_YCbCr_Luminance, "extractYCbCr_Y");
    connectRegistryChannelAction(m_ui->actionExtract_YCbCr_Blue_Chrominance, "extractYCbCr_Cb");
    connectRegistryChannelAction(m_ui->actionExtract_YCbCr_Red_Chrominance, "extractYCbCr_Cr");
    connectRegistryChannelAction(m_ui->actionExtract_YCbCr, "extractYCbCr");
    connectRegistryChannelAction(m_ui->actionConvert_to_8_bit, "convertTo8Bit");
    connectRegistryChannelAction(m_ui->actionConvert_to_signed_16_bit, "convertToSigned16Bit");
    connectRegistryChannelAction(m_ui->actionConvert_to_unsigned_16_bit, "convertToUnsigned16Bit");
    connectRegistryChannelAction(m_ui->actionConvert_to_signed_32_bit, "convertToSigned32Bit");
    connectRegistryChannelAction(m_ui->actionConvert_to_unsigned_32_bit, "convertToUnsigned32Bit");
    connectRegistryChannelAction(m_ui->actionConvert_to_32_bit_float, "convertToFloat32");
    connectRegistryChannelAction(m_ui->actionConvert_to_64_bit_float, "convertToFloat64");

    auto connectRegistryImageOpAction = [&](QAction* action, void (ImageOperationsCoordinator::*handler)() const) {
        (void)handler;
        bindRegistryAction(action, kCategoryNonInteractive);
    };

    connectRegistryImageOpAction(m_ui->actionConvert_to_SER, &ImageOperationsCoordinator::convertToSERSeq);
    connectRegistryImageOpAction(m_ui->actionConvert_to_Image_List, &ImageOperationsCoordinator::convertToImageListSeq);
    connectRegistryImageOpAction(m_ui->actionExport, &ImageOperationsCoordinator::exportSeq);
    connectRegistryImageOpAction(m_ui->actionConvert_to_FITS, &ImageOperationsCoordinator::convertToFITSSeq);

    connectRegistryProcessAction(m_ui->actionAdjustBrightness_Contrast, kCategoryToneBrightness,
                                 "adjustBrightnessAndContrast");
    connectRegistryProcessAction(m_ui->actionGamma_Correction, kCategoryToneBrightness, "gammaCorrection");
    connectRegistryProcessAction(m_ui->actionExposure, kCategoryToneBrightness, "exposureCorrection");
    connectRegistryProcessAction(m_ui->actionAdjustHSL, kCategoryToneBrightness, "adjustHSL");
    connectRegistryProcessAction(m_ui->actionAdjustLightness_Chroma_Hue, kCategoryToneBrightness, "adjustLCh");
    connectRegistryProcessAction(m_ui->actionArcSinh_Stretch, kCategoryToneBrightness, "arcSinhStretch");
    connectRegistryProcessAction(m_ui->actionHistogram_Transformation, kCategoryToneBrightness,
                                 "histogramTransformation");
    connectRegistryProcessAction(m_ui->actionGeneralized_Hyperbolic_Transformation, kCategoryTransforms,
                                 "generalizedHyperbolicTransformation");
    connectRegistryProcessAction(m_ui->actionCurves_Transformation, kCategoryTransforms, "curvesTransformation");
    connectRegistryProcessAction(m_ui->actionStarnet, kCategoryStarObject, "starnet");
    connectRegistryProcessAction(m_ui->actionStar_Reduction, kCategoryStarObject, "executeStarReduction");
    connectRegistryProcessAction(m_ui->actionStarmask_Recombination, kCategoryMultiImageStacking,
                                 "starmaskRecombination", ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionProfiled_Starmask_Recombination, kCategoryMultiImageStacking,
                                 "profiledStarmaskRecombination", ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionSCNR, kCategoryColor, "executeSCNR");
    connectRegistryProcessAction(m_ui->actionMedian_Filter, kCategoryFilters, "executeMedianFilter");
    connectRegistryProcessAction(m_ui->actionDeepSNR, kCategoryDenoising, "executeDeepSNR");
    connectRegistryProcessAction(m_ui->actionNon_Local_Bayes_Filter, kCategoryDenoising, "executeNLBayesFilter");
    connectRegistryProcessAction(m_ui->actionWavelet_Denoise_Filter, kCategoryDenoising, "executeWaveletDenoiseFilter");
    connectRegistryProcessAction(m_ui->actionPatch_PCA_Filter, kCategoryDenoising, "executePatchPCAFilter");
    connectRegistryProcessAction(m_ui->actionLocal_Histogram_Equalization, kCategorySharpening,
                                 "executeLocalHistogramEqualizationFilter");
    connectRegistryProcessAction(m_ui->actionLocal_Contrast_Enhancement, kCategorySharpening,
                                 "executeLocalContrastEnhancementFilter");
    connectRegistryProcessAction(m_ui->actionDark_Structure_Enhance, kCategorySharpening,
                                 "executeDarkStructureEnhance");
    connectRegistryProcessAction(m_ui->actionMultiscale_Alpha_Sharpen, kCategorySharpening,
                                 "executeSharpenAlphaFilter");
    connectRegistryProcessAction(m_ui->actionGaussian_Blur, kCategoryFilters, "executeGaussianBlurFilter");
    connectRegistryProcessAction(m_ui->actionHigh_Pass, kCategorySharpening, "executeHighPassFilter");
    connectRegistryProcessAction(m_ui->actionChannelComposition, kCategoryColor, "executeChannelComposition",
                                 ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionDebayer, kCategorySensorCalibration, "executeImageDebayer");
    connectRegistryProcessAction(m_ui->actionSplit_CFA, kCategorySensorCalibration, "executeSplitCFA");
    connectRegistryProcessAction(m_ui->actionMerge_CFA, kCategorySensorCalibration, "executeMergeCFA",
                                 ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionResize, kCategoryGeometry, "executeResize");
    connectRegistryProcessAction(m_ui->actionIntegerResample, kCategoryGeometry, "executeIntegerResample");
    connectRegistryProcessAction(m_ui->actionCFA_Integer_Resample, kCategoryGeometry, "executeCFAIntegerResample");
    connectRegistryProcessAction(m_ui->actionCrop, kCategoryGeometry, "executeCrop");
    connectRegistryProcessAction(m_ui->actionRotate, kCategoryGeometry, "executeRotate");
    connectRegistryProcessAction(m_ui->actionMask_Drawing, kCategoryMaskGeneration, "executeMaskDrawing");
    connectRegistryProcessAction(m_ui->actionRange_Selection, kCategorySensorCalibration, "executeRangeSelection");
    connectRegistryProcessAction(m_ui->actionSelect_Mask, kCategoryNonInteractive, "executeSelectMask");
    connectRegistryProcessAction(m_ui->actionRemove_Mask, kCategoryNonInteractive, "executeRemoveMask");
    connectRegistryProcessAction(m_ui->actionStar_Mask, kCategoryStarObject, "executeStarMask");
    connectRegistryProcessAction(m_ui->actionStarnet_Star_Mask, kCategoryStarObject, "executeStarnetStarMask");
    connectRegistryProcessAction(m_ui->actionCLA_Histogram_Equalization, kCategorySharpening, "executeCLAHE");
    connectRegistryProcessAction(m_ui->actionColor_Saturation, kCategoryColor, "executeColorSaturation");
    connectRegistryProcessAction(m_ui->actionColor_Calibration_2, kCategoryColor, "executeColorCalibration");
    connectRegistryProcessAction(m_ui->actionPhotometric_Color_Calibration, kCategoryColor,
                                 "executePhotometricColorCalibration");
    connectRegistryProcessAction(m_ui->actionSpectrophotometric_Color_Calibration, kCategoryColor,
                                 "executeSpectrophotometricColorCalibration");
    connectRegistryProcessAction(m_ui->actionBackground_Neutralization, kCategoryColor,
                                 "executeBackgroundNeutralization");
    connectRegistryProcessAction(m_ui->actionBackground_Extraction, kCategoryBackgroundExtraction,
                                 "executeBackgroundExtraction");
    connectRegistryProcessAction(m_ui->actionAuto_Gradient_Removal, kCategoryBackgroundExtraction,
                                 "executeAutoGradientRemoval");
    connectRegistryProcessAction(m_ui->actionMultiscale_Linear_Transform, kCategoryTransforms,
                                 "executeMultiscaleTransform");
    connectRegistryProcessAction(m_ui->actionMorphological_Transformation, kCategoryFilters,
                                 "executeMorphologicalTransformation");
    connectRegistryProcessAction(m_ui->actionTighten_Edges, kCategorySharpening, "executeTightenEdges");
    connectRegistryProcessAction(m_ui->actionGRAYCstoration_Filter, kCategoryDenoising, "executeGRAYCstoration");
    connectRegistryProcessAction(m_ui->actionCosmetic_Correction_Filter, kCategoryFilters, "executeCosmeticCorrection");
    connectRegistryProcessAction(m_ui->actionBanding_Reduction_Filter, kCategoryFilters, "executeBandingReduction");
    connectRegistryProcessAction(m_ui->actionBilateral_Filter, kCategoryFilters, "executeBilateralFilter");
    connectRegistryProcessAction(m_ui->actionStar_Profiler_v1, kCategoryStarObject, "executeStarProfilerV1");
    connectRegistryProcessAction(m_ui->actionStar_Profiler_v2, kCategoryStarObject, "executeStarProfilerV2");
    connectRegistryProcessAction(m_ui->actionStatistical_Stretch, kCategoryToneBrightness, "executeStatisticalStretch");
    connectRegistryProcessAction(m_ui->actionUnsharp_Mask, kCategorySharpening, "executeUnsharpMask");
    connectRegistryProcessAction(m_ui->actionNoise_Generator, kCategoryFilters, "executeNoiseGenerator");
    connectRegistryProcessAction(m_ui->actionTotal_Variation_Denoise_Filter, kCategoryDenoising,
                                 "executeTotalVariation");
    connectRegistryProcessAction(m_ui->actionImage_Compare, kCategoryMultiImageStacking, "executeImageCompare");
    connectRegistryProcessAction(m_ui->actionImage_Blend, kCategoryMultiImageStacking, "executeImageBlend");
    connectRegistryProcessAction(m_ui->actionImage_List_Analyzer, kCategoryMultiImageStacking,
                                 "executeImageListAnalyzer", ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionLRGB_Combination, kCategoryColor, "executeLRGBCombination");
    connectRegistryProcessAction(m_ui->actionNBRGB_Combination, kCategoryColor, "executeNBRGBCombination");
    connectRegistryProcessAction(m_ui->actionNBColorMapper, kCategoryColor, "executeNBColorMapper");
    connectRegistryProcessAction(m_ui->actionNarrowbandPaletteComposer, kCategoryColor,
                                 "executeNarrowbandPaletteComposer", ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionColorize, kCategoryColor, "executeColorize");
    connectRegistryProcessAction(m_ui->actionImage_Calibration, kCategorySensorCalibration, "executeImageCalibration",
                                 ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionImage_Stacking, kCategoryMultiImageStacking, "executeImageStacking",
                                 ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionImage_Registration, kCategoryMultiImageStacking,
                                 "executeImageRegistration", ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionImage_Integration, kCategoryMultiImageStacking, "executeImageIntegration",
                                 ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionImage_Integration_Lite, kCategoryMultiImageStacking,
                                 "executeImageIntegrationLite", ProcessLaunchMode::TopLevel);
    connectRegistryProcessAction(m_ui->actionLinear_Match, kCategoryColor, "executeLinearMatch");
    connectRegistryProcessAction(m_ui->actionPixel_Math_2, kCategoryTransforms, "executePixelMath");
    connectRegistryProcessAction(m_ui->actionBinarize, kCategorySensorCalibration, "executeBinarize");
    m_processActionsConnected = true;
}

void MenuActionManager::setActionIcons() const
{
    if (!m_ui || !m_dependencies.iconManager)
    {
        return;
    }

    const auto iconManager = m_dependencies.iconManager();
    if (!iconManager)
    {
        return;
    }

    m_ui->actionOpen->setIcon(iconManager->getIcon("folder_open"));
    m_ui->actionNew_Sequence->setIcon(iconManager->getIcon("movie"));
    m_ui->actionSave_as->setIcon(iconManager->getIcon("file_save"));
    m_ui->actionClose->setIcon(iconManager->getIcon("close"));
    m_ui->actionClose_All->setIcon(iconManager->getIcon("close"));
    m_ui->actionClose_All_Others->setIcon(iconManager->getIcon("close"));
    m_ui->actionClose_2->setIcon(iconManager->getIcon("close"));
    m_ui->actionExit->setIcon(iconManager->getIcon("exit"));
    m_ui->actionUndo->setIcon(iconManager->getIcon("undo"));
    m_ui->actionRedo->setIcon(iconManager->getIcon("redo"));
    m_ui->actionImage_History->setIcon(iconManager->getIcon("history"));

    m_ui->actionDuplicate->setIcon(iconManager->getIcon("copy"));
    m_ui->actionExtract_Frame->setIcon(iconManager->getIcon("film"));
    m_ui->actionExtract_Sequence->setIcon(iconManager->getIcon("movie"));
    m_ui->actionExport->setIcon(iconManager->getIcon("movie"));
    m_ui->actionRename->setIcon(iconManager->getIcon("edit"));
    m_ui->actionSave_File->setIcon(iconManager->getIcon("file_save"));

    m_ui->actionConvert_to_8_bit->setIcon(iconManager->getIcon(""));
    m_ui->actionConvert_to_unsigned_16_bit->setIcon(iconManager->getIcon(""));
    m_ui->actionConvert_to_32_bit_float->setIcon(iconManager->getIcon(""));
    m_ui->actionConvert_to_64_bit_float->setIcon(iconManager->getIcon(""));

    m_ui->actionExtract_Red_Channel->setIcon(iconManager->getIcon("red_circle"));
    m_ui->actionExtract_Green_Channel->setIcon(iconManager->getIcon("green_circle"));
    m_ui->actionExtract_Blue_Channel->setIcon(iconManager->getIcon("blue_circle"));
    m_ui->actionExtract_RGB_Channels->setIcon(iconManager->getIcon("rgb"));
    m_ui->actionExtract_CIE_L->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_a->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_b->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_a_b->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_L_a_b->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_L_c_h->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_c->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_h->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_X->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_Y->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_Z->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_CIE_X_Y_Z->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_HSL_Hue->setIcon(iconManager->getIcon("hsl"));
    m_ui->actionExtract_HSL_Saturation->setIcon(iconManager->getIcon("hsl"));
    m_ui->actionExtract_HSL_Lightness->setIcon(iconManager->getIcon("hsl"));
    m_ui->actionExtract_HSL->setIcon(iconManager->getIcon("hsl"));
    m_ui->actionExtract_HSV_Hue->setIcon(iconManager->getIcon("hsv"));
    m_ui->actionExtract_HSV_Saturation->setIcon(iconManager->getIcon("hsv"));
    m_ui->actionExtract_HSV_Value->setIcon(iconManager->getIcon("hsv"));
    m_ui->actionExtract_HSV->setIcon(iconManager->getIcon("hsv"));
    m_ui->actionExtract_YCbCr_Luminance->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_YCbCr_Blue_Chrominance->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_YCbCr_Red_Chrominance->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExtract_YCbCr->setIcon(iconManager->getIcon("cielab"));

    m_ui->actionHorizontal_Mirror->setIcon(iconManager->getIcon("swap_horiz"));
    m_ui->actionVertical_Mirror->setIcon(iconManager->getIcon("swap_vert"));
    m_ui->actionRotate_180->setIcon(iconManager->getIcon("rotate_180"));
    m_ui->actionRotate_90_Left->setIcon(iconManager->getIcon("rotate_90_ccw"));
    m_ui->actionRotate_90_Right->setIcon(iconManager->getIcon("rotate_90_cw"));

    m_ui->actionStatistics->setIcon(iconManager->getIcon("stats"));
    m_ui->actionHistogram->setIcon(iconManager->getIcon("histogram"));
    m_ui->actionImage_EXIFViewer->setIcon(iconManager->getIcon("camera"));
    m_ui->actionFITSHeader->setIcon(iconManager->getIcon("header"));
    m_ui->actionAstrometryInfo->setIcon(iconManager->getIcon("summary"));
    m_ui->actionPlateSolve->setIcon(iconManager->getIcon("grid"));
    m_ui->actionSummary->setIcon(iconManager->getIcon("summary"));

    m_ui->actionSelect_Mask->setIcon(iconManager->getIcon(""));
    m_ui->actionRemove_Mask->setIcon(iconManager->getIcon(""));
    m_ui->actionInvert_Mask->setIcon(iconManager->getIcon(""));
    m_ui->actionEnable_Mask->setIcon(iconManager->getIcon(""));
    m_ui->actionShow_Mask->setIcon(iconManager->getIcon(""));
    m_ui->actionOverlay_Red->setIcon(iconManager->getIcon("red_circle"));
    m_ui->actionOverlay_Green->setIcon(iconManager->getIcon("green_circle"));
    m_ui->actionOverlay_Blue->setIcon(iconManager->getIcon("blue_circle"));
    m_ui->actionOverlay_Yellow->setIcon(iconManager->getIcon("yellow_circle"));
    m_ui->actionOverlay_Magenta->setIcon(iconManager->getIcon("magenta_circle"));
    m_ui->actionOverlay_Cyan->setIcon(iconManager->getIcon("cyan_circle"));
    m_ui->actionReplace->setIcon(iconManager->getIcon(""));
    m_ui->actionMultiply->setIcon(iconManager->getIcon(""));

    m_ui->actionZoom_In->setIcon(iconManager->getIcon("zoom_in"));
    m_ui->actionZoom_Out->setIcon(iconManager->getIcon("zoom_out"));
    m_ui->actionZoom_100->setIcon(iconManager->getIcon("zoom_100"));
    m_ui->actionFit_To_Window->setIcon(iconManager->getIcon("zoom_fit"));
    m_ui->actionMaximize_View->setIcon(iconManager->getIcon("fullscreen"));

    m_ui->actionAdjustBrightness_Contrast->setIcon(iconManager->getIcon("brightness"));
    m_ui->actionGamma_Correction->setIcon(iconManager->getIcon("gamma"));
    m_ui->actionAdjustHSL->setIcon(iconManager->getIcon("hsl"));
    m_ui->actionAdjustLightness_Chroma_Hue->setIcon(iconManager->getIcon("cielab"));
    m_ui->actionExposure->setIcon(iconManager->getIcon("exposure"));
    m_ui->actionNegative_Transformation->setIcon(iconManager->getIcon("film"));
    m_ui->actionConvert_to_RGB->setIcon(iconManager->getIcon("rgb"));
    m_ui->actionConvert_to_Grayscale->setIcon(iconManager->getIcon("grayscale"));

    m_ui->actionAbout_Qt->setIcon(iconManager->getIcon(""));
    m_ui->actionAbout_AfterNight->setIcon(iconManager->getIcon(""));
    m_ui->actionUser_Guide->setIcon(iconManager->getIcon("help"));

    m_ui->actionLock_Layout->setIcon(iconManager->getIcon("lock"));
    m_ui->actionUnlock_Layout->setIcon(iconManager->getIcon("unlock"));

    m_ui->actionFit_Images_To_Window->setIcon(iconManager->getIcon("zoom_fit"));

    m_ui->actionGroup_All_Images->setIcon(iconManager->getIcon("tab_group"));
    m_ui->actionSplit_All_Images->setIcon(iconManager->getIcon("grid_3"));

    m_ui->actionCrop->setIcon(iconManager->getIcon("crop"));
    m_ui->actionRotate->setIcon(iconManager->getIcon("crop_rotate"));
    m_ui->actionIntegerResample->setIcon(iconManager->getIcon("grid_2"));
    m_ui->actionCFA_Integer_Resample->setIcon(iconManager->getIcon("grid_2"));
    m_ui->actionResize->setIcon(iconManager->getIcon("zoom_100"));

    m_ui->actionSettings->setIcon(iconManager->getIcon("settings"));
    m_ui->actionManage_Extensions->setIcon(iconManager->getIcon("extension"));
}

} // namespace an::ui
