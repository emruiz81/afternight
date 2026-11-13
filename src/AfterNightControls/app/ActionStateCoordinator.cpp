// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ActionStateCoordinator.h"

#include "app/AfterNightAppUiBuilder.h"
#include "app/ProcessRegistry.h"

#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QAction>
#include <QMenu>

#include <vector>

namespace
{

std::string registryDescriptorId(const QAction* action)
{
    if (!action)
    {
        return {};
    }

    const QString descriptor_id = action->property("anRegistryDescriptorId").toString().trimmed();
    return descriptor_id.toStdString();
}

void collectRegistryDescriptorActions(QMenu* menu, std::vector<QAction*>& actions)
{
    if (!menu)
    {
        return;
    }

    for (QAction* action : menu->actions())
    {
        if (!registryDescriptorId(action).empty())
        {
            actions.push_back(action);
        }

        collectRegistryDescriptorActions(action ? action->menu() : nullptr, actions);
    }
}

void applyRegistryActionState(QAction* action, const an::ui::process_registry::LaunchGuardResult& result)
{
    if (!action)
    {
        return;
    }

    action->setEnabled(result.ok());
    if (result.ok())
    {
        action->setToolTip({});
        action->setStatusTip({});
        action->setWhatsThis({});
        return;
    }

    const QString message = QString::fromUtf8(result.message.c_str());
    action->setToolTip(message);
    action->setStatusTip(message);
    action->setWhatsThis(message);
}

/// Whether an image's channels are colour, which the colour actions need.
bool isColourImage(const an::ImageDescriptor& descriptor)
{
    return descriptor.channelSchema() == an::ChannelSchema::Rgb ||
           descriptor.channelSchema() == an::ChannelSchema::Rgba;
}

/// The "Convert to" action of a representation; null for none.
QAction* convertActionFor(const an::ui::app::AfterNightAppUiElements& ui, const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::UInt8:
    case an::SampleType::Int8:
        return ui.actionConvert_to_8_bit;
    case an::SampleType::Int16:
        return ui.actionConvert_to_signed_16_bit;
    case an::SampleType::UInt16:
        return ui.actionConvert_to_unsigned_16_bit;
    case an::SampleType::Int32:
        return ui.actionConvert_to_signed_32_bit;
    case an::SampleType::UInt32:
        return ui.actionConvert_to_unsigned_32_bit;
    case an::SampleType::Float32:
        return ui.actionConvert_to_32_bit_float;
    case an::SampleType::Float64:
        return ui.actionConvert_to_64_bit_float;
    }
    return nullptr;
}

} // anonymous namespace

namespace an::ui
{

ActionStateCoordinator::ActionStateCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void ActionStateCoordinator::setEnableImageActions(const bool enable) const
{
    if (!m_dependencies.ui)
    {
        return;
    }

    m_dependencies.ui->actionSave_as->setEnabled(enable);
    m_dependencies.ui->actionClose->setEnabled(enable);
    m_dependencies.ui->actionClose_All->setEnabled(enable);
    m_dependencies.ui->actionClose_All_Others->setEnabled(enable);

    m_dependencies.ui->menuActions->setEnabled(enable);
    m_dependencies.ui->menuView->setEnabled(enable);

    m_dependencies.ui->menuMask->setEnabled(enable);

    m_dependencies.ui->menuConvert_Format->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_8_bit->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_signed_16_bit->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_unsigned_16_bit->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_signed_32_bit->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_unsigned_32_bit->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_32_bit_float->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_64_bit_float->setEnabled(enable);
    m_dependencies.ui->menuGeometry->setEnabled(enable);
    m_dependencies.ui->actionHorizontal_Mirror->setEnabled(enable);
    m_dependencies.ui->actionVertical_Mirror->setEnabled(enable);

    m_dependencies.ui->menuInformation->setEnabled(enable);
    m_dependencies.ui->menuAdjustments->setEnabled(enable);
    m_dependencies.ui->menuIntensity_Transformations->setEnabled(enable);
    m_dependencies.ui->menuStar_Processing->setEnabled(enable);
    m_dependencies.ui->menuNoise_Reduction->setEnabled(enable);
    m_dependencies.ui->menuColor_Spaces->setEnabled(true);
    m_dependencies.ui->menuGeometry_2->setEnabled(enable);
    m_dependencies.ui->menuBackground_Modelization->setEnabled(enable);
    m_dependencies.ui->menuDetails->setEnabled(enable);
    m_dependencies.ui->menuFilters->setEnabled(enable);
    m_dependencies.ui->menuMorphology->setEnabled(enable);

    m_dependencies.ui->menuColor_Calibration->setEnabled(enable);

    m_dependencies.ui->actionStatistics->setEnabled(enable);
    m_dependencies.ui->actionHistogram->setEnabled(enable);

    m_dependencies.ui->menuMask_Generation->setEnabled(enable);
    m_dependencies.ui->menuSequence->setEnabled(enable);

    m_dependencies.ui->menuPixel_Math->setEnabled(enable);

    m_dependencies.ui->actionImage_Blend->setEnabled(enable);
    m_dependencies.ui->actionImage_Compare->setEnabled(enable);
    m_dependencies.ui->actionNoise_Generator->setEnabled(enable);
    m_dependencies.ui->actionImage_List_Analyzer->setEnabled(true);

    m_dependencies.ui->menuMultiscale->setEnabled(enable);

    m_dependencies.ui->actionFit_Images_To_Window->setEnabled(enable);

    m_dependencies.ui->actionGroup_All_Images->setEnabled(enable);
    m_dependencies.ui->actionSplit_All_Images->setEnabled(enable);

    if (!enable)
    {
        m_dependencies.ui->actionFITSHeader->setEnabled(enable);
        m_dependencies.ui->actionAstrometryInfo->setEnabled(enable);
        m_dependencies.ui->actionImage_EXIFViewer->setEnabled(enable);
        m_dependencies.ui->actionPlateSolve->setEnabled(enable);
        m_dependencies.ui->actionAnnotateImage->setEnabled(enable);
        m_dependencies.ui->actionSummary->setEnabled(enable);
        m_dependencies.ui->actionShow_In_File_Browser->setEnabled(enable);
    }

    setEnableImageColorActions(enable);
}

void ActionStateCoordinator::setEnableImageColorActions(const bool enable) const
{
    if (!m_dependencies.ui)
    {
        return;
    }

    m_dependencies.ui->menuExtract_Channel->setEnabled(enable);

    m_dependencies.ui->actionAdjustHSL->setEnabled(enable);
    m_dependencies.ui->actionAdjustLightness_Chroma_Hue->setEnabled(enable);
    m_dependencies.ui->actionConvert_to_RGB->setEnabled(!enable);
    m_dependencies.ui->actionConvert_to_Grayscale->setEnabled(enable);

    m_dependencies.ui->actionColor_Calibration_2->setEnabled(enable);
    m_dependencies.ui->actionPhotometric_Color_Calibration->setEnabled(enable);
    m_dependencies.ui->actionSpectrophotometric_Color_Calibration->setEnabled(enable);
    m_dependencies.ui->actionBackground_Neutralization->setEnabled(enable);

    m_dependencies.ui->actionColor_Saturation->setEnabled(enable);
    m_dependencies.ui->actionSCNR->setEnabled(enable);

    m_dependencies.ui->actionLRGB_Combination->setEnabled(enable);
    m_dependencies.ui->actionNBRGB_Combination->setEnabled(enable);
    m_dependencies.ui->actionNarrowbandPaletteComposer->setEnabled(true);
}

void ActionStateCoordinator::updateRegistryProcessActionStates(ImageViewControl* control) const
{
    if (!m_dependencies.ui || !m_dependencies.ui->menuProcess)
    {
        return;
    }

    std::vector<QAction*> actions;
    collectRegistryDescriptorActions(m_dependencies.ui->menuProcess, actions);
    for (QAction* action : actions)
    {
        const std::string descriptor_id = registryDescriptorId(action);
        const auto* descriptor = process_registry::ProcessRegistry::instance().find(descriptor_id);
        if (!descriptor)
        {
            process_registry::LaunchGuardResult result;
            result.reason = process_registry::LaunchGuardFailureReason::DescriptorUnavailable;
            result.message = "This extension action is still loading and cannot be launched yet.";
            applyRegistryActionState(action, result);
            continue;
        }

        auto result = process_registry::evaluateProcessAvailability(*descriptor);
        // An extension's process whose environment is still to be prepared meets the guards its launch meets once the
        // environment is ready: its launch prepares it.
        const bool preparable = !result.ok() && process_registry::preparesBeforeLaunch(*descriptor);
        if (preparable)
        {
            result = process_registry::LaunchGuardResult{};
        }
        if (result.ok() && descriptor->launch_mode == process_registry::ProcessLaunchMode::SingleImage)
        {
            result = process_registry::evaluateSingleImageLaunchGuards(
                *descriptor, process_registry::launchGuardInputFor(*descriptor, control));
        }

        applyRegistryActionState(action, result);
        if (preparable && result.ok())
        {
            // The action says why the environment is not ready yet, which its launch deals with first.
            const QString reason = QString::fromUtf8(descriptor->availability_reason.c_str());
            action->setToolTip(reason);
            action->setStatusTip(reason);
            action->setWhatsThis(reason);
        }
    }
}

void ActionStateCoordinator::updateActiveImageActions() const
{
    if (!m_dependencies.ui || !m_dependencies.imageViewControlManager)
    {
        return;
    }

    auto* control = m_dependencies.imageViewControlManager->getActiveImageViewControl();
    const bool hasActiveControl = control != nullptr;
    // The description is read without a lease, so an edit holding the view's writer never makes the menu wait.
    const std::optional<an::ImageDescriptor> descriptor =
        hasActiveControl ? control->imageDescriptor() : std::optional<an::ImageDescriptor>{};
    const bool hasImage = descriptor.has_value();
    if (shouldDisableImageActions(hasActiveControl, hasImage))
    {
        setEnableImageActions(false);
        updateRegistryProcessActionStates(nullptr);
        return;
    }

    setEnableImageActions(true);
    setEnableImageColorActions(isColourImage(*descriptor));

    if (control->isSERSequence() || control->isVideoSequence())
    {
        m_dependencies.ui->actionConvert_to_signed_16_bit->setEnabled(false);
        m_dependencies.ui->actionConvert_to_signed_32_bit->setEnabled(false);
        m_dependencies.ui->actionConvert_to_unsigned_32_bit->setEnabled(false);
        m_dependencies.ui->actionConvert_to_32_bit_float->setEnabled(false);
        m_dependencies.ui->actionConvert_to_64_bit_float->setEnabled(false);
    }

    if (control->isSequence())
    {
        m_dependencies.ui->menuSequence->setEnabled(true);
        m_dependencies.ui->actionConvert_to_SER->setEnabled(true);
        m_dependencies.ui->actionConvert_to_Image_List->setEnabled(true);
        m_dependencies.ui->actionExport->setEnabled(true);
        m_dependencies.ui->actionConvert_to_FITS->setEnabled(false);

        if (control->isSERSequence())
        {
            m_dependencies.ui->actionConvert_to_SER->setEnabled(false);
        }
        else if (control->isImageListSequence())
        {
            m_dependencies.ui->actionConvert_to_Image_List->setEnabled(false);
        }
    }
    else
    {
        m_dependencies.ui->menuSequence->setEnabled(false);
    }

    // The action that would convert to the representation the image already has is the one disabled.
    if (QAction* current = convertActionFor(*m_dependencies.ui, descriptor->sampleType()))
    {
        current->setEnabled(false);
    }

    m_dependencies.ui->actionStatistics->setEnabled(control->isStatisticsActionEnabled());
    m_dependencies.ui->actionHistogram->setEnabled(control->isHistogramActionEnabled());

    m_dependencies.ui->actionFITSHeader->setEnabled(control->isFitsHeaderActionEnabled());
    m_dependencies.ui->actionAstrometryInfo->setEnabled(true);
    m_dependencies.ui->actionImage_EXIFViewer->setEnabled(control->isExifActionEnabled());
    m_dependencies.ui->actionPlateSolve->setEnabled(control->canPlateSolveCurrentImage());
    m_dependencies.ui->actionAnnotateImage->setEnabled(control->canAnnotateCurrentImage());
    m_dependencies.ui->actionSummary->setEnabled(true);
    // Offered while the image is a file: read from one, saved to one, or a sequence's.
    m_dependencies.ui->actionShow_In_File_Browser->setEnabled(!control->getFileName().empty());

    QTimer::singleShot(0, m_dependencies.timerContext, [this, control]() {
        const bool terminating = m_dependencies.isTerminating ? *m_dependencies.isTerminating : false;
        if (terminating || !m_dependencies.ui || !m_dependencies.imageViewControlManager)
        {
            return;
        }

        auto* activeControl = m_dependencies.imageViewControlManager->getActiveImageViewControl();
        if (!activeControl)
        {
            return;
        }

        if (activeControl->isMaximized())
        {
            m_dependencies.ui->actionMaximize_View->setIcon(
                m_dependencies.iconProvider ? m_dependencies.iconProvider("fullscreen_exit") : QIcon());
            m_dependencies.ui->actionMaximize_View->setText("Restore View");
        }
        else
        {
            m_dependencies.ui->actionMaximize_View->setIcon(
                m_dependencies.iconProvider ? m_dependencies.iconProvider("fullscreen") : QIcon());
            m_dependencies.ui->actionMaximize_View->setText("Maximize View");
        }

        const bool floating = activeControl->isInFloatingWindow();
        m_dependencies.ui->actionMaximize_View->setEnabled(!floating);
    });

    if (m_dependencies.isMaximizedViewActive && m_dependencies.isMaximizedViewActive())
    {
        m_dependencies.ui->actionSplit_All_Images->setEnabled(false);
        m_dependencies.ui->actionGroup_All_Images->setEnabled(false);
    }
    else
    {
        m_dependencies.ui->actionSplit_All_Images->setEnabled(true);
        m_dependencies.ui->actionGroup_All_Images->setEnabled(true);
    }

    m_dependencies.ui->actionSelect_Mask->setEnabled(true);

    if (shouldEnableMaskRenderingMenu(control->isMasked()))
    {
        m_dependencies.ui->actionRemove_Mask->setEnabled(true);
        m_dependencies.ui->actionInvert_Mask->setEnabled(true);
        m_dependencies.ui->actionInvert_Mask->setChecked(control->isMaskInverted());
        m_dependencies.ui->actionEnable_Mask->setEnabled(true);
        m_dependencies.ui->actionEnable_Mask->setChecked(control->isMaskEnabled());
        m_dependencies.ui->actionShow_Mask->setEnabled(true);
        m_dependencies.ui->actionShow_Mask->setChecked(control->isShowMaskEnabled());
        m_dependencies.ui->menuRendering_Mode->setEnabled(true);
        m_dependencies.ui->actionOverlay_Red->setChecked(control->getMaskRenderingMode() ==
                                                         MaskRenderingMode::red_mode);
        m_dependencies.ui->actionOverlay_Green->setChecked(control->getMaskRenderingMode() ==
                                                           MaskRenderingMode::green_mode);
        m_dependencies.ui->actionOverlay_Blue->setChecked(control->getMaskRenderingMode() ==
                                                          MaskRenderingMode::blue_mode);
        m_dependencies.ui->actionOverlay_Yellow->setChecked(control->getMaskRenderingMode() ==
                                                            MaskRenderingMode::yellow_mode);
        m_dependencies.ui->actionOverlay_Cyan->setChecked(control->getMaskRenderingMode() ==
                                                          MaskRenderingMode::cyan_mode);
        m_dependencies.ui->actionOverlay_Magenta->setChecked(control->getMaskRenderingMode() ==
                                                             MaskRenderingMode::magenta_mode);
        m_dependencies.ui->actionReplace->setChecked(control->getMaskRenderingMode() ==
                                                     MaskRenderingMode::replace_mode);
        m_dependencies.ui->actionMultiply->setChecked(control->getMaskRenderingMode() ==
                                                      MaskRenderingMode::multiply_mode);
    }
    else
    {
        m_dependencies.ui->actionRemove_Mask->setEnabled(false);
        m_dependencies.ui->actionInvert_Mask->setChecked(false);
        m_dependencies.ui->actionInvert_Mask->setEnabled(false);
        m_dependencies.ui->actionEnable_Mask->setChecked(false);
        m_dependencies.ui->actionEnable_Mask->setEnabled(false);
        m_dependencies.ui->actionShow_Mask->setChecked(false);
        m_dependencies.ui->actionShow_Mask->setEnabled(false);
        m_dependencies.ui->menuRendering_Mode->setEnabled(false);
    }

    updateRegistryProcessActionStates(control);
}

void ActionStateCoordinator::updateActiveImageHistory() const
{
    if (!m_dependencies.ui || !m_dependencies.imageViewControlManager)
    {
        return;
    }

    auto* control = m_dependencies.imageViewControlManager->getActiveImageViewControl();
    if (!control || !control->viewHistory())
    {
        m_dependencies.ui->actionUndo->setEnabled(false);
        m_dependencies.ui->actionRedo->setEnabled(false);
        m_dependencies.ui->actionImage_History->setEnabled(false);
        return;
    }

    const an::ui::IViewHistory* history = control->viewHistory();
    m_dependencies.ui->actionUndo->setText("Undo");
    m_dependencies.ui->actionImage_History->setEnabled(true);

    if (history->hasUndo())
    {
        m_dependencies.ui->actionUndo->setEnabled(true);
        const auto name = history->previousStepName();
        if (!name.empty())
        {
            const std::string undoLabel = fmt::format("Undo <{}>", name);
            m_dependencies.ui->actionUndo->setText(QString::fromUtf8(undoLabel));
        }
    }
    else
    {
        m_dependencies.ui->actionUndo->setEnabled(false);
    }

    m_dependencies.ui->actionRedo->setText("Redo");
    if (history->hasRedo())
    {
        m_dependencies.ui->actionRedo->setEnabled(true);
        const auto name = history->nextStepName();
        if (!name.empty())
        {
            const std::string redoLabel = fmt::format("Redo <{}>", name);
            m_dependencies.ui->actionRedo->setText(QString::fromUtf8(redoLabel));
        }
    }
    else
    {
        m_dependencies.ui->actionRedo->setEnabled(false);
    }
}

} // namespace an::ui
